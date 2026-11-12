#include <math.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "N64.h"
#include "GLideN64.h"
#include "Types.h"
#include "RSP.h"
#include "GBI.h"
#include "gSP.h"
#include "gDP.h"
#include "3DMath.h"
#include "CRC.h"
#include "VI.h"
#include "Config.h"
#include "gli_drawer.h"
#include "gli_framebuffer.h"
#include <retro_inline.h>

/* Immersive mode */
extern int libretro_vr_immersive_pose(float rotation[2][9], float tangents[2][4]);
extern void libretro_vr_immersive_present(int pose);

static INLINE f32 gsp_minf(f32 _a, f32 _b)
{
	return _b < _a ? _b : _a;
}

static INLINE f32 gsp_maxf(f32 _a, f32 _b)
{
	return _a < _b ? _b : _a;
}

#define gsp_fabsf(_x) ((f32)fabs((double)(_x)))
#define gsp_sqrtf(_x) ((f32)sqrt((double)(_x)))
#define gsp_floorf(_x) ((f32)floor((double)(_x)))
#define gsp_acosf(_x) ((f32)acos((double)(_x)))
#define gsp_powf(_x, _y) ((f32)pow((double)(_x), (double)(_y)))
#define gsp_log2f(_x) ((f32)(log((double)(_x)) / 0.69314718055994530942))

#define INDEXMAP_SIZE 80U

#ifdef __VEC4_OPT
#define VEC_OPT 4U
#else
#define VEC_OPT 1U
#endif

static bool g_ConkerUcode;

void gli_gSPFlushTriangles(void)
{
	if ((gSP.geometryMode & G_SHADING_SMOOTH) == 0) {
		gli_drawer_draw_triangles();
		return;
	}

	if (
		(RSP.nextCmd != G_TRI1) &&
		(RSP.nextCmd != G_TRI2) &&
		(RSP.nextCmd != G_TRIX) &&
		(RSP.nextCmd != G_QUAD)
		) {
		gli_drawer_draw_triangles();
	}
}

/* Stereoscopic 3D. The matrix supplies the eye-dependent separation * w term. The */
/* convergence term is applied after the transform, where it can be limited safely in screen space. */
/* Vertices are transformed as a row vector, so x reads column 0 and w reads column 3. */
static u32 l_stereoEye = stereoOff;

/* Which eye's pose and frustum uses in immersive mode */
static INLINE int _immersiveEye(void) { return l_stereoEye == stereoRightEye ? 1 : 0; }
static bool l_stereoPerspectiveSeen = false;

static
bool _isOrthographic(f32 matrix[4][4])
{
	return gsp_fabsf(matrix[0][3]) < 1e-6f && gsp_fabsf(matrix[1][3]) < 1e-6f && gsp_fabsf(matrix[2][3]) < 1e-6f;
}

/* Automatic convergence. */
#define kStereoBins 96
static u32 l_logWHistogram[kStereoBins];
static u32 l_logWCount = 0;
static f32 l_autoLogConvergence = 0.0f;
static bool l_haveAutoConvergence = false;
static f32 l_autoConvergence = 0.0f;

static
void _stereoMeasure(f32 w)
{
	int bin = (int)((gsp_log2f(w) + 8.0f) * 2.0f);
	if (bin > kStereoBins - 1)
		bin = kStereoBins - 1;
	if (bin < 0)
		bin = 0;
	++l_logWHistogram[bin];
	++l_logWCount;
}

static
f32 _stereoPercentile(f32 fraction)
{
	int bin;
	const u32 target = (u32)(l_logWCount * fraction);
	u32 sum = 0;
	for (bin = 0; bin < kStereoBins; ++bin) {
		sum += l_logWHistogram[bin];
		if (sum > target)
			return (bin + 0.5f) * 0.5f - 8.0f;
	}
	return (kStereoBins - 0.5f) * 0.5f - 8.0f;
}

static void _immersiveBeginFrame(void);
static void _immersiveCountVotes(void);

void gli_gSPStereoBeginTask(void)
{
	_immersiveBeginFrame();
}

/* Once per shown picture: what its display lists measured */
void gli_gSPStereoEndFrame(void)
{
	_immersiveCountVotes();

	if (l_logWCount >= 32) {
		const f32 nearEdge = _stereoPercentile(0.1f);
		l_autoLogConvergence = l_haveAutoConvergence
			? l_autoLogConvergence + (nearEdge - l_autoLogConvergence) * 0.1f
			: nearEdge;
		l_haveAutoConvergence = true;
		l_autoConvergence = exp2f(l_autoLogConvergence);
	}
	memset(l_logWHistogram, 0, sizeof(l_logWHistogram));
	l_logWCount = 0;
}

/* Immersive mode. */
static bool l_immersiveActive = false;      /* a pose arrived for this frame */
static bool l_immersiveVoted = false;
static bool l_immersiveFlat = false;
static bool _immersiveOn(void) { return l_immersiveActive && !l_immersiveFlat; }
static bool l_immersiveNeedsPose = true;    /* a frame was shown since the last pose */
static int l_immersivePose = 0;             /* id of the pose the current frame is drawn with */
static u32 l_immersivePerspective = 0;
static void _immersiveSetFlat(bool _flat, const char * _why);

/* Menus, logos and title screens should not use head tracking. */
static
bool _immersiveMenuPicture(u32 _perspective)
{
	return _perspective == 0 || VI.width > 384;
}

static f32 l_immersiveQ[2][3][3] = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
	{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};  /* camera direction -> eye direction */
static f32 l_immersiveTan[2][2] = {{1.0f, 1.0f}, {1.0f, 1.0f}};
static f32 l_immersiveCentre[2][2];
#define STEREO_FLIP_Y ((gli_GBI->type == F5Rogue || gli_GBI->type == F5Indi_Naboo) ? -1.0f : 1.0f)
static bool l_combiningMatrices = false;
static f32 l_immersiveDistance = 0.0f;
static f32 l_immersiveRefNearest = 0.0f;   /* nearest depth of the scene the game plays at */
static bool l_immersiveDistanceLocked = false;
static u32 l_immersiveDistanceFrames = 0;
static u32 l_immersiveNearestOffFrames = 0;
static f32 l_gameSx = 1.0f;
static f32 l_gameSy = 1.33f;                /* perspective matrix */
#define kImmersiveBuffers 8
static u32 l_immersiveBufferAddress[kImmersiveBuffers];
static int l_immersiveBufferPose[kImmersiveBuffers];
static int l_immersiveBufferNext = 0;
static u32 l_immersivePresentsSincePose = 0;

/* Samples the head pose once per shown frame, so every display list of a frame and both eyes use */
/* the same camera. */
static
void _immersiveBeginFrame(void)
{
	float rotation[2][9];
	float tangents[2][4];
	int pose;
	int eye;
	if (config.stereo.immersive == 0) {
		l_immersiveActive = false;
		return;
	}
	if (!l_immersiveNeedsPose && l_immersiveActive)
		return;
	if (l_immersiveActive && !l_immersiveVoted)
		_immersiveSetFlat(_immersiveMenuPicture(l_immersivePerspective), "a menu or a logo,");
	l_immersivePerspective = 0;
	pose = libretro_vr_immersive_pose(rotation, tangents);
	if (pose <= 0) {
		l_immersiveActive = false;
		return;
	}
	for (eye = 0; eye < 2; ++eye) {
		int i;
		/* tangents: left, right, up, down, all positive */
		const f32 * t = tangents[eye];
		if (t[0] + t[1] <= 0.0f || t[2] + t[3] <= 0.0f) {
			l_immersiveActive = false;
			return;
		}
		for (i = 0; i < 9; ++i)
			l_immersiveQ[eye][i / 3][i % 3] = rotation[eye][i];
		l_immersiveTan[eye][0] = (t[0] + t[1]) * 0.5f;
		l_immersiveTan[eye][1] = (t[2] + t[3]) * 0.5f;
		l_immersiveCentre[eye][0] = (t[1] - t[0]) * 0.5f;
		l_immersiveCentre[eye][1] = (t[2] - t[3]) * 0.5f;
	}
	l_immersivePose = pose;
	/* How far back the camera goes. */
	if (config.stereo.immersiveDistance > 0.0f && l_haveAutoConvergence && l_autoConvergence > 0.0f) {
		const f32 nearest = l_autoConvergence / (1.0f + config.stereo.immersiveDistance);
		if (l_immersiveRefNearest <= 0.0f) {
			l_immersiveRefNearest = nearest;
		} else if (nearest > l_immersiveRefNearest / 1.5f && nearest < l_immersiveRefNearest * 1.5f) {
			l_immersiveNearestOffFrames = 0;
			l_immersiveRefNearest += (nearest - l_immersiveRefNearest) * 0.05f;
		} else if (++l_immersiveNearestOffFrames > 60) {
			l_immersiveNearestOffFrames = 0;
			l_immersiveRefNearest = nearest;
		}
		if (!l_immersiveDistanceLocked) {
			const f32 target = config.stereo.immersiveDistance * l_immersiveRefNearest;
			l_immersiveDistance += (target - l_immersiveDistance) * 0.2f;
			if (++l_immersiveDistanceFrames > 120)
				l_immersiveDistanceLocked = true;
		}
	} else {
		l_immersiveDistance = 0.0f;
		l_immersiveRefNearest = 0.0f;
	}
	l_immersiveActive = true;
	l_immersiveNeedsPose = false;
	gSP.changed |= CHANGED_MATRIX;
}

bool gli_gSPImmersiveActive(void)
{
	return _immersiveOn() && (l_stereoEye == stereoLeftEye || l_stereoEye == stereoRightEye);
}

bool gli_gSPImmersiveIsFlat() { return l_immersiveFlat; }

/* Remembers which pose drew a color image, for when that image is shown */
void gli_gSPImmersiveFrameDrawn(u32 _address)
{
	int i;
	if (!l_immersiveActive)
		return;
	for (i = 0; i < kImmersiveBuffers; ++i) {
		if (l_immersiveBufferAddress[i] == _address) {
			l_immersiveBufferPose[i] = l_immersivePose;
			return;
		}
	}
	l_immersiveBufferAddress[l_immersiveBufferNext] = _address;
	l_immersiveBufferPose[l_immersiveBufferNext] = l_immersivePose;
	l_immersiveBufferNext = (l_immersiveBufferNext + 1) % kImmersiveBuffers;
}

