#include "wheel.h"
#include "ini.h"
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>

WheelSettings g_wheelSettings;
namespace {
SDL_Joystick* devices[4] = {};
SDL_Haptic* haptic = nullptr;
int effectId = -1;
SDL_HapticEffect effect = {};
bool active = false, initialized = false;
Uint32 lastScan = 0;
float previousSteer = 0;
bool forcePrimed = false, forceRunning = false;
// Passive condition effects run inside the wheel firmware at its own rate,
// which the 30 Hz game loop cannot match for damping without oscillating.
int damperId = -1, frictionId = -1;
float damperLevel = -1, frictionLevel = -1; // -1 = not running
racing::ForceShaper shaper;
std::string configPath;
long long configStamp = 0;
Uint32 lastConfigCheck = 0;
long long fileStamp(const char* path) {
    struct stat st;
    if (!path || !*path || stat(path,&st)) return 0;
    return (long long)st.st_mtime * 1000003LL + (long long)st.st_size;
}
SDL_HapticEffect conditionEffect(Uint16 type, float level) {
    SDL_HapticEffect e = SDL_HapticEffect();
    e.type = type;
    e.condition.direction.type = SDL_HAPTIC_CARTESIAN;
    e.condition.direction.dir[0] = 1;
    e.condition.length = SDL_HAPTIC_INFINITY;
    e.condition.right_sat[0] = e.condition.left_sat[0] = 0xFFFF;
    e.condition.right_coeff[0] = e.condition.left_coeff[0] = static_cast<Sint16>(racing::clamp(level,0,1)*32767);
    return e;
}
void updateCondition(int id, Uint16 type, float level, float& current) {
    if (!haptic || id < 0) return;
    if (current >= 0 && std::fabs(level-current) < .01f) return;
    SDL_HapticEffect e = conditionEffect(type,level);
    if (SDL_HapticUpdateEffect(haptic,id,&e) < 0 || (current < 0 && SDL_HapticRunEffect(haptic,id,1) < 0)) {
        static bool reported=false; if(!reported){SDL_Log("Wheel condition effect failed: %s",SDL_GetError());reported=true;}
        return;
    }
    current = level;
}
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
    if (haptic) {
        if (effectId >= 0) SDL_HapticDestroyEffect(haptic,effectId);
        if (damperId >= 0) SDL_HapticDestroyEffect(haptic,damperId);
        if (frictionId >= 0) SDL_HapticDestroyEffect(haptic,frictionId);
        SDL_HapticClose(haptic);
    }
    haptic = nullptr; effectId = damperId = frictionId = -1;
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
    if(SDL_HapticQuery(haptic)&SDL_HAPTIC_GAIN) SDL_HapticSetGain(haptic,100);
    effect = SDL_HapticEffect();
    effect.type = SDL_HAPTIC_CONSTANT;
    effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
    effect.constant.direction.dir[0] = 1;
    // Short effects expire even if the simulation stops updating unexpectedly.
    effect.constant.length = 120;
    effectId = SDL_HapticNewEffect(haptic,&effect);
    if (effectId < 0) { SDL_Log("Wheel FFB effect failed: %s",SDL_GetError()); return; }
    unsigned features = SDL_HapticQuery(haptic);
    if (features & SDL_HAPTIC_DAMPER) {
        SDL_HapticEffect e = conditionEffect(SDL_HAPTIC_DAMPER,0);
        damperId = SDL_HapticNewEffect(haptic,&e);
    }
    if (features & SDL_HAPTIC_FRICTION) {
        SDL_HapticEffect e = conditionEffect(SDL_HAPTIC_FRICTION,0);
        frictionId = SDL_HapticNewEffect(haptic,&e);
    }
    SDL_Log("Wheel FFB: constant force%s%s",damperId >= 0 ? " + damper" : " (software damping)",frictionId >= 0 ? " + friction" : "");
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
    if (haptic && forceRunning) {
        if (effectId >= 0) SDL_HapticStopEffect(haptic,effectId);
        if (damperId >= 0) SDL_HapticStopEffect(haptic,damperId);
        if (frictionId >= 0) SDL_HapticStopEffect(haptic,frictionId);
    }
    forceRunning = false;
    damperLevel = frictionLevel = -1;
    shaper.reset();
    forcePrimed = false;
}
void Wheel_Shutdown() { closeDevices(); initialized=false; active=false; }
// In VR the headset can own the input focus while the desktop window does not.
bool (*g_wheelExtraFocus)() = nullptr;
static bool hasInputFocus() { return SDL_GetKeyboardFocus() || (g_wheelExtraFocus && g_wheelExtraFocus()); }
void Wheel_Poll(bool driving) {
    if (!initialized) return;
    bool detached = false;
    for (auto* d : devices) if (d && !SDL_JoystickGetAttached(d)) detached=true;
    if (detached) closeDevices();
    if (g_wheelSettings.enabled && !devices[0] && SDL_GetTicks()-lastScan > 1000) {
        lastScan=SDL_GetTicks(); scan();
    }
    // Live tuning: wheel.ini edited while the game runs is applied within a second.
    if (!configPath.empty() && SDL_GetTicks()-lastConfigCheck > 1000) {
        lastConfigCheck = SDL_GetTicks();
        long long stamp = fileStamp(configPath.c_str());
        if (stamp && stamp != configStamp) {
            std::string path = configPath;
            Wheel_LoadConfig(path.c_str());
            SDL_Log("Wheel: reloaded %s",path.c_str());
        }
    }
    active = driving && devices[0] && hasInputFocus();
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
bool Wheel_RawButton(int index) {
    return devices[0] && index>=0 && index<SDL_JoystickNumButtons(devices[0]) && SDL_JoystickGetButton(devices[0],index);
}
unsigned Wheel_Buttons(unsigned& edges) {
    static unsigned previous=0;
    unsigned bits=0;
    if(devices[0] && hasInputFocus()) {
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
racing::ForceReport Wheel_Force(const racing::ForceFrame& frame) {
    if (!active || !haptic || effectId < 0 || !hasInputFocus()) { Wheel_Stop(); return racing::ForceReport(); }
    const racing::ForceSetup& setup = g_wheelSettings.force;
    const float steer = axisValue(0);
    racing::ForceReport report = shaper.step(frame,setup);
    float level = report.output;
    // A 30 Hz software damper lags; only use it when the firmware has none.
    if (damperId < 0 && forcePrimed) level -= (steer-previousSteer)*racing::clamp(setup.damping,0,1)*20;
    // Software end stop for a reduced in-game rotation, always at full strength.
    float lock=racing::clamp(g_wheelSettings.rotation/std::max(180.f,g_wheelSettings.hardwareRotation),.05f,1);
    if(std::fabs(steer)>lock) level-=(steer>0?1:-1)*racing::clamp((std::fabs(steer)-lock)*15,0,1);
    previousSteer=steer;
    forcePrimed=true;
    level = racing::deadbandCompensate(racing::clamp(level,-1,1),setup.minimum);
    effect.constant.level=static_cast<Sint16>(racing::clamp(level*(g_wheelSettings.forceInvert?-1:1),-1,1)*32767);
    forceRunning = true;
    if (SDL_HapticUpdateEffect(haptic,effectId,&effect)<0 || SDL_HapticRunEffect(haptic,effectId,1)<0) {
        static bool reported=false;if(!reported){SDL_Log("Wheel FFB submission failed: %s",SDL_GetError());reported=true;}
        Wheel_Stop(); return racing::ForceReport();
    }
    const racing::ConditionLevels passive = racing::conditionLevels(frame.speed,setup);
    updateCondition(damperId,SDL_HAPTIC_DAMPER,passive.damper,damperLevel);
    updateCondition(frictionId,SDL_HAPTIC_FRICTION,passive.friction,frictionLevel);
    report.output = level;
    return report;
}

void Wheel_LoadConfig(const char* path) {
    ini_t* cfg=ini_load(path); if (!cfg) return;
    if (configPath != path) configPath = path;
    configStamp = fileStamp(path);
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
    // FFB model v2. The former ffb_centering (speed spring) is ignored on purpose:
    // a spring to the centre opposes the countersteer the tyres ask for in a slide.
    auto& ff=g_wheelSettings.force;
    readFloat("ffb_gain",ff.gain); ff.gain=racing::clamp(ff.gain,0,1.5f);
    readFloat("ffb_filter",ff.filter); ff.filter=racing::clamp(ff.filter,.05f,1);
    readFloat("ffb_road",ff.road); ff.road=racing::clamp(ff.road,0,1);
    readFloat("ffb_minimum",ff.minimum); ff.minimum=racing::clamp(ff.minimum,0,.25f);
    readFloat("ffb_damping",ff.damping); ff.damping=racing::clamp(ff.damping,0,1);
    readFloat("ffb_friction",ff.friction); ff.friction=racing::clamp(ff.friction,0,1);
    readFloat("ffb_caster",ff.caster); ff.caster=racing::clamp(ff.caster,0,2);
    readFloat("ffb_self_center",ff.selfCenter); ff.selfCenter=racing::clamp(ff.selfCenter,0,1);
    readFloat("ffb_load",ff.load); ff.load=racing::clamp(ff.load,0,1);
    readFloat("ffb_knee",ff.knee); ff.knee=racing::clamp(ff.knee,.3f,1);
    readInt("ffb_hud",g_wheelSettings.ffbHud); readInt("ffb_log",g_wheelSettings.ffbLog);
    readFloat("handbrake_on",g_wheelSettings.handbrakeOn);readFloat("handbrake_off",g_wheelSettings.handbrakeOff);
    g_wheelSettings.handbrakeOn=racing::clamp(g_wheelSettings.handbrakeOn,.05f,1);
    g_wheelSettings.handbrakeOff=racing::clamp(g_wheelSettings.handbrakeOff,0,g_wheelSettings.handbrakeOn-.01f);
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
    const auto& ff=g_wheelSettings.force;
    fprintf(f,"handbrake_button=%d\nrotation=%g\nsteering_lock=%g\nffb_invert=%d\n",
        g_wheelSettings.handbrakeButton,g_wheelSettings.rotation,g_wheelSettings.steeringLock,g_wheelSettings.forceInvert);
    fprintf(f,"ffb_gain=%g\nffb_filter=%g\nffb_road=%g\nffb_minimum=%g\nffb_damping=%g\nffb_friction=%g\nffb_caster=%g\nffb_self_center=%g\nffb_load=%g\nffb_knee=%g\nffb_hud=%d\nffb_log=%d\n",
        ff.gain,ff.filter,ff.road,ff.minimum,ff.damping,ff.friction,ff.caster,ff.selfCenter,ff.load,ff.knee,g_wheelSettings.ffbHud,g_wheelSettings.ffbLog);
    fprintf(f,"hardware_rotation=%g\n",g_wheelSettings.hardwareRotation);
    fprintf(f,"handbrake_on=%g\nhandbrake_off=%g\n",g_wheelSettings.handbrakeOn,g_wheelSettings.handbrakeOff);
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
