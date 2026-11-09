static void VN(gSPLightVertexStandard)(u32 v, SPVertex * spVtx)
{
#ifndef __NEON_OPT
	if (!gli_isHWLightingAllowed()) {
		int j;
		for (j = 0; j < VNUM; ++j) {
			u32 i;
			SPVertex * vtx = &spVtx[v+j];
			vtx->r = gSP.lights.rgb[gSP.numLights][R];
			vtx->g = gSP.lights.rgb[gSP.numLights][G];
			vtx->b = gSP.lights.rgb[gSP.numLights][B];
			vtx->HWLight = 0;

			for (i = 0; i < gSP.numLights; ++i) {
				const f32 intensity = DotProduct( &vtx->nx, gSP.lights.i_xyz[i] );
				if (intensity > 0.0f) {
					vtx->r += gSP.lights.rgb[i][R] * intensity;
					vtx->g += gSP.lights.rgb[i][G] * intensity;
					vtx->b += gSP.lights.rgb[i][B] * intensity;
				}
			}
			vtx->r = gsp_minf(1.0f, vtx->r);
			vtx->g = gsp_minf(1.0f, vtx->g);
			vtx->b = gsp_minf(1.0f, vtx->b);
		}
	} else {
		int j;
		for (j = 0; j < VNUM; ++j) {
			SPVertex * vtx = &spVtx[v+j];
			gli_TransformVectorNormalize(&vtx->r, gSP.matrix.modelView[gSP.matrix.modelViewi]);
			vtx->HWLight = gSP.numLights;
		}
	}
#else
	void gSPLightVertex_NEON(u32 vnum, u32 v, SPVertex * spVtx);
	gSPLightVertex_NEON(VNUM, v, spVtx);
#endif
}

static void VN(gSPLightVertexCBFD_basic)(u32 v, SPVertex * spVtx)
{
	int j;
	for (j = 0; j < VNUM; ++j) {
		f32 r;
		f32 g;
		f32 b;
		int l;
		f32 vPos[3];
		SPVertex * vtx = &spVtx[v + j];
		vtx->HWLight = 0;
		if (vtx->flag < 0)
			continue;

		r = gSP.lights.rgb[gSP.numLights][R];
		g = gSP.lights.rgb[gSP.numLights][G];
		b = gSP.lights.rgb[gSP.numLights][B];
		vPos[0] = (vtx->x + gSP.cbfd.vertexCoordMod[ 8])*gSP.cbfd.vertexCoordMod[12];
		vPos[1] = (vtx->y + gSP.cbfd.vertexCoordMod[ 9])*gSP.cbfd.vertexCoordMod[13];
		vPos[2] = (vtx->z + gSP.cbfd.vertexCoordMod[10])*gSP.cbfd.vertexCoordMod[14];

		for (l = gSP.numLights - 2; l >= 0; --l) {
			const f32 vx = vPos[X] - gSP.lights.pos_xyzw[l][X];
			const f32 vy = vPos[Y] - gSP.lights.pos_xyzw[l][Y];
			const f32 vz = vPos[Z] - gSP.lights.pos_xyzw[l][Z];
			const f32 len = 2.0f * (vx*vx + vy*vy + vz*vz) * FIXED2FLOATRECIP16;
			f32 intensity = gsp_minf(1.0f, gSP.lights.ca[l] / len);
			if (intensity > 0.0f) {
				r += gSP.lights.rgb[l][R] * intensity;
				g += gSP.lights.rgb[l][G] * intensity;
				b += gSP.lights.rgb[l][B] * intensity;
			}
		}

		r = gsp_minf(1.0f, r);
		g = gsp_minf(1.0f, g);
		b = gsp_minf(1.0f, b);

		vtx->r *= r;
		vtx->g *= g;
		vtx->b *= b;
	}
}

