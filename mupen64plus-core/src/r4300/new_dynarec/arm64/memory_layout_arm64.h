#ifndef M64P_MEMORY_LAYOUT_ARM64_H
#define M64P_MEMORY_LAYOUT_ARM64_H

#include <stdint.h>
#ifdef __APPLE__
#include <sys/types.h>
#endif

#include "../../recomp_types.h"

typedef struct recompiler_memory_layout
{
    char rml_dynarec_local[256];
    uint32_t rml_next_interrupt;
    int rml_cycle_count;
    int rml_last_count;
    int rml_pending_exception;
    int rml_pcaddr;
    int rml_stop;
    char* rml_invc_ptr;
    uint32_t rml_address;
    uint64_t rml_readmem_dword;
    uint64_t rml_cpu_dword;
    uint32_t rml_cpu_word;
    uint16_t rml_cpu_hword;
    uint8_t rml_cpu_byte;
    uint32_t rml_FCR0;
    uint32_t rml_FCR31;
    int64_t rml_reg[32];
    int64_t rml_hi;
    int64_t rml_lo;
    uint32_t rml_g_cp0_regs[32];
    float* rml_reg_cop1_simple[32];
    double* rml_reg_cop1_double[32];
    u_int rml_rounding_modes[4];
    int rml_branch_target;
    int __reserved0;
    struct precomp_instr *rml_PC;
    struct precomp_instr rml_fake_pc;
    int __reserved1[3];
    uint64_t rml_ram_offset;
    uint64_t rml_mini_ht[32][2];
    u_char rml_restore_candidate[512];
    int rml_instr_addr;
    int64_t rml_link_register;
    uint64_t rml_memory_map[1048576];
} recompiler_memory_layout_t;

extern recompiler_memory_layout_t memory_layout;
#define RECOMPILER_MEMORY (&memory_layout)

// Copied from linkage_aarch64.S declaration
#define RML_SIZE_EXTRA_MEMORY 0
#define RML_SIZE_DYNAREC_LOCAL     RML_SIZE_EXTRA_MEMORY      + 0
#define RML_SIZE_NEXT_INTERRUPT    RML_SIZE_DYNAREC_LOCAL     + 256
#define RML_SIZE_CYCLE_COUNT       RML_SIZE_NEXT_INTERRUPT    + 4
#define RML_SIZE_LAST_COUNT        RML_SIZE_CYCLE_COUNT       + 4
#define RML_SIZE_PENDING_EXCEPTION RML_SIZE_LAST_COUNT        + 4
#define RML_SIZE_PCADDR            RML_SIZE_PENDING_EXCEPTION + 4
#define RML_SIZE_STOP              RML_SIZE_PCADDR            + 4
#define RML_SIZE_INVC_PTR          RML_SIZE_STOP              + 4
#define RML_SIZE_ADDRESS           RML_SIZE_INVC_PTR          + 8
#define RML_SIZE_READMEM_DWORD     RML_SIZE_ADDRESS           + 8
#define RML_SIZE_CPU_DWORD         RML_SIZE_READMEM_DWORD     + 8
#define RML_SIZE_CPU_WORD          RML_SIZE_CPU_DWORD         + 8
#define RML_SIZE_CPU_HWORD         RML_SIZE_CPU_WORD          + 4
#define RML_SIZE_CPU_BYTE          RML_SIZE_CPU_HWORD         + 2
#define RML_SIZE_FCR0              RML_SIZE_CPU_HWORD         + 4
#define RML_SIZE_FCR31             RML_SIZE_FCR0              + 4
#define RML_SIZE_REG               RML_SIZE_FCR31             + 4
#define RML_SIZE_HI                RML_SIZE_REG               + 256
#define RML_SIZE_LO                RML_SIZE_HI                + 8
#define RML_SIZE_G_CP0_REGS        RML_SIZE_LO                + 8
#define RML_SIZE_REG_COP1_SIMPLE   RML_SIZE_G_CP0_REGS        + 128
#define RML_SIZE_REG_COP1_DOUBLE   RML_SIZE_REG_COP1_SIMPLE   + 256
#define RML_SIZE_ROUNDING_MODES    RML_SIZE_REG_COP1_DOUBLE   + 256
#define RML_SIZE_BRANCH_TARGET     RML_SIZE_ROUNDING_MODES    + 16
#define RML_SIZE_PC                RML_SIZE_BRANCH_TARGET     + 8
#define RML_SIZE_FAKE_PC           RML_SIZE_PC                + 8
#define RML_SIZE_RAM_OFFSET        RML_SIZE_FAKE_PC           + 208
#define RML_SIZE_MINI_HT           RML_SIZE_RAM_OFFSET        + 8
#define RML_SIZE_RESTORE_CANDIDATE RML_SIZE_MINI_HT           + 512
#define RML_SIZE_INSTR_ADDR        RML_SIZE_RESTORE_CANDIDATE + 512
#define RML_SIZE_LINK_REGISTER     RML_SIZE_INSTR_ADDR        + 8
#define RML_SIZE_MEMORY_MAP        RML_SIZE_LINK_REGISTER     + 8

