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
    int autoGearing=t.autoGearing; readInt("auto_gearing",autoGearing); t.autoGearing=autoGearing!=0;
    readFloat("gear_torque",t.gearTorque); t.gearTorque=racing::clamp(t.gearTorque,0,1);
    readFloat("clutch_window",t.clutchWindow); t.clutchWindow=racing::clamp(t.clutchWindow,.05f,.6f);
    readFloat("shift_window",t.shiftWindow); t.shiftWindow=racing::clamp(t.shiftWindow,0,1);
    ini_free(cfg);
}
bool Wheel_SaveConfig(const char* path) {
    std::string temp=std::string(path)+".tmp";
    FILE* f=fopen(temp.c_str(),"wb"); if(!f) return false;
    // Every key is written with its explanation, so the file can be edited by
    // hand without the documentation. ASCII only: MSVC reads this source with
    // the ANSI code page. The ini parser skips lines starting with ;.
    auto say=[&](const char* text){ fprintf(f,"%s\n",text); };
    say("; =====================================================================");
    say("; REDRIVER2 - configuration du volant (Logitech G29 et compatibles)");
    say("; Les lignes commencant par ; sont des commentaires.");
    say("; Le fichier est relu pendant le jeu : une modification est appliquee");
    say("; en moins d'une seconde. WheelConfig (touche S) le reecrit avec ces");
    say("; commentaires et garde la version precedente dans wheel.ini.bak.");
    say("; Les valeurs hors plage sont ramenees dans la plage indiquee.");
    say("; =====================================================================");
    say("[wheel]");
    say("");
    say("; Support du volant : 1 = actif, 0 = desactive (manette seule).");
    fprintf(f,"enabled=%d\n",g_wheelSettings.enabled);
    say("");
    say("; ---------------------------------------------------------------------");
    say("; PERIPHERIQUES");
    say("; Volant, pedalier, shifter et frein a main peuvent etre des appareils");
    say("; distincts. Le GUID est prioritaire : s'il est renseigne, l'index est");
    say("; ignore. Index -1 sans GUID : le volant cherche un G29/G920/G923, les");
    say("; autres groupes reprennent l'appareil du volant.");
    say("; ---------------------------------------------------------------------");
    const char* names[]={"device","pedals_device","shifter_device","handbrake_device"};
    const char* deviceHelp[]={
        "; Volant (direction et retour de force) : index SDL de l'appareil.",
        "; Pedalier : index SDL. Les trois pedales doivent etre sur cet appareil.",
        "; Shifter (boite en H) : index SDL.",
        "; Frein a main USB : index SDL."};
    for(int i=0;i<4;++i){
        say(deviceHelp[i]);
        fprintf(f,"%s=%d\n",names[i],*deviceSetting(i));
        say("; GUID SDL de cet appareil (rempli par WheelConfig ; vide = utiliser l'index).");
        fprintf(f,"%s=%s\n",guidNames[i],guidSetting(i));
    }
    say("");
    say("; ---------------------------------------------------------------------");
    say("; AXES");
    say("; Valeurs brutes SDL sur 16 bits (-32768 a 32767), mesurees par la");
    say("; calibration de WheelConfig (touche C). Mesures G29/G HUB : volant 0,");
    say("; accelerateur 1, frein 2, embrayage 3.");
    say("; ---------------------------------------------------------------------");
    const char* axisHelp[]={
        "; --- Direction (volant) ---",
        "; --- Accelerateur ---",
        "; --- Frein (une pression a fond peut bloquer les roues : pas d'ABS) ---",
        "; --- Embrayage : 0 = relache, 1 = enfonce (transmission coupee) ---",
        "; --- Frein a main analogique (optionnel, en plus de handbrake_button) ---"};
    for(int i=0;i<5;++i){ const auto& a=g_wheelSettings.axes[i]; const char* n=axisNames[i];
        if(i) say("");
        say(axisHelp[i]);
        say("; Numero d'axe SDL, a partir de 0 (-1 = non affecte).");
        fprintf(f,"%s_axis=%d\n",n,a.index);
        say("; Valeur brute a une butee (pedale : repos ou enfoncee selon l'appareil).");
        fprintf(f,"%s_min=%d\n",n,a.minimum);
        say(i?"; Valeur brute au centre : ignoree pour une pedale.":"; Valeur brute volant droit.");
        fprintf(f,"%s_center=%d\n",n,a.center);
        say("; Valeur brute a l'autre butee.");
        fprintf(f,"%s_max=%d\n",n,a.maximum);
        say(i?"; 1 = inverser le sens (pedales G29 : 1, elles valent 32767 au repos).":"; 1 = inverser le sens de la direction.");
        fprintf(f,"%s_invert=%d\n",n,a.inverted);
        say("; Zone morte : part de la course ignoree (0 a 0.4).");
        fprintf(f,"%s_deadzone=%.6f\n",n,a.deadzone);
        say("; Courbe de reponse : 1 = lineaire, >1 = plus doux au debut, <1 = plus vif (0.2 a 5).");
        fprintf(f,"%s_gamma=%.6f\n",n,a.gamma);
    }
    say("");
    say("; ---------------------------------------------------------------------");
    say("; BOUTONS");
    say("; Numeros de bouton SDL a partir de 0 ; -1 = non affecte.");
    say("; ---------------------------------------------------------------------");
    for(int i=0;i<6;++i){
        if(i<5) fprintf(f,"; Shifter, position %d : rapport %d.\n",i+1,i+1);
        else say("; Shifter, position 6 : marche arriere (la marche arriere physique n'est pas utilisee).");
        fprintf(f,"gear%d_button=%d\n",i+1,g_wheelSettings.gearButtons[i]);
    }
    say("; Bouton du frein a main, lu sur l'appareil handbrake_device.");
    fprintf(f,"handbrake_button=%d\n",g_wheelSettings.handbrakeButton);
    say("");
    say("; ---------------------------------------------------------------------");
    say("; DIRECTION");
    say("; ---------------------------------------------------------------------");
    say("; Plage reglee dans le pilote du volant (G HUB), en degres de butee a butee.");
    fprintf(f,"hardware_rotation=%g\n",g_wheelSettings.hardwareRotation);
    say("; Plage utilisee en jeu (180 a 2520). Plus courte que hardware_rotation :");
    say("; une butee logicielle est ajoutee par le retour de force.");
    fprintf(f,"rotation=%g\n",g_wheelSettings.rotation);
    say("; Braquage maximal des roues avant, en degres par cote (10 a 45).");
    fprintf(f,"steering_lock=%g\n",g_wheelSettings.steeringLock);
    say("");
    say("; ---------------------------------------------------------------------");
    say("; RETOUR DE FORCE");
    say("; ---------------------------------------------------------------------");
    const auto& ff=g_wheelSettings.force;
    say("; 1 = inverser le sens du retour de force (le volant pousse du mauvais cote).");
    fprintf(f,"ffb_invert=%d\n",g_wheelSettings.forceInvert);
    say("; Force globale (0 a 1.5, compression douce au-dessus de 1). Trop mou : augmenter.");
    fprintf(f,"ffb_gain=%g\n",ff.gain);
    say("; Lissage du couple, 1 = aucun (0.05 a 1). A-coups : diminuer ; retard : augmenter.");
    fprintf(f,"ffb_filter=%g\n",ff.filter);
    say("; Bosses, compression des suspensions, herbe et sable (0 a 1).");
    fprintf(f,"ffb_road=%g\n",ff.road);
    say("; Compensation de la zone morte du moteur (0 a 0.25). Centre mort : 0.10 a 0.12.");
    fprintf(f,"ffb_minimum=%g\n",ff.minimum);
    say("; Amortisseur (0 a 1). Oscillations en lachant le volant : augmenter.");
    fprintf(f,"ffb_damping=%g\n",ff.damping);
    say("; Frottement des pneus a l'arret, s'efface vers 30 km/h (0 a 1).");
    fprintf(f,"ffb_friction=%g\n",ff.friction);
    say("; Poids conserve a la limite d'adherence (0 a 2). Allegement trop brutal : 0.4 a 0.6.");
    fprintf(f,"ffb_caster=%g\n",ff.caster);
    say("; Rappel vers le centre independant de la vitesse (0 a 1).");
    fprintf(f,"ffb_self_center=%g\n",ff.selfCenter);
    say("; Transfert de charge : direction plus lourde au freinage (0 a 1, 0 = desactive).");
    fprintf(f,"ffb_load=%g\n",ff.load);
    say("; Debut de la compression douce (0.3 a 1). CLIP frequent : baisser d'abord ffb_gain.");
    fprintf(f,"ffb_knee=%g\n",ff.knee);
    say("; 1 = afficher FFB (niveau), GRIP (glissement avant, 100 = limite) et CLIP a l'ecran.");
    fprintf(f,"ffb_hud=%d\n",g_wheelSettings.ffbHud);
    say("; 1 = ecrire ffb_telemetry.csv dans le dossier du jeu (20 minutes au plus).");
    fprintf(f,"ffb_log=%d\n",g_wheelSettings.ffbLog);
    say("");
    say("; ---------------------------------------------------------------------");
    say("; FREIN A MAIN (axe analogique)");
    say("; ---------------------------------------------------------------------");
    say("; Course a partir de laquelle le frein a main serre (effet Triangle), 0.05 a 1.");
    fprintf(f,"handbrake_on=%g\n",g_wheelSettings.handbrakeOn);
    say("; Course sous laquelle il se relache ; inferieure a handbrake_on (hysteresis).");
    fprintf(f,"handbrake_off=%g\n",g_wheelSettings.handbrakeOff);
    say("");
    say("; ---------------------------------------------------------------------");
    say("; EMBRAYAGE ET BOITE DE VITESSES");
    say("; ---------------------------------------------------------------------");
    const auto& t=g_wheelSettings.transmission;
    say("; 1 = embrayage assiste (la pedale reste prioritaire), 0 = manuel.");
    fprintf(f,"assisted_clutch=%d\n",t.assisted);
    say("; 1 = burnout Driver (Circle) en premiere, au-dela de 97 % d'accelerateur.");
    fprintf(f,"driver_burnout=%d\n",t.burnout);
    say("; Course d'embrayage ou la transmission est coupee et le passage autorise (0.1 a 0.95).");
    fprintf(f,"clutch_bite=%g\n",t.bite);
    say("; Zone de patinage sous clutch_bite (0.05 a 0.6). En dessous de");
    say("; clutch_bite - clutch_window, l'embrayage transmet tout.");
    fprintf(f,"clutch_window=%g\n",t.clutchWindow);
    say("; Delai en secondes pendant lequel un rapport est encore accepte apres");
    say("; avoir debraye (0 a 1). Passages rapides refuses (CLUTCH!) : augmenter.");
    fprintf(f,"shift_window=%g\n",t.shiftWindow);
    say("; 1 = etagement calcule pour chaque voiture : la 5e atteint sa vitesse de");
    say("; pointe d'origine sous le limiteur. 0 = utiliser final_drive et tyre_radius.");
    fprintf(f,"auto_gearing=%d\n",t.autoGearing);
    say("; Part de la demultiplication transmise aux roues (0 a 1). 0 = poussee");
    say("; constante d'origine dans tous les rapports ; 0.7 = reprises plus franches.");
    fprintf(f,"gear_torque=%g\n",t.gearTorque);
    say("; Rapport de pont, utilise seulement avec auto_gearing=0 (1 a 8).");
    fprintf(f,"final_drive=%g\n",t.finalDrive);
    say("; Rayon de roue en metres, utilise seulement avec auto_gearing=0 (0.2 a 0.6).");
    fprintf(f,"tyre_radius=%g\n",t.tyreRadius);
    say("; Regime de ralenti en tr/min (600 a 1500).");
    fprintf(f,"idle_rpm=%g\n",t.idle);
    say("; Regime maxi en tr/min, debut du limiteur (3000 a 10000).");
    fprintf(f,"redline_rpm=%g\n",t.redline);
    say("; Rapports de boite (0.3 a 6). Avec auto_gearing=1, seuls les ecarts entre");
    say("; eux comptent : la 5e est toujours calee sur la vitesse de pointe.");
    for(int i=0;i<6;++i){
        if(i) fprintf(f,"; Rapport de la %d%s.\n",i,i==1?"re":"e");
        else say("; Marche arriere.");
        fprintf(f,"ratio%d=%g\n",i,t.ratios[i]);
    }
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