/* Called just before the frame showing the image at _address is swapped out to the headset */
void gli_gSPImmersivePresent(u32 _address)
{
	int pose;
	int i;
	if (!l_immersiveActive) {
		l_immersiveNeedsPose = true;
		libretro_vr_immersive_present(0);
		return;
	}
	pose = l_immersivePose;
	for (i = 0; i < kImmersiveBuffers; ++i) {
		if (l_immersiveBufferAddress[i] == _address && l_immersiveBufferPose[i] > 0) {
			pose = l_immersiveBufferPose[i];
			break;
		}
	}
	/* A new pose once the picture drawn with the current one is on screen. */
	if (pose == l_immersivePose || ++l_immersivePresentsSincePose >= 4) {
		l_immersiveNeedsPose = true;
		l_immersivePresentsSincePose = 0;
	}
	libretro_vr_immersive_present(l_immersiveFlat ? -pose : pose);
}

/* The game's projection scales. */
typedef struct ImmersiveVote { f32 sx, sy; u32 count; } ImmersiveVote;
#define kImmersiveVotes 16
static ImmersiveVote l_immersiveVotes[kImmersiveVotes];
static int l_immersiveVoteCount = 0;

static
void _immersiveVote(f32 sx, f32 sy)
{
	int i;
	for (i = 0; i < l_immersiveVoteCount; ++i) {
		ImmersiveVote * vote = &l_immersiveVotes[i];
		if (gsp_fabsf(vote->sx - sx) < 0.01f * sx && gsp_fabsf(vote->sy - sy) < 0.01f * sy) {
			++vote->count;
			return;
		}
	}
	if (l_immersiveVoteCount < kImmersiveVotes)
		{
		l_immersiveVotes[l_immersiveVoteCount].sx = sx;
		l_immersiveVotes[l_immersiveVoteCount].sy = sy;
		l_immersiveVotes[l_immersiveVoteCount++].count = 1;
	}
	if (!l_immersiveVoted) {
		/* Nothing agreed on yet: better than the defaults */
		l_gameSx = sx;
		l_gameSy = sy;
	}
}

/* A cut scene camera with a much wider view would otherwise be shown at its own angles, far away */
/* and past the edge of what the game draws. */
static const f32 kImmersiveScaleSlack = 1.2f;
static f32 l_immersiveRefSx = 0.0f;
static f32 l_immersiveRefSy = 0.0f;
static u32 l_immersiveOffFrames = 0;
static u32 l_immersiveFlatFrames = 0;
static u32 l_immersiveSceneFrames = 0;

static
void _immersiveSetFlat(bool _flat, const char * _why)
{
	const u32 kToScreen = 3;
	const u32 kToView = 20;
	if (_flat) {
		l_immersiveSceneFrames = 0;
		if (!l_immersiveFlat && ++l_immersiveFlatFrames > kToScreen) {
			l_immersiveFlat = true;
			l_immersiveRefNearest = l_immersiveDistance = 0.0f;
			l_immersiveDistanceLocked = false;
			l_immersiveDistanceFrames = 0;
		}
	} else {
		l_immersiveFlatFrames = 0;
		if (l_immersiveFlat && ++l_immersiveSceneFrames > kToView) {
			l_immersiveFlat = false;
			l_immersiveRefNearest = l_immersiveDistance = 0.0f;
			l_immersiveDistanceLocked = false;
			l_immersiveDistanceFrames = 0;
		}
	}
}

static
void _immersiveCountVotes(void)
{
	int best;
	int match;
	int i;
	f32 dominantSx;
	f32 dominantSy;
	f32 kNarrowTangent;
	if (VI.width > 384) {
		/* Hi-res: a menu or a logo, see _immersiveMenuPicture() */
		l_immersiveVoteCount = 0;
		_immersiveSetFlat(true, "a menu or a logo,");
		return;
	}
	if (l_immersiveVoteCount == 0) {
		if (l_immersiveVoted)
			_immersiveSetFlat(true, "nothing is drawn in perspective,");
		return;
	}
	best = 0;
	match = -1;
	for (i = 0; i < l_immersiveVoteCount; ++i) {
		if (l_immersiveVotes[i].count > l_immersiveVotes[best].count)
			best = i;
		/* The camera of the frame is whichever scale still matches the one the game plays at */
		if (l_immersiveRefSx > 0.0f &&
			l_immersiveVotes[i].sx > l_immersiveRefSx / kImmersiveScaleSlack &&
			l_immersiveVotes[i].sx < l_immersiveRefSx * kImmersiveScaleSlack &&
			(match < 0 || l_immersiveVotes[i].count > l_immersiveVotes[match].count))
			match = i;
	}
	dominantSx = l_immersiveVotes[best].sx;
	dominantSy = l_immersiveVotes[best].sy;
	if (l_immersiveRefSx <= 0.0f) {
		l_immersiveRefSx = dominantSx;
		l_immersiveRefSy = dominantSy;
		l_immersiveVoted = true;
	} else if (dominantSx > l_immersiveRefSx / kImmersiveScaleSlack &&
			dominantSx < l_immersiveRefSx * kImmersiveScaleSlack) {
		l_immersiveOffFrames = 0;
		l_immersiveRefSx += (dominantSx - l_immersiveRefSx) * 0.1f;
		l_immersiveRefSy += (dominantSy - l_immersiveRefSy) * 0.1f;
	} else if (++l_immersiveOffFrames > 10) {
		l_immersiveOffFrames = 0;
		l_immersiveRefSx = dominantSx;
		l_immersiveRefSy = dominantSy;
	}

	kNarrowTangent = 0.577f;
	_immersiveSetFlat(1.0f / l_immersiveRefSx < kNarrowTangent, "the camera sees only");
	l_gameSx = l_immersiveRefSx;
	l_gameSy = l_immersiveRefSy;
	l_immersiveVoteCount = 0;
}

static
void _immersiveMeasureProjection(f32 m[4][4])
{
	f32 sx;
	f32 sy;
	const f32 lw = gsp_sqrtf(m[0][3] * m[0][3] + m[1][3] * m[1][3] + m[2][3] * m[2][3]);
	if (lw < 1e-9f)
		return;
	++l_immersivePerspective;
	sx = gsp_sqrtf(m[0][0] * m[0][0] + m[1][0] * m[1][0] + m[2][0] * m[2][0]) / lw;
	sy = gsp_sqrtf(m[0][1] * m[0][1] + m[1][1] * m[1][1] + m[2][1] * m[2][1]) / lw;
	if (sx > 0.05f && sx < 50.0f && sy > 0.05f && sy < 50.0f) {
		if (l_combiningMatrices) {
			l_gameSx = sx;
			l_gameSy = sy;
		} else {
			_immersiveVote(sx, sy);
		}
	}
}

/* Turns a direction in the game camera's view space, looking down -z, into the head's clip space */
static INLINE
void _immersiveProject(f32 dx, f32 dy, f32 dz, f32 * x, f32 * y, f32 * w)
{
	f32 ez;
	const int eye = _immersiveEye();
	f32 (*q)[3] = l_immersiveQ[eye];
	ez = q[2][0] * dx + q[2][1] * dy + q[2][2] * dz;
	/* x/w - centre spans -1..1 over the eye's frustum, with w = -ez */
	*x = (q[0][0] * dx + q[0][1] * dy + q[0][2] * dz + l_immersiveCentre[eye][0] * ez) / l_immersiveTan[eye][0];
	*y = (q[1][0] * dx + q[1][1] * dy + q[1][2] * dz + l_immersiveCentre[eye][1] * ez) / l_immersiveTan[eye][1];
	*w = -ez;
}

/* Right-multiplies a perspective combined matrix with the clip-space turn: */
/* view direction d = (x/sx, y/sy, -w), turned d' = Q*d, then x' = d'x/tan, y' = d'y/tan, w' = -d'z, */
/* and z' = z + A*(w - w') keeps the game's own depth mapping along the new line of sight. */
static
void _immersiveApply(f32 m[4][4])
{
	f32 t[4][4];
	f32 world;
	int i;
	f32 result[4][4];
	int j;
	const f32 c3 = m[0][3] * m[0][3] + m[1][3] * m[1][3] + m[2][3] * m[2][3];
	const f32 A = c3 > 0.0f ? -(m[0][2] * m[0][3] + m[1][2] * m[1][3] + m[2][2] * m[2][3]) / c3 : 0.0f;
	/* z = -A*w + B is the game's depth mapping, with the near and far plane it was built for */
	const f32 B = m[3][2] + A * m[3][3];
	if (l_immersiveDistance > 0.0f && gsp_fabsf(A) > 1e-6f && gsp_fabsf(B) > 1e-6f) {
		const f32 nearPlane = B / (A - 1.0f) + l_immersiveDistance;
		const f32 farPlane = B / (A + 1.0f) + l_immersiveDistance;
		if (nearPlane > 0.0f && farPlane > nearPlane) {
			f32 movedB;
			f32 movedA;
			int i;
			m[3][3] += l_immersiveDistance;
			movedB = -2.0f * nearPlane * farPlane / (farPlane - nearPlane);
			movedA = movedB / nearPlane + 1.0f;
			for (i = 0; i < 3; ++i)
				m[i][2] = -movedA * m[i][3];
			m[3][2] = -movedA * m[3][3] + movedB;
		}
	}
	memset(t, 0, sizeof(t));
	/* Rows are the inputs x, y, z, w */
	world = config.stereo.immersiveWorldScale;
	_immersiveProject(world / l_gameSx, 0.0f, 0.0f, &t[0][0], &t[0][1], &t[0][3]);
	_immersiveProject(0.0f, STEREO_FLIP_Y * world / l_gameSy, 0.0f, &t[1][0], &t[1][1], &t[1][3]);
	_immersiveProject(0.0f, 0.0f, -1.0f, &t[3][0], &t[3][1], &t[3][3]);
	t[2][2] = 1.0f;
	t[0][2] = -A * t[0][3];
	t[1][2] = -A * t[1][3];
	t[3][2] = A - A * t[3][3];
	for (i = 0; i < 4; ++i)
		t[i][1] *= STEREO_FLIP_Y;
	for (i = 0; i < 4; ++i)
		for (j = 0; j < 4; ++j)
			result[i][j] = m[i][0] * t[0][j] + m[i][1] * t[1][j] + m[i][2] * t[2][j] + m[i][3] * t[3][j];
	memcpy(m, result, sizeof(result));
}