#if __STDC_VERSION__ > 201112L
#define RML_CHECK_SIZE(name, off) _Static_assert(offsetof(recompiler_memory_layout_t, name) == (off), "Recompiler layout for '" #name "' does not match asm")
RML_CHECK_SIZE(rml_dynarec_local    , RML_SIZE_DYNAREC_LOCAL);
RML_CHECK_SIZE(rml_next_interrupt   , RML_SIZE_NEXT_INTERRUPT);
RML_CHECK_SIZE(rml_cycle_count      , RML_SIZE_CYCLE_COUNT);
RML_CHECK_SIZE(rml_last_count       , RML_SIZE_LAST_COUNT);
RML_CHECK_SIZE(rml_pending_exception, RML_SIZE_PENDING_EXCEPTION);
RML_CHECK_SIZE(rml_pcaddr           , RML_SIZE_PCADDR);
RML_CHECK_SIZE(rml_stop             , RML_SIZE_STOP);
RML_CHECK_SIZE(rml_invc_ptr         , RML_SIZE_INVC_PTR);
RML_CHECK_SIZE(rml_address          , RML_SIZE_ADDRESS);
RML_CHECK_SIZE(rml_readmem_dword    , RML_SIZE_READMEM_DWORD);
RML_CHECK_SIZE(rml_cpu_dword        , RML_SIZE_CPU_DWORD);
RML_CHECK_SIZE(rml_cpu_word         , RML_SIZE_CPU_WORD);
RML_CHECK_SIZE(rml_cpu_hword        , RML_SIZE_CPU_HWORD);
RML_CHECK_SIZE(rml_cpu_byte         , RML_SIZE_CPU_BYTE);
RML_CHECK_SIZE(rml_FCR0             , RML_SIZE_FCR0);
RML_CHECK_SIZE(rml_FCR31            , RML_SIZE_FCR31);
RML_CHECK_SIZE(rml_reg              , RML_SIZE_REG);
RML_CHECK_SIZE(rml_hi               , RML_SIZE_HI);
RML_CHECK_SIZE(rml_lo               , RML_SIZE_LO);
RML_CHECK_SIZE(rml_g_cp0_regs       , RML_SIZE_G_CP0_REGS);
RML_CHECK_SIZE(rml_reg_cop1_simple  , RML_SIZE_REG_COP1_SIMPLE);
RML_CHECK_SIZE(rml_reg_cop1_double  , RML_SIZE_REG_COP1_DOUBLE);
RML_CHECK_SIZE(rml_rounding_modes   , RML_SIZE_ROUNDING_MODES);
RML_CHECK_SIZE(rml_branch_target    , RML_SIZE_BRANCH_TARGET);
RML_CHECK_SIZE(rml_PC               , RML_SIZE_PC);
RML_CHECK_SIZE(rml_fake_pc          , RML_SIZE_FAKE_PC);
RML_CHECK_SIZE(rml_ram_offset       , RML_SIZE_RAM_OFFSET);
RML_CHECK_SIZE(rml_mini_ht          , RML_SIZE_MINI_HT);
RML_CHECK_SIZE(rml_restore_candidate, RML_SIZE_RESTORE_CANDIDATE);
RML_CHECK_SIZE(rml_instr_addr       , RML_SIZE_INSTR_ADDR);
RML_CHECK_SIZE(rml_link_register    , RML_SIZE_LINK_REGISTER);
RML_CHECK_SIZE(rml_memory_map       , RML_SIZE_MEMORY_MAP);
#undef RML_CHECK_SIZE
#endif

#endif
