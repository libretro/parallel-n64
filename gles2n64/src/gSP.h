#ifndef GSP_H
#define GSP_H

#include <stdint.h>
#include <boolean.h>
#include "GBI.h"
#include "gDP.h"

#define CHANGED_VIEWPORT        0x01
#define CHANGED_MATRIX          0x02
#define CHANGED_COLORBUFFER     0x04
#define CHANGED_GEOMETRYMODE    0x08
#define CHANGED_TEXTURE         0x10
#define CHANGED_FOGPOSITION     0x20
#define CHANGED_LIGHT			  0x40
#define CHANGED_CPU_FB_WRITE	  0x80

#define CLIP_X      0x03
#define CLIP_NEGX   0x01
#define CLIP_POSX   0x02

#define CLIP_Y      0x0C
#define CLIP_NEGY   0x04
#define CLIP_POSY   0x08

#define CLIP_Z      0x10

#define CLIP_ALL    0x1F // CLIP_NEGX|CLIP_POSX|CLIP_NEGY|CLIP_POSY|CLIP_Z

typedef struct
{
    float     x, y, z, w;
    float     nx, ny, nz, __pad0;
    float     r, g, b, a;
    float flat_r, flat_g, flat_b, flat_a;
    float     s, t;
    uint8_t HWLight;
    int16_t     flag;
    uint32_t     clip;
} SPVertex;

typedef struct SPLight
{
	float r, g, b;
	float x, y, z;
	float posx, posy, posz, posw;
	float ca, la, qa;
} SPLight;

typedef struct
{
    uint32_t segment[16];

    struct
    {
        uint32_t modelViewi, stackSize, billboard;
        float modelView[32][4][4];
        float projection[4][4];
        float combined[4][4];
    } matrix;

    struct
    {
        float A, B, C, D;
        float X, Y;
        float baseScaleX, baseScaleY;
    } objMatrix;

    uint32_t objRendermode;

    uint32_t vertexColorBase;
    uint32_t vertexi;

    struct SPLight lights[12];
    SPLight lookat[2];
    bool lookatEnable;

    struct
    {
        float scales, scalet;
        int32_t level, on, tile;
    } texture;

    gDPTile *textureTile[2];

    struct
    {
        float vscale[4];
        float vtrans[4];
        float x, y, width, height;
        float nearz, farz;
    } viewport;

    struct
    {
        int16_t multiplier, offset;
    } fog;

    struct
    {
        uint32_t address, width, height, format, size, palette;
        float imageX, imageY, scaleW, scaleH;
    } bgImage;

    uint32_t geometryMode;
    int32_t numLights;

    uint32_t changed;

    uint32_t status[4];

    struct
    {
       uint32_t vtx, mtx, tex_offset, tex_shift, tex_count;
    } DMAOffsets;

	// CBFD
	uint32_t vertexNormalBase;
	float vertexCoordMod[16];
} gSPInfo;

extern gSPInfo gSP;