static void VN(gSPLightVertexCBFD_advanced)(u32 v, SPVertex * spVtx)
{
	int j;
	for (j = 0; j < VNUM; ++j) {
		int l;
		f32 r;
		f32 g;
		f32 b;
		f32 intensity;
		f32 vPos[3];
		SPVertex * vtx = &spVtx[v + j];
		vtx->HWLight = 0;
		if (vtx->flag < 0)
			continue;

		vPos[0] = (vtx->x + gSP.cbfd.vertexCoordMod[ 8])*gSP.cbfd.vertexCoordMod[12];
		vPos[1] = (vtx->y + gSP.cbfd.vertexCoordMod[ 9])*gSP.cbfd.vertexCoordMod[13];
		vPos[2] = (vtx->z + gSP.cbfd.vertexCoordMod[10])*gSP.cbfd.vertexCoordMod[14];
		l = gSP.numLights;
		r = gSP.lights.rgb[l][R];
		g = gSP.lights.rgb[l][G];
		b = gSP.lights.rgb[l][B];
		--l;
		intensity = gsp_minf(1.0f, DotProduct(&vtx->nx, gSP.lights.i_xyz[l]));
		if (intensity > 0.0f) {
			r += gSP.lights.rgb[l][R] * intensity;
			g += gSP.lights.rgb[l][G] * intensity;
			b += gSP.lights.rgb[l][B] * intensity;
		}

		while (--l >= 0) {
			const f32 vx = vPos[X] - gSP.lights.pos_xyzw[l][X];
			const f32 vy = vPos[Y] - gSP.lights.pos_xyzw[l][Y];
			const f32 vz = vPos[Z] - gSP.lights.pos_xyzw[l][Z];
			const f32 len = 2.0f * (vx*vx + vy*vy + vz*vz) * FIXED2FLOATRECIP16;
			intensity = gsp_minf(1.0f, gSP.lights.ca[l] / len);
			if ((gSP.geometryMode & G_POINT_LIGHTING) != 0)
				intensity *= gsp_minf(1.0f, DotProduct(&vtx->nx, gSP.lights.i_xyz[l]));;
			if (intensity > 0.0f) {
				r += gSP.lights.rgb[l][R] * intensity;
				g += gSP.lights.rgb[l][G] * intensity;
				b += gSP.lights.rgb[l][B] * intensity;
			}
		}

		r = gsp_minf(1.0f, r);
		g = gsp_minf(1.0f, g);
		b = gsp_minf(1.0f, b);

		vtx->r *= r;
		vtx->g *= g;
		vtx->b *= b;
	}
}

static void VN(gli_gSPLightVertex)(u32 _v, SPVertex * _spVtx)
{
	if (g_ConkerUcode) {
		if (gSP.cbfd.advancedLighting)
			VN(gSPLightVertexCBFD_advanced)(_v, _spVtx);
		else
			VN(gSPLightVertexCBFD_basic)(_v, _spVtx);
	} else
		VN(gSPLightVertexStandard)(_v, _spVtx);
}

