/* test_r4300_interp -- the interpreters' exceptions and r0 handling.
 *
 *  - ADD/ADDI/SUB/DADD/DADDI/DSUB raise Ov on signed overflow and leave
 *    the destination alone; in-range results are written;
 *  - misaligned loads and stores raise AdEL/AdES with BadVAddr set, and
 *    make no access;
 *  - an instruction with r0 as its target still runs: SC $zero stores and
 *    clears the link bit, a misaligned LW $zero still faults, and r0 reads
 *    back zero;
 *  - CTC1 keeps only the implemented FCR31 bits and traps when a cause is
 *    enabled; CFC1 of an unimplemented control register reads zero;
 *  - a recompiler's MI_MODE window opens only for the store that sets a
 *    mode, not for a stale one;
 *  - an exception taken inside a handler keeps Cause.BD;
 *  - MTC0 Config writes only K0, CU, BE and EP; MTC0 Index keeps six bits
 *    without stopping the emulator; MTC0 Cause raising an enabled
 *    software interrupt takes it; DMFC0 sign-extends BadVAddr and
 *    ErrorEPC;
 *  - a reserved COP1 encoding is coprocessor-unusable with CU1 clear and
 *    an unimplemented-operation FPE with it set.
 *
 * Runs single instructions through the pure interpreter (InterpretOpcode).
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "api/callbacks.h"
#include "device/device.h"
#include "device/r4300/cp0.h"
#include "device/r4300/cp1.h"
#include "device/r4300/interrupt.h"
#include "device/r4300/r4300_core.h"
#include "device/r4300/tlb.h"
#include "main/main.h"
#include "main/savestates.h"

#include "../mupen64plus-core/src/device/r4300/pure_interp.c"

/* ---- the rest of the core, reduced to what the interpreter reaches ---- */

unsigned int r4300_emumode = EMUMODE_PURE_INTERPRETER;
unsigned int r4300_jit_backend = 0;
int g_aleck64_enabled = 0;
int g_gs_vi_counter = 0;
int frame_break = 0;

static struct r4300_core s_r4300;
static struct mi_controller s_mi;
static uint32_t s_ram[0x4000 / 4];   /* KSEG0 0x80000000.. */
static uint32_t s_jumped_to;
static int s_jumps;

