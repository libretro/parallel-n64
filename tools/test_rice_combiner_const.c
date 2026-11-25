/* Rice's combiner constant factors (mupen64plus-video-rice/src/CombinerConst.c):
 *
 *   * CalculateConstFactor: a whole (A-B)*C+D per channel on bytes, the
 *     colour from the first equation, the alpha from the second: inputs 0,
 *     1, K5, the primitive and environment colours, the shade colour for
 *     the rest, Conker's unknown input as 1; each complemented and
 *     alpha-replicated; the subtraction clamped at 0, the product over
 *     256, the sum clamped at 255;
 *   * Rice_GetConstFactor: one source per channel, the shade colour for 0
 *     and unknown sources, 0 forced, 1, the primitive and environment
 *     colours and their alphas, the two LOD fractions; complemented, the
 *     colour alpha-replicated.
 *
 * The table's results are what the C++ IColor code these replace gave for
 * the same inputs, on a little-endian host (a colour's bytes are worked
 * on in memory order). */
#include <stdio.h>

#include "Config.h"
#include "CombinerDefs.h"
#include "RenderBase.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

ALIGN(16, RDP_Options gRDP);
GlobalOptions options;

static const struct { int conker; uint32_t a, b, shade, calculated, single; } cases[] = {
   { 0, 0x5bd29bb9, 0x15cb9647, 0x7cb07ab6, 0x7c7c7c7c, 0x304f8549 },
   { 0, 0x911c4085, 0x5bd41410, 0x358ed15c, 0xd493c9f3, 0x353fbfdf },
   { 0, 0x037bdb8e, 0x488f42db, 0xe81c52c5, 0xe8d4d9e4, 0x17a5a5a5 },
   { 1, 0xce135567, 0xc4c84205, 0x49f0a614, 0xddb6b6b6, 0x80303030 },
   { 0, 0x5c57dd0d, 0x0a7114cb, 0x821fb9da, 0x8282a0b1, 0x7f303030 },
   { 0, 0xd9688a88, 0x50063e41, 0x8441fd33, 0xff7b7b7b, 0xffbe02cc },
   { 0, 0x9f994140, 0x59d55c8e, 0xa1f81e2d, 0xa107e1d2, 0xa5a1a1a1 },
   { 1, 0x51cfc713, 0x0edc81e7, 0xf9ca03e5, 0xfdffffff, 0xcfca03e5 },
   { 0, 0x1451ed5e, 0x07de190c, 0x89165c24, 0x3020662e, 0x89898989 },
   { 0, 0x1af849cd, 0x979ccc8f, 0xfd6c9f3c, 0x026c9f3c, 0x58cfcfcf },
   { 0, 0x2fb580d9, 0x9c504cdd, 0xed244caa, 0x12244caa, 0x12121212 },
   { 1, 0xc6614412, 0x8cd28106, 0xb4508516, 0x7f4b4b4b, 0xb4508516 },
   { 0, 0xd8c41a4f, 0xc6538a94, 0x8efeed81, 0x71717176, 0x71a7a7a7 },
   { 0, 0x8c86d8c6, 0xd754bc0d, 0xced0f7a3, 0xaf2f085c, 0x30313131 },
   { 0, 0x048d0af1, 0x53861852, 0x1dc61a45, 0x1dccccb7, 0x1de2e2e2 },
   { 1, 0x8c7b5402, 0x0048d49e, 0xe64351dc, 0x00bcae23, 0x194351dc },
   { 0, 0xc9920840, 0x9352d652, 0x148ecaf6, 0xebebebeb, 0x14141414 },
   { 0, 0x98df015e, 0xc68ada13, 0x339c9d50, 0xcc6362af, 0x33333333 },
   { 0, 0xbc53dd0b, 0x8f107b84, 0x0a57e5e8, 0xffa81a17, 0xf5808080 },
   { 1, 0x89851c1d, 0x805bc8da, 0xfe2e8e41, 0xffd171be, 0x012e8e41 },
   { 0, 0xc48f1152, 0x19ec5e0a, 0xd4cf5e35, 0xd42b75a8, 0xd4d4d4d4 },
   { 0, 0x56d25f11, 0x4f160d97, 0xcb4bbc98, 0xcbcbcbcb, 0x344bbc98 },
   { 0, 0xbc4659d4, 0xde409092, 0xe7684ef6, 0x1897b109, 0x18181818 },
   { 1, 0x9ad7114d, 0x9d58fcd1, 0x6d1ced50, 0x92e312af, 0x92303030 },
   { 0, 0x1fc2934b, 0x2c3491b2, 0x514f4869, 0x514f4869, 0xae808080 },
   { 0, 0x9ac73088, 0xd9015c80, 0xf3048f54, 0x18fb70ab, 0x0cfb70ab },
   { 0, 0xd6f1fcc3, 0xc654d0c5, 0x91c8e23b, 0xb56e6e6e, 0x7f6e6e6e },
   { 1, 0x1445161c, 0x47955459, 0xa3b4c1cd, 0x30b4c1cd, 0xa3b4c1cd },
   { 0, 0xd0c1ce0b, 0x4f56f152, 0x23632446, 0x23000000, 0x23808080 },
   { 0, 0x1d045573, 0xe55bc584, 0x76ca6666, 0x83ca6666, 0x89767676 },
   { 0, 0x5dd001c5, 0x8ecd5100, 0x10a0b0c6, 0xef101010, 0x107f7f7f },
   { 1, 0x4212973a, 0xd11e28ca, 0xa4506b9c, 0x00a4a4c6, 0x5b506b9c },
   { 0, 0x1482d04d, 0xdb101a81, 0x1e8dada3, 0xe19ab6ad, 0x00303030 },
   { 0, 0x1143197f, 0xe15d16d4, 0xc13454c7, 0x009ea6c7, 0x3ec1c1c1 },
   { 0, 0x2144f351, 0x5b11451b, 0x20638a84, 0x20ffffff, 0x20202020 },
   { 1, 0x97170ddd, 0x76b31bcc, 0x7d362ea5, 0x7fd9e05a, 0x82828282 },
   { 0, 0xc54a91d4, 0x524408c0, 0xedabaeea, 0xfd7f7f7f, 0x12121212 },
   { 0, 0xcc5c0f51, 0x0c5dfc54, 0xee7955bb, 0xff7d9f40, 0xeeeeeeee },
   { 0, 0xc63b9cfc, 0xd692c85f, 0xc5e6eb84, 0x59575c3a, 0xc53a3a3a },
   { 1, 0x5390850f, 0x9ccc99c8, 0x9fd2158e, 0x609f9f9f, 0x60a7a7a7 },
   { 0, 0x8b0fd93f, 0xcd4c4608, 0x184034ca, 0xe7bfcb35, 0x184034ca },
   { 0, 0x9a544046, 0x95844976, 0x553b84a8, 0xaae09773, 0x55555555 },
   { 0, 0xa8cbbd9f, 0x961e0d51, 0x8a92d206, 0x756d2df9, 0x8a6d2df9 },
   { 1, 0x2087c24f, 0xdf59df8a, 0xad68e2d2, 0x52380521, 0x52a7a7a7 },
   { 0, 0x858090cb, 0x842ed652, 0x3da1a01b, 0xc2ffffff, 0x3d7f7f7f },
   { 0, 0x9400034a, 0x5e841f1d, 0x971b8694, 0x97ffffff, 0x97979797 },
   { 0, 0xc1c615b0, 0x42991d42, 0xe4a1856b, 0xe4000000, 0xe4000000 },
   { 1, 0x9e41da4d, 0xc0c5d541, 0x46d968f8, 0xff269707, 0xff303030 },
   { 0, 0x59c3111b, 0xd71f1380, 0x5eaa896a, 0xdc5e5e5e, 0xa1aa896a },
   { 0, 0x184d8c89, 0x1dc010c2, 0xb00ca033, 0xb00ca033, 0x4ff35fcc },
   { 0, 0xd24a5ad6, 0x4f8a1681, 0xb09dc2ba, 0xb04f4f4f, 0x004f4f4f },
   { 1, 0x0857d45f, 0xde4d9ddb, 0x8a001551, 0x750b205c, 0x758a8a8a },
   { 0, 0x0691a284, 0xd44c8d07, 0x176b2418, 0xe86b2418, 0x3094dbe7 },
   { 0, 0x1e285d4c, 0xfa56127e, 0x0890ee96, 0xf790ee96, 0x08080808 },
   { 0, 0x0a1cd6f7, 0x4bd957ce, 0x2e0605d6, 0xb30605d6, 0xa5d1d1d1 },
   { 1, 0x848ad8c8, 0x08338a13, 0x01fc6c3b, 0x010393c4, 0x01fefefe },
   { 0, 0x4e891c46, 0x3247d000, 0x379dfa4e, 0x41373737, 0x37373737 },
   { 0, 0x8dcfdf44, 0xc6d5d1f4, 0x07beed73, 0xf841128c, 0xf8070707 },
   { 0, 0xd0f302bb, 0xf65e0d55, 0x1252c03b, 0xed54007e, 0x12ad3fc4 },
   { 1, 0x071ad691, 0x5db2037a, 0x2f4484eb, 0x2f60f0a0, 0x2f000000 },
   { 0, 0x990b8407, 0x446a54b7, 0x0442c027, 0x07bdc3d8, 0xfb60f0a0 },
   { 0, 0xd1b358c4, 0xdbaf9d00, 0x00827045, 0xffffffff, 0x00ffffff },
   { 0, 0xdd564a00, 0x01b0f543, 0x86594371, 0xff797979, 0x86594371 },
   { 1, 0xcdd91a0a, 0x1fcf159d, 0xb8066a00, 0xb8474747, 0x47066a00 },
   { 0, 0x04514dc3, 0x998100df, 0x79241147, 0x862a174d, 0x86868686 },
   { 0, 0xc25286c1, 0x4a105181, 0x1f38579c, 0x1fe0e0e0, 0x00000000 },
   { 0, 0xd4921a99, 0x0b3d8e24, 0x0b400e7f, 0x0bfffff4, 0x0bbff180 },
   { 1, 0x1cc6d918, 0x894adc99, 0x9869b1f6, 0x6769ceff, 0x6769b1f6 },
   { 0, 0xe5c78bdd, 0x96140c91, 0x67016dc5, 0xab7f83cb, 0x98989898 },
   { 0, 0x9a485039, 0x9d88c7dd, 0xdacc92d9, 0x25336d26, 0x25cc92d9 },
   { 0, 0x895ddfd2, 0x1faf1345, 0x378496b7, 0x707b6948, 0x80c8c8c8 },
   { 1, 0xcd16d41e, 0x168a0a0f, 0xd07272c7, 0xd04c4ca5, 0xa77272c7 },
   { 0, 0xd43fdb52, 0x8e874689, 0x9c8cfef7, 0x63829b99, 0x639c9c9c },
   { 0, 0x1cd8805e, 0xc08dcf83, 0x12b116b7, 0xffb116b7, 0xed121212 },
   { 0, 0x5ad90764, 0x415ade1f, 0x0d480315, 0xff0d0d0d, 0x0d0d0d0d },
   { 1, 0x5c10d740, 0x87dd0a0c, 0x5c5fc8e3, 0xcf5c5c5c, 0x5c5c5c5c },
   { 0, 0xd76a0202, 0xb8968197, 0xb3e5553b, 0x62ffa187, 0x4ce5553b },
   { 0, 0x465a56da, 0xc11dc09a, 0x26d2bd2e, 0x00404040, 0xd9d9d9d9 },
   { 0, 0x5f4c0dd5, 0x13cd5fde, 0xa9a83b7f, 0xa9a9baa9, 0x56565656 },
   { 1, 0x0b43b21c, 0x7b9d93dd, 0x20a827d4, 0x20b227e9, 0xdfa827d4 },
   { 0, 0x80f71f9d, 0x164806a3, 0x85758275, 0x85ffffff, 0x7a8a7d8a },
   { 0, 0x02038d88, 0xddd046cf, 0xf8a037e8, 0x07a037e8, 0x585fc817 },
   { 0, 0x88ced786, 0x18384d04, 0xaed034a6, 0xae2ff15b, 0xae2fcb59 },
   { 1, 0x1109de04, 0x0f99d23d, 0xb6320327, 0xd5ffffff, 0xb6320327 },
   { 0, 0xd17fc80c, 0x84d09e8f, 0x60d3e41b, 0x9fb2b89f, 0x58d3e41b },
   { 0, 0x901e0493, 0x926846d0, 0xf3edbb05, 0x0c000004, 0x0c1244fa },
   { 0, 0x8b8bc903, 0x45820a9d, 0xac158a64, 0x80ea8ea5, 0x53158a64 },
   { 1, 0x1d0a4988, 0x5b0799df, 0x5f131167, 0x5f1d1a7d, 0xa0ecee98 },
   { 0, 0x5d059d18, 0x824c4d0a, 0x6ba627e1, 0x94a46b83, 0x6ba627e1 },
   { 0, 0x0646110c, 0x4d5558d0, 0xa6cf18f0, 0xa6cf18f0, 0x59cf18f0 },
   { 0, 0x94cb6698, 0x8d148647, 0x59db9c7b, 0xa624699f, 0x30246384 },
   { 1, 0xd18b0291, 0xdbd84a46, 0x9b4a39ec, 0x64000000, 0x9b000000 },
   { 0, 0x0db35b17, 0xcdf43e05, 0xb6c2c494, 0x49c4c794, 0x80c2c494 },
   { 0, 0x454d8d1c, 0x121a2642, 0xdc2295ff, 0xdc80a4ff, 0xdc2295ff },
   { 0, 0xbc04db51, 0x53099a1c, 0xc7572d1e, 0xffd8ebf1, 0xc7c7c7c7 },
   { 1, 0x9bccda44, 0xe614931c, 0x62a4c1f1, 0x9d5b3e0e, 0x62626262 },
};

