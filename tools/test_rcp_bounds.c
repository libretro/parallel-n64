/* test_rcp_bounds -- RCP register blocks, PI DMA and the cart-side DMA
 * handlers stay inside the memory that backs them.
 *
 *  - every register block answers its whole 64 KiB window: indices past
 *    the block's registers read as 0 and writes to them change nothing;
 *  - a PI DMA hands its handler only the part of the transfer that lies
 *    inside RDRAM, and a first block shorter than the DRAM misalignment
 *    transfers nothing;
 *  - the 32 KiB SRAM mirrors through its domain;
 *  - 64DD buffer DMAs stop at the end of the buffer they name;
 *  - a debug message longer than the formatting buffer is truncated;
 *  - an SI DMA touches only the RDRAM that is installed, and with bus
 *    timing on lasts as long as its joybus command list;
 *  - an SP DMA wraps inside its 4 KiB bank, skips only between rows and
 *    leaves the address and length registers where the hardware does;
 *  - an IS-Viewer transfer length past its buffer is ignored;
 *  - DPC_CLOCK counts on across an unfreeze, the 32-bit count wrap and a
 *    savestate round trip;
 *  - an SP DMA into RDRAM invalidates the recompiled code it overwrites,
 *    wrapping at the end of RDRAM;
 *  - a CLR_BROKE/CLR_HALT write while a finished task's SP_INT is still
 *    pending does not run the task again;
 *  - VI_V_SYNC keeps ten bits;
 *  - an AI_LEN write with both FIFO slots taken is dropped.
 *
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api/callbacks.h"
#include "backends/api/storage_backend.h"
#include "device/cart/is_viewer.h"
#include "device/cart/sram.h"
#include "device/dd/dd_controller.h"
#include "device/dd/disk.h"
#include "device/device.h"
#include "device/pif/pif.h"
#include "device/r4300/cp0.h"
#include "device/r4300/interrupt.h"
#include "device/r4300/r4300_core.h"
#include "device/rcp/ai/ai_controller.h"
#include "device/rcp/mi/mi_controller.h"
#include "device/rcp/pi/pi_controller.h"
#include "device/rcp/rdp/fb.h"
#include "device/rcp/rdp/rdp_core.h"
#include "device/rcp/ri/ri_controller.h"
#include "device/rcp/rsp/rsp_core.h"
#include "device/rcp/si/si_controller.h"
#include "device/rcp/vi/vi_controller.h"
#include "device/rdram/rdram.h"
#include "main/main.h"
#include "plugin/audio_libretro/audio_plugin.h"
#include "plugin/plugin.h"

/* ---- the rest of the core, reduced to what these units reach ---------- */

gfx_plugin_functions gfx;
rsp_plugin_functions rsp;
rsp_plugin_functions rsp_audio;
uint32_t CountPerScanlineOverride = 0;
int angrylion_sync_full_seen = 0;

void add_interrupt_event(struct cp0* cp0, int type, unsigned int delay)
{
    (void)cp0; (void)type; (void)delay;
}
unsigned int add_random_interrupt_time(struct r4300_core* r4300)
{
    (void)r4300;
    return 0;
}
void cp0_update_count(struct r4300_core* r4300) { (void)r4300; }
void gen_interrupt(struct r4300_core* r4300) { (void)r4300; }
static unsigned int s_sp_int_pending;
unsigned int* get_event(const struct interrupt_queue* q, int type)
{
    (void)q;
    return (type == SP_INT && s_sp_int_pending) ? &s_sp_int_pending : NULL;
}
void remove_event(struct interrupt_queue* q, int type) { (void)q; (void)type; }
void r4300_check_interrupt(struct r4300_core* r4300, uint32_t cause_ip, int set_cause)
{
    (void)r4300; (void)cause_ip; (void)set_cause;
}
static uint32_t s_cp0_regs[CP0_REGS_COUNT];
static int s_cycle_count;
uint32_t* r4300_cp0_regs(struct cp0* cp0) { (void)cp0; return s_cp0_regs; }
int* r4300_cp0_cycle_count(struct cp0* cp0) { (void)cp0; return &s_cycle_count; }
/* the ranges handed to the recompiler, for the DMA checks */
static uint32_t s_inval_addr[8];
static size_t s_inval_size[8];
static unsigned s_inval_count;
void invalidate_r4300_cached_code(struct r4300_core* r4300, uint32_t address, size_t size)
{
    (void)r4300;
    if (s_inval_count < 8) {
        s_inval_addr[s_inval_count] = address;
        s_inval_size[s_inval_count] = size;
    }
    s_inval_count++;
}
void new_frame(void) { }
void protect_framebuffers(struct fb* fb) { (void)fb; }
void unprotect_framebuffers(struct fb* fb) { (void)fb; }
void pre_framebuffer_read(struct fb* fb, uint32_t address) { (void)fb; (void)address; }
void post_framebuffer_write(struct fb* fb, uint32_t address, uint32_t length)
{
    (void)fb; (void)address; (void)length;
}
void set_audio_format_via_libretro(unsigned int frequency, unsigned int clock,
                                   unsigned int divider)
{
    (void)frequency; (void)clock; (void)divider;
}
void push_audio_samples_via_libretro(const void *buffer, size_t size)
{
    (void)buffer; (void)size;
}
void update_pif_ram(struct pif* pif) { (void)pif; }
unsigned int get_zone_from_head_track(unsigned int head, unsigned int track)
{
    (void)head; (void)track;
    return 0;
}
int angrylion_zboss_shadow(unsigned char *rdram, unsigned int rdram_size,
                           unsigned char *dmem)
{
    (void)rdram; (void)rdram_size; (void)dmem;
    return 0;
}

