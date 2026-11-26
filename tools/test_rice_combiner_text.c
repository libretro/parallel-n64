/* Rice's combiner programs as text (mupen64plus-video-rice/src/
 * RiceCombinerText.c): each decoded mux's GLSL, the texels it samples.
 *
 * The table's hashes are of what the C++ GenerateProgramStr this replaces
 * gave for the same muxes: each (stage, kind) a random search met, with
 * and without Simplify. */
#include <stdio.h>
#include <string.h>

#include "Config.h"
#include "DecodedMux.h"
#include "RenderBase.h"
#include "RiceCombinerText.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

ALIGN(16, RSP_Options gRSP);
GlobalOptions options;
GameSetting g_curRomInfo;

static const struct
{
    uint32_t mux0, mux1;
    int simplify;
    uint32_t hash;
    int tex0, tex1;
} cases[] = {
   { 0x00454419, 0x3c00f93a, 1, 0xe94c4656u, 1, 1 },
   { 0x001122b8, 0x3081edfb, 0, 0x9dbbd001u, 1, 1 },
   { 0x007ad14d, 0x0431dae4, 0, 0xa09a8c03u, 1, 0 },
   { 0x00d488a1, 0x42d41de1, 1, 0x47ad8933u, 1, 1 },
   { 0x008b2b13, 0x38c83507, 1, 0xe7c7b51bu, 1, 1 },
   { 0x008a28ee, 0xca671005, 0, 0xb8d7a896u, 1, 1 },
   { 0x00759090, 0x879b4143, 0, 0x62b273acu, 1, 0 },
   { 0x0038fc0b, 0x4d8bbbb7, 1, 0xb697de8fu, 0, 1 },
   { 0x000c1d34, 0x8ef6fcc1, 0, 0x774a6b01u, 1, 1 },
   { 0x00fc5046, 0xe6696bbc, 1, 0x3978f5ffu, 0, 1 },
   { 0x00641e91, 0xac7e3a8a, 1, 0x277b2af5u, 1, 1 },
   { 0x0035a89e, 0x2be7eead, 0, 0x518e738au, 1, 1 },
   { 0x009e8cfb, 0x5f37dc1b, 1, 0x6057f940u, 1, 1 },
   { 0x00860c03, 0x5bc84180, 0, 0xa2c44b96u, 1, 1 },
   { 0x000fd1af, 0x5f79abf2, 1, 0xf47c0b60u, 0, 1 },
   { 0x00432cae, 0x8202b865, 0, 0x27e4ce48u, 1, 1 },
   { 0x004c1c81, 0x5f57830f, 0, 0xb1dafcd5u, 1, 1 },
   { 0x0014b4e3, 0xca99adc7, 1, 0x5faeef46u, 1, 1 },
   { 0x0028c415, 0xd4e88cc0, 0, 0xc7ef179bu, 1, 1 },
   { 0x00ac6e87, 0x02e7df19, 1, 0x7fec827du, 1, 1 },
   { 0x008a68d1, 0xd2953bb9, 0, 0x30f5be84u, 1, 1 },
   { 0x0073161a, 0x34bc8f02, 0, 0x6dce4b51u, 1, 1 },
   { 0x009327cf, 0x3433a43a, 0, 0x2fb6454cu, 1, 1 },
   { 0x00a31634, 0x1cd65ec6, 1, 0xbae48519u, 1, 0 },
   { 0x00b437f0, 0x850c0b75, 0, 0x02b5802au, 1, 0 },
   { 0x003974e6, 0xfa11d534, 1, 0xef6c14b5u, 0, 1 },
   { 0x009fcb34, 0xc5d9cf48, 0, 0x70663348u, 1, 1 },
   { 0x0023a75c, 0x97c80738, 1, 0x6c8c96bbu, 0, 1 },
   { 0x006d5447, 0x0d503a15, 0, 0x602573a1u, 0, 1 },
   { 0x0024be06, 0xa26c6ff4, 1, 0x90ab185du, 0, 1 },
   { 0x00cd2d66, 0xcf562616, 0, 0xaa91fcb8u, 0, 0 },
   { 0x00619ed0, 0x4fcde32d, 1, 0x406e67fbu, 0, 0 },
};

static uint32_t fnv(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s)
    {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

/* the stack the text is built on left dirty, so text that is not
 * cleared first shows */
static void dirty_stack(void)
{
    volatile char junk[3 * RICE_COMBINER_TEXT_SIZE];
    unsigned i;
    for (i = 0; i < sizeof(junk); i++)
        junk[i] = (char)('a' + i % 26);
}

static void text_of(uint32_t mux0, uint32_t mux1, int simplify, char *out, bool *t0, bool *t1)
{
    DecodedMux mux;
    dirty_stack();
    DecodedMux_Init(&mux);
    DecodedMux_Decode(&mux, mux0, mux1);
    if (simplify)
        DecodedMux_Simplify(&mux);
    RiceCombiner_ProgramText(&mux, out, t0, t1);
}

int main(void)
{
    static char text[RICE_COMBINER_TEXT_SIZE];
    unsigned i;
    bool t0, t1;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        text_of(cases[i].mux0, cases[i].mux1, cases[i].simplify, text, &t0, &t1);
        if (fnv(text) != cases[i].hash || t0 != cases[i].tex0 || t1 != cases[i].tex1)
        {
            printf("FAILED case %u (%08x %08x %d): hash %08x tex %d %d\n", i,
                  cases[i].mux0, cases[i].mux1, cases[i].simplify, fnv(text), t0, t1);
            failures++;
        }
        CHECK((strstr(text, "#define NEED_TEX0\n") == text) == t0);
        CHECK((strstr(text, "#define NEED_TEX1\n") != NULL) == t1);
    }

    /* TEXEL0 * SHADE, then alpha TEXEL1 * TEXEL0, then SHADE * COMBINED */
    text_of(0x00127e04, 0xff0bf3ff, 1, text, &t0, &t1);
    CHECK(t0 && t1);
    CHECK(strstr(text, "comb.rgb = t0.bgr * vShadeColor.rgb;\n"
                       "comb.a = t1.a * t0.a;\n"
                       "comb.rgb = vShadeColor.rgb * comb.rgb;\n") != NULL);
    CHECK(strstr(text, "#ifdef FOG") != NULL && strstr(text, "#ifdef ALPHA_TEST") != NULL);
    CHECK(strspn(strrchr(text, '}') + 1, " \n") == strlen(strrchr(text, '}') + 1));
    /* either texel flag pointer may be NULL */
    text_of(0x00127e04, 0xff0bf3ff, 1, text, NULL, NULL);
    CHECK(fnv(text) != 0);

    printf("test_rice_combiner_text: %s (%u cases)\n", failures ? "FAILED" : "ok",
          (unsigned)(sizeof(cases) / sizeof(cases[0])));
    return failures != 0;
}
