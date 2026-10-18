#include <math.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <algorithm>
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
#include "FrameBuffer.h"
#include "Config.h"
#include "Log.h"
#include "DisplayWindow.h"
#include "GraphicsDrawer.h"

// Immersive mode
extern "C" int libretro_vr_immersive_pose(float rotation[2][9], float tangents[2][4]);
extern "C" void libretro_vr_immersive_present(int pose);

using namespace std;
using namespace graphics;

#define INDEXMAP_SIZE 80U

#ifdef __VEC4_OPT
#define VEC_OPT 4U
#else
#define VEC_OPT 1U
#endif

static bool g_ConkerUcode;

void gSPFlushTriangles()
{
	if ((gSP.geometryMode & G_SHADING_SMOOTH) == 0) {
		dwnd().getDrawer().drawTriangles();
		return;
	}

	if (
		(RSP.nextCmd != G_TRI1) &&
		(RSP.nextCmd != G_TRI2) &&
		(RSP.nextCmd != G_TRIX) &&
		(RSP.nextCmd != G_QUAD)
		) {
		dwnd().getDrawer().drawTriangles();
	}
}

// Stereoscopic 3D. The matrix supplies the eye-dependent separation * w term. The
// convergence term is applied after the transform, where it can be limited safely in screen space.
// Vertices are transformed as a row vector, so x reads column 0 and w reads column 3.
static u32 l_stereoEye = Config::stereoOff;

// Which eye's pose and frustum uses in immersive mode
static inline int _immersiveEye() { return l_stereoEye == Config::stereoRightEye ? 1 : 0; }
static bool l_stereoPerspectiveSeen = false;

static
bool _isOrthographic(const f32 matrix[4][4])
{
	return fabsf(matrix[0][3]) < 1e-6f && fabsf(matrix[1][3]) < 1e-6f && fabsf(matrix[2][3]) < 1e-6f;
}

// Automatic convergence.
static const int kStereoBins = 96;
static u32 l_logWHistogram[kStereoBins] = {};
static u32 l_logWCount = 0;
static f32 l_autoLogConvergence = 0.0f;
static bool l_haveAutoConvergence = false;
static f32 l_autoConvergence = 0.0f;

static
void _stereoMeasure(f32 w)
{
	int bin = static_cast<int>((log2f(w) + 8.0f) * 2.0f);
	bin = std::max(0, std::min(kStereoBins - 1, bin));
	++l_logWHistogram[bin];
	++l_logWCount;
}

static
f32 _stereoPercentile(f32 fraction)
{
	const u32 target = static_cast<u32>(l_logWCount * fraction);
	u32 sum = 0;
	for (int bin = 0; bin < kStereoBins; ++bin) {
		sum += l_logWHistogram[bin];
		if (sum > target)
			return (bin + 0.5f) * 0.5f - 8.0f;
	}
	return (kStereoBins - 0.5f) * 0.5f - 8.0f;
}

static void _immersiveBeginFrame();
static void _immersiveCountVotes();

void gSPStereoBeginTask()
{
	_immersiveBeginFrame();
}

// Once per shown picture: what its display lists measured
void gSPStereoEndFrame()
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

// Immersive mode.
static bool l_immersiveActive = false;      // a pose arrived for this frame
static bool l_immersiveVoted = false;
static bool l_immersiveFlat = false;
static bool _immersiveOn() { return l_immersiveActive && !l_immersiveFlat; }
static bool l_immersiveNeedsPose = true;    // a frame was shown since the last pose
static int l_immersivePose = 0;             // id of the pose the current frame is drawn with
static u32 l_immersivePerspective = 0;
static void _immersiveSetFlat(bool _flat, const char * _why);

// Menus, logos and title screens should not use head tracking.
static
bool _immersiveMenuPicture(u32 _perspective)
{
	return _perspective == 0 || VI.width > 384;
}

static f32 l_immersiveQ[2][3][3] = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
	{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};  // camera direction -> eye direction
static f32 l_immersiveTan[2][2] = {{1.0f, 1.0f}, {1.0f, 1.0f}};
static f32 l_immersiveCentre[2][2] = {};
#define STEREO_FLIP_Y ((GBI.getMicrocodeType() == F5Rogue || GBI.getMicrocodeType() == F5Indi_Naboo) ? -1.0f : 1.0f)
static bool l_combiningMatrices = false;
static f32 l_immersiveDistance = 0.0f;
static f32 l_immersiveRefNearest = 0.0f;   // nearest depth of the scene the game plays at
static bool l_immersiveDistanceLocked = false;
static u32 l_immersiveDistanceFrames = 0;
static u32 l_immersiveNearestOffFrames = 0;
static f32 l_gameSx = 1.0f;
static f32 l_gameSy = 1.33f;                // perspective matrix
static const int kImmersiveBuffers = 8;
static u32 l_immersiveBufferAddress[kImmersiveBuffers] = {};
static int l_immersiveBufferPose[kImmersiveBuffers] = {};
static int l_immersiveBufferNext = 0;
static u32 l_immersivePresentsSincePose = 0;

// Samples the head pose once per shown frame, so every display list of a frame and both eyes use
// the same camera.
static
void _immersiveBeginFrame()
{
	if (config.stereo.immersive == 0) {
		l_immersiveActive = false;
		return;
	}
	if (!l_immersiveNeedsPose && l_immersiveActive)
		return;
	if (l_immersiveActive && !l_immersiveVoted)
		_immersiveSetFlat(_immersiveMenuPicture(l_immersivePerspective), "a menu or a logo,");
	l_immersivePerspective = 0;
	float rotation[2][9];
	float tangents[2][4];
	const int pose = libretro_vr_immersive_pose(rotation, tangents);
	if (pose <= 0) {
		l_immersiveActive = false;
		return;
	}
	for (int eye = 0; eye < 2; ++eye) {
		// tangents: left, right, up, down, all positive
		const f32 * t = tangents[eye];
		if (t[0] + t[1] <= 0.0f || t[2] + t[3] <= 0.0f) {
			l_immersiveActive = false;
			return;
		}
		for (int i = 0; i < 9; ++i)
			l_immersiveQ[eye][i / 3][i % 3] = rotation[eye][i];
		l_immersiveTan[eye][0] = (t[0] + t[1]) * 0.5f;
		l_immersiveTan[eye][1] = (t[2] + t[3]) * 0.5f;
		l_immersiveCentre[eye][0] = (t[1] - t[0]) * 0.5f;
		l_immersiveCentre[eye][1] = (t[2] - t[3]) * 0.5f;
	}
	l_immersivePose = pose;
	// How far back the camera goes.
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

bool gSPImmersiveActive()
{
	return _immersiveOn() && (l_stereoEye == Config::stereoLeftEye || l_stereoEye == Config::stereoRightEye);
}

bool gSPImmersiveIsFlat() { return l_immersiveFlat; }

// Remembers which pose drew a color image, for when that image is shown
void gSPImmersiveFrameDrawn(u32 _address)
{
	if (!l_immersiveActive)
		return;
	for (int i = 0; i < kImmersiveBuffers; ++i) {
		if (l_immersiveBufferAddress[i] == _address) {
			l_immersiveBufferPose[i] = l_immersivePose;
			return;
		}
	}
	l_immersiveBufferAddress[l_immersiveBufferNext] = _address;
	l_immersiveBufferPose[l_immersiveBufferNext] = l_immersivePose;
	l_immersiveBufferNext = (l_immersiveBufferNext + 1) % kImmersiveBuffers;
}

// Called just before the frame showing the image at _address is swapped out to the headset
void gSPImmersivePresent(u32 _address)
{
	if (!l_immersiveActive) {
		l_immersiveNeedsPose = true;
		libretro_vr_immersive_present(0);
		return;
	}
	int pose = l_immersivePose;
	for (int i = 0; i < kImmersiveBuffers; ++i) {
		if (l_immersiveBufferAddress[i] == _address && l_immersiveBufferPose[i] > 0) {
			pose = l_immersiveBufferPose[i];
			break;
		}
	}
	// A new pose once the picture drawn with the current one is on screen.
	if (pose == l_immersivePose || ++l_immersivePresentsSincePose >= 4) {
		l_immersiveNeedsPose = true;
		l_immersivePresentsSincePose = 0;
	}
	libretro_vr_immersive_present(l_immersiveFlat ? -pose : pose);
}

// The game's projection scales.
struct ImmersiveVote { f32 sx, sy; u32 count; };
static const int kImmersiveVotes = 16;
static ImmersiveVote l_immersiveVotes[kImmersiveVotes];
static int l_immersiveVoteCount = 0;

static
void _immersiveVote(f32 sx, f32 sy)
{
	for (int i = 0; i < l_immersiveVoteCount; ++i) {
		ImmersiveVote & vote = l_immersiveVotes[i];
		if (fabsf(vote.sx - sx) < 0.01f * sx && fabsf(vote.sy - sy) < 0.01f * sy) {
			++vote.count;
			return;
		}
	}
	if (l_immersiveVoteCount < kImmersiveVotes)
		l_immersiveVotes[l_immersiveVoteCount++] = {sx, sy, 1};
	if (!l_immersiveVoted) {
		// Nothing agreed on yet: better than the defaults
		l_gameSx = sx;
		l_gameSy = sy;
	}
}

// A cut scene camera with a much wider view would otherwise be shown at its own angles, far away
// and past the edge of what the game draws.
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
			LOG(LOG_MINIMAL, "Immersive: on the screen, %s %.0f degrees to the side", _why,
				l_immersiveRefSx > 0.0f ? atanf(1.0f / l_immersiveRefSx) * 57.3f : 0.0f);
		}
	} else {
		l_immersiveFlatFrames = 0;
		if (l_immersiveFlat && ++l_immersiveSceneFrames > kToView) {
			l_immersiveFlat = false;
			l_immersiveRefNearest = l_immersiveDistance = 0.0f;
			l_immersiveDistanceLocked = false;
			l_immersiveDistanceFrames = 0;
			LOG(LOG_MINIMAL, "Immersive: back in the view, the camera sees %.0f degrees to the side",
				l_immersiveRefSx > 0.0f ? atanf(1.0f / l_immersiveRefSx) * 57.3f : 0.0f);
		}
	}
}

static
void _immersiveCountVotes()
{
	if (VI.width > 384) {
		// Hi-res: a menu or a logo, see _immersiveMenuPicture()
		l_immersiveVoteCount = 0;
		_immersiveSetFlat(true, "a menu or a logo,");
		return;
	}
	if (l_immersiveVoteCount == 0) {
		if (l_immersiveVoted)
			_immersiveSetFlat(true, "nothing is drawn in perspective,");
		return;
	}
	int best = 0, near = -1;
	for (int i = 0; i < l_immersiveVoteCount; ++i) {
		if (l_immersiveVotes[i].count > l_immersiveVotes[best].count)
			best = i;
		// The camera of the frame is whichever scale still matches the one the game plays at
		if (l_immersiveRefSx > 0.0f &&
			l_immersiveVotes[i].sx > l_immersiveRefSx / kImmersiveScaleSlack &&
			l_immersiveVotes[i].sx < l_immersiveRefSx * kImmersiveScaleSlack &&
			(near < 0 || l_immersiveVotes[i].count > l_immersiveVotes[near].count))
			near = i;
	}
	const f32 dominantSx = l_immersiveVotes[best].sx;
	const f32 dominantSy = l_immersiveVotes[best].sy;
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

	const f32 kNarrowTangent = 0.577f;  // 30 degrees to the side
	_immersiveSetFlat(1.0f / l_immersiveRefSx < kNarrowTangent, "the camera sees only");
	l_gameSx = l_immersiveRefSx;
	l_gameSy = l_immersiveRefSy;
	l_immersiveVoteCount = 0;
}

