/* test_fb_write -- CPU stores into a framebuffer are reported to the
 * graphics plugin at the N64 address and size they cover.
 *
 * Every byte, halfword, word and partial-word (SWL/SWR) store shape is
 * written through write_rdram_fb() to each byte offset of a word inside a
 * framebuffer the plugin has reported, and the fBWrite notifications must
 * name exactly the bytes the store's mask selects.  A doubleword store
 * split into two words can carry an empty mask; it reports nothing.
 *
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "api/callbacks.h"
#include "device/memory/m64p_memory.h"
#include "device/r4300/r4300_core.h"
#include "device/rcp/rdp/fb.h"
#include "device/rdram/rdram.h"
#include "plugin/plugin.h"

/* ---- the rest of the core, reduced to what fb.c reaches --------------- */

gfx_plugin_functions gfx;
unsigned int r4300_jit_backend;

void DebugMessage(int level, const char *message, ...)
{
    (void)level; (void)message;
}
void apply_mem_mapping(struct memory* mem, const struct mem_mapping* mapping)
{
    (void)mem; (void)mapping;
}
void invalidate_r4300_cached_code(struct r4300_core* r4300, uint32_t address, size_t size)
{
    (void)r4300; (void)address; (void)size;
}
void read_rdram_dram(void* opaque, uint32_t address, uint32_t* value)
{
    (void)opaque; (void)address;
    *value = 0;
}
void write_rdram_dram(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    (void)opaque; (void)address; (void)value; (void)mask;
}

/* ---- harness ----------------------------------------------------------- */

#define FB_ADDR 0x00100000u

static unsigned char s_reported[16];
static int s_bad_size;

static void record_write(unsigned int addr, unsigned int size)
{
    unsigned i;
    if (size == 0 || size > 4)
        s_bad_size = 1;
    for (i = 0; i < size; i++)
        if (addr + i >= FB_ADDR && addr + i < FB_ADDR + sizeof(s_reported))
            s_reported[addr + i - FB_ADDR] = 1;
}

static int s_failures;

static void check_store(struct fb* fb, uint32_t mask, const char* shape)
{
    unsigned k, expected[4];
    char what[96];
    int ok = 1;

    for (k = 0; k < 4; k++)
        expected[k] = ((mask >> ((3 - k) * 8)) & 0xffu) != 0;

    memset(s_reported, 0, sizeof(s_reported));
    s_bad_size = 0;
    write_rdram_fb(fb, FB_ADDR + 4, 0, mask);

    for (k = 0; k < sizeof(s_reported); k++) {
        unsigned want = (k >= 4 && k < 8) ? expected[k - 4] : 0;
        if (s_reported[k] != want)
            ok = 0;
    }
    sprintf(what, "%s (mask %08x) reports the bytes it writes", shape, (unsigned)mask);
    if (!ok || s_bad_size) {
        printf("FAIL: %s\n", what);
        s_failures++;
    }
}

int main(void)
{
    static struct fb fb;
    unsigned k;

    gfx.fBWrite = record_write;
    memset(&fb, 0, sizeof(fb));
    fb.infos[0].addr = FB_ADDR;
    fb.infos[0].width = 320;
    fb.infos[0].height = 240;
    fb.infos[0].size = 2;

    for (k = 0; k < 4; k++) {
        /* SB at offset k */
        check_store(&fb, UINT32_C(0xff) << (((k & 3) ^ 3) * 8), "SB");
        /* SWL at offset k keeps the top 4-k bytes of the register */
        check_store(&fb, UINT32_C(0xffffffff) >> (k * 8), "SWL");
        /* SWR at offset k */
        check_store(&fb, UINT32_C(0xffffffff) << ((3 - k) * 8), "SWR");
    }
    check_store(&fb, 0xffff0000u, "SH at offset 0");
    check_store(&fb, 0x0000ffffu, "SH at offset 2");
    check_store(&fb, 0xffffffffu, "SW");
    check_store(&fb, 0x00000000u, "empty half of a split SD");

    if (s_failures) {
        printf("test_fb_write: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_fb_write: all checks passed\n");
    return 0;
}
