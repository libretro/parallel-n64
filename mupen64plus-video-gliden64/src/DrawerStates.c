#include <math.h>
#include <string.h>

#include "Types.h"
#include "GBI.h"
#include "gDP.h"
#include "gSP.h"
#include "Config.h"
#include "gli_drawer.h"

int gli_drawer_cull_mode(void)
{
	if ((gSP.geometryMode & G_CULL_BOTH) == 0)
		return GLI_CULL_NONE;
	if ((gSP.geometryMode & G_CULL_BOTH) == G_CULL_BOTH && gli_GBI->cullBoth)
		return GLI_CULL_FRONT_AND_BACK;
	if ((gSP.geometryMode & G_CULL_BACK) == G_CULL_BACK)
		return GLI_CULL_BACK;
	return GLI_CULL_FRONT;
}

void gli_drawer_depth_state(gli_depth_state *_out)
{
	_out->set = 0;
	if (config.frameBufferEmulation.N64DepthCompare != 0) {
		/* the shader compares depth itself */
		_out->set = GLI_DS_TEST | GLI_DS_WRITE;
		_out->test = 0;
		_out->write = 0;
		return;
	}
	if ((gDP.changed & (CHANGED_RENDERMODE | CHANGED_CYCLETYPE)) == 0)
		return;

	if (((gSP.geometryMode & G_ZBUFFER) || gDP.otherMode.depthSource == G_ZS_PRIM) &&
			gDP.otherMode.cycleType <= G_CYC_2CYCLE) {
		_out->set = GLI_DS_TEST | GLI_DS_WRITE | GLI_DS_COMPARE;
		_out->offset = 0;
		_out->compare = GLI_CMP_ALWAYS;
		if (gDP.otherMode.depthCompare != 0) {
			switch (gDP.otherMode.depthMode) {
			case ZMODE_INTER:
				_out->compare = GLI_CMP_LEQUAL;
				break;
			case ZMODE_OPA:
			case ZMODE_XLU:
				/* Max with a far primitive depth, else Infront */
				_out->compare = gDP.otherMode.depthSource == G_ZS_PRIM && gDP.primDepth.z == 1.0f ?
					GLI_CMP_LEQUAL : GLI_CMP_LESS;
				break;
			case ZMODE_DEC:
				_out->offset = 1;
				_out->compare = GLI_CMP_LEQUAL;
				break;
			default:
				/* depthMode is two bits; nothing else to set */
				_out->set &= ~GLI_DS_COMPARE;
				break;
			}
		}
		_out->write = gDP.otherMode.depthUpdate != 0;
		_out->test = 1;
		_out->clipping = 1;
	} else {
		_out->set = GLI_DS_TEST;
		_out->test = 0;
		_out->clipping = 0;
	}
	if (!gli_GBI->NoN)
		_out->set |= GLI_DS_CLAMP;
}

void gli_drawer_texrect_correct(f32 *_rect, f32 *_prev)
{
	/* _rect and _prev are ulx, uly, lrx, lry */
	if (config.graphics2D.correctTexrectCoords == tcSmart) {
		if (_rect[0] == _prev[0] && _rect[2] == _prev[2]) {
			if (fabs(_rect[1] - _prev[3]) < 0.51f)
				_rect[1] = _prev[3];
			else if (fabs(_rect[3] - _prev[1]) < 0.51f)
				_rect[3] = _prev[1];
		} else if (_rect[1] == _prev[1] && _rect[3] == _prev[3]) {
			if (fabs(_rect[0] - _prev[2]) < 0.51f)
				_rect[0] = _prev[2];
			else if (fabs(_rect[2] - _prev[0]) < 0.51f)
				_rect[2] = _prev[0];
		}
	} else if (config.graphics2D.correctTexrectCoords == tcForce) {
		_rect[2] += 0.25f;
		_rect[3] += 0.25f;
	}
	memcpy(_prev, _rect, 4 * sizeof(f32));
}