/* Flat geometry and screen rectangles have no depth of their own. */
static
void _immersivePlaceFlat(f32 nx, f32 ny, f32 nz, f32 * x, f32 * y, f32 * z, f32 * w, f32 _scale)
{
	_immersiveProject(nx * _scale / l_gameSx, ny * _scale / l_gameSy, -1.0f, x, y, w);
	*z = nz * *w;
}

static
void _immersivePlaceSky(f32 nx, f32 ny, f32 nz, f32 * x, f32 * y, f32 * z, f32 * w)
{
	const f32 kEdge = 0.98f;
	const f32 kFar = 40.0f;  /* about 85 degrees at a typical game field of view */
	if (nx >= kEdge) nx = kFar; else if (nx <= -kEdge) nx = -kFar;
	if (ny >= kEdge) ny = kFar; else if (ny <= -kEdge) ny = -kFar;
	/* A background belongs to the scene, so it follows the world, not the overlay scale */
	_immersivePlaceFlat(nx, ny, nz, x, y, z, w, config.stereo.immersiveWorldScale);
}

/* Stereo separation is in normalized screen units. */
static
f32 _stereoSeparation(void)
{
	const f32 separation = l_stereoEye == stereoLeftEye
		? -config.stereo.separation : config.stereo.separation;
	return _immersiveOn() ? separation * 0.5f / l_immersiveTan[_immersiveEye()][0] : separation;
}

/* Games that install their own matrices, like Factor 5's, clear only their viewport. Immersive mode */
/* stretches that viewport over the whole picture, so the border keeps old frames around. */
bool gli_gSPImmersiveClearsShownBuffers(void)
{
	return l_immersiveActive && l_immersiveVoted;
}

/* Some microcodes draw world effects, such as Star Wars Racer's engine glow, as screen rectangles */
/* they placed themselves. Those belong in the scene, not on the HUD. */
static bool l_immersiveWorldSprite = false;

void gli_gSPImmersiveWorldSprite(bool _world)
{
	l_immersiveWorldSprite = _world;
}

/* Each content starts with nothing measured */
void gli_gSPStereoReset(void)
{
	int eye, i;
	l_stereoEye = stereoOff;
	l_stereoPerspectiveSeen = false;
	memset(l_logWHistogram, 0, sizeof(l_logWHistogram));
	l_logWCount = 0;
	l_autoLogConvergence = 0.0f;
	l_haveAutoConvergence = false;
	l_autoConvergence = 0.0f;
	l_immersiveActive = false;
	l_immersiveVoted = false;
	l_immersiveFlat = false;
	l_immersiveNeedsPose = true;
	l_immersivePose = 0;
	l_immersivePerspective = 0;
	for (eye = 0; eye < 2; ++eye) {
		for (i = 0; i < 9; ++i)
			l_immersiveQ[eye][i / 3][i % 3] = (i % 4) == 0 ? 1.0f : 0.0f;
		l_immersiveTan[eye][0] = l_immersiveTan[eye][1] = 1.0f;
		l_immersiveCentre[eye][0] = l_immersiveCentre[eye][1] = 0.0f;
	}
	l_combiningMatrices = false;
	l_immersiveDistance = 0.0f;
	l_immersiveRefNearest = 0.0f;
	l_immersiveDistanceLocked = false;
	l_immersiveDistanceFrames = 0;
	l_immersiveNearestOffFrames = 0;
	l_gameSx = 1.0f;
	l_gameSy = 1.33f;
	for (i = 0; i < kImmersiveBuffers; ++i) {
		l_immersiveBufferAddress[i] = 0;
		l_immersiveBufferPose[i] = 0;
	}
	l_immersiveBufferNext = 0;
	l_immersivePresentsSincePose = 0;
	l_immersiveVoteCount = 0;
	l_immersiveRefSx = 0.0f;
	l_immersiveRefSy = 0.0f;
	l_immersiveOffFrames = 0;
	l_immersiveFlatFrames = 0;
	l_immersiveSceneFrames = 0;
	l_immersiveWorldSprite = false;
}

bool gli_gSPImmersiveBackground(void)
{
	return !l_stereoPerspectiveSeen;
}

/* Screen space triangles carry no depth of their own, but the game put them where its camera saw */
/* them, so their direction is known. Factor 5 draws engine glows and other particles this way. */
void gli_gSPImmersiveScreenTriangles(SPVertex * _vertices, u32 _count)
{
	f32 halfWidth;
	f32 halfHeight;
	u32 i;
	if (!gli_gSPImmersiveActive() || !l_immersiveVoted)
		return;
	halfWidth = (f32)(gDP.colorImage.width) * 0.5f;
	halfHeight = (VI.height > 0 ? (f32)(VI.height) : halfWidth * 1.5f) * 0.5f;
	for (i = 0; i < _count; ++i) {
		SPVertex * vtx = &_vertices[i];
		f32 x, y, z, w;
		_immersivePlaceFlat(vtx->x / halfWidth - 1.0f, 1.0f - vtx->y / halfHeight,
			vtx->z, &x, &y, &z, &w, config.stereo.immersiveWorldScale);
		if (w <= 0.0f)
			continue;
		/* The shader multiplies these by w again, so they stay plain pixels */
		vtx->x = (1.0f + x / w) * halfWidth;
		vtx->y = (1.0f - y / w) * halfHeight;
		vtx->w = w;
	}
}

void gli_gSPImmersiveRect(RectVertex * _vertices, u32 _count, bool _fullScreen)
{
	bool world;
	f32 width;
	f32 height;
	u32 i;
	if (!gli_gSPImmersiveActive())
		return;
	width = (f32)(gDP.colorImage.width);
	height = VI.height > 0 ? (f32)(VI.height) : width * 0.75f;
	if (!l_stereoPerspectiveSeen) {
		f32 separation;
		u32 i;
		/* Drawn before any 3D: a clear or a background such as a sky, spread over the sky plane at */
		/* infinity. The game's screen is its scissor box. */
		const f32 x0 = gDP.scissor.ulx, x1 = gDP.scissor.lrx;
		const f32 y0 = gDP.scissor.uly, y1 = gDP.scissor.lry;
		if (x1 - x0 < 8.0f || y1 - y0 < 8.0f)
			return;
		separation = _stereoSeparation();
		for (i = 0; i < _count; ++i) {
			RectVertex * v = &_vertices[i];
			f32 x, y, z, w;
			_immersivePlaceSky((v->x - x0) / (x1 - x0) * 2.0f - 1.0f, 1.0f - (v->y - y0) / (y1 - y0) * 2.0f,
				v->z, &x, &y, &z, &w);
			x += separation * w;
			v->x = (w + x) * width * 0.5f;
			v->y = (w - y) * height * 0.5f;
			v->z = z;
			v->w = w;
		}
		return;
	}
	if (_count > 0) {
		u32 i;
		f32 sw;
		f32 sh;
		/* Games fade, flash and darken with an overlay over their whole viewport, as GoldenEye does */
		/* between the shots of a level's intro. */
		f32 x0 = _vertices[0].x, x1 = x0, y0 = _vertices[0].y, y1 = y0;
		for (i = 1; i < _count; ++i) {
			x0 = gsp_minf(x0, _vertices[i].x); x1 = gsp_maxf(x1, _vertices[i].x);
			y0 = gsp_minf(y0, _vertices[i].y); y1 = gsp_maxf(y1, _vertices[i].y);
		}
		sw = gDP.scissor.lrx - gDP.scissor.ulx;
		sh = gDP.scissor.lry - gDP.scissor.uly;
		if (sw > 8.0f && sh > 8.0f && x1 - x0 >= 0.9f * sw && y1 - y0 >= 0.9f * sh) {
			u32 i;
			for (i = 0; i < _count; ++i) {
				RectVertex * v = &_vertices[i];
				f32 x, y, z, w;
				_immersivePlaceSky((v->x - gDP.scissor.ulx) / sw * 2.0f - 1.0f,
					1.0f - (v->y - gDP.scissor.uly) / sh * 2.0f, v->z, &x, &y, &z, &w);
				v->x = (w + x) * width * 0.5f;
				v->y = (w - y) * height * 0.5f;
				v->z = z;
				v->w = w;
			}
			return;
		}
	}
	/* A rectangle that is tested against the depth of the scene belongs to the world, like Star Wars */
	/* Racer's engine glow, not to the HUD. Only for games whose own matrices are followed, so the */
	/* plain path stays as it was. */
	world = l_immersiveWorldSprite || (l_immersiveVoted &&
		(gDP.otherMode.depthSource == G_ZS_PRIM || gDP.otherMode.depthCompare != 0));
	for (i = 0; i < _count; ++i) {
		RectVertex * v = &_vertices[i];
		const f32 nx = v->x / (width * 0.5f) - 1.0f;
		const f32 ny = 1.0f - v->y / (height * 0.5f);
		f32 x, y, z, w;
		_immersivePlaceFlat(nx, ny, v->z, &x, &y, &z, &w,
			world ? config.stereo.immersiveWorldScale : config.stereo.immersiveHudScale);
		/* Back to homogeneous N64 pixels, which the rectangle shader divides out */
		v->x = (w + x) * width * 0.5f;
		v->y = (w - y) * height * 0.5f;
		v->z = z;
		v->w = w;
	}
}

void gli_gSPSetStereoEye(u32 _eye)
{
	l_stereoPerspectiveSeen = false;
	if (l_stereoEye == _eye)
		return;
	l_stereoEye = _eye;
	gSP.changed |= CHANGED_MATRIX;
}

void gli_gSPApplyStereo(f32 matrix[4][4])
{
	f32 separation;
	int i;
	if (l_stereoEye != stereoLeftEye && l_stereoEye != stereoRightEye)
		return;

	if (!_isOrthographic(matrix)) {
		if (l_immersiveActive) {
			/* Measured on the projection when it holds the perspective. */
			_immersiveMeasureProjection(l_combiningMatrices && !_isOrthographic(gSP.matrix.projection)
				? gSP.matrix.projection : matrix);
		}
		if (_immersiveOn())
			_immersiveApply(matrix);
	}

	separation = _stereoSeparation();
	for (i = 0; i < 4; ++i)
		matrix[i][0] += separation * matrix[i][3];
}

