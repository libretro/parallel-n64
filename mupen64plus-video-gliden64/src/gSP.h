#ifndef GSP_H
#define GSP_H

#include <boolean.h>
#include "Types.h"
#include "GBI.h"
#include "gDP.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CHANGED_VIEWPORT		0x01U
#define CHANGED_MATRIX			0x02U
#define CHANGED_TEXTURE			0x04U
#define CHANGED_GEOMETRYMODE	0x08U
#define CHANGED_FOGPOSITION		0x10U
#define CHANGED_LIGHT			0x20U
#define CHANGED_LOOKAT			0x40U
#define CHANGED_TEXTURESCALE	0x80U
#define CHANGED_HW_LIGHT		0x100U

#define CLIP_X      0x03
#define CLIP_NEGX   0x01
#define CLIP_POSX   0x02
#define CLIP_Y      0x0C
#define CLIP_NEGY   0x04
#define CLIP_POSY   0x08
#define CLIP_W      0x10
#define CLIP_ALL	0x1F /* CLIP_NEGX|CLIP_POSX|CLIP_NEGY|CLIP_POSY|CLIP_W */

#define MODIFY_XY	0x000000FF
#define MODIFY_Z	0x0000FF00
#define MODIFY_ST	0x00FF0000
#define MODIFY_RGBA	0xFF000000
#define MODIFY_ALL	0xFFFFFFFF

enum Component { R, G, B };
enum Axis { X ,Y, Z, W };

typedef struct SPVertex SPVertex;
struct SPVertex
{
	f32 x, y, z, w;
	f32 nx, ny, nz, __pad0;
	f32 r, g, b, a;
	f32 flat_r, flat_g, flat_b, flat_a;
	f32 s, t;
	u32 modify;
	u8 HWLight;
	u8 clip;
	s16 flag;
};

typedef struct gSPInfo gSPInfo;
struct gSPInfo
{
	u32 segment[16];

	struct
	{
		u32 modelViewi, stackSize, billboard;
		f32 modelView[32][4][4];
		f32 projection[4][4];
		f32 combined[4][4];
	} matrix;

	u32 objRendermode;

	u32 vertexColorBase;
	u32 vertexi;

	struct
	{
		f32 rgb[12][3];
		f32 xyz[12][3];
		f32 i_xyz[12][3];
		f32 pos_xyzw[12][4];
		f32 ca[12], la[12], qa[12];
	} lights;

	struct
	{
		f32 rgb[2][3];
		f32 xyz[2][3];
		f32 i_xyz[2][3];
		f32 pos_xyzw[2][4];
		f32 ca[2], la[2], qa[2];
	} lookat;

	u32 numLights;
	bool lookatEnable;

	struct
	{
		f32 scales, scalet;
		u32 level, on, tile;
	} texture;

	gDPTile *textureTile[2];

	struct Viewport
	{
		f32 vscale[4];
		f32 vtrans[4];
		f32 x, y, width, height;
		f32 nearz, farz;
	} viewport;

	struct
	{
		s16 multiplier, offset;
		f32 multiplierf, offsetf;
	} fog;

	struct
	{
		u32 address, width, height, format, size, palette;
		f32 imageX, imageY, scaleW, scaleH;
		u8 clampS, clampT;
	} bgImage;

	u32 geometryMode;
	u32 changed;

	struct {
		u8 sid;
		u32 flag;
		u32 addr;
	} selectDL;
	u32 status[4];

	struct
	{
		u32 vtx, mtx, tex_offset, tex_shift, tex_count;
	} DMAOffsets;

	u32 DMAIO_address;

	u32 tri_num;

	u32 clipRatio;

	struct
	{
		u32 vertexNormalBase;
		f32 vertexCoordMod[16];
		bool advancedLighting;
	} cbfd;

	u32 textureCoordScaleOrg;
	u32 textureCoordScale[2];
};

extern gSPInfo gSP;

