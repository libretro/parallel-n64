/* test_cp0_exceptions -- COP0 exception entry and return.
 *
 *  - an exception taken with EXL clear records EPC and BD; one taken from
 *    inside a handler (EXL set) leaves them for the handler's own ERET;
 *  - Status.BEV selects the bootstrap vector;
 *  - a queued interrupt check takes the exception only if an enabled
 *    interrupt is still pending when it fires;
 *  - ERET returns through ErrorEPC and clears ERL when ERL is set, and
 *    through EPC clearing EXL otherwise.
 *
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "api/callbacks.h"
#include "device/device.h"
#include "device/pif/bootrom_hle.h"
#include "device/r4300/cp0.h"
#include "device/r4300/interrupt.h"
#include "device/r4300/r4300_core.h"
#include "device/r4300/tlb.h"
#include "main/main.h"
#include "main/savestates.h"

/* ---- the rest of the core, reduced to what cp0.c and interrupt.c reach - */

unsigned int r4300_emumode = EMUMODE_PURE_INTERPRETER;
unsigned int r4300_jit_backend = 0;
int g_gs_vi_counter = 0;

static uint32_t s_pc;
static uint32_t s_jumped_to;
static int s_jumps;
static int s_stop;
static struct precomp_instr* s_pc_struct;

void DebugMessage(int level, const char *message, ...)
{
    (void)level; (void)message;
}
uint32_t* r4300_pc(struct r4300_core* r4300) { (void)r4300; return &s_pc; }
struct precomp_instr** r4300_pc_struct(struct r4300_core* r4300) { (void)r4300; return &s_pc_struct; }
int* r4300_stop(struct r4300_core* r4300) { (void)r4300; return &s_stop; }
void generic_jump_to(struct r4300_core* r4300, unsigned int address)
{
    (void)r4300;
    s_jumped_to = address;
    s_jumps++;
    s_pc = address;
}
void invalidate_r4300_cached_code(struct r4300_core* r4300, uint32_t address, size_t size)
{
    (void)r4300; (void)address; (void)size;
}
void pif_bootrom_hle_execute(struct r4300_core* r4300) { (void)r4300; }
void poweron_device(struct device* dev) { (void)dev; }
void poweron_tlb(struct tlb* tlb) { (void)tlb; }
void reset_pif(struct pif* pif, unsigned int reset_type) { (void)pif; (void)reset_type; }
savestates_job savestates_get_job(void) { return savestates_job_nothing; }
int savestates_load(void) { return 0; }
int savestates_save(void) { return 0; }

/* ---- harness ----------------------------------------------------------- */

static struct r4300_core s_r4300;
static int s_failures;

#define CHECK(cond, what) \
    do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

static uint32_t* regs(void)
{
    return r4300_cp0_regs(&s_r4300.cp0);
}

static void reset_cpu(uint32_t status, uint32_t pc, int delay_slot)
{
    memset(regs(), 0, sizeof(uint32_t) * CP0_REGS_COUNT);
    regs()[CP0_STATUS_REG] = status;
    s_pc = pc;
    s_r4300.cp0.last_addr = pc;
    s_r4300.delay_slot = delay_slot;
    s_jumps = 0;
    s_jumped_to = 0;
}