static void VN(gSPPointLightVertexZeldaMM)(u32 v, float _vecPos[VNUM][4], SPVertex * spVtx)
{
	int j;
	f32 intensity = 0.0f;
	for (j = 0; j < VNUM; ++j) {
		u32 l;
		SPVertex * vtx = &spVtx[v + j];
		vtx->HWLight = 0;
		vtx->r = gSP.lights.rgb[gSP.numLights][R];
		vtx->g = gSP.lights.rgb[gSP.numLights][G];
		vtx->b = gSP.lights.rgb[gSP.numLights][B];
		gSPTransformVector(_vecPos[j], gSP.matrix.modelView[gSP.matrix.modelViewi]);

		for (l = 0; l < gSP.numLights; ++l) {
			if (gSP.lights.ca[l] != 0.0f) {
				f32 K;
				f32 KS;
				u32 i;
				f32 V;
				f32 KSF;
				f32 D;
				f32 recip = FIXED2FLOATRECIP16;
				/* Point lighting */
				f32 lvec[3];
				lvec[0] = gSP.lights.pos_xyzw[l][X];
				lvec[1] = gSP.lights.pos_xyzw[l][Y];
				lvec[2] = gSP.lights.pos_xyzw[l][Z];
				lvec[0] -= _vecPos[j][0];
				lvec[1] -= _vecPos[j][1];
				lvec[2] -= _vecPos[j][2];

				K = lvec[0] * lvec[0] + lvec[1] * lvec[1] + lvec[2] * lvec[2] * 2.0f;
				KS = gsp_sqrtf(K);

				gSPInverseTransformVector(lvec, gSP.matrix.modelView[gSP.matrix.modelViewi]);

				for (i = 0; i < 3; ++i) {
					lvec[i] = (4.0f * lvec[i] / KS);
					if (lvec[i] < -1.0f)
						lvec[i] = -1.0f;
					if (lvec[i] > 1.0f)
						lvec[i] = 1.0f;
				}

				V = lvec[0] * vtx->nx + lvec[1] * vtx->ny + lvec[2] * vtx->nz;
				if (V < -1.0f)
					V = -1.0f;
				if (V > 1.0f)
					V = 1.0f;

				KSF = gsp_floorf(KS);
				D = (KSF * gSP.lights.la[l] * 2.0f + KSF * KSF * gSP.lights.qa[l] / 8.0f) * recip + 1.0f;
				intensity = V / D;
			} else {
				/* Standard lighting */
				intensity = DotProduct(&vtx->nx, gSP.lights.i_xyz[l]);
			}
			if (intensity > 0.0f) {
				vtx->r += gSP.lights.rgb[l][R] * intensity;
				vtx->g += gSP.lights.rgb[l][G] * intensity;
				vtx->b += gSP.lights.rgb[l][B] * intensity;
			}
		}
		if (vtx->r > 1.0f) vtx->r = 1.0f;
		if (vtx->g > 1.0f) vtx->g = 1.0f;
		if (vtx->b > 1.0f) vtx->b = 1.0f;
	}
}

static void VN(gSPPointLightVertex)(u32 _v, float _vecPos[VNUM][4], SPVertex * _spVtx)
{
	if (g_ConkerUcode) {
		if (gSP.cbfd.advancedLighting)
			VN(gSPLightVertexCBFD_advanced)(_v, _spVtx);
		else
			VN(gSPLightVertexCBFD_basic)(_v, _spVtx);
	} else
		VN(gSPPointLightVertexZeldaMM)(_v, _vecPos, _spVtx);
}

static void VN(gSPPointLightVertexAcclaim)(u32 v, SPVertex * spVtx)
{
	int j;
	for (j = 0; j < VNUM; ++j) {
		u32 l;
		SPVertex * vtx = &spVtx[v + j];
		vtx->HWLight = 0;

		for (l = 2; l < 10; ++l) {
			f32 dX;
			f32 dY;
			f32 dZ;
			f32 distance;
			f32 light_intensity;
			if (gSP.lights.ca[l] < 0)
				continue;

			dX = gsp_fabsf(gSP.lights.pos_xyzw[l][X] - vtx->x);
			dY = gsp_fabsf(gSP.lights.pos_xyzw[l][Y] - vtx->y);
			dZ = gsp_fabsf(gSP.lights.pos_xyzw[l][Z] - vtx->z);
			distance = dX + dY + dZ - gSP.lights.ca[l];
			if (distance >= 0.0f)
				continue;

			light_intensity = -distance * gSP.lights.la[l];
			vtx->r += gSP.lights.rgb[l][R] * light_intensity;
			vtx->g += gSP.lights.rgb[l][G] * light_intensity;
			vtx->b += gSP.lights.rgb[l][B] * light_intensity;
		}

		if (vtx->r > 1.0f) vtx->r = 1.0f;
		if (vtx->g > 1.0f) vtx->g = 1.0f;
		if (vtx->b > 1.0f) vtx->b = 1.0f;
	}
}

static void VN(gSPBillboardVertex)(u32 v, SPVertex * spVtx)
{
	u32 j;
#ifndef __NEON_OPT
	SPVertex * vtx0 = &spVtx[0];
	for (j = 0; j < VNUM; ++j) {
		SPVertex * vtx = &spVtx[v + j];
		vtx->x += vtx0->x;
		vtx->y += vtx0->y;
		vtx->z += vtx0->z;
		vtx->w += vtx0->w;
	}
#else
	if (VNUM == 1) {
		SPVertex * vtx0 = &spVtx[0];
		SPVertex * vtx = &spVtx[v];
		vtx->x += vtx0->x;
		vtx->y += vtx0->y;
		vtx->z += vtx0->z;
		vtx->w += vtx0->w;
	} else {
		void gSPBillboardVertex4NEON(u32 v);
		gSPBillboardVertex4NEON(v);
	}
#endif /* __NEON_OPT */
}