int main(void)
{
    unsigned i;
#ifndef MSB_FIRST
    gRDP.primitiveColor = 0x80C04020;
    gRDP.envColor       = 0x3060F0A0;
    gRDP.LODFrac        = 0x5A;
    gRDP.primLODFrac    = 0xA7;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        uint32_t calc, single;
        options.enableHackForGames = cases[i].conker ? HACK_FOR_CONKER : NO_HACK_FOR_GAME;
        calc   = CalculateConstFactor(cases[i].a, cases[i].b, cases[i].shade);
        single = Rice_GetConstFactor(cases[i].a & 0xff, cases[i].b & 0xff, cases[i].shade);
        if (calc != cases[i].calculated || single != cases[i].single)
        {
            printf("FAILED case %u: %08x %08x %08x -> %08x %08x, wanted %08x %08x\n", i, cases[i].a, cases[i].b,
                   cases[i].shade, calc, single, cases[i].calculated, cases[i].single);
            failures++;
        }
    }

    /* the edges: clamps at 0 and 255, the product over 256, complements,
     * alpha replicated, each source of both lists, Conker's unknown input */
    options.enableHackForGames = NO_HACK_FOR_GAME;
    CHECK(CalculateConstFactor(MUX_1 | MUX_SHADE << 8 | MUX_1 << 16 | MUX_0 << 24, 0, 0x10FF2080) == 0x1000DF7F);
    CHECK(CalculateConstFactor(MUX_SHADE | MUX_1 << 8 | MUX_1 << 16 | MUX_0 << 24, 0, 0x10FF2080) == 0x10000000);
    CHECK(CalculateConstFactor(MUX_SHADE | MUX_0 << 8 | MUX_SHADE << 16 | MUX_0 << 24, 0, 0x8040FF80) == 0x8010FE40);
    CHECK(CalculateConstFactor(MUX_SHADE | MUX_0 << 8 | MUX_ENV << 16 | MUX_PRIM << 24, 0, 0x8040FF80) == 0x80D8FF70);
    CHECK(CalculateConstFactor(MUX_0 | MUX_0 << 8 | MUX_0 << 16 | MUX_SHADE << 24, MUX_0 | MUX_0 << 8 | MUX_0 << 16 | MUX_SHADE << 24, 0x12345678) == 0x2468ACF0);
    CHECK(CalculateConstFactor(MUX_PRIM | MUX_0 << 8 | MUX_1 << 16 | MUX_ENV << 24, 0, 0) == 0x00FFFFC0);
    CHECK(CalculateConstFactor((MUX_PRIM | MUX_COMPLEMENT | MUX_ALPHAREPLICATE) | MUX_0 << 8 | MUX_1 << 16 | MUX_0 << 24, 0, 0) == 0x007F7F7F);
    CHECK(CalculateConstFactor(MUX_K5 | MUX_ENV << 8 | MUX_PRIM << 16 | MUX_0 << 24, MUX_ENV | MUX_SHADE << 8 | MUX_PRIM << 16 | MUX_K5 << 24, 0x40404040) == 0xFF77030B);
    CHECK(Rice_GetConstFactor(MUX_PRIM | MUX_ALPHAREPLICATE, MUX_ENV | MUX_COMPLEMENT, 0) == 0xCF808080);
    CHECK(Rice_GetConstFactor(MUX_FORCE_0, MUX_1, 0x55555555) == 0xFFAAAAAA);
    CHECK(Rice_GetConstFactor(MUX_1, MUX_FORCE_0, 0x55555555) == 0xAAFFFFFF);
    CHECK(Rice_GetConstFactor(MUX_0, MUX_0, 0x55555555) == 0x55555555);
    CHECK(Rice_GetConstFactor(MUX_LODFRAC, MUX_PRIMLODFRAC, 0) == 0xA75A5A5A);
    CHECK(Rice_GetConstFactor(MUX_PRIMLODFRAC, MUX_LODFRAC, 0) == 0x5AA7A7A7);
    CHECK(Rice_GetConstFactor(MUX_PRIM_ALPHA, MUX_ENV_ALPHA, 0) == 0x30808080);
    CHECK(Rice_GetConstFactor(MUX_ENV_ALPHA, MUX_PRIM_ALPHA, 0) == 0x80303030);
    CHECK(Rice_GetConstFactor(MUX_ENV, MUX_PRIM, 0) == 0x8060F0A0);
    CHECK(Rice_GetConstFactor(MUX_K5, MUX_K5, 0x01020304) == 0x01FFFFFF);
    CHECK(Rice_GetConstFactor(MUX_UNK, MUX_UNK, 0x01020304) == 0x01020304);
    options.enableHackForGames = HACK_FOR_CONKER;
    CHECK(Rice_GetConstFactor(MUX_UNK, MUX_UNK, 0x01020304) == 0x01FFFFFF);
    CHECK(CalculateConstFactor(MUX_UNK | MUX_0 << 8 | MUX_1 << 16 | MUX_0 << 24, 0, 0x01020304) == 0x01FFFFFF);
    options.enableHackForGames = NO_HACK_FOR_GAME;
    CHECK(CalculateConstFactor(MUX_UNK | MUX_0 << 8 | MUX_1 << 16 | MUX_0 << 24, 0, 0x01020304) == 0x01020304);
#endif

    if (failures)
        return 1;
    printf("test_rice_combiner_const: ok\n");
    return 0;
}
