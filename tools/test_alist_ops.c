/* test_alist_ops -- audio list arithmetic and buffer commands match the
 * microcode.
 *
 *  - the ENVMIXER VMADN low clamp saturates on the whole of ACC[47:16],
 *    including values whose low half alone looks in range;
 *  - NEAD CLEARBUFF clears whole 16-byte blocks and takes a 16-bit count.
 *
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include "../mupen64plus-rsp-hle/src/alist.c"
#include "../mupen64plus-rsp-hle/src/alist_nead.c"

#include <stdio.h>
#include <string.h>

void HleVerboseMessage(void* user_defined, const char *message, ...)
{
    (void)user_defined; (void)message;
}
void HleWarnMessage(void* user_defined, const char *message, ...)
{
    (void)user_defined; (void)message;
}
void rsp_break(struct hle_t* hle, unsigned int setbits)
{
    (void)hle; (void)setbits;
}

static int s_failures;

#define CHECK(cond, what) \
    do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

static void test_clamp_low(void)
{
    CHECK(ev_clamp_al(((int64_t)0x17fff << 16) | 0x1234) == (int16_t)0xffff,
          "VMADN: ACC[47:16] = 0x17fff saturates high");
    CHECK(ev_clamp_al(-((int64_t)0x18000 * 65536) + 0x1234) == 0,
          "VMADN: ACC[47:16] = -0x18000 saturates low");
    CHECK(ev_clamp_al(((int64_t)0x8000 << 16) | 0x1234) == (int16_t)0xffff,
          "VMADN: ACC[47:16] just past +32767 saturates high");
    CHECK(ev_clamp_al(-65536 + 0x1234) == 0x1234,
          "VMADN: an in-range ACC[47:16] passes the low half through");
}

static void test_nead_clearbuff(void)
{
    static struct hle_t hle;
    unsigned i;
    int ok = 1;

    memset(hle.alist_buffer, 0xaa, sizeof(hle.alist_buffer));
    CLEARBUFF(&hle, 0x100, 0x18);
    for (i = 0x100; i < 0x120; i++)
        if (*alist_u8(&hle, (uint16_t)i) != 0)
            ok = 0;
    CHECK(ok && *alist_u8(&hle, 0x120) == 0xaa && *alist_u8(&hle, 0xff) == 0xaa,
          "NEAD CLEARBUFF: a partial block clears sixteen bytes");

    memset(hle.alist_buffer, 0xaa, sizeof(hle.alist_buffer));
    CLEARBUFF(&hle, 0x000, 0x1000);
    ok = 1;
    for (i = 0; i < 0x1000; i++)
        if (*alist_u8(&hle, (uint16_t)i) != 0)
            ok = 0;
    CHECK(ok, "NEAD CLEARBUFF: the count is sixteen bits wide");
}

int main(void)
{
    test_clamp_low();
    test_nead_clearbuff();

    if (s_failures) {
        printf("test_alist_ops: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("test_alist_ops: all checks passed\n");
    return 0;
}
