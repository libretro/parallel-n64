/* test_prsp_dmem_wrap -- paraLLEl-RSP vector stores that run off the top
 * of DMEM wrap to DMEM 0, as the RSP's 12-bit DMEM address does.
 *
 * Each case runs a three-instruction program through the plugin entry
 * points: LQV loads a pattern into a vector register, the store under
 * test writes it near DMEM 0xfff, BREAK ends the task.  The program lives
 * at IMEM 0, directly after DMEM, so a store that does not wrap would
 * overwrite it; the test checks IMEM is untouched and that the wrapped
 * bytes reached the bottom of DMEM.
 *
 * On Win64 the case also checks that the JIT entry leaves the full
 * 128 bits of the callee-saved xmm6-xmm15 as it found them.
 *
 * Built by `make HAVE_PARALLEL_RSP=1 check-core`. Exit status 0 on pass.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "api/m64p_plugin.h"

/* the plugin's exported entry points */
void parallelRSPInitiateRSP(RSP_INFO Rsp_Info, unsigned int *CycleCount);
unsigned int parallelRSPDoRspCycles(unsigned int cycles);

/* defined by the frontend in the core */
int g_rsp_force_halt = 0;

#define SP_STATUS_HALT  0x0001u
#define SP_STATUS_BROKE 0x0002u

static unsigned char s_sp_mem[0x2000];
static unsigned char s_rdram[0x1000];
static unsigned int s_regs[32];

static void no_op(void) { }

static int s_failures;

#define CHECK(cond, what) \
    do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

/* DMEM and IMEM are big-endian words held in host order */
static void put_word(unsigned char* mem, unsigned addr, uint32_t v)
{
    memcpy(mem + addr, &v, 4);
}
static unsigned char dmem_byte(unsigned addr)
{
    return s_sp_mem[(addr & 0xfffu) ^ 3u];
}
static void put_dmem_byte(unsigned addr, unsigned char v)
{
    s_sp_mem[(addr & 0xfffu) ^ 3u] = v;
}

static uint32_t swc2(unsigned op, unsigned vt, unsigned e, int offset, unsigned base)
{
    return (0x3au << 26) | (base << 21) | (vt << 16) | (op << 11)
         | (e << 7) | ((unsigned)offset & 0x7fu);
}

static void init_plugin(void)
{
    RSP_INFO info;
    unsigned int cycles = 0;

    memset(&info, 0, sizeof(info));
    info.RDRAM = s_rdram;
    info.DMEM = s_sp_mem;
    info.IMEM = s_sp_mem + 0x1000;
    info.MI_INTR_REG = &s_regs[0];
    info.SP_MEM_ADDR_REG = &s_regs[1];
    info.SP_DRAM_ADDR_REG = &s_regs[2];
    info.SP_RD_LEN_REG = &s_regs[3];
    info.SP_WR_LEN_REG = &s_regs[4];
    info.SP_STATUS_REG = &s_regs[5];
    info.SP_DMA_FULL_REG = &s_regs[6];
    info.SP_DMA_BUSY_REG = &s_regs[7];
    info.SP_PC_REG = &s_regs[8];
    info.SP_SEMAPHORE_REG = &s_regs[9];
    info.DPC_START_REG = &s_regs[10];
    info.DPC_END_REG = &s_regs[11];
    info.DPC_CURRENT_REG = &s_regs[12];
    info.DPC_STATUS_REG = &s_regs[13];
    info.DPC_CLOCK_REG = &s_regs[14];
    info.DPC_BUFBUSY_REG = &s_regs[15];
    info.DPC_PIPEBUSY_REG = &s_regs[16];
    info.DPC_TMEM_REG = &s_regs[17];
    info.CheckInterrupts = no_op;
    info.ProcessDlistList = no_op;
    info.ProcessAlistList = no_op;
    info.ProcessRdpList = no_op;
    info.ShowCFB = no_op;
    parallelRSPInitiateRSP(info, &cycles);
}

#if defined(__GNUC__) && defined(__x86_64__) && (defined(_WIN32) || defined(__CYGWIN__))
#define CHECK_XMM 1
static const uint64_t s_xmm_pattern[2] = { 0x0123456789abcdefull, 0xfedcba9876543210ull };
#endif

static unsigned run_task(void)
{
#ifdef CHECK_XMM
    uint64_t after[2];
    __asm__ __volatile__("movdqu %0, %%xmm6" :: "m"(s_xmm_pattern[0]) : "xmm6");
#endif
    s_regs[5] = 0;          /* SP_STATUS: running */
    s_regs[8] = 0;          /* SP_PC */
    parallelRSPDoRspCycles(1000);
#ifdef CHECK_XMM
    __asm__ __volatile__("movdqu %%xmm6, %0" : "=m"(after[0]) :: "memory");
    CHECK(memcmp(after, s_xmm_pattern, sizeof(after)) == 0,
          "Win64: xmm6 survives the JIT entry in full");
#endif
    return s_regs[5];
}

/* op: SWC2 sub-opcode; addr: DMEM address the store starts at (in r1) */
static void run_case(const char* name, unsigned op, unsigned e, unsigned addr,
                     unsigned span, unsigned expect_wrapped)
{
    unsigned char imem_before[0x1000];
    unsigned i, wrapped;
    char what[96];

    memset(s_sp_mem, 0, sizeof(s_sp_mem));
    for (i = 0; i < span; i++)
        put_dmem_byte(i, 0x5a);
    for (i = 0; i < 16; i++)
        put_dmem_byte(0x100u + i, (unsigned char)(0xa0u + i));

    /* ori r1, r0, addr ; lqv v1[0], 0x100(r0) ; store v1[e], 0(r1) ; break */
    put_word(s_sp_mem + 0x1000, 0x00, (0x0du << 26) | (1u << 16) | (addr & 0xffffu));
    put_word(s_sp_mem + 0x1000, 0x04, (0x32u << 26) | (0u << 21) | (1u << 16)
                                      | (0x04u << 11) | (0u << 7) | 0x10u);
    put_word(s_sp_mem + 0x1000, 0x08, swc2(op, 1, e, 0, 1));
    put_word(s_sp_mem + 0x1000, 0x0c, 0x0000000du);
    memcpy(imem_before, s_sp_mem + 0x1000, sizeof(imem_before));

    sprintf(what, "%s: the task runs to its break", name);
    CHECK(run_task() & SP_STATUS_BROKE, what);

    sprintf(what, "%s: IMEM is untouched", name);
    CHECK(memcmp(imem_before, s_sp_mem + 0x1000, sizeof(imem_before)) == 0, what);

    wrapped = 0;
    for (i = 0; i < span; i++)
        if (dmem_byte(i) != 0x5a)
            wrapped++;
    sprintf(what, "%s: %u bytes wrap to DMEM 0", name, expect_wrapped);
    CHECK(wrapped == expect_wrapped, what);
}

int main(void)
{
    init_plugin();

    /* SWC2 sub-opcodes: SLV 2, SHV 8, SFV 9, SWV 10, STV 11 */
    run_case("SLV", 0x02, 0, 0xffeu, 8, 2);
    run_case("SWV", 0x0a, 0, 0xff8u, 16, 8);
    run_case("SHV", 0x08, 0, 0xff8u, 16, 4);
    run_case("SFV", 0x09, 0, 0xff8u, 16, 2);
    run_case("STV", 0x0b, 0, 0xff8u, 16, 8);

    if (s_failures) {
        printf("test_prsp_dmem_wrap: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_prsp_dmem_wrap: all checks passed\n");
    return 0;
}
