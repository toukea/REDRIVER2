#pragma once
#ifndef PSX
#include "wheel_model.h"
struct ini_t;
struct WheelSettings {
    int enabled = 1;
    int device = -1, pedals = -1, shifter = -1, handbrakeDevice = -1;
    char guid[64] = {}, pedalsGuid[64] = {}, shifterGuid[64] = {}, handbrakeGuid[64] = {};
    racing::Axis axes[5] = { racing::Axis(0), racing::Axis(1,true), racing::Axis(2,true), racing::Axis(3,true), racing::Axis(-1) };
    int gearButtons[6] = {12,13,14,15,16,17};
    int handbrakeButton = 2;
    float rotation = 900, hardwareRotation = 900, steeringLock = 35;
    float handbrakeOn = .30f, handbrakeOff = .20f;
    racing::ForceSetup force;
    int forceInvert = 0;
    int ffbHud = 1, ffbLog = 0; // on-screen force meter, CSV telemetry for tuning
    racing::Setup transmission;
};
extern WheelSettings g_wheelSettings;
void Wheel_Init();
void Wheel_Shutdown();
void Wheel_Poll(bool driving);
bool Wheel_Read(racing::Input& input);
bool Wheel_SampleDevices(racing::Input& input); // Read-only diagnostics, no focus requirement.
unsigned Wheel_Buttons(unsigned& edges);
bool Wheel_RawButton(int index);           // raw device button (VR recentre binding)
racing::ForceReport Wheel_Force(const racing::ForceFrame& frame);
void Wheel_Stop();
void Wheel_LoadConfig(const char* path);
bool Wheel_SaveConfig(const char* path);
int Wheel_Configure(const char* configPath = "wheel.ini"); // Standalone SDL calibration UI.
#endif
