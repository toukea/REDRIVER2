#ifndef PSX
#include "driver2.h"
#include "vrview.h"
#include "camera.h"
#include "convert.h"
#include "draw.h"
#include "cars.h"
#include "players.h"
#include "mission.h"
#include "main.h"
#include "cutscene.h"
#include "event.h"
#include "overmap.h"
#include "system.h"
#include "cockpit.h"
#include "../../utils/vr_openxr.h"
#include "PsyX/PsyX_globals.h"

extern int gLoadedOverlay;

namespace {
bool trackedFrame = false;
int savedScrZ = 256;

int toFixed(float v) { return int(std::lround(v * 4096.f)); }
}

bool VRView_Tracked() { return trackedFrame; }

int VRView_FrAng() {
	// The generation frustum spans +-256/scr_z horizontally (512-unit wide GTE screen margin
	// included); keep a margin for the head turning during one 30 Hz step and for pitch.
	int a = ratan2(256, VR_ScrZ());
	return MIN(1000, a + 64);
}

bool VRView_Apply(int view)
{
	trackedFrame = false;
	if (!VR_Active() || NumPlayers != 1)
		return false;

	PLAYER& lp = player[view];
	const bool inside = CameraViewIsInside(lp.cameraView);
	const bool chase = lp.cameraView == 0;

	// Director, cutscene and event cameras stay on the virtual cinema screen.
	if (!(inside || chase) || events.cameraEvent || gInGameCutsceneActive)
		return false;

	CAR_DATA* car = lp.cameraCarId >= 0 && lp.cameraCarId < MAX_CARS ? &car_data[lp.cameraCarId] : NULL;
	if (car && !car->ap.carCos)
		car = NULL;

	const float upm = VR_UnitsPerMetre(car ? Cockpit_UnitsPerMetre(car) : 0.f);
	VRHead head;
	if (!VR_BeginTrackedFrame(upm, &head))
		return false;

	// World -> camera rotation of the game, without the PSX aspect scaling.
	vrm::Mat3 w;
	for (int i = 0; i < 3; i++)
	{
		const float a = MAX(1, aspect.m[i][i]) / 4096.f;
		for (int j = 0; j < 3; j++)
			w.m[i][j] = inv_camera_matrix.m[i][j] / 4096.f / a;
	}
	w = vrm::orthonormalRows(w);

	// Inside the car the body roll and pitch can be removed from the view (horizon lock):
	// the cockpit then moves around a level head, which is far easier on the inner ear.
	vrm::Mat3 base = w;
	const float lock = inside ? VR_HorizonLock() : 0.f;
	if (lock > 0.f)
		base = vrm::toMat3(vrm::slerp(vrm::fromMat3(w), vrm::fromMat3(vrm::levelWorldToCamera(w)), lock));

	const vrm::Mat3 headed = vrm::headWorldToCamera(base, head.q);

	// Eye position: tracked head offset plus the seat adjustment (both in the base frame).
	vrm::Vec3 seat = inside ? VR_SeatOffset() : vrm::Vec3();
	vrm::Vec3 offset = vrm::trackingToGame(head.p + vrm::Vec3(seat.x, seat.y, -seat.z)) * upm;
	vrm::Vec3 world = vrm::mul(vrm::transpose(base), offset);
	camera_position.vx += int(std::lround(world.x));
	camera_position.vy += int(std::lround(world.y));
	camera_position.vz += int(std::lround(world.z));

	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			inv_camera_matrix.m[i][j] = toFixed(headed.m[i][j] * aspect.m[i][i] / 4096.f);
	InvertMatrix(&inv_camera_matrix, &camera_matrix);

	camera_angle.vy = int(std::lround(vrm::frustumYaw(headed) * 2048.f / 3.14159265f)) & 4095;

	face_camera_work.m[0][0] = ONE;
	face_camera_work.m[0][2] = 0;
	face_camera_work.m[2][0] = 0;
	face_camera_work.m[2][2] = ONE;
	RotMatrixY(-(camera_angle.vy & 0xfff), &face_camera_work);
	MulMatrix0(&inv_camera_matrix, &face_camera_work, &face_camera);

	// Wide generation frustum: everything a turning head may see within one step is drawn.
	savedScrZ = scr_z;
	SetGeomScreen(scr_z = VR_ScrZ());

	// Minimap region of the 2D overlay (window NDC), redirected to the dashboard screen.
	if (gLoadedOverlay && VR_MapOnDashboard())
	{
		const float aspectRatio = float(g_windowWidth) / float(MAX(1, g_windowHeight));
		const float x0 = float(gMapXOffset - 2), x1 = float(gMapXOffset + MAP_SIZE_W + 2);
		const float y0 = float(gMapYOffset - 2), y1 = float(gMapYOffset + MAP_SIZE_H + 2);
		const float sx = 1.f / (0.375f * aspectRatio);
		float rect[4] = {
			(x0 / 320.f - .5f) * sx, -2.f * (y1 / float(SCREEN_H) - .5f),
			(x1 / 320.f - .5f) * sx, -2.f * (y0 / float(SCREEN_H) - .5f) };
		Cockpit_SetVRMapRect(rect);
	}

	trackedFrame = true;
	return true;
}

void VRView_Restore()
{
	if (!trackedFrame)
		return;
	SetGeomScreen(scr_z = savedScrZ);
}
#endif
