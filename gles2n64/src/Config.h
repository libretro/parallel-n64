#ifndef CONFIG_H
#define CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __LIBRETRO__ // Prefix symbol
#define config gln64config
#endif

#include <stdbool.h>

typedef struct
{
    int     version;

    struct
    {
        int flipVertical, width, height;
    } screen;

    struct
    {
        int force, width, height;
    } video;

    struct
    {
        int maxAnisotropy;
        int enableMipmap;
        int forceBilinear;
        int sai2x;
        int useIA;
        int fastCRC;
        int pow2;
    } texture;

    int     updateMode;
    int     forceBufferClear;
    int     zHack;

    int     enableFog;
    int     enablePrimZ;
    int     enableLighting;
    int     enableAlphaTest;
    int     enableClipping;
    int     enableFaceCulling;
    int     enableNoise;

// paulscode: removed from pre-compile to a config option
//// (part of the Galaxy S Zelda crash-fix
    int     tribufferOpt;
//

    int     hackBanjoTooie;
    int     hackZelda;
    int     hackAlpha;

    bool    stretchVideo;
    bool    romPAL;    //is the rom PAL
    char    romName[21];
} Config;

extern Config config;

void Config_gln64_LoadConfig(void);
void Config_gln64_LoadRomConfig(unsigned char* header);

#ifdef __cplusplus
}
#endif

#endif