void DebugMessage(int level, const char *message, ...) { (void)level; (void)message; }
int64_t* r4300_regs(struct r4300_core* r4300) { return r4300->regs; }
int64_t* r4300_mult_hi(struct r4300_core* r4300) { return &r4300->hi; }
int64_t* r4300_mult_lo(struct r4300_core* r4300) { return &r4300->lo; }
struct precomp_instr** r4300_pc_struct(struct r4300_core* r4300) { return &r4300->pc; }
uint32_t* r4300_pc(struct r4300_core* r4300) { return &r4300->pc->addr; }
int* r4300_stop(struct r4300_core* r4300) { return &r4300->stop; }
void generic_jump_to(struct r4300_core* r4300, unsigned int address)
{
    s_jumped_to = address;
    s_jumps++;
    *r4300_pc(r4300) = address;
}
void invalidate_r4300_cached_code(struct r4300_core* r4300, uint32_t address, size_t size)
{
    (void)r4300; (void)address; (void)size;
}
static uint32_t* word_at(uint32_t address)
{
    uint32_t off = address & UINT32_C(0x1ffffffc);
    return (off < sizeof(s_ram)) ? &s_ram[off / 4] : NULL;
}
uint32_t* fast_mem_access(struct r4300_core* r4300, uint32_t address)
{
    (void)r4300;
    return word_at(address);
}
uint32_t* mem_base_u32(void* mem_base, uint32_t address) { (void)mem_base; return word_at(address); }
uint32_t* aleck64_fast_mem(uint32_t address) { return word_at(address); }
int r4300_read_aligned_word(struct r4300_core* r4300, uint32_t address, uint32_t* value)
{
    uint32_t* w = word_at(address);
    (void)r4300;
    if (!w) return 0;
    *value = *w;
    return 1;
}
int r4300_read_aligned_dword(struct r4300_core* r4300, uint32_t address, uint64_t* value)
{
    uint32_t* w = word_at(address);
    (void)r4300;
    if (!w) return 0;
    *value = ((uint64_t)w[0] << 32) | w[1];
    return 1;
}
int r4300_write_aligned_word(struct r4300_core* r4300, uint32_t address, uint32_t value, uint32_t mask)
{
    uint32_t* w = word_at(address);
    (void)r4300;
    if (!w) return 0;
    *w = (*w & ~mask) | (value & mask);
    return 1;
}
int r4300_write_aligned_dword(struct r4300_core* r4300, uint32_t address, uint64_t value, uint64_t mask)
{
    uint32_t* w = word_at(address);
    (void)r4300;
    if (!w) return 0;
    w[0] = (w[0] & ~(uint32_t)(mask >> 32)) | (uint32_t)((value & mask) >> 32);
    w[1] = (w[1] & ~(uint32_t)mask) | (uint32_t)(value & mask);
    return 1;
}
uint64_t* r4300_cp2_latch(struct cp2* cp2) { static uint64_t latch; (void)cp2; return &latch; }
void pif_bootrom_hle_execute(struct r4300_core* r4300) { (void)r4300; }
void poweron_device(struct device* dev) { (void)dev; }
void poweron_tlb(struct tlb* tlb) { (void)tlb; }
void tlb_map(struct tlb* tlb, size_t entry) { (void)tlb; (void)entry; }
void tlb_unmap(struct tlb* tlb, size_t entry) { (void)tlb; (void)entry; }
void reset_pif(struct pif* pif, unsigned int reset_type) { (void)pif; (void)reset_type; }
savestates_job savestates_get_job(void) { return savestates_job_nothing; }
int savestates_load(void) { return 0; }
int savestates_save(void) { return 0; }

/* ---- harness ----------------------------------------------------------- */

static int s_failures;

#define CHECK(cond, what) \
    do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

#define PC0 UINT32_C(0x80001000)

static uint32_t* cp0(void) { return r4300_cp0_regs(&s_r4300.cp0); }

static void reset(void)
{
    memset(s_r4300.regs, 0, sizeof(s_r4300.regs));
    memset(cp0(), 0, sizeof(uint32_t) * CP0_REGS_COUNT);
    cp0()[CP0_STATUS_REG] = CP0_STATUS_CU1 | UINT32_C(0x04000000);
    s_r4300.pc = &s_r4300.interp_PC;
    s_r4300.interp_PC.addr = PC0;
    s_r4300.delay_slot = 0;
    s_r4300.llbit = 0;
    s_jumps = 0;
    s_jumped_to = 0;
    *r4300_cp1_fcr31(&s_r4300.cp1) = 0;
}

static void step(uint32_t op)
{
    s_ram[(PC0 & 0x3fff) / 4] = op;
    InterpretOpcode(&s_r4300);
}

static uint32_t exccode(void) { return cp0()[CP0_CAUSE_REG] & CP0_CAUSE_EXCCODE_MASK; }

static int took(uint32_t code)
{
    return s_jumps == 1 && s_jumped_to == UINT32_C(0x80000180)
        && exccode() == code && cp0()[CP0_EPC_REG] == PC0;
}

static uint32_t r_type(unsigned rs, unsigned rt, unsigned rd, unsigned funct)
{
    return (rs << 21) | (rt << 16) | (rd << 11) | funct;
}

static uint32_t i_type(unsigned op, unsigned rs, unsigned rt, uint16_t imm)
{
    return (op << 26) | (rs << 21) | (rt << 16) | imm;
}

