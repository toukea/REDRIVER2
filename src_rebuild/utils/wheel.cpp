#include "wheel.h"
#include "ini.h"
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <string>

WheelSettings g_wheelSettings;
namespace {
SDL_Joystick* devices[4] = {};
SDL_Haptic* haptic = nullptr;
int effectId = -1;
SDL_HapticEffect effect = {};
bool active = false, initialized = false;
Uint32 lastScan = 0;
float filteredForce = 0, previousSteer = 0;
bool forcePrimed = false;
const char* axisNames[5] = {"steering", "throttle", "brake", "clutch", "handbrake"};
const char* guidNames[4] = {"guid", "pedals_guid", "shifter_guid", "handbrake_guid"};
int* deviceSetting(int i) {
    int* indices[] = {&g_wheelSettings.device,&g_wheelSettings.pedals,&g_wheelSettings.shifter,&g_wheelSettings.handbrakeDevice};
    return indices[i];
}
char* guidSetting(int i) {
    char* guids[] = {g_wheelSettings.guid,g_wheelSettings.pedalsGuid,g_wheelSettings.shifterGuid,g_wheelSettings.handbrakeGuid};
    return guids[i];
}
void closeDevices() {
    Wheel_Stop();
    if (haptic) { if (effectId >= 0) SDL_HapticDestroyEffect(haptic,effectId); SDL_HapticClose(haptic); }
    haptic = nullptr; effectId = -1;
    for (auto& device : devices) { if (device) SDL_JoystickClose(device); device = nullptr; }
}
int findDevice(int group) {
    const char* wanted = guidSetting(group);
    int chosen = *deviceSetting(group);
    for (int i=0; i<SDL_NumJoysticks(); ++i) {
        char guid[64]; SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i),guid,sizeof(guid));
        if (*wanted) { if (!strcmp(guid,wanted)) return i; }
        else if (chosen >= 0) { if (i == chosen) return i; }
        else if (group == 0) {
            // Default only to a recognized wheel, never a normal gamepad.
            const char* name = SDL_JoystickNameForIndex(i);
            if (name && (strstr(name,"G29") || strstr(name,"G920") || strstr(name,"G923"))) return i;
        }
    }
    return group && !*wanted && chosen < 0 ? -2 : -1;
}
void scan() {
    closeDevices();
    for (int group=0; group<4; ++group) {
        int index = findDevice(group);
        if (index >= 0) devices[group] = SDL_JoystickOpen(index);
        else if (index == -2 && devices[0]) {
            int base = findDevice(0); if (base >= 0) devices[group] = SDL_JoystickOpen(base);
        }
    }
    if (!devices[0]) return;
    SDL_Log("Wheel: %s, %d axes, %d buttons",SDL_JoystickName(devices[0]),SDL_JoystickNumAxes(devices[0]),SDL_JoystickNumButtons(devices[0]));
    haptic = SDL_HapticOpenFromJoystick(devices[0]);
    if (!haptic) { SDL_Log("Wheel FFB unavailable: %s",SDL_GetError()); return; }
    if (!(SDL_HapticQuery(haptic) & SDL_HAPTIC_CONSTANT)) {
        SDL_Log("Wheel does not expose constant-force effects"); SDL_HapticClose(haptic); haptic=nullptr; return;
    }
    SDL_HapticSetAutocenter(haptic,0);
    effect = SDL_HapticEffect();
    effect.type = SDL_HAPTIC_CONSTANT;
    effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
    effect.constant.direction.dir[0] = 1;
    // Short effects expire even if the simulation stops updating unexpectedly.
    effect.constant.length = 120;
    effectId = SDL_HapticNewEffect(haptic,&effect);
    if (effectId < 0) SDL_Log("Wheel FFB effect failed: %s",SDL_GetError());
}
int groupForAxis(int a) { return a == 0 ? 0 : a == 4 ? 3 : 1; }
float axisValue(int a) {
    auto* device = devices[groupForAxis(a)];
    const auto& axis = g_wheelSettings.axes[a];
    if (!device || axis.index < 0 || axis.index >= SDL_JoystickNumAxes(device)) return a == 3 && !g_wheelSettings.transmission.assisted ? 1.f : 0.f;
    return axis.read(SDL_JoystickGetAxis(device,axis.index),a==0);
}
bool button(SDL_Joystick* device,int b) { return device && b>=0 && b<SDL_JoystickNumButtons(device) && SDL_JoystickGetButton(device,b); }
}

