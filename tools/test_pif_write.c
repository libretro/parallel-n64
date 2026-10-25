/* test_pif_write -- CPU writes to PIF RAM.
 *
 *  - the words of a command written one after another all land in PIF
 *    RAM but schedule a single SI completion event, not one per word;
 *  - a CPU write while an SI read DMA is in flight neither changes PIF
 *    RAM nor turns the transfer into a write.
 *
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include "../mupen64plus-core/src/device/pif/pif.c"

#include <stdio.h>
#include <stdlib.h>

static int s_events;

void add_interrupt_event(struct cp0* cp0, int type, unsigned int delay)
{
    (void)cp0; (void)delay;
    if (type == SI_INT)
        s_events++;
}
void cp0_update_count(struct r4300_core* r4300) { (void)r4300; }

/* the rest of the unit's callees, never reached here */
void DebugMessage(int level, const char *message, ...) { (void)level; (void)message; }
void init_cic_using_ipl3(struct cic* cic, const void* ipl3) { (void)cic; (void)ipl3; }
void n64_cic_nus_6105(char chl[], char rsp[], int len) { (void)chl; (void)rsp; (void)len; }
void inputControllerCommand(int Control, unsigned char *Command) { (void)Control; (void)Command; }
void inputReadController(int Control, unsigned char *Command) { (void)Control; (void)Command; }
void raise_maskable_interrupt(struct r4300_core* r4300, uint32_t cause_ip)
{
    (void)r4300; (void)cause_ip;
}

static int s_failures;

#define CHECK(cond, what) \
    do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

static void test_writes(void)
{
    static uint8_t base[PIF_ROM_SIZE + PIF_RAM_SIZE];
    static struct pif pif;
    static struct si_controller si;
    static struct r4300_core r4300;
    uint32_t ram = 0x1fc00000u + PIF_ROM_SIZE;
    unsigned i;

    pif.base = base;
    pif.ram = base + PIF_ROM_SIZE;
    pif.si = &si;
    pif.r4300 = &r4300;
    si.pif = &pif;

    /* a 16-word command written word by word */
    for (i = 0; i < 16; i++)
        write_pif_mem(&pif, ram + i * 4, 0x01020300u + i, ~0u);
    CHECK(s_events == 1, "PIF: one SI event for a command written word by word");
    CHECK(pif.ram[0] == 0x01 && pif.ram[15 * 4 + 3] == 15,
          "PIF: every word of the command is stored");
    CHECK(si.dma_dir == SI_DMA_WRITE, "PIF: a CPU write is a PIF write transfer");

    /* the event ends; the next command schedules a new one */
    si.regs[SI_STATUS_REG] &= ~(SI_STATUS_DMA_BUSY | SI_STATUS_IO_BUSY);
    si.dma_dir = SI_NO_DMA;
    write_pif_mem(&pif, ram + 0x3c, 0x00000001u, ~0u);
    CHECK(s_events == 2, "PIF: a write after completion schedules again");

    /* an SI read DMA in flight */
    si.regs[SI_STATUS_REG] = SI_STATUS_DMA_BUSY;
    si.dma_dir = SI_DMA_READ;
    pif.ram[0] = 0xaa;
    write_pif_mem(&pif, ram, 0x55555555u, ~0u);
    CHECK(si.dma_dir == SI_DMA_READ, "PIF: a write keeps an in-flight read a read");
    CHECK(pif.ram[0] == 0xaa, "PIF: a write during a DMA is lost");
    CHECK(s_events == 2, "PIF: no extra event during a DMA");
}

int main(void)
{
    test_writes();
    if (s_failures) {
        printf("test_pif_write: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_pif_write: all checks passed\n");
    return 0;
}
