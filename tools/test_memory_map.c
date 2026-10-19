/* test_memory_map -- every region the core publishes to the frontend's
 * memory map lies inside the memory behind its pointer.
 *
 * The frontend reads these regions directly (cheats, achievements, memory
 * viewers), so a descriptor longer than its backing store reads host
 * memory.  The device here is built from buffers of the real sizes, the
 * descriptors are generated, and each one is checked against the buffer
 * it points into.
 *
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include <stdio.h>
#include <string.h>

#include "device/device.h"
#include "device/device_memory_map.h"

static int s_failures;

#define CHECK(cond, what) \
    do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

#define DRAM_SIZE    0x800000u
#define ROM_SIZE     0x1000000u
#define DD_ROM_SIZE  0x400000u

static uint32_t s_dram[DRAM_SIZE / 4];
static uint32_t s_sp_mem[SP_MEM_SIZE / 4];
static uint8_t  s_pif[PIF_ROM_SIZE + PIF_RAM_SIZE];
static uint8_t  s_rom[ROM_SIZE];
static uint32_t s_dd_rom[DD_ROM_SIZE / 4];
static struct device s_dev;

struct backing { const char* name; const void* base; size_t size; };

static int inside(const struct retro_memory_descriptor* d,
                  const struct backing* b, size_t count)
{
    size_t i;
    for (i = 0; i < count; i++) {
        const uint8_t* lo = (const uint8_t*)b[i].base;
        const uint8_t* p  = (const uint8_t*)d->ptr;
        if (p >= lo && p < lo + b[i].size)
            return d->len <= (size_t)(lo + b[i].size - p);
    }
    return 0;
}

static void check_map(int with_dd)
{
    struct retro_memory_descriptor descs[DEVICE_MEMORY_DESCRIPTORS_MAX];
    struct backing b[5];
    size_t n, i;
    char what[96];

    b[0].name = "RDRAM";   b[0].base = s_dram;   b[0].size = sizeof(s_dram);
    b[1].name = "RSP mem"; b[1].base = s_sp_mem; b[1].size = sizeof(s_sp_mem);
    b[2].name = "PIF";     b[2].base = s_pif;    b[2].size = sizeof(s_pif);
    b[3].name = "ROM";     b[3].base = s_rom;    b[3].size = sizeof(s_rom);
    b[4].name = "DD ROM";  b[4].base = s_dd_rom; b[4].size = sizeof(s_dd_rom);

    memset(&s_dev, 0, sizeof(s_dev));
    s_dev.rdram.dram = s_dram;
    s_dev.rdram.dram_size = sizeof(s_dram);
    s_dev.sp.mem = s_sp_mem;
    s_dev.pif.base = s_pif;
    s_dev.pif.ram = s_pif + PIF_ROM_SIZE;
    s_dev.cart.cart_rom.rom = s_rom;
    s_dev.cart.cart_rom.rom_size = sizeof(s_rom);
    if (with_dd) {
        s_dev.dd.rom = s_dd_rom;
        s_dev.dd.rom_size = sizeof(s_dd_rom);
    }

    n = device_memory_descriptors(&s_dev, descs, DEVICE_MEMORY_DESCRIPTORS_MAX);
    sprintf(what, "%s: RDRAM x2, RSP mem, PIF ROM and RAM, ROM%s are published",
            with_dd ? "with 64DD" : "cart only", with_dd ? ", DD ROM" : "");
    CHECK(n == (size_t)(with_dd ? 7 : 6), what);

    for (i = 0; i < n; i++) {
        sprintf(what, "descriptor %u at 0x%08x stays inside its memory",
                (unsigned)i, (unsigned)descs[i].start);
        CHECK(inside(&descs[i], b, 5), what);
    }

    CHECK(n > 0 && descs[0].start == (R4300_KSEG0 | MM_RDRAM_DRAM)
          && descs[0].select == 0x20000000 && descs[0].len == sizeof(s_dram)
          && (descs[0].flags & RETRO_MEMDESC_SYSTEM_RAM),
          "cached RDRAM keeps its KSEG0 window");

    /* the descriptor array is never overrun */
    n = device_memory_descriptors(&s_dev, descs, 3);
    CHECK(n == 3, "the builder stops at the caller's descriptor count");
}

int main(void)
{
    check_map(0);
    check_map(1);

    if (s_failures) {
        printf("test_memory_map: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_memory_map: all checks passed\n");
    return 0;
}
