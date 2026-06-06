/* Trampoline allocator for the aarch64 dynarec.
 *
 * The JIT cache lives further than +-128MB from the core's code, so
 * emitted 'bl'/'b' instructions cannot reach C functions directly.
 * Jump trampolines (adrp/adr + br sequences) and pointer-sized data
 * slots are carved out of an arena that grows downward from the start
 * of the JIT cache, in fixed MAX_TRAMPOLINE_SIZE slots; leftover bytes
 * inside a slot are recycled as data holes.
 *
 * Plain MSVC C89: the lookup tables are sorted arrays with binary
 * search instead of std::map, grown with realloc. Insertions only
 * happen while the dynarec warms up, lookups dominate afterwards.
 */

#include "trampoline_arm64.h"
#include "../clear_cache.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TRAMPOLINE_SIZE 32
#define POINTERS_TO_RESERVE 1024

static char* DataBase;
static char* CurrentData;
static char* LastCommitedData;

/* func -> jump trampoline, sorted by func */
typedef struct
{
   void* func;
   void* trampoline;
} func_map_entry_t;

/* data pointer -> data slot, sorted by data pointer */
typedef struct
{
   void*  data;
   void** slot;
} data_map_entry_t;

static func_map_entry_t* FuncMap      = NULL;
static size_t            FuncMapLen   = 0;
static size_t            FuncMapCap   = 0;

static data_map_entry_t* DataMap      = NULL;
static size_t            DataMapLen   = 0;
static size_t            DataMapCap   = 0;

/* slot index (counted down from DataBase) -> original function */
static void**            TrampolineToFuncs    = NULL;
static size_t            TrampolineToFuncsLen = 0;
static size_t            TrampolineToFuncsCap = 0;

/* recyclable pointer-sized holes inside already-allocated slots */
static void***           VacantDataHoles    = NULL;
static size_t            VacantDataHolesLen = 0;
static size_t            VacantDataHolesCap = 0;

#define ROUND_UP(x, y) (((x) + (y) - 1) & ~((y) - 1))
#define ROUND_DOWN(x,y) ((x) & ~((y) - 1))

static void* xgrow(void* ptr, size_t* cap, size_t elem_size)
{
   size_t ncap = *cap ? (*cap * 2) : POINTERS_TO_RESERVE;
   void* n = realloc(ptr, ncap * elem_size);
   assert(n);
   *cap = ncap;
   return n;
}

/* Index of the first entry with key >= data (std::map::lower_bound). */
static size_t data_map_lower_bound(void* data)
{
   size_t lo = 0;
   size_t hi = DataMapLen;
   while (lo < hi)
   {
      size_t mid = lo + ((hi - lo) >> 1);
      if ((uintptr_t)DataMap[mid].data < (uintptr_t)data)
         lo = mid + 1;
      else
         hi = mid;
   }
   return lo;
}

static size_t func_map_lower_bound(void* func)
{
   size_t lo = 0;
   size_t hi = FuncMapLen;
   while (lo < hi)
   {
      size_t mid = lo + ((hi - lo) >> 1);
      if ((uintptr_t)FuncMap[mid].func < (uintptr_t)func)
         lo = mid + 1;
      else
         hi = mid;
   }
   return lo;
}

static void trampoline_to_funcs_push(void* func)
{
   if (TrampolineToFuncsLen == TrampolineToFuncsCap)
      TrampolineToFuncs = (void**)xgrow(TrampolineToFuncs,
            &TrampolineToFuncsCap, sizeof(*TrampolineToFuncs));
   TrampolineToFuncs[TrampolineToFuncsLen++] = func;
}

/* Allocates a slot and remembers which function it stands for */
static uint32_t* alloc_trampoline(void* for_func)
{
   uint32_t* trampoline;
   CurrentData -= MAX_TRAMPOLINE_SIZE;
   trampoline = (uint32_t*)CurrentData;
   trampoline_to_funcs_push(for_func);
   return trampoline;
}

/* Allocates an internal slot: lookups resolve it to itself */
static uint32_t* alloc_internal_trampoline(void)
{
   uint32_t* trampoline;
   CurrentData -= MAX_TRAMPOLINE_SIZE;
   trampoline = (uint32_t*)CurrentData;
   trampoline_to_funcs_push(trampoline);
   return trampoline;
}

static void add_holes(void* ustart, void* uend)
{
   void** start = (void**)ROUND_UP  ((uintptr_t)ustart, sizeof(void*));
   void** end   = (void**)ROUND_DOWN((uintptr_t)uend  , sizeof(void*));
   void** cur;
   for (cur = start; cur < end; cur++)
   {
      if (VacantDataHolesLen == VacantDataHolesCap)
         VacantDataHoles = (void***)xgrow(VacantDataHoles,
               &VacantDataHolesCap, sizeof(*VacantDataHoles));
      VacantDataHoles[VacantDataHolesLen++] = cur;
   }
}