/* PE links keep every function of a unit, so the parts of these units the
 * tests never reach still need their callees. */
void add_interrupt_event_count(struct cp0* cp0, int type, unsigned int count)
{
    (void)cp0; (void)type; (void)count;
}
void remove_interrupt_event(struct cp0* cp0) { (void)cp0; }
void raise_maskable_interrupt(struct r4300_core* r4300, uint32_t cause_ip)
{
    (void)r4300; (void)cause_ip;
}
void process_pif_ram(struct pif* pif) { (void)pif; }
void push_audio_silence_via_libretro(size_t frames) { (void)frames; }
void new_vi(void) { }
void init_fb(struct fb* fb, struct memory* mem, struct rdram* rdram,
             struct r4300_core* r4300)
{
    (void)fb; (void)mem; (void)rdram; (void)r4300;
}
void poweron_fb(struct fb* fb) { (void)fb; }
void dd_ipl_hle_rom_fetch(uint32_t rom_off) { (void)rom_off; }
uint8_t* get_sector_base(const struct dd_disk* disk, unsigned int head,
                         unsigned int track, unsigned int block, unsigned int sector)
{
    (void)disk; (void)head; (void)track; (void)block; (void)sector;
    return NULL;
}

/* ---- harness ----------------------------------------------------------- */

static int s_failures;

#define CHECK(cond, what) \
    do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

#define GUARD_BYTE 0x5a
#define WINDOW     0x10000u

static int all_bytes(const unsigned char* p, size_t n, unsigned char v)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (p[i] != v)
            return 0;
    return 1;
}

static struct r4300_core s_r4300;
static struct mi_controller s_mi;

typedef void (*reg_read_fn)(void*, uint32_t, uint32_t*);
typedef void (*reg_write_fn)(void*, uint32_t, uint32_t, uint32_t);

/* A register block followed by one window of guard bytes: an index off
 * the end of the block lands in the guard, wherever the block keeps its
 * array. */
static void check_block(const char* name, void* block, size_t size,
                        size_t regs_offset, unsigned count, uint32_t base,
                        reg_read_fn rd, reg_write_fn wr)
{
    unsigned char* arena = (unsigned char*)block;
    unsigned char* saved = (unsigned char*)malloc(size);
    uint32_t probes[3];
    unsigned i;
    char what[96];

    probes[0] = base + count * 4u;
    probes[1] = base + 0x0ff0u;
    probes[2] = base + WINDOW - 4u;

    memcpy(saved, arena, size);
    for (i = 0; i < 3; i++)
    {
        uint32_t value = 0xdeadbeefu;
        uint32_t off = probes[i] - base;

        if (regs_offset + off + 4u > size)
            continue;

        rd(block, probes[i], &value);
        sprintf(what, "%s: read past the block at +0x%x is 0", name, (unsigned)off);
        CHECK(value == 0, what);

        wr(block, probes[i], 0xffffffffu, 0xffffffffu);
        sprintf(what, "%s: write past the block at +0x%x is dropped", name, (unsigned)off);
        CHECK(memcmp(saved, arena, size) == 0, what);
        memcpy(arena, saved, size);
    }
    free(saved);
}

#define BLOCK(type, field) \
    struct { type c; unsigned char guard[WINDOW]; }

