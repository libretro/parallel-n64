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

static uint32_t* ptrampoline_alloc()
{
    CurrentData -= MAX_TRAMPOLINE_SIZE;
    return (uint32_t*) CurrentData;
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

void* trampoline_jump_alloc_or_find(void* func)
{
    auto it = FuncToTrampolines.find(func);
    if (it == FuncToTrampolines.end())
    {
        uint32_t* trampoline = ptrampoline_alloc();

        intptr_t addr = (intptr_t) func;
        intptr_t out = (intptr_t) trampoline;
        intptr_t offset=((addr&(intptr_t)~0xfff)-((intptr_t)out&(intptr_t)~0xfff))>>12;
        assert((((intptr_t)out&(intptr_t)~0xfff)+(offset<<12))==(addr&(intptr_t)~0xfff));
        int rt = 17;
        // adrp x17, func@PAGE
        trampoline[0] = 0x90000000|((unsigned int)offset&0x3)<<29|(((unsigned int)offset>>2)&0x7ffff)<<5|rt;
        if((addr&(intptr_t)0xfff)!=0)
        {
            // add x17, x17, func@PAGEOFF
            trampoline[1] = 0x91000000|(addr&0xfff)<<10|rt<<5|rt;
            // br x17
            trampoline[2] = 0xd61f0000|rt<<5;
        }
        else
        {
            // br x17
            trampoline[1] = 0xd61f0000|rt<<5;
        }

        FuncToTrampolines[func] = trampoline;
        TrampolineToFuncs.push_back(func);
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