static void test_overflow(void)
{
    reset();
    s_r4300.regs[1] = 0x7fffffff; s_r4300.regs[2] = 1; s_r4300.regs[3] = 0x55;
    step(r_type(1, 2, 3, 0x20));                 /* ADD r3,r1,r2 */
    CHECK(took(CP0_CAUSE_EXCCODE_OV) && s_r4300.regs[3] == 0x55,
          "ADD: signed overflow traps and leaves rd alone");

    reset();
    s_r4300.regs[1] = 5;
    step(i_type(0x08, 1, 3, 0xffff));            /* ADDI r3,r1,-1 */
    CHECK(s_jumps == 0 && s_r4300.regs[3] == 4 && *r4300_pc(&s_r4300) == PC0 + 4,
          "ADDI: an in-range sum is written");

    reset();
    s_r4300.regs[1] = (int32_t)0x80000000; s_r4300.regs[2] = 1;
    step(r_type(1, 2, 0, 0x22));                 /* SUB r0,r1,r2 */
    CHECK(took(CP0_CAUSE_EXCCODE_OV), "SUB: overflow traps even with r0 as rd");

    reset();
    s_r4300.regs[1] = INT64_MIN; s_r4300.regs[2] = 1; s_r4300.regs[3] = 0x55;
    step(r_type(1, 2, 3, 0x2e));                 /* DSUB r3,r1,r2 */
    CHECK(took(CP0_CAUSE_EXCCODE_OV) && s_r4300.regs[3] == 0x55, "DSUB: overflow traps");

    reset();
    s_r4300.regs[1] = INT64_MAX;
    step(i_type(0x18, 1, 3, 1));                 /* DADDI r3,r1,1 */
    CHECK(took(CP0_CAUSE_EXCCODE_OV), "DADDI: overflow traps");
}

static void test_alignment(void)
{
    reset();
    s_r4300.regs[1] = (int32_t)0x80002002; s_r4300.regs[3] = 0x55;
    step(i_type(0x23, 1, 3, 0));                 /* LW r3,0(r1) */
    CHECK(took(CP0_CAUSE_EXCCODE_ADEL) && cp0()[CP0_BADVADDR_REG] == 0x80002002u
          && s_r4300.regs[3] == 0x55, "LW: misaligned address raises AdEL");

    reset();
    s_ram[0x2000 / 4] = 0x11223344u;
    s_r4300.regs[1] = (int32_t)0x80002002; s_r4300.regs[3] = 0x77;
    step(i_type(0x2b, 1, 3, 0));                 /* SW r3,0(r1) */
    CHECK(took(CP0_CAUSE_EXCCODE_ADES) && s_ram[0x2000 / 4] == 0x11223344u,
          "SW: misaligned address raises AdES and stores nothing");

    reset();
    s_r4300.regs[1] = (int32_t)0x80002004;
    step(i_type(0x37, 1, 3, 0));                 /* LD r3,0(r1) */
    CHECK(took(CP0_CAUSE_EXCCODE_ADEL), "LD: a word-aligned dword raises AdEL");

    reset();
    s_r4300.regs[1] = (int32_t)0x80002001;
    step(i_type(0x21, 1, 3, 0));                 /* LH r3,0(r1) */
    CHECK(took(CP0_CAUSE_EXCCODE_ADEL), "LH: odd address raises AdEL");

    reset();
    s_r4300.regs[1] = (int32_t)0x80002002;
    step(i_type(0x23, 1, 0, 0));                 /* LW r0,0(r1) */
    CHECK(took(CP0_CAUSE_EXCCODE_ADEL), "LW $zero: still faults");
}

static void test_r0(void)
{
    reset();
    s_ram[0x2000 / 4] = 0xdeadbeefu;
    s_r4300.regs[1] = (int32_t)0x80002000;
    s_r4300.llbit = 1;
    step(i_type(0x38, 1, 0, 0));                 /* SC r0,0(r1) */
    CHECK(s_ram[0x2000 / 4] == 0 && s_r4300.llbit == 0,
          "SC $zero: stores zero and clears the link bit");
    step(0);                                     /* any next instruction */
    CHECK(s_r4300.regs[0] == 0, "r0 reads back zero");
}

