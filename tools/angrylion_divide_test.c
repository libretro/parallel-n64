/* angrylion_divide_test -- the HLE emitter's double-precision divide unit
 * returns what the RSP's VRCPH/VRCPL and VRSQH/VRSQL pairs return.
 *
 * Each input runs through the cxd4 interpreter as
 *     vrcph v1[0], v2[e]   ; DivIn high half
 *     vrcpl v1[1], v3[e]   ; DivIn low half, DivOut low half
 *     vrcph v1[0], v0[0]   ; DivOut high half
 * (vrsqh/vrsql for the square root) and the emitter's rsp_rcp32(),
 * rsp_rcp32_dp() and rsp_rsq32() must agree on every value, the sign and
 * normalisation corners included.
 *
 * Built by `make check-angrylion` (top-level Makefile). Exit status 0 on pass.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "api/m64p_plugin.h"
#include "rdp_emit_rsp.h"

void cxd4InitiateRSP(RSP_INFO Rsp_Info, unsigned int *CycleCount);
unsigned int cxd4DoRspCycles(unsigned int cycles);

/* referenced by the interpreter's frontend glue */
int g_rsp_force_halt = 0;
void DebugMessage(int level, const char *message, ...)
{
    (void)level; (void)message;
}

static unsigned char s_sp_mem[0x2000];
static unsigned char s_rdram[0x1000];
static unsigned int s_regs[32];

static void no_op(void) { }

static void put32(unsigned char* mem, unsigned addr, uint32_t v)
{
    memcpy(mem + addr, &v, 4);
}
static void put16(unsigned char* mem, unsigned addr, unsigned v)
{
    mem[(addr ^ 3u)] = (unsigned char)(v >> 8);
    mem[((addr + 1u) ^ 3u)] = (unsigned char)v;
}
static unsigned get16(const unsigned char* mem, unsigned addr)
{
    return ((unsigned)mem[addr ^ 3u] << 8) | mem[(addr + 1u) ^ 3u];
}

static uint32_t cop2_vector(unsigned funct, unsigned e, unsigned vt, unsigned de, unsigned vd)
{
    return (0x12u << 26) | (1u << 25) | (e << 21) | (vt << 16) | (de << 11)
         | (vd << 6) | funct;
}
static uint32_t lsc2(unsigned op, unsigned rt, unsigned sub, unsigned offset)
{
    return (op << 26) | (0u << 21) | (rt << 16) | (sub << 11) | (offset & 0x7fu);
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
    cxd4InitiateRSP(info, &cycles);

    put32(s_sp_mem + 0x1000, 0x00, lsc2(0x32, 2, 0x04, 0));   /* lqv v2, 0x00 */
    put32(s_sp_mem + 0x1000, 0x04, lsc2(0x32, 3, 0x04, 1));   /* lqv v3, 0x10 */
    put32(s_sp_mem + 0x1000, 0x14, lsc2(0x3a, 1, 0x04, 2));   /* sqv v1, 0x20 */
    put32(s_sp_mem + 0x1000, 0x18, 0x0000000du);              /* break */
}

/* VRCPH 0x32 / VRCPL 0x31, VRSQH 0x36 / VRSQL 0x35 */
static int32_t rsp_divide(unsigned high, unsigned low, int32_t in)
{
    put32(s_sp_mem + 0x1000, 0x08, cop2_vector(high, 0, 2, 0, 1));
    put32(s_sp_mem + 0x1000, 0x0c, cop2_vector(low, 0, 3, 1, 1));
    put32(s_sp_mem + 0x1000, 0x10, cop2_vector(high, 0, 0, 0, 1));
    put16(s_sp_mem, 0x00, ((uint32_t)in >> 16) & 0xffffu);
    put16(s_sp_mem, 0x10, (uint32_t)in & 0xffffu);
    s_regs[5] = 0;
    s_regs[8] = 0;
    cxd4DoRspCycles(1000);
    return (int32_t)((get16(s_sp_mem, 0x20) << 16) | get16(s_sp_mem, 0x22));
}

typedef int32_t (*divide_fn)(int32_t);

static unsigned check(const char* name, unsigned high, unsigned low, divide_fn fn)
{
    static const int32_t fixed[] = {
        0, 1, -1, 2, -2, 0x7fff, -0x7fff, 0x8000, -0x8000, -0x8001,
        0x10000, -0x10000, 0x7fffffff, (int32_t)0x80000000u, (int32_t)0x80000001u,
        0x12345678, -0x12345678, 0x00012000, -0x00012000, 0x40000000, -0x40000000
    };
    unsigned i, failures = 0, count = sizeof(fixed) / sizeof(fixed[0]) + 20000u;
    uint32_t x = 0x2545f491u;

    for (i = 0; i < count; i++) {
        int32_t in, want, got;

        if (i < sizeof(fixed) / sizeof(fixed[0])) {
            in = fixed[i];
        } else {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            /* spread the magnitudes over every normalisation shift */
            in = (int32_t)(x >> (x & 31u));
            if (x & 0x80000000u)
                in = (int32_t)(0u - (uint32_t)in);
        }
        want = rsp_divide(high, low, in);
        got = fn(in);
        if (want != got) {
            if (failures < 4)
                printf("FAIL: %s(0x%08x): RSP 0x%08x, emitter 0x%08x\n",
                       name, (unsigned)in, (unsigned)want, (unsigned)got);
            failures++;
        }
    }
    printf("%s: %u of %u inputs differ from the RSP\n", name, failures, count);
    return failures;
}

int main(void)
{
    unsigned failures = 0;

    init_plugin();
    failures += check("rsp_rcp32", 0x32, 0x31, rsp_rcp32);
    failures += check("rsp_rcp32_dp", 0x32, 0x31, rsp_rcp32_dp);
    failures += check("rsp_rsq32", 0x36, 0x35, rsp_rsq32);

    if (failures) {
        printf("angrylion_divide_test: FAIL\n");
        return 1;
    }
    printf("angrylion_divide_test: all checks passed\n");
    return 0;
}