static void VN(gSPApplyStereoConvergence)(u32 v, SPVertex * spVtx)
{
	f32 separation;
	bool orthographic;
	bool measure;
	f32 convergence;
	u32 j;
	if (l_stereoEye != stereoLeftEye && l_stereoEye != stereoRightEye)
		return;

	separation = _stereoSeparation();
	orthographic = _isOrthographic(gSP.matrix.combined);
	if (!orthographic)
		l_stereoPerspectiveSeen = true;
	convergence = l_autoConvergence;
	measure = !orthographic &&
		(config.stereo.mode != stereoBothEyes || l_stereoEye == stereoLeftEye);
	for (j = 0; j < VNUM; ++j) {
		f32 limit;
		f32 requestedShift;
		f32 safeShift;
		SPVertex * vtx = &spVtx[v+j];
		/* gli_gSPApplyStereo() already contributed separation*w, which is the parallax at infinity. */
		const f32 matrixShift = separation * vtx->w;
		if (orthographic && _immersiveOn()) {
			/* A background covers the game's screen and is stretched over */
			/* the whole view, keeping the parallax at infinity the matrix gave. Overlays are placed */
			/* where the game's screen was, on the screen plane. */
			const f32 ow = vtx->w != 0.0f ? vtx->w : 1.0f;
			f32 x, y, z, w;
			if (!l_stereoPerspectiveSeen) {
				_immersivePlaceSky((vtx->x - matrixShift) / ow, STEREO_FLIP_Y * vtx->y / ow, vtx->z / ow, &x, &y, &z, &w);
				x += separation * w;
			} else if (l_immersiveVoted && (gsp_fabsf((vtx->x - matrixShift) / ow) >= 0.95f || gsp_fabsf(vtx->y / ow) >= 0.95f)) {
				/* Factor 5 overlays reaching the edge of the screen, such as fades, cover the view */
				_immersivePlaceSky((vtx->x - matrixShift) / ow, STEREO_FLIP_Y * vtx->y / ow, vtx->z / ow, &x, &y, &z, &w);
			} else {
				_immersivePlaceFlat((vtx->x - matrixShift) / ow, STEREO_FLIP_Y * vtx->y / ow, vtx->z / ow, &x, &y, &z, &w,
					config.stereo.immersiveHudScale);
			}
			vtx->x = x;
			vtx->y = STEREO_FLIP_Y * y;
			vtx->z = z;
			vtx->w = w;
			continue;
		}
		if (orthographic) {
			/* A flat projection carries no depth. Backgrounds keep the parallax at infinity, overlays */
			/* drawn after the scene. */
			if (l_stereoPerspectiveSeen)
				vtx->x -= matrixShift;
			continue;
		}
		/* Replace that contribution with separation*(w-convergence), capped to the magnitude of the */
		/* parallax at infinity. */
		if (measure && vtx->w > 0.0f)
			_stereoMeasure(vtx->w);
		/* Nothing measured yet: keep the parallax at infinity the matrix gave */
		if (convergence <= 0.0f)
			continue;
		/* Physical parallax in screen space is separation * (1 - convergence/w). */
		requestedShift = config.stereo.depthBoost > 0.0f && vtx->w > 0.0f
			? separation * vtx->w * (1.0f - gsp_powf(convergence / vtx->w, 1.0f - 0.7f * config.stereo.depthBoost))
			: separation * (vtx->w - convergence);
		limit = gsp_fabsf(separation * vtx->w);
		safeShift = gsp_maxf(-limit, gsp_minf(requestedShift, limit));
		vtx->x += safeShift - matrixShift;
	}
}

