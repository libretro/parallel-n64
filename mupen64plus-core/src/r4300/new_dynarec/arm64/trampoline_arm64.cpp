#include "trampoline_arm64.h"
#include "../clear_cache.h"

#include <map>
#include <vector>

// #define TRAMPOLINE_DEBUG

#ifdef TRAMPOLINE_DEBUG
#include <dlfcn.h>
#include <string>
#endif

#define MAX_TRAMPOLINE_SIZE 32
#define POINTERS_TO_RESERVE 1024

static char* DataBase;
static char* CurrentData;
static char* LastCommitedData;

class SymbolDesc
{
public:
    SymbolDesc() = default;

#ifdef TRAMPOLINE_DEBUG
    virtual ~SymbolDesc() = default;
    explicit SymbolDesc(void* addr)
    {
        if (!addr)
            return;

        Dl_info info;
        if (0 != dladdr(addr, &info))
        {
            desc_ = std::string{ info.dli_fname } + ": " + info.dli_sname + " + " + std::to_string(((uintptr_t) addr) - ((uintptr_t) info.dli_saddr));
        }
        else
        {
            desc_ = strerror(errno);
        }
    }

private:
    std::string desc_;
#else
    SymbolDesc(void* addr)
    { }
#endif
};

class CachedFuncTrampoline : SymbolDesc
{
public:
    CachedFuncTrampoline() = default;
    CachedFuncTrampoline(void* tr, void* orig)
    : SymbolDesc(orig)
    , trampoline_(tr)
    { }

    void* trampoline() const
    { return trampoline_; }

private:
    void* trampoline_;
};

struct CachedDataTrampoline : SymbolDesc
{
public:
    CachedDataTrampoline() = default;
    CachedDataTrampoline(void** tr, size_t size, void* orig)
    : SymbolDesc(orig)
    , trampoline_(tr)
    , size_(size)
    { }

    void** trampoline() const
    { return trampoline_; }
    size_t size() const
    { return size_; }

private:
    void** trampoline_;
    // This size is merely a suggestion
    size_t size_;
};

static std::map<void*, CachedFuncTrampoline> FuncToTrampolines;
static std::map<void*, CachedDataTrampoline> DataToTrampolines;
static std::vector<void*> TrampolineToFuncs;
static std::vector<void**> VacantDataHoles;

#define ROUND_UP(x, y) (((x) + (y) - 1) & ~((y) - 1))
#define ROUND_DOWN(x,y) ((x) & ~((y) - 1))

using Trampoline = uint32_t*;

// Allocates the trampoline from the data and automatically registers it
static Trampoline alloc_trampoline(void* for_func)
{
    CurrentData -= MAX_TRAMPOLINE_SIZE;
    Trampoline trampoline = (Trampoline) CurrentData;
    FuncToTrampolines[for_func] = CachedFuncTrampoline{ trampoline, for_func };
    TrampolineToFuncs.push_back(for_func);

    return trampoline;
}

// Allocates internal trampoline that registers trampoline on self
static Trampoline alloc_internal_trampoline(void)
{
    CurrentData -= MAX_TRAMPOLINE_SIZE;
    Trampoline trampoline = (Trampoline) CurrentData;
    // don't put internal functions here, we do not expect anyone to lookup for internal stuff
    // FuncToTrampolines[trampoline] = trampoline;
    TrampolineToFuncs.push_back(trampoline);

    return trampoline;
}

static void add_holes(void* ustart, void* uend)
{
    void** start = (void**) ROUND_UP  ((uintptr_t) ustart, sizeof(void*));
    void** end   = (void**) ROUND_DOWN((uintptr_t) uend  , sizeof(void*));
    for (void** cur = start; cur < end; cur++)
    {
        VacantDataHoles.push_back(cur);
    }
}

// Allocates data directly without being smart about offsets etc
static void** data_alloc_or_find(void* data, size_t size = sizeof(void*))
{
    auto res = DataToTrampolines.emplace(data, CachedDataTrampoline{});
    auto it = res.first;
    auto inserted = res.second;
    auto& desc = it->second;

    // If 'emplace' succeeded, it inserted tmp pointer that is not assigned yet
    if (inserted)
    {
        if (VacantDataHoles.empty())
        {
            // If there are no holes, add a few by ways of allocating useless trampoline
            Trampoline tr = alloc_internal_trampoline();
            add_holes(tr, ((char*)tr) + MAX_TRAMPOLINE_SIZE);
        }

        assert(!VacantDataHoles.empty());
        void** hole = VacantDataHoles.back();
        VacantDataHoles.pop_back();
        *hole = data;
        desc = CachedDataTrampoline{ hole, size, data };
    }

    assert(desc.trampoline());
    return desc.trampoline();
}

