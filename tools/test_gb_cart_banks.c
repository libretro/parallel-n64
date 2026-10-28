/* test_gb_cart_banks -- banked GB cartridge ROM and RAM addressing.
 *
 * A switchable ROM bank lives at rom_bank * 0x4000 and a RAM bank at
 * ram_bank * 0x2000 in the cartridge image; both offsets pass 0xffff once
 * the bank number reaches 4 (ROM) or 8 (RAM), so the image offset must not
 * be narrowed to 16 bits on its way to the storage backend:
 *
 *  - MBC5 with a 1 MiB ROM: every bank 1..63 reads back its own fill at
 *    both ends of the 0x4000-0x7fff window;
 *  - MBC1 with a 512 KiB ROM: every bank 1..31 reads back its own fill;
 *  - Pocket Camera with 128 KiB RAM: a byte written through bank 9 reads
 *    back through bank 9 and leaves bank 1 untouched, for every bank pair
 *    whose offsets alias under a 16-bit truncation.
 *
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/api/storage_backend.h"
#include "device/gb/gb_cart.h"

void DebugMessage(int level, const char *message, ...)
{
    (void)level; (void)message;
}

struct mem_storage
{
    uint8_t* data;
    size_t size;
};

static uint8_t* mem_data(const void* storage)
{
    return ((const struct mem_storage*)storage)->data;
}

static size_t mem_size(const void* storage)
{
    return ((const struct mem_storage*)storage)->size;
}

static void mem_save(void* storage, size_t start, size_t size)
{
    (void)storage; (void)start; (void)size;
}

static const struct storage_backend_interface s_imem =
{
    mem_data, mem_size, mem_save
};

static struct mem_storage s_rom;
static struct mem_storage s_ram;

static void init_rom(void* user_data, void** storage, const struct storage_backend_interface** istorage)
{
    (void)user_data;
    *storage = &s_rom;
    *istorage = &s_imem;
}

static void release_rom(void* user_data) { (void)user_data; }

static void init_ram(void* user_data, size_t ram_size, void** storage, const struct storage_backend_interface** istorage)
{
    (void)user_data;
    s_ram.data = (uint8_t*)calloc(1, ram_size);
    s_ram.size = ram_size;
    *storage = &s_ram;
    *istorage = &s_imem;
}

static void release_ram(void* user_data) { (void)user_data; }

static int s_failures;

static void CHECK(int cond, const char* fmt, ...)
{
    va_list ap;

    if (cond)
        return;
    ++s_failures;
    fputs("FAIL: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* each byte of ROM bank b holds b ^ 0x5a, except the header bytes */
static void make_rom(size_t size, uint8_t cart_type, uint8_t ram_code)
{
    size_t i;

    s_rom.data = (uint8_t*)malloc(size);
    s_rom.size = size;
    for (i = 0; i < size; ++i)
        s_rom.data[i] = (uint8_t)((i / 0x4000) ^ 0x5a);
    s_rom.data[0x147] = cart_type;
    s_rom.data[0x149] = ram_code;
}

static void load_cart(struct gb_cart* cart)
{
    memset(cart, 0, sizeof(*cart));
    init_gb_cart(cart,
            NULL, init_rom, release_rom,
            NULL, init_ram, release_ram,
            NULL, NULL, NULL, NULL, NULL, NULL);
    poweron_gb_cart(cart);
}

static void unload_cart(void)
{
    free(s_rom.data);
    free(s_ram.data);
    memset(&s_rom, 0, sizeof(s_rom));
    memset(&s_ram, 0, sizeof(s_ram));
}

static void write8(struct gb_cart* cart, uint16_t address, uint8_t value)
{
    write_gb_cart(cart, address, &value, 1);
}

static uint8_t read8(struct gb_cart* cart, uint16_t address)
{
    uint8_t value = 0xee;
    read_gb_cart(cart, address, &value, 1);
    return value;
}

static void check_rom_banks(struct gb_cart* cart, const char* name, unsigned banks)
{
    unsigned bank;

    CHECK(cart->read_gb_cart != NULL, "%s: cart did not load", name);
    if (cart->read_gb_cart == NULL)
        return;

    for (bank = 1; bank < banks; ++bank)
    {
        uint8_t expect = (uint8_t)(bank ^ 0x5a);
        uint8_t lo, hi;

        write8(cart, 0x2000, (uint8_t)bank);
        lo = read8(cart, 0x4000);
        hi = read8(cart, 0x7fff);
        CHECK(lo == expect && hi == expect,
              "%s: ROM bank %u read %02x/%02x, expected %02x",
              name, bank, lo, hi, expect);
    }
}

static void test_mbc5_rom(void)
{
    struct gb_cart cart;

    make_rom(0x100000, 0x19, 0x00);
    load_cart(&cart);
    check_rom_banks(&cart, "MBC5", 64);
    unload_cart();
}

static void test_mbc1_rom(void)
{
    struct gb_cart cart;

    make_rom(0x80000, 0x01, 0x00);
    load_cart(&cart);
    check_rom_banks(&cart, "MBC1", 32);
    unload_cart();
}

static void test_camera_ram(void)
{
    struct gb_cart cart;
    unsigned low;

    make_rom(0x80000, 0xfc, 0x04);
    load_cart(&cart);
    CHECK(cart.read_gb_cart != NULL && s_ram.size == 0x20000,
          "camera: cart did not load with 128 KiB RAM");
    if (cart.read_gb_cart == NULL || s_ram.size != 0x20000)
    {
        unload_cart();
        return;
    }

    write8(&cart, 0x0000, 0x0a);

    /* banks low and low + 8 sit 0x10000 apart */
    for (low = 0; low < 8; ++low)
    {
        unsigned high = low + 8;
        uint8_t got_low, got_high;

        write8(&cart, 0x4000, (uint8_t)low);
        write8(&cart, 0xa123, (uint8_t)(0x10 + low));
        write8(&cart, 0x4000, (uint8_t)high);
        write8(&cart, 0xa123, (uint8_t)(0x80 + high));

        got_high = read8(&cart, 0xa123);
        write8(&cart, 0x4000, (uint8_t)low);
        got_low = read8(&cart, 0xa123);

        CHECK(got_high == 0x80 + high,
              "camera: RAM bank %u read %02x, expected %02x", high, got_high, 0x80 + high);
        CHECK(got_low == 0x10 + low,
              "camera: RAM bank %u read %02x after a bank %u write, expected %02x",
              low, got_low, high, 0x10 + low);
        CHECK(s_ram.data[high * 0x2000 + 0x123] == 0x80 + high,
              "camera: bank %u write missed its RAM offset %05x", high, high * 0x2000 + 0x123);
    }

    unload_cart();
}

int main(void)
{
    test_mbc5_rom();
    test_mbc1_rom();
    test_camera_ram();

    if (s_failures)
    {
        fprintf(stderr, "test_gb_cart_banks: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_gb_cart_banks: all checks passed\n");
    return 0;
}
