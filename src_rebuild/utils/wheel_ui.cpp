#include "wheel.h"
#include <SDL.h>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cmath>

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
        if(ch==':'){SDL_Rect a={x+4,y+3,2,2},b={x+4,y+10,2,2};SDL_RenderFillRect(r,&a);SDL_RenderFillRect(r,&b);}
        if(ch=='+'){SDL_Rect a={x,y+6,8,2},b={x+3,y+3,2,8};SDL_RenderFillRect(r,&a);SDL_RenderFillRect(r,&b);}
        if(ch=='_'){SDL_Rect p={x,y+13,10,2};SDL_RenderFillRect(r,&p);}
        if(ch=='/')for(int k=0;k<7;++k){SDL_Rect p={x+8-k,y+k*2,2,2};SDL_RenderFillRect(r,&p);}
        if(ch==','){SDL_Rect p={x+3,y+12,2,3};SDL_RenderFillRect(r,&p);}
    }
}
// ---------------------------------------------------------------------------
// Force feedback page, in the spirit of Content Manager: sliders, presets,
// response curve, live preview on the wheel and automatic direction check.
SDL_HapticEffect uiCondition(Uint16 type,float level){
    SDL_HapticEffect e=SDL_HapticEffect();e.type=type;
    e.condition.direction.type=SDL_HAPTIC_CARTESIAN;e.condition.direction.dir[0]=1;
    e.condition.length=SDL_HAPTIC_INFINITY;
    e.condition.right_sat[0]=e.condition.left_sat[0]=0xFFFF;
    e.condition.right_coeff[0]=e.condition.left_coeff[0]=static_cast<Sint16>(racing::clamp(level,0,1)*32767);
    return e;
}
struct Param { const char* label; float* value; float lo,hi,step; const char* help1; const char* help2; };
struct Preset { const char* name; float v[10]; };
const Preset presets[]={
    {"DOUX",{.65f,.60f,.15f,.06f,.15f,.10f,.40f,.06f,.35f,.70f}},
    {"EQUILIBRE",{.80f,.70f,.25f,.08f,.12f,.15f,.25f,.08f,.50f,.75f}},
    {"REALISTE",{.85f,.80f,.30f,.08f,.08f,.18f,.15f,.05f,.65f,.80f}},
    {"FORT",{1.f,.75f,.35f,.10f,.10f,.20f,.25f,.10f,.55f,.70f}},
};
const int barX=330,barW=300,rowY=100,rowH=34,graphX=750,graphY=110,graphW=320,graphH=220;
struct ForcePage {
    SDL_Joystick* wheel=nullptr;
    SDL_Haptic* haptic=nullptr;SDL_Joystick* hapticFor=nullptr;
    int constId=-1,damperId=-1,frictionId=-1;
    SDL_HapticEffect constant=SDL_HapticEffect();
    bool preview=false,dragging=false;int detecting=0;Uint32 detectStart=0,lastSend=0;float detectFrom=0;
    float previewSpeed=15,lastOutput=0,lastSlip=0,lastDamper=-1,lastFriction=-1;
    racing::ForceShaper shaper;
    int selected=0;
    const char* status="TAB : RETOUR AUX ENTREES";
    std::vector<Param> params;
    ForcePage(){
        auto& f=g_wheelSettings.force;
        params={
            {"FORCE GLOBALE",&f.gain,0,1.5f,.05f,"FORCE DE TOUT LE RETOUR. AU DELA DE 1 LES PICS SONT COMPRESSES.","TROP MOU PARTOUT : AUGMENTER. TROP DUR PARTOUT : DIMINUER."},
            {"LISSAGE",&f.filter,.05f,1,.05f,"LISSAGE DU COUPLE DES PNEUS. 1 = AUCUN LISSAGE.","GRAIN OU A-COUPS : DIMINUER. RETARD RESSENTI : AUGMENTER."},
            {"EFFETS DE ROUTE",&f.road,0,1,.05f,"BOSSES, COMPRESSION DES SUSPENSIONS, HERBE ET SABLE.","TROP DE VIBRATIONS : DIMINUER."},
            {"FORCE MINIMALE",&f.minimum,0,.25f,.01f,"COMPENSE LA ZONE MORTE DES MOTEURS DU G29 AU CENTRE.","CENTRE MORT : 0.10 A 0.12. CLIQUETIS AU CENTRE : 0.05."},
            {"AMORTISSEMENT",&f.damping,0,1,.02f,"AMORTISSEUR EXECUTE PAR LE VOLANT LUI MEME.","OSCILLATIONS EN LIGNE DROITE : AUGMENTER. VISQUEUX : DIMINUER."},
            {"FROTTEMENT",&f.friction,0,1,.02f,"FROTTEMENT DES PNEUS A L ARRET, DISPARAIT VERS 30 KMH.","MANOEUVRES TROP LOURDES : DIMINUER."},
            {"CHASSE MECANIQUE",&f.caster,0,2,.05f,"POIDS CONSERVE QUAND L AVANT DECROCHE.","ALLEGEMENT EN SOUS VIRAGE TROP BRUTAL : AUGMENTER."},
            {"RAPPEL AU CENTRE",&f.selfCenter,0,1,.01f,"RAPPEL GEOMETRIQUE INDEPENDANT DE LA VITESSE.","VOLANT FLOTTANT A BASSE VITESSE : AUGMENTER."},
            {"TRANSFERT DE CHARGE",&f.load,0,1,.05f,"DIRECTION PLUS LOURDE AU FREINAGE, PLUS LEGERE EN ACCELERATION.","0 = DESACTIVE."},
            {"COMPRESSION",&f.knee,.3f,1,.05f,"NIVEAU OU COMMENCE LA COMPRESSION DES PICS.","CLIP AFFICHE SOUVENT EN JEU : BAISSER D ABORD LA FORCE GLOBALE."},
        };
    }
    int rows() const { return int(params.size())+3; }
    int* toggle(int row){ int t=row-int(params.size()); return t==0?&g_wheelSettings.forceInvert:t==1?&g_wheelSettings.ffbHud:&g_wheelSettings.ffbLog; }
    const char* toggleLabel(int row) const { int t=row-int(params.size()); return t==0?"INVERSER LE SENS":t==1?"AFFICHAGE EN JEU":"TELEMETRIE CSV"; }
    const char* toggleHelp(int row) const { int t=row-int(params.size());
        return t==0?"SENS DU RETOUR DE FORCE. D DETECTE LE BON SENS AUTOMATIQUEMENT.":t==1?"AFFICHE FFB, GRIP ET CLIP PENDANT LA CONDUITE.":"ENREGISTRE DATA/FFB_TELEMETRY.CSV POUR ANALYSER LES REGLAGES."; }
    float steering(){
        const auto& a=g_wheelSettings.axes[0];
        if(!wheel || a.index<0 || a.index>=SDL_JoystickNumAxes(wheel)) return 0;
        return a.read(SDL_JoystickGetAxis(wheel,a.index),true);
    }
    void closeHaptic(){
        stop();
        if(haptic){SDL_HapticClose(haptic);} // closing also destroys its effects
        haptic=nullptr;hapticFor=nullptr;constId=damperId=frictionId=-1;
    }
    bool openHaptic(){
        if(haptic && hapticFor==wheel) return true;
        closeHaptic();
        if(!wheel) {status="AUCUN VOLANT CONNECTE";return false;}
        haptic=SDL_HapticOpenFromJoystick(wheel);
        if(!haptic || !(SDL_HapticQuery(haptic)&SDL_HAPTIC_CONSTANT)){
            if(haptic)SDL_HapticClose(haptic);haptic=nullptr;status="CE VOLANT N A PAS DE RETOUR DE FORCE UTILISABLE";return false;
        }
        hapticFor=wheel;
        SDL_HapticSetAutocenter(haptic,0);
        if(SDL_HapticQuery(haptic)&SDL_HAPTIC_GAIN)SDL_HapticSetGain(haptic,100);
        constant=SDL_HapticEffect();constant.type=SDL_HAPTIC_CONSTANT;
        constant.constant.direction.type=SDL_HAPTIC_CARTESIAN;constant.constant.direction.dir[0]=1;constant.constant.length=150;
        constId=SDL_HapticNewEffect(haptic,&constant);
        unsigned features=SDL_HapticQuery(haptic);
        if(features&SDL_HAPTIC_DAMPER){SDL_HapticEffect e=uiCondition(SDL_HAPTIC_DAMPER,0);damperId=SDL_HapticNewEffect(haptic,&e);}
        if(features&SDL_HAPTIC_FRICTION){SDL_HapticEffect e=uiCondition(SDL_HAPTIC_FRICTION,0);frictionId=SDL_HapticNewEffect(haptic,&e);}
        return constId>=0;
    }
    void sendConstant(float level){
        if(!haptic||constId<0)return;
        constant.constant.level=static_cast<Sint16>(racing::clamp(level,-1,1)*32767);
        SDL_HapticUpdateEffect(haptic,constId,&constant);SDL_HapticRunEffect(haptic,constId,1);
    }
    void sendCondition(int id,Uint16 type,float level,float& current){
        if(!haptic||id<0||(current>=0&&std::fabs(level-current)<.01f))return;
        SDL_HapticEffect e=uiCondition(type,level);
        if(SDL_HapticUpdateEffect(haptic,id,&e)<0)return;
        if(current<0)SDL_HapticRunEffect(haptic,id,1);
        current=level;
    }
    void stop(){
        if(haptic){if(constId>=0)SDL_HapticStopEffect(haptic,constId);if(damperId>=0)SDL_HapticStopEffect(haptic,damperId);if(frictionId>=0)SDL_HapticStopEffect(haptic,frictionId);}
        preview=false;detecting=0;lastDamper=lastFriction=-1;lastOutput=0;lastSlip=0;shaper.reset();
    }
    bool busy() const { return preview||detecting; }
    void applyPreset(int i){
        float* targets[10];for(int p=0;p<10;++p)targets[p]=params[p].value;
        for(int p=0;p<10;++p)*targets[p]=presets[i].v[p];
        status="PRESET APPLIQUE. S POUR ENREGISTRER";
    }
    void setValue(int row,float v){
        if(row>=int(params.size()))return;
        Param& p=params[row];
        v=std::round(v/p.step)*p.step;*p.value=racing::clamp(v,p.lo,p.hi);
    }
    void adjust(int dir,bool fine){
        if(selected>=int(params.size())){int* t=toggle(selected);*t=!*t;return;}
        Param& p=params[selected];
        float step=fine?p.step/5:p.step;*p.value=racing::clamp(*p.value+dir*step,p.lo,p.hi);
    }
    void mouse(int x,int y,bool press){
        int row=(y-rowY+8)/rowH;if(y<rowY-8||row<0||row>=rows())return;
        if(press)selected=row;
        if(row>=int(params.size())){if(press && x>=barX && x<barX+barW){int* t=toggle(row);*t=!*t;}return;}
        if(press && (x<barX||x>barX+barW))return;
        if(press)dragging=true;
        if(dragging && row==selected){Param& p=params[row];setValue(row,p.lo+(p.hi-p.lo)*racing::clamp(float(x-barX)/barW,0,1));}
    }
    void handle(const SDL_Event& e){
        if(e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT)mouse(e.button.x,e.button.y,true);
        if(e.type==SDL_MOUSEMOTION && dragging)mouse(e.motion.x,e.motion.y,false);
        if(e.type==SDL_MOUSEBUTTONUP)dragging=false;
        if(e.type!=SDL_KEYDOWN)return;
        SDL_Keycode key=e.key.keysym.sym;bool fine=(e.key.keysym.mod&KMOD_SHIFT)!=0;
        if(key==SDLK_UP)selected=(selected+rows()-1)%rows();
        if(key==SDLK_DOWN)selected=(selected+1)%rows();
        if(key==SDLK_LEFT)adjust(-1,fine);
        if(key==SDLK_RIGHT)adjust(1,fine);
        if(key>=SDLK_F1 && key<=SDLK_F4)applyPreset(int(key-SDLK_F1));
        if(key==SDLK_r){applyPreset(1);status="VALEURS PAR DEFAUT. S POUR ENREGISTRER";}
        if(key==SDLK_v){previewSpeed=previewSpeed<1?15:previewSpeed<20?30:0;}
        if(key==SDLK_p){
            if(preview){stop();status="APERCU ARRETE";}
            else if(openHaptic()){stop();preview=true;status="APERCU : TOURNEZ LE VOLANT COMME EN VIRAGE. P POUR ARRETER";}
        }
        if(key==SDLK_d && openHaptic()){
            stop();detecting=1;detectStart=SDL_GetTicks();detectFrom=steering();
            status="DETECTION : LACHEZ LE VOLANT AU CENTRE...";
        }
    }
    void tick(){
        if(haptic && hapticFor!=wheel)closeHaptic();
        Uint32 now=SDL_GetTicks();
        if(detecting){
            if(now-detectStart<450){sendConstant(.35f);return;}
            float delta=steering()-detectFrom;stop();
            if(delta>.01f){g_wheelSettings.forceInvert=0;status="SENS CORRECT : INVERSION DESACTIVEE. S POUR ENREGISTRER";}
            else if(delta<-.01f){g_wheelSettings.forceInvert=1;status="SENS INVERSE DETECTE : INVERSION ACTIVEE. S POUR ENREGISTRER";}
            else status="AUCUN MOUVEMENT. VERIFIEZ G HUB ET LACHEZ LE VOLANT PUIS REESSAYEZ";
            return;
        }
        if(!preview||now-lastSend<33)return;
        lastSend=now;
        // Simulated corner: front slip grows with steering and speed, as in the game.
        const auto& f=g_wheelSettings.force;
        float wheelPos=steering();
        float steer=racing::clamp(wheelPos*racing::clamp(g_wheelSettings.hardwareRotation,180,2520)/racing::clamp(g_wheelSettings.rotation,180,2520),-1,1);
        float slip=racing::clamp(-steer*3*std::min(1.f,previewSpeed/15),-1,1);
        racing::ForceFrame frame;frame.speed=previewSpeed;
        frame.torque=previewSpeed>0?racing::alignTorque(slip,f.caster):0;
        frame.torque-=steer*racing::clamp(f.selfCenter,0,1);
        racing::ForceReport r=shaper.step(frame,f);
        float level=racing::deadbandCompensate(r.output,f.minimum);
        sendConstant(level*(g_wheelSettings.forceInvert?-1:1));
        racing::ConditionLevels c=racing::conditionLevels(previewSpeed,f);
        sendCondition(damperId,SDL_HAPTIC_DAMPER,c.damper,lastDamper);
        sendCondition(frictionId,SDL_HAPTIC_FRICTION,c.friction,lastFriction);
        lastOutput=level;lastSlip=std::fabs(slip);
    }
    float curve(float slip,float load) const {
        const auto& f=g_wheelSettings.force;
        float t=racing::alignTorque(racing::clamp(slip,0,1),f.caster)*load;
        return racing::deadbandCompensate(racing::softClip(racing::clamp(f.gain,0,1.5f)*t,f.knee),f.minimum);
    }
    void draw(SDL_Renderer* r){
        char line[120];
        SDL_SetRenderDrawColor(r,220,230,240,255);
        text(r,24,20,"REDRIVER2 RETOUR DE FORCE");
        text(r,24,50,"TAB : ENTREES ET CALIBRATION");
        for(int row=0;row<rows();++row){
            int y=rowY+row*rowH;bool sel=row==selected;
            SDL_SetRenderDrawColor(r,sel?255:165,sel?210:180,110,255);
            if(row<int(params.size())){
                const Param& p=params[row];text(r,24,y,p.label);
                SDL_SetRenderDrawColor(r,45,55,72,255);SDL_Rect track={barX,y,barW,14};SDL_RenderFillRect(r,&track);
                SDL_SetRenderDrawColor(r,sel?255:120,sel?190:160,sel?90:210,255);
                SDL_Rect fill={barX,y,int(barW*(*p.value-p.lo)/(p.hi-p.lo)),14};SDL_RenderFillRect(r,&fill);
                SDL_SetRenderDrawColor(r,sel?255:165,sel?210:180,110,255);
                snprintf(line,sizeof(line),"%.2F",*p.value);text(r,barX+barW+14,y,line);
            } else {
                text(r,24,y,toggleLabel(row));text(r,barX,y,*toggle(row)?"OUI":"NON");
            }
        }
        // Response curve: output sent to the wheel against front slip (100 = grip limit).
        SDL_SetRenderDrawColor(r,32,40,54,255);SDL_Rect area={graphX,graphY,graphW,graphH};SDL_RenderFillRect(r,&area);
        SDL_SetRenderDrawColor(r,70,82,100,255);
        int limitX=graphX+int(graphW/1.2f);SDL_RenderDrawLine(r,limitX,graphY,limitX,graphY+graphH);
        const auto& f=g_wheelSettings.force;
        int kneeY=graphY+graphH-int(graphH*racing::clamp(f.knee,0,1));
        for(int x=graphX;x<graphX+graphW;x+=8)SDL_RenderDrawLine(r,x,kneeY,x+3,kneeY);
        float braking=racing::clamp(1+f.load*.55f,.6f,1.4f);
        for(int pass=0;pass<2;++pass){
            if(pass==0)SDL_SetRenderDrawColor(r,110,160,230,255);else SDL_SetRenderDrawColor(r,255,200,90,255);
            int px=-1,py=-1;
            for(int x=0;x<graphW;++x){
                float s=x*1.2f/graphW;float v=curve(s,pass==0?braking:1);
                int X=graphX+x,Y=graphY+graphH-int(graphH*racing::clamp(v,0,1));
                if(px>=0){SDL_RenderDrawLine(r,px,py,X,Y);SDL_RenderDrawLine(r,px,py+1,X,Y+1);}
                px=X;py=Y;
            }
        }
        if(preview){
            SDL_SetRenderDrawColor(r,120,255,140,255);
            SDL_Rect dot={graphX+int(graphW*lastSlip/1.2f)-4,graphY+graphH-int(graphH*racing::clamp(std::fabs(lastOutput),0,1))-4,8,8};SDL_RenderFillRect(r,&dot);
        }
        SDL_SetRenderDrawColor(r,205,215,230,255);
        text(r,graphX,graphY-30,"COURBE DE REPONSE");
        SDL_SetRenderDrawColor(r,255,200,90,255);text(r,graphX,graphY+graphH+10,"NORMAL");
        SDL_SetRenderDrawColor(r,110,160,230,255);text(r,graphX+100,graphY+graphH+10,"FREINAGE");
        SDL_SetRenderDrawColor(r,140,150,165,255);text(r,graphX+226,graphY+graphH+10,"LIMITE");
        SDL_SetRenderDrawColor(r,205,215,230,255);
        text(r,graphX,graphY+graphH+44,"PRESETS");
        for(int i=0;i<4;++i){snprintf(line,sizeof(line),"F%d %s",i+1,presets[i].name);text(r,graphX+(i%2)*170,graphY+graphH+72+(i/2)*26,line);}
        snprintf(line,sizeof(line),"APERCU %s  %d KMH",preview?"ACTIF":"ARRETE",int(previewSpeed*3.6f+.5f));
        text(r,graphX,graphY+graphH+136,line);
        SDL_SetRenderDrawColor(r,45,55,72,255);SDL_Rect meter={graphX,graphY+graphH+162,graphW,14};SDL_RenderFillRect(r,&meter);
        SDL_SetRenderDrawColor(r,120,255,140,255);SDL_Rect level={graphX+graphW/2,graphY+graphH+162,int(graphW/2*racing::clamp(lastOutput,-1,1)),14};
        if(level.w<0){level.x+=level.w;level.w=-level.w;}SDL_RenderFillRect(r,&level);
        SDL_SetRenderDrawColor(r,170,185,205,255);
        const char* h1=selected<int(params.size())?params[selected].help1:toggleHelp(selected);
        const char* h2=selected<int(params.size())?params[selected].help2:"";
        text(r,24,560,h1);text(r,24,586,h2);
        SDL_SetRenderDrawColor(r,205,215,230,255);
        text(r,24,630,"HAUT BAS CHOISIR  GAUCHE DROITE OU SOURIS REGLER  MAJ FIN");
        text(r,24,656,"P APERCU AU VOLANT  V VITESSE  D DETECTER LE SENS  R DEFAUT");
        text(r,24,682,"S ENREGISTRER  ECHAP QUITTER");
        SDL_SetRenderDrawColor(r,255,210,110,255);text(r,24,716,status);
    }
};
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
    int page=0;
    ForcePage force;
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
            if(e.type==SDL_JOYDEVICEADDED || e.type==SDL_JOYDEVICEREMOVED){force.closeHaptic();if(calibrating>=0)g_wheelSettings.axes[calibrating]=previousCalibration;discover();binding=-1;calibrating=-1;}
            if(e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_TAB && binding<0 && calibrating<0){force.stop();page=!page;continue;}
            if(page==1){
                if(e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_ESCAPE){if(force.busy()){force.stop();force.status="ARRETE";}else running=false;continue;}
                if(e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_s){g_wheelSettings.enabled=1;force.status=Wheel_SaveConfig(configPath)?"ENREGISTRE DANS WHEEL.INI. LE JEU L APPLIQUE EN DIRECT":"ECHEC DE L ENREGISTREMENT";continue;}
                force.handle(e);continue;
            }
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
            if(key==SDLK_EQUALS)g_wheelSettings.force.gain=racing::clamp(g_wheelSettings.force.gain+.05f,0,1);
            if(key==SDLK_MINUS)g_wheelSettings.force.gain=racing::clamp(g_wheelSettings.force.gain-.05f,0,1);
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
        {int w=devIndex(0);force.wheel=w>=0?joys[w]:nullptr;}
        if(page==1){force.tick();SDL_SetRenderDrawColor(renderer,18,23,33,255);SDL_RenderClear(renderer);force.draw(renderer);SDL_RenderPresent(renderer);SDL_Delay(16);continue;}
        if(calibrating>=0){int d=devIndex(calibrating==0?0:calibrating==4?3:1);if(d>=0){auto& a=g_wheelSettings.axes[calibrating];int raw=SDL_JoystickGetAxis(joys[d],a.index);a.minimum=std::min(a.minimum,raw);a.maximum=std::max(a.maximum,raw);}}
        SDL_SetRenderDrawColor(renderer,18,23,33,255);SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer,220,230,240,255);
        text(renderer,24,20,"REDRIVER2 RACING WHEEL CALIBRATION");
        char line[100];snprintf(line,sizeof(line),"DEVICES %d  ROTATION %.0F  FFB %.0F PERCENT  CLUTCH %s",int(joys.size()),g_wheelSettings.rotation,g_wheelSettings.force.gain*100,g_wheelSettings.transmission.assisted?"ASSISTED":"MANUAL");text(renderer,24,50,line);
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
        text(renderer,24,600,"TAB FORCE FEEDBACK SETTINGS");
        text(renderer,24,630,status);
        int d=devIndex(0);if(d>=0)text(renderer,24,680,SDL_JoystickName(joys[d]));
        SDL_RenderPresent(renderer);SDL_Delay(16);
    }
    force.closeHaptic();
    for(auto* j:joys)if(j)SDL_JoystickClose(j);
    SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);
    return 0;
}
