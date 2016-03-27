#include "Turbo3D.h"
#include "N64.h"
#include "RSP.h"
#include "RDP.h"
#include "gSP.h"
#include "gDP.h"
#include "OpenGL.h"

/******************Turbo3D microcode*************************/

struct T3DGlobState
{
	uint16_t pad0;
	uint16_t perspNorm;
	uint32_t flag;
	uint32_t othermode0;
	uint32_t othermode1;
	uint32_t segBases[16];
	/* the viewport to use */
	int16_t vsacle1;
	int16_t vsacle0;
	int16_t vsacle3;
	int16_t vsacle2;
	int16_t vtrans1;
	int16_t vtrans0;
	int16_t vtrans3;
	int16_t vtrans2;
	uint32_t rdpCmds;
};

struct T3DState
{
	uint32_t renderState;	/* render state */
	uint32_t textureState;	/* texture state */
	uint8_t flag;
	uint8_t triCount;	/* how many tris? */
	uint8_t vtxV0;		/* where to load verts? */
	uint8_t vtxCount;	/* how many verts? */
	uint32_t rdpCmds;	/* ptr (segment address) to RDP DL */
	uint32_t othermode0;
	uint32_t othermode1;
};


struct T3DTriN
{
	uint8_t	flag, v2, v1, v0;	/* flag is which one for flat shade */
};


static
void Turbo3D_ProcessRDP(uint32_t _cmds)
{
	uint32_t addr = RSP_SegmentToPhysical(_cmds) >> 2;
	if (addr != 0) {
		__RSP.bLLE = true;
		uint32_t w0 = ((uint32_t*)RDRAM)[addr++];
		uint32_t w1 = ((uint32_t*)RDRAM)[addr++];
		__RSP.cmd = _SHIFTR( w0, 24, 8 );
		while (w0 + w1 != 0) {
			GBI.cmd[__RSP.cmd]( w0, w1 );
			w0 = ((uint32_t*)RDRAM)[addr++];
			w1 = ((uint32_t*)RDRAM)[addr++];
			__RSP.cmd = _SHIFTR( w0, 24, 8 );
			if (__RSP.cmd == 0xE4 || __RSP.cmd == 0xE5) {
				__RDP.w2 = ((uint32_t*)RDRAM)[addr++];
				__RDP.w3 = ((uint32_t*)RDRAM)[addr++];
			}
		}
		__RSP.bLLE = false;
	}
}

static
void Turbo3D_LoadGlobState(uint32_t pgstate)
{
	const uint32_t addr = RSP_SegmentToPhysical(pgstate);
	T3DGlobState *gstate = (T3DGlobState*)&RDRAM[addr];
	const uint32_t w0 = gstate->othermode0;
	const uint32_t w1 = gstate->othermode1;
	gDPSetOtherMode( _SHIFTR( w0, 0, 24 ),	// mode0
					 w1 );					// mode1

	for (int s = 0; s < 16; ++s)
		gSPSegment(s, gstate->segBases[s] & 0x00FFFFFF);

	gSPViewport(pgstate + 80);

	Turbo3D_ProcessRDP(gstate->rdpCmds);
}

static
void Turbo3D_LoadObject(uint32_t pstate, uint32_t pvtx, uint32_t ptri)
{
	uint32_t addr = RSP_SegmentToPhysical(pstate);
	T3DState *ostate = (T3DState*)&RDRAM[addr];
	const uint32_t tile = (ostate->textureState)&7;
	gSP.texture.tile = tile;
	gSP.textureTile[0] = &gDP.tiles[tile];
	gSP.textureTile[1] = &gDP.tiles[(tile + 1) & 7];
	gSP.texture.scales = 1.0f;
	gSP.texture.scalet = 1.0f;

	const uint32_t w0 = ostate->othermode0;
	const uint32_t w1 = ostate->othermode1;
	gDPSetOtherMode( _SHIFTR( w0, 0, 24 ),	// mode0
					 w1 );					// mode1

	gSPSetGeometryMode(ostate->renderState);

	if ((ostate->flag&1) == 0) //load matrix
		gSPForceMatrix(pstate + sizeof(T3DState));

	gSPClearGeometryMode(G_LIGHTING);
	gSPSetGeometryMode(G_SHADING_SMOOTH);
	if (pvtx != 0) //load vtx
		gSPVertex(pvtx, ostate->vtxCount, ostate->vtxV0);

	Turbo3D_ProcessRDP(ostate->rdpCmds);

	if (ptri != 0) {
		addr = RSP_SegmentToPhysical(ptri);
		for (int t = 0; t < ostate->triCount; ++t) {
			T3DTriN * tri = (T3DTriN*)&RDRAM[addr];
			addr += 4;
			gSPTriangle(tri->v0, tri->v1, tri->v2);
		}
		video().getRender().drawTriangles();
	}
}

void RunTurbo3D()
{
	uint32_t pstate;
	do {
		uint32_t addr = __RSP.PC[__RSP.PCi] >> 2;
		const uint32_t pgstate = ((uint32_t*)RDRAM)[addr++];
		pstate = ((uint32_t*)RDRAM)[addr++];
		const uint32_t pvtx = ((uint32_t*)RDRAM)[addr++];
		const uint32_t ptri = ((uint32_t*)RDRAM)[addr];
		if (pstate == 0) {
			__RSP.halt = 1;
			break;
		}
		if (pgstate != 0)
			Turbo3D_LoadGlobState(pgstate);
		Turbo3D_LoadObject(pstate, pvtx, ptri);
		// Go to the next instruction
		__RSP.PC[__RSP.PCi] += 16;
	} while (pstate != 0);
}