void trampoline_init(void* base)
{
    DataBase = (char*) base;
    CurrentData = DataBase;
    LastCommitedData = DataBase;
    FuncToTrampolines.clear();
    DataToTrampolines.clear();
    TrampolineToFuncs.clear();
    TrampolineToFuncs.reserve(POINTERS_TO_RESERVE);
    VacantDataHoles.clear();
    VacantDataHoles.reserve(POINTERS_TO_RESERVE);
}

void trampoline_add_data_hint(void* ptr, size_t size)
{
    return (void) data_alloc_or_find(ptr, size);
}

class CodeEmitter
{
public:
    CodeEmitter(Trampoline trampoline)
    : start_(trampoline)
    , current_(trampoline)
    { }

    ~CodeEmitter()
    {
        assert(size_bytes() <= MAX_TRAMPOLINE_SIZE);
        uintptr_t* end = (uintptr_t*) (((char*) start_) + MAX_TRAMPOLINE_SIZE);
        add_holes(current_, end);
    }

    CodeEmitter(const CodeEmitter&) = delete;
    CodeEmitter& operator=(const CodeEmitter&) = delete;

    void far_load(void* func, int rt)
    {        
        intptr_t addr = (intptr_t) func;
        intptr_t out = (intptr_t) current_;
        intptr_t offset = ((addr&(intptr_t)~0xfff)-((intptr_t)out&(intptr_t)~0xfff))>>12;
        if (-0xfffff <= offset && offset <= 0xfffff)
        {
            assert((((intptr_t)out&(intptr_t)~0xfff)+(offset<<12))==(addr&(intptr_t)~0xfff));
            // adrp xRT, func@PAGE
            next() = 0x90000000|((unsigned int)offset&0x3)<<29|(((unsigned int)offset>>2)&0x7ffff)<<5|rt;
            if((addr&(intptr_t)0xfff)!=0)
            {
                // add xRT, xRT, func@PAGEOFF
                next() = 0x91000000|(addr&0xfff)<<10|rt<<5|rt;
            }
        }
        else
        {
            // far jump via loading the data, takes extra space in the trampoline
            void** pfunc = data_alloc_or_find(func);
            intptr_t tiny_off = ((intptr_t) pfunc) - out;
            assert(tiny_off>=-1048576&&tiny_off<1048576);
            // adr xRT, pdata
            next() = 0x10000000|((u_int)tiny_off&0x3)<<29|(((u_int)tiny_off>>2)&0x7ffff)<<5|rt;
            // ldr xRT, [xRT]
            next() = 0xf9400000|rt<<5|rt;
        }
    }

    void br(int rt)
    {
        // br xRT
        next() = 0xd61f0000|rt<<5;
    }

    void far_jump(void* func)
    {
        const int rt = 17;
        far_load(func, rt);
        br(rt);
    }

    void mov_to_w0(int rt)
    {
        next() = 0x2a0003e0 | (rt << 16);
    }

    void put_asm(uint32_t inst)
    {
        next() = inst;
    }

    uintptr_t size_bytes() const
    { 
        return ((uintptr_t) current_) - ((uintptr_t) start_);
    }

private:
    uint32_t& next()
    { return *(current_++); }

    const Trampoline start_;
    Trampoline current_;
};

#ifdef TRAMPOLINE_DEBUG
static void indirect_jump() {}
#endif