/* Returns a near, dereferenceable slot holding 'data', allocating one
 * from the hole pool if it is not registered yet. */
static void** data_alloc_or_find(void* data)
{
   void** hole;
   size_t idx = data_map_lower_bound(data);

   if (idx < DataMapLen && DataMap[idx].data == data)
      return DataMap[idx].slot;

   if (VacantDataHolesLen == 0)
   {
      /* No holes left: allocating an unused slot creates some */
      uint32_t* tr = alloc_internal_trampoline();
      add_holes(tr, ((char*)tr) + MAX_TRAMPOLINE_SIZE);
   }
   assert(VacantDataHolesLen != 0);
   hole = VacantDataHoles[--VacantDataHolesLen];
   *hole = data;

   if (DataMapLen == DataMapCap)
      DataMap = (data_map_entry_t*)xgrow(DataMap, &DataMapCap,
            sizeof(*DataMap));
   memmove(&DataMap[idx + 1], &DataMap[idx],
         (DataMapLen - idx) * sizeof(*DataMap));
   DataMap[idx].data = data;
   DataMap[idx].slot = hole;
   DataMapLen++;

   return hole;
}

void trampoline_init(void* base)
{
   DataBase = (char*)base;
   CurrentData = DataBase;
   LastCommitedData = DataBase;
   FuncMapLen = 0;
   DataMapLen = 0;
   TrampolineToFuncsLen = 0;
   VacantDataHolesLen = 0;
}

void trampoline_add_data_hint(void* ptr, size_t size)
{
   (void)size;
   (void)data_alloc_or_find(ptr);
}

/* --- tiny straight-line emitter ------------------------------------- */

typedef struct
{
   uint32_t* start;
   uint32_t* current;
} code_emitter_t;

static void emitter_begin(code_emitter_t* em, uint32_t* trampoline)
{
   em->start   = trampoline;
   em->current = trampoline;
}

/* Recycle the unused tail of the slot as data holes */
static void emitter_finish(code_emitter_t* em)
{
   char* end = ((char*)em->start) + MAX_TRAMPOLINE_SIZE;
   assert(((uintptr_t)em->current - (uintptr_t)em->start)
         <= MAX_TRAMPOLINE_SIZE);
   add_holes(em->current, end);
}

static void emit_put(code_emitter_t* em, uint32_t inst)
{
   *(em->current)++ = inst;
}

static void emit_far_load(code_emitter_t* em, void* func, int rt)
{
   intptr_t addr = (intptr_t)func;
   intptr_t out = (intptr_t)em->current;
   intptr_t offset = ((addr & (intptr_t)~0xfff)
         - (out & (intptr_t)~0xfff)) >> 12;
   if (-0xfffff <= offset && offset <= 0xfffff)
   {
      assert(((out & (intptr_t)~0xfff) + (offset << 12))
            == (addr & (intptr_t)~0xfff));
      /* adrp xRT, func@PAGE */
      emit_put(em, 0x90000000 | ((unsigned int)offset & 0x3) << 29
            | (((unsigned int)offset >> 2) & 0x7ffff) << 5 | rt);
      if ((addr & (intptr_t)0xfff) != 0)
      {
         /* add xRT, xRT, func@PAGEOFF */
         emit_put(em, 0x91000000 | (addr & 0xfff) << 10 | rt << 5 | rt);
      }
   }
   else
   {
      /* Too far even for adrp: go through a nearby data slot */
      void** pfunc = data_alloc_or_find(func);
      intptr_t tiny_off = ((intptr_t)pfunc) - (intptr_t)em->current;
      assert(tiny_off >= -1048576 && tiny_off < 1048576);
      /* adr xRT, pdata */
      emit_put(em, 0x10000000 | ((uint32_t)tiny_off & 0x3) << 29
            | (((uint32_t)tiny_off >> 2) & 0x7ffff) << 5 | rt);
      /* ldr xRT, [xRT] */
      emit_put(em, 0xf9400000 | rt << 5 | rt);
   }
}

static void emit_far_jump(code_emitter_t* em, void* func)
{
   emit_far_load(em, func, 17);
   /* br x17 */
   emit_put(em, 0xd61f0000 | 17 << 5);
}

