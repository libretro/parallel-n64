#include "trampoline_arm64.h"
#include "../clear_cache.h"

#include <map>
#include <vector>

#define MAX_TRAMPOLINE_SIZE 32

static char* DataBase;
static char* CurrentData;
static char* LastCommitedData;
static std::map<void*, void*> FuncToTrampolines;
static std::vector<void*> TrampolineToFuncs;

using Trampoline = uint32_t*;

// Allocates the trampoline from the data and automatically registers it
static Trampoline alloc_trampoline(void* for_func)
{
    CurrentData -= MAX_TRAMPOLINE_SIZE;
    Trampoline trampoline = (Trampoline) CurrentData;
    FuncToTrampolines[for_func] = trampoline;
    TrampolineToFuncs.push_back(for_func);

    return trampoline;
}

// Allocates internal trampoline that registers trampoline on self
static Trampoline alloc_internal_trampoline(void)
{
    CurrentData -= MAX_TRAMPOLINE_SIZE;
    Trampoline trampoline = (Trampoline) CurrentData;
    FuncToTrampolines[trampoline] = trampoline;
    TrampolineToFuncs.push_back(trampoline);

    return trampoline;
}

void trampoline_init(void* base)
{
    DataBase = (char*) base;
    CurrentData = DataBase;
    LastCommitedData = DataBase;
    FuncToTrampolines.clear();
    TrampolineToFuncs.clear();
    TrampolineToFuncs.reserve(1024);
}

class CodeEmitter
{
public:
    CodeEmitter(Trampoline current) : current_(current) 
    { }

    void far_load(void* func, int rt)
    {        
        intptr_t addr = (intptr_t) func;
        intptr_t out = (intptr_t) current_;
        intptr_t offset=((addr&(intptr_t)~0xfff)-((intptr_t)out&(intptr_t)~0xfff))>>12;
        assert((((intptr_t)out&(intptr_t)~0xfff)+(offset<<12))==(addr&(intptr_t)~0xfff));
        // adrp xRT, func@PAGE
        next() = 0x90000000|((unsigned int)offset&0x3)<<29|(((unsigned int)offset>>2)&0x7ffff)<<5|rt;
        if((addr&(intptr_t)0xfff)!=0)
        {
            // add xRT, xRT, func@PAGEOFF
            next() = 0x91000000|(addr&0xfff)<<10|rt<<5|rt;
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

    uintptr_t size_bytes(Trampoline start)
    { return ((uintptr_t) current_) - ((uintptr_t) start); }

private:
    uint32_t& next()
    { return *(current_++); }

    Trampoline current_;
};

trampolines_reg_jump_t trampoline_alloc_reg_jump(void* jump_vaddr_fn)
{
    trampolines_reg_jump_t generated;
    {
        Trampoline indirects = alloc_internal_trampoline();
        // ldr    x0, [x0, x1, lsl #3]
        // nop
        // ldr    w18, [x29, #264 ]
        // add    w2, w2, w18 
        // str    w2, [x29, #636 ]
        // br     x0
        indirects[0] = 0xf8617800;
        indirects[1] = 0xd503201f;
        indirects[2] = 0xb9410bb2;
        indirects[3] = 0x0b120042;
        indirects[4] = 0xb9027fa2;
        indirects[5] = 0xd61f0000;

        generated.indirect_jump_indexed = (uintptr_t) indirects;
        generated.indirect_jump = generated.indirect_jump_indexed + 2 * sizeof(uint32_t);
    }
    for (int i = 0; i < sizeof(generated.jump_vaddr_reg) / sizeof(*generated.jump_vaddr_reg); i++)
    {
        // mov    w0, wI
        // far_jump _jump_vaddr
        Trampoline trampoline = alloc_internal_trampoline();
        {
            CodeEmitter emit{ trampoline };
            if (0 != i)
                emit.mov_to_w0(i);

            emit.far_jump(jump_vaddr_fn);
            assert(emit.size_bytes(trampoline) <= MAX_TRAMPOLINE_SIZE);
        }

        generated.jump_vaddr_reg[i] = (uintptr_t) trampoline;
    }
    return generated;
}

void* trampoline_jump_alloc_or_find(void* func)
{
    auto it = FuncToTrampolines.find(func);
    if (it == FuncToTrampolines.end())
    {
        Trampoline trampoline = alloc_trampoline(func);
        {
            CodeEmitter emit{ trampoline };
            emit.far_jump(func);
            assert(emit.size_bytes(trampoline) <= MAX_TRAMPOLINE_SIZE);
        }
        return trampoline;
    }
    else
    {
        return it->second;
    }
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

void trampoline_commit(void)
{
    if (CurrentData != LastCommitedData)
    {
        clear_instruction_cache(CurrentData, LastCommitedData);
        LastCommitedData = CurrentData;
    }
}
