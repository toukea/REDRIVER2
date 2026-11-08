#pragma once
#ifndef PSX
#define WHEEL_REPLAY_MAGIC 0x574c5250
// Include after driver2.h. Host-only state never changes the PSX data layouts.
void Wheel_GameReset(int car);
void Wheel_GameApply(CAR_DATA* car);
bool Wheel_GameActive(const CAR_DATA* car);
float Wheel_GameBrake(const CAR_DATA* car);
float Wheel_GameHandbrake(const CAR_DATA* car);
int Wheel_GameGear(const CAR_DATA* car);
int Wheel_GameRPM(const CAR_DATA* car);
float Wheel_GameSteeringDegrees(const CAR_DATA* car);
// slide: lateral slip, +-12500 at the friction limit. load: front grip relative
// to the car's coasting value, 4096 = nominal. locked: wheel skidding.
void Wheel_GameContact(CAR_DATA* car,int wheel,int slide,int load,int locked,int bump,int surface);
void Wheel_GameFeedback();
void Wheel_GameHUD();
void Wheel_ReplayReset();
bool Wheel_ReplayHasFrames();
bool Wheel_ReplaySave(const char* path,const void* base,int size);
bool Wheel_ReplayLoad(const char* path,const void* base,int size);
#endif