static
void _gSPCombineMatrices(void)
{
	gli_MultMatrix(gSP.matrix.projection, gSP.matrix.modelView[gSP.matrix.modelViewi], gSP.matrix.combined);
	l_combiningMatrices = true;
	gli_gSPApplyStereo(gSP.matrix.combined);
	l_combiningMatrices = false;
	gSP.changed &= ~CHANGED_MATRIX;
}

void gli_gSPCombineMatrices(u32 _mode)
{
	if (_mode == 1)
		_gSPCombineMatrices();
}

void gli_gSPTriangle(s32 v0, s32 v1, s32 v2)
{
	if ((v0 < INDEXMAP_SIZE) && (v1 < INDEXMAP_SIZE) && (v2 < INDEXMAP_SIZE)) {
		if (GLI_IS_CLIPPED(gli_drawer_vertices(), v0, v1, v2)) {
			return;
		}
		if (gli_drawer_is_rejected(v0, v1, v2)) {
			return;
		}
		gli_drawer_add_triangle(v0, v1, v2);
	}
}

void gli_gSP1Triangle( const s32 v0, const s32 v1, const s32 v2)
{

	gli_gSPTriangle( v0, v1, v2);
	gli_gSPFlushTriangles();
}

void gli_gSP2Triangles(const s32 v00, const s32 v01, const s32 v02, const s32 flag0,
				   const s32 v10, const s32 v11, const s32 v12, const s32 flag1 )
{

	gli_gSPTriangle( v00, v01, v02);
	gli_gSPTriangle( v10, v11, v12);
	gli_gSPFlushTriangles();
}

void gli_gSP4Triangles(const s32 v00, const s32 v01, const s32 v02,
				   const s32 v10, const s32 v11, const s32 v12,
				   const s32 v20, const s32 v21, const s32 v22,
				   const s32 v30, const s32 v31, const s32 v32 )
{

	gli_gSPTriangle(v00, v01, v02);
	gli_gSPTriangle(v10, v11, v12);
	gli_gSPTriangle(v20, v21, v22);
	gli_gSPTriangle(v30, v31, v32);
	gli_gSPFlushTriangles();
}

gSPInfo gSP;

static
f32 identityMatrix[4][4] =
{
	{ 1.0f, 0.0f, 0.0f, 0.0f },
	{ 0.0f, 1.0f, 0.0f, 0.0f },
	{ 0.0f, 0.0f, 1.0f, 0.0f },
	{ 0.0f, 0.0f, 0.0f, 1.0f }
};

void gli_gSPLoadUcodeEx( u32 uc_start, u32 uc_dstart, u16 uc_dsize )
{
	gSP.matrix.modelViewi = 0;
	gSP.status[0] = gSP.status[1] = gSP.status[2] = gSP.status[3] = 0;
	gSP.fog.multiplier = gSP.fog.offset = 0;
	gSP.fog.multiplierf = gSP.fog.offsetf = 0.0f;
	gSP.geometryMode = 0U;
	gSP.changed |= CHANGED_MATRIX | CHANGED_LIGHT | CHANGED_LOOKAT | CHANGED_GEOMETRYMODE;

	if ((((uc_start & 0x1FFFFFFF) + 4096) > RDRAMSize) || (((uc_dstart & 0x1FFFFFFF) + uc_dsize) > RDRAMSize)) {
		return;
	}

	gli_GBI_loadMicrocode(uc_start, uc_dstart, uc_dsize);
	RSP.uc_start = uc_start;
	RSP.uc_dstart = uc_dstart;

}

void gli_gSPNoOp(void)
{
	gli_gSPFlushTriangles();
}

void gli_gSPMatrix( u32 matrix, u8 param )
{

	f32 mtx[4][4];
	u32 address = RSP_SegmentToPhysical( matrix );

	if (address + 64 > RDRAMSize) {
		return;
	}

	gli_RSP_LoadMatrix( mtx, address );

	if (param & G_MTX_PROJECTION) {
		if (param & G_MTX_LOAD)
			gli_CopyMatrix( gSP.matrix.projection, mtx );
		else
			gli_MultMatrix2( gSP.matrix.projection, mtx );
	} else {
		if ((param & G_MTX_PUSH)) {
			if (gSP.matrix.modelViewi < (gSP.matrix.stackSize)) {
				gli_CopyMatrix(gSP.matrix.modelView[gSP.matrix.modelViewi + 1], gSP.matrix.modelView[gSP.matrix.modelViewi]);
				gSP.matrix.modelViewi++;
			}
		}

		if (param & G_MTX_LOAD)
			gli_CopyMatrix( gSP.matrix.modelView[gSP.matrix.modelViewi], mtx );
		else
			gli_MultMatrix2( gSP.matrix.modelView[gSP.matrix.modelViewi], mtx );
		gSP.changed |= CHANGED_LIGHT | CHANGED_LOOKAT;
	}

	gSP.changed |= CHANGED_MATRIX;

}

void gli_gSPDMAMatrix( u32 matrix, u8 index, u8 multiply )
{
	f32 mtx[4][4];
	u32 address = gSP.DMAOffsets.mtx + RSP_SegmentToPhysical( matrix );

	if (address + 64 > RDRAMSize) {
		return;
	}

	if (index >= 32)
		return;

	gli_RSP_LoadMatrix(mtx, address);

	gSP.matrix.modelViewi = index;

	if (multiply)
		gli_MultMatrix(gSP.matrix.modelView[0], mtx, gSP.matrix.modelView[gSP.matrix.modelViewi]);
	else
		gli_CopyMatrix( gSP.matrix.modelView[gSP.matrix.modelViewi], mtx );

	gli_CopyMatrix( gSP.matrix.projection, identityMatrix );


	gSP.changed |= CHANGED_MATRIX | CHANGED_LIGHT | CHANGED_LOOKAT;

}

void gli_gSPViewport( u32 v )
{
	u32 address = RSP_SegmentToPhysical( v );

	if ((address + 16) > RDRAMSize) {
		return;
	}

	gSP.viewport.vscale[0] = _FIXED2FLOAT( *(s16*)&RDRAM[address +  2], 2 );
	gSP.viewport.vscale[1] = _FIXED2FLOAT( *(s16*)&RDRAM[address     ], 2 );
	gSP.viewport.vscale[2] = _FIXED2FLOAT( *(s16*)&RDRAM[address +  6], 10 );/* * 0.00097847357f; */
	gSP.viewport.vscale[3] = *(s16*)&RDRAM[address +  4];
	gSP.viewport.vtrans[0] = _FIXED2FLOAT( *(s16*)&RDRAM[address + 10], 2 );
	gSP.viewport.vtrans[1] = _FIXED2FLOAT( *(s16*)&RDRAM[address +  8], 2 );
	gSP.viewport.vtrans[2] = _FIXED2FLOAT( *(s16*)&RDRAM[address + 14], 10 );/* * 0.00097847357f; */
	gSP.viewport.vtrans[3] = *(s16*)&RDRAM[address + 12];

	if (gSP.viewport.vscale[1] < 0.0f && !gli_GBI->negativeY)
		gSP.viewport.vscale[1] = -gSP.viewport.vscale[1];

	gSP.viewport.x		= gSP.viewport.vtrans[0] - gSP.viewport.vscale[0];
	gSP.viewport.y		= gSP.viewport.vtrans[1] - gSP.viewport.vscale[1];
	gSP.viewport.width = fabs(gSP.viewport.vscale[0]) * 2;
	gSP.viewport.height	= fabs(gSP.viewport.vscale[1] * 2);
	gSP.viewport.nearz	= gSP.viewport.vtrans[2] - gSP.viewport.vscale[2];
	gSP.viewport.farz	= (gSP.viewport.vtrans[2] + gSP.viewport.vscale[2]) ;

	gSP.changed |= CHANGED_VIEWPORT;

}

void gli_gSPForceMatrix( u32 mptr )
{
	u32 address = RSP_SegmentToPhysical( mptr );

	if (address + 64 > RDRAMSize) {
		return;
	}

	gli_RSP_LoadMatrix(gSP.matrix.combined, address);
	gli_gSPApplyStereo(gSP.matrix.combined);

	gSP.changed &= ~CHANGED_MATRIX;

}

void gli_gSPLight( u32 l, s32 n )
{
	u32 addrByte;
	Light *light;
	--n;
	addrByte = RSP_SegmentToPhysical( l );

	if ((addrByte + sizeof( Light )) > RDRAMSize) {
		return;
	}

	light = (Light*)&RDRAM[addrByte];

	if (n >= 0 && n < 8) {
		u32 addrShort;
		gSP.lights.rgb[n][R] = _FIXED2FLOATCOLOR(light->r,8);
		gSP.lights.rgb[n][G] = _FIXED2FLOATCOLOR(light->g,8);
		gSP.lights.rgb[n][B] = _FIXED2FLOATCOLOR(light->b,8);

		gSP.lights.xyz[n][X] = light->x;
		gSP.lights.xyz[n][Y] = light->y;
		gSP.lights.xyz[n][Z] = light->z;

		gli_Normalize( gSP.lights.xyz[n] );
		addrShort = addrByte >> 1;
		gSP.lights.pos_xyzw[n][X] = (float)(((short*)RDRAM)[(addrShort+4)^1]);
		gSP.lights.pos_xyzw[n][Y] = (float)(((short*)RDRAM)[(addrShort+5)^1]);
		gSP.lights.pos_xyzw[n][Z] = (float)(((short*)RDRAM)[(addrShort+6)^1]);
		gSP.lights.ca[n] = (float)(RDRAM[(addrByte +  3) ^ 3]);
		gSP.lights.la[n] = (float)(RDRAM[(addrByte +  7) ^ 3]);
		gSP.lights.qa[n] = (float)(RDRAM[(addrByte + 14) ^ 3]);
	}

	gSP.changed |= CHANGED_LIGHT;

}