void gli_gSPLoadUcodeEx( u32 uc_start, u32 uc_dstart, u16 uc_dsize );
void gli_gSPNoOp(void);
void gli_gSPMatrix( u32 matrix, u8 param );
void gli_gSPDMAMatrix( u32 matrix, u8 index, u8 multiply );
void gli_gSPViewport( u32 v );
void gli_gSPForceMatrix( u32 mptr );
void gli_gSPLight( u32 l, s32 n );
void gli_gSPLightCBFD( u32 l, s32 n );
void gli_gSPLookAt( u32 l, u32 n );
void gli_gSPLightAcclaim(u32 l, s32 n);
void gli_gSPVertex( u32 v, u32 n, u32 v0 );
void gli_gSPCIVertex( u32 v, u32 n, u32 v0 );
void gli_gSPDMAVertex( u32 v, u32 n, u32 v0 );
void gli_gSPCBFDVertex( u32 v, u32 n, u32 v0 );
void gli_gSPT3DUXVertex(u32 v, u32 n, u32 ci);
void gli_gSPF3DAMVertex( u32 v, u32 n, u32 v0 );
void gli_gSPDisplayList(u32 dl);
void gli_gSPBranchList( u32 dl );
void gli_gSPBranchLessZ(u32 branchdl, u32 vtx, u32 zval);
void gli_gSPBranchLessW( u32 branchdl, u32 vtx, u32 wval );
void gli_gSPDlistCount(u32 count, u32 v);
void gli_gSPSprite2DBase(u32 _base );
void gli_gSPDMATriangles( u32 tris, u32 n );
void gli_gSP1Quadrangle( s32 v0, s32 v1, s32 v2, s32 v3 );
void gli_gSPCullDisplayList( u32 v0, u32 vn );
void gli_gSPPopMatrix( u32 param );
void gli_gSPPopMatrixN( u32 param, u32 num );
void gli_gSPSegment( s32 seg, s32 base );
void gli_gSPRelSegment( s32 seg, s32 base );
void gli_gSPClipRatio( u32 ratio );
void gli_gSPInsertMatrix( u32 where, u32 num );
void gli_gSPModifyVertex(u32 _vtx, u32 _where, u32 _val );
void gli_gSPNumLights( s32 n );
void gli_gSPLightColor( u32 lightNum, u32 packedColor );
void gli_gSPFogFactor( s16 fm, s16 fo );
void gli_gSPPerspNormalize( u16 scale );
void gli_gSPTexture( f32 sc, f32 tc, u32 level, u32 tile, u32 on );
void gli_gSPEndDisplayList(void);
void gli_gSPGeometryMode( u32 clear, u32 set );
void gli_gSPSetGeometryMode( u32 mode );
void gli_gSPClearGeometryMode( u32 mode );
void gli_gSPSetOtherMode_H(u32 _length, u32 _shift, u32 _data);
void gli_gSPSetOtherMode_L(u32 _length, u32 _shift, u32 _data);
void gli_gSPLine3D(u32 v0, u32 v1, s32 wd, u32 flag);
void gli_gSPSetStatus(u32 sid, u32 val);
void gli_gSPSetDMAOffsets( u32 mtxoffset, u32 vtxoffset );
void gli_gSPSetDMATexOffset(u32 _addr);
void gli_gSPSetVertexColorBase( u32 base );
void gli_gSPCombineMatrices(u32 _mode);

/** Stereo rendering. */
void gli_gSPStereoBeginTask(void);
void gli_gSPStereoEndFrame(void);
void gli_gSPStereoReset(void);
void gli_gSPSetStereoEye(u32 _eye);
void gli_gSPApplyStereo(f32 matrix[4][4]);

/* Immersive mode */
struct RectVertex;
bool gli_gSPImmersiveActive(void);
bool gli_gSPImmersiveBackground(void);
bool gli_gSPImmersiveClearsShownBuffers(void);
void gli_gSPImmersiveFrameDrawn(u32 _address);
void gli_gSPImmersivePresent(u32 _address);
bool gli_gSPImmersiveIsFlat(void);
void gli_gSPImmersiveWorldSprite(bool _world);

void gli_gSPTriangle(s32 v0, s32 v1, s32 v2);
void gli_gSP1Triangle(s32 v0, s32 v1, s32 v2);
void gli_gSP2Triangles(const s32 v00, const s32 v01, const s32 v02, const s32 flag0,
					const s32 v10, const s32 v11, const s32 v12, const s32 flag1 );
void gli_gSP4Triangles(const s32 v00, const s32 v01, const s32 v02,
					const s32 v10, const s32 v11, const s32 v12,
					const s32 v20, const s32 v21, const s32 v22,
					const s32 v30, const s32 v31, const s32 v32 );


extern void (*gSPTransformVector)(float vtx[4], float mtx[4][4]);
extern void (*gSPInverseTransformVector)(float vec[3], float mtx[4][4]);
void gli_gSPSetupFunctions(void);
void gli_gSPFlushTriangles(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
struct RectVertex;
void gli_gSPSWVertex(const SWVertex * vertex, u32 n, const bool * const verticesToProcess);
void gli_gSPSWVertex(const SWVertex * vertex, u32 v0, u32 n);
void gli_gSPImmersiveRect(RectVertex * _vertices, u32 _count, bool _fullScreen);
void gli_gSPImmersiveScreenTriangles(SPVertex * _vertices, u32 _count);
void gli_gSPLightVertex(SPVertex & _vtx);
#endif

#endif
