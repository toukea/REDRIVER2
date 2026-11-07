#pragma once
#ifndef PSX
#include "wheel_model.h"
struct ini_t;
struct WheelSettings {
    int enabled = 1;
    int device = -1, pedals = -1, shifter = -1, handbrakeDevice = -1;
    char guid[64] = {}, pedalsGuid[64] = {}, shifterGuid[64] = {}, handbrakeGuid[64] = {};
    racing::Axis axes[5] = { racing::Axis(0), racing::Axis(2,true), racing::Axis(3,true), racing::Axis(1,true), racing::Axis(-1) };
    int gearButtons[6] = {12,13,14,15,16,17};
    int handbrakeButton = 2;
    float rotation = 900, hardwareRotation = 900, steeringLock = 35;
    float gain = .35f, damping = .08f, road = .12f, filter = .35f;
    int forceInvert = 0;
    racing::Setup transmission;
};
extern WheelSettings g_wheelSettings;
void Wheel_Init();
void Wheel_Shutdown();
void Wheel_Poll(bool driving);
bool Wheel_Read(racing::Input& input);
bool Wheel_SampleDevices(racing::Input& input); // Read-only diagnostics, no focus requirement.
unsigned Wheel_Buttons(unsigned& edges);
void Wheel_Force(float aligning, float road);
void Wheel_Stop();
void Wheel_LoadConfig(const char* path);
bool Wheel_SaveConfig(const char* path);
int Wheel_Configure(); // Standalone SDL calibration UI; no game assets needed.
#endif