trampolines_reg_jump_t trampoline_alloc_reg_jump(void* jump_vaddr_fn)
{
    trampolines_reg_jump_t generated;
    {
#ifdef TRAMPOLINE_DEBUG
        Trampoline indirects = alloc_trampoline((void*) indirect_jump);
#else
        Trampoline indirects = alloc_internal_trampoline();
#endif
        // ldr    x0, [x0, x1, lsl #3]
        // nop
        // ldr    w18, [x29, #264 ]
        // add    w2, w2, w18 
        // str    w2, [x29, #636 ]
        // br     x0
        {
            CodeEmitter emit{ indirects };
            emit.put_asm(0xf8617800);
            emit.put_asm(0xd503201f);
            emit.put_asm(0xb9410bb2);
            emit.put_asm(0x0b120042);
            emit.put_asm(0xb9027fa2);
            emit.put_asm(0xd61f0000);
        }

        generated.indirect_jump_indexed = (uintptr_t) indirects;
        generated.indirect_jump = generated.indirect_jump_indexed + 2 * sizeof(uint32_t);
    }
    for (int i = 0; i < sizeof(generated.jump_vaddr_reg) / sizeof(*generated.jump_vaddr_reg); i++)
    {
        // mov    w0, wI
        // far_jump _jump_vaddr
#ifdef TRAMPOLINE_DEBUG
        Trampoline trampoline = alloc_trampoline((void*) ((uintptr_t) jump_vaddr_fn + i));
#else
        Trampoline trampoline = alloc_internal_trampoline();
#endif
        {
            CodeEmitter emit{ trampoline };
            if (0 != i)
                emit.mov_to_w0(i);

            emit.far_jump(jump_vaddr_fn);
        }

        generated.jump_vaddr_reg[i] = (uintptr_t) trampoline;
    }
    return generated;
}

void* trampoline_jump_alloc_or_find(void* func)
{
    auto res = FuncToTrampolines.emplace(func, CachedFuncTrampoline{});
    auto it = res.first;
    auto& desc = it->second;
    auto inserted = res.second;

    // If 'emplace' succeeded, it inserted the trampoline that was not initialized yet
    if (inserted)
    {
        Trampoline trampoline = alloc_trampoline(func);
        {
            CodeEmitter emit{ trampoline };
            emit.far_jump(func);
        }
        desc = CachedFuncTrampoline{ trampoline, func };
    }

    assert(desc.trampoline());
    return desc.trampoline();
}

trampoline_data_alloc_or_find_return_t trampoline_data_alloc_or_find(void* data)
{
    // Try to find the address that is close enough (4096*4096 distance)
    // It can return minimal s.t. it->first >= data
    auto it = DataToTrampolines.lower_bound(data);
    if (it->first == data)
    {
        return { it->second.trampoline(), 0 };
    }

    uintptr_t vdata = (uintptr_t) data;
    if (DataToTrampolines.begin() != it)
    {
        // we need the previous element that might encompass 'data'
        --it;
        uintptr_t vtrdata = (uintptr_t) it->first;
        // must be positive
        uintptr_t dist = vdata - vtrdata;
#ifdef TRAMPOLINE_DEBUG
        size_t limit = it->second.size();
#else
        constexpr size_t limit = 4096*4096;
#endif
        if (dist < limit)
        {
            return { it->second.trampoline(), (int) dist };
        }
    }

    void** trampoline = data_alloc_or_find(data);
    return { trampoline, 0 };
}

void* trampoline_convert_trampoline_to_func(void* tramp)
{
    uintptr_t val = (uintptr_t) tramp;
    uintptr_t max = (uintptr_t) DataBase;
    uintptr_t min = (uintptr_t) CurrentData;
    if (min < val && val <= max)
    {
        assert(0 == (val % MAX_TRAMPOLINE_SIZE));
        int off = (max - val) / MAX_TRAMPOLINE_SIZE - 1;
        assert(off < TrampolineToFuncs.size());
        return TrampolineToFuncs[off];
    }
    else
    {
        return tramp;
    }
}

void* trampoline_convert_trampoline_to_data(void** tramp)
{
    uintptr_t val = (uintptr_t) tramp;
    uintptr_t max = (uintptr_t) DataBase;
    uintptr_t min = (uintptr_t) CurrentData;
    // This logic may seem weird but normally data is trampolined if adrp is too short
    // So here are 2 options here:
    // short: adrp _pdata@PAGE + add _pdata@PAGEOFF
    // far:   adrp _tramp@PAGE + add _tramp@PAGEOFF + ldr [_tramp]
    // This means if we are not in 'far' case, '_tramp' is '_pdata'
    if (min < val && val <= max)
    {
        return *tramp;
    }
    else
    {
        return (void*) tramp;
    }
}

void trampoline_commit(void)
{
    if (CurrentData != LastCommitedData)
    {
        clear_instruction_cache(CurrentData, LastCommitedData);
        LastCommitedData = CurrentData;
    }
}