static
void _immersiveMeasureProjection(const f32 m[4][4])
{
	const f32 lw = sqrtf(m[0][3] * m[0][3] + m[1][3] * m[1][3] + m[2][3] * m[2][3]);
	if (lw < 1e-9f)
		return;
	++l_immersivePerspective;
	const f32 sx = sqrtf(m[0][0] * m[0][0] + m[1][0] * m[1][0] + m[2][0] * m[2][0]) / lw;
	const f32 sy = sqrtf(m[0][1] * m[0][1] + m[1][1] * m[1][1] + m[2][1] * m[2][1]) / lw;
	if (sx > 0.05f && sx < 50.0f && sy > 0.05f && sy < 50.0f) {
		if (l_combiningMatrices) {
			l_gameSx = sx;
			l_gameSy = sy;
		} else {
			_immersiveVote(sx, sy);
		}
	}
}

// Turns a direction in the game camera's view space, looking down -z, into the head's clip space
static inline
void _immersiveProject(f32 dx, f32 dy, f32 dz, f32 & x, f32 & y, f32 & w)
{
	const int eye = _immersiveEye();
	const f32 (&q)[3][3] = l_immersiveQ[eye];
	const f32 ez = q[2][0] * dx + q[2][1] * dy + q[2][2] * dz;
	// x/w - centre spans -1..1 over the eye's frustum, with w = -ez
	x = (q[0][0] * dx + q[0][1] * dy + q[0][2] * dz + l_immersiveCentre[eye][0] * ez) / l_immersiveTan[eye][0];
	y = (q[1][0] * dx + q[1][1] * dy + q[1][2] * dz + l_immersiveCentre[eye][1] * ez) / l_immersiveTan[eye][1];
	w = -ez;
}

// Right-multiplies a perspective combined matrix with the clip-space turn:
// view direction d = (x/sx, y/sy, -w), turned d' = Q*d, then x' = d'x/tan, y' = d'y/tan, w' = -d'z,
// and z' = z + A*(w - w') keeps the game's own depth mapping along the new line of sight.
static
void _immersiveApply(f32 m[4][4])
{
	const f32 c3 = m[0][3] * m[0][3] + m[1][3] * m[1][3] + m[2][3] * m[2][3];
	const f32 A = c3 > 0.0f ? -(m[0][2] * m[0][3] + m[1][2] * m[1][3] + m[2][2] * m[2][3]) / c3 : 0.0f;
	// z = -A*w + B is the game's depth mapping, with the near and far plane it was built for
	const f32 B = m[3][2] + A * m[3][3];
	if (l_immersiveDistance > 0.0f && fabsf(A) > 1e-6f && fabsf(B) > 1e-6f) {
		const f32 nearPlane = B / (A - 1.0f) + l_immersiveDistance;
		const f32 farPlane = B / (A + 1.0f) + l_immersiveDistance;
		if (nearPlane > 0.0f && farPlane > nearPlane) {
			m[3][3] += l_immersiveDistance;
			const f32 movedB = -2.0f * nearPlane * farPlane / (farPlane - nearPlane);
			const f32 movedA = movedB / nearPlane + 1.0f;
			for (int i = 0; i < 3; ++i)
				m[i][2] = -movedA * m[i][3];
			m[3][2] = -movedA * m[3][3] + movedB;
		}
	}
	f32 t[4][4] = {};
	// Rows are the inputs x, y, z, w
	const f32 world = config.stereo.immersiveWorldScale;
	_immersiveProject(world / l_gameSx, 0.0f, 0.0f, t[0][0], t[0][1], t[0][3]);
	_immersiveProject(0.0f, STEREO_FLIP_Y * world / l_gameSy, 0.0f, t[1][0], t[1][1], t[1][3]);
	_immersiveProject(0.0f, 0.0f, -1.0f, t[3][0], t[3][1], t[3][3]);
	t[2][2] = 1.0f;
	t[0][2] = -A * t[0][3];
	t[1][2] = -A * t[1][3];
	t[3][2] = A - A * t[3][3];
	for (int i = 0; i < 4; ++i)
		t[i][1] *= STEREO_FLIP_Y;
	f32 result[4][4];
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j)
			result[i][j] = m[i][0] * t[0][j] + m[i][1] * t[1][j] + m[i][2] * t[2][j] + m[i][3] * t[3][j];
	memcpy(m, result, sizeof(result));
}

// Flat geometry and screen rectangles have no depth of their own.
static
void _immersivePlaceFlat(f32 nx, f32 ny, f32 nz, f32 & x, f32 & y, f32 & z, f32 & w, f32 _scale)
{
	_immersiveProject(nx * _scale / l_gameSx, ny * _scale / l_gameSy, -1.0f, x, y, w);
	z = nz * w;
}

static
void _immersivePlaceSky(f32 nx, f32 ny, f32 nz, f32 & x, f32 & y, f32 & z, f32 & w)
{
	const f32 kEdge = 0.98f;
	const f32 kFar = 40.0f;  // about 85 degrees at a typical game field of view
	if (nx >= kEdge) nx = kFar; else if (nx <= -kEdge) nx = -kFar;
	if (ny >= kEdge) ny = kFar; else if (ny <= -kEdge) ny = -kFar;
	// A background belongs to the scene, so it follows the world, not the overlay scale
	_immersivePlaceFlat(nx, ny, nz, x, y, z, w, config.stereo.immersiveWorldScale);
}

// Stereo separation is in normalized screen units.
static
f32 _stereoSeparation()
{
	const f32 separation = l_stereoEye == Config::stereoLeftEye
		? -config.stereo.separation : config.stereo.separation;
	return _immersiveOn() ? separation * 0.5f / l_immersiveTan[_immersiveEye()][0] : separation;
}

// Games that install their own matrices, like Factor 5's, clear only their viewport. Immersive mode
// stretches that viewport over the whole picture, so the border keeps old frames around.
bool gSPImmersiveClearsShownBuffers()
{
	return l_immersiveActive && l_immersiveVoted;
}

// Some microcodes draw world effects, such as Star Wars Racer's engine glow, as screen rectangles
// they placed themselves. Those belong in the scene, not on the HUD.
static bool l_immersiveWorldSprite = false;

void gSPImmersiveWorldSprite(bool _world)
{
	l_immersiveWorldSprite = _world;
}

