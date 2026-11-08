#pragma once
#ifndef PSX
#include "../../utils/cockpit_model.h"
// Include after driver2.h; no additions to original PSX/replay structures.
void Cockpit_LoadConfig();
bool Cockpit_Enabled();
int Cockpit_ViewPitch();
cockpit::Profile Cockpit_Profile(const CAR_DATA* car);
void Cockpit_EyeOffset(const CAR_DATA* car, VECTOR* offset);
void Cockpit_Draw(int view);
#endif
