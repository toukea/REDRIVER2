#ifndef PSX
#include "driver2.h"
#include "wheel.h"
#include "../../utils/wheel.h"
#include "players.h"
#include "main.h"
#include "mission.h"
#include "glaunch.h"
#include "cutscene.h"
#include "camera.h"
#include "pause.h"
#include "pres.h"
#include "cars.h"
#include "handling.h"
#include "system.h"
#include <vector>
#include <cstdint>
#include <cstring>
#include <string>

namespace {
// The sidecar records the final controls so playback does not depend on connected
// hardware, calibration, clutch mode, or later transmission configuration changes.
struct Frame {
    int32_t frame=0, car=-1;
    int16_t steer=0, thrust=0, rpm=0, gear=0;
    uint16_t throttle=0, brake=0, clutch=0, handbrake=0;
    uint16_t flags=0, reserved=0; // 1 active, 2 burnout, 4 stalled, 8 rejected shift
};
static_assert(sizeof(Frame)==28,"Wheel replay layout must be stable");
Frame state[MAX_CARS];
racing::Transmission transmissions[MAX_CARS];
std::vector<Frame> frames;
bool overflow=false;
float aligning=0, bumps=0;
const size_t frameLimit=216000; // Two hours at the game's 30 Hz simulation rate.
unsigned checksum(const void* data,int size) {
    const unsigned char* p=static_cast<const unsigned char*>(data);unsigned hash=2166136261u;
    for(int i=0;i<size;++i)hash=(hash^p[i])*16777619u;return hash;
}
bool validCar(const CAR_DATA* cp){return cp && cp->id>=0 && cp->id<MAX_CARS;}
void apply(CAR_DATA* cp,const Frame& f){
    cp->wheel_angle=f.steer;cp->thrust=f.thrust;cp->hd.autoBrake=0;
    cp->handbrake=0; // Applied progressively at the wheels instead of the legacy lock.
    cp->wheelspin=(f.flags&2)!=0;cp->hd.revs=f.rpm;cp->hd.gear=0;cp->hd.changingGear=0;
}
}
void Wheel_GameReset(int car){if(car>=0 && car<MAX_CARS){state[car]=Frame();transmissions[car].reset();}}
bool Wheel_GameActive(const CAR_DATA* cp){return validCar(cp) && (state[cp->id].flags&1)!=0;}
float Wheel_GameBrake(const CAR_DATA* cp){return Wheel_GameActive(cp)?state[cp->id].brake/65535.f:0;}
float Wheel_GameHandbrake(const CAR_DATA* cp){return Wheel_GameActive(cp)?state[cp->id].handbrake/65535.f:0;}
int Wheel_GameGear(const CAR_DATA* cp){return Wheel_GameActive(cp)?state[cp->id].gear:0;}
int Wheel_GameRPM(const CAR_DATA* cp){return Wheel_GameActive(cp)?state[cp->id].rpm:0;}
void Wheel_GameApply(CAR_DATA* cp){
    if(!validCar(cp))return;
    Frame& f=state[cp->id];bool wasActive=(f.flags&1)!=0;f.flags=0;
    if(cp->controlType!=CONTROL_TYPE_PLAYER || GetPlayerId(cp)!=0)return;
    if(NoPlayerControl){
        if(CameraCnt>=0 && size_t(CameraCnt)<frames.size() && frames[CameraCnt].frame==CameraCnt && frames[CameraCnt].car==cp->id){f=frames[CameraCnt];if(f.flags&1)apply(cp,f);}
        return;
    }
    if(gStopPadReads || gCantDrive || MaxPlayerDamage[0]<=cp->totalDamage || gInGameCutsceneActive){Wheel_Stop();return;}
    racing::Input input;
    if(!Wheel_Read(input)){
        if(!wasActive)return;
        input.brake=1; // Focus loss or disconnection: release drive and stop the car.
    }
    // Driver units are approximately centimetres, velocities are units per frame.
    float speedMS=cp->hd.wheel_speed/4096.f*.3f;
    auto& transmission=transmissions[cp->id];
    auto output=transmission.step(input,speedMS,1.f/30,g_wheelSettings.transmission);
    f=Frame();f.frame=CameraCnt;f.car=cp->id;
    f.steer=static_cast<int16_t>(input.steering*racing::clamp(g_wheelSettings.steeringLock,10,45)*4096/360);
    float drive=output.drive;
    if(std::fabs(speedMS)>.2f)drive-=output.engineBrake*(speedMS>0?1:-1);
    f.thrust=static_cast<int16_t>(racing::clamp(cp->ap.carCos->powerRatio/4096.f*4915*drive,-15000,15000));
    f.rpm=static_cast<int16_t>(racing::clamp(output.rpm/6500*30000,0,32000));
    f.gear=output.gear;
    f.throttle=static_cast<uint16_t>(input.throttle*65535);f.brake=static_cast<uint16_t>(input.brake*65535);
    f.clutch=static_cast<uint16_t>(input.clutch*65535);f.handbrake=static_cast<uint16_t>(input.handbrake*65535);
    f.flags=1|(output.burnout?2:0)|(output.stalled?4:0)|(output.rejectedShift?8:0);
    apply(cp,f);
    if(CameraCnt>=0 && size_t(CameraCnt)<frameLimit){if(frames.size()<=size_t(CameraCnt))frames.resize(CameraCnt+1);frames[CameraCnt]=f;}
    else overflow=true;
}
void Wheel_GameContact(CAR_DATA* cp,int wheel,int lateral,int bump,int surface){
    if(!Wheel_GameActive(cp) || NoPlayerControl || GetPlayerId(cp)!=0)return;
    if((wheel&1)==0){
        // Approximate aligning moment from the actual front tyre lateral forces.
        aligning+=racing::clamp(lateral/16000.f,-.5f,.5f);
        bumps+=racing::clamp(bump/120.f,-.25f,.25f);
        if(surface&0x80)bumps+=((CameraCnt&1)?1:-1)*.08f;
    }
}
void Wheel_GameFeedback(){
    if(!pauseflag && !NoPlayerControl && MainPlayer.playerType==PLAYER_TYPE_CAR && MainPlayer.playerCarId>=0 && MainPlayer.playerCarId<MAX_CARS && Wheel_GameActive(&car_data[MainPlayer.playerCarId])) Wheel_Force(aligning,bumps);
    else Wheel_Stop();
    aligning=0;bumps=0;
}
void Wheel_GameHUD(){
    int id=MainPlayer.playerCarId;if(id<0 || id>=MAX_CARS || !Wheel_GameActive(&car_data[id]))return;
    const Frame& f=state[id];char label[80];char gear[8];
    if(f.gear<0)strcpy(gear,"R");else if(!f.gear)strcpy(gear,"N");else snprintf(gear,sizeof(gear),"%d",f.gear);
    snprintf(label,sizeof(label),"%s  %d RPM%s",gear,int(f.rpm*6500/30000),(f.flags&4)?" STALLED":(f.flags&8)?" CLUTCH!":"");
    PrintString(label,16,SCREEN_H-28);
}
void Wheel_ReplayReset(){frames.clear();overflow=false;for(int i=0;i<MAX_CARS;++i)Wheel_GameReset(i);}
bool Wheel_ReplayHasFrames(){return !frames.empty();}
bool Wheel_ReplaySave(const char* path,const void* base,int size){
    if(overflow){printError("Wheel replay exceeds two hours; cannot save accurately\n");return false;}
    if(frames.empty())return true;
    std::string name=std::string(path)+".wheel";
    FILE* file=fopen(name.c_str(),"wb");if(!file)return false;
    const uint32_t header[]={0x314c4857,1,checksum(base,size),static_cast<uint32_t>(frames.size()),sizeof(Frame)};
    bool ok=fwrite(header,sizeof(header),1,file)==1;
    if(!frames.empty())ok=fwrite(frames.data(),sizeof(Frame),frames.size(),file)==frames.size() && ok;
    if(fclose(file))ok=false;return ok;
}
bool Wheel_ReplayLoad(const char* path,const void* base,int size){
    Wheel_ReplayReset();
    uint32_t magic=0;if(size>=4)memcpy(&magic,base,4);
    if(magic!=WHEEL_REPLAY_MAGIC)return true; // Ignore stale companions of legacy replays.
    std::string name=std::string(path)+".wheel";FILE* file=fopen(name.c_str(),"rb");
    if(!file){printError("Missing required wheel replay: %s\n",name.c_str());return false;}
    uint32_t h[5]={};bool ok=fread(h,sizeof(h),1,file)==1 && h[0]==0x314c4857 && h[1]==1 && h[2]==checksum(base,size) && h[3]<=frameLimit && h[4]==sizeof(Frame);
    if(ok){frames.resize(h[3]);ok=frames.empty()||fread(frames.data(),sizeof(Frame),frames.size(),file)==frames.size();}
    fclose(file);
    if(ok)for(size_t i=0;i<frames.size();++i){const Frame& f=frames[i];if(f.flags&1)if(f.frame!=int(i)||f.car<0||f.car>=MAX_CARS||f.gear<-1||f.gear>5||std::abs(int(f.steer))>512||std::abs(int(f.thrust))>15000||f.rpm<0||f.rpm>32000){ok=false;break;}}
    if(!ok){Wheel_ReplayReset();printError("Invalid wheel replay sidecar: %s\n",name.c_str());}return ok;
}
#endif