static void test_register_blocks(void)
{
    static BLOCK(struct ai_controller, regs) ai;
    static BLOCK(struct mi_controller, regs) mi;
    static BLOCK(struct pi_controller, regs) pi;
    static BLOCK(struct ri_controller, regs) ri;
    static BLOCK(struct si_controller, regs) si;
    static BLOCK(struct vi_controller, regs) vi;
    static BLOCK(struct rsp_core, regs) sp;
    static BLOCK(struct rdp_core, dpc_regs) dp;
    static BLOCK(struct dd_controller, regs) dd;

    s_mi.r4300 = &s_r4300;
    memset(&ai, GUARD_BYTE, sizeof(ai)); ai.c.mi = &s_mi;
    memset(&mi, GUARD_BYTE, sizeof(mi)); mi.c.r4300 = &s_r4300;
    memset(&pi, GUARD_BYTE, sizeof(pi)); pi.c.mi = &s_mi;
    memset(&ri, GUARD_BYTE, sizeof(ri));
    memset(&si, GUARD_BYTE, sizeof(si)); si.c.mi = &s_mi;
    memset(&vi, GUARD_BYTE, sizeof(vi)); vi.c.mi = &s_mi;
    memset(&sp, GUARD_BYTE, sizeof(sp)); sp.c.mi = &s_mi;
    memset(&dp, GUARD_BYTE, sizeof(dp)); dp.c.mi = &s_mi;
    memset(&dd, GUARD_BYTE, sizeof(dd)); dd.c.r4300 = &s_r4300;

    check_block("AI", &ai, sizeof(ai), offsetof(struct ai_controller, regs),
                AI_REGS_COUNT, MM_AI_REGS, read_ai_regs, write_ai_regs);
    check_block("MI", &mi, sizeof(mi), offsetof(struct mi_controller, regs),
                MI_REGS_COUNT, MM_MI_REGS, read_mi_regs, write_mi_regs);
    check_block("PI", &pi, sizeof(pi), offsetof(struct pi_controller, regs),
                PI_REGS_COUNT, MM_PI_REGS, read_pi_regs, write_pi_regs);
    check_block("RI", &ri, sizeof(ri), offsetof(struct ri_controller, regs),
                RI_REGS_COUNT, MM_RI_REGS, read_ri_regs, write_ri_regs);
    check_block("SI", &si, sizeof(si), offsetof(struct si_controller, regs),
                SI_REGS_COUNT, MM_SI_REGS, read_si_regs, write_si_regs);
    check_block("VI", &vi, sizeof(vi), offsetof(struct vi_controller, regs),
                VI_REGS_COUNT, MM_VI_REGS, read_vi_regs, write_vi_regs);
    check_block("SP", &sp, sizeof(sp), offsetof(struct rsp_core, regs),
                SP_REGS_COUNT, MM_RSP_REGS, read_rsp_regs, write_rsp_regs);
    check_block("SP2", &sp, sizeof(sp), offsetof(struct rsp_core, regs2),
                SP_REGS2_COUNT, MM_RSP_REGS2, read_rsp_regs2, write_rsp_regs2);
    check_block("DPC", &dp, sizeof(dp), offsetof(struct rdp_core, dpc_regs),
                DPC_REGS_COUNT, MM_DPC_REGS, read_dpc_regs, write_dpc_regs);
    check_block("DPS", &dp, sizeof(dp), offsetof(struct rdp_core, dps_regs),
                DPS_REGS_COUNT, MM_DPS_REGS, read_dps_regs, write_dps_regs);
    /* the ASIC registers end where the micro-sequencer RAM starts */
    {
        uint32_t value = 0xdeadbeefu;
        uint32_t addr = MM_DD_REGS + DD_ASIC_REGS_COUNT * 4u;
        unsigned char saved[sizeof(dd.c)];

        memcpy(saved, &dd.c, sizeof(saved));
        for (; addr < MM_DD_MS_RAM; addr += 4u)
        {
            read_dd_regs(&dd.c, addr, &value);
            CHECK(value == 0, "DD: read past the ASIC registers is 0");
            write_dd_regs(&dd.c, addr, 0xffffffffu, 0xffffffffu);
        }
        CHECK(memcmp(saved, &dd.c, sizeof(saved)) == 0,
              "DD: writes past the ASIC registers are dropped");
    }
}

/* ---- PI DMA ------------------------------------------------------------- */

#define DRAM_SIZE 0x400000u

static uint32_t s_seen_length;
static int s_calls;

static unsigned int rec_dma_read(void* opaque, const uint8_t* dram, uint32_t dram_addr, uint32_t cart_addr, uint32_t length)
{
    (void)opaque; (void)dram; (void)dram_addr; (void)cart_addr;
    s_seen_length = length;
    s_calls++;
    return 0;
}
static unsigned int rec_dma_write(void* opaque, uint8_t* dram, uint32_t dram_addr, uint32_t cart_addr, uint32_t length)
{
    (void)opaque; (void)dram; (void)dram_addr; (void)cart_addr;
    s_seen_length = length;
    s_calls++;
    return 0;
}
static const struct pi_dma_handler s_rec_handler = { rec_dma_read, rec_dma_write };

static void rec_getter(struct cart* cart, struct dd_controller* dd, uint32_t address,
                       void** opaque, const struct pi_dma_handler** handler)
{
    (void)cart; (void)dd; (void)address;
    *opaque = NULL;
    *handler = &s_rec_handler;
}

static uint32_t pi_dma(struct pi_controller* pi, int to_dram,
                       uint32_t dram_addr, uint32_t len_reg)
{
    pi->regs[PI_STATUS_REG] = 0;
    pi->regs[PI_DRAM_ADDR_REG] = dram_addr;
    pi->regs[PI_CART_ADDR_REG] = MM_CART_ROM;
    s_seen_length = 0xdeadbeefu;
    write_pi_regs(pi, MM_PI_REGS + (to_dram ? PI_WR_LEN_REG : PI_RD_LEN_REG) * 4u,
                  len_reg, 0xffffffffu);
    return s_seen_length;
}