void gli_gSPLightCBFD( u32 l, s32 n )
{
	Light *light;
	u32 addrByte = RSP_SegmentToPhysical( l );

	if ((addrByte + sizeof( Light )) > RDRAMSize) {
		return;
	}

	light = (Light*)&RDRAM[addrByte];

	if (n >= 0 && n < 12) {
		u32 addrShort;
		gSP.lights.rgb[n][R] = _FIXED2FLOATCOLOR(light->r, 8);
		gSP.lights.rgb[n][G] = _FIXED2FLOATCOLOR(light->g, 8);
		gSP.lights.rgb[n][B] = _FIXED2FLOATCOLOR(light->b, 8);

		gSP.lights.xyz[n][X] = light->x;
		gSP.lights.xyz[n][Y] = light->y;
		gSP.lights.xyz[n][Z] = light->z;

		gli_Normalize( gSP.lights.xyz[n] );
		addrShort = addrByte >> 1;
		gSP.lights.pos_xyzw[n][X] = (float)(((short*)RDRAM)[(addrShort+16)^1]);
		gSP.lights.pos_xyzw[n][Y] = (float)(((short*)RDRAM)[(addrShort+17)^1]);
		gSP.lights.pos_xyzw[n][Z] = (float)(((short*)RDRAM)[(addrShort+18)^1]);
		gSP.lights.pos_xyzw[n][W] = (float)(((short*)RDRAM)[(addrShort+19)^1]);
		gSP.lights.ca[n] = (float)(RDRAM[(addrByte + 12) ^ 3]) / 16.0f;
	}

	gSP.changed |= CHANGED_LIGHT;

}

void gli_gSPLightAcclaim(u32 l, s32 n)
{
	u32 addrByte = RSP_SegmentToPhysical(l);

	if ((addrByte + 16) > RDRAMSize)
		return;

	if (n >= 0 && n < 10) {
		const u32 addrShort = addrByte >> 1;
		gSP.lights.pos_xyzw[n][X] = (f32)(((s16*)RDRAM)[(addrShort + 0) ^ 1]);
		gSP.lights.pos_xyzw[n][Y] = (f32)(((s16*)RDRAM)[(addrShort + 1) ^ 1]);
		gSP.lights.pos_xyzw[n][Z] = (f32)(((s16*)RDRAM)[(addrShort + 2) ^ 1]);
		gSP.lights.ca[n] = (f32)(((s16*)RDRAM)[(addrShort + 5) ^ 1]);
		gSP.lights.la[n] = _FIXED2FLOAT((((u16*)RDRAM)[(addrShort + 6) ^ 1]), 16);
		gSP.lights.qa[n] = (f32)(((u16*)RDRAM)[(addrShort + 7) ^ 1]);
		gSP.lights.rgb[n][R] = _FIXED2FLOATCOLOR((RDRAM[(addrByte + 6) ^ 3]), 8);
		gSP.lights.rgb[n][G] = _FIXED2FLOATCOLOR((RDRAM[(addrByte + 7) ^ 3]), 8);
		gSP.lights.rgb[n][B] = _FIXED2FLOATCOLOR((RDRAM[(addrByte + 8) ^ 3]), 8);
	}

	gSP.changed |= CHANGED_LIGHT;

}

void gli_gSPLookAt( u32 _l, u32 _n )
{
	Light *light;
	u32 address = RSP_SegmentToPhysical(_l);

	if ((address + sizeof(Light)) > RDRAMSize) {
		return;
	}
	if (_n >= 2)
		return;

	light = (Light*)&RDRAM[address];

	gSP.lookat.xyz[_n][X] = light->x;
	gSP.lookat.xyz[_n][Y] = light->y;
	gSP.lookat.xyz[_n][Z] = light->z;

	gSP.lookatEnable = (_n == 0) || (_n == 1 && (light->x != 0 || light->y != 0));

	gli_Normalize(gSP.lookat.xyz[_n]);
	gSP.changed |= CHANGED_LOOKAT;
}

static
void gSPUpdateLightVectors(void)
{
	gli_InverseTransformVectorNormalizeN(&gSP.lights.xyz[0], &gSP.lights.i_xyz[0], 
			gSP.matrix.modelView[gSP.matrix.modelViewi], gSP.numLights);
	gSP.changed ^= CHANGED_LIGHT;
	gSP.changed |= CHANGED_HW_LIGHT;
}

static
void gSPUpdateLookatVectors(void)
{
	if (gSP.lookatEnable) {
		gli_InverseTransformVectorNormalizeN(&gSP.lookat.xyz[0], &gSP.lookat.i_xyz[0],
				gSP.matrix.modelView[gSP.matrix.modelViewi], 2);
	}
	gSP.changed ^= CHANGED_LOOKAT;
}

/*---------------------------------Vertex Load------------------------------------*/

static
void gSPTransformVector_default(float vtx[4], float mtx[4][4])
{
	const float x = vtx[0];
	const float y = vtx[1];
	const float z = vtx[2];

	vtx[0] = x * mtx[0][0] + y * mtx[1][0] + z * mtx[2][0] + mtx[3][0];
	vtx[1] = x * mtx[0][1] + y * mtx[1][1] + z * mtx[2][1] + mtx[3][1];
	vtx[2] = x * mtx[0][2] + y * mtx[1][2] + z * mtx[2][2] + mtx[3][2];
	vtx[3] = x * mtx[0][3] + y * mtx[1][3] + z * mtx[2][3] + mtx[3][3];
}

static
void gSPInverseTransformVector_default(float vec[3], float mtx[4][4])
{
	const float x = vec[0];
	const float y = vec[1];
	const float z = vec[2];

	vec[0] = mtx[0][0] * x + mtx[0][1] * y + mtx[0][2] * z;
	vec[1] = mtx[1][0] * x + mtx[1][1] * y + mtx[1][2] * z;
	vec[2] = mtx[2][0] * x + mtx[2][1] * y + mtx[2][2] * z;
}

#define VN3(_name, _n) _name##_##_n
#define VN2(_name, _n) VN3(_name, _n)
#define VN(_name) VN2(_name, VNUM)
#define VEC_NAME(_name) VN2(_name, VEC_OPT)

static void calcF3DAMTexCoords(const Vertex * _vertex, SPVertex * _vtx);

#define VNUM 1U
#include "gSP_vertex.inc"
#undef VNUM
#ifdef __VEC4_OPT
#define VNUM 4U
#include "gSP_vertex.inc"
#undef VNUM
#endif

void gli_gSPLightVertex(SPVertex * _vtx)
{
	gli_gSPLightVertex_1U(0, _vtx);
}

/* Immersive mode computes positions for the whole picture. */
void gli_gSPVertex(u32 a, u32 n, u32 v0)
{
	u32 address;
	const Vertex *vertex;
	SPVertex * spVtx;
	u32 i;

	if ((n + v0) > INDEXMAP_SIZE) {
		return;
	}

	address = RSP_SegmentToPhysical(a);

	if ((address + sizeof(Vertex)* n) > RDRAMSize) {
		return;
	}

	if ((gSP.geometryMode & G_LIGHTING) != 0) {

		if ((gSP.changed & CHANGED_LIGHT) != 0)
			gSPUpdateLightVectors();

		if (((gSP.geometryMode & G_TEXTURE_GEN) != 0) && ((gSP.changed & CHANGED_LOOKAT) != 0))
			gSPUpdateLookatVectors();
	}

	vertex = (Vertex*)&RDRAM[address];
	spVtx = gli_drawer_vertices();
	i = VEC_NAME(gSPLoadVertexData)(vertex, spVtx, v0, v0, n);
	if (i < n + v0)
		gSPLoadVertexData_1U(vertex + (i - v0), spVtx, v0, i, n);
}

void gli_gSPCIVertex( u32 a, u32 n, u32 v0 )
{
	u32 address;
	const PDVertex *vertex;
	SPVertex * spVtx;
	u32 i;

	if ((n + v0) > INDEXMAP_SIZE) {
		return;
	}

	address = RSP_SegmentToPhysical( a );

	if ((address + sizeof( PDVertex ) * n) > RDRAMSize)
		return;

	if ((gSP.geometryMode & G_LIGHTING) != 0) {

		if ((gSP.changed & CHANGED_LIGHT) != 0)
			gSPUpdateLightVectors();

		if (((gSP.geometryMode & G_TEXTURE_GEN) != 0) && ((gSP.changed & CHANGED_LOOKAT) != 0))
			gSPUpdateLookatVectors();
	}

	vertex = (PDVertex*)&RDRAM[address];
	spVtx = gli_drawer_vertices();
	i = VEC_NAME(gSPLoadCIVertexData)(vertex, spVtx, v0, v0, n);
	if (i < n + v0)
		gSPLoadCIVertexData_1U(vertex + (i - v0), spVtx, v0, i, n);
}


void gli_gSPDMAVertex( u32 a, u32 n, u32 v0 )
{
	u32 address;
	SPVertex * spVtx;
	u32 i;

	if ((n + v0) > INDEXMAP_SIZE) {
		return;
	}

	address = gSP.DMAOffsets.vtx + RSP_SegmentToPhysical(a);

	if ((address + 10 * n) > RDRAMSize)
		return;

	spVtx = gli_drawer_vertices();
	i = VEC_NAME(gSPLoadDMAVertexData)(address, spVtx, v0, v0, n);
	if (i < n + v0)
		gSPLoadDMAVertexData_1U(address + (i - v0) * 10, spVtx, v0, i, n);
}

void gli_gSPCBFDVertex( u32 a, u32 n, u32 v0 )
{
	u32 address;
	const Vertex *vertex;
	SPVertex * spVtx;
	u32 i;

	if ((n + v0) > INDEXMAP_SIZE) {
		return;
	}

	address = RSP_SegmentToPhysical(a);

	if ((address + sizeof( Vertex ) * n) > RDRAMSize)
		return;

	if ((gSP.geometryMode & G_LIGHTING) != 0) {

		if ((gSP.changed & CHANGED_LIGHT) != 0)
			gSPUpdateLightVectors();

		if (((gSP.geometryMode & G_TEXTURE_GEN) != 0) && ((gSP.changed & CHANGED_LOOKAT) != 0))
			gSPUpdateLookatVectors();
	}

	vertex = (Vertex*)&RDRAM[address];
	spVtx = gli_drawer_vertices();
	i = VEC_NAME(gSPLoadCBFDVertexData)(vertex, spVtx, v0, v0, n);
	if (i < n + v0)
		gSPLoadCBFDVertexData_1U(vertex + (i - v0), spVtx, v0, i, n);
}

