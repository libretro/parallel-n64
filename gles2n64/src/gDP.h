#ifndef GDP_H
#define GDP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHANGED_RENDERMODE      0x0001
#define CHANGED_CYCLETYPE       0x0002
#define CHANGED_SCISSOR         0x0004
#define CHANGED_TMEM            0x0008
#define CHANGED_TILE            0x0010
#define CHANGED_COMBINE_COLORS  0x020
#define CHANGED_COMBINE			  0x040
#define CHANGED_ALPHACOMPARE	  0x080
#define CHANGED_FOGCOLOR		  0x100

#define CHANGED_FB_TEXTURE      0x0200

#define TEXTUREMODE_NORMAL          0
#define TEXTUREMODE_TEXRECT         1
#define TEXTUREMODE_BGIMAGE         2
#define TEXTUREMODE_FRAMEBUFFER     3
/* New GLiden64 define */
#define TEXTUREMODE_FRAMEBUFFER_BG	4

#define LOADTYPE_BLOCK          0
#define LOADTYPE_TILE           1

typedef struct
{
    union
    {
        struct
        {
            // muxs1
            unsigned    aA1     : 3;
            unsigned    sbA1    : 3;
            unsigned    aRGB1   : 3;
            unsigned    aA0     : 3;
            unsigned    sbA0    : 3;
            unsigned    aRGB0   : 3;
            unsigned    mA1     : 3;
            unsigned    saA1    : 3;
            unsigned    sbRGB1  : 4;
            unsigned    sbRGB0  : 4;

            // muxs0
            unsigned    mRGB1   : 5;
            unsigned    saRGB1  : 4;
            unsigned    mA0     : 3;
            unsigned    saA0    : 3;
            unsigned    mRGB0   : 5;
            unsigned    saRGB0  : 4;
        };

        struct
        {
            uint32_t         muxs1, muxs0;
        };

        uint64_t             mux;
    };
} gDPCombine;

struct FrameBuffer;
typedef struct 
{
    uint32_t format, size, line, tmem, palette;

    union
    {
        struct
        {
            unsigned    mirrort : 1;
            unsigned    clampt  : 1;
            unsigned    pad0    : 30;

            unsigned    mirrors : 1;
            unsigned    clamps  : 1;
            unsigned    pad1    : 30;
        };

        struct
        {
            uint32_t cmt, cms;
        };
    };

    uint32_t maskt, masks;
    uint32_t shiftt, shifts;
    float fuls, fult, flrs, flrt;
    uint32_t uls, ult, lrs, lrt;

    uint32_t textureMode;
    uint32_t loadType;
    uint32_t imageAddress;
    struct FrameBuffer *frameBuffer;
} gDPTile;

struct gDPLoadTileInfo {
	uint8_t size;
	uint8_t loadType;
	uint16_t uls;
	uint16_t ult;
	uint16_t width;
	uint16_t height;
	uint16_t texWidth;
	uint32_t texAddress;
	uint32_t dxt;
};

typedef struct
{
    struct
    {
        union
        {
            struct
            {
                unsigned int alphaCompare : 2;
                unsigned int depthSource : 1;

//              struct
//              {
                    unsigned int AAEnable : 1;
                    unsigned int depthCompare : 1;
                    unsigned int depthUpdate : 1;
                    unsigned int imageRead : 1;
                    unsigned int clearOnCvg : 1;

                    unsigned int cvgDest : 2;
                    unsigned int depthMode : 2;

                    unsigned int cvgXAlpha : 1;
                    unsigned int alphaCvgSel : 1;
                    unsigned int forceBlender : 1;
                    unsigned int textureEdge : 1;
//              } renderMode;

                //struct
                //{
                    unsigned int c2_m2b : 2;
                    unsigned int c1_m2b : 2;
                    unsigned int c2_m2a : 2;
                    unsigned int c1_m2a : 2;
                    unsigned int c2_m1b : 2;
                    unsigned int c1_m1b : 2;
                    unsigned int c2_m1a : 2;
                    unsigned int c1_m1a : 2;
                //} blender;

                unsigned int blendMask : 4;
                unsigned int alphaDither : 2;
                unsigned int colorDither : 2;

                unsigned int combineKey : 1;
                unsigned int textureConvert : 3;
                unsigned int textureFilter : 2;
                unsigned int textureLUT : 2;

                unsigned int textureLOD : 1;
                unsigned int textureDetail : 2;
                unsigned int texturePersp : 1;
                unsigned int cycleType : 2;
                unsigned int unusedColorDither : 1; // unsupported
                unsigned int pipelineMode : 1;

                unsigned int pad : 8;

            };

            uint64_t         _u64;

            struct
            {
                uint32_t         l, h;
            };
        };
    } otherMode;

    gDPCombine combine;

    gDPTile tiles[8], *loadTile;

    struct Color
    {
       float r, g, b, a;
    } fogColor,  blendColor, envColor;

    struct
    {
        float z, dz;
        float r, g, b, a;
        uint32_t color;
    } fillColor;

    struct
    {
        uint32_t m;
        float l, r, g, b, a;
    } primColor;

    struct
    {
        float z, deltaZ;
    } primDepth;

    struct
    {
        uint32_t format, size, width, bpl;
        uint32_t address;
    } textureImage;

    struct
    {
        uint32_t format, size, width, height, bpl;
        uint32_t address, changed;
        uint32_t depthImage;
    } colorImage;

    uint32_t depthImageAddress;

    struct
    {
        uint32_t mode;
        float ulx, uly, lrx, lry;
    } scissor;

    struct
    {
        float k0, k1, k2, k3, k4, k5;
    } convert;

    struct
    {
        struct
        {
            float r, g, b, a;
        } center, scale, width;
    } key;

    struct
    {
       uint32_t width, height;
    } texRect;

    uint32_t changed;

    uint16_t TexFilterPalette[512];
    uint32_t paletteCRC16[16];
    uint32_t paletteCRC256;
    uint32_t half_1, half_2;

	 struct gDPLoadTileInfo loadInfo[512];
} gDPInfo;

