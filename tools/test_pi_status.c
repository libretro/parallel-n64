/* test_pi_status -- PI status, interrupt and domain register behaviour.
 *
 *  - a DMA raises the PI interrupt when it completes;
 *  - a CPU write to the bus (IO busy only) completes without one;
 *  - PI_STATUS reset abandons the transfer in flight, so its completion
 *    event is withdrawn; it clears the busy and error bits only, leaving
 *    a pending interrupt and an unrelated bus write's event alone;
 *  - the domain timing registers keep only their implemented bits;
 *  - with bus timing on, a DMA takes the duration its domain's latency,
 *    pulse width, release and page size registers give.
 *
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "api/callbacks.h"
#include "device/device.h"
#include "device/r4300/cp0.h"
#include "device/r4300/interrupt.h"
#include "device/r4300/r4300_core.h"
#include "device/rcp/mi/mi_controller.h"
#include "device/rcp/pi/pi_controller.h"
#include "device/rcp/rdp/fb.h"
#include "device/rcp/ri/ri_controller.h"
#include "device/rdram/rdram.h"

/* ---- the rest of the core, reduced to what pi and mi reach ------------ */

static int s_pi_events;
static int s_pi_removed;
static int s_maskable_raised;
static int s_cycle_count;

void DebugMessage(int level, const char *message, ...)
{
    (void)level; (void)message;
}
void add_interrupt_event(struct cp0* cp0, int type, unsigned int delay)
{
    (void)cp0; (void)delay;
    if (type == PI_INT)
        s_pi_events++;
}
void remove_event(struct interrupt_queue* q, int type)
{
    (void)q;
    if (type == PI_INT)
        s_pi_removed++;
}
void cp0_update_count(struct r4300_core* r4300) { (void)r4300; }
void invalidate_r4300_cached_code(struct r4300_core* r4300, uint32_t address, size_t size)
{
    (void)r4300; (void)address; (void)size;
}
void gen_interrupt(struct r4300_core* r4300) { (void)r4300; }
int* r4300_cp0_cycle_count(struct cp0* cp0) { (void)cp0; return &s_cycle_count; }
void r4300_check_interrupt(struct r4300_core* r4300, uint32_t cause_ip, int set_cause)
{
    (void)r4300; (void)cause_ip; (void)set_cause;
}
void raise_maskable_interrupt(struct r4300_core* r4300, uint32_t cause_ip)
{
    (void)r4300; (void)cause_ip;
    s_maskable_raised++;
}
void pre_framebuffer_read(struct fb* fb, uint32_t address) { (void)fb; (void)address; }
void post_framebuffer_write(struct fb* fb, uint32_t address, uint32_t length)
{
    (void)fb; (void)address; (void)length;
}

/* ---- harness ----------------------------------------------------------- */

static int s_failures;

#define CHECK(cond, what) \
    do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

static struct r4300_core s_r4300;
static struct mi_controller s_mi;
static struct pi_controller s_pi;

static void reset(void)
{
    memset(&s_mi, 0, sizeof(s_mi));
    memset(&s_pi, 0, sizeof(s_pi));
    s_mi.r4300 = &s_r4300;
    s_mi.regs[MI_INTR_MASK_REG] = MI_INTR_PI;
    s_pi.mi = &s_mi;
    s_pi_events = s_pi_removed = s_maskable_raised = 0;
}

static void write_pi(unsigned reg, uint32_t value)
{
    write_pi_regs(&s_pi, MM_PI_REGS + reg * 4u, value, 0xffffffffu);
}

static uint32_t read_pi(unsigned reg)
{
    uint32_t value = 0;
    read_pi_regs(&s_pi, MM_PI_REGS + reg * 4u, &value);
    return value;
}

