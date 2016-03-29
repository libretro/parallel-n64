#ifndef GSP_H
#define GSP_H

#include <stdint.h>

#include "GBI.h"
#include "gDP.h"

#define CHANGED_VIEWPORT		0x01
#define CHANGED_MATRIX			0x02
#define CHANGED_GEOMETRYMODE	0x08
#define CHANGED_TEXTURE			0x10
#define CHANGED_FOGPOSITION		0x20
#define CHANGED_LIGHT			0x40
#define CHANGED_TEXTURESCALE	0x80

#define CLIP_X      0x03
#define CLIP_NEGX   0x01
#define CLIP_POSX   0x02

#define CLIP_Y      0x0C
#define CLIP_NEGY   0x04
#define CLIP_POSY   0x08

#define CLIP_Z      0x10

#define CLIP_ALL	0x1F // CLIP_NEGX|CLIP_POSX|CLIP_NEGY|CLIP_POSY|CLIP_Z

#define SC_POSITION             1
#define SC_COLOR                2
#define SC_TEXCOORD0            3
#define SC_TEXCOORD1            4
#define SC_NUMLIGHTS            5

struct SPVertex
{
	float x, y, z, w;
	float nx, ny, nz, __pad0;
	float r, g, b, a;
	float flat_r, flat_g, flat_b, flat_a;
	float s, t;
	uint8_t HWLight;
	int16_t flag;
	uint32_t clip;
};

struct SPLight
{
	float r, g, b;
	float x, y, z;
	float posx, posy, posz, posw;
	float ca, la, qa;
};

struct gSPInfo
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

	SPLight lights[12];
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
};

extern gSPInfo gSP;

void gln64gSPLoadUcodeEx( uint32_t uc_start, uint32_t uc_dstart, uint16_t uc_dsize );
void gln64gSPNoOp();
void gln64gSPMatrix( uint32_t matrix, uint8_t param );
void gln64gSPDMAMatrix( uint32_t matrix, uint8_t index, uint8_t multiply );
void gln64gSPViewport( uint32_t v );
void gln64gSPForceMatrix( uint32_t mptr );
void gln64gSPLight( uint32_t l, int32_t n );
void gln64gSPLightCBFD( uint32_t l, int32_t n );
void gln64gSPLookAt( uint32_t l, uint32_t n );
void gln64gSPVertex( uint32_t v, uint32_t n, uint32_t v0 );
void gln64gSPCIVertex( uint32_t v, uint32_t n, uint32_t v0 );
void gln64gSPDMAVertex( uint32_t v, uint32_t n, uint32_t v0 );
void gln64gSPCBFDVertex( uint32_t v, uint32_t n, uint32_t v0 );
void gln64gSPDisplayList( uint32_t dl );
void gln64gSPBranchList( uint32_t dl );
void gln64gSPBranchLessZ( uint32_t branchdl, uint32_t vtx, float zval );
void gln64gSPDlistCount(uint32_t count, uint32_t v);
void gln64gSPSprite2DBase(uint32_t _base );
void gln64gSPDMATriangles( uint32_t tris, uint32_t n );
void gln64gSP1Quadrangle( int32_t v0, int32_t v1, int32_t v2, int32_t v3 );
void gln64gSPCullDisplayList( uint32_t v0, uint32_t vn );
void gln64gSPPopMatrix( uint32_t param );
void gln64gSPPopMatrixN( uint32_t param, uint32_t num );
void gln64gSPSegment( int32_t seg, int32_t base );
void gln64gSPClipRatio( uint32_t r );
void gln64gSPInsertMatrix( uint32_t where, uint32_t num );
void gln64gSPModifyVertex(uint32_t _vtx, uint32_t _where, uint32_t _val );
void gln64gSPNumLights( int32_t n );
void gln64gSPLightColor( uint32_t lightNum, uint32_t packedColor );
void gln64gSPFogFactor( int16_t fm, int16_t fo );
void gln64gSPPerspNormalize( uint16_t scale );
void gln64gSPTexture( float sc, float tc, int32_t level, int32_t tile, int32_t on );
void gln64gSPEndDisplayList();
void gln64gSPGeometryMode( uint32_t clear, uint32_t set );
void gln64gSPSetGeometryMode( uint32_t mode );
void gln64gSPClearGeometryMode( uint32_t mode );
void gln64gSPSetOtherMode_H(uint32_t _length, uint32_t _shift, uint32_t _data);
void gln64gSPSetOtherMode_L(uint32_t _length, uint32_t _shift, uint32_t _data);
void gln64gSPLine3D(int32_t v0, int32_t v1, int32_t flag);
void gln64gSPLineW3D( int32_t v0, int32_t v1, int32_t wd, int32_t flag );
void gln64gSPObjRectangle(uint32_t _sp );
void gln64gSPObjRectangleR(uint32_t _sp);
void gln64gSPObjSprite(uint32_t _sp);
void gln64gSPObjLoadTxtr( uint32_t tx );
void gln64gSPObjLoadTxSprite( uint32_t txsp );
void gln64gSPObjLoadTxRect(uint32_t txsp);
void gln64gSPObjLoadTxRectR( uint32_t txsp );
void gln64gSPBgRect1Cyc(uint32_t _bg );
void gln64gSPBgRectCopy(uint32_t _bg );
void gln64gSPObjMatrix( uint32_t mtx );
void gln64gSPObjSubMatrix( uint32_t mtx );
void gln64gSPObjRendermode(uint32_t _mode);
void gln64gSPSetDMAOffsets( uint32_t mtxoffset, uint32_t vtxoffset );
void gln64gSPSetDMATexOffset(uint32_t _addr);
void gln64gSPSetVertexColorBase( uint32_t base );
void gln64gSPSetVertexNormaleBase( uint32_t base );
void gln64gSPProcessVertex(uint32_t v);
void gln64gSPCoordMod(uint32_t _w0, uint32_t _w1);

void gln64gSPTriangleUnknown();

void gln64gSPTriangle(int32_t v0, int32_t v1, int32_t v2);
void gln64gSP1Triangle(int32_t v0, int32_t v1, int32_t v2);
void gln64gSP2Triangles(const int32_t v00, const int32_t v01, const int32_t v02, const int32_t flag0,
					const int32_t v10, const int32_t v11, const int32_t v12, const int32_t flag1 );
void gln64gSP4Triangles(const int32_t v00, const int32_t v01, const int32_t v02,
					const int32_t v10, const int32_t v11, const int32_t v12,
					const int32_t v20, const int32_t v21, const int32_t v22,
					const int32_t v30, const int32_t v31, const int32_t v32 );

#ifdef __VEC4_OPT
extern void (*gln64gSPTransformVertex4)(uint32_t v, float mtx[4][4]);
extern void (*gln64gSPTransformNormal4)(uint32_t v, float mtx[4][4]);
extern void (*gln64gSPLightVertex4)(uint32_t v);
extern void (*gln64gSPPointLightVertex4)(uint32_t v, float _vPos[4][3]);
extern void (*gln64gSPBillboardVertex4)(uint32_t v);
#endif
extern void (*gln64gSPTransformVertex)(float vtx[4], float mtx[4][4]);
extern void (*gln64gSPLightVertex)(SPVertex & _vtx);
extern void (*gln64gSPPointLightVertex)(SPVertex & _vtx, float * _vPos);
extern void (*gln64gSPBillboardVertex)(uint32_t v, uint32_t i);
void gSPSetupFunctions();
#endif
