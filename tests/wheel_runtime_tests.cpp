#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "../src_rebuild/utils/wheel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
int tests=0;
void require(bool value,const char* msg){++tests;if(!value){fprintf(stderr,"FAIL: %s (%s)\n",msg,SDL_GetError());exit(1);}}
int main(int argc,char** argv){
    SDL_SetMainReady();
    if(argc>1 && !strcmp(argv[1],"--devices")){
        if(SDL_Init(SDL_INIT_JOYSTICK|SDL_INIT_HAPTIC))return 1;
        printf("SDL devices: %d\n",SDL_NumJoysticks());
        for(int i=0;i<SDL_NumJoysticks();++i){auto* j=SDL_JoystickOpen(i);if(!j)continue;char guid[64];SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(j),guid,sizeof(guid));
            printf("%d: %s | GUID %s | axes %d buttons %d | haptic %d\n",i,SDL_JoystickName(j),guid,SDL_JoystickNumAxes(j),SDL_JoystickNumButtons(j),SDL_JoystickIsHaptic(j));SDL_JoystickClose(j);}
        SDL_Quit();return 0;
    }
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);
    require(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK|SDL_INIT_HAPTIC)==0,"headless SDL initialization");
    SDL_VirtualJoystickDesc desc={};desc.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;desc.type=SDL_JOYSTICK_TYPE_WHEEL;desc.naxes=4;desc.nbuttons=18;desc.name="Logitech G29 test device";
    int index=SDL_JoystickAttachVirtualEx(&desc);require(index>=0,"attach virtual wheel");
    auto* joystick=SDL_JoystickOpen(index);require(joystick!=nullptr,"open virtual wheel");
    g_wheelSettings.axes[0].deadzone=0;g_wheelSettings.axes[1].deadzone=0;
    g_wheelSettings.device=index;
    const char* path="src_rebuild/bin/tests/wheel-roundtrip.ini";
    g_wheelSettings.axes[0].center=123;g_wheelSettings.transmission.assisted=false;
    require(Wheel_SaveConfig(path),"save calibration");
    g_wheelSettings=WheelSettings();Wheel_LoadConfig(path);
    require(g_wheelSettings.axes[0].center==123 && !g_wheelSettings.transmission.assisted,"calibration round trip");
    g_wheelSettings.axes[0].center=0;
    Wheel_Init();
    require(SDL_JoystickSetVirtualAxis(joystick,0,16384)==0,"set analog steering");
    require(SDL_JoystickSetVirtualAxis(joystick,1,32767)==0,"release clutch");
    require(SDL_JoystickSetVirtualAxis(joystick,2,0)==0,"half accelerator");
    require(SDL_JoystickSetVirtualAxis(joystick,3,32767)==0,"release brake");
    require(SDL_JoystickSetVirtualButton(joystick,17,1)==0,"select sixth position");
    SDL_JoystickUpdate();Wheel_Poll(false);
    racing::Input input;
    require(!Wheel_Read(input),"menus never drive the vehicle");
    require(Wheel_SampleDevices(input),"read raw diagnostics without activating vehicle");
    require(std::fabs(input.steering-.5f)<.001f,"16 bit steering reaches game unquantized");
    require(std::fabs(input.throttle-.5f)<.001f && input.brake==0,"independent progressive pedals");
    require(input.gear==-1,"sixth shifter position maps to reverse");
    SDL_JoystickSetVirtualButton(joystick,17,0);SDL_JoystickSetVirtualButton(joystick,12,1);SDL_JoystickSetVirtualButton(joystick,13,1);SDL_JoystickUpdate();
    Wheel_SampleDevices(input);require(input.gear==0,"conflicting shifter buttons become neutral");
    SDL_JoystickSetVirtualButton(joystick,13,0);SDL_JoystickSetVirtualAxis(joystick,1,-32768);SDL_JoystickUpdate();
    Wheel_SampleDevices(input);require(input.gear==1 && input.clutch==1,"first gear and fully depressed clutch");
    g_wheelSettings.rotation=450;Wheel_SampleDevices(input);require(input.steering>.99f,"reduced rotation maps to earlier lock");
    // No physical window or focus is acquired by this automated test.
    Wheel_Poll(true);require(!Wheel_Read(input),"no force or input without keyboard focus");
    Wheel_Shutdown();SDL_JoystickClose(joystick);
    require(SDL_JoystickDetachVirtual(index)==0,"detach virtual wheel");
    remove(path);remove("src_rebuild/bin/tests/wheel-roundtrip.ini.bak");
    SDL_Quit();printf("PASS: %d SDL device lifecycle and configuration checks\n",tests);
}
