#include "wheel.h"
#include <SDL.h>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cctype>

namespace {
// Small built-in font: the configuration tool has no dependency on game assets.
const unsigned char glyphs[][5] = {
 {62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},{24,20,18,127,16},
 {39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},{54,73,73,73,54},{6,73,73,41,30},
 {126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},{127,73,73,73,65},
 {127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},{0,65,127,65,0},{32,64,65,63,1},
 {127,8,20,34,65},{127,64,64,64,64},{127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},
 {127,9,9,9,6},{62,65,81,33,94},{127,9,25,41,70},{70,73,73,73,49},{1,1,127,1,1},
 {63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},{7,8,112,8,7},{97,81,73,69,67}
};
void text(SDL_Renderer* r,int x,int y,const char* s) {
    for(;*s;++s,x+=12){ int ch=std::toupper(static_cast<unsigned char>(*s)); int i=ch>='0'&&ch<='9'?ch-'0':ch>='A'&&ch<='Z'?ch-'A'+10:-1;
        if(i>=0) for(int col=0;col<5;++col) for(int row=0;row<7;++row) if(glyphs[i][col]&(1<<row)) {
            SDL_Rect p={x+col*2,y+row*2,2,2}; SDL_RenderFillRect(r,&p);
        }
        if(ch=='-' || ch=='='){SDL_Rect p={x,y+6,8,2};SDL_RenderFillRect(r,&p);}
        if(ch=='.'){SDL_Rect p={x+4,y+12,2,2};SDL_RenderFillRect(r,&p);}
    }
}
}