trampolines_reg_jump_t trampoline_alloc_reg_jump(void* jump_vaddr_fn)
{
   trampolines_reg_jump_t generated;
   code_emitter_t em;
   size_t i;

   {
      uint32_t* indirects = alloc_internal_trampoline();
      /* ldr    x0, [x0, x1, lsl #3]
       * nop
       * ldr    w16, [x29, #264 ]
       * add    w2, w2, w16
       * str    w2, [x29, #636 ]
       * br     x0
       * (w16 as scratch: x18 is reserved by the OS on Apple platforms
       * and can be clobbered asynchronously - see EXCLUDE_REG.) */
      emitter_begin(&em, indirects);
      emit_put(&em, 0xf8617800);
      emit_put(&em, 0xd503201f);
      emit_put(&em, 0xb9410bb0);
      emit_put(&em, 0x0b100042);
      emit_put(&em, 0xb9027fa2);
      emit_put(&em, 0xd61f0000);
      emitter_finish(&em);

      generated.indirect_jump_indexed = (uintptr_t)indirects;
      generated.indirect_jump = generated.indirect_jump_indexed
            + 2 * sizeof(uint32_t);
   }
   for (i = 0;
        i < sizeof(generated.jump_vaddr_reg)
              / sizeof(*generated.jump_vaddr_reg);
        i++)
   {
      /* mov w0, wI ; far_jump _jump_vaddr */
      uint32_t* trampoline = alloc_internal_trampoline();
      emitter_begin(&em, trampoline);
      if (0 != i)
         emit_put(&em, 0x2a0003e0 | ((uint32_t)i << 16));
      emit_far_jump(&em, jump_vaddr_fn);
      emitter_finish(&em);

      generated.jump_vaddr_reg[i] = (uintptr_t)trampoline;
   }
   return generated;
}

void* trampoline_jump_alloc_or_find(void* func)
{
   uint32_t* trampoline;
   code_emitter_t em;
   size_t idx = func_map_lower_bound(func);

   if (idx < FuncMapLen && FuncMap[idx].func == func)
      return FuncMap[idx].trampoline;

   trampoline = alloc_trampoline(func);
   emitter_begin(&em, trampoline);
   emit_far_jump(&em, func);
   emitter_finish(&em);
#ifndef __APPLE__
   /* On Apple the flush is batched in trampoline_commit(), driven by
    * the W^X unprotect exit. Nothing drives that on other platforms,
    * so flush the freshly written jump code immediately. */
   clear_instruction_cache(trampoline,
         (char*)trampoline + MAX_TRAMPOLINE_SIZE);
#endif

   if (FuncMapLen == FuncMapCap)
      FuncMap = (func_map_entry_t*)xgrow(FuncMap, &FuncMapCap,
            sizeof(*FuncMap));
   memmove(&FuncMap[idx + 1], &FuncMap[idx],
         (FuncMapLen - idx) * sizeof(*FuncMap));
   FuncMap[idx].func = func;
   FuncMap[idx].trampoline = trampoline;
   FuncMapLen++;

   return trampoline;
}

trampoline_data_alloc_or_find_return_t trampoline_data_alloc_or_find(void* data)
{
   trampoline_data_alloc_or_find_return_t ret;
   size_t idx = data_map_lower_bound(data);

   if (idx < DataMapLen && DataMap[idx].data == data)
   {
      ret.base = DataMap[idx].slot;
      ret.off  = 0;
      return ret;
   }

   if (idx > 0)
   {
      /* The previous registered pointer might encompass 'data':
       * registered base + a small positive 'add'-able offset. */
      uintptr_t vdata   = (uintptr_t)data;
      uintptr_t vtrdata = (uintptr_t)DataMap[idx - 1].data;
      uintptr_t dist    = vdata - vtrdata;
      if (dist < (uintptr_t)4096 * 4096)
      {
         ret.base = DataMap[idx - 1].slot;
         ret.off  = (int)dist;
         return ret;
      }
   }

   ret.base = data_alloc_or_find(data);
   ret.off  = 0;
   return ret;
}

void* trampoline_convert_trampoline_to_func(void* tramp)
{
   uintptr_t val = (uintptr_t)tramp;
   uintptr_t max = (uintptr_t)DataBase;
   uintptr_t min = (uintptr_t)CurrentData;
   /* Slots are allocated downward and start at CurrentData, so the
    * newest trampoline sits exactly at 'min' and DataBase itself is
    * never a slot: the valid range is [min, max). */
   if (min <= val && val < max)
   {
      size_t off;
      assert(0 == (val % MAX_TRAMPOLINE_SIZE));
      off = (max - val) / MAX_TRAMPOLINE_SIZE - 1;
      assert(off < TrampolineToFuncsLen);
      return TrampolineToFuncs[off];
   }
   return tramp;
}

void* trampoline_convert_trampoline_to_data(void** tramp)
{
   uintptr_t val = (uintptr_t)tramp;
   uintptr_t max = (uintptr_t)DataBase;
   uintptr_t min = (uintptr_t)CurrentData;
   /* Two emission shapes exist for data access:
    * near: adrp _pdata@PAGE + add _pdata@PAGEOFF
    * far:  adrp _slot@PAGE  + add _slot@PAGEOFF + ldr [_slot]
    * If 'tramp' is not a slot in the arena, it is the data itself. */
   if (min <= val && val < max)
      return *tramp;
   return (void*)tramp;
}

void trampoline_commit(void)
{
   if (CurrentData != LastCommitedData)
   {
      clear_instruction_cache(CurrentData, LastCommitedData);
      LastCommitedData = CurrentData;
   }
}