static void test_entry(void)
{
    reset_cpu(0, 0x80001000u, 0);
    exception_general(&s_r4300);
    CHECK(regs()[CP0_EPC_REG] == 0x80001000u && !(regs()[CP0_CAUSE_REG] & CP0_CAUSE_BD)
          && (regs()[CP0_STATUS_REG] & CP0_STATUS_EXL) && s_jumped_to == 0x80000180u,
          "exception: EPC, EXL and the general vector");

    reset_cpu(0, 0x80001004u, 1);
    exception_general(&s_r4300);
    CHECK(regs()[CP0_EPC_REG] == 0x80001000u && (regs()[CP0_CAUSE_REG] & CP0_CAUSE_BD),
          "exception in a delay slot: EPC on the branch, BD set");

    /* taken again from inside the handler */
    s_pc = 0x80000200u;
    s_r4300.delay_slot = 0;
    exception_general(&s_r4300);
    CHECK(regs()[CP0_EPC_REG] == 0x80001000u && (regs()[CP0_CAUSE_REG] & CP0_CAUSE_BD),
          "nested exception: EPC and BD of the first exception kept");

    reset_cpu(CP0_STATUS_BEV, 0x80001000u, 0);
    exception_general(&s_r4300);
    CHECK(s_jumped_to == 0xbfc00380u, "BEV: bootstrap general vector");
}

static void test_check_interrupt(void)
{
    /* pending, enabled (IM2 sits at the same bit as IP2) */
    reset_cpu(CP0_STATUS_IE | CP0_CAUSE_IP2, 0x80002000u, 0);
    regs()[CP0_CAUSE_REG] = CP0_CAUSE_IP2;
    check_int_handler(&s_r4300);
    CHECK(s_jumps == 1 && regs()[CP0_EPC_REG] == 0x80002000u,
          "interrupt check: a pending, enabled interrupt is taken");

    /* cleared before the check fired */
    reset_cpu(CP0_STATUS_IE | CP0_CAUSE_IP2, 0x80002000u, 0);
    check_int_handler(&s_r4300);
    CHECK(s_jumps == 0 && regs()[CP0_EPC_REG] == 0,
          "interrupt check: an interrupt no longer pending is not taken");

    /* disabled before the check fired */
    reset_cpu(CP0_CAUSE_IP2, 0x80002000u, 0);
    regs()[CP0_CAUSE_REG] = CP0_CAUSE_IP2;
    check_int_handler(&s_r4300);
    CHECK(s_jumps == 0, "interrupt check: IE clear, nothing taken");

    /* already in a handler */
    reset_cpu(CP0_STATUS_IE | CP0_STATUS_EXL | CP0_CAUSE_IP2, 0x80000300u, 0);
    regs()[CP0_CAUSE_REG] = CP0_CAUSE_IP2;
    regs()[CP0_EPC_REG] = 0x80002000u;
    check_int_handler(&s_r4300);
    CHECK(s_jumps == 0 && regs()[CP0_EPC_REG] == 0x80002000u,
          "interrupt check: EXL set, nothing taken");
}

static void test_return(void)
{
    uint32_t target;

    reset_cpu(CP0_STATUS_EXL | CP0_STATUS_IE, 0x80000180u, 0);
    regs()[CP0_EPC_REG] = 0x80003000u;
    regs()[CP0_ERROREPC_REG] = 0x80004000u;
    target = cp0_return_from_exception(&s_r4300);
    CHECK(target == 0x80003000u && !(regs()[CP0_STATUS_REG] & CP0_STATUS_EXL)
          && (regs()[CP0_STATUS_REG] & CP0_STATUS_IE),
          "ERET: EXL cleared, back to EPC");

    reset_cpu(CP0_STATUS_ERL | CP0_STATUS_EXL, 0x80000180u, 0);
    regs()[CP0_EPC_REG] = 0x80003000u;
    regs()[CP0_ERROREPC_REG] = 0x80004000u;
    target = cp0_return_from_exception(&s_r4300);
    CHECK(target == 0x80004000u && !(regs()[CP0_STATUS_REG] & CP0_STATUS_ERL)
          && (regs()[CP0_STATUS_REG] & CP0_STATUS_EXL),
          "ERET with ERL: ERL cleared, back to ErrorEPC, EXL kept");
}

int main(void)
{
    s_r4300.cp0.count_per_op = 2;

    test_entry();
    test_check_interrupt();
    test_return();

    if (s_failures) {
        printf("test_cp0_exceptions: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_cp0_exceptions: all checks passed\n");
    return 0;
}