static void test_pi_dma(void)
{
    static struct rdram rdram;
    static struct ri_controller ri;
    static struct rdp_core dp;
    static struct pi_controller pi;

    rdram.dram = (uint32_t*)calloc(1, DRAM_SIZE);
    rdram.dram_size = DRAM_SIZE;
    ri.rdram = &rdram;
    s_mi.r4300 = &s_r4300;
    pi.get_pi_dma_handler = rec_getter;
    pi.mi = &s_mi;
    pi.ri = &ri;
    pi.dp = &dp;

    s_inval_count = 0;
    CHECK(pi_dma(&pi, 1, 0x1000u, 0xffu) == 0x100u, "PI: in-range write DMA keeps its length");
    CHECK(s_inval_count == 2
          && s_inval_addr[0] == 0x80001000u && s_inval_size[0] == 0x100u
          && s_inval_addr[1] == 0xa0001000u && s_inval_size[1] == 0x100u,
          "PI: a write DMA from any source invalidates the recompiled code it overwrote");
    s_inval_count = 0;
    CHECK(pi_dma(&pi, 0, 0x1000u, 0xffu) == 0x100u && s_inval_count == 0,
          "PI: a read DMA leaves recompiled code alone");
    CHECK(pi_dma(&pi, 1, 0x1006u, 0x00u) == 0u,
          "PI: a first block shorter than the DRAM misalignment transfers nothing");
    CHECK(pi_dma(&pi, 1, 0x1006u, 0x07u) == 2u, "PI: misalignment trims the first block");
    CHECK(pi_dma(&pi, 1, DRAM_SIZE - 0x100u, 0x1ffu) == 0x100u,
          "PI: a write DMA stops at the end of RDRAM");
    CHECK(pi_dma(&pi, 1, DRAM_SIZE + 0x100u, 0xffu) == 0u,
          "PI: a write DMA past RDRAM moves no data");
    CHECK(pi_dma(&pi, 0, DRAM_SIZE - 0x80u, 0xffu) == 0x80u,
          "PI: a read DMA stops at the end of RDRAM");
    CHECK(pi_dma(&pi, 0, 0x2000u, 0xffu) == 0x100u, "PI: in-range read DMA keeps its length");
    CHECK(pi.regs[PI_DRAM_ADDR_REG] == 0x2100u, "PI: DRAM address advances by the full length");

    free(rdram.dram);
}

/* ---- SRAM --------------------------------------------------------------- */

static unsigned char s_sram_arena[SRAM_SIZE + 0x10000u];
static size_t s_saved_start, s_saved_size;

static uint8_t* sram_data(const void* storage) { (void)storage; return s_sram_arena; }
static size_t sram_size(const void* storage) { (void)storage; return SRAM_SIZE; }
static void sram_save(void* storage, size_t start, size_t size)
{
    (void)storage;
    s_saved_start = start;
    s_saved_size = size;
}
static const struct storage_backend_interface s_sram_iface = { sram_data, sram_size, sram_save };

static void test_sram(void)
{
    static struct sram sram;
    static unsigned char dram[0x200];
    unsigned i;
    uint32_t value;

    init_sram(&sram, NULL, &s_sram_iface);
    memset(s_sram_arena, GUARD_BYTE, sizeof(s_sram_arena));
    for (i = 0; i < sizeof(dram); i++)
        dram[i ^ S8] = (unsigned char)i;

    /* 0x100 bytes from 0x7ff0: 0x10 at the top, 0xf0 wrapped to 0 */
    sram_dma_read(&sram, dram, 0, MM_DOM2_ADDR2 + 0x7ff0u, 0x100u);
    CHECK(all_bytes(s_sram_arena + SRAM_SIZE, sizeof(s_sram_arena) - SRAM_SIZE, GUARD_BYTE),
          "SRAM: a DMA across the top wraps instead of running past the save");
    CHECK(s_sram_arena[(0x7ff0u) ^ S8] == 0x00 && s_sram_arena[0x00u ^ S8] == 0x10
          && s_sram_arena[0xefu ^ S8] == 0xff,
          "SRAM: the wrapped bytes land at the bottom");
    CHECK(s_saved_start == 0 && s_saved_size == SRAM_SIZE,
          "SRAM: a wrapped DMA persists the whole save");

    /* the second 32 KiB of the domain mirrors the first */
    memset(dram, 0, sizeof(dram));
    sram_dma_write(&sram, dram, 0, MM_DOM2_ADDR2 + 0xfff0u, 0x20u);
    CHECK(dram[0x00u ^ S8] == 0x00 && dram[0x10u ^ S8] == 0x10,
          "SRAM: the upper mirror reads the same bytes");

    value = 0;
    read_sram(&sram, MM_DOM2_ADDR2 + 0xfffcu, &value);
    write_sram(&sram, MM_DOM2_ADDR2 + 0xfffcu, 0x01020304u, 0xffffffffu);
    CHECK(all_bytes(s_sram_arena + SRAM_SIZE, sizeof(s_sram_arena) - SRAM_SIZE, GUARD_BYTE),
          "SRAM: CPU accesses in the upper mirror stay inside the save");
}

