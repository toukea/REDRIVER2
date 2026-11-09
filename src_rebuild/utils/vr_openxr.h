#pragma once
#ifndef PSX
// OpenXR virtual reality mode (PC). The simulation keeps its 30 Hz step; every frame the
// game draws is replayed for both eyes at the headset rate with the latest head pose.
#include "vr_math.h"

struct VRHead { vrm::Quat q; vrm::Vec3 p; };   // head relative to the recentred origin (tracking axes, metres)

// Before PsyX_Initialise: reads vr.ini and the command line (-vr / -flat), shows the launcher.
void VR_Configure(int argc, char** argv);
// After the OpenGL context exists: starts the headset session when VR was chosen.
void VR_Init();
void VR_Shutdown();

bool VR_Requested();       // the player chose VR at launch (or toggled it on)
bool VR_Active();          // a headset session is running: the game camera should be head-tracked
bool VR_PacesGame();       // the headset frame loop paces the 30 Hz simulation
bool VR_InputFocused();    // the headset has input focus (the desktop window may not)

// Called by the game camera once per drawn frame. Returns false when the frame is not head-tracked.
bool VR_BeginTrackedFrame(float unitsPerMetre, VRHead* head);
float VR_UnitsPerMetre(float cockpitUnitsPerMetre);   // game units per real metre (with world_scale)
int VR_ScrZ();             // GTE projection distance used while head-tracked (wide culling frustum)
float VR_HorizonLock();    // 0 = view rolls with the car, 1 = horizon stays level
vrm::Vec3 VR_SeatOffset(); // metres: x right, y up, z forward
bool VR_HideWheel();       // hide the virtual steering wheel and arms (a real wheel is in your hands)
bool VR_MapOnDashboard();
// Minimap screen: rect = window NDC (xmin,ymin,xmax,ymax); centre/halfRight/halfUp in game camera units.
void VR_SetMapScreen(const float rect[4], vrm::Vec3 centre, vrm::Vec3 halfRight, vrm::Vec3 halfUp);
#endif