static
void calcF3DAMTexCoords(const Vertex * _vertex, SPVertex * _vtx)
{
	const u32 s0 = (u32)_vertex->s;
	const u32 t0 = (u32)_vertex->t;
	const u32 acum_0 = ((_SHIFTR(gSP.textureCoordScaleOrg, 0, 16) * t0) << 1) + 0x8000;
	const u32 acum_1 = ((_SHIFTR(gSP.textureCoordScale[1], 0, 16) * t0) << 1) + 0x8000;
	const u32 sres = ((_SHIFTR(gSP.textureCoordScaleOrg, 16, 16) * s0) << 1) + acum_0;
	const u32 tres = ((_SHIFTR(gSP.textureCoordScale[1], 16, 16) * s0) << 1) + acum_1;
	const s16 s = _SHIFTR(sres, 16, 16) + _SHIFTR(gSP.textureCoordScale[0], 16, 16);
	const s16 t = _SHIFTR(tres, 16, 16) + _SHIFTR(gSP.textureCoordScale[0], 0, 16);

	_vtx->s = _FIXED2FLOAT( s, 5 );
	_vtx->t = _FIXED2FLOAT( t, 5 );
}

void gli_gSPF3DAMVertex(u32 a, u32 n, u32 v0)
{
	u32 address;
	const Vertex *vertex;
	SPVertex * spVtx;
	u32 i;

	if ((n + v0) > INDEXMAP_SIZE) {
		return;
	}

	address = RSP_SegmentToPhysical(a);

	if ((address + sizeof(Vertex)* n) > RDRAMSize) {
		return;
	}

	if ((gSP.geometryMode & G_LIGHTING) != 0) {

		if ((gSP.changed & CHANGED_LIGHT) != 0)
			gSPUpdateLightVectors();

		if (((gSP.geometryMode & G_TEXTURE_GEN) != 0) && ((gSP.changed & CHANGED_LOOKAT) != 0))
			gSPUpdateLookatVectors();
	}

	vertex = (Vertex*)&RDRAM[address];
	spVtx = gli_drawer_vertices();
	i = VEC_NAME(gSPLoadF3DAMVertexData)(vertex, spVtx, v0, v0, n);
	if (i < n + v0)
		gSPLoadF3DAMVertexData_1U(vertex + (i - v0), spVtx, v0, i, n);
}

static void gsp_sw_vertex_selected(const SWVertex * vertex, u32 n, const bool * const verticesToProcess);
static void gsp_sw_vertex_at(const SWVertex * vertex, u32 v0, u32 n);

void gli_gSPSWVertexAt(const SWVertex * vertex, u32 v0, u32 n)
{
	gsp_sw_vertex_at(vertex, v0, n);
}

void gli_gSPSWVertexSelected(const SWVertex * vertex, u32 n, const bool * verticesToProcess)
{
	gsp_sw_vertex_selected(vertex, n, verticesToProcess);
}

void gli_gSPSWVertexN(const SWVertex * vertex, u32 n)
{
	gsp_sw_vertex_selected(vertex, n, NULL);
}

static void gsp_sw_vertex_selected(const SWVertex * vertex, u32 n, const bool * const verticesToProcess)
{

	SPVertex * spVtx = gli_drawer_vertices();
	if (verticesToProcess == NULL) {
		u32 i = VEC_NAME(gSPLoadSWVertexData)(vertex, spVtx, 0, n);
		if (i < n)
			gSPLoadSWVertexData_1U(vertex + i, spVtx, i, n);
	} else {
		u32 i;
		for (i = 0; i < n; ++i) {
			if (verticesToProcess[i])
				gSPLoadSWVertexData_1U(vertex + i, spVtx, i, i + 1);
		}
	}
}

static void gsp_sw_vertex_at(const SWVertex * vertex, u32 v0, u32 n)
{

	SPVertex * spVtx = gli_drawer_vertices();
	const u32 endIdx = v0 + n;
	u32 i = VEC_NAME(gSPLoadSWVertexData)(vertex, spVtx, v0, endIdx);
	if (i < endIdx)
		gSPLoadSWVertexData_1U(vertex + i - v0, spVtx, i, endIdx);
}

void gli_gSPT3DUXVertex(u32 a, u32 n, u32 ci)
{
	SPVertex * spVtx;
	u32 i;
	const u32 address = RSP_SegmentToPhysical(a);
	const u32 colors = RSP_SegmentToPhysical(ci);

	struct T3DUXVertex {
		s16 y;
		s16 x;
		u16 flag;
		s16 z;
	} *vertex = (struct T3DUXVertex*)&RDRAM[address];

	struct T3DUXColor
	{
		u8 a;
		u8 b;
		u8 g;
		u8 r;
	} *color = (struct T3DUXColor*)&RDRAM[colors];

	/* The sibling vertex loaders bound the destination to INDEXMAP_SIZE
	 * (here v0 is implicitly 0) and both source spans to RDRAM. This one
	 * only checked the vertex source: add the destination and colour
	 * source bounds too. */
	if (n > INDEXMAP_SIZE)
		return;

	if ((address + sizeof(struct T3DUXVertex)* n) > RDRAMSize)
		return;

	if ((colors + sizeof(struct T3DUXColor)* n) > RDRAMSize)
		return;

	spVtx = gli_drawer_vertices();
	i = 0;
#ifdef __VEC4_OPT
	for (; i < n - (n % 4); i += 4) {
		int j;
		u32 v = i;
		for (j = 0; j < 4; ++j) {
			SPVertex * vtx = &spVtx[v+j];
			vtx->x = vertex->x;
			vtx->y = vertex->y;
			vtx->z = vertex->z;
			vtx->s = 0;
			vtx->t = 0;
			vtx->r = _FIXED2FLOATCOLOR(color->r, 8);
			vtx->g = _FIXED2FLOATCOLOR(color->g, 8);
			vtx->b = _FIXED2FLOATCOLOR(color->b, 8);
			vtx->a = _FIXED2FLOATCOLOR(color->a, 8);
			vertex++;
			color++;
		}
		gSPProcessVertex_4U(v, spVtx);
	}
#endif
	for (; i < n; ++i) {
		SPVertex * vtx = &spVtx[i];
		vtx->x = vertex->x;
		vtx->y = vertex->y;
		vtx->z = vertex->z;
		vtx->s = 0;
		vtx->t = 0;
		vtx->r = _FIXED2FLOATCOLOR(color->r, 8);
		vtx->g = _FIXED2FLOATCOLOR(color->g, 8);
		vtx->b = _FIXED2FLOATCOLOR(color->b, 8);
		vtx->a = _FIXED2FLOATCOLOR(color->a, 8);
		gSPProcessVertex_1U(i, spVtx);
		vertex++;
		color++;
	}
}

void gli_gSPDisplayList( u32 dl )
{
	u32 address = RSP_SegmentToPhysical( dl );

	if ((address + 8) > RDRAMSize) {
		return;
	}

	if (RSP.PCi < (gli_GBIPCStackSize - 1)) {
		RSP.PCi++;
		RSP.PC[RSP.PCi] = address;
		RSP.nextCmd = _SHIFTR( *(u32*)&RDRAM[address], 24, 8 );
	} else {
		assert(false);
	}
}

void gli_gSPBranchList( u32 dl )
{
	u32 address = RSP_SegmentToPhysical( dl );

	if ((address + 8) > RDRAMSize) {
		return;
	}


	if (address == (RSP.PC[RSP.PCi] - 8)) {
		RSP.infloop = true;
		RSP.PC[RSP.PCi] -= 8;
		RSP.halt = true;
		return;
	}

	RSP.PC[RSP.PCi] = address;
	RSP.nextCmd = _SHIFTR( *(u32*)&RDRAM[address], 24, 8 );
}

void gli_gSPBranchLessZ(u32 branchdl, u32 vtx, u32 zval)
{
	SPVertex * v;
	u32 zTest;
	const u32 address = RSP_SegmentToPhysical( branchdl );

	if ((address + 8) > RDRAMSize) {
		return;
	}

	v = &gli_drawer_vertices()[vtx];
	zTest = (u32)((v->z / v->w) * 1023.0f);
	if (zTest > 0x03FF || zTest <= zval)
		RSP.PC[RSP.PCi] = address;

}

void gli_gSPBranchLessW( u32 branchdl, u32 vtx, u32 wval )
{
	SPVertex * v;
	const u32 address = RSP_SegmentToPhysical( branchdl );

	if ((address + 8) > RDRAMSize) {
		return;
	}

	v = &gli_drawer_vertices()[vtx];
	if (v->w < (float)wval)
		RSP.PC[RSP.PCi] = address;

}

void gli_gSPDlistCount(u32 count, u32 v)
{
	u32 address = RSP_SegmentToPhysical( v );
	if (address == 0 || (address + 8) > RDRAMSize) {
		return;
	}

	if (RSP.PCi >= 9) {
		return;
	}


	++RSP.PCi;  /* go to the next PC in the stack */
	RSP.PC[RSP.PCi] = address;  /* jump to the address */
	RSP.nextCmd = _SHIFTR( *(u32*)&RDRAM[address], 24, 8 );
	RSP.count = count + 1;
}

void gli_gSPSetDMAOffsets( u32 mtxoffset, u32 vtxoffset )
{
	gSP.DMAOffsets.mtx = mtxoffset;
	gSP.DMAOffsets.vtx = vtxoffset;

}

void gli_gSPSetDMATexOffset(u32 _addr)
{
	gSP.DMAOffsets.tex_offset = RSP_SegmentToPhysical(_addr);
	gSP.DMAOffsets.tex_shift = 0;
	gSP.DMAOffsets.tex_count = 0;
}

void gli_gSPSetVertexColorBase( u32 base )
{
	gSP.vertexColorBase = RSP_SegmentToPhysical( base );

}