/* ---- 64DD buffers ------------------------------------------------------- */

static void test_dd_buffers(void)
{
    static struct dd_controller dd;
    static unsigned char dram[0x1000];
    static uint32_t rom[0x100];
    unsigned char ms_ram[sizeof(dd.ms_ram)];
    unsigned i;
    int clean;

    memset(&dd, GUARD_BYTE, sizeof(dd));
    dd.r4300 = &s_r4300;
    dd.rom = rom;
    dd.rom_size = sizeof(rom);
    memset(dd.c2s_buf, 0x11, sizeof(dd.c2s_buf));
    memset(dd.ds_buf, 0x22, sizeof(dd.ds_buf));
    memset(rom, 0x33, sizeof(rom));
    memcpy(ms_ram, dd.ms_ram, sizeof(ms_ram));

    memset(dram, 0xee, sizeof(dram));
    dd_dom_dma_write(&dd, dram, 0, MM_DD_C2S_BUFFER, 0x800u);
    CHECK(all_bytes(dram, sizeof(dd.c2s_buf), 0x11), "DD: C2S buffer DMA copies the buffer");
    CHECK(all_bytes(dram + sizeof(dd.c2s_buf), 0x800u - sizeof(dd.c2s_buf), 0),
          "DD: C2S buffer DMA reads zero past the buffer");

    memset(dram, 0xee, sizeof(dram));
    dd_dom_dma_write(&dd, dram, 0, MM_DD_ROM, 0x800u);
    CHECK(all_bytes(dram, sizeof(rom), 0x33) && all_bytes(dram + sizeof(rom), 0x800u - sizeof(rom), 0),
          "DD: ROM DMA reads zero past the IPL");

    memset(dram, 0x44, sizeof(dram));
    dd_dom_dma_read(&dd, dram, 0, MM_DD_DS_BUFFER, 0x200u);
    clean = memcmp(ms_ram, dd.ms_ram, sizeof(ms_ram)) == 0;
    for (i = 0; i < DD_ASIC_REGS_COUNT; i++)
        if (dd.regs[i] != 0x5a5a5a5au)
            clean = 0;
    CHECK(all_bytes(dd.ds_buf, sizeof(dd.ds_buf), 0x44) && clean,
          "DD: a data-sector DMA stops at the end of the buffer");
}

/* ---- debug messages ---------------------------------------------------- */

static size_t s_message_length;

static void record_message(void* context, int level, const char* message)
{
    (void)context; (void)level;
    s_message_length = strlen(message);
}

static void test_debug_message(void)
{
    static char long_name[1024];

    memset(long_name, 'x', sizeof(long_name) - 1);
    SetDebugCallback(record_message, NULL);
    DebugMessage(M64MSG_INFO, "loading %s", long_name);
    SetDebugCallback(NULL, NULL);
    CHECK(s_message_length > 0 && s_message_length < 256,
          "DebugMessage: a long message is truncated to the buffer");
}

static void test_si_dma(void)
{
    static struct rdram rdram;
    static struct ri_controller ri;
    static struct pif pif;
    static struct si_controller si;
    static uint8_t pif_ram[PIF_RAM_SIZE];
    unsigned char* bytes;
    unsigned int i;
    int clean = 1;

    bytes = (unsigned char*)calloc(1, DRAM_SIZE + 0x100u);
    memset(bytes + DRAM_SIZE, GUARD_BYTE, 0x100u);
    rdram.dram = (uint32_t*)bytes;
    rdram.dram_size = DRAM_SIZE;
    ri.rdram = &rdram;
    s_mi.r4300 = &s_r4300;
    memset(&si, 0, sizeof(si));
    si.mi = &s_mi;
    si.pif = &pif;
    si.ri = &ri;
    pif.ram = pif_ram;

    /* PIF -> RDRAM straddling the end of RDRAM */
    memset(pif.ram, 0x3c, PIF_RAM_SIZE);
    si.regs[SI_DRAM_ADDR_REG] = DRAM_SIZE - 8u;
    write_si_regs(&si, MM_SI_REGS + SI_PIF_ADDR_RD64B_REG * 4u, 0x1fc007c0u, ~0u);
    si_end_of_dma_event(&si);
    for (i = 0; i < 0x100u; i++)
        if (bytes[DRAM_SIZE + i] != GUARD_BYTE)
            clean = 0;
    CHECK(clean && bytes[DRAM_SIZE - 1u] == 0x3c,
          "SI: a PIF read DMA stops at the end of RDRAM");

    /* RDRAM -> PIF from the same place reads zeros past the end */
    memset(pif.ram, 0x77, PIF_RAM_SIZE);
    write_si_regs(&si, MM_SI_REGS + SI_STATUS_REG * 4u, 0, ~0u);
    si.regs[SI_STATUS_REG] = 0;
    write_si_regs(&si, MM_SI_REGS + SI_PIF_ADDR_WR64B_REG * 4u, 0x1fc007c0u, ~0u);
    CHECK(pif.ram[0] == 0x3c && pif.ram[8] == 0 && pif.ram[PIF_RAM_SIZE - 1u] == 0,
          "SI: a PIF write DMA reads zero past the end of RDRAM");

    /* durations: fixed by default, from the command list with bus timing */
    si.dma_duration = SI_DMA_DURATION_DEFAULT;
    g_dma_bus_timing = 0;
    CHECK(si_dma_duration(&si, 1) == SI_DMA_DURATION_DEFAULT, "SI: fixed duration by default");
    g_dma_bus_timing = 1;
    CHECK(si_dma_duration(&si, 0) == (4065u * 3u + 3u) / 4u, "SI: bus-timed write");
    memset(pif.ram, 0, PIF_RAM_SIZE);
    pif.ram[0] = 0xfe;
    CHECK(si_dma_duration(&si, 1) == ((13600u + 1420u) * 3u + 3u) / 4u,
          "SI: bus-timed read of an empty command list");
    pif.ram[0] = 0x01; pif.ram[1] = 0x04; pif.ram[2] = 0x01; pif.ram[7] = 0xfe;
    CHECK(si_dma_duration(&si, 1) == ((13600u + 18000u + 1420u) * 3u + 3u) / 4u,
          "SI: bus-timed read of one controller poll");
    si.dma_duration = 0x1234;
    CHECK(si_dma_duration(&si, 1) == 0x1234, "SI: a database duration wins");
    g_dma_bus_timing = 0;

    free(bytes);
}

