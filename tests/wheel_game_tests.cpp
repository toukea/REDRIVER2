// Exercise the real game bridge with a deterministic input device and game state.
#include "../src_rebuild/Game/C/wheel.c"
#include <cstdlib>
#include <cstdarg>
PLAYER player[MAX_PLAYERS]={};
CAR_DATA car_data[MAX_CARS+2]={};
int CameraCnt=0,gStopPadReads=0,gCantDrive=0,gInGameCutsceneActive=0,pauseflag=0;
int MaxPlayerDamage[2]={10000,10000};
char NoPlayerControl=0;
WheelSettings g_wheelSettings;
racing::Input testInput;
bool connected=true;
int forceCount=0,stopCount=0;
bool Wheel_Read(racing::Input& value){value=testInput;return connected;}
void Wheel_Stop(){++stopCount;}
void Wheel_Force(float,float){++forceCount;}
int GetPlayerId(CAR_DATA*){return 0;}
int PrintString(char*,int,int){return 0;}
void PsyX_Log_Error(const char*,...){}
int tests=0;
void require(bool value,const char* message){++tests;if(!value){fprintf(stderr,"FAIL: %s\n",message);exit(1);}}
int main(){
    CAR_COSMETICS cosmetics={};cosmetics.powerRatio=4096;cosmetics.traction=4096;
    auto* cp=&car_data[0];cp->id=0;cp->controlType=CONTROL_TYPE_PLAYER;cp->ap.carCos=&cosmetics;
    MainPlayer.playerType=PLAYER_TYPE_CAR;MainPlayer.playerCarId=0;
    testInput.steering=.5f;testInput.throttle=.7f;testInput.brake=.3f;testInput.clutch=1;testInput.handbrake=.4f;testInput.gear=1;
    Wheel_GameApply(cp);
    require(Wheel_GameActive(cp),"wheel bridge active");
    require(cp->wheel_angle>190 && cp->wheel_angle<210,"half steering bypasses PSX quantization");
    require(Wheel_GameBrake(cp)>.29f && Wheel_GameBrake(cp)<.31f,"analog brake preserved");
    require(Wheel_GameHandbrake(cp)==1 && cp->handbrake==1,"30 percent pull triggers Triangle handbrake");
    const int steer=cp->wheel_angle, rpm=cp->hd.revs;
    const uint32_t base[]={WHEEL_REPLAY_MAGIC,123,456,789};
    const char* file="src_rebuild/bin/tests/wheel-test.D2RP";
    require(Wheel_ReplaySave(file,base,sizeof(base)),"save wheel replay");
    Wheel_ReplayReset();require(!Wheel_GameActive(cp),"clear stale state");
    require(Wheel_ReplayLoad(file,base,sizeof(base)),"load matching replay");
    NoPlayerControl=1;connected=false;g_wheelSettings.steeringLock=10;testInput=racing::Input();
    Wheel_GameApply(cp);
    require(cp->wheel_angle==steer && cp->hd.revs==rpm,"playback independent of hardware and settings");
    require(Wheel_GameBrake(cp)>.29f && Wheel_GameHandbrake(cp)==1,"pedals and Triangle latch preserved in playback");
    Wheel_GameFeedback();require(!forceCount,"replay does not send force to hardware");
    uint32_t wrong[]={WHEEL_REPLAY_MAGIC,1,2,3};
    require(!Wheel_ReplayLoad(file,wrong,sizeof(wrong)),"mismatched base replay rejected");
    FILE* truncated=fopen("src_rebuild/bin/tests/wheel-test.D2RP.wheel","wb");require(truncated!=nullptr,"create truncated sidecar fixture");fputc(0,truncated);fclose(truncated);
    require(!Wheel_ReplayLoad(file,base,sizeof(base)),"truncated companion rejected");
    uint32_t legacy[]={0,1,2,3};require(Wheel_ReplayLoad(file,legacy,sizeof(legacy)),"legacy replay ignores unrelated companion");
    remove("src_rebuild/bin/tests/wheel-test.D2RP.wheel");
    require(!Wheel_ReplayLoad(file,base,sizeof(base)),"missing required companion rejected");
    require(Wheel_ReplayLoad(file,legacy,sizeof(legacy)),"legacy replay accepted without companion");
    NoPlayerControl=0;connected=true;testInput.gear=1;testInput.clutch=0;testInput.throttle=1;
    Wheel_GameApply(cp);connected=false;Wheel_GameApply(cp);
    require(cp->thrust==0 && Wheel_GameBrake(cp)>.99f,"disconnect releases throttle and applies brake");
    pauseflag=1;int stops=stopCount;Wheel_GameFeedback();require(stopCount>stops,"pause stops feedback");
    gStopPadReads=1;Wheel_GameApply(cp);require(!Wheel_GameActive(cp),"mission input lock respected");
    gStopPadReads=0;connected=true;testInput.handbrake=.35f;Wheel_GameApply(cp);
    require(cp->handbrake==1,"handbrake engages above threshold");
    testInput.handbrake=.25f;Wheel_GameApply(cp);require(cp->handbrake==1,"handbrake hysteresis holds near threshold");
    testInput.handbrake=.15f;Wheel_GameApply(cp);require(cp->handbrake==0,"handbrake releases below lower threshold");
    testInput.handbrake=.25f;Wheel_GameApply(cp);require(cp->handbrake==0,"partial pull below threshold does not reengage");
    pauseflag=0;MainPlayer.playerType=PLAYER_TYPE_CAR;testInput.handbrake=0;testInput.steering=.5f;Wheel_GameApply(cp);
    int sent=forceCount;Wheel_GameContact(cp,0,-2000,20,0);Wheel_GameFeedback();require(forceCount>sent,"front contact produces feedback command");
    printf("PASS: %d game integration and replay checks\n",tests);
}