void Wheel_Init() {
    if (initialized) return;
    initialized = SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC) == 0;
    if (initialized && g_wheelSettings.enabled) scan();
}
void Wheel_Stop() {
    if (haptic && effectId >= 0) SDL_HapticStopEffect(haptic,effectId);
    filteredForce = 0;
    forcePrimed = false;
}
void Wheel_Shutdown() { closeDevices(); initialized=false; active=false; }
void Wheel_Poll(bool driving) {
    if (!initialized) return;
    bool detached = false;
    for (auto* d : devices) if (d && !SDL_JoystickGetAttached(d)) detached=true;
    if (detached) closeDevices();
    if (g_wheelSettings.enabled && !devices[0] && SDL_GetTicks()-lastScan > 1000) {
        lastScan=SDL_GetTicks(); scan();
    }
    active = driving && devices[0] && SDL_GetKeyboardFocus();
    if (!active) Wheel_Stop();
    SDL_JoystickUpdate();
}
bool Wheel_Read(racing::Input& input) {
    if (!active || !devices[0]) return false;
    return Wheel_SampleDevices(input);
}
bool Wheel_SampleDevices(racing::Input& input) {
    if (!devices[0] || !SDL_JoystickGetAttached(devices[0])) return false;
    input = racing::Input();
    input.steering=racing::clamp(axisValue(0)*racing::clamp(g_wheelSettings.hardwareRotation,180,2520)/racing::clamp(g_wheelSettings.rotation,180,2520),-1,1);
    input.throttle=axisValue(1); input.brake=axisValue(2);
    input.clutch=axisValue(3); input.handbrake=std::max(axisValue(4),button(devices[3],g_wheelSettings.handbrakeButton)?1.f:0.f);
    int pressed=0;
    for (int i=0;i<6;++i) if (button(devices[2],g_wheelSettings.gearButtons[i])) { input.gear=i==5?-1:i+1; ++pressed; }
    if (pressed>1) input.gear=0;
    return true;
}
unsigned Wheel_Buttons(unsigned& edges) {
    static unsigned previous=0;
    unsigned bits=0;
    if(devices[0] && SDL_GetKeyboardFocus()) {
        const unsigned map[]={0x40,0x80,0x20,0x10,0x8,0x4,0x1,0x2,0x100,0x800,0x200,0x400};
        for(int b=0;b<12;++b) if(button(devices[0],b)) bits|=map[b];
        if(SDL_JoystickNumHats(devices[0])) {
            Uint8 hat=SDL_JoystickGetHat(devices[0],0);
            if(hat&SDL_HAT_UP)bits|=0x1000;if(hat&SDL_HAT_RIGHT)bits|=0x2000;
            if(hat&SDL_HAT_DOWN)bits|=0x4000;if(hat&SDL_HAT_LEFT)bits|=0x8000;
        }
    }
    edges=bits&~previous;previous=bits;return bits;
}
void Wheel_Force(float aligning,float road) {
    if (!active || !haptic || effectId < 0 || !SDL_GetKeyboardFocus()) { Wheel_Stop(); return; }
    float steer=axisValue(0);
    float force = aligning + racing::clamp(road,-1,1)*g_wheelSettings.road - (forcePrimed ? steer-previousSteer : 0)*g_wheelSettings.damping*30;
    float lock=racing::clamp(g_wheelSettings.rotation/std::max(180.f,g_wheelSettings.hardwareRotation),.05f,1);
    if(std::fabs(steer)>lock) force-=(steer>0?1:-1)*racing::clamp((std::fabs(steer)-lock)*15,0,1);
    previousSteer=steer;
    forcePrimed=true;
    filteredForce += (racing::clamp(force,-1,1)-filteredForce)*racing::clamp(g_wheelSettings.filter,.01f,1);
    float gain=racing::clamp(g_wheelSettings.gain,0,1);
    effect.constant.level=static_cast<Sint16>(racing::clamp(filteredForce*gain*(g_wheelSettings.forceInvert?-1:1),-1,1)*32767);
    if (SDL_HapticUpdateEffect(haptic,effectId,&effect)<0 || SDL_HapticRunEffect(haptic,effectId,1)<0) Wheel_Stop();
}