static void test_sp_dma(void)
{
    static struct rdram rdram;
    static struct ri_controller ri;
    static struct rdp_core dp;
    static struct rsp_core sp;
    static uint32_t sp_mem[0x2000 / 4];
    unsigned char* mem = (unsigned char*)sp_mem;
    unsigned char* dram;
    unsigned i;
    int ok = 1;

    dram = (unsigned char*)calloc(1, 0x800000u);
    for (i = 0; i < 0x200u; i++)
        dram[i ^ S8] = (unsigned char)i;
    rdram.dram = (uint32_t*)dram;
    rdram.dram_size = 0x800000u;
    ri.rdram = &rdram;
    s_mi.r4300 = &s_r4300;
    memset(&sp, 0, sizeof(sp));
    memset(sp_mem, 0xee, sizeof(sp_mem));
    sp.mem = sp_mem;
    sp.ri = &ri;
    sp.dp = &dp;
    sp.mi = &s_mi;

    /* RDRAM -> IMEM, two 16-byte rows at the top of IMEM, skip 0x18 with
     * junk in its low bits */
    write_rsp_regs(&sp, MM_RSP_REGS + 4u * SP_MEM_ADDR_REG, 0x1ff8u, ~0u);
    write_rsp_regs(&sp, MM_RSP_REGS + 4u * SP_DRAM_ADDR_REG, 0x100u, ~0u);
    write_rsp_regs(&sp, MM_RSP_REGS + 4u * SP_RD_LEN_REG,
                   (0x01bu << 20) | (1u << 12) | 0x00fu, ~0u);

    for (i = 0; i < 8; i++)
        if (mem[(0x1ff8u + i) ^ S8] != (unsigned char)(0x100u + i)
         || mem[(0x1000u + i) ^ S8] != (unsigned char)(0x108u + i)
         || mem[(0x1008u + i) ^ S8] != (unsigned char)(0x128u + i)
         || mem[(0x1010u + i) ^ S8] != (unsigned char)(0x130u + i))
            ok = 0;
    for (i = 0; i < 0x1000u; i++)
        if (mem[i] != 0xee)
            ok = 0;
    CHECK(ok, "SP DMA: rows wrap inside the IMEM bank, skip between rows");
    CHECK(sp.regs[SP_MEM_ADDR_REG] == 0x1018u, "SP DMA: memory address keeps its bank");
    CHECK(sp.regs[SP_DRAM_ADDR_REG] == 0x138u, "SP DMA: no skip after the last row");
    CHECK(sp.regs[SP_RD_LEN_REG] == 0x01800ff8u && sp.regs[SP_WR_LEN_REG] == 0x01800ff8u,
          "SP DMA: both length registers read back skip and 0xff8");

    write_rsp_regs(&sp, MM_RSP_REGS + 4u * SP_MEM_ADDR_REG, 0xffffffffu, ~0u);
    write_rsp_regs(&sp, MM_RSP_REGS + 4u * SP_DRAM_ADDR_REG, 0xffffffffu, ~0u);
    CHECK(sp.regs[SP_MEM_ADDR_REG] == 0x1ff8u && sp.regs[SP_DRAM_ADDR_REG] == 0xfffff8u,
          "SP DMA: address registers hold aligned values");

    free(dram);
}

/* ---- RSP task dispatch ------------------------------------------------- */

#if !defined(_WIN32)
/* The debug switches the dispatcher looks up; counted rather than read, so
 * the check sees whether every task queries the environment again. */