void gSPLoadUcodeEx( uint32_t uc_start, uint32_t uc_dstart, uint16_t uc_dsize );
void gSPNoOp();
void gSPMatrix( uint32_t matrix, uint8_t param );
void gSPDMAMatrix( uint32_t matrix, uint8_t index, uint8_t multiply );
void gSPViewport( uint32_t v );
void gSPForceMatrix( uint32_t mptr );
void gSPLight( uint32_t l, int32_t n );
void gSPLightCBFD( uint32_t l, int32_t n );
void gSPLookAt( uint32_t _l, uint32_t _n );
void gSPVertex( uint32_t v, uint32_t n, uint32_t v0 );
void gSPCIVertex( uint32_t v, uint32_t n, uint32_t v0 );
void gSPDMAVertex( uint32_t v, uint32_t n, uint32_t v0 );
void gSPCBFDVertex( uint32_t v, uint32_t n, uint32_t v0 );
void gSPDisplayList( uint32_t dl );
void gSPDMADisplayList( uint32_t dl, uint32_t n );
void gSPBranchList( uint32_t dl );
void gSPBranchLessZ( uint32_t branchdl, uint32_t vtx, float zval );
void gSPDlistCount(uint32_t count, uint32_t v);
void gSPSprite2DBase( uint32_t base );
void gSPDMATriangles( uint32_t tris, uint32_t n );
void gSP1Quadrangle( int32_t v0, int32_t v1, int32_t v2, int32_t v3 );
void gSPCullDisplayList( uint32_t v0, uint32_t vn );
void gSPPopMatrix( uint32_t param );
void gSPPopMatrixN( uint32_t param, uint32_t num );
void gSPSegment( int32_t seg, int32_t base );
void gSPClipRatio( uint32_t r );
void gSPInsertMatrix( uint32_t where, uint32_t num );
void gSPModifyVertex( uint32_t vtx, uint32_t where, uint32_t val );
void gSPNumLights( int32_t n );
void gSPLightColor( uint32_t lightNum, uint32_t packedColor );
void gSPFogFactor( int16_t fm, int16_t fo );
void gSPPerspNormalize( uint16_t scale );
void gSPTexture( float sc, float tc, int32_t level, int32_t tile, int32_t on );
void gSPEndDisplayList();
void gSPGeometryMode( uint32_t clear, uint32_t set );
void gSPSetGeometryMode( uint32_t mode );
void gSPClearGeometryMode( uint32_t mode );
void gSPSetOtherMode_H(uint32_t _length, uint32_t _shift, uint32_t _data);
void gSPSetOtherMode_L(uint32_t _length, uint32_t _shift, uint32_t _data);
void gSPLine3D( int32_t v0, int32_t v1, int32_t flag );
void gSPLineW3D( int32_t v0, int32_t v1, int32_t wd, int32_t flag );
void gSPObjRectangle( uint32_t sp );
void gSPObjRectangleR(uint32_t _sp);
void gSPObjSprite( uint32_t sp );
void gSPObjLoadTxtr( uint32_t tx );
void gSPObjLoadTxSprite( uint32_t txsp );
void gSPObjLoadTxRectR( uint32_t txsp );
void gSPBgRect1Cyc( uint32_t bg );
void gSPBgRectCopy( uint32_t bg );
void gSPObjMatrix( uint32_t mtx );
void gSPObjSubMatrix( uint32_t mtx );
void gSPObjRendermode(uint32_t _mode);
void gSPSetDMAOffsets( uint32_t mtxoffset, uint32_t vtxoffset );
void gSPSetVertexColorBase( uint32_t base );
void gSPSetVertexNormaleBase( uint32_t base );
void gSPProcessVertex(uint32_t v);
void gSPCoordMod(uint32_t _w0, uint32_t _w1);
void gSPCombineMatrices(void);

void gSPTriangleUnknown(void);

void gSPTriangle(int32_t v0, int32_t v1, int32_t v2);
void gSP1Triangle(int32_t v0, int32_t v1, int32_t v2);
void gSP2Triangles(const int32_t v00, const int32_t v01, const int32_t v02, const int32_t flag0,
                    const int32_t v10, const int32_t v11, const int32_t v12, const int32_t flag1 );
void gSP4Triangles(const int32_t v00, const int32_t v01, const int32_t v02,
                    const int32_t v10, const int32_t v11, const int32_t v12,
                    const int32_t v20, const int32_t v21, const int32_t v22,
                    const int32_t v30, const int32_t v31, const int32_t v32 );


extern void (*gSPTransformVertex)(float vtx[4], float mtx[4][4]);
extern void (*gSPLightVertex)(SPVertex * _vtx);
extern void (*gSPPointLightVertex)(SPVertex *_vtx, float * _vPos);
extern void (*gSPBillboardVertex)(uint32_t v, uint32_t i);
void gSPSetupFunctions(void);
void gSPSetDMATexOffset(uint32_t _addr);

#endif