static void VN(gSPClipVertex)(u32 v, SPVertex * spVtx)
{
	u32 j;
	for (j = 0; j < VNUM; ++j) {
		SPVertex * vtx = &spVtx[v+j];
		vtx->clip = 0;
		if (vtx->x > +vtx->w) vtx->clip |= CLIP_POSX;
		if (vtx->x < -vtx->w) vtx->clip |= CLIP_NEGX;
		if (vtx->y > +vtx->w) vtx->clip |= CLIP_POSY;
		if (vtx->y < -vtx->w) vtx->clip |= CLIP_NEGY;
		if (vtx->w < 0.01f) vtx->clip |= CLIP_W;
	}
}

static void VN(gSPImmersiveViewport)(u32 v, SPVertex * spVtx)
{
	f32 halfWidth;
	f32 halfHeight;
	u32 j;
	if (!gli_gSPImmersiveActive() || gSP.viewport.vscale[0] == 0.0f || gSP.viewport.vscale[1] == 0.0f)
		return;
	halfWidth = (f32)(gDP.colorImage.width) * 0.5f;
	halfHeight = VI.height > 0 ? (f32)(VI.height) * 0.5f : halfWidth * 0.75f;
	for (j = 0; j < VNUM; ++j) {
		SPVertex * vtx = &spVtx[v+j];
		vtx->x = (halfWidth * vtx->x + (halfWidth - gSP.viewport.vtrans[0]) * vtx->w) / gSP.viewport.vscale[0];
		vtx->y = STEREO_FLIP_Y * (halfHeight * STEREO_FLIP_Y * vtx->y - (halfHeight - gSP.viewport.vtrans[1]) * vtx->w) /
			gSP.viewport.vscale[1];
	}
}

static void VN(gSPTransformVertex)(u32 v, SPVertex * spVtx, float mtx[4][4])
{
	int i;
#ifndef __NEON_OPT
	float x, y, z;
	for (i = 0; i < VNUM; ++i) {
		SPVertex * vtx = &spVtx[v+i];
		x = vtx->x;
		y = vtx->y;
		z = vtx->z;
		vtx->x = x * mtx[0][0] + y * mtx[1][0] + z * mtx[2][0] + mtx[3][0];
		vtx->y = x * mtx[0][1] + y * mtx[1][1] + z * mtx[2][1] + mtx[3][1];
		vtx->z = x * mtx[0][2] + y * mtx[1][2] + z * mtx[2][2] + mtx[3][2];
		vtx->w = x * mtx[0][3] + y * mtx[1][3] + z * mtx[2][3] + mtx[3][3];
	}
#else
	void gSPTransformVector_NEON(float vtx[4], float mtx[4][4]);
	void gSPTransformVertex4NEON(u32 v, float mtx[4][4]);
	if (VNUM == 1)
		gSPTransformVector_NEON(&spVtx[v].x, mtx);
	else
		gSPTransformVertex4NEON(v, mtx);
#endif /* __NEON_OPT */
}