void gli_gSPDMATriangles( u32 tris, u32 n ){
	SPVertex * verts;
	SPVertex * first;
	DKRTriangle *triangles;
	SPVertex * pVtx;
	u32 i;
	const u32 address = RSP_SegmentToPhysical( tris );

	if (address + sizeof( DKRTriangle ) * n > RDRAMSize) {
		return;
	}

	verts = gli_drawer_vertices();
	first = gli_drawer_dma_vertices(n * 3);

	triangles = (DKRTriangle*)&RDRAM[address];
	pVtx = first;
	for (i = 0; i < n; ++i) {
		s32 v0;
		s32 v1;
		s32 v2;
		int mode = 0;
		if (!(triangles->flag & 0x40)) {
			if (gSP.viewport.vscale[0] > 0)
				mode |= G_CULL_BACK;
			else
				mode |= G_CULL_FRONT;
		}
		if ((gSP.geometryMode&G_CULL_BOTH) != mode) {
			gli_drawer_draw_dma_triangles((u32)(pVtx - first));
			pVtx = first;
			gSP.geometryMode &= ~G_CULL_BOTH;
			gSP.geometryMode |= mode;
			gSP.changed |= CHANGED_GEOMETRYMODE;
		}

		v0 = triangles->v0;
		v1 = triangles->v1;
		v2 = triangles->v2;
		if (GLI_IS_CLIPPED(verts, v0, v1, v2)) {
			++triangles;
			continue;
		}
		*pVtx = verts[v0];
		pVtx->s = _FIXED2FLOAT(triangles->s0, 5);
		pVtx->t = _FIXED2FLOAT(triangles->t0, 5);
		++pVtx;
		*pVtx = verts[v1];
		pVtx->s = _FIXED2FLOAT(triangles->s1, 5);
		pVtx->t = _FIXED2FLOAT(triangles->t1, 5);
		++pVtx;
		*pVtx = verts[v2];
		pVtx->s = _FIXED2FLOAT(triangles->s2, 5);
		pVtx->t = _FIXED2FLOAT(triangles->t2, 5);
		++pVtx;
		++triangles;
	}
	gli_drawer_draw_dma_triangles((u32)(pVtx - first));
}

void gli_gSP1Quadrangle( s32 v0, s32 v1, s32 v2, s32 v3 )
{
	gli_gSPTriangle( v0, v1, v2);
	gli_gSPTriangle( v0, v2, v3);
	gli_gSPFlushTriangles();

}

bool gSPCullVertices( u32 v0, u32 vn )
{
	u32 clip;
	const SPVertex * verts;
	u32 i;
	if (vn < v0) {
		/* Aidyn Chronicles - The First Mage seems to pass parameters in reverse order. */
		const u32 v = v0;
		v0 = vn;
		vn = v;
	}
	clip = 0;
	verts = gli_drawer_vertices();
	for (i = v0; i <= vn; ++i) {
		clip |= (~verts[i].clip) & CLIP_ALL;
		if (clip == CLIP_ALL)
			return false;
	}
	return true;
}

void gli_gSPCullDisplayList( u32 v0, u32 vn )
{
	if (gSPCullVertices( v0, vn )) {
		if (RSP.PCi > 0)
			RSP.PCi--;
		else {
			RSP.halt = true;
		}
	} else {
	}
}

void gli_gSPPopMatrixN(u32 param, u32 num)
{
	if (gSP.matrix.modelViewi > num - 1) {
		gSP.matrix.modelViewi -= num;
		gSP.changed |= CHANGED_MATRIX | CHANGED_LIGHT | CHANGED_LOOKAT;
	} else {
	}
}

void gli_gSPPopMatrix( u32 param )
{
	switch (param) {
	case 0: /* modelview */
		if (gSP.matrix.modelViewi > 0) {
			gSP.matrix.modelViewi--;

			gSP.changed |= CHANGED_MATRIX | CHANGED_LIGHT | CHANGED_LOOKAT;
		}
	break;
	case 1: /* projection, can't */
	break;
	default:
		break;
	}
}

void gli_gSPSegment( s32 seg, s32 base )
{
	if ((u32)seg < 16)
		gSP.segment[seg] = base;

}

void gli_gSPRelSegment( s32 seg, s32 base )
{
	/* F3DEX3 G_RELSEGMENT: like G_MW_SEGMENT (gli_gSPSegment) but the address */
	/* is encoded as 0x0?123456 where the upper nibble selects an existing */
	/* segment to add to the 24-bit offset. */
	const s32 offset = base & 0x00FFFFFF;
	const s32 rel    = (base >> 24) & 0xf;
	gSP.segment[seg] = offset + gSP.segment[rel];

}

void gli_gSPClipRatio(u32 ratio)
{
	gSP.clipRatio = abs((s16)(ratio & 0xFFFF));
}

void gli_gSPInsertMatrix( u32 where, u32 num )
{
	f32 * pMtx;
	u16 addr;
	const u16 * pData;
	u32 index;
	u32 i;

	if ((where & 0x3) != 0)
		return;

	pMtx = NULL;
	addr = (where + 0x80) & 0xFFFF;
	if (addr < 0x40) {
		pMtx = (f32*)(gSP.matrix.modelView[gSP.matrix.modelViewi]);
	} else if (addr < 0x80) {
		pMtx = (f32*)(gSP.matrix.projection);
		addr -= 0x40;
	} else if (addr < 0xC0) {
		pMtx = (f32*)(gSP.matrix.combined);
		addr -= 0x80;
	} else
		return;

	pData = (u16*)(&num);
	index = (addr < 0x20) ? (addr >> 1) : ((addr - 0x20) >> 1);
	for (i = 0; i < 2; i++) {
		if (addr < 0x20) {
			/* integer elements of the matrix to be changed */
			const s16 integer = (s16)(pData[i ^ 1]);
			const u16 fract = (u16)((s32)(pMtx[index + i] * 65536.0f) & 0xFFFF);
			pMtx[index + i] = GetFloatMatrixElement(integer, fract);
		} else {
			/* fractional elements of the matrix to be changed */
			const s16 integer = (s16)((s32)(pMtx[index + i] * 65536.0f) >> 16);
			const u16 fract = pData[i ^ 1];
			pMtx[index + i] = GetFloatMatrixElement(integer, fract);
		}
	}
}

void gli_gSPModifyVertex( u32 _vtx, u32 _where, u32 _val )
{
	SPVertex * vtx0 = &gli_drawer_vertices()[_vtx];
	switch (_where) {
		case G_MWO_POINT_RGBA:
			vtx0->r = _FIXED2FLOATCOLOR(_SHIFTR( _val, 24, 8 ),8);
			vtx0->g = _FIXED2FLOATCOLOR(_SHIFTR( _val, 16, 8 ),8);
			vtx0->b = _FIXED2FLOATCOLOR(_SHIFTR( _val, 8, 8 ),8);
			vtx0->a = _FIXED2FLOATCOLOR(_SHIFTR( _val, 0, 8 ),8);
			vtx0->modify |= MODIFY_RGBA;
			break;
		case G_MWO_POINT_ST:
			vtx0->s = _FIXED2FLOAT( (s16)_SHIFTR( _val, 16, 16 ), 5 ) / gSP.texture.scales;
			vtx0->t = _FIXED2FLOAT((s16)_SHIFTR(_val, 0, 16), 5) / gSP.texture.scalet;
			/* vtx0->modify |= MODIFY_ST; // still neeed to divide by 2 in vertex shader if TexturePersp disabled */
			break;
		case G_MWO_POINT_XYSCREEN:
			vtx0->x = _FIXED2FLOAT((s16)_SHIFTR(_val, 16, 16), 2);
			vtx0->y = _FIXED2FLOAT((s16)_SHIFTR(_val, 0, 16), 2);
			if ((config.generalEmulation.hacks & hack_ModifyVertexXyInShader) == 0) {
				vtx0->x = (vtx0->x - gSP.viewport.vtrans[0]) / gSP.viewport.vscale[0];
				if (gSP.viewport.vscale[0] < 0)
					vtx0->x = -vtx0->x;
				vtx0->x *= vtx0->w;

				if (gli_window_is_adjust_screen()) {
					const f32 adjustScale = gli_window_adjust_scale();
					vtx0->x *= adjustScale;
					if (gSP.matrix.projection[3][2] == -1.f)
						vtx0->w *= adjustScale;
				}

				vtx0->y = -(vtx0->y - gSP.viewport.vtrans[1]) / gSP.viewport.vscale[1];
				if (gSP.viewport.vscale[1] < 0)
					vtx0->y = -vtx0->y;
				vtx0->y *= vtx0->w;
			} else {
				vtx0->modify |= MODIFY_XY;
				if (vtx0->w == 0.0f) {
					vtx0->w = 1.0f;
					vtx0->clip &= ~(CLIP_W);
				}
			}
			vtx0->clip &= ~(CLIP_POSX | CLIP_NEGX | CLIP_POSY | CLIP_NEGY);
		break;
		case G_MWO_POINT_ZSCREEN:
		{
			/* All 32 bits of _val are the z value (16.11) */
			f32 scrZ = (f32)(_val) / 65535.0f / 2048.0f;
			vtx0->z = scrZ;
			vtx0->clip &= ~CLIP_W;
			vtx0->modify |= MODIFY_Z;
		}
		break;
	}
}

void gli_gSPNumLights( s32 n )
{
	if (n >= 0 && n < 12) {
		gSP.numLights = n;
		gSP.changed |= CHANGED_LIGHT;
	} else {
	}

}

void gli_gSPLightColor( u32 lightNum, u32 packedColor )
{
	--lightNum;

	if (lightNum < 8)
	{
		gSP.lights.rgb[lightNum][R] = _FIXED2FLOATCOLOR(_SHIFTR( packedColor, 24, 8 ),8);
		gSP.lights.rgb[lightNum][G] = _FIXED2FLOATCOLOR(_SHIFTR( packedColor, 16, 8 ),8);
		gSP.lights.rgb[lightNum][B] = _FIXED2FLOATCOLOR(_SHIFTR( packedColor, 8, 8 ),8);
		gSP.changed |= CHANGED_HW_LIGHT;
	}
}

void gli_gSPFogFactor( s16 fm, s16 fo )
{
	gSP.fog.multiplier = fm;
	gSP.fog.offset = fo;
	gSP.fog.multiplierf = _FIXED2FLOAT(fm, 8);
	gSP.fog.offsetf = _FIXED2FLOAT(fo, 8);

	gSP.changed |= CHANGED_FOGPOSITION;
}

void gli_gSPPerspNormalize( u16 scale )
{
}