static unsigned s_getenv_calls;
char* getenv(const char* name)
{
    if (strncmp(name, "HARNESS_GFX_CAP", 15) == 0 || strncmp(name, "ZB_SHADOW", 9) == 0)
        s_getenv_calls++;
    return NULL;
}
#endif

static unsigned int rsp_task_cycles(unsigned int cycles) { return cycles; }

static void test_rsp_task_env(void)
{
    static struct rdram rdram;
    static struct ri_controller ri;
    static struct rdp_core dp;
    static struct rsp_core sp;
    static uint32_t sp_mem[0x2000 / 4];
    unsigned i;

    rdram.dram = (uint32_t*)calloc(1, 0x800000u);
    rdram.dram_size = 0x800000u;
    ri.rdram = &rdram;
    s_mi.r4300 = &s_r4300;
    memset(&sp, 0, sizeof(sp));
    sp.mem = sp_mem;
    sp.ri = &ri;
    sp.dp = &dp;
    sp.mi = &s_mi;
    rsp.doRspCycles = rsp_task_cycles;
    rsp_audio.doRspCycles = rsp_task_cycles;

    for (i = 0; i < 8; i++) {
        sp_mem[0xfc0 / 4] = (i & 1) ? 2 : 1;   /* graphics, audio, ... */
        sp.regs[SP_STATUS_REG] = SP_STATUS_HALT;
        do_SP_Task(&sp);
    }
#if !defined(_WIN32)
    CHECK(s_getenv_calls <= 4,
          "RSP: the debug switches are read once, not on every task");
#endif
    free(rdram.dram);
}

static void test_is_viewer(void)
{
    static struct is_viewer isv;

    poweron_is_viewer(&isv);
    write_is_viewer(&isv, 0x14u, 0xffffffffu, ~0u);
    CHECK(isv.buffer_pos == 0, "IS64: a wrapping transfer length is ignored");
    write_is_viewer(&isv, 0x14u, IS_BUFFER_SIZE, ~0u);
    CHECK(isv.buffer_pos == 0, "IS64: a length past the text area is ignored");

    memcpy(&isv.data[0x20], "ok", 2);
    write_is_viewer(&isv, 0x14u, 2u, ~0u);
    CHECK(isv.buffer_pos == 2 && memcmp(isv.output_buffer, "ok", 2) == 0,
          "IS64: an in-range transfer is buffered");
}

static uint32_t dpc_clock_read(struct rdp_core* dp)
{
    uint32_t value = 0;
    read_dpc_regs(dp, MM_DPC_REGS + 4u * DPC_CLOCK_REG, &value);
    return value;
}

static uint32_t dpc_clock_expect(uint64_t counts)
{
    return (uint32_t)((counts * 4u) / 3u) & UINT32_C(0x00ffffff);
}

static void test_dpc_clock(void)
{
    static struct rdp_core dp;
    static struct rdp_core restored;
    uint32_t saved;

    memset(&dp, 0, sizeof(dp));
    dp.mi = &s_mi;
    s_mi.r4300 = &s_r4300;

    s_cp0_regs[CP0_COUNT_REG] = 1000u;
    write_dpc_regs(&dp, MM_DPC_REGS + 4u * DPC_STATUS_REG, DPC_CLR_CLOCK_CTR, ~0u);
    CHECK(dpc_clock_read(&dp) == 0u, "DPC: clearing the clock counter zeroes it");

    s_cp0_regs[CP0_COUNT_REG] = 1300u;
    CHECK(dpc_clock_read(&dp) == 400u, "DPC: clock advances four per three counts");

    write_dpc_regs(&dp, MM_DPC_REGS + 4u * DPC_STATUS_REG, DPC_CLR_FREEZE, ~0u);
    CHECK(dpc_clock_read(&dp) == 400u, "DPC: an unfreeze leaves the clock alone");

    /* 2^32 + 3 counts after the clear, read once on the way */
    s_cp0_regs[CP0_COUNT_REG] = 0u;
    write_dpc_regs(&dp, MM_DPC_REGS + 4u * DPC_STATUS_REG, DPC_CLR_CLOCK_CTR, ~0u);
    s_cp0_regs[CP0_COUNT_REG] = 0x80000000u;
    (void)dpc_clock_read(&dp);
    s_cp0_regs[CP0_COUNT_REG] = 3u;
    CHECK(dpc_clock_read(&dp) == dpc_clock_expect(UINT64_C(0x100000003)),
          "DPC: clock stays continuous across the count wrap");

    s_cp0_regs[CP0_COUNT_REG] = 5000u;
    rdp_sync_dpc_clock(&dp);
    saved = dp.dpc_regs[DPC_CLOCK_REG];

    memset(&restored, 0, sizeof(restored));
    restored.mi = &s_mi;
    restored.dpc_regs[DPC_CLOCK_REG] = saved;
    rdp_restore_dpc_clock(&restored);
    CHECK(dpc_clock_read(&restored) == saved, "DPC: a loaded state resumes the saved clock");
    s_cp0_regs[CP0_COUNT_REG] = 5003u;
    CHECK(dpc_clock_read(&restored) == ((saved + 4u) & UINT32_C(0x00ffffff)),
          "DPC: the restored clock keeps counting");
}

