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
    for(int i=0;i<15;++i)box.step(input,5,1.f/30,cfg);
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
    // Gearing derived from the car's native top speed (61 m/s for powerRatio 4096).
    {
        const float top=61;Setup g;g.assisted=false;g.burnout=false;Input in;in.clutch=0;in.throttle=1;
        auto run=[&](int gear,float v,Input i2){Transmission t;const float c=i2.clutch;i2.gear=gear;i2.clutch=1;t.step(i2,v,1.f/30,g,top);i2.clutch=c;
            for(int i=0;i<60;++i)t.step(i2,v,1.f/30,g,top);return t.out;};
        Output fifth=run(5,top,in);
        check(std::fabs(fifth.drive-1)<.02f,"top gear gives exactly native thrust at native top speed");
        check(fifth.rpm<g.redline*.9f,"top gear reaches native top speed well below the limiter");
        check(run(5,top*1.1f,in).drive>.8f,"no limiter cut just above native top speed (chase banding)");
        check(run(4,top*.8f,in).drive>run(5,top*.8f,in).drive,"fourth pulls harder than fifth");
        check(run(3,top*.5f,in).drive>1.2f,"third gear multiplies Driver's thrust");
        check(run(1,5,in).drive>run(2,5,in).drive && run(2,15,in).drive>run(3,15,in).drive,"each lower gear pulls harder at equal speed");
        float last=10;bool smooth=true;
        for(float v=10;v<36;v+=.25f){float d=run(2,v,in).drive;if(d>last+.05f)smooth=false;last=d;}
        check(smooth && last<.05f,"soft limiter fades second gear without surges");
        check(run(2,top*.49f,in).shiftUp && !run(2,top*.2f,in).shiftUp,"upshift hint only near the top of second");
        check(!run(5,top,in).shiftUp,"no upshift hint in top gear");
        Input slightly=in;slightly.clutch=.2f;
        check(std::fabs(run(3,30,slightly).drive-run(3,30,in).drive)<.001f,"resting foot on the clutch costs nothing");
        Input bite=in;bite.clutch=g.bite;check(run(3,30,bite).drive==0,"clutch fully disengaged at the bite point");
        Setup flat=g;flat.gearTorque=0;Transmission t;Input i2=in;i2.gear=2;i2.clutch=1;t.step(i2,20,1.f/30,flat,top);i2.clutch=0;
        for(int i=0;i<60;++i)t.step(i2,20,1.f/30,flat,top);
        check(t.out.drive<1.1f,"gear_torque=0 restores Driver's constant thrust");
        // Quick H-pattern shift: the lever reaches the gate as the pedal comes up.
        Transmission q;Input s=in;s.gear=2;s.clutch=1;q.step(s,15,1.f/30,g,top);s.clutch=0;q.step(s,15,1.f/30,g,top);
        s.gear=0;for(int i=0;i<3;++i)q.step(s,15,1.f/30,g,top);s.gear=3;s.clutch=.1f;
        for(int i=0;i<3;++i)q.step(s,15,1.f/30,g,top);
        check(q.out.gear==3 && !q.out.rejectedShift,"fast shift accepted with the clutch already rising");
        s.gear=0;q.step(s,15,1.f/30,g,top);for(int i=0;i<20;++i)q.step(s,15,1.f/30,g,top);s.gear=4;q.step(s,15,1.f/30,g,top);
        check(q.out.gear==0 && q.out.rejectedShift,"shift long after the clutch was released is refused");
        // Flooring it in first after the burnout never gives less than X.
        Setup b=g;b.assisted=true;b.burnout=true;Transmission l;Input f=in;f.gear=1;
        for(int i=0;i<60;++i)l.step(f,34,1.f/30,b,top);
        check(!l.out.burnout && l.out.drive>=.99f,"first after burnout keeps native acceleration");
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