int Wheel_Configure(const char* configPath) {
    Wheel_Stop();
    SDL_Window* window=SDL_CreateWindow("REDRIVER2 Racing wheel configuration",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1100,750,0);
    if(!window) return 1;
    SDL_Renderer* renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if(!renderer){SDL_DestroyWindow(window);return 1;}
    std::vector<SDL_Joystick*> joys;
    auto discover=[&](){
        for(auto* j:joys) SDL_JoystickClose(j); joys.clear();
        for(int i=0;i<SDL_NumJoysticks();++i) joys.push_back(SDL_JoystickOpen(i));
    };
    discover();
    const char* labels[]={"STEERING","THROTTLE","BRAKE","CLUTCH","HANDBRAKE AXIS","GEAR 1","GEAR 2","GEAR 3","GEAR 4","GEAR 5","GEAR 6 IS REVERSE","HANDBRAKE BUTTON"};
    int selected=0, binding=-1, calibrating=-1;
    racing::Axis previousCalibration;
    std::vector<std::vector<int>> baseline;
    std::vector<std::vector<bool>> buttonBaseline;
    bool running=true;
    const char* status="SELECT A ROW WITH UP DOWN. ENTER TO BIND.";
    auto devIndex=[&](int group){
        const char* wanted=group==0?g_wheelSettings.guid:group==1?g_wheelSettings.pedalsGuid:group==2?g_wheelSettings.shifterGuid:g_wheelSettings.handbrakeGuid;
        int index=group==0?g_wheelSettings.device:group==1?g_wheelSettings.pedals:group==2?g_wheelSettings.shifter:g_wheelSettings.handbrakeDevice;
        if(!*wanted && index<0) { wanted=g_wheelSettings.guid; index=g_wheelSettings.device; }
        for(int i=0;i<int(joys.size());++i) if(joys[i]){char guid[64];SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joys[i]),guid,sizeof(guid));
            if((*wanted && !strcmp(wanted,guid)) || (!*wanted && (index==i || (index<0 && strstr(SDL_JoystickName(joys[i]),"G29"))))) return i;
        } return -1;
    };
    auto bindDevice=[&](int group,int d){
        char* guid=group==0?g_wheelSettings.guid:group==1?g_wheelSettings.pedalsGuid:group==2?g_wheelSettings.shifterGuid:g_wheelSettings.handbrakeGuid;
        int& index=group==0?g_wheelSettings.device:group==1?g_wheelSettings.pedals:group==2?g_wheelSettings.shifter:g_wheelSettings.handbrakeDevice;
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joys[d]),guid,64); index=d;
    };
    while(running){
        SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type==SDL_QUIT) running=false;
            if(e.type==SDL_JOYDEVICEADDED || e.type==SDL_JOYDEVICEREMOVED){if(calibrating>=0)g_wheelSettings.axes[calibrating]=previousCalibration;discover();binding=-1;calibrating=-1;}
            if(e.type!=SDL_KEYDOWN) continue;
            SDL_Keycode key=e.key.keysym.sym;
            if(key==SDLK_ESCAPE){ if(binding>=0||calibrating>=0){if(calibrating>=0)g_wheelSettings.axes[calibrating]=previousCalibration;binding=-1;calibrating=-1;status="CANCELLED";}else running=false; }
            if(key==SDLK_UP && binding<0 && calibrating<0) selected=(selected+11)%12;
            if(key==SDLK_DOWN && binding<0 && calibrating<0) selected=(selected+1)%12;
            if(key==SDLK_RETURN && calibrating<0){
                binding=selected;baseline.clear();buttonBaseline.clear();
                for(auto* j:joys){std::vector<int> axes; if(j)for(int a=0;a<SDL_JoystickNumAxes(j);++a)axes.push_back(SDL_JoystickGetAxis(j,a));baseline.push_back(axes);}
                for(auto* j:joys){std::vector<bool> buttons;if(j)for(int b=0;b<SDL_JoystickNumButtons(j);++b)buttons.push_back(SDL_JoystickGetButton(j,b)!=0);buttonBaseline.push_back(buttons);}
                status=selected<5?"MOVE ONLY THE AXIS TO ASSIGN IT":"RELEASE THEN ENGAGE THE GEAR OR PRESS THE BUTTON";
            }
            if(key==SDLK_c && selected<5 && binding<0){
                auto& a=g_wheelSettings.axes[selected];int d=devIndex(selected==0?0:selected==4?3:1);
                if(d>=0 && a.index>=0 && a.index<SDL_JoystickNumAxes(joys[d])){
                    if(calibrating==selected){
                        int release=SDL_JoystickGetAxis(joys[d],a.index);
                        if(a.maximum-a.minimum<1024)status="MOVE THE FULL RANGE BEFORE FINISHING CALIBRATION";
                        else if(selected==0 && (release<a.minimum+(a.maximum-a.minimum)/10 || release>a.maximum-(a.maximum-a.minimum)/10))status="CENTER THE WHEEL THEN PRESS C";
                        else{a.center=release;if(selected!=0)a.inverted=release>(a.minimum+a.maximum)/2;calibrating=-1;status="CALIBRATED. PRESS S TO SAVE";}
                    }else{previousCalibration=a;a.minimum=a.maximum=SDL_JoystickGetAxis(joys[d],a.index);calibrating=selected;status="MOVE FULL RANGE. CENTER WHEEL OR RELEASE PEDAL. C TO FINISH";}
                }
            }
            if(key==SDLK_i && selected<5) g_wheelSettings.axes[selected].inverted=!g_wheelSettings.axes[selected].inverted;
            if(key==SDLK_DELETE){if(selected<5)g_wheelSettings.axes[selected].index=-1;else if(selected<11)g_wheelSettings.gearButtons[selected-5]=-1;else g_wheelSettings.handbrakeButton=-1;}
            if(key==SDLK_a)g_wheelSettings.transmission.assisted=!g_wheelSettings.transmission.assisted;
            if(key==SDLK_EQUALS)g_wheelSettings.gain=racing::clamp(g_wheelSettings.gain+.05f,0,1);
            if(key==SDLK_MINUS)g_wheelSettings.gain=racing::clamp(g_wheelSettings.gain-.05f,0,1);
            if(key==SDLK_LEFTBRACKET)g_wheelSettings.rotation=racing::clamp(g_wheelSettings.rotation-90,180,2520);
            if(key==SDLK_RIGHTBRACKET)g_wheelSettings.rotation=racing::clamp(g_wheelSettings.rotation+90,180,2520);
            if(key==SDLK_s && binding<0 && calibrating<0){g_wheelSettings.enabled=1;status=Wheel_SaveConfig(configPath)?"SAVED TO WHEEL.INI":"SAVE FAILED. CHECK FOLDER PERMISSIONS";}
        }
        SDL_JoystickUpdate();
        for(int d=0;d<int(joys.size());++d)if(joys[d]){
            if(binding>=0 && binding<5 && d<int(baseline.size()))for(int a=0;a<int(baseline[d].size());++a) {
                if(std::abs(SDL_JoystickGetAxis(joys[d],a)-baseline[d][a])>12000){
                    int group=binding==0?0:binding==4?3:1;bindDevice(group,d);g_wheelSettings.axes[binding].index=a;
                    binding=-1;status="BOUND. C TO CALIBRATE. I TO INVERT";break;
                }
            }
            if(binding>=5 && d<int(buttonBaseline.size())) for(int b=0;b<SDL_JoystickNumButtons(joys[d]);++b){
                bool down=SDL_JoystickGetButton(joys[d],b)!=0;
                bool rising=down && !buttonBaseline[d][b];buttonBaseline[d][b]=down;
                if(!rising)continue;
                bindDevice(binding==11?3:2,d);
                if(binding<11)g_wheelSettings.gearButtons[binding-5]=b;else g_wheelSettings.handbrakeButton=b;
                binding=-1;status="BUTTON BOUND. PRESS S TO SAVE";break;
            }
        }
        if(calibrating>=0){int d=devIndex(calibrating==0?0:calibrating==4?3:1);if(d>=0){auto& a=g_wheelSettings.axes[calibrating];int raw=SDL_JoystickGetAxis(joys[d],a.index);a.minimum=std::min(a.minimum,raw);a.maximum=std::max(a.maximum,raw);}}
        SDL_SetRenderDrawColor(renderer,18,23,33,255);SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer,220,230,240,255);
        text(renderer,24,20,"REDRIVER2 RACING WHEEL CALIBRATION");
        char line[100];snprintf(line,sizeof(line),"DEVICES %d  ROTATION %.0F  FFB %.0F PERCENT  CLUTCH %s",int(joys.size()),g_wheelSettings.rotation,g_wheelSettings.gain*100,g_wheelSettings.transmission.assisted?"ASSISTED":"MANUAL");text(renderer,24,50,line);
        for(int row=0;row<12;++row){
            SDL_SetRenderDrawColor(renderer,row==selected?255:165,row==selected?210:180,110,255);text(renderer,24,95+row*34,labels[row]);
            int group=row==0?0:row<4?1:row==4||row==11?3:2;int d=devIndex(group);
            int index=row<5?g_wheelSettings.axes[row].index:row<11?g_wheelSettings.gearButtons[row-5]:g_wheelSettings.handbrakeButton;
            snprintf(line,sizeof(line),"D %d  %s %d",d,row<5?"AXIS":"BUTTON",index);text(renderer,300,95+row*34,line);
            if(d>=0){
                float value=0;
                if(row<5 && index>=0 && index<SDL_JoystickNumAxes(joys[d])){int raw=SDL_JoystickGetAxis(joys[d],index);value=g_wheelSettings.axes[row].read(raw,row==0);snprintf(line,sizeof(line),"RAW %d  OUT %.2F",raw,value);text(renderer,550,95+row*34,line);if(row==0)value=(value+1)*.5f;}
                else if(row>=5 && index>=0 && index<SDL_JoystickNumButtons(joys[d]))value=SDL_JoystickGetButton(joys[d],index);
                SDL_Rect bar={900,95+row*34,int(160*racing::clamp(value,0,1)),16};SDL_RenderFillRect(renderer,&bar);
            }
        }
        SDL_SetRenderDrawColor(renderer,205,215,230,255);
        text(renderer,24,520,"UP DOWN SELECT   ENTER BIND   C CALIBRATE   I INVERT");
        text(renderer,24,550,"A CLUTCH ASSIST   MINUS PLUS FFB   BRACKETS ROTATION");
        text(renderer,24,580,"DELETE CLEAR BINDING   S SAVE   ESC CANCEL OR EXIT");
        text(renderer,24,630,status);
        int d=devIndex(0);if(d>=0)text(renderer,24,680,SDL_JoystickName(joys[d]));
        SDL_RenderPresent(renderer);SDL_Delay(16);
    }
    for(auto* j:joys)if(j)SDL_JoystickClose(j);
    SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);
    return 0;
}