static void test_fcr31(void)
{
    reset();
    s_r4300.regs[1] = (int32_t)0x7f800003;
    step((0x11u << 26) | (0x06u << 21) | (1u << 16) | (31u << 11));   /* CTC1 r1,$31 */
    CHECK(s_jumps == 0 && *r4300_cp1_fcr31(&s_r4300.cp1) == 0x01800003u,
          "CTC1: only FS, C, cause, enables, flags and RM are kept");

    reset();
    s_r4300.regs[1] = (int32_t)((1u << 14) | (1u << 9));               /* cause Z, enable Z */
    step((0x11u << 26) | (0x06u << 21) | (1u << 16) | (31u << 11));
    CHECK(took(CP0_CAUSE_EXCCODE_FPE), "CTC1: an enabled cause traps");

    reset();
    s_r4300.regs[3] = 0x55;
    step((0x11u << 26) | (0x02u << 21) | (3u << 16) | (1u << 11));    /* CFC1 r3,$1 */
    CHECK(s_r4300.regs[3] == 0, "CFC1: an unimplemented register reads zero");
}

/* A recompiler hands the stores after an MI_MODE write to the interpreter
 * only when that write set the init or EBUS test mode. A write that just
 * clears the DP interrupt, with a mode left over from earlier, does not
 * open the window: Conker's Bad Fur Day does that every frame, and the
 * stale init mode cut its next store down to one byte. */
static void test_mi_window(void)
{
    CHECK(pure_interp_mi_window_wanted(0xa4300000u, 0x100u, ~0u),
          "MI window: setting init mode opens it");
    CHECK(pure_interp_mi_window_wanted(0xa4300000u, 0x40fu, ~0u),
          "MI window: setting EBUS test mode opens it");
    CHECK(!pure_interp_mi_window_wanted(0xa4300000u, 0x800u, ~0u),
          "MI window: clearing the DP interrupt does not");
    CHECK(!pure_interp_mi_window_wanted(0xa4300000u, 0x0080u, ~0u),
          "MI window: clearing init mode does not");
    CHECK(!pure_interp_mi_window_wanted(0xa4300004u, 0x100u, ~0u),
          "MI window: another MI register does not");
    CHECK(!pure_interp_mi_window_wanted(0xa4300000u, 0x100u, 0xffu),
          "MI window: a masked-out set bit does not");
}

static uint32_t cop0_op(unsigned rs, unsigned rt, unsigned rd)
{
    return (0x10u << 26) | (rs << 21) | (rt << 16) | (rd << 11);
}

static void test_cause_bd(void)
{
    reset();
    cp0()[CP0_STATUS_REG] |= CP0_STATUS_EXL;
    cp0()[CP0_CAUSE_REG] = CP0_CAUSE_BD | CP0_CAUSE_EXCCODE_SYS;
    cp0()[CP0_EPC_REG] = 0x80000400u;
    s_r4300.regs[1] = (int32_t)0x80002002;
    step(i_type(0x23, 1, 3, 0));                 /* LW r3,0(r1) */
    CHECK((cp0()[CP0_CAUSE_REG] & CP0_CAUSE_BD) && exccode() == CP0_CAUSE_EXCCODE_ADEL
          && cp0()[CP0_EPC_REG] == 0x80000400u,
          "exception inside a handler: Cause.BD and EPC are kept");
}