static void VN(gSPProcessVertex)(u32 v, SPVertex * spVtx)
{
	float vPos[VNUM][4];
	u32 i;
	if (gSP.changed & CHANGED_MATRIX)
		_gSPCombineMatrices();

	for (i = 0; i < VNUM; ++i) {
		SPVertex * vtx = &spVtx[v+i];
		vPos[i][0] = vtx->x;
		vPos[i][1] = vtx->y;
		vPos[i][2] = vtx->z;
		vPos[i][3] = 0.0f;
		vtx->modify = 0;
	}

	VN(gSPTransformVertex)(v, spVtx, gSP.matrix.combined );

	if (gli_window_is_adjust_screen() && (gDP.colorImage.width > VI.width * 98 / 100)) {
		int i;
		const f32 adjustScale = gli_window_adjust_scale();
		for (i = 0; i < VNUM; ++i) {
			SPVertex * vtx = &spVtx[v+i];
			vtx->x *= adjustScale;
			if (gSP.matrix.projection[3][2] == -1.f)
				vtx->w *= adjustScale;
		}
	}
	if (gSP.viewport.vscale[0] < 0) {
		int i;
		for (i = 0; i < VNUM; ++i) {
			SPVertex * vtx = &spVtx[v+i];
			vtx->x = -vtx->x;
		}
	}
	if (gSP.viewport.vscale[1] < 0) {
		int i;
		for (i = 0; i < VNUM; ++i) {
			SPVertex * vtx = &spVtx[v+i];
			vtx->y = -vtx->y;
		}
	}

	if (gSP.matrix.billboard)
		VN(gSPBillboardVertex)(v, spVtx);

	VN(gSPApplyStereoConvergence)(v, spVtx);
	VN(gSPClipVertex)(v, spVtx);
	VN(gSPImmersiveViewport)(v, spVtx);

	if (gSP.geometryMode & G_LIGHTING) {
		if (gSP.geometryMode & G_POINT_LIGHTING)
			VN(gSPPointLightVertex)(v, vPos, spVtx);
		else
			VN(gli_gSPLightVertex)(v, spVtx);

		if (gSP.geometryMode & G_ACCLAIM_LIGHTING)
			VN(gSPPointLightVertexAcclaim)(v, spVtx);

		if ((gSP.geometryMode & G_TEXTURE_GEN) != 0) {
			if (gli_GBI->type != F3DFLX2) {
				int i;
				for (i = 0; i < VNUM; ++i) {
					SPVertex * vtx = &spVtx[v+i];
					f32 fLightDir[3];
					f32 x, y;
					fLightDir[0] = vtx->nx;
					fLightDir[1] = vtx->ny;
					fLightDir[2] = vtx->nz;
					if (gSP.lookatEnable) {
						x = DotProduct(gSP.lookat.i_xyz[0], fLightDir);
						y = DotProduct(gSP.lookat.i_xyz[1], fLightDir);
					} else {
						fLightDir[0] *= 128.0f;
						fLightDir[1] *= 128.0f;
						fLightDir[2] *= 128.0f;
						gli_TransformVectorNormalize(fLightDir, gSP.matrix.modelView[gSP.matrix.modelViewi]);
						x = fLightDir[0];
						y = fLightDir[1];
					}
					if (gSP.geometryMode & G_TEXTURE_GEN_LINEAR) {
						if (x < -1.0f) x = -1.0f;
						if (x > 1.0f) x = 1.0f;
						if (y < -1.0f) y = -1.0f;
						if (y > 1.0f) y = 1.0f;
						vtx->s = gsp_acosf(-x) * 325.94931f;
						vtx->t = gsp_acosf(-y) * 325.94931f;
					} else { /* G_TEXTURE_GEN */
						vtx->s = (x + 1.0f) * 512.0f;
						vtx->t = (y + 1.0f) * 512.0f;
					}
				}
			} else {
				int i;
				for (i = 0; i < VNUM; ++i) {
					SPVertex * vtx = &spVtx[v+i];
					const f32 intensity = DotProduct(gSP.lookat.i_xyz[0], &vtx->nx) * 128.0f;
					const s16 index = (s16)(intensity);
					vtx->a = _FIXED2FLOATCOLOR(RDRAM[((gSP.DMAIO_address + 128 + index) & RDRAMSize) ^ 3], 8);
				}
			}
		}
	} else if (gSP.geometryMode & G_ACCLAIM_LIGHTING) {
		VN(gSPPointLightVertexAcclaim)(v, spVtx);
	} else {
		u32 i;
		for (i = 0; i < VNUM; ++i)
			spVtx[v].HWLight = 0;
	}

	for (i = 0; i < VNUM; ++i) {
		SPVertex * vtx = &spVtx[v+i];
	}
}