static unsigned s_rsp_tasks;
static unsigned int rsp_count_task(unsigned int cycles) { s_rsp_tasks++; return cycles; }

static void test_device_writes(void)
{
    static struct rdram rdram;
    static struct ri_controller ri;
    static struct rdp_core dp;
    static struct rsp_core sp;
    static struct vi_controller vi;
    static struct ai_controller ai;
    static uint32_t sp_mem[0x2000 / 4];

    rdram.dram = (uint32_t*)calloc(1, 0x800000u);
    rdram.dram_size = 0x800000u;
    ri.rdram = &rdram;
    s_mi.r4300 = &s_r4300;
    memset(&sp, 0, sizeof(sp));
    sp.mem = sp_mem;
    sp.ri = &ri;
    sp.dp = &dp;
    sp.mi = &s_mi;

    /* DMEM -> RDRAM, one 16-byte row from 8 bytes below the top of RDRAM */
    s_inval_count = 0;
    write_rsp_regs(&sp, MM_RSP_REGS + 4u * SP_MEM_ADDR_REG, 0x000u, ~0u);
    write_rsp_regs(&sp, MM_RSP_REGS + 4u * SP_DRAM_ADDR_REG, 0x7ffff8u, ~0u);
    write_rsp_regs(&sp, MM_RSP_REGS + 4u * SP_WR_LEN_REG, 0x00fu, ~0u);
    CHECK(s_inval_count == 4
       && s_inval_addr[0] == 0x807ffff8u && s_inval_size[0] == 8
       && s_inval_addr[1] == 0xa07ffff8u && s_inval_size[1] == 8
       && s_inval_addr[2] == 0x80000000u && s_inval_size[2] == 8
       && s_inval_addr[3] == 0xa0000000u && s_inval_size[3] == 8,
          "SP DMA: a write to RDRAM invalidates the code it overwrites, wrapping");

    /* a finished task waits for its SP_INT before it can run again */
    rsp.doRspCycles = rsp_count_task;
    rsp_audio.doRspCycles = rsp_count_task;
    sp_mem[0xfc0 / 4] = 2;
    sp.regs[SP_STATUS_REG] = 0;
    sp.rsp_task_locked = 0;
    s_sp_int_pending = 1;
    s_rsp_tasks = 0;
    write_rsp_regs(&sp, MM_RSP_REGS + 4u * SP_STATUS_REG, 0x4u, ~0u);   /* CLR_BROKE */
    write_rsp_regs(&sp, MM_RSP_REGS + 4u * SP_STATUS_REG, 0x1u, ~0u);   /* CLR_HALT */
    CHECK(s_rsp_tasks == 0, "SP_STATUS: no replay while the task's SP_INT is pending");
    s_sp_int_pending = 0;
    write_rsp_regs(&sp, MM_RSP_REGS + 4u * SP_STATUS_REG, 0x1u, ~0u);
    CHECK(s_rsp_tasks == 1, "SP_STATUS: CLR_HALT runs the task once SP_INT has fired");

    /* VI_V_SYNC */
    memset(&vi, 0, sizeof(vi));
    vi.mi = &s_mi;
    vi.clock = 48681812;
    vi.expected_refresh_rate = 60;
    write_vi_regs(&vi, MM_VI_REGS + 4u * VI_V_SYNC_REG, 0xfffff20du, ~0u);
    CHECK(vi.regs[VI_V_SYNC_REG] == 0x20du && vi.count_per_scanline != 0,
          "VI: V_SYNC keeps ten bits");

    /* AI_LEN with the FIFO full */
    memset(&ai, 0, sizeof(ai));
    ai.mi = &s_mi;
    ai.ri = &ri;
    ai.vi = &vi;
    ai.dma_modifier = 100;
    ai.regs[AI_STATUS_REG] = UINT32_C(0xc0000000);       /* busy, full */
    ai.regs[AI_DRAM_ADDR_REG] = 0x2000;
    ai.fifo[1].address = 0x1000;
    ai.fifo[1].length = 0x100;
    write_ai_regs(&ai, MM_AI_REGS + 4u * AI_LEN_REG, 0x200u, ~0u);
    CHECK(ai.fifo[1].address == 0x1000 && ai.fifo[1].length == 0x100,
          "AI: AI_LEN with both FIFO slots taken is dropped");

    free(rdram.dram);
}

int main(void)
{
    test_debug_message();
    test_register_blocks();
    test_pi_dma();
    test_sram();
    test_dd_buffers();
    test_dpc_clock();
    test_si_dma();
    test_sp_dma();
    test_rsp_task_env();
    test_is_viewer();
    test_device_writes();

    if (s_failures) {
        printf("test_rcp_bounds: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_rcp_bounds: all checks passed\n");
    return 0;
}