static void test_cop0_moves(void)
{
    reset();
    cp0()[CP0_CONFIG_REG] = 0x6e463u;
    s_r4300.regs[1] = (int32_t)0x8002;
    step(cop0_op(4, 1, CP0_CONFIG_REG));         /* MTC0 r1,Config */
    CHECK(cp0()[CP0_CONFIG_REG] == 0x6e462u, "MTC0 Config: K0 can be cleared");
    reset();
    cp0()[CP0_CONFIG_REG] = 0x6e463u;
    s_r4300.regs[1] = -1;
    step(cop0_op(4, 1, CP0_CONFIG_REG));
    CHECK(cp0()[CP0_CONFIG_REG] == 0x0f06e46fu, "MTC0 Config: only K0, CU, BE and EP are written");

    reset();
    s_r4300.stop = 0;
    s_r4300.regs[1] = 0x3f;
    step(cop0_op(4, 1, CP0_INDEX_REG));          /* MTC0 r1,Index */
    CHECK(cp0()[CP0_INDEX_REG] == 0x3fu && s_r4300.stop == 0,
          "MTC0 Index: an index past the TLB is kept, the emulator runs on");

    reset();
    cp0()[CP0_BADVADDR_REG] = 0x80001234u;
    cp0()[CP0_ERROREPC_REG] = 0xbfc00400u;
    step(cop0_op(1, 3, CP0_BADVADDR_REG));       /* DMFC0 r3,BadVAddr */
    CHECK(s_r4300.regs[3] == (int64_t)(int32_t)0x80001234u, "DMFC0 BadVAddr: sign-extended");
    s_r4300.interp_PC.addr = PC0;
    step(cop0_op(1, 3, CP0_ERROREPC_REG));       /* DMFC0 r3,ErrorEPC */
    CHECK(s_r4300.regs[3] == (int64_t)(int32_t)0xbfc00400u, "DMFC0 ErrorEPC: sign-extended");

    /* a software interrupt raised through Cause with Status enabling it */
    reset();
    s_r4300.mi = &s_mi;
    s_r4300.cp0.interrupt_handlers[2].callback = check_int_handler;
    s_r4300.cp0.interrupt_handlers[2].opaque = &s_r4300;
    init_interrupt(&s_r4300.cp0);
    cp0()[CP0_COUNT_REG] = 0x100;
    s_r4300.cp0.last_addr = PC0;
    cp0()[CP0_STATUS_REG] |= CP0_STATUS_IE | CP0_CAUSE_IP0;
    s_r4300.regs[1] = (int32_t)CP0_CAUSE_IP0;
    step(cop0_op(4, 1, CP0_CAUSE_REG));          /* MTC0 r1,Cause */
    CHECK((cp0()[CP0_CAUSE_REG] & CP0_CAUSE_IP0) && s_jumps == 1
          && s_jumped_to == UINT32_C(0x80000180) && exccode() == 0
          && cp0()[CP0_EPC_REG] == PC0 + 4,
          "MTC0 Cause: an enabled software interrupt is taken after the write");
}

static void test_reserved_cop1(void)
{
    const uint32_t op = (0x11u << 26) | (0x10u << 21) | 0x10;   /* S-format funct 16 */

    reset();
    *r4300_cp1_fcr31(&s_r4300.cp1) = UINT32_C(0x0001f000);
    step(op);
    CHECK(took(CP0_CAUSE_EXCCODE_FPE) && *r4300_cp1_fcr31(&s_r4300.cp1) == UINT32_C(0x00020000),
          "reserved COP1: unimplemented-operation FPE with CU1 set");

    reset();
    cp0()[CP0_STATUS_REG] &= ~CP0_STATUS_CU1;
    step(op);
    CHECK(took(CP0_CAUSE_EXCCODE_CPU) && (cp0()[CP0_CAUSE_REG] & CP0_CAUSE_CE1),
          "reserved COP1: coprocessor unusable with CU1 clear");
}

int main(void)
{
    s_r4300.cp0.tlb.LUT_r[0] = 0;
    init_cp1(&s_r4300.cp1, NULL);
    poweron_cp1(&s_r4300.cp1);
    set_fpr_pointers(&s_r4300.cp1, UINT32_C(0x04000000));

    test_overflow();
    test_alignment();
    test_r0();
    test_fcr31();
    test_mi_window();
    test_cause_bd();
    test_cop0_moves();
    test_reserved_cop1();

    if (s_failures) {
        printf("test_r4300_interp: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_r4300_interp: all checks passed\n");
    return 0;
}