static u32 VN(gSPLoadVertexData)(const Vertex *orgVtx, SPVertex * spVtx, u32 v0, u32 vi, u32 n)
{
	const u32 end = n - (n%VNUM) + v0;
	for (; vi < end; vi += VNUM) {
		u32 j;
		for (j = 0; j < VNUM; ++j) {
			SPVertex * vtx = &spVtx[vi+j];
			vtx->x = orgVtx->x;
			vtx->y = orgVtx->y;
			vtx->z = orgVtx->z;
			/* vtx->flag = vertex->flag; */
			vtx->s = _FIXED2FLOAT( orgVtx->s, 5 );
			vtx->t = _FIXED2FLOAT( orgVtx->t, 5 );

			if (gSP.geometryMode & G_LIGHTING) {
				vtx->nx = _FIXED2FLOATCOLOR(orgVtx->normal.x, 7);
				vtx->ny = _FIXED2FLOATCOLOR(orgVtx->normal.y, 7);
				vtx->nz = _FIXED2FLOATCOLOR(orgVtx->normal.z, 7);
				if (gli_isHWLightingAllowed()) {
					vtx->r = orgVtx->normal.x;
					vtx->g = orgVtx->normal.y;
					vtx->b = orgVtx->normal.z;
				}
			} else {
				vtx->r = _FIXED2FLOATCOLOR(orgVtx->color.r, 8);
				vtx->g = _FIXED2FLOATCOLOR(orgVtx->color.g, 8);
				vtx->b = _FIXED2FLOATCOLOR(orgVtx->color.b, 8);
			}
			vtx->a = _FIXED2FLOATCOLOR(orgVtx->color.a, 8);

			++orgVtx;
		}
		VN(gSPProcessVertex)(vi, spVtx);
	}
	return vi;
}

static u32 VN(gSPLoadCIVertexData)(const PDVertex *orgVtx, SPVertex * spVtx, u32 v0, u32 vi, u32 n)
{
	const u32 end = n - (n%VNUM) + v0;
	for (; vi < end; vi += VNUM) {
		u32 j;
		for (j = 0; j < VNUM; ++j) {
			u8 *color;
			SPVertex * vtx = &spVtx[vi+j];
			vtx->x = orgVtx->x;
			vtx->y = orgVtx->y;
			vtx->z = orgVtx->z;
			vtx->s = _FIXED2FLOAT( orgVtx->s, 5 );
			vtx->t = _FIXED2FLOAT( orgVtx->t, 5 );
			color = &RDRAM[(gSP.vertexColorBase + (orgVtx->ci & 0xff)) & RDRAMSize];

			if (gSP.geometryMode & G_LIGHTING) {
				vtx->nx = _FIXED2FLOATCOLOR((s8)color[3], 7);
				vtx->ny = _FIXED2FLOATCOLOR((s8)color[2], 7);
				vtx->nz = _FIXED2FLOATCOLOR((s8)color[1], 7);
				if (gli_isHWLightingAllowed()) {
					vtx->r = (s8)color[3];
					vtx->g = (s8)color[2];
					vtx->b = (s8)color[1];
				}
			} else {
				vtx->r = _FIXED2FLOATCOLOR(color[3], 8);
				vtx->g = _FIXED2FLOATCOLOR(color[2], 8);
				vtx->b = _FIXED2FLOATCOLOR(color[1], 8);
			}
			vtx->a = _FIXED2FLOATCOLOR(color[0], 8);

			++orgVtx;
		}
		VN(gSPProcessVertex)(vi, spVtx);
	}
	return vi;
}

static u32 VN(gSPLoadDMAVertexData)(u32 address, SPVertex * spVtx, u32 v0, u32 vi, u32 n)
{
	const u32 end = n - (n%VNUM) + v0;
	for (; vi < end; vi += VNUM) {
		u32 j;
		for (j = 0; j < VNUM; ++j) {
			SPVertex * vtx = &spVtx[vi+j];
			vtx->x = *(s16*)&RDRAM[address ^ 2];
			vtx->y = *(s16*)&RDRAM[(address + 2) ^ 2];
			vtx->z = *(s16*)&RDRAM[(address + 4) ^ 2];

			vtx->r = _FIXED2FLOATCOLOR((*(u8*)&RDRAM[(address + 6) ^ 3]), 8);
			vtx->g = _FIXED2FLOATCOLOR((*(u8*)&RDRAM[(address + 7) ^ 3]), 8);
			vtx->b = _FIXED2FLOATCOLOR((*(u8*)&RDRAM[(address + 8) ^ 3]), 8);
			vtx->a = _FIXED2FLOATCOLOR((*(u8*)&RDRAM[(address + 9) ^ 3]), 8);

			address += 10;
		}
		VN(gSPProcessVertex)(vi, spVtx);
	}
	return vi;
}