extern gDPInfo gDP;

void gln64gDPSetOtherMode( uint32_t mode0, uint32_t mode1 );
void gln64gDPSetPrimDepth( uint16_t z, uint16_t dz );
void gln64gDPPipelineMode( uint32_t mode );
void gln64gDPSetCycleType( uint32_t type );
void gln64gDPSetTexturePersp( uint32_t enable );
void gln64gDPSetTextureLUT( uint32_t mode );
void gln64gDPSetCombineKey( uint32_t type );
void gln64gDPSetAlphaCompare( uint32_t mode );
void gln64gDPSetCombine( int32_t muxs0, int32_t muxs1 );
void gDPSetColorImage( uint32_t format, uint32_t size, uint32_t width, uint32_t address );
void gDPSetTextureImage( uint32_t format, uint32_t size, uint32_t width, uint32_t address );
void gDPSetDepthImage( uint32_t address );
void gDPSetEnvColor( uint32_t r, uint32_t g, uint32_t b, uint32_t a );
void gDPSetBlendColor( uint32_t r, uint32_t g, uint32_t b, uint32_t a );
void gDPSetFogColor( uint32_t r, uint32_t g, uint32_t b, uint32_t a );
void gDPSetFillColor( uint32_t c );
void gDPSetPrimColor( uint32_t m, uint32_t l, uint32_t r, uint32_t g, uint32_t b, uint32_t a );
void gDPSetTile(
    uint32_t format, uint32_t size, uint32_t line, uint32_t tmem, uint32_t tile, uint32_t palette, uint32_t cmt,
    uint32_t cms, uint32_t maskt, uint32_t masks, uint32_t shiftt, uint32_t shifts );
void gln64gDPSetTileSize( uint32_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t lrt );
void gln64gDPLoadTile( uint32_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t lrt );
void gln64gDPLoadBlock( uint32_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t dxt );
void gln64gDPLoadTLUT( uint32_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t lrt );
void gln64gDPSetScissor( uint32_t mode, float ulx, float uly, float lrx, float lry );
void gln64gDPFillRectangle( int32_t ulx, int32_t uly, int32_t lrx, int32_t lry );
void gln64gDPSetConvert( int32_t k0, int32_t k1, int32_t k2, int32_t k3, int32_t k4, int32_t k5 );
void gln64gDPSetKeyR( uint32_t cR, uint32_t sR, uint32_t wR );
void gln64gDPSetKeyGB(uint32_t cG, uint32_t sG, uint32_t wG, uint32_t cB, uint32_t sB, uint32_t wB );
void gln64gDPTextureRectangle( float ulx, float uly, float lrx, float lry, int32_t tile, float s, float t, float dsdx, float dtdy );
void gln64gDPTextureRectangleFlip( float ulx, float uly, float lrx, float lry, int32_t tile, float s, float t, float dsdx, float dtdy );
void gln64gDPFullSync(void);
void gln64gDPTileSync(void);
void gln64gDPPipeSync(void);
void gln64gDPLoadSync(void);
void gln64gDPNoOp(void);

void gln64gDPTriFill(uint32_t w0, uint32_t w1);
void gln64gDPTriFillZ(uint32_t w0, uint32_t w1);
void gln64gDPTriShadeZ(uint32_t w0, uint32_t w1);
void gln64gDPTriTxtrZ(uint32_t w0, uint32_t w1);
void gln64gDPTriTxtr(uint32_t w0, uint32_t w1);
void gln64gDPTriShadeTxtrZ(uint32_t w0, uint32_t w1);
void gln64gDPTriShadeTxtr(uint32_t w0, uint32_t w1);

#ifdef __cplusplus
}
#endif

#endif

