#pragma once
// Original, code-generated prototype geometry. No assets from Assetto Corsa.
// Cabin coordinates: X right, Y up, Z forward, in Driver model units.
#include <cmath>
#include <vector>
#include <algorithm>

namespace cockpit {
constexpr float pi = 3.14159265358979323846f;
inline float bound(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
struct Vec {
    float x, y, z;
    Vec(float x_=0, float y_=0, float z_=0):x(x_),y(y_),z(z_){}
    Vec operator+(Vec b) const { return {x+b.x,y+b.y,z+b.z}; }
    Vec operator-(Vec b) const { return {x-b.x,y-b.y,z-b.z}; }
    Vec operator*(float s) const { return {x*s,y*s,z*s}; }
};
inline float dot(Vec a, Vec b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec cross(Vec a, Vec b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline float length(Vec a) { return std::sqrt(dot(a,a)); }
inline Vec unit(Vec a) { return a*(1.f/std::max(.0001f,length(a))); }
enum Family { TwoSeat, FourSeat, Truck };
struct Profile {
    Family family;
    float width, floor, roof, front, rear;
    Vec eye, wheel;
    float radius, tilt;
    float roofWidth, roofFront, roofRear;
    bool meshFitted=false;
};
inline Profile profile(Family family, float halfWidth, float halfHeight, float halfLength) {
    Profile p;
    p.family=family;
    p.width=bound(halfWidth*.86f,65,180);
    float h=bound(halfHeight,65,320), l=bound(halfLength,140,1400);
    p.floor=-h*.65f; p.roof=h*.92f;
    p.front=l*(family==Truck?.78f:.32f);
    p.rear=family==Truck?p.front-180.f:-l*.68f;
    p.eye={-p.width*.48f,p.roof-30,p.front-(family==Truck?100.f:125.f)};
    p.wheel=p.eye+Vec(0,-38,family==Truck?66.f:96.f);
    p.radius=family==Truck?23.f:19.f;
    p.tilt=family==Truck?50.f:20.f;
    p.roofWidth=p.width*.82f;p.roofFront=p.front-35;p.roofRear=p.rear+20;
    return p;
}
// Only use a size heuristic as a fallback; model-specific configuration wins.
inline Family classify(float halfHeight, float halfLength) {
    if(halfHeight>145 || (halfLength>580 && halfHeight>120)) return Truck;
    return halfLength<260 || halfHeight<78?TwoSeat:FourSeat;
}
// Fit the unmodified exterior mesh, not the collision box or dented car.
// Roof edges anchor the height; narrow central beacons are excluded.
inline Profile fitProfile(Family family,float halfWidth,float halfHeight,float halfLength,const std::vector<Vec>& points) {
    auto p=profile(family,halfWidth,halfHeight,halfLength);
    if(points.size()<12)return p;
    std::vector<Vec> candidates;
    float maxZ=-1e6f,minZ=1e6f;
    for(auto v:points){maxZ=std::max(maxZ,v.z);minZ=std::min(minZ,v.z);}
    for(auto v:points)if(std::isfinite(v.y) && (family!=Truck || v.z>minZ+(maxZ-minZ)*.65f))candidates.push_back(v);
    if(candidates.size()<4)return p;
    float edge=-1e6f;
    for(auto v:candidates)if(std::fabs(v.x)>=halfWidth*.5f)edge=std::max(edge,v.y);
    float roof=-1e6f;
    for(auto v:candidates)if(v.y<=edge+(family==Truck?24.f:14.f))roof=std::max(roof,v.y);
    float front=-1e6f,rear=1e6f,width=0;int count=0;
    for(auto v:candidates)if(v.y>=roof-14 && v.y<=roof+8){front=std::max(front,v.z);rear=std::min(rear,v.z);width=std::max(width,std::fabs(v.x));++count;}
    if(count<2 || width<30 || roof<35)return p;
    if(front-rear<25){if(family!=Truck)return p;rear=front-155;}
    p.roof=roof-3;p.roofWidth=bound(width-3,p.width*.5f,p.width*.98f);
    p.roofFront=front-4;p.roofRear=rear+4;
    p.front=std::min(maxZ-5,front+(family==Truck?55.f:65.f));
    p.rear=family==Truck?std::max(rear-20,front-195):rear-55;
    float eyeZ=family==Truck?bound(front-76,rear+12,front-24):front-90;
    p.eye={-p.width*.45f,p.roof-(family==Truck?36.f:28.f),eyeZ};
    p.wheel=p.eye+Vec(0,-38,family==Truck?66.f:96.f);p.meshFitted=true;
    return p;
}
// Wheel plane normal leans forward as tilt increases (truck wheel more horizontal).
inline Vec wheelPoint(const Profile& p, float angle, float radial=1, float depth=0) {
    float a=angle*pi/180, t=p.tilt*pi/180;
    float x=p.radius*radial*std::cos(a), y=p.radius*radial*std::sin(a);
    return p.wheel+Vec(x,y*std::cos(t)+depth*std::sin(t),y*std::sin(t)-depth*std::cos(t));
}
// Analytic two-bone IK: maintain limb lengths instead of stretching an arm.
inline Vec elbow(Vec shoulder, Vec wrist, float upper, float lower, int side) {
    Vec axis=unit(wrist-shoulder);
    float d=bound(length(wrist-shoulder),.001f,upper+lower-.001f);
    float along=(upper*upper-lower*lower+d*d)/(2*d);
    float height=std::sqrt(std::max(0.f,upper*upper-along*along));
    Vec pole=Vec(float(side),-.8f,-.3f);
    Vec bend=unit(pole-axis*dot(pole,axis));
    return shoulder+axis*along+bend*height;
}
struct Color { unsigned char r,g,b; };
enum Material { Solid,Plastic,Vinyl,Cloth,Wood,Metal,SpeedDial,RpmDial,Radio,Leather,Carpet,Headliner,SmallDial };
struct UV {float u,v;};
struct Triangle { Vec a,b,c; Color color; Material material; UV uv[3]; };
struct Mesh {
    std::vector<Triangle> triangles;
    Material material=Solid;
    void tri(Vec a, Vec b, Vec c, Color color) { triangles.push_back({a,b,c,color,material,{{0,0},{1,0},{1,1}}}); }
    void quad(Vec a,Vec b,Vec c,Vec d,Color color) {
        tri(a,b,c,color);tri(a,c,d,color);triangles.back().uv[1]={1,1};triangles.back().uv[2]={0,1};
    }
    void box(Vec a,Vec b,Color c) {
        Color top={static_cast<unsigned char>(std::min(255,int(c.r)+12)),static_cast<unsigned char>(std::min(255,int(c.g)+12)),static_cast<unsigned char>(std::min(255,int(c.b)+12))};
        quad({a.x,a.y,a.z},{b.x,a.y,a.z},{b.x,b.y,a.z},{a.x,b.y,a.z},c);
        quad({a.x,a.y,b.z},{a.x,b.y,b.z},{b.x,b.y,b.z},{b.x,a.y,b.z},c);
        quad({a.x,a.y,a.z},{a.x,b.y,a.z},{a.x,b.y,b.z},{a.x,a.y,b.z},c);
        quad({b.x,a.y,a.z},{b.x,a.y,b.z},{b.x,b.y,b.z},{b.x,b.y,a.z},c);
        quad({a.x,b.y,a.z},{b.x,b.y,a.z},{b.x,b.y,b.z},{a.x,b.y,b.z},top);
        quad({a.x,a.y,a.z},{a.x,a.y,b.z},{b.x,a.y,b.z},{b.x,a.y,a.z},c);
    }
    void bar(Vec a,Vec b,float radius,Color c) {
        Vec axis=unit(b-a), u=unit(cross(axis,std::fabs(axis.y)<.9f?Vec(0,1,0):Vec(1,0,0)))*radius;
        Vec v=unit(cross(axis,u))*radius;
        quad(a-u-v,b-u-v,b+u-v,a+u-v,c);
        quad(a+u-v,b+u-v,b+u+v,a+u+v,c);
        quad(a+u+v,b+u+v,b-u+v,a-u+v,c);
        quad(a-u+v,b-u+v,b-u-v,a-u-v,c);
        quad(a-u-v,a+u-v,a+u+v,a-u+v,c);
        quad(b-u-v,b-u+v,b+u+v,b+u-v,c);
    }
    void tube(Vec a,Vec b,float r0,float r1,Color color,int sides=8) {
        Vec axis=unit(b-a),u=unit(cross(axis,std::fabs(axis.y)<.9f?Vec(0,1,0):Vec(1,0,0))),v=unit(cross(axis,u));
        for(int i=0;i<sides;++i){float t=i*2*pi/sides,t1=(i+1)*2*pi/sides;
            Vec n=u*std::cos(t)+v*std::sin(t),n1=u*std::cos(t1)+v*std::sin(t1);
            quad(a+n*r0,b+n*r1,b+n1*r1,a+n1*r0,color);tri(a,a+n1*r0,a+n*r0,color);tri(b,b+n*r1,b+n1*r1,color);
        }
    }
};
inline void seat(Mesh& m,float x,float z,const Profile& p) {
    Color cloth={112,105,90};m.material=Cloth;
    m.box({x-23,p.floor+15,z-22},{x+23,p.floor+27,z+25},cloth);
    m.box({x-23,p.floor+22,z-30},{x+23,p.eye.y-17,z-20},cloth);
    m.box({x-14,p.eye.y-17,z-29},{x+14,p.eye.y+8,z-21},cloth);
    m.material=Vinyl;
    m.box({x-25,p.floor+18,z-22},{x-20,p.floor+33,z+25},{90,83,72});
    m.box({x+20,p.floor+18,z-22},{x+25,p.floor+33,z+25},{90,83,72});
}
inline void cabin(Mesh& m,const Profile& p) {
    m.triangles.clear();
    Color trim={78,72,63}, frame={100,96,85}, dash={57,55,50};
    float w=p.width,f=p.front,r=p.rear, sill=p.eye.y-53;
    m.material=Carpet;m.box({-w,p.floor,r},{w,p.floor+6,f},{100,91,77});
    m.material=Headliner;
    m.box({-p.roofWidth,p.roof,r},{p.roofWidth,p.roof+4,p.roofFront},frame);
    m.material=Vinyl;
    m.box({-w,p.floor,r},{-w+7,sill,f},trim);
    m.box({w-7,p.floor,r},{w,sill,f},trim);
    m.box({-w,p.floor,r},{w,sill,r+7},trim);
    // Windshield, side and rear windows are actual holes. No opaque glazing.
    m.material=Headliner;
    m.bar({-w,sill,f},{-p.roofWidth,p.roof,p.roofFront},3,frame);
    m.bar({w,sill,f},{p.roofWidth,p.roof,p.roofFront},3,frame);
    m.bar({-p.roofWidth,p.roof,p.roofFront},{p.roofWidth,p.roof,p.roofFront},3,frame);
    m.bar({-w,sill,r},{-p.roofWidth,p.roof,p.roofRear},4,frame);
    m.bar({w,sill,r},{p.roofWidth,p.roof,p.roofRear},4,frame);
    // Window seals, armrests, pulls and winding handles retain the period styling.
    for(int side=-1;side<=1;side+=2){
        float x=w*side;
        m.material=Leather;m.bar({x,sill,r},{x,sill,f},2,{34,32,28});
        m.material=Vinyl;m.box({x-4,p.floor+20,p.eye.z-22},{x+4,sill-9,p.eye.z+46},trim);
        m.material=Metal;m.bar({x-side*5.f,sill-17,p.eye.z+27},{x-side*5.f,sill-17,p.eye.z+43},1.2f,{160,157,143});
        m.bar({x-side*5.f,sill-26,p.eye.z+62},{x-side*5.f,sill-35,p.eye.z+70},1.2f,{160,157,143});
    }
    if(p.family==FourSeat) {
        for(int side=-1;side<=1;side+=2)
            m.bar({w*side,sill,p.eye.z-65},{p.roofWidth*side,p.roof,p.eye.z-65},3,frame);
        m.material=Cloth;
        m.box({-w+10,p.floor+15,r+15},{w-10,p.floor+28,r+58},{62,67,75});
        m.box({-w+10,p.floor+22,r+8},{w-10,p.eye.y-18,r+20},{62,67,75});
    }
    if(p.family==Truck) // rear bulkhead instead of a passenger compartment
        m.box({-w,sill,r},{w,p.roof,r+6},trim);
    seat(m,p.eye.x,p.eye.z-8,p);seat(m,-p.eye.x,p.eye.z-8,p);
    float panel=p.eye.z+(p.family==Truck?88.f:126.f),top=p.eye.y-25;
    // Faceted padded dash: cowl, sloping top and recessed vertical fascia.
    m.material=Plastic;
    m.quad({-w+5,top-4,panel},{w-5,top-4,panel},{w-7,top+2,f},{-w+7,top+2,f},dash);
    m.box({-w+5,top-35,panel},{w-5,top-4,panel+8},dash);
    m.material=p.family==Truck?Plastic:Wood;
    m.box({-w+7,top-31,panel-1},{w-7,top-21,panel+1},p.family==Truck?dash:Color{80,60,42});
    // Gauge binnacle above the fascia; needles/faces are added by controls().
    m.material=Vinyl;
    m.box({p.eye.x-34,top-18,panel-3},{p.eye.x+34,top+7,panel+11},{36,35,31});
    m.quad({p.eye.x-37,top+7,panel-6},{p.eye.x+37,top+7,panel-6},{p.eye.x+34,top+11,panel+17},{p.eye.x-34,top+11,panel+17},dash);
    // Analog radio and simple heater controls, no contemporary touchscreen.
    m.material=Radio;
    m.quad({-19,top-17,panel-2},{19,top-17,panel-2},{19,top-3,panel-2},{-19,top-3,panel-2},{150,147,135});
    m.material=Metal;
    for(int i=0;i<3;++i)m.tube({-10.f+i*10,top-25,panel-4},{-10.f+i*10,top-25,panel-1},2,2,{122,119,108},8);
    m.material=Plastic;
    for(int side=-1;side<=1;side+=2){
        float x=side*(w-19);
        m.box({x-9,top-17,panel-2},{x+9,top-4,panel},dash);
        m.material=Solid;
        for(int j=0;j<4;++j)m.bar({x-8,top-15+j*3.f,panel-3},{x+8,top-15+j*3.f,panel-3},.5f,{20,20,18});
    }
    m.material=Vinyl;
    m.box({-14,p.floor+6,p.eye.z-25},{14,p.floor+28,panel},trim);
    m.material=Leather;m.box({-5,p.floor+28,p.eye.z+40},{9,p.floor+35,p.eye.z+54},{44,39,32});
    m.material=Metal;m.tube({2,p.floor+33,p.eye.z+47},{2,p.floor+58,p.eye.z+43},1,1,{150,148,136});
    m.material=Leather;m.tube({2,p.floor+56,p.eye.z+43},{2,p.floor+64,p.eye.z+43},4,3,{39,33,27});
    // Mirror housing only; reflective rear rendering is a separate feature.
    m.material=Plastic;m.bar({0,p.roof-3,p.roofFront+13},{0,p.roof-10,p.roofFront+15},.8f,dash);
    m.box({-9,p.roof-14,p.roofFront+13},{9,p.roof-5,p.roofFront+17},dash);
    m.material=Metal;m.quad({-8,p.roof-13,p.roofFront+12},{8,p.roof-13,p.roofFront+12},{8,p.roof-6,p.roofFront+12},{-8,p.roof-6,p.roofFront+12},{61,74,76});
    m.material=Solid;
}
inline void gauge(Mesh& m, Vec center,float radius,float value,Material dial) {
    m.material=dial;
    m.quad(center+Vec(-radius,radius,0),center+Vec(radius,radius,0),center+Vec(radius,-radius,0),center+Vec(-radius,-radius,0),{128,128,122});
    float a=(210-bound(value,0,1)*240)*pi/180;
    m.material=Solid;
    Vec tip=center+Vec(std::cos(a)*radius*.73f,std::sin(a)*radius*.73f,-1);
    m.tri(center+Vec(-.5f,0,-1),tip,center+Vec(.5f,0,-1),{235,125,59});
}
inline void controls(Mesh& m,const Profile& p,float degrees,float speed,float rpm,bool driver,bool wheelAndDials=true) {
    Color rubber={72,61,45}, metal={157,151,130}, shirt={96,113,130}, skin={193,148,108}, pants={58,67,78};
    if(wheelAndDials){
    m.material=Leather;
    for(int i=0;i<32;++i) {
        float a=i*360.f/32,b=(i+1)*360.f/32;
        // Shared cross-section vertices avoid cracks between independent bars.
        m.quad(wheelPoint(p,a,1.07f,1.5f),wheelPoint(p,b,1.07f,1.5f),wheelPoint(p,b,.93f,1.5f),wheelPoint(p,a,.93f,1.5f),rubber);
        m.quad(wheelPoint(p,a,.93f,-1.5f),wheelPoint(p,b,.93f,-1.5f),wheelPoint(p,b,1.07f,-1.5f),wheelPoint(p,a,1.07f,-1.5f),rubber);
        m.quad(wheelPoint(p,a,1.07f,-1.5f),wheelPoint(p,b,1.07f,-1.5f),wheelPoint(p,b,1.07f,1.5f),wheelPoint(p,a,1.07f,1.5f),rubber);
        m.quad(wheelPoint(p,a,.93f,1.5f),wheelPoint(p,b,.93f,1.5f),wheelPoint(p,b,.93f,-1.5f),wheelPoint(p,a,.93f,-1.5f),rubber);
    }
    // Positive command is clockwise when viewed by the driver.
    m.material=Metal;
    for(int i=0;i<3;++i) {
        float a=(i==0?180.f:i==1?0.f:270.f)-degrees;
        m.quad(wheelPoint(p,a-9,.23f,1),wheelPoint(p,a-5,.94f,1),wheelPoint(p,a+5,.94f,1),wheelPoint(p,a+9,.23f,1),metal);
    }
    m.material=Leather;m.tube(p.wheel,wheelPoint(p,0,0,4),5,5,{53,48,39},12);
    float face=p.eye.z+(p.family==Truck?83.f:121.f);
    gauge(m,{p.eye.x-14,p.eye.y-29,face},11,speed/200,SpeedDial);
    gauge(m,{p.eye.x+12,p.eye.y-29,face},11,rpm/8000,RpmDial);
    gauge(m,{p.eye.x-30,p.eye.y-31,face},4,.65f,SmallDial);
    gauge(m,{p.eye.x+29,p.eye.y-31,face},4,.5f,SmallDial);
    }
    if(!driver) return;
    // Tanner: brown leather jacket over a grey T-shirt, jeans. Head deliberately omitted:
    // the seat/torso, not the camera, owns the body.
    const Color jacket={92,60,40}, jacketDark={64,42,29}, tee={128,128,124}, jeans={58,72,98}, shoe={38,33,30};
    Vec hip={p.eye.x,p.floor+30,p.eye.z-10};
    float chestTop=p.eye.y-20;
    m.material=Leather;
    m.box(hip+Vec(-18,0,-14),{p.eye.x+18,chestTop,p.eye.z+5},jacket);
    // open jacket: T-shirt strip and zip edges on the chest
    m.material=Cloth;
    m.quad({p.eye.x-4.5f,hip.y+6,p.eye.z+5.2f},{p.eye.x+4.5f,hip.y+6,p.eye.z+5.2f},{p.eye.x+6,chestTop-1,p.eye.z+5.2f},{p.eye.x-6,chestTop-1,p.eye.z+5.2f},tee);
    m.material=Leather;
    m.bar({p.eye.x-5.5f,hip.y+6,p.eye.z+5.6f},{p.eye.x-7,chestTop-1,p.eye.z+5.6f},.7f,jacketDark);
    m.bar({p.eye.x+5.5f,hip.y+6,p.eye.z+5.6f},{p.eye.x+7,chestTop-1,p.eye.z+5.6f},.7f,jacketDark);
    float r=p.radius, finger=std::max(.75f,r*.036f);
    for(int side=-1;side<=1;side+=2) {
        Vec knee={p.eye.x+side*11.f,p.floor+23,p.eye.z+38};
        Vec ankle={p.eye.x+side*12.f,p.floor+10,p.eye.z+76};
        m.material=Cloth;m.tube(hip+Vec(side*10.f,0,0),knee,8,7,jeans,10);m.tube(knee,ankle,6.5f,4.5f,jeans,10);
        m.material=Leather;m.box(ankle+Vec(-5,-5,-3),ankle+Vec(5,2,15),shoe);
        // Simplified sliding grip above 100 degrees avoids impossible arm twists.
        float grip=(side<0?180.f:0.f)-bound(degrees,-100,100);
        float a=grip*pi/180, t=p.tilt*pi/180;
        // Rim frame at the grip: outward radial, tangent (counter-clockwise), normal towards the driver.
        Vec centre=wheelPoint(p,grip,1,0);
        Vec radial=unit(wheelPoint(p,grip,1.2f,0)-centre);
        Vec normal=unit(Vec(0,std::sin(t),-std::cos(t)));
        Vec tangent=unit(cross(normal,radial));
        if(dot(tangent,wheelPoint(p,grip+5,1,0)-centre)<0)tangent=tangent*-1;
        float thick=r*.07f+1.5f;                       // rim cross-section radius plus skin
        Vec wrist=centre+radial*(thick*.9f)+normal*(thick*2.6f)-tangent*(side*r*.05f);
        Vec shoulder={p.eye.x+side*18.f,p.eye.y-24,p.eye.z-4};
        Vec joint=elbow(shoulder,wrist,64,58,side);
        Vec cuff=wrist+unit(joint-wrist)*(r*.22f);
        m.material=Leather;
        m.tube(shoulder,joint,6.2f,5.2f,jacket,10);
        m.tube(joint,cuff,5.2f,4.4f,jacket,10);
        m.tube(cuff,cuff+unit(wrist-cuff)*1.2f,4.6f,4.6f,jacketDark,10);   // ribbed cuff
        m.material=Solid;
        const Color skin={196,150,112};
        m.tube(cuff,wrist,2.8f,2.6f,skin,8);
        // Back of the hand rides over the rim towards the driver.
        Vec knuckle=centre+radial*(thick*.7f)+normal*(thick*1.15f);
        m.tube(wrist,knuckle,2.9f,2.5f,skin,8);
        // Four fingers wrap the rim: knuckle side -> outer edge -> behind the rim.
        for(int f=0;f<4;++f) {
            float along=(f-1.5f)*finger*2.3f;
            Vec off=tangent*along;
            float len=f==3?.8f:1.f;
            Vec k=knuckle+off;
            Vec m1=centre+off+radial*(thick*1.05f)+normal*(thick*.35f);
            Vec m2=centre+off+radial*(thick*.55f)-normal*(thick*.95f);
            Vec tip=centre+off+radial*(-thick*.35f*len)-normal*(thick*.9f);
            m.tube(k,m1,finger*1.1f,finger,skin,6);
            m.tube(m1,m2,finger,finger*.95f,skin,6);
            m.tube(m2,tip,finger*.95f,finger*.8f,skin,6);
        }
        // Thumb along the inner edge of the rim, pointing at the spokes.
        Vec thumbBase=wrist-radial*(thick*.6f)+normal*(thick*.4f);
        Vec thumbTip=wheelPoint(p,grip+side*-14.f,.9f,thick*.9f);
        m.tube(thumbBase,(thumbBase+thumbTip)*.5f,finger*1.25f,finger*1.1f,skin,6);
        m.tube((thumbBase+thumbTip)*.5f,thumbTip,finger*1.1f,finger*.85f,skin,6);
    }
}
// Sutherland-Hodgman against the camera near plane, before GTE projection.
inline int clipNear(const Vec* input,int count,Vec* output,float nearZ) {
    int n=0;
    for(int i=0;i<count;++i) {
        Vec a=input[i],b=input[(i+1)%count];bool inside=a.z>=nearZ, next=b.z>=nearZ;
        if(inside) output[n++]=a;
        if(inside!=next) output[n++]=a+(b-a)*((nearZ-a.z)/(b.z-a.z));
    }
    return n;
}
struct ClipVertex {Vec position;UV uv;};
inline int clipNearUV(const ClipVertex* input,int count,ClipVertex* output,float nearZ) {
    int n=0;
    for(int i=0;i<count;++i){auto a=input[i],b=input[(i+1)%count];
        bool inside=a.position.z>=nearZ,next=b.position.z>=nearZ;
        if(inside)output[n++]=a;
        if(inside!=next){float t=(nearZ-a.position.z)/(b.position.z-a.position.z);
            output[n++]={a.position+(b.position-a.position)*t,{a.uv.u+(b.uv.u-a.uv.u)*t,a.uv.v+(b.uv.v-a.uv.v)*t}};
        }
    }
    return n;
}
}
