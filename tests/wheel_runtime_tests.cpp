#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "../src_rebuild/utils/wheel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
int tests=0;
void require(bool value,const char* msg){++tests;if(!value){fprintf(stderr,"FAIL: %s (%s)\n",msg,SDL_GetError());exit(1);}}
int main(int argc,char** argv){
    SDL_SetMainReady();
    if(argc>1 && !strcmp(argv[1],"--monitor")){
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
        if(SDL_Init(SDL_INIT_JOYSTICK))return 1;
        struct Device {SDL_Joystick* joy;std::vector<int> lo,hi,last;std::vector<unsigned char> buttons;};
        std::vector<Device> devices;
        for(int i=0;i<SDL_NumJoysticks();++i){auto* joy=SDL_JoystickOpen(i);if(!joy)continue;
            Device d;d.joy=joy;
            for(int a=0;a<SDL_JoystickNumAxes(joy);++a){int raw=SDL_JoystickGetAxis(joy,a);d.lo.push_back(raw);d.hi.push_back(raw);d.last.push_back(raw);}
            d.buttons.resize(SDL_JoystickNumButtons(joy));devices.push_back(d);
            printf("DEVICE %d: %s\n",int(devices.size()-1),SDL_JoystickName(joy));
        }
        int seconds=argc>2?std::max(1,std::min(120,atoi(argv[2]))):45;
        Uint32 start=SDL_GetTicks(),lastPrint=0;
        printf("READ ONLY CAPTURE %d seconds. No force effects are opened or sent.\n",seconds);fflush(stdout);
        while(SDL_GetTicks()-start<Uint32(seconds*1000)){
            SDL_PumpEvents();SDL_JoystickUpdate();Uint32 now=SDL_GetTicks()-start;
            for(size_t i=0;i<devices.size();++i){auto& d=devices[i];if(!SDL_JoystickGetAttached(d.joy))continue;
                bool changed=now<50;
                for(int a=0;a<int(d.last.size());++a){int raw=SDL_JoystickGetAxis(d.joy,a);d.lo[a]=std::min(d.lo[a],raw);d.hi[a]=std::max(d.hi[a],raw);if(std::abs(raw-d.last[a])>1024)changed=true;}
                if(changed && (now-lastPrint>500 || now<50)){
                    printf("%6.2fs D%d",now/1000.,int(i));for(int a=0;a<int(d.last.size());++a){d.last[a]=SDL_JoystickGetAxis(d.joy,a);printf(" A%d=%d",a,d.last[a]);}printf("\n");
                }
                for(int b=0;b<int(d.buttons.size());++b){auto down=SDL_JoystickGetButton(d.joy,b);if(down!=d.buttons[b]){printf("%6.2fs D%d B%d=%d\n",now/1000.,int(i),b,int(down));d.buttons[b]=down;}}
            }
            if(now-lastPrint>500)lastPrint=now;
            fflush(stdout);SDL_Delay(20);
        }
        for(size_t i=0;i<devices.size();++i){auto& d=devices[i];for(int a=0;a<int(d.last.size());++a)printf("RANGE D%d A%d MIN=%d MAX=%d END=%d\n",int(i),a,d.lo[a],d.hi[a],SDL_JoystickGetAxis(d.joy,a));SDL_JoystickClose(d.joy);}
        SDL_Quit();return 0;
    }
    if(argc>1 && !strcmp(argv[1],"--devices")){
        if(SDL_Init(SDL_INIT_JOYSTICK|SDL_INIT_HAPTIC))return 1;
        printf("SDL devices: %d\n",SDL_NumJoysticks());
        for(int i=0;i<SDL_NumJoysticks();++i){auto* j=SDL_JoystickOpen(i);if(!j)continue;char guid[64];SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(j),guid,sizeof(guid));
            printf("%d: %s | GUID %s | axes %d buttons %d | haptic %d\n",i,SDL_JoystickName(j),guid,SDL_JoystickNumAxes(j),SDL_JoystickNumButtons(j),SDL_JoystickIsHaptic(j));SDL_JoystickClose(j);}
        SDL_Quit();return 0;
    }
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    require(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK|SDL_INIT_HAPTIC)==0,"headless SDL initialization");
    SDL_VirtualJoystickDesc desc={};desc.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;desc.type=SDL_JOYSTICK_TYPE_WHEEL;desc.naxes=4;desc.nbuttons=18;desc.name="Logitech G29 test device";
    int index=SDL_JoystickAttachVirtualEx(&desc);require(index>=0,"attach virtual wheel");
    auto* joystick=SDL_JoystickOpen(index);require(joystick!=nullptr,"open virtual wheel");
    g_wheelSettings.axes[0].deadzone=0;g_wheelSettings.axes[1].deadzone=0;
    g_wheelSettings.device=index;
    // Physical USB devices can enumerate late on Windows and shift device indices.
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joystick),g_wheelSettings.guid,sizeof(g_wheelSettings.guid));
    const char* path="src_rebuild/bin/tests/wheel-roundtrip.ini";
    g_wheelSettings.axes[0].center=123;g_wheelSettings.transmission.assisted=false;
    require(Wheel_SaveConfig(path),"save calibration");
    g_wheelSettings=WheelSettings();Wheel_LoadConfig(path);
    require(g_wheelSettings.axes[0].center==123 && !g_wheelSettings.transmission.assisted,"calibration round trip");
    g_wheelSettings.force.caster=.4f;g_wheelSettings.force.friction=.3f;g_wheelSettings.ffbLog=1;
    require(Wheel_SaveConfig(path),"save force feedback settings");
    g_wheelSettings=WheelSettings();Wheel_LoadConfig(path);
    require(std::fabs(g_wheelSettings.force.caster-.4f)<.001f && std::fabs(g_wheelSettings.force.friction-.3f)<.001f && g_wheelSettings.ffbLog==1,"force feedback settings round trip");
    Wheel_Init();SDL_Delay(1100); // Let the file timestamp change.
    g_wheelSettings.force.gain=.5f;require(Wheel_SaveConfig(path),"edit configuration externally");
    g_wheelSettings.force.gain=.8f;Wheel_Poll(false);
    require(std::fabs(g_wheelSettings.force.gain-.5f)<.001f,"wheel.ini edits are reloaded while running");
    g_wheelSettings.ffbLog=0;Wheel_Shutdown();
    g_wheelSettings.axes[0].center=0;
    Wheel_Init();
    require(SDL_JoystickSetVirtualAxis(joystick,0,16384)==0,"set analog steering");
    require(SDL_JoystickSetVirtualAxis(joystick,1,0)==0,"half accelerator");
    require(SDL_JoystickSetVirtualAxis(joystick,2,32767)==0,"release brake");
    require(SDL_JoystickSetVirtualAxis(joystick,3,32767)==0,"release clutch");
    require(SDL_JoystickSetVirtualButton(joystick,17,1)==0,"select sixth position");
    SDL_JoystickUpdate();Wheel_Poll(false);
    racing::Input input;
    require(!Wheel_Read(input),"menus never drive the vehicle");
    require(Wheel_SampleDevices(input),"read raw diagnostics without activating vehicle");
    require(std::fabs(input.steering-.5f)<.001f,"16 bit steering reaches game unquantized");
    require(std::fabs(input.throttle-.5f)<.001f && input.brake==0,"independent progressive pedals");
    require(input.gear==-1,"sixth shifter position maps to reverse");
    SDL_JoystickSetVirtualAxis(joystick,2,-32768);SDL_JoystickUpdate();Wheel_SampleDevices(input);
    require(input.brake==1 && input.clutch==0 && std::fabs(input.throttle-.5f)<.001f,"brake does not change accelerator or clutch");
    SDL_JoystickSetVirtualAxis(joystick,2,32767);SDL_JoystickUpdate();
    SDL_JoystickSetVirtualButton(joystick,17,0);SDL_JoystickSetVirtualButton(joystick,12,1);SDL_JoystickSetVirtualButton(joystick,13,1);SDL_JoystickUpdate();
    Wheel_SampleDevices(input);require(input.gear==0,"conflicting shifter buttons become neutral");
    SDL_JoystickSetVirtualButton(joystick,13,0);SDL_JoystickSetVirtualAxis(joystick,3,-32768);SDL_JoystickUpdate();
    Wheel_SampleDevices(input);require(input.gear==1 && input.clutch==1,"first gear and fully depressed clutch");
    require(input.brake==0 && std::fabs(input.throttle-.5f)<.001f,"clutch does not change accelerator or brake");
    g_wheelSettings.rotation=450;Wheel_SampleDevices(input);require(input.steering>.99f,"reduced rotation maps to earlier lock");
    // No physical window or focus is acquired by this automated test.
    Wheel_Poll(true);require(!Wheel_Read(input),"no force or input without keyboard focus");
    Wheel_Shutdown();
    {
        // Configuration tool smoke test: force feedback tab, preset, slider, toggle and save.
        auto key=[](SDL_Keycode k,Uint16 mod=0){SDL_Event e;SDL_zero(e);e.type=SDL_KEYDOWN;e.key.keysym.sym=k;e.key.keysym.mod=mod;SDL_PushEvent(&e);};
        const char* uiPath="src_rebuild/bin/tests/wheel-ui.ini";
        SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);
        key(SDLK_TAB);key(SDLK_F1);key(SDLK_DOWN);key(SDLK_RIGHT);key(SDLK_RIGHT,KMOD_SHIFT);
        for(int i=0;i<11;++i)key(SDLK_DOWN); // last row: telemetry toggle
        key(SDLK_RIGHT);key(SDLK_s);
        SDL_Event quit;SDL_zero(quit);quit.type=SDL_QUIT;SDL_PushEvent(&quit);
        g_wheelSettings.ffbLog=0;
        require(Wheel_Configure(uiPath)==0,"configuration window runs headless");
        require(std::fabs(g_wheelSettings.force.gain-.65f)<.001f,"F1 applies the soft preset");
        require(std::fabs(g_wheelSettings.force.filter-.66f)<.001f,"arrow keys adjust the selected slider, shift for fine steps");
        require(g_wheelSettings.ffbLog==1,"toggle rows switch with the arrow keys");
        g_wheelSettings=WheelSettings();Wheel_LoadConfig(uiPath);
        require(std::fabs(g_wheelSettings.force.gain-.65f)<.001f && g_wheelSettings.ffbLog==1,"force feedback tab saves wheel.ini");
        remove(uiPath);remove("src_rebuild/bin/tests/wheel-ui.ini.bak");
    }
    SDL_JoystickID instance=SDL_JoystickInstanceID(joystick);
    int detachIndex=-1;
    for(int i=0;i<SDL_NumJoysticks();++i)
        if(SDL_JoystickGetDeviceInstanceID(i)==instance)detachIndex=i;
    SDL_JoystickClose(joystick);
    require(SDL_JoystickDetachVirtual(detachIndex)==0,"detach virtual wheel");
    remove(path);remove("src_rebuild/bin/tests/wheel-roundtrip.ini.bak");
    SDL_Quit();printf("PASS: %d SDL device lifecycle and configuration checks\n",tests);
}
