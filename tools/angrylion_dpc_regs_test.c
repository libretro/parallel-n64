/* angrylion_dpc_regs_test -- processing a command list moves DPC_CURRENT
 * to DPC_END and leaves DPC_START as the CPU wrote it.
 *
 * DPC_START reads back the address the CPU last wrote; only CURRENT
 * advances as the RDP reads. The renderer used to overwrite START with
 * END after every list, so a CPU reading the register back saw the end
 * of the list it had just submitted.
 *
 * Built by `make check-angrylion` (top-level Makefile). Exit status 0 on pass.
 */
#include "n64video.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define RDRAM_SIZE (8u << 20)
#define CMD_ADDR   0x200000u

static uint8_t *rdram;
static uint8_t dmem[4096];
static uint32_t regs[8];
static uint32_t *dp_reg[8];
static uint32_t *vi_reg[16];
static uint32_t vi_regs[16];
static uint32_t mi_intr;
static void intr_cb(void) {}
static uint32_t ncmd;

static void emit(uint32_t w0, uint32_t w1)
{
    uint32_t *cmd = (uint32_t*)(rdram + CMD_ADDR);
    cmd[ncmd++] = w0;
    cmd[ncmd++] = w1;
}

int main(void)
{
    struct n64video_config cfg;
    unsigned i;
    int failures = 0;

    rdram = calloc(RDRAM_SIZE, 1);
    for (i = 0; i < 8; i++) dp_reg[i] = &regs[i];
    for (i = 0; i < 16; i++) vi_reg[i] = &vi_regs[i];
    n64video_config_init(&cfg);
    cfg.gfx.rdram = rdram; cfg.gfx.rdram_size = RDRAM_SIZE; cfg.gfx.dmem = dmem;
    cfg.gfx.vi_reg = vi_reg; cfg.gfx.dp_reg = dp_reg;
    cfg.gfx.mi_intr_reg = &mi_intr; cfg.gfx.mi_intr_cb = intr_cb;
    n64video_init(&cfg);

    emit(0x27000000u, 0);                                    /* sync pipe */
    emit(0x29000000u, 0);                                    /* sync full */
    regs[DP_STATUS] = 0;
    regs[DP_START] = regs[DP_CURRENT] = CMD_ADDR;
    regs[DP_END] = CMD_ADDR + ncmd * 4u;
    n64video_process_list();

    if (regs[DP_START] != CMD_ADDR) {
        printf("FAIL: DPC_START reads %08x after the list, expected %08x\n",
               (unsigned)regs[DP_START], (unsigned)CMD_ADDR);
        failures++;
    }
    if (regs[DP_CURRENT] != regs[DP_END]) {
        printf("FAIL: DPC_CURRENT did not reach DPC_END\n");
        failures++;
    }
    n64video_close();
    free(rdram);
    if (failures)
        return 1;
    printf("angrylion_dpc_regs_test: all checks passed\n");
    return 0;
}

void msg_error(const char *err, ...) { va_list ap; va_start(ap, err); vfprintf(stderr, err, ap); va_end(ap); fputc('\n', stderr); exit(1); }
void msg_warning(const char *err, ...) { (void)err; }
void msg_debug(const char *err, ...) { (void)err; }
void vdac_init(struct n64video_config *c) { (void)c; }
void vdac_write(void *fb, int w, int h, int p, int o) { (void)fb; (void)w; (void)h; (void)p; (void)o; }
void *vdac_acquire(unsigned width, unsigned height, unsigned *pitch, int need_read) { (void)width; (void)height; (void)pitch; (void)need_read; return 0; }
void vdac_sync(int i) { (void)i; }
void vdac_close(void) {}
int aleck64_e90_overlay(void *a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; return 0; }
