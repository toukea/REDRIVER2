#ifndef PSX
#include "driver2.h"
#include "ASM/rndrasm.h"
#include "cockpit.h"
#include "camera.h"
#include "convert.h"
#include "draw.h"
#include "cars.h"
#include "players.h"
#include "mission.h"
#include "main.h"
#include "map.h"
#include "system.h"
#include "wheel.h"
#include "../../utils/ini.h"
#include <cstdlib>

namespace {
ini_t* config=nullptr;
bool enabled=true,showDriver=true;
cockpit::Mesh fixedMesh, movingMesh;
cockpit::Profile cached={};
bool cachedValid=false;
float number(const char* section,const char* key,float fallback,float lo,float hi) {
    const char* s=config?ini_get(config,section,key):nullptr;
    if(!s)return fallback;
    char* end=nullptr;float v=std::strtof(s,&end);
    return end==s || !std::isfinite(v)?fallback:cockpit::bound(v,lo,hi);
}
int modelId(const CAR_DATA* car) {
    int slot=car->ap.model;
    if(slot<0 || slot>=MAX_CAR_RESIDENT_MODELS)return -1;
    int model=residentCarModels[slot];
    if(model==13) {model=10-residentCarModels[0]-residentCarModels[1]-residentCarModels[2];model=MAX(1,MIN(4,model));}
    return model;
}
cockpit::Vec transform(cockpit::Vec v,const MATRIX& m) {
    return {(m.m[0][0]*v.x+m.m[0][1]*v.y+m.m[0][2]*v.z)/4096.f,
            (m.m[1][0]*v.x+m.m[1][1]*v.y+m.m[1][2]*v.z)/4096.f,
            (m.m[2][0]*v.x+m.m[2][1]*v.y+m.m[2][2]*v.z)/4096.f};
}
void drawMesh(const cockpit::Mesh& mesh,const MATRIX& rotation,cockpit::Vec translation) {
    MATRIX identity;InitMatrix(identity);VECTOR zero={0};
    gte_SetRotMatrix(&identity);gte_SetTransVector(&zero);
    for(const auto& tri:mesh.triangles) {
        cockpit::Vec source[3]={transform(tri.a*-1,rotation)+translation,transform(tri.b*-1,rotation)+translation,transform(tri.c*-1,rotation)+translation};
        cockpit::Vec clipped[5];int count=cockpit::clipNear(source,3,clipped,12);
        for(int j=1;j<count-1;++j) {
            if(current->primptr+sizeof(POLY_F3)>current->primtab+PRIMTAB_SIZE-4096)return;
            cockpit::Vec v[3]={clipped[0],clipped[j],clipped[j+1]};
            SVECTOR a={short(v[0].x),short(v[0].y),short(v[0].z),0};
            SVECTOR b={short(v[1].x),short(v[1].y),short(v[1].z),0};
            SVECTOR c={short(v[2].x),short(v[2].y),short(v[2].z),0};
            POLY_F3* poly=(POLY_F3*)current->primptr;
            gte_ldv3(&a,&b,&c);gte_rtpt();
            setPolyF3(poly);setRGB0(poly,tri.color.r,tri.color.g,tri.color.b);
            gte_stsxy3(&poly->x0,&poly->x1,&poly->x2);
            // Same depth scale as world/car primitives, preserving HUD ordering.
            int bucket=MAX(5,MIN(OTSIZE-1,int((v[0].z+v[1].z+v[2].z)/24)));
            addPrim(current->ot+bucket,poly);
            current->primptr+=sizeof(POLY_F3);
        }
    }
}
}
void Cockpit_LoadConfig() {
    if(config)ini_free(config);
    config=ini_load("cockpit.ini");cachedValid=false;
    enabled=number("cockpit","enabled",1,0,1)!=0;
    showDriver=number("cockpit","driver",1,0,1)!=0;
}
bool Cockpit_Enabled(){return enabled;}
int Cockpit_ViewPitch(){return int(number("cockpit","pitch",6,-20,30)*4096/360);}
cockpit::Profile Cockpit_Profile(const CAR_DATA* car) {
    const auto& box=car->ap.carCos->colBox;
    char section[64];snprintf(section,sizeof(section),"%s.%d",LevelNames[GameLevel],modelId(car));
    int family=int(number(section,"family",float(cockpit::classify(box.vy,box.vz)),0,2));
    auto p=cockpit::profile(cockpit::Family(family),
        number(section,"half_width",float(box.vx),65,220),
        number(section,"half_height",float(box.vy),65,320),
        number(section,"half_length",float(box.vz),140,1400));
    p.eye.x=number(section,"eye_x",p.eye.x,-p.width+12,p.width-12);
    p.eye.y=number(section,"eye_y",p.eye.y,p.floor+50,p.roof-8);
    p.eye.z=number(section,"eye_z",p.eye.z,p.rear+40,p.front-55);
    p.wheel=p.eye+cockpit::Vec(0,-32,78);
    p.wheel.x=number(section,"wheel_x",p.wheel.x,-p.width+25,p.width-25);
    p.wheel.y=number(section,"wheel_y",p.wheel.y,p.floor+35,p.eye.y-15);
    p.wheel.z=number(section,"wheel_z",p.wheel.z,p.eye.z+35,p.front-10);
    p.radius=number(section,"wheel_radius",p.radius,16,32);
    p.tilt=number(section,"wheel_tilt",p.tilt,0,65);
    return p;
}
void Cockpit_EyeOffset(const CAR_DATA* car,VECTOR* offset) {
    auto eye=Cockpit_Profile(car).eye;
    offset->vx=-int(eye.x);offset->vy=-int(eye.y);offset->vz=-int(eye.z);
}
void Cockpit_Draw(int view) {
    const PLAYER& lp=player[view];
    if(!enabled || lp.cameraView!=CAMERA_VIEW_COCKPIT || lp.cameraCarId<0 || lp.cameraCarId>=MAX_CARS)return;
    CAR_DATA* car=&car_data[lp.cameraCarId];if(!car->ap.carCos)return;
    auto p=Cockpit_Profile(car);
    if(!cachedValid || cached.family!=p.family || cached.width!=p.width || cached.floor!=p.floor || cached.roof!=p.roof || cached.front!=p.front || cached.rear!=p.rear || cached.eye.x!=p.eye.x || cached.eye.y!=p.eye.y || cached.eye.z!=p.eye.z) {
        cockpit::cabin(fixedMesh,p);cached=p;cachedValid=true;
    }
    movingMesh.triangles.clear();
    float angle=Wheel_GameSteeringDegrees(car);
    float rpm=Wheel_GameActive(car)?Wheel_GameRPM(car)*6500.f/30000:car->hd.revs*6500.f/30000;
    cockpit::controls(movingMesh,p,angle,std::fabs(car->hd.wheel_speed/4096.f*.3f)*3.6f,rpm,showDriver);
    MATRIX rotation;MulMatrix0(&inv_camera_matrix,&car->hd.drawCarMat,&rotation);
    // Match DrawCarObject's center of geometry, including the cosmetics offset.
    const auto& cog=car->ap.carCos->cog;
    cockpit::Vec origin=transform({float(cog.vx),float(cog.vy),float(cog.vz)},car->hd.drawCarMat);
    origin=origin+cockpit::Vec(float(car->hd.where.t[0]-camera_position.vx),float(-car->hd.where.t[1]-camera_position.vy),float(car->hd.where.t[2]-camera_position.vz));
    auto translation=transform(origin,inv_camera_matrix);
    drawMesh(fixedMesh,rotation,translation);drawMesh(movingMesh,rotation,translation);
    Set_Inv_CameraMatrix();SetCameraVector();
}
#endif