int main(void)
{
    /* DMA completion */
    reset();
    s_pi.regs[PI_STATUS_REG] = PI_STATUS_DMA_BUSY;
    pi_end_of_dma_event(&s_pi);
    CHECK(!(s_pi.regs[PI_STATUS_REG] & PI_STATUS_DMA_BUSY)
          && (s_pi.regs[PI_STATUS_REG] & PI_STATUS_INTERRUPT)
          && (s_mi.regs[MI_INTR_REG] & MI_INTR_PI),
          "DMA completion raises the PI interrupt");

    /* CPU bus write completion */
    reset();
    s_pi.regs[PI_STATUS_REG] = PI_STATUS_IO_BUSY;
    pi_end_of_dma_event(&s_pi);
    CHECK(s_pi.regs[PI_STATUS_REG] == 0 && !(s_mi.regs[MI_INTR_REG] & MI_INTR_PI)
          && s_maskable_raised == 0,
          "a bus write completes without a PI interrupt");

    /* reset withdraws the pending completion */
    reset();
    s_pi.regs[PI_STATUS_REG] = PI_STATUS_DMA_BUSY;
    write_pi(PI_STATUS_REG, PI_STATUS_RESET);
    CHECK(s_pi.regs[PI_STATUS_REG] == 0 && s_pi_removed == 1,
          "PI reset abandons the DMA in flight");

    /* reset leaves the interrupt pending */
    reset();
    s_pi.regs[PI_STATUS_REG] = PI_STATUS_DMA_BUSY | PI_STATUS_ERROR | PI_STATUS_INTERRUPT;
    write_pi(PI_STATUS_REG, PI_STATUS_RESET);
    CHECK(s_pi.regs[PI_STATUS_REG] == PI_STATUS_INTERRUPT && s_pi_removed == 1,
          "PI reset clears busy and error, not the interrupt");

    /* with no DMA in flight there is no completion to withdraw */
    reset();
    s_pi.regs[PI_STATUS_REG] = PI_STATUS_IO_BUSY;
    write_pi(PI_STATUS_REG, PI_STATUS_RESET);
    CHECK(s_pi.regs[PI_STATUS_REG] == PI_STATUS_IO_BUSY && s_pi_removed == 0,
          "PI reset leaves a bus write's completion alone");

    /* clearing the interrupt alone leaves a transfer running */
    reset();
    s_pi.regs[PI_STATUS_REG] = PI_STATUS_DMA_BUSY | PI_STATUS_INTERRUPT;
    write_pi(PI_STATUS_REG, PI_STATUS_CLR_INTR);
    CHECK(s_pi.regs[PI_STATUS_REG] == PI_STATUS_DMA_BUSY && s_pi_removed == 0,
          "clearing the PI interrupt keeps the transfer");

    /* bus-register DMA duration: 4 KiB from an aligned cart address with the
     * retail cartridge timings (latency 0x40, pulse 0x12, page 2^9, release
     * 3) crosses 8 pages and flushes 26 buffers:
     * (15 + 64) * 8 + (18 + 3 + 2) * 2048 + 26 * 28 = 48464 RCP clocks */
    {
        uint32_t regs[PI_REGS_COUNT];
        memset(regs, 0, sizeof(regs));
        regs[PI_BSD_DOM1_LAT_REG] = 0x40;
        regs[PI_BSD_DOM1_PWD_REG] = 0x12;
        regs[PI_BSD_DOM1_PGS_REG] = 0x07;
        regs[PI_BSD_DOM1_RLS_REG] = 0x03;
        CHECK(pi_dma_duration(regs, 0x10000000u, 0x1000u) == (48464u * 3u + 3u) / 4u,
              "bus timing: a 4 KiB cartridge DMA from the domain registers");
        CHECK(pi_dma_duration(regs, 0x10000000u, 0x80u) == (((15u + 64u) + 23u * 64u + 28u) * 3u + 3u) / 4u,
              "bus timing: one whole 128-byte buffer");
        regs[PI_BSD_DOM2_LAT_REG] = 0x05;
        regs[PI_BSD_DOM2_PWD_REG] = 0x0c;
        regs[PI_BSD_DOM2_PGS_REG] = 0x0d;
        regs[PI_BSD_DOM2_RLS_REG] = 0x02;
        CHECK(pi_dma_duration(regs, 0x08000000u, 0x10u) == (((15u + 5u) + 16u * 8u + 16u) * 3u + 3u) / 4u,
              "bus timing: SRAM in domain 2 uses the domain 2 registers");
    }

    /* domain registers */
    reset();
    write_pi(PI_BSD_DOM1_LAT_REG, 0xffffffffu);
    write_pi(PI_BSD_DOM1_PWD_REG, 0xffffffffu);
    write_pi(PI_BSD_DOM1_PGS_REG, 0xffffffffu);
    write_pi(PI_BSD_DOM1_RLS_REG, 0xffffffffu);
    write_pi(PI_BSD_DOM2_PGS_REG, 0xffffffffu);
    write_pi(PI_BSD_DOM2_RLS_REG, 0xffffffffu);
    CHECK(read_pi(PI_BSD_DOM1_LAT_REG) == 0xff && read_pi(PI_BSD_DOM1_PWD_REG) == 0xff
          && read_pi(PI_BSD_DOM1_PGS_REG) == 0x0f && read_pi(PI_BSD_DOM1_RLS_REG) == 0x03
          && read_pi(PI_BSD_DOM2_PGS_REG) == 0x0f && read_pi(PI_BSD_DOM2_RLS_REG) == 0x03,
          "domain registers keep latency/pulse 8 bits, page 4, release 2");

    if (s_failures) {
        printf("test_pi_status: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_pi_status: all checks passed\n");
    return 0;
}