static u32 VN(gSPLoadCBFDVertexData)(const Vertex *orgVtx, SPVertex * spVtx, u32 v0, u32 vi, u32 n)
{
	const u32 end = n - (n%VNUM) + v0;
	for (; vi < end; vi += VNUM) {
		u32 j;
		for (j = 0; j < VNUM; ++j) {
			SPVertex * vtx = &spVtx[vi+j];
			vtx->x = orgVtx->x;
			vtx->y = orgVtx->y;
			vtx->z = orgVtx->z;
			vtx->s = _FIXED2FLOAT( orgVtx->s, 5 );
			vtx->t = _FIXED2FLOAT( orgVtx->t, 5 );
			vtx->flag = orgVtx->flag;
			if (gSP.geometryMode & G_LIGHTING) {
				const u32 normaleAddrOffset = ((vi+j)<<1);
				vtx->nx = _FIXED2FLOATCOLOR(((s8*)RDRAM)[(gSP.cbfd.vertexNormalBase + normaleAddrOffset + 0) ^ 3], 7);
				vtx->ny = _FIXED2FLOATCOLOR(((s8*)RDRAM)[(gSP.cbfd.vertexNormalBase + normaleAddrOffset + 1) ^ 3], 7);
				vtx->nz = _FIXED2FLOATCOLOR((s8)(orgVtx->flag & 0xFF), 7);
			}
			vtx->r = _FIXED2FLOATCOLOR(orgVtx->color.r, 8);
			vtx->g = _FIXED2FLOATCOLOR(orgVtx->color.g, 8);
			vtx->b = _FIXED2FLOATCOLOR(orgVtx->color.b, 8);
			vtx->a = _FIXED2FLOATCOLOR(orgVtx->color.a, 8);
			++orgVtx;
		}
		VN(gSPProcessVertex)(vi, spVtx);
	}
	return vi;
}

static u32 VN(gSPLoadF3DAMVertexData)(const Vertex *orgVtx, SPVertex * spVtx, u32 v0, u32 vi, u32 n)
{
	const u32 end = n - (n%VNUM) + v0;
	for (; vi < end; vi += VNUM) {
		u32 j;
		for (j = 0; j < VNUM; ++j) {
			SPVertex * vtx = &spVtx[vi+j];
			vtx->x = orgVtx->x;
			vtx->y = orgVtx->y;
			vtx->z = orgVtx->z;
			/* vtx->flag = orgVtx->flag; */
			calcF3DAMTexCoords(orgVtx, vtx);
			if (gSP.geometryMode & G_LIGHTING) {
				vtx->nx = _FIXED2FLOATCOLOR( orgVtx->normal.x, 7 );
				vtx->ny = _FIXED2FLOATCOLOR( orgVtx->normal.y, 7 );
				vtx->nz = _FIXED2FLOATCOLOR( orgVtx->normal.z, 7 );
				vtx->a = _FIXED2FLOATCOLOR(orgVtx->color.a,8);
			} else {
				vtx->r = _FIXED2FLOATCOLOR(orgVtx->color.r,8);
				vtx->g = _FIXED2FLOATCOLOR(orgVtx->color.g,8);
				vtx->b = _FIXED2FLOATCOLOR(orgVtx->color.b,8);
				vtx->a = _FIXED2FLOATCOLOR(orgVtx->color.a,8);
			}
			++orgVtx;
		}
		VN(gSPProcessVertex)(vi, spVtx);
	}
	return vi;
}

static u32 VN(gSPLoadSWVertexData)(const SWVertex *orgVtx, SPVertex * spVtx, u32 vi, u32 n)
{
	const u32 end = n - (n%VNUM);
	for (; vi < end; vi += VNUM) {
		u32 j;
		for (j = 0; j < VNUM; ++j) {
			SPVertex * vtx = &spVtx[vi+j];
			vtx->x = orgVtx->x;
			vtx->y = orgVtx->y;
			vtx->z = orgVtx->z;
			++orgVtx;
		}
		VN(gSPProcessVertex)(vi, spVtx);
		for (j = 0; j < VNUM; ++j) {
			SPVertex * vtx = &spVtx[vi+j];
			vtx->y = -vtx->y;
		}
	}
	return vi;
}
