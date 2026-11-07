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
    // Circle stays active through the original launch speed, then becomes X.
    cfg.assisted=true;input=Input();input.clutch=0;input.throttle=1;input.gear=1;box.reset();
    for(int i=0;i<60;++i)box.step(input,20,1.f/30,cfg);
    check(box.out.burnout && box.out.drive>1,"burnout maintained beyond old 8 m/s cutoff");
    for(int i=0;i<60;++i)box.step(input,34,1.f/30,cfg);
    check(!box.out.burnout && box.out.drive>.99f,"burnout becomes normal acceleration at native cutoff");
    box.step(input,30,1.f/30,cfg);check(!box.out.burnout,"cutoff does not oscillate back into burnout");
    input.throttle=0;box.step(input,0,1.f/30,cfg);input.throttle=1;box.step(input,0,1.f/30,cfg);
    check(box.out.burnout,"lifting throttle rearms burnout");
    for(int gear=3;gear<=5;++gear){box.reset();input.gear=gear;
        float speeds[]={0,0,0,25,40,60};
        for(int i=0;i<90;++i)box.step(input,speeds[gear],1.f/30,cfg);
        check(box.out.drive>.99f,"high gear preserves stock propulsion in operating range");
    }
    input.throttle=0;box.step(input,60,1.f/30,cfg);check(box.out.engineBrake>0,"engine braking remains off throttle");
    // Force feedback: tyre torque, soft clipping, dead band and passive effects.
    {
        float peakSlip=0,peak=0;
        for(int i=0;i<=100;++i){float t=alignTorque(i/100.f,.25f);if(t>peak){peak=t;peakSlip=i/100.f;}}
        check(std::fabs(peak-1)<.01f,"aligning torque normalised to its peak");
        check(peakSlip>.5f && peakSlip<.8f,"aligning torque peaks before the grip limit");
        check(alignTorque(1,.25f)<.6f && alignTorque(1,.25f)>.3f,"steering lightens but keeps caster feel when saturated");
        check(alignTorque(.2f,.25f)>alignTorque(.1f,.25f) && alignTorque(.1f,.25f)>0,"torque builds progressively with slip");
        check(alignTorque(-.3f,.25f)==-alignTorque(.3f,.25f),"aligning torque is symmetric");
        check(alignTorque(1,.6f)>alignTorque(1,.1f),"more caster keeps more weight at the limit");
        float previous=0;bool monotonic=true,continuous=true;
        for(int i=1;i<=300;++i){float x=i/100.f,y=softClip(x,.75f);if(y<previous)monotonic=false;if(y-previous>.02f)continuous=false;previous=y;}
        check(monotonic && continuous && previous<=1,"soft clip is smooth, monotonic and bounded");
        check(softClip(.5f,.75f)==.5f,"soft clip is linear below the knee");
        check(softClip(-2,.75f)<-.95f,"soft clip handles negative overload");
        check(deadbandCompensate(0,.08f)==0,"no force at exact centre");
        check(std::fabs(deadbandCompensate(.03f,.08f)-(.08f+.92f*.03f))<.0001f,"minimum force reached quickly");
        check(deadbandCompensate(.001f,.08f)<.01f,"no step at centre");
        check(std::fabs(deadbandCompensate(1,.08f)-1)<.0001f && deadbandCompensate(-.5f,.08f)<-.5f,"full range preserved");
        ForceSetup f;
        ConditionLevels parked=conditionLevels(0,f),fast=conditionLevels(30,f);
        check(parked.friction>fast.friction*2 && fast.friction>0,"scrub friction fades with speed");
        check(parked.damper==fast.damper && fast.damper>0,"damper stays active at speed");
        ForceShaper shaper;ForceFrame frame;frame.torque=1;
        ForceReport r;for(int i=0;i<30;++i)r=shaper.step(frame,f);
        check(std::fabs(r.output-f.gain)<.01f && !r.clipping,"steady torque reaches gain without clipping");
        f.gain=1.4f;r=shaper.step(frame,f);check(r.clipping && r.output<1,"overload reported and compressed");
    }
    std::printf("PASS: %d wheel model checks\n",checks);
}
