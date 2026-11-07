#include "../src_rebuild/utils/wheel_model.h"
#include <cstdio>
#include <cstdlib>

int checks=0;
void check(bool result,const char* message){++checks;if(!result){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(){
    using namespace racing;
    Axis wheel(0);wheel.deadzone=0;
    check(std::fabs(wheel.read(-32768,true)+1)<.0001f,"left lock");
    check(std::fabs(wheel.read(32767,true)-1)<.0001f,"right lock");
    check(wheel.read(0,true)==0,"center");
    check(std::fabs(wheel.read(16384,true)-.5f)<.001f,"linear half lock");
    wheel.minimum=wheel.maximum=wheel.center=0;check(std::isfinite(wheel.read(0,true)),"degenerate calibration");
    Axis pedal(1,true);pedal.deadzone=0;
    check(pedal.read(32767,false)==0,"released pedal");check(pedal.read(-32768,false)==1,"fully pressed pedal");
    check(std::fabs(pedal.read(0,false)-.5f)<.001f,"half pedal remains analog");
    Setup cfg;cfg.assisted=false;
    Input input;Transmission box;
    input.throttle=1;input.gear=1;input.clutch=1;
    for(int i=0;i<90;++i)box.step(input,0,1.f/30,cfg);
    check(box.out.gear==1 && box.out.drive==0 && box.out.rpm>5000,"clutch disconnects engine");
    input.clutch=0;box.step(input,0,1.f/30,cfg);
    check(box.out.drive>0 && box.out.burnout,"first gear burnout on release");
    input.gear=2;box.step(input,5,1.f/30,cfg);
    check(box.out.gear==1 && box.out.rejectedShift,"manual shift needs clutch");
    input.clutch=1;box.step(input,5,1.f/30,cfg);check(box.out.gear==2,"shift after depressing clutch");
    input.gear=-1;box.step(input,10,1.f/30,cfg);check(box.out.gear==2,"reverse rejected at forward speed");
    box.step(input,0,1.f/30,cfg);check(box.out.gear==-1,"reverse accepted at standstill");
    input.clutch=0;box.step(input,0,1.f/30,cfg);check(box.out.drive<=0 && !box.out.burnout,"reverse torque direction");
    input.gear=0;box.step(input,0,1.f/30,cfg);check(box.out.drive==0,"neutral disconnects drive");
    cfg.assisted=true;input.gear=1;input.clutch=0;input.throttle=.5f;box.reset();
    for(int i=0;i<30;++i)box.step(input,3,1.f/30,cfg);float half=box.out.drive;
    input.throttle=1;for(int i=0;i<30;++i)box.step(input,3,1.f/30,cfg);
    check(box.out.drive>half,"progressive accelerator");
    input.clutch=1;box.step(input,3,1.f/30,cfg);check(box.out.drive==0,"physical clutch overrides clutch assistance");input.clutch=0;
    input.gear=5;for(int i=0;i<30;++i)box.step(input,3,1.f/30,cfg);
    check(box.out.drive<half,"fifth gear has less wheel torque than first");
    cfg.assisted=false;box.reset();input.gear=1;input.clutch=1;input.throttle=0;box.step(input,0,1.f/30,cfg);
    input.clutch=0;for(int i=0;i<90;++i)box.step(input,0,1.f/30,cfg);check(box.out.stalled,"manual engine can stall");
    input.clutch=1;input.throttle=.2f;box.step(input,0,1.f/30,cfg);check(!box.out.stalled,"restart using clutch and accelerator");
    std::printf("PASS: %d wheel model checks\n",checks);
}