void Wheel_LoadConfig(const char* path) {
    ini_t* cfg=ini_load(path); if (!cfg) return;
    auto readInt=[&](const char* key,int& value){ ini_sget(cfg,"wheel",key,"%d",&value); };
    auto readFloat=[&](const char* key,float& value){ float v=value; ini_sget(cfg,"wheel",key,"%f",&v); if(std::isfinite(v)) value=v; };
    readInt("enabled",g_wheelSettings.enabled);
    const char* names[]={"device","pedals_device","shifter_device","handbrake_device"};
    for (int i=0;i<4;++i) {
        readInt(names[i],*deviceSetting(i));
        const char* v=ini_get(cfg,"wheel",guidNames[i]); if(v) snprintf(guidSetting(i),64,"%s",v);
    }
    for(int i=0;i<5;++i) {
        auto& a=g_wheelSettings.axes[i]; char key[64];
        #define AXIS_INT(suffix,member) snprintf(key,sizeof(key),"%s_%s",axisNames[i],suffix); readInt(key,a.member)
        AXIS_INT("axis",index); AXIS_INT("min",minimum); AXIS_INT("center",center); AXIS_INT("max",maximum);
        #undef AXIS_INT
        snprintf(key,sizeof(key),"%s_invert",axisNames[i]); int invert=a.inverted; readInt(key,invert); a.inverted=invert!=0;
        snprintf(key,sizeof(key),"%s_deadzone",axisNames[i]); readFloat(key,a.deadzone);
        snprintf(key,sizeof(key),"%s_gamma",axisNames[i]); readFloat(key,a.gamma);
    }
    for(int i=0;i<6;++i) { char key[32]; snprintf(key,sizeof(key),"gear%d_button",i+1); readInt(key,g_wheelSettings.gearButtons[i]); }
    readInt("handbrake_button",g_wheelSettings.handbrakeButton);
    readInt("ffb_invert",g_wheelSettings.forceInvert);
    readFloat("rotation",g_wheelSettings.rotation); readFloat("hardware_rotation",g_wheelSettings.hardwareRotation); readFloat("steering_lock",g_wheelSettings.steeringLock);
    readFloat("ffb_gain",g_wheelSettings.gain); readFloat("ffb_damping",g_wheelSettings.damping);
    readFloat("ffb_road",g_wheelSettings.road); readFloat("ffb_filter",g_wheelSettings.filter);
    auto& t=g_wheelSettings.transmission;
    int assisted=t.assisted,burnout=t.burnout; readInt("assisted_clutch",assisted); readInt("driver_burnout",burnout);
    t.assisted=assisted!=0; t.burnout=burnout!=0;
    readFloat("clutch_bite",t.bite); t.bite=racing::clamp(t.bite,.1f,.95f);
    readFloat("final_drive",t.finalDrive); t.finalDrive=racing::clamp(t.finalDrive,1,8);
    readFloat("tyre_radius",t.tyreRadius); t.tyreRadius=racing::clamp(t.tyreRadius,.2f,.6f);
    readFloat("idle_rpm",t.idle); t.idle=racing::clamp(t.idle,600,1500);
    readFloat("redline_rpm",t.redline); t.redline=racing::clamp(t.redline,3000,10000);
    for(int i=0;i<6;++i){ char key[32]; snprintf(key,sizeof(key),"ratio%d",i); readFloat(key,t.ratios[i]); t.ratios[i]=racing::clamp(t.ratios[i],.3f,6); }
    ini_free(cfg);
}
bool Wheel_SaveConfig(const char* path) {
    std::string temp=std::string(path)+".tmp";
    FILE* f=fopen(temp.c_str(),"wb"); if(!f) return false;
    fprintf(f,"[wheel]\nenabled=%d\n",g_wheelSettings.enabled);
    const char* names[]={"device","pedals_device","shifter_device","handbrake_device"};
    for(int i=0;i<4;++i) fprintf(f,"%s=%d\n%s=%s\n",names[i],*deviceSetting(i),guidNames[i],guidSetting(i));
    for(int i=0;i<5;++i){ const auto& a=g_wheelSettings.axes[i];
        fprintf(f,"%s_axis=%d\n%s_min=%d\n%s_center=%d\n%s_max=%d\n%s_invert=%d\n%s_deadzone=%.6f\n%s_gamma=%.6f\n",
            axisNames[i],a.index,axisNames[i],a.minimum,axisNames[i],a.center,axisNames[i],a.maximum,axisNames[i],a.inverted,
            axisNames[i],a.deadzone,axisNames[i],a.gamma);
    }
    for(int i=0;i<6;++i) fprintf(f,"gear%d_button=%d\n",i+1,g_wheelSettings.gearButtons[i]);
    fprintf(f,"handbrake_button=%d\nrotation=%g\nsteering_lock=%g\nffb_gain=%g\nffb_damping=%g\nffb_road=%g\nffb_filter=%g\nffb_invert=%d\n",
        g_wheelSettings.handbrakeButton,g_wheelSettings.rotation,g_wheelSettings.steeringLock,g_wheelSettings.gain,
        g_wheelSettings.damping,g_wheelSettings.road,g_wheelSettings.filter,g_wheelSettings.forceInvert);
    fprintf(f,"hardware_rotation=%g\n",g_wheelSettings.hardwareRotation);
    const auto& t=g_wheelSettings.transmission;
    fprintf(f,"assisted_clutch=%d\ndriver_burnout=%d\nclutch_bite=%g\nfinal_drive=%g\ntyre_radius=%g\nidle_rpm=%g\nredline_rpm=%g\n",t.assisted,t.burnout,t.bite,t.finalDrive,t.tyreRadius,t.idle,t.redline);
    for(int i=0;i<6;++i) fprintf(f,"ratio%d=%g\n",i,t.ratios[i]);
    bool ok=!ferror(f); if(fclose(f)) ok=false;
    if(!ok) return false;
    // SDL's portable file API has no atomic replacement; retain a recoverable backup.
    std::string backup=std::string(path)+".bak";
    remove(backup.c_str());
    bool exists=false; if(FILE* old=fopen(path,"rb")){ exists=true; fclose(old); }
    if(exists && rename(path,backup.c_str())) return false;
    if(rename(temp.c_str(),path)){ if(exists) rename(backup.c_str(),path); return false; }
    return true;
}
