#pragma once
#ifndef PSX
#include "../../utils/cockpit_model.h"
// Include after driver2.h; no additions to original PSX/replay structures.
void Cockpit_LoadConfig();
bool Cockpit_Enabled();
bool Cockpit_StartInside();
int Cockpit_ViewPitch();
cockpit::Profile Cockpit_Profile(const CAR_DATA* car);
void Cockpit_EyeOffset(const CAR_DATA* car, VECTOR* offset);
void Cockpit_Draw(int view);
float Cockpit_UnitsPerMetre(const CAR_DATA* car); // imported cabin scale, 0 without one
void Cockpit_SetVRMapRect(const float rect[4]);     // [VR] minimap region to show on the dash screen
// Body triangles in front of the windscreen (bonnet, wing tops) to draw from the cockpit.
// Fills pGT3/numGT3 only; false when disabled (trucks by default) or the model is unknown.
bool Cockpit_HoodModel(const CAR_DATA* car, CAR_MODEL* out);
#endif