// Each content starts with nothing measured
void gSPStereoReset()
{
	int eye, i;
	l_stereoEye = Config::stereoOff;
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

bool gSPImmersiveBackground()
{
	return !l_stereoPerspectiveSeen;
}

// Screen space triangles carry no depth of their own, but the game put them where its camera saw
// them, so their direction is known. Factor 5 draws engine glows and other particles this way.
void gSPImmersiveScreenTriangles(SPVertex * _vertices, u32 _count)
{
	if (!gSPImmersiveActive() || !l_immersiveVoted)
		return;
	const f32 halfWidth = static_cast<f32>(gDP.colorImage.width) * 0.5f;
	const f32 halfHeight = (VI.height > 0 ? static_cast<f32>(VI.height) : halfWidth * 1.5f) * 0.5f;
	for (u32 i = 0; i < _count; ++i) {
		SPVertex & vtx = _vertices[i];
		f32 x, y, z, w;
		_immersivePlaceFlat(vtx.x / halfWidth - 1.0f, 1.0f - vtx.y / halfHeight,
			vtx.z, x, y, z, w, config.stereo.immersiveWorldScale);
		if (w <= 0.0f)
			continue;
		// The shader multiplies these by w again, so they stay plain pixels
		vtx.x = (1.0f + x / w) * halfWidth;
		vtx.y = (1.0f - y / w) * halfHeight;
		vtx.w = w;
	}
}

void gSPImmersiveRect(RectVertex * _vertices, u32 _count, bool _fullScreen)
{
	if (!gSPImmersiveActive())
		return;
	const f32 width = static_cast<f32>(gDP.colorImage.width);
	const f32 height = VI.height > 0 ? static_cast<f32>(VI.height) : width * 0.75f;
	if (!l_stereoPerspectiveSeen) {
		// Drawn before any 3D: a clear or a background such as a sky, spread over the sky plane at
		// infinity. The game's screen is its scissor box.
		const f32 x0 = gDP.scissor.ulx, x1 = gDP.scissor.lrx;
		const f32 y0 = gDP.scissor.uly, y1 = gDP.scissor.lry;
		if (x1 - x0 < 8.0f || y1 - y0 < 8.0f)
			return;
		const f32 separation = _stereoSeparation();
		for (u32 i = 0; i < _count; ++i) {
			RectVertex & v = _vertices[i];
			f32 x, y, z, w;
			_immersivePlaceSky((v.x - x0) / (x1 - x0) * 2.0f - 1.0f, 1.0f - (v.y - y0) / (y1 - y0) * 2.0f,
				v.z, x, y, z, w);
			x += separation * w;
			v.x = (w + x) * width * 0.5f;
			v.y = (w - y) * height * 0.5f;
			v.z = z;
			v.w = w;
		}
		return;
	}
	if (_count > 0) {
		// Games fade, flash and darken with an overlay over their whole viewport, as GoldenEye does
		// between the shots of a level's intro.
		f32 x0 = _vertices[0].x, x1 = x0, y0 = _vertices[0].y, y1 = y0;
		for (u32 i = 1; i < _count; ++i) {
			x0 = std::min(x0, _vertices[i].x); x1 = std::max(x1, _vertices[i].x);
			y0 = std::min(y0, _vertices[i].y); y1 = std::max(y1, _vertices[i].y);
		}
		const f32 sw = gDP.scissor.lrx - gDP.scissor.ulx, sh = gDP.scissor.lry - gDP.scissor.uly;
		if (sw > 8.0f && sh > 8.0f && x1 - x0 >= 0.9f * sw && y1 - y0 >= 0.9f * sh) {
			for (u32 i = 0; i < _count; ++i) {
				RectVertex & v = _vertices[i];
				f32 x, y, z, w;
				_immersivePlaceSky((v.x - gDP.scissor.ulx) / sw * 2.0f - 1.0f,
					1.0f - (v.y - gDP.scissor.uly) / sh * 2.0f, v.z, x, y, z, w);
				v.x = (w + x) * width * 0.5f;
				v.y = (w - y) * height * 0.5f;
				v.z = z;
				v.w = w;
			}
			return;
		}
	}
	// A rectangle that is tested against the depth of the scene belongs to the world, like Star Wars
	// Racer's engine glow, not to the HUD. Only for games whose own matrices are followed, so the
	// plain path stays as it was.
	const bool world = l_immersiveWorldSprite || (l_immersiveVoted &&
		(gDP.otherMode.depthSource == G_ZS_PRIM || gDP.otherMode.depthCompare != 0));
	for (u32 i = 0; i < _count; ++i) {
		RectVertex & v = _vertices[i];
		const f32 nx = v.x / (width * 0.5f) - 1.0f;
		const f32 ny = 1.0f - v.y / (height * 0.5f);
		f32 x, y, z, w;
		_immersivePlaceFlat(nx, ny, v.z, x, y, z, w,
			world ? config.stereo.immersiveWorldScale : config.stereo.immersiveHudScale);
		// Back to homogeneous N64 pixels, which the rectangle shader divides out
		v.x = (w + x) * width * 0.5f;
		v.y = (w - y) * height * 0.5f;
		v.z = z;
		v.w = w;
	}
}

void gSPSetStereoEye(u32 _eye)
{
	l_stereoPerspectiveSeen = false;
	if (l_stereoEye == _eye)
		return;
	l_stereoEye = _eye;
	gSP.changed |= CHANGED_MATRIX;
}

void gSPApplyStereo(f32 matrix[4][4])
{
	if (l_stereoEye != Config::stereoLeftEye && l_stereoEye != Config::stereoRightEye)
		return;

	if (!_isOrthographic(matrix)) {
		if (l_immersiveActive) {
			// Measured on the projection when it holds the perspective.
			_immersiveMeasureProjection(l_combiningMatrices && !_isOrthographic(gSP.matrix.projection)
				? gSP.matrix.projection : matrix);
		}
		if (_immersiveOn())
			_immersiveApply(matrix);
	}

	const f32 separation = _stereoSeparation();
	for (int i = 0; i < 4; ++i)
		matrix[i][0] += separation * matrix[i][3];
}

static
void _gSPCombineMatrices()
{
	MultMatrix(gSP.matrix.projection, gSP.matrix.modelView[gSP.matrix.modelViewi], gSP.matrix.combined);
	l_combiningMatrices = true;
	gSPApplyStereo(gSP.matrix.combined);
	l_combiningMatrices = false;
	gSP.changed &= ~CHANGED_MATRIX;
}

void gSPCombineMatrices(u32 _mode)
{
	if (_mode == 1)
		_gSPCombineMatrices();
}

void gSPTriangle(s32 v0, s32 v1, s32 v2)
{
	GraphicsDrawer & drawer = dwnd().getDrawer();
	if ((v0 < INDEXMAP_SIZE) && (v1 < INDEXMAP_SIZE) && (v2 < INDEXMAP_SIZE)) {
		if (drawer.isClipped(v0, v1, v2)) {
			return;
		}
		if (drawer.isRejected(v0, v1, v2)) {
			return;
		}
		drawer.addTriangle(v0, v1, v2);
	}
}

void gSP1Triangle( const s32 v0, const s32 v1, const s32 v2)
{

	gSPTriangle( v0, v1, v2);
	gSPFlushTriangles();
}

void gSP2Triangles(const s32 v00, const s32 v01, const s32 v02, const s32 flag0,
				   const s32 v10, const s32 v11, const s32 v12, const s32 flag1 )
{

	gSPTriangle( v00, v01, v02);
	gSPTriangle( v10, v11, v12);
	gSPFlushTriangles();
}

void gSP4Triangles(const s32 v00, const s32 v01, const s32 v02,
				   const s32 v10, const s32 v11, const s32 v12,
				   const s32 v20, const s32 v21, const s32 v22,
				   const s32 v30, const s32 v31, const s32 v32 )
{

	gSPTriangle(v00, v01, v02);
	gSPTriangle(v10, v11, v12);
	gSPTriangle(v20, v21, v22);
	gSPTriangle(v30, v31, v32);
	gSPFlushTriangles();
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

void gSPLoadUcodeEx( u32 uc_start, u32 uc_dstart, u16 uc_dsize )
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

	GBI.loadMicrocode(uc_start, uc_dstart, uc_dsize);
	RSP.uc_start = uc_start;
	RSP.uc_dstart = uc_dstart;

}

void gSPNoOp()
{
	gSPFlushTriangles();
}

void gSPMatrix( u32 matrix, u8 param )
{

	f32 mtx[4][4];
	u32 address = RSP_SegmentToPhysical( matrix );

	if (address + 64 > RDRAMSize) {
		return;
	}

	RSP_LoadMatrix( mtx, address );

	if (param & G_MTX_PROJECTION) {
		if (param & G_MTX_LOAD)
			CopyMatrix( gSP.matrix.projection, mtx );
		else
			MultMatrix2( gSP.matrix.projection, mtx );
	} else {
		if ((param & G_MTX_PUSH)) {
			if (gSP.matrix.modelViewi < (gSP.matrix.stackSize)) {
				CopyMatrix(gSP.matrix.modelView[gSP.matrix.modelViewi + 1], gSP.matrix.modelView[gSP.matrix.modelViewi]);
				gSP.matrix.modelViewi++;
			}
		}

		if (param & G_MTX_LOAD)
			CopyMatrix( gSP.matrix.modelView[gSP.matrix.modelViewi], mtx );
		else
			MultMatrix2( gSP.matrix.modelView[gSP.matrix.modelViewi], mtx );
		gSP.changed |= CHANGED_LIGHT | CHANGED_LOOKAT;
	}

	gSP.changed |= CHANGED_MATRIX;

}

void gSPDMAMatrix( u32 matrix, u8 index, u8 multiply )
{
	f32 mtx[4][4];
	u32 address = gSP.DMAOffsets.mtx + RSP_SegmentToPhysical( matrix );

	if (address + 64 > RDRAMSize) {
		return;
	}

	RSP_LoadMatrix(mtx, address);

	gSP.matrix.modelViewi = index;

	if (multiply)
		MultMatrix(gSP.matrix.modelView[0], mtx, gSP.matrix.modelView[gSP.matrix.modelViewi]);
	else
		CopyMatrix( gSP.matrix.modelView[gSP.matrix.modelViewi], mtx );

	CopyMatrix( gSP.matrix.projection, identityMatrix );


	gSP.changed |= CHANGED_MATRIX | CHANGED_LIGHT | CHANGED_LOOKAT;

}

void gSPViewport( u32 v )
{
	u32 address = RSP_SegmentToPhysical( v );

	if ((address + 16) > RDRAMSize) {
		return;
	}

	gSP.viewport.vscale[0] = _FIXED2FLOAT( *(s16*)&RDRAM[address +  2], 2 );
	gSP.viewport.vscale[1] = _FIXED2FLOAT( *(s16*)&RDRAM[address     ], 2 );
	gSP.viewport.vscale[2] = _FIXED2FLOAT( *(s16*)&RDRAM[address +  6], 10 );// * 0.00097847357f;
	gSP.viewport.vscale[3] = *(s16*)&RDRAM[address +  4];
	gSP.viewport.vtrans[0] = _FIXED2FLOAT( *(s16*)&RDRAM[address + 10], 2 );
	gSP.viewport.vtrans[1] = _FIXED2FLOAT( *(s16*)&RDRAM[address +  8], 2 );
	gSP.viewport.vtrans[2] = _FIXED2FLOAT( *(s16*)&RDRAM[address + 14], 10 );// * 0.00097847357f;
	gSP.viewport.vtrans[3] = *(s16*)&RDRAM[address + 12];

	if (gSP.viewport.vscale[1] < 0.0f && !GBI.isNegativeY())
		gSP.viewport.vscale[1] = -gSP.viewport.vscale[1];

	gSP.viewport.x		= gSP.viewport.vtrans[0] - gSP.viewport.vscale[0];
	gSP.viewport.y		= gSP.viewport.vtrans[1] - gSP.viewport.vscale[1];
	gSP.viewport.width = fabs(gSP.viewport.vscale[0]) * 2;
	gSP.viewport.height	= fabs(gSP.viewport.vscale[1] * 2);
	gSP.viewport.nearz	= gSP.viewport.vtrans[2] - gSP.viewport.vscale[2];
	gSP.viewport.farz	= (gSP.viewport.vtrans[2] + gSP.viewport.vscale[2]) ;

	gSP.changed |= CHANGED_VIEWPORT;

}

void gSPForceMatrix( u32 mptr )
{
	u32 address = RSP_SegmentToPhysical( mptr );

	if (address + 64 > RDRAMSize) {
		return;
	}

	RSP_LoadMatrix(gSP.matrix.combined, address);
	gSPApplyStereo(gSP.matrix.combined);

	gSP.changed &= ~CHANGED_MATRIX;

}

void gSPLight( u32 l, s32 n )
{
	--n;
	u32 addrByte = RSP_SegmentToPhysical( l );

	if ((addrByte + sizeof( Light )) > RDRAMSize) {
		return;
	}

	Light *light = (Light*)&RDRAM[addrByte];

	if (n < 8) {
		gSP.lights.rgb[n][R] = _FIXED2FLOATCOLOR(light->r,8);
		gSP.lights.rgb[n][G] = _FIXED2FLOATCOLOR(light->g,8);
		gSP.lights.rgb[n][B] = _FIXED2FLOATCOLOR(light->b,8);

		gSP.lights.xyz[n][X] = light->x;
		gSP.lights.xyz[n][Y] = light->y;
		gSP.lights.xyz[n][Z] = light->z;

		Normalize( gSP.lights.xyz[n] );
		u32 addrShort = addrByte >> 1;
		gSP.lights.pos_xyzw[n][X] = (float)(((short*)RDRAM)[(addrShort+4)^1]);
		gSP.lights.pos_xyzw[n][Y] = (float)(((short*)RDRAM)[(addrShort+5)^1]);
		gSP.lights.pos_xyzw[n][Z] = (float)(((short*)RDRAM)[(addrShort+6)^1]);
		gSP.lights.ca[n] = (float)(RDRAM[(addrByte +  3) ^ 3]);
		gSP.lights.la[n] = (float)(RDRAM[(addrByte +  7) ^ 3]);
		gSP.lights.qa[n] = (float)(RDRAM[(addrByte + 14) ^ 3]);
	}

	gSP.changed |= CHANGED_LIGHT;

}

void gSPLightCBFD( u32 l, s32 n )
{
	u32 addrByte = RSP_SegmentToPhysical( l );

	if ((addrByte + sizeof( Light )) > RDRAMSize) {
		return;
	}

	Light *light = (Light*)&RDRAM[addrByte];

	if (n < 12) {
		gSP.lights.rgb[n][R] = _FIXED2FLOATCOLOR(light->r, 8);
		gSP.lights.rgb[n][G] = _FIXED2FLOATCOLOR(light->g, 8);
		gSP.lights.rgb[n][B] = _FIXED2FLOATCOLOR(light->b, 8);

		gSP.lights.xyz[n][X] = light->x;
		gSP.lights.xyz[n][Y] = light->y;
		gSP.lights.xyz[n][Z] = light->z;

		Normalize( gSP.lights.xyz[n] );
		u32 addrShort = addrByte >> 1;
		gSP.lights.pos_xyzw[n][X] = (float)(((short*)RDRAM)[(addrShort+16)^1]);
		gSP.lights.pos_xyzw[n][Y] = (float)(((short*)RDRAM)[(addrShort+17)^1]);
		gSP.lights.pos_xyzw[n][Z] = (float)(((short*)RDRAM)[(addrShort+18)^1]);
		gSP.lights.pos_xyzw[n][W] = (float)(((short*)RDRAM)[(addrShort+19)^1]);
		gSP.lights.ca[n] = (float)(RDRAM[(addrByte + 12) ^ 3]) / 16.0f;
	}

	gSP.changed |= CHANGED_LIGHT;

}

void gSPLightAcclaim(u32 l, s32 n)
{
	u32 addrByte = RSP_SegmentToPhysical(l);

	if (n < 10) {
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

void gSPLookAt( u32 _l, u32 _n )
{
	u32 address = RSP_SegmentToPhysical(_l);

	if ((address + sizeof(Light)) > RDRAMSize) {
		return;
	}
	assert(_n < 2);

	Light *light = (Light*)&RDRAM[address];

	gSP.lookat.xyz[_n][X] = light->x;
	gSP.lookat.xyz[_n][Y] = light->y;
	gSP.lookat.xyz[_n][Z] = light->z;

	gSP.lookatEnable = (_n == 0) || (_n == 1 && (light->x != 0 || light->y != 0));

	Normalize(gSP.lookat.xyz[_n]);
	gSP.changed |= CHANGED_LOOKAT;
}

static
void gSPUpdateLightVectors()
{
	InverseTransformVectorNormalizeN(&gSP.lights.xyz[0], &gSP.lights.i_xyz[0], 
			gSP.matrix.modelView[gSP.matrix.modelViewi], gSP.numLights);
	gSP.changed ^= CHANGED_LIGHT;
	gSP.changed |= CHANGED_HW_LIGHT;
}

static
void gSPUpdateLookatVectors()
{
	if (gSP.lookatEnable) {
		InverseTransformVectorNormalizeN(&gSP.lookat.xyz[0], &gSP.lookat.i_xyz[0],
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

template <u32 VNUM>
void gSPLightVertexStandard(u32 v, SPVertex * spVtx)
{
#ifndef __NEON_OPT
	if (!isHWLightingAllowed()) {
		for(int j = 0; j < VNUM; ++j) {
			SPVertex & vtx = spVtx[v+j];
			vtx.r = gSP.lights.rgb[gSP.numLights][R];
			vtx.g = gSP.lights.rgb[gSP.numLights][G];
			vtx.b = gSP.lights.rgb[gSP.numLights][B];
			vtx.HWLight = 0;

			for (u32 i = 0; i < gSP.numLights; ++i) {
				const f32 intensity = DotProduct( &vtx.nx, gSP.lights.i_xyz[i] );
				if (intensity > 0.0f) {
					vtx.r += gSP.lights.rgb[i][R] * intensity;
					vtx.g += gSP.lights.rgb[i][G] * intensity;
					vtx.b += gSP.lights.rgb[i][B] * intensity;
				}
			}
			vtx.r = min(1.0f, vtx.r);
			vtx.g = min(1.0f, vtx.g);
			vtx.b = min(1.0f, vtx.b);
		}
	} else {
		for(int j = 0; j < VNUM; ++j) {
			SPVertex & vtx = spVtx[v+j];
			TransformVectorNormalize(&vtx.r, gSP.matrix.modelView[gSP.matrix.modelViewi]);
			vtx.HWLight = gSP.numLights;
		}
	}
#else
	void gSPLightVertex_NEON(u32 vnum, u32 v, SPVertex * spVtx);
	gSPLightVertex_NEON(VNUM, v, spVtx);
#endif
}

template <u32 VNUM>
void gSPLightVertexCBFD_basic(u32 v, SPVertex * spVtx)
{
	for (int j = 0; j < VNUM; ++j) {
		SPVertex & vtx = spVtx[v + j];
		vtx.HWLight = 0;
		if (vtx.flag < 0)
			continue;

		f32 r = gSP.lights.rgb[gSP.numLights][R];
		f32 g = gSP.lights.rgb[gSP.numLights][G];
		f32 b = gSP.lights.rgb[gSP.numLights][B];
		const f32 vPos[3] = {
			(vtx.x + gSP.cbfd.vertexCoordMod[ 8])*gSP.cbfd.vertexCoordMod[12],
			(vtx.y + gSP.cbfd.vertexCoordMod[ 9])*gSP.cbfd.vertexCoordMod[13],
			(vtx.z + gSP.cbfd.vertexCoordMod[10])*gSP.cbfd.vertexCoordMod[14]
		};

		for (int l = gSP.numLights - 2; l >= 0; --l) {
			const f32 vx = vPos[X] - gSP.lights.pos_xyzw[l][X];
			const f32 vy = vPos[Y] - gSP.lights.pos_xyzw[l][Y];
			const f32 vz = vPos[Z] - gSP.lights.pos_xyzw[l][Z];
			const f32 len = 2.0f * (vx*vx + vy*vy + vz*vz) * FIXED2FLOATRECIP16;
			f32 intensity = std::min(1.0f, gSP.lights.ca[l] / len);
			if (intensity > 0.0f) {
				r += gSP.lights.rgb[l][R] * intensity;
				g += gSP.lights.rgb[l][G] * intensity;
				b += gSP.lights.rgb[l][B] * intensity;
			}
		}

		r = min(1.0f, r);
		g = min(1.0f, g);
		b = min(1.0f, b);

		vtx.r *= r;
		vtx.g *= g;
		vtx.b *= b;
	}
}

template <u32 VNUM>
void gSPLightVertexCBFD_advanced(u32 v, SPVertex * spVtx)
{
	for (int j = 0; j < VNUM; ++j) {
		SPVertex & vtx = spVtx[v + j];
		vtx.HWLight = 0;
		if (vtx.flag < 0)
			continue;

		const f32 vPos[3] = {
			(vtx.x + gSP.cbfd.vertexCoordMod[ 8])*gSP.cbfd.vertexCoordMod[12],
			(vtx.y + gSP.cbfd.vertexCoordMod[ 9])*gSP.cbfd.vertexCoordMod[13],
			(vtx.z + gSP.cbfd.vertexCoordMod[10])*gSP.cbfd.vertexCoordMod[14]
		};
		int l = gSP.numLights;
		f32 r = gSP.lights.rgb[l][R];
		f32 g = gSP.lights.rgb[l][G];
		f32 b = gSP.lights.rgb[l][B];
		--l;
		f32 intensity = std::min(1.0f, DotProduct(&vtx.nx, gSP.lights.i_xyz[l]));
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
			intensity = std::min(1.0f, gSP.lights.ca[l] / len);
			if ((gSP.geometryMode & G_POINT_LIGHTING) != 0)
				intensity *= std::min(1.0f, DotProduct(&vtx.nx, gSP.lights.i_xyz[l]));;
			if (intensity > 0.0f) {
				r += gSP.lights.rgb[l][R] * intensity;
				g += gSP.lights.rgb[l][G] * intensity;
				b += gSP.lights.rgb[l][B] * intensity;
			}
		}

		r = min(1.0f, r);
		g = min(1.0f, g);
		b = min(1.0f, b);

		vtx.r *= r;
		vtx.g *= g;
		vtx.b *= b;
	}
}

template <u32 VNUM>
void gSPLightVertex(u32 _v, SPVertex * _spVtx)
{
	if (g_ConkerUcode) {
		if (gSP.cbfd.advancedLighting)
			gSPLightVertexCBFD_advanced<VNUM>(_v, _spVtx);
		else
			gSPLightVertexCBFD_basic<VNUM>(_v, _spVtx);
	} else
		gSPLightVertexStandard<VNUM>(_v, _spVtx);
}

void gSPLightVertex(SPVertex & _vtx)
{
	gSPLightVertex<1>(0, &_vtx);
}

template <u32 VNUM>
void gSPPointLightVertexZeldaMM(u32 v, float _vecPos[VNUM][4], SPVertex * spVtx)
{
	f32 intensity = 0.0f;
	for (int j = 0; j < VNUM; ++j) {
		SPVertex & vtx = spVtx[v + j];
		vtx.HWLight = 0;
		vtx.r = gSP.lights.rgb[gSP.numLights][R];
		vtx.g = gSP.lights.rgb[gSP.numLights][G];
		vtx.b = gSP.lights.rgb[gSP.numLights][B];
		gSPTransformVector(_vecPos[j], gSP.matrix.modelView[gSP.matrix.modelViewi]);

		for (u32 l = 0; l < gSP.numLights; ++l) {
			if (gSP.lights.ca[l] != 0.0f) {
				f32 recip = FIXED2FLOATRECIP16;
				// Point lighting
				f32 lvec[3] = { gSP.lights.pos_xyzw[l][X], gSP.lights.pos_xyzw[l][Y], gSP.lights.pos_xyzw[l][Z] };
				lvec[0] -= _vecPos[j][0];
				lvec[1] -= _vecPos[j][1];
				lvec[2] -= _vecPos[j][2];

				const f32 K = lvec[0] * lvec[0] + lvec[1] * lvec[1] + lvec[2] * lvec[2] * 2.0f;
				const f32 KS = sqrtf(K);

				gSPInverseTransformVector(lvec, gSP.matrix.modelView[gSP.matrix.modelViewi]);

				for (u32 i = 0; i < 3; ++i) {
					lvec[i] = (4.0f * lvec[i] / KS);
					if (lvec[i] < -1.0f)
						lvec[i] = -1.0f;
					if (lvec[i] > 1.0f)
						lvec[i] = 1.0f;
				}

				f32 V = lvec[0] * vtx.nx + lvec[1] * vtx.ny + lvec[2] * vtx.nz;
				if (V < -1.0f)
					V = -1.0f;
				if (V > 1.0f)
					V = 1.0f;

				const f32 KSF = floorf(KS);
				const f32 D = (KSF * gSP.lights.la[l] * 2.0f + KSF * KSF * gSP.lights.qa[l] / 8.0f) * recip + 1.0f;
				intensity = V / D;
			} else {
				// Standard lighting
				intensity = DotProduct(&vtx.nx, gSP.lights.i_xyz[l]);
			}
			if (intensity > 0.0f) {
				vtx.r += gSP.lights.rgb[l][R] * intensity;
				vtx.g += gSP.lights.rgb[l][G] * intensity;
				vtx.b += gSP.lights.rgb[l][B] * intensity;
			}
		}
		if (vtx.r > 1.0f) vtx.r = 1.0f;
		if (vtx.g > 1.0f) vtx.g = 1.0f;
		if (vtx.b > 1.0f) vtx.b = 1.0f;
	}
}

template <u32 VNUM>
void gSPPointLightVertex(u32 _v, float _vecPos[VNUM][4], SPVertex * _spVtx)
{
	if (g_ConkerUcode) {
		if (gSP.cbfd.advancedLighting)
			gSPLightVertexCBFD_advanced<VNUM>(_v, _spVtx);
		else
			gSPLightVertexCBFD_basic<VNUM>(_v, _spVtx);
	} else
		gSPPointLightVertexZeldaMM<VNUM>(_v, _vecPos, _spVtx);
}

template <u32 VNUM>
void gSPPointLightVertexAcclaim(u32 v, SPVertex * spVtx)
{
	for (int j = 0; j < VNUM; ++j) {
		SPVertex & vtx = spVtx[v + j];
		vtx.HWLight = 0;

		for (u32 l = 2; l < 10; ++l) {
			if (gSP.lights.ca[l] < 0)
				continue;

			const f32 dX = fabsf(gSP.lights.pos_xyzw[l][X] - vtx.x);
			const f32 dY = fabsf(gSP.lights.pos_xyzw[l][Y] - vtx.y);
			const f32 dZ = fabsf(gSP.lights.pos_xyzw[l][Z] - vtx.z);
			const f32 distance = dX + dY + dZ - gSP.lights.ca[l];
			if (distance >= 0.0f)
				continue;

			const f32 light_intensity = -distance * gSP.lights.la[l];
			vtx.r += gSP.lights.rgb[l][R] * light_intensity;
			vtx.g += gSP.lights.rgb[l][G] * light_intensity;
			vtx.b += gSP.lights.rgb[l][B] * light_intensity;
		}

		if (vtx.r > 1.0f) vtx.r = 1.0f;
		if (vtx.g > 1.0f) vtx.g = 1.0f;
		if (vtx.b > 1.0f) vtx.b = 1.0f;
	}
}

template <u32 VNUM>
void gSPBillboardVertex(u32 v, SPVertex * spVtx)
{
#ifndef __NEON_OPT
	SPVertex & vtx0 = spVtx[0];
	for (u32 j = 0; j < VNUM; ++j) {
		SPVertex & vtx = spVtx[v + j];
		vtx.x += vtx0.x;
		vtx.y += vtx0.y;
		vtx.z += vtx0.z;
		vtx.w += vtx0.w;
	}
#else
	if (VNUM == 1) {
		SPVertex & vtx0 = spVtx[0];
		SPVertex & vtx = spVtx[v];
		vtx.x += vtx0.x;
		vtx.y += vtx0.y;
		vtx.z += vtx0.z;
		vtx.w += vtx0.w;
	} else {
		void gSPBillboardVertex4NEON(u32 v);
		gSPBillboardVertex4NEON(v);
	}
#endif //__NEON_OPT
}

template <u32 VNUM>
void gSPApplyStereoConvergence(u32 v, SPVertex * spVtx)
{
	if (l_stereoEye != Config::stereoLeftEye && l_stereoEye != Config::stereoRightEye)
		return;

	const f32 separation = _stereoSeparation();
	const bool orthographic = _isOrthographic(gSP.matrix.combined);
	if (!orthographic)
		l_stereoPerspectiveSeen = true;
	const f32 convergence = l_autoConvergence;
	const bool measure = !orthographic &&
		(config.stereo.mode != Config::stereoBothEyes || l_stereoEye == Config::stereoLeftEye);
	for (u32 j = 0; j < VNUM; ++j) {
		SPVertex & vtx = spVtx[v+j];
		// gSPApplyStereo() already contributed separation*w, which is the parallax at infinity.
		const f32 matrixShift = separation * vtx.w;
		if (orthographic && _immersiveOn()) {
			// A background covers the game's screen and is stretched over
			// the whole view, keeping the parallax at infinity the matrix gave. Overlays are placed
			// where the game's screen was, on the screen plane.
			const f32 ow = vtx.w != 0.0f ? vtx.w : 1.0f;
			f32 x, y, z, w;
			if (!l_stereoPerspectiveSeen) {
				_immersivePlaceSky((vtx.x - matrixShift) / ow, STEREO_FLIP_Y * vtx.y / ow, vtx.z / ow, x, y, z, w);
				x += separation * w;
			} else if (l_immersiveVoted && (fabsf((vtx.x - matrixShift) / ow) >= 0.95f || fabsf(vtx.y / ow) >= 0.95f)) {
				// Factor 5 overlays reaching the edge of the screen, such as fades, cover the view
				_immersivePlaceSky((vtx.x - matrixShift) / ow, STEREO_FLIP_Y * vtx.y / ow, vtx.z / ow, x, y, z, w);
			} else {
				_immersivePlaceFlat((vtx.x - matrixShift) / ow, STEREO_FLIP_Y * vtx.y / ow, vtx.z / ow, x, y, z, w,
					config.stereo.immersiveHudScale);
			}
			vtx.x = x;
			vtx.y = STEREO_FLIP_Y * y;
			vtx.z = z;
			vtx.w = w;
			continue;
		}
		if (orthographic) {
			// A flat projection carries no depth. Backgrounds keep the parallax at infinity, overlays
			// drawn after the scene.
			if (l_stereoPerspectiveSeen)
				vtx.x -= matrixShift;
			continue;
		}
		// Replace that contribution with separation*(w-convergence), capped to the magnitude of the
		// parallax at infinity.
		if (measure && vtx.w > 0.0f)
			_stereoMeasure(vtx.w);
		// Nothing measured yet: keep the parallax at infinity the matrix gave
		if (convergence <= 0.0f)
			continue;
		// Physical parallax in screen space is separation * (1 - convergence/w).
		const f32 requestedShift = config.stereo.depthBoost > 0.0f && vtx.w > 0.0f
			? separation * vtx.w * (1.0f - powf(convergence / vtx.w, 1.0f - 0.7f * config.stereo.depthBoost))
			: separation * (vtx.w - convergence);
		const f32 limit = fabsf(separation * vtx.w);
		const f32 safeShift = max(-limit, min(requestedShift, limit));
		vtx.x += safeShift - matrixShift;
	}
}

template <u32 VNUM>
void gSPClipVertex(u32 v, SPVertex * spVtx)
{
	for (u32 j = 0; j < VNUM; ++j) {
		SPVertex & vtx = spVtx[v+j];
		vtx.clip = 0;
		if (vtx.x > +vtx.w) vtx.clip |= CLIP_POSX;
		if (vtx.x < -vtx.w) vtx.clip |= CLIP_NEGX;
		if (vtx.y > +vtx.w) vtx.clip |= CLIP_POSY;
		if (vtx.y < -vtx.w) vtx.clip |= CLIP_NEGY;
		if (vtx.w < 0.01f) vtx.clip |= CLIP_W;
	}
}

// Immersive mode computes positions for the whole picture.
template <u32 VNUM>
void gSPImmersiveViewport(u32 v, SPVertex * spVtx)
{
	if (!gSPImmersiveActive() || gSP.viewport.vscale[0] == 0.0f || gSP.viewport.vscale[1] == 0.0f)
		return;
	const f32 halfWidth = static_cast<f32>(gDP.colorImage.width) * 0.5f;
	const f32 halfHeight = VI.height > 0 ? static_cast<f32>(VI.height) * 0.5f : halfWidth * 0.75f;
	for (u32 j = 0; j < VNUM; ++j) {
		SPVertex & vtx = spVtx[v+j];
		vtx.x = (halfWidth * vtx.x + (halfWidth - gSP.viewport.vtrans[0]) * vtx.w) / gSP.viewport.vscale[0];
		vtx.y = STEREO_FLIP_Y * (halfHeight * STEREO_FLIP_Y * vtx.y - (halfHeight - gSP.viewport.vtrans[1]) * vtx.w) /
			gSP.viewport.vscale[1];
	}
}

template <u32 VNUM>
void gSPTransformVertex(u32 v, SPVertex * spVtx, float mtx[4][4])
{
#ifndef __NEON_OPT
	float x, y, z;
	for (int i = 0; i < VNUM; ++i) {
		SPVertex & vtx = spVtx[v+i];
		x = vtx.x;
		y = vtx.y;
		z = vtx.z;
		vtx.x = x * mtx[0][0] + y * mtx[1][0] + z * mtx[2][0] + mtx[3][0];
		vtx.y = x * mtx[0][1] + y * mtx[1][1] + z * mtx[2][1] + mtx[3][1];
		vtx.z = x * mtx[0][2] + y * mtx[1][2] + z * mtx[2][2] + mtx[3][2];
		vtx.w = x * mtx[0][3] + y * mtx[1][3] + z * mtx[2][3] + mtx[3][3];
	}
#else
	void gSPTransformVector_NEON(float vtx[4], float mtx[4][4]);
	void gSPTransformVertex4NEON(u32 v, float mtx[4][4]);
	if (VNUM == 1)
		gSPTransformVector_NEON(&spVtx[v].x, mtx);
	else
		gSPTransformVertex4NEON(v, mtx);
#endif //__NEON_OPT
}

template <u32 VNUM>
void gSPProcessVertex(u32 v, SPVertex * spVtx)
{
	if (gSP.changed & CHANGED_MATRIX)
		_gSPCombineMatrices();

	float vPos[VNUM][4];
	for(u32 i = 0; i < VNUM; ++i) {
		SPVertex & vtx = spVtx[v+i];
		vPos[i][0] = vtx.x;
		vPos[i][1] = vtx.y;
		vPos[i][2] = vtx.z;
		vPos[i][3] = 0.0f;
		vtx.modify = 0;
	}

	gSPTransformVertex<VNUM>(v, spVtx, gSP.matrix.combined );

	if (dwnd().isAdjustScreen() && (gDP.colorImage.width > VI.width * 98 / 100)) {
		const f32 adjustScale = dwnd().getAdjustScale();
		for(int i = 0; i < VNUM; ++i) {
			SPVertex & vtx = spVtx[v+i];
			vtx.x *= adjustScale;
			if (gSP.matrix.projection[3][2] == -1.f)
				vtx.w *= adjustScale;
		}
	}
	if (gSP.viewport.vscale[0] < 0) {
		for(int i = 0; i < VNUM; ++i) {
			SPVertex & vtx = spVtx[v+i];
			vtx.x = -vtx.x;
		}
	}
	if (gSP.viewport.vscale[1] < 0) {
		for(int i = 0; i < VNUM; ++i) {
			SPVertex & vtx = spVtx[v+i];
			vtx.y = -vtx.y;
		}
	}

	if (gSP.matrix.billboard)
		gSPBillboardVertex<VNUM>(v, spVtx);

	gSPApplyStereoConvergence<VNUM>(v, spVtx);
	gSPClipVertex<VNUM>(v, spVtx);
	gSPImmersiveViewport<VNUM>(v, spVtx);

	if (gSP.geometryMode & G_LIGHTING) {
		if (gSP.geometryMode & G_POINT_LIGHTING)
			gSPPointLightVertex<VNUM>(v, vPos, spVtx);
		else
			gSPLightVertex<VNUM>(v, spVtx);

		if (gSP.geometryMode & G_ACCLAIM_LIGHTING)
			gSPPointLightVertexAcclaim<VNUM>(v, spVtx);

		if ((gSP.geometryMode & G_TEXTURE_GEN) != 0) {
			if (GBI.getMicrocodeType() != F3DFLX2) {
				for(int i = 0; i < VNUM; ++i) {
					SPVertex & vtx = spVtx[v+i];
					f32 fLightDir[3] = {vtx.nx, vtx.ny, vtx.nz};
					f32 x, y;
					if (gSP.lookatEnable) {
						x = DotProduct(gSP.lookat.i_xyz[0], fLightDir);
						y = DotProduct(gSP.lookat.i_xyz[1], fLightDir);
					} else {
						fLightDir[0] *= 128.0f;
						fLightDir[1] *= 128.0f;
						fLightDir[2] *= 128.0f;
						TransformVectorNormalize(fLightDir, gSP.matrix.modelView[gSP.matrix.modelViewi]);
						x = fLightDir[0];
						y = fLightDir[1];
					}
					if (gSP.geometryMode & G_TEXTURE_GEN_LINEAR) {
						if (x < -1.0f) x = -1.0f;
						if (x > 1.0f) x = 1.0f;
						if (y < -1.0f) y = -1.0f;
						if (y > 1.0f) y = 1.0f;
						vtx.s = acosf(-x) * 325.94931f;
						vtx.t = acosf(-y) * 325.94931f;
					} else { // G_TEXTURE_GEN
						vtx.s = (x + 1.0f) * 512.0f;
						vtx.t = (y + 1.0f) * 512.0f;
					}
				}
			} else {
				for(int i = 0; i < VNUM; ++i) {
					SPVertex & vtx = spVtx[v+i];
					const f32 intensity = DotProduct(gSP.lookat.i_xyz[0], &vtx.nx) * 128.0f;
					const s16 index = static_cast<s16>(intensity);
					vtx.a = _FIXED2FLOATCOLOR(RDRAM[(gSP.DMAIO_address + 128 + index) ^ 3], 8);
				}
			}
		}
	} else if (gSP.geometryMode & G_ACCLAIM_LIGHTING) {
		gSPPointLightVertexAcclaim<VNUM>(v, spVtx);
	} else {
		for(u32 i = 0; i < VNUM; ++i)
			spVtx[v].HWLight = 0;
	}

	for(u32 i = 0; i < VNUM; ++i) {
		SPVertex & vtx = spVtx[v+i];
	}
}

template <u32 VNUM>
u32 gSPLoadVertexData(const Vertex *orgVtx, SPVertex * spVtx, u32 v0, u32 vi, u32 n)
{
	const u32 end = n - (n%VNUM) + v0;
	for (; vi < end; vi += VNUM) {
		for(u32 j = 0; j < VNUM; ++j) {
			SPVertex & vtx = spVtx[vi+j];
			vtx.x = orgVtx->x;
			vtx.y = orgVtx->y;
			vtx.z = orgVtx->z;
			//vtx.flag = vertex->flag;
			vtx.s = _FIXED2FLOAT( orgVtx->s, 5 );
			vtx.t = _FIXED2FLOAT( orgVtx->t, 5 );

			if (gSP.geometryMode & G_LIGHTING) {
				vtx.nx = _FIXED2FLOATCOLOR(orgVtx->normal.x, 7);
				vtx.ny = _FIXED2FLOATCOLOR(orgVtx->normal.y, 7);
				vtx.nz = _FIXED2FLOATCOLOR(orgVtx->normal.z, 7);
				if (isHWLightingAllowed()) {
					vtx.r = orgVtx->normal.x;
					vtx.g = orgVtx->normal.y;
					vtx.b = orgVtx->normal.z;
				}
			} else {
				vtx.r = _FIXED2FLOATCOLOR(orgVtx->color.r, 8);
				vtx.g = _FIXED2FLOATCOLOR(orgVtx->color.g, 8);
				vtx.b = _FIXED2FLOATCOLOR(orgVtx->color.b, 8);
			}
			vtx.a = _FIXED2FLOATCOLOR(orgVtx->color.a, 8);

			++orgVtx;
		}
		gSPProcessVertex<VNUM>(vi, spVtx);
	}
	return vi;
}

void gSPVertex(u32 a, u32 n, u32 v0)
{

	if ((n + v0) > INDEXMAP_SIZE) {
		LOG(LOG_ERROR, "Using Vertex outside buffer v0=%i, n=%i\n", v0, n);
		return;
	}

	const u32 address = RSP_SegmentToPhysical(a);

	if ((address + sizeof(Vertex)* n) > RDRAMSize) {
		return;
	}

	if ((gSP.geometryMode & G_LIGHTING) != 0) {

		if ((gSP.changed & CHANGED_LIGHT) != 0)
			gSPUpdateLightVectors();

		if (((gSP.geometryMode & G_TEXTURE_GEN) != 0) && ((gSP.changed & CHANGED_LOOKAT) != 0))
			gSPUpdateLookatVectors();
	}

	const Vertex *vertex = (Vertex*)&RDRAM[address];
	SPVertex * spVtx = dwnd().getDrawer().getVertexPtr(0);
	u32 i = gSPLoadVertexData<VEC_OPT>(vertex, spVtx, v0, v0, n);
	if (i < n + v0)
		gSPLoadVertexData<1>(vertex + (i - v0), spVtx, v0, i, n);
}

template <u32 VNUM>
u32 gSPLoadCIVertexData(const PDVertex *orgVtx, SPVertex * spVtx, u32 v0, u32 vi, u32 n)
{
	const u32 end = n - (n%VNUM) + v0;
	for (; vi < end; vi += VNUM) {
		for(u32 j = 0; j < VNUM; ++j) {
			SPVertex & vtx = spVtx[vi+j];
			vtx.x = orgVtx->x;
			vtx.y = orgVtx->y;
			vtx.z = orgVtx->z;
			vtx.s = _FIXED2FLOAT( orgVtx->s, 5 );
			vtx.t = _FIXED2FLOAT( orgVtx->t, 5 );
			u8 *color = &RDRAM[gSP.vertexColorBase + (orgVtx->ci & 0xff)];

			if (gSP.geometryMode & G_LIGHTING) {
				vtx.nx = _FIXED2FLOATCOLOR((s8)color[3], 7);
				vtx.ny = _FIXED2FLOATCOLOR((s8)color[2], 7);
				vtx.nz = _FIXED2FLOATCOLOR((s8)color[1], 7);
				if (isHWLightingAllowed()) {
					vtx.r = (s8)color[3];
					vtx.g = (s8)color[2];
					vtx.b = (s8)color[1];
				}
			} else {
				vtx.r = _FIXED2FLOATCOLOR(color[3], 8);
				vtx.g = _FIXED2FLOATCOLOR(color[2], 8);
				vtx.b = _FIXED2FLOATCOLOR(color[1], 8);
			}
			vtx.a = _FIXED2FLOATCOLOR(color[0], 8);

			++orgVtx;
		}
		gSPProcessVertex<VNUM>(vi, spVtx);
	}
	return vi;
}

void gSPCIVertex( u32 a, u32 n, u32 v0 )
{

	if ((n + v0) > INDEXMAP_SIZE) {
		LOG(LOG_ERROR, "Using Vertex outside buffer v0=%i, n=%i\n", v0, n);
		return;
	}

	const u32 address = RSP_SegmentToPhysical( a );

	if ((address + sizeof( PDVertex ) * n) > RDRAMSize)
		return;

	if ((gSP.geometryMode & G_LIGHTING) != 0) {

		if ((gSP.changed & CHANGED_LIGHT) != 0)
			gSPUpdateLightVectors();

		if (((gSP.geometryMode & G_TEXTURE_GEN) != 0) && ((gSP.changed & CHANGED_LOOKAT) != 0))
			gSPUpdateLookatVectors();
	}

	const PDVertex *vertex = (PDVertex*)&RDRAM[address];
	SPVertex * spVtx = dwnd().getDrawer().getVertexPtr(0);
	u32 i = gSPLoadCIVertexData<VEC_OPT>(vertex, spVtx, v0, v0, n);
	if (i < n + v0)
		gSPLoadCIVertexData<1>(vertex + (i - v0), spVtx, v0, i, n);
}


template <u32 VNUM>
u32 gSPLoadDMAVertexData(u32 address, SPVertex * spVtx, u32 v0, u32 vi, u32 n)
{
	const u32 end = n - (n%VNUM) + v0;
	for (; vi < end; vi += VNUM) {
		for(u32 j = 0; j < VNUM; ++j) {
			SPVertex & vtx = spVtx[vi+j];
			vtx.x = *(s16*)&RDRAM[address ^ 2];
			vtx.y = *(s16*)&RDRAM[(address + 2) ^ 2];
			vtx.z = *(s16*)&RDRAM[(address + 4) ^ 2];

			vtx.r = _FIXED2FLOATCOLOR((*(u8*)&RDRAM[(address + 6) ^ 3]), 8);
			vtx.g = _FIXED2FLOATCOLOR((*(u8*)&RDRAM[(address + 7) ^ 3]), 8);
			vtx.b = _FIXED2FLOATCOLOR((*(u8*)&RDRAM[(address + 8) ^ 3]), 8);
			vtx.a = _FIXED2FLOATCOLOR((*(u8*)&RDRAM[(address + 9) ^ 3]), 8);

			address += 10;
		}
		gSPProcessVertex<VNUM>(vi, spVtx);
	}
	return vi;
}

void gSPDMAVertex( u32 a, u32 n, u32 v0 )
{

	if ((n + v0) > INDEXMAP_SIZE) {
		LOG(LOG_ERROR, "Using Vertex outside buffer v0=%i, n=%i\n", v0, n);
		return;
	}

	const u32 address = gSP.DMAOffsets.vtx + RSP_SegmentToPhysical(a);

	if ((address + 10 * n) > RDRAMSize)
		return;

	SPVertex * spVtx = dwnd().getDrawer().getVertexPtr(0);
	u32 i = gSPLoadDMAVertexData<VEC_OPT>(address, spVtx, v0, v0, n);
	if (i < n + v0)
		gSPLoadDMAVertexData<1>(address + (i - v0) * 10, spVtx, v0, i, n);
}

template <u32 VNUM>
u32 gSPLoadCBFDVertexData(const Vertex *orgVtx, SPVertex * spVtx, u32 v0, u32 vi, u32 n)
{
	const u32 end = n - (n%VNUM) + v0;
	for (; vi < end; vi += VNUM) {
		for(u32 j = 0; j < VNUM; ++j) {
			SPVertex & vtx = spVtx[vi+j];
			vtx.x = orgVtx->x;
			vtx.y = orgVtx->y;
			vtx.z = orgVtx->z;
			vtx.s = _FIXED2FLOAT( orgVtx->s, 5 );
			vtx.t = _FIXED2FLOAT( orgVtx->t, 5 );
			vtx.flag = orgVtx->flag;
			if (gSP.geometryMode & G_LIGHTING) {
				const u32 normaleAddrOffset = ((vi+j)<<1);
				vtx.nx = _FIXED2FLOATCOLOR(((s8*)RDRAM)[(gSP.cbfd.vertexNormalBase + normaleAddrOffset + 0) ^ 3], 7);
				vtx.ny = _FIXED2FLOATCOLOR(((s8*)RDRAM)[(gSP.cbfd.vertexNormalBase + normaleAddrOffset + 1) ^ 3], 7);
				vtx.nz = _FIXED2FLOATCOLOR((s8)(orgVtx->flag & 0xFF), 7);
			}
			vtx.r = _FIXED2FLOATCOLOR(orgVtx->color.r, 8);
			vtx.g = _FIXED2FLOATCOLOR(orgVtx->color.g, 8);
			vtx.b = _FIXED2FLOATCOLOR(orgVtx->color.b, 8);
			vtx.a = _FIXED2FLOATCOLOR(orgVtx->color.a, 8);
			++orgVtx;
		}
		gSPProcessVertex<VNUM>(vi, spVtx);
	}
	return vi;
}

void gSPCBFDVertex( u32 a, u32 n, u32 v0 )
{

	if ((n + v0) > INDEXMAP_SIZE) {
		LOG(LOG_ERROR, "Using Vertex outside buffer v0=%i, n=%i\n", v0, n);
		return;
	}

	const u32 address = RSP_SegmentToPhysical(a);

	if ((address + sizeof( Vertex ) * n) > RDRAMSize)
		return;

	if ((gSP.geometryMode & G_LIGHTING) != 0) {

		if ((gSP.changed & CHANGED_LIGHT) != 0)
			gSPUpdateLightVectors();

		if (((gSP.geometryMode & G_TEXTURE_GEN) != 0) && ((gSP.changed & CHANGED_LOOKAT) != 0))
			gSPUpdateLookatVectors();
	}

	const Vertex *vertex = (Vertex*)&RDRAM[address];
	SPVertex * spVtx = dwnd().getDrawer().getVertexPtr(0);
	u32 i = gSPLoadCBFDVertexData<VEC_OPT>(vertex, spVtx, v0, v0, n);
	if (i < n + v0)
		gSPLoadCBFDVertexData<1>(vertex + (i - v0), spVtx, v0, i, n);
}

static
void calcF3DAMTexCoords(const Vertex * _vertex, SPVertex & _vtx)
{
	const u32 s0 = (u32)_vertex->s;
	const u32 t0 = (u32)_vertex->t;
	const u32 acum_0 = ((_SHIFTR(gSP.textureCoordScaleOrg, 0, 16) * t0) << 1) + 0x8000;
	const u32 acum_1 = ((_SHIFTR(gSP.textureCoordScale[1], 0, 16) * t0) << 1) + 0x8000;
	const u32 sres = ((_SHIFTR(gSP.textureCoordScaleOrg, 16, 16) * s0) << 1) + acum_0;
	const u32 tres = ((_SHIFTR(gSP.textureCoordScale[1], 16, 16) * s0) << 1) + acum_1;
	const s16 s = _SHIFTR(sres, 16, 16) + _SHIFTR(gSP.textureCoordScale[0], 16, 16);
	const s16 t = _SHIFTR(tres, 16, 16) + _SHIFTR(gSP.textureCoordScale[0], 0, 16);

	_vtx.s = _FIXED2FLOAT( s, 5 );
	_vtx.t = _FIXED2FLOAT( t, 5 );
}

template <u32 VNUM>
u32 gSPLoadF3DAMVertexData(const Vertex *orgVtx, SPVertex * spVtx, u32 v0, u32 vi, u32 n)
{
	const u32 end = n - (n%VNUM) + v0;
	for (; vi < end; vi += VNUM) {
		for(u32 j = 0; j < VNUM; ++j) {
			SPVertex & vtx = spVtx[vi+j];
			vtx.x = orgVtx->x;
			vtx.y = orgVtx->y;
			vtx.z = orgVtx->z;
			//vtx.flag = orgVtx->flag;
			calcF3DAMTexCoords(orgVtx, vtx);
			if (gSP.geometryMode & G_LIGHTING) {
				vtx.nx = _FIXED2FLOATCOLOR( orgVtx->normal.x, 7 );
				vtx.ny = _FIXED2FLOATCOLOR( orgVtx->normal.y, 7 );
				vtx.nz = _FIXED2FLOATCOLOR( orgVtx->normal.z, 7 );
				vtx.a = _FIXED2FLOATCOLOR(orgVtx->color.a,8);
			} else {
				vtx.r = _FIXED2FLOATCOLOR(orgVtx->color.r,8);
				vtx.g = _FIXED2FLOATCOLOR(orgVtx->color.g,8);
				vtx.b = _FIXED2FLOATCOLOR(orgVtx->color.b,8);
				vtx.a = _FIXED2FLOATCOLOR(orgVtx->color.a,8);
			}
			++orgVtx;
		}
		gSPProcessVertex<VNUM>(vi, spVtx);
	}
	return vi;
}

void gSPF3DAMVertex(u32 a, u32 n, u32 v0)
{

	if ((n + v0) > INDEXMAP_SIZE) {
		LOG(LOG_ERROR, "Using Vertex outside buffer v0=%i, n=%i\n", v0, n);
		return;
	}

	const u32 address = RSP_SegmentToPhysical(a);

	if ((address + sizeof(Vertex)* n) > RDRAMSize) {
		return;
	}

	if ((gSP.geometryMode & G_LIGHTING) != 0) {

		if ((gSP.changed & CHANGED_LIGHT) != 0)
			gSPUpdateLightVectors();

		if (((gSP.geometryMode & G_TEXTURE_GEN) != 0) && ((gSP.changed & CHANGED_LOOKAT) != 0))
			gSPUpdateLookatVectors();
	}

	const Vertex *vertex = (Vertex*)&RDRAM[address];
	SPVertex * spVtx = dwnd().getDrawer().getVertexPtr(0);
	u32 i = gSPLoadF3DAMVertexData<VEC_OPT>(vertex, spVtx, v0, v0, n);
	if (i < n + v0)
		gSPLoadF3DAMVertexData<1>(vertex + (i - v0), spVtx, v0, i, n);
}

template <u32 VNUM>
u32 gSPLoadSWVertexData(const SWVertex *orgVtx, SPVertex * spVtx, u32 vi, u32 n)
{
	const u32 end = n - (n%VNUM);
	for (; vi < end; vi += VNUM) {
		for(u32 j = 0; j < VNUM; ++j) {
			SPVertex & vtx = spVtx[vi+j];
			vtx.x = orgVtx->x;
			vtx.y = orgVtx->y;
			vtx.z = orgVtx->z;
			++orgVtx;
		}
		gSPProcessVertex<VNUM>(vi, spVtx);
		for (u32 j = 0; j < VNUM; ++j) {
			SPVertex & vtx = spVtx[vi+j];
			vtx.y = -vtx.y;
		}
	}
	return vi;
}

void gSPSWVertex(const SWVertex * vertex, u32 n, const bool * const verticesToProcess)
{

	SPVertex * spVtx = dwnd().getDrawer().getVertexPtr(0);
	if (verticesToProcess == nullptr) {
		u32 i = gSPLoadSWVertexData<VEC_OPT>(vertex, spVtx, 0, n);
		if (i < n)
			gSPLoadSWVertexData<1>(vertex + i, spVtx, i, n);
	} else {
		for (u32 i = 0; i < n; ++i) {
			if (verticesToProcess[i])
				gSPLoadSWVertexData<1>(vertex + i, spVtx, i, i + 1);
		}
	}
}

void gSPSWVertex(const SWVertex * vertex, u32 v0, u32 n)
{

	SPVertex * spVtx = dwnd().getDrawer().getVertexPtr(0);
	const u32 endIdx = v0 + n;
	u32 i = gSPLoadSWVertexData<VEC_OPT>(vertex, spVtx, v0, endIdx);
	if (i < endIdx)
		gSPLoadSWVertexData<1>(vertex + i - v0, spVtx, i, endIdx);
}

void gSPT3DUXVertex(u32 a, u32 n, u32 ci)
{
	const u32 address = RSP_SegmentToPhysical(a);
	const u32 colors = RSP_SegmentToPhysical(ci);

	struct T3DUXVertex {
		s16 y;
		s16 x;
		u16 flag;
		s16 z;
	} *vertex = (T3DUXVertex*)&RDRAM[address];

	struct T3DUXColor
	{
		u8 a;
		u8 b;
		u8 g;
		u8 r;
	} *color = (T3DUXColor*)&RDRAM[colors];

	/* The sibling vertex loaders bound the destination to INDEXMAP_SIZE
	 * (here v0 is implicitly 0) and both source spans to RDRAM. This one
	 * only checked the vertex source: add the destination and colour
	 * source bounds too. */
	if (n > INDEXMAP_SIZE)
		return;

	if ((address + sizeof(T3DUXVertex)* n) > RDRAMSize)
		return;

	if ((colors + sizeof(T3DUXColor)* n) > RDRAMSize)
		return;

	SPVertex * spVtx = dwnd().getDrawer().getVertexPtr(0);
	u32 i = 0;
#ifdef __VEC4_OPT
	for (; i < n - (n % 4); i += 4) {
		u32 v = i;
		for (int j = 0; j < 4; ++j) {
			SPVertex & vtx = spVtx[v+j];
			vtx.x = vertex->x;
			vtx.y = vertex->y;
			vtx.z = vertex->z;
			vtx.s = 0;
			vtx.t = 0;
			vtx.r = _FIXED2FLOATCOLOR(color->r, 8);
			vtx.g = _FIXED2FLOATCOLOR(color->g, 8);
			vtx.b = _FIXED2FLOATCOLOR(color->b, 8);
			vtx.a = _FIXED2FLOATCOLOR(color->a, 8);
			vertex++;
			color++;
		}
		gSPProcessVertex<4>(v, spVtx);
	}
#endif
	for (; i < n; ++i) {
		SPVertex & vtx = spVtx[i];
		vtx.x = vertex->x;
		vtx.y = vertex->y;
		vtx.z = vertex->z;
		vtx.s = 0;
		vtx.t = 0;
		vtx.r = _FIXED2FLOATCOLOR(color->r, 8);
		vtx.g = _FIXED2FLOATCOLOR(color->g, 8);
		vtx.b = _FIXED2FLOATCOLOR(color->b, 8);
		vtx.a = _FIXED2FLOATCOLOR(color->a, 8);
		gSPProcessVertex<1>(i, spVtx);
		vertex++;
		color++;
	}
}

void gSPDisplayList( u32 dl )
{
	u32 address = RSP_SegmentToPhysical( dl );

	if ((address + 8) > RDRAMSize) {
		return;
	}

	if (RSP.PCi < (GBI.PCStackSize - 1)) {
		RSP.PCi++;
		RSP.PC[RSP.PCi] = address;
		RSP.nextCmd = _SHIFTR( *(u32*)&RDRAM[address], 24, 8 );
	} else {
		assert(false);
	}
}

void gSPBranchList( u32 dl )
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

void gSPBranchLessZ(u32 branchdl, u32 vtx, u32 zval)
{
	const u32 address = RSP_SegmentToPhysical( branchdl );

	if ((address + 8) > RDRAMSize) {
		return;
	}

	SPVertex & v = dwnd().getDrawer().getVertex(vtx);
	const u32 zTest = u32((v.z / v.w) * 1023.0f);
	if (zTest > 0x03FF || zTest <= zval)
		RSP.PC[RSP.PCi] = address;

}

void gSPBranchLessW( u32 branchdl, u32 vtx, u32 wval )
{
	const u32 address = RSP_SegmentToPhysical( branchdl );

	if ((address + 8) > RDRAMSize) {
		return;
	}

	SPVertex & v = dwnd().getDrawer().getVertex(vtx);
	if (v.w < (float)wval)
		RSP.PC[RSP.PCi] = address;

}

void gSPDlistCount(u32 count, u32 v)
{
	u32 address = RSP_SegmentToPhysical( v );
	if (address == 0 || (address + 8) > RDRAMSize) {
		return;
	}

	if (RSP.PCi >= 9) {
		return;
	}


	++RSP.PCi;  // go to the next PC in the stack
	RSP.PC[RSP.PCi] = address;  // jump to the address
	RSP.nextCmd = _SHIFTR( *(u32*)&RDRAM[address], 24, 8 );
	RSP.count = count + 1;
}

void gSPSetDMAOffsets( u32 mtxoffset, u32 vtxoffset )
{
	gSP.DMAOffsets.mtx = mtxoffset;
	gSP.DMAOffsets.vtx = vtxoffset;

}

void gSPSetDMATexOffset(u32 _addr)
{
	gSP.DMAOffsets.tex_offset = RSP_SegmentToPhysical(_addr);
	gSP.DMAOffsets.tex_shift = 0;
	gSP.DMAOffsets.tex_count = 0;
}

void gSPSetVertexColorBase( u32 base )
{
	gSP.vertexColorBase = RSP_SegmentToPhysical( base );

}

void gSPDMATriangles( u32 tris, u32 n ){
	const u32 address = RSP_SegmentToPhysical( tris );

	if (address + sizeof( DKRTriangle ) * n > RDRAMSize) {
		return;
	}

	GraphicsDrawer & drawer = dwnd().getDrawer();
	drawer.setDMAVerticesSize(n * 3);

	DKRTriangle *triangles = (DKRTriangle*)&RDRAM[address];
	SPVertex * pVtx = drawer.getDMAVerticesData();
	for (u32 i = 0; i < n; ++i) {
		int mode = 0;
		if (!(triangles->flag & 0x40)) {
			if (gSP.viewport.vscale[0] > 0)
				mode |= G_CULL_BACK;
			else
				mode |= G_CULL_FRONT;
		}
		if ((gSP.geometryMode&G_CULL_BOTH) != mode) {
			drawer.drawDMATriangles(static_cast<u32>(pVtx - drawer.getDMAVerticesData()));
			pVtx = drawer.getDMAVerticesData();
			gSP.geometryMode &= ~G_CULL_BOTH;
			gSP.geometryMode |= mode;
			gSP.changed |= CHANGED_GEOMETRYMODE;
		}

		const s32 v0 = triangles->v0;
		const s32 v1 = triangles->v1;
		const s32 v2 = triangles->v2;
		if (drawer.isClipped(v0, v1, v2)) {
			++triangles;
			continue;
		}
		*pVtx = drawer.getVertex(v0);
		pVtx->s = _FIXED2FLOAT(triangles->s0, 5);
		pVtx->t = _FIXED2FLOAT(triangles->t0, 5);
		++pVtx;
		*pVtx = drawer.getVertex(v1);
		pVtx->s = _FIXED2FLOAT(triangles->s1, 5);
		pVtx->t = _FIXED2FLOAT(triangles->t1, 5);
		++pVtx;
		*pVtx = drawer.getVertex(v2);
		pVtx->s = _FIXED2FLOAT(triangles->s2, 5);
		pVtx->t = _FIXED2FLOAT(triangles->t2, 5);
		++pVtx;
		++triangles;
	}
	drawer.drawDMATriangles(static_cast<u32>(pVtx - drawer.getDMAVerticesData()));
}

void gSP1Quadrangle( s32 v0, s32 v1, s32 v2, s32 v3 )
{
	gSPTriangle( v0, v1, v2);
	gSPTriangle( v0, v2, v3);
	gSPFlushTriangles();

}

bool gSPCullVertices( u32 v0, u32 vn )
{
	if (vn < v0) {
		// Aidyn Chronicles - The First Mage seems to pass parameters in reverse order.
		const u32 v = v0;
		v0 = vn;
		vn = v;
	}
	u32 clip = 0;
	GraphicsDrawer & drawer = dwnd().getDrawer();
	for (u32 i = v0; i <= vn; ++i) {
		clip |= (~drawer.getVertex(i).clip) & CLIP_ALL;
		if (clip == CLIP_ALL)
			return false;
	}
	return true;
}

void gSPCullDisplayList( u32 v0, u32 vn )
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

void gSPPopMatrixN(u32 param, u32 num)
{
	if (gSP.matrix.modelViewi > num - 1) {
		gSP.matrix.modelViewi -= num;
		gSP.changed |= CHANGED_MATRIX | CHANGED_LIGHT | CHANGED_LOOKAT;
	} else {
	}
}

void gSPPopMatrix( u32 param )
{
	switch (param) {
	case 0: // modelview
		if (gSP.matrix.modelViewi > 0) {
			gSP.matrix.modelViewi--;

			gSP.changed |= CHANGED_MATRIX | CHANGED_LIGHT | CHANGED_LOOKAT;
		}
	break;
	case 1: // projection, can't
	break;
	default:
		break;
	}
}

void gSPSegment( s32 seg, s32 base )
{
	gSP.segment[seg] = base;

}

void gSPRelSegment( s32 seg, s32 base )
{
	// F3DEX3 G_RELSEGMENT: like G_MW_SEGMENT (gSPSegment) but the address
	// is encoded as 0x0?123456 where the upper nibble selects an existing
	// segment to add to the 24-bit offset.
	const s32 offset = base & 0x00FFFFFF;
	const s32 rel    = (base >> 24) & 0xf;
	gSP.segment[seg] = offset + gSP.segment[rel];

}

void gSPClipRatio(u32 ratio)
{
	gSP.clipRatio = std::abs(static_cast<s16>(ratio & 0xFFFF));
}

void gSPInsertMatrix( u32 where, u32 num )
{

	if ((where & 0x3) != 0)
		return;

	f32 * pMtx = nullptr;
	u16 addr = (where + 0x80) & 0xFFFF;
	if (addr < 0x40) {
		pMtx = reinterpret_cast<f32*>(gSP.matrix.modelView[gSP.matrix.modelViewi]);
	} else if (addr < 0x80) {
		pMtx = reinterpret_cast<f32*>(gSP.matrix.projection);
		addr -= 0x40;
	} else if (addr < 0xC0) {
		pMtx = reinterpret_cast<f32*>(gSP.matrix.combined);
		addr -= 0x80;
	} else
		return;

	const u16 * pData = reinterpret_cast<u16*>(&num);
	const u32 index = (addr < 0x20) ? (addr >> 1) : ((addr - 0x20) >> 1);
	for (u32 i = 0; i < 2; i++) {
		if (addr < 0x20) {
			// integer elements of the matrix to be changed
			const s16 integer = static_cast<s16>(pData[i ^ 1]);
			const u16 fract = GetIntMatrixElement(pMtx[index + i]).second;
			pMtx[index + i] = GetFloatMatrixElement(integer, fract);
		} else {
			// fractional elements of the matrix to be changed
			const s16 integer = GetIntMatrixElement(pMtx[index + i]).first;
			const u16 fract = pData[i ^ 1];
			pMtx[index + i] = GetFloatMatrixElement(integer, fract);
		}
	}
}

void gSPModifyVertex( u32 _vtx, u32 _where, u32 _val )
{
	GraphicsDrawer & drawer = dwnd().getDrawer();

	SPVertex & vtx0 = drawer.getVertex(_vtx);
	switch (_where) {
		case G_MWO_POINT_RGBA:
			vtx0.r = _FIXED2FLOATCOLOR(_SHIFTR( _val, 24, 8 ),8);
			vtx0.g = _FIXED2FLOATCOLOR(_SHIFTR( _val, 16, 8 ),8);
			vtx0.b = _FIXED2FLOATCOLOR(_SHIFTR( _val, 8, 8 ),8);
			vtx0.a = _FIXED2FLOATCOLOR(_SHIFTR( _val, 0, 8 ),8);
			vtx0.modify |= MODIFY_RGBA;
			break;
		case G_MWO_POINT_ST:
			vtx0.s = _FIXED2FLOAT( (s16)_SHIFTR( _val, 16, 16 ), 5 ) / gSP.texture.scales;
			vtx0.t = _FIXED2FLOAT((s16)_SHIFTR(_val, 0, 16), 5) / gSP.texture.scalet;
			//vtx0.modify |= MODIFY_ST; // still neeed to divide by 2 in vertex shader if TexturePersp disabled
			break;
		case G_MWO_POINT_XYSCREEN:
			vtx0.x = _FIXED2FLOAT((s16)_SHIFTR(_val, 16, 16), 2);
			vtx0.y = _FIXED2FLOAT((s16)_SHIFTR(_val, 0, 16), 2);
			if ((config.generalEmulation.hacks & hack_ModifyVertexXyInShader) == 0) {
				vtx0.x = (vtx0.x - gSP.viewport.vtrans[0]) / gSP.viewport.vscale[0];
				if (gSP.viewport.vscale[0] < 0)
					vtx0.x = -vtx0.x;
				vtx0.x *= vtx0.w;

				if (dwnd().isAdjustScreen()) {
					const f32 adjustScale = dwnd().getAdjustScale();
					vtx0.x *= adjustScale;
					if (gSP.matrix.projection[3][2] == -1.f)
						vtx0.w *= adjustScale;
				}

				vtx0.y = -(vtx0.y - gSP.viewport.vtrans[1]) / gSP.viewport.vscale[1];
				if (gSP.viewport.vscale[1] < 0)
					vtx0.y = -vtx0.y;
				vtx0.y *= vtx0.w;
			} else {
				vtx0.modify |= MODIFY_XY;
				if (vtx0.w == 0.0f) {
					vtx0.w = 1.0f;
					vtx0.clip &= ~(CLIP_W);
				}
			}
			vtx0.clip &= ~(CLIP_POSX | CLIP_NEGX | CLIP_POSY | CLIP_NEGY);
		break;
		case G_MWO_POINT_ZSCREEN:
		{
			// All 32 bits of _val are the z value (16.11)
			f32 scrZ = float(_val) / 65535.0f / 2048.0f;
			vtx0.z = scrZ;
			vtx0.clip &= ~CLIP_W;
			vtx0.modify |= MODIFY_Z;
		}
		break;
	}
}

void gSPNumLights( s32 n )
{
	if (n < 12) {
		gSP.numLights = n;
		gSP.changed |= CHANGED_LIGHT;
	} else {
	}

}

void gSPLightColor( u32 lightNum, u32 packedColor )
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

void gSPFogFactor( s16 fm, s16 fo )
{
	gSP.fog.multiplier = fm;
	gSP.fog.offset = fo;
	gSP.fog.multiplierf = _FIXED2FLOAT(fm, 8);
	gSP.fog.offsetf = _FIXED2FLOAT(fo, 8);

	gSP.changed |= CHANGED_FOGPOSITION;
}

void gSPPerspNormalize( u16 scale )
{
}

extern "C" uint32_t LegacySm64ToolsHacks;
void gSPTexture( f32 sc, f32 tc, u32 level, u32 tile, u32 on )
{
	gSP.texture.on = on;
	if (on == 0) {
		if (LegacySm64ToolsHacks)
			gDPSetCombine(0xffffff, 0xFFFE793C);

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

void gSPEndDisplayList()
{
	if (RSP.PCi > 0)
		--RSP.PCi;
	else {
		RSP.halt = true;
	}

}

void gSPGeometryMode( u32 clear, u32 set )
{
	gSP.geometryMode = (gSP.geometryMode & ~clear) | set;

	gSP.changed |= CHANGED_GEOMETRYMODE;

}

void gSPSetGeometryMode( u32 mode )
{
	gSP.geometryMode |= mode;

	gSP.changed |= CHANGED_GEOMETRYMODE;

}

void gSPClearGeometryMode( u32 mode )
{
	gSP.geometryMode &= ~mode;

	gSP.changed |= CHANGED_GEOMETRYMODE;

}

void gSPSetOtherMode_H(u32 _length, u32 _shift, u32 _data)
{
	const u32 mask = (((u64)1 << _length) - 1) << _shift;
	gDP.otherMode.h = (gDP.otherMode.h&(~mask)) | _data;

	if (mask & 0x00300000)  // cycle type
		gDP.changed |= CHANGED_CYCLETYPE;

}

void gSPSetOtherMode_L(u32 _length, u32 _shift, u32 _data)
{
	if (LegacySm64ToolsHacks)
	{
		// Typo fixrefix
		// !!! This is very cheesy fix
		const u32 maskH = (((u64)1 << 2) - 1) << 0x14;
		if (!(gDP.otherMode.h & maskH) && _data == 0xC8113078)
		{
			_data = 0x00443078;
		}
	}

	const u32 mask = (((u64)1 << _length) - 1) << _shift;
	gDP.otherMode.l = (gDP.otherMode.l&(~mask)) | _data;

	if (mask & 0x00000003)  // alpha compare
		gDP.changed |= CHANGED_ALPHACOMPARE;

	if (mask & 0xFFFFFFF8)  // rendermode / blender bits
		gDP.changed |= CHANGED_RENDERMODE;

}

void gSPLine3D(u32 v0, u32 v1, s32 wd, u32 flag)
{
	dwnd().getDrawer().drawLine(v0, v1, 1.5f + wd * 0.5f, flag);

}

void gSPSetStatus(u32 sid, u32 val)
{
	assert(sid <= 12);
	gSP.status[sid>>2] = val;

}

struct uSprite {
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
};    /* 24 bytes */

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
		FrameBuffer *pBuffer = frameBufferList().findBuffer(gSP.bgImage.address);
		if (pBuffer != nullptr) {
			if (pBuffer->m_cfb) {
				frameBufferList().removeBuffer(pBuffer->m_startAddress);
				return;
			}
			gDP.tiles[0].frameBufferAddress = pBuffer->m_startAddress;
			gDP.tiles[0].textureMode = TEXTUREMODE_FRAMEBUFFER_BG;
			gDP.tiles[0].loadType = LOADTYPE_TILE;
			gDP.changed |= CHANGED_TMEM;
		}
	}
}

void gSPSprite2DBase(u32 _base)
{
	assert(RSP.nextCmd == 0xBE);
	const u32 address = RSP_SegmentToPhysical( _base );
	uSprite *pSprite = (uSprite*)&RDRAM[address];

	if (pSprite->tlutPtr != 0) {
		gDPSetTextureImage( G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, pSprite->tlutPtr );
		gDPSetTile( G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 256, 7, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, 0, 0, 0, 0 );
		gDPLoadTLUT( 7, 0, 0, 1020, 0 );

		if (pSprite->imageFmt != G_IM_FMT_RGBA)
			gDP.otherMode.textureLUT = G_TT_RGBA16;
		else
			gDP.otherMode.textureLUT = G_TT_NONE;
	} else
		gDP.otherMode.textureLUT = G_TT_NONE;

	_loadSpriteImage(pSprite);
	gSPTexture( 1.0f, 1.0f, 0, 0, TRUE );
	gDP.otherMode.texturePersp = 1;

	const f32 z = (gDP.otherMode.depthSource == G_ZS_PRIM) ? gDP.primDepth.z : gSP.viewport.nearz;
	const f32 w = 1.0f;

	f32 scaleX = 1.0f, scaleY = 1.0f;
	u32 flipX = 0, flipY = 0;
	do {
		u32 w0 = *(u32*)&RDRAM[RSP.PC[RSP.PCi]];
		u32 w1 = *(u32*)&RDRAM[RSP.PC[RSP.PCi] + 4];
		RSP.cmd = _SHIFTR( w0, 24, 8 );

		RSP.PC[RSP.PCi] += 8;
		RSP.nextCmd = _SHIFTR( *(u32*)&RDRAM[RSP.PC[RSP.PCi]], 24, 8 );

		if ( RSP.cmd == 0xBE ) { // gSPSprite2DScaleFlip
			scaleX  = _FIXED2FLOAT( _SHIFTR(w1, 16, 16), 10 );
			scaleY  = _FIXED2FLOAT( _SHIFTR(w1,  0, 16), 10 );
			flipX = _SHIFTR(w0, 8, 8);
			flipY = _SHIFTR(w0, 0, 8);
			continue;
		}
		// gSPSprite2DDraw
		const f32 frameX = _FIXED2FLOAT(((s16)_SHIFTR(w1, 16, 16)), 2);
		const f32 frameY = _FIXED2FLOAT(((s16)_SHIFTR(w1,  0, 16)), 2);
		const f32 frameW = pSprite->imageW / scaleX;
		const f32 frameH = pSprite->imageH / scaleY;

		f32 ulx, uly, lrx, lry;
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

		f32 uls = pSprite->imageX;
		f32 ult = pSprite->imageY;
		f32 lrs = uls + pSprite->imageW - 1;
		f32 lrt = ult + pSprite->imageH - 1;

		// Hack for WCW Nitro.
		if ((config.generalEmulation.hacks & hack_WCWNitro) != 0) {
			const u32 scale = u32(scaleY);
			gSP.bgImage.height /= scale;
			gSP.bgImage.imageY /= scale;
			ult /= scaleY;
			lrt /= scaleY;
			gSP.bgImage.width *= scale;
		}

		GraphicsDrawer & drawer = dwnd().getDrawer();
		drawer.setDMAVerticesSize(4);
		SPVertex * pVtx = drawer.getDMAVerticesData();

		SPVertex & vtx0 = pVtx[0];
		vtx0.x = ulx;
		vtx0.y = uly;
		vtx0.z = z;
		vtx0.w = w;
		vtx0.s = uls;
		vtx0.t = ult;
		SPVertex & vtx1 = pVtx[1];
		vtx1.x = lrx;
		vtx1.y = uly;
		vtx1.z = z;
		vtx1.w = w;
		vtx1.s = lrs;
		vtx1.t = ult;
		SPVertex & vtx2 = pVtx[2];
		vtx2.x = ulx;
		vtx2.y = lry;
		vtx2.z = z;
		vtx2.w = w;
		vtx2.s = uls;
		vtx2.t = lrt;
		SPVertex & vtx3 = pVtx[3];
		vtx3.x = lrx;
		vtx3.y = lry;
		vtx3.z = z;
		vtx3.w = w;
		vtx3.s = lrs;
		vtx3.t = lrt;

		if (pSprite->stride > 0)
			drawer.drawScreenSpaceTriangle(4);
	} while (RSP.nextCmd == 0xBD || RSP.nextCmd == 0xBE);
}

#ifndef __NEON_OPT
void(*gSPInverseTransformVector)(float vec[3], float mtx[4][4]) = gSPInverseTransformVector_default;
void(*gSPTransformVector)(float vtx[4], float mtx[4][4]) = gSPTransformVector_default;
#else
void gSPInverseTransformVector_NEON(float vec[3], float mtx[4][4]);
void gSPTransformVector_NEON(float vtx[4], float mtx[4][4]);
void(*gSPInverseTransformVector)(float vec[3], float mtx[4][4]) = gSPInverseTransformVector_NEON;
void(*gSPTransformVector)(float vtx[4], float mtx[4][4]) = gSPTransformVector_NEON;
#endif //__NEON_OPT

void gSPSetupFunctions()
{
	g_ConkerUcode = GBI.getMicrocodeType() == F3DEX2CBFD;
}