void gli_gSPTexture( f32 sc, f32 tc, u32 level, u32 tile, u32 on )
{
	gSP.texture.on = on;
	if (on == 0) {
		if (gli_legacy_sm64_tools())
			gli_gDPSetCombine(0xffffff, 0xFFFE793C);

		return;
	}

	gSP.texture.scales = sc;
	gSP.texture.scalet = tc;

	if (gSP.texture.scales == 0.0f) gSP.texture.scales = 1.0f;
	if (gSP.texture.scalet == 0.0f) gSP.texture.scalet = 1.0f;

	gSP.texture.level = level;

	gSP.texture.tile = tile;
	gSP.textureTile[0] = &gDP.tiles[tile];
	gSP.textureTile[1] = &gDP.tiles[(tile + 1) & 7];

	gSP.changed |= CHANGED_TEXTURE;

}

void gli_gSPEndDisplayList(void)
{
	if (RSP.PCi > 0)
		--RSP.PCi;
	else {
		RSP.halt = true;
	}

}

void gli_gSPGeometryMode( u32 clear, u32 set )
{
	gSP.geometryMode = (gSP.geometryMode & ~clear) | set;

	gSP.changed |= CHANGED_GEOMETRYMODE;

}

void gli_gSPSetGeometryMode( u32 mode )
{
	gSP.geometryMode |= mode;

	gSP.changed |= CHANGED_GEOMETRYMODE;

}

void gli_gSPClearGeometryMode( u32 mode )
{
	gSP.geometryMode &= ~mode;

	gSP.changed |= CHANGED_GEOMETRYMODE;

}

void gli_gSPSetOtherMode_H(u32 _length, u32 _shift, u32 _data)
{
	const u32 mask = (((u64)1 << _length) - 1) << _shift;
	gDP.otherMode.h = (gDP.otherMode.h&(~mask)) | _data;

	if (mask & 0x00300000)  /* cycle type */
		gDP.changed |= CHANGED_CYCLETYPE;

}

void gli_gSPSetOtherMode_L(u32 _length, u32 _shift, u32 _data)
{
	u32 mask;
	if (gli_legacy_sm64_tools())
	{
		/* Typo fixrefix */
		/* !!! This is very cheesy fix */
		const u32 maskH = (((u64)1 << 2) - 1) << 0x14;
		if (!(gDP.otherMode.h & maskH) && _data == 0xC8113078)
		{
			_data = 0x00443078;
		}
	}

	mask = (((u64)1 << _length) - 1) << _shift;
	gDP.otherMode.l = (gDP.otherMode.l&(~mask)) | _data;

	if (mask & 0x00000003)  /* alpha compare */
		gDP.changed |= CHANGED_ALPHACOMPARE;

	if (mask & 0xFFFFFFF8)  /* rendermode / blender bits */
		gDP.changed |= CHANGED_RENDERMODE;

}

void gli_gSPLine3D(u32 v0, u32 v1, s32 wd, u32 flag)
{
	gli_drawer_draw_line(v0, v1, 1.5f + wd * 0.5f, flag);

}

void gli_gSPSetStatus(u32 sid, u32 val)
{
	assert(sid <= 12);
	gSP.status[sid>>2] = val;

}

typedef struct uSprite {
	u32 imagePtr;
	u32 tlutPtr;
	s16	imageW;
	s16	stride;
	s8	imageSiz;
	s8	imageFmt;
	s16	imageH;
	s16	imageY;
	s16	imageX;
	s8	dummy[4];
} uSprite;    /* 24 bytes */

static
void _loadSpriteImage(const uSprite *_pSprite)
{
	gSP.bgImage.address = RSP_SegmentToPhysical( _pSprite->imagePtr );

	gSP.bgImage.width = _pSprite->stride;
	gSP.bgImage.height = _pSprite->imageY + _pSprite->imageH;
	gSP.bgImage.format = _pSprite->imageFmt;
	gSP.bgImage.size = _pSprite->imageSiz;
	gSP.bgImage.palette = 0;
	gDP.tiles[0].textureMode = TEXTUREMODE_BGIMAGE;
	gSP.bgImage.imageX = _pSprite->imageX;
	gSP.bgImage.imageY = _pSprite->imageY;
	gSP.bgImage.scaleW = gSP.bgImage.scaleH = 1.0f;

	if (config.frameBufferEmulation.enable != 0)
	{
		struct FrameBuffer *pBuffer = gli_fb_find(gSP.bgImage.address);
		if (pBuffer != NULL) {
			if (gli_fb_is_cpu(pBuffer)) {
				gli_fb_remove(gli_fb_start_address(pBuffer));
				return;
			}
			gDP.tiles[0].frameBufferAddress = gli_fb_start_address(pBuffer);
			gDP.tiles[0].textureMode = TEXTUREMODE_FRAMEBUFFER_BG;
			gDP.tiles[0].loadType = LOADTYPE_TILE;
			gDP.changed |= CHANGED_TMEM;
		}
	}
}

void gli_gSPSprite2DBase(u32 _base)
{
	u32 address;
	uSprite *pSprite;
	f32 z;
	f32 w;
	f32 scaleX;
	f32 scaleY;
	u32 flipX;
	u32 flipY;
	assert(RSP.nextCmd == 0xBE);
	address = RSP_SegmentToPhysical( _base );
	pSprite = (uSprite*)&RDRAM[address];

	if (pSprite->tlutPtr != 0) {
		gli_gDPSetTextureImage( G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, pSprite->tlutPtr );
		gli_gDPSetTile( G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 256, 7, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, 0, 0, 0, 0 );
		gli_gDPLoadTLUT( 7, 0, 0, 1020, 0 );

		if (pSprite->imageFmt != G_IM_FMT_RGBA)
			gDP.otherMode.textureLUT = G_TT_RGBA16;
		else
			gDP.otherMode.textureLUT = G_TT_NONE;
	} else
		gDP.otherMode.textureLUT = G_TT_NONE;

	_loadSpriteImage(pSprite);
	gli_gSPTexture( 1.0f, 1.0f, 0, 0, TRUE );
	gDP.otherMode.texturePersp = 1;

	z = (gDP.otherMode.depthSource == G_ZS_PRIM) ? gDP.primDepth.z : gSP.viewport.nearz;
	w = 1.0f;

	scaleX = 1.0f;
	scaleY = 1.0f;
	flipX = 0;
	flipY = 0;
	do {
		f32 frameX;
		f32 frameY;
		f32 frameW;
		f32 frameH;
		f32 ulx;
		f32 uly;
		f32 lrx;
		f32 lry;
		f32 uls;
		f32 ult;
		f32 lrs;
		f32 lrt;
		SPVertex * pVtx;
		SPVertex * vtx0;
		SPVertex * vtx1;
		SPVertex * vtx2;
		SPVertex * vtx3;
		u32 w0 = *(u32*)&RDRAM[RSP.PC[RSP.PCi]];
		u32 w1 = *(u32*)&RDRAM[RSP.PC[RSP.PCi] + 4];
		RSP.cmd = _SHIFTR( w0, 24, 8 );

		RSP.PC[RSP.PCi] += 8;
		RSP.nextCmd = _SHIFTR( *(u32*)&RDRAM[RSP.PC[RSP.PCi]], 24, 8 );

		if ( RSP.cmd == 0xBE ) { /* gSPSprite2DScaleFlip */
			scaleX  = _FIXED2FLOAT( _SHIFTR(w1, 16, 16), 10 );
			scaleY  = _FIXED2FLOAT( _SHIFTR(w1,  0, 16), 10 );
			flipX = _SHIFTR(w0, 8, 8);
			flipY = _SHIFTR(w0, 0, 8);
			continue;
		}
		/* gSPSprite2DDraw */
		frameX = _FIXED2FLOAT(((s16)_SHIFTR(w1, 16, 16)), 2);
		frameY = _FIXED2FLOAT(((s16)_SHIFTR(w1,  0, 16)), 2);
		frameW = pSprite->imageW / scaleX;
		frameH = pSprite->imageH / scaleY;

		if (flipX != 0) {
			ulx = frameX + frameW;
			lrx = frameX;
		} else {
			ulx = frameX;
			lrx = frameX + frameW;
		}
		if (flipY != 0) {
			uly = frameY + frameH;
			lry = frameY;
		} else {
			uly = frameY;
			lry = frameY + frameH;
		}

		uls = pSprite->imageX;
		ult = pSprite->imageY;
		lrs = uls + pSprite->imageW - 1;
		lrt = ult + pSprite->imageH - 1;

		/* Hack for WCW Nitro. */
		if ((config.generalEmulation.hacks & hack_WCWNitro) != 0) {
			const u32 scale = (u32)(scaleY);
			gSP.bgImage.height /= scale;
			gSP.bgImage.imageY /= scale;
			ult /= scaleY;
			lrt /= scaleY;
			gSP.bgImage.width *= scale;
		}

		pVtx = gli_drawer_dma_vertices(4);

		vtx0 = &pVtx[0];
		vtx0->x = ulx;
		vtx0->y = uly;
		vtx0->z = z;
		vtx0->w = w;
		vtx0->s = uls;
		vtx0->t = ult;
		vtx1 = &pVtx[1];
		vtx1->x = lrx;
		vtx1->y = uly;
		vtx1->z = z;
		vtx1->w = w;
		vtx1->s = lrs;
		vtx1->t = ult;
		vtx2 = &pVtx[2];
		vtx2->x = ulx;
		vtx2->y = lry;
		vtx2->z = z;
		vtx2->w = w;
		vtx2->s = uls;
		vtx2->t = lrt;
		vtx3 = &pVtx[3];
		vtx3->x = lrx;
		vtx3->y = lry;
		vtx3->z = z;
		vtx3->w = w;
		vtx3->s = lrs;
		vtx3->t = lrt;

		if (pSprite->stride > 0)
			gli_drawer_draw_screen_strip(4);
	} while (RSP.nextCmd == 0xBD || RSP.nextCmd == 0xBE);
}

#ifndef __NEON_OPT
void(*gSPInverseTransformVector)(float vec[3], float mtx[4][4]) = gSPInverseTransformVector_default;
void(*gSPTransformVector)(float vtx[4], float mtx[4][4]) = gSPTransformVector_default;
#else
void(*gSPInverseTransformVector)(float vec[3], float mtx[4][4]) = gSPInverseTransformVector_NEON;
void(*gSPTransformVector)(float vtx[4], float mtx[4][4]) = gSPTransformVector_NEON;
#endif /* __NEON_OPT */

void gli_gSPSetupFunctions(void)
{
	g_ConkerUcode = gli_GBI->type == F3DEX2CBFD;
}
