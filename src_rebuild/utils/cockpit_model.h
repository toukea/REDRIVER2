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
    p.wheel=p.eye+Vec(0,-32,78);
    p.radius=family==Truck?26.f:22.f;
    p.tilt=family==Truck?50.f:20.f;
    return p;
}
// Only use a size heuristic as a fallback; model-specific configuration wins.
inline Family classify(float halfHeight, float halfLength) {
    if(halfHeight>145 || (halfLength>580 && halfHeight>120)) return Truck;
    return halfLength<260 || halfHeight<78?TwoSeat:FourSeat;
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
struct Triangle { Vec a,b,c; Color color; };
struct Mesh {
    std::vector<Triangle> triangles;
    void tri(Vec a, Vec b, Vec c, Color color) { triangles.push_back({a,b,c,color}); }
    void quad(Vec a,Vec b,Vec c,Vec d,Color color) {tri(a,b,c,color);tri(a,c,d,color);}
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
};
inline void seat(Mesh& m,float x,float z,const Profile& p) {
    Color cloth={62,67,75};
    m.box({x-23,p.floor+15,z-22},{x+23,p.floor+27,z+25},cloth);
    m.box({x-23,p.floor+22,z-30},{x+23,p.eye.y-17,z-20},cloth);
    m.box({x-14,p.eye.y-17,z-29},{x+14,p.eye.y+8,z-21},cloth);
}
inline void cabin(Mesh& m,const Profile& p) {
    m.triangles.clear();
    Color trim={45,49,55}, frame={82,85,89}, dash={36,40,46};
    float w=p.width,f=p.front,r=p.rear, sill=p.eye.y-53;
    m.box({-w,p.floor,r},{w,p.floor+6,f},trim);
    m.box({-w,p.roof,r},{w,p.roof+5,f},trim);
    m.box({-w,p.floor,r},{-w+7,sill,f},trim);
    m.box({w-7,p.floor,r},{w,sill,f},trim);
    m.box({-w,p.floor,r},{w,sill,r+7},trim);
    // Windshield, side and rear windows are actual holes. No opaque glazing.
    m.bar({-w,sill,f},{-w*.82f,p.roof,f-25},5,frame);
    m.bar({w,sill,f},{w*.82f,p.roof,f-25},5,frame);
    m.bar({-w*.82f,p.roof,f-25},{w*.82f,p.roof,f-25},5,frame);
    m.bar({-w,sill,r},{-w,p.roof,r},6,frame);
    m.bar({w,sill,r},{w,p.roof,r},6,frame);
    if(p.family==FourSeat) {
        for(int side=-1;side<=1;side+=2)
            m.bar({w*side,sill,p.eye.z-65},{w*side,p.roof,p.eye.z-65},5,frame);
        m.box({-w+10,p.floor+15,r+15},{w-10,p.floor+28,r+58},{62,67,75});
        m.box({-w+10,p.floor+22,r+8},{w-10,p.eye.y-18,r+20},{62,67,75});
    }
    if(p.family==Truck) // rear bulkhead instead of a passenger compartment
        m.box({-w,sill,r},{w,p.roof,r+6},trim);
    seat(m,p.eye.x,p.eye.z-8,p);seat(m,-p.eye.x,p.eye.z-8,p);
    m.box({-w+6,p.floor+15,f-45},{w-6,p.eye.y-40,f},dash);
    m.box({-w+6,p.eye.y-40,f-52},{w-6,p.eye.y-34,f-5},trim);
    // Center tunnel, shifter and radio/vents.
    m.box({-12,p.floor+6,p.eye.z-25},{12,p.floor+34,f-40},trim);
    m.bar({8,p.floor+34,p.eye.z+25},{8,p.floor+57,p.eye.z+20},2,frame);
    m.box({4,p.floor+54,p.eye.z+16},{12,p.floor+61,p.eye.z+24},{20,22,25});
    for(int i=0;i<3;++i)
        m.box({-16.f+i*13,p.eye.y-48,f-45},{-6.f+i*13,p.eye.y-40,f-43},{12,15,18});
}
inline void gauge(Mesh& m, Vec center,float radius,float value) {
    Color black={9,13,16}, tick={170,190,178};
    for(int i=0;i<20;++i) {
        float a=i*pi/10,b=(i+1)*pi/10;
        m.tri(center,center+Vec(std::cos(a)*radius,std::sin(a)*radius,0),center+Vec(std::cos(b)*radius,std::sin(b)*radius,0),black);
    }
    for(int i=0;i<9;++i) {
        float a=(210-i*30)*pi/180;
        m.bar(center+Vec(std::cos(a)*radius*.78f,std::sin(a)*radius*.78f,-1),center+Vec(std::cos(a)*radius*.94f,std::sin(a)*radius*.94f,-1),.5f,tick);
    }
    float a=(210-bound(value,0,1)*240)*pi/180;
    m.bar(center+Vec(0,0,-2),center+Vec(std::cos(a)*radius*.73f,std::sin(a)*radius*.73f,-2),.6f,{225,88,45});
}
inline void controls(Mesh& m,const Profile& p,float degrees,float speed,float rpm,bool driver) {
    Color rubber={17,19,22}, metal={125,130,135}, shirt={72,88,110}, skin={183,137,99}, pants={39,47,62};
    for(int i=0;i<32;++i) {
        float a=i*360.f/32,b=(i+1)*360.f/32;
        // Shared cross-section vertices avoid cracks between independent bars.
        m.quad(wheelPoint(p,a,1.11f,2),wheelPoint(p,b,1.11f,2),wheelPoint(p,b,.89f,2),wheelPoint(p,a,.89f,2),rubber);
        m.quad(wheelPoint(p,a,.89f,-2),wheelPoint(p,b,.89f,-2),wheelPoint(p,b,1.11f,-2),wheelPoint(p,a,1.11f,-2),rubber);
        m.quad(wheelPoint(p,a,1.11f,-2),wheelPoint(p,b,1.11f,-2),wheelPoint(p,b,1.11f,2),wheelPoint(p,a,1.11f,2),rubber);
        m.quad(wheelPoint(p,a,.89f,2),wheelPoint(p,b,.89f,2),wheelPoint(p,b,.89f,-2),wheelPoint(p,a,.89f,-2),rubber);
    }
    // Positive command is clockwise when viewed by the driver.
    for(int i=0;i<3;++i) {
        float a=30+i*120.f-degrees;
        m.bar(wheelPoint(p,a,.12f),wheelPoint(p,a,.91f),2,metal);
    }
    m.bar(p.wheel,wheelPoint(p,0,0,5),7,rubber);
    gauge(m,{p.eye.x-15,p.eye.y-45,p.front-53},12,speed/200);
    gauge(m,{p.eye.x+15,p.eye.y-45,p.front-53},12,rpm/7000);
    if(!driver) return;
    // Head deliberately omitted. The seat/torso, not the camera, owns the body.
    Vec hip={p.eye.x,p.floor+30,p.eye.z-10};
    m.box(hip+Vec(-17,0,-13),{p.eye.x+17,p.eye.y-20,p.eye.z+6},shirt);
    for(int side=-1;side<=1;side+=2) {
        Vec knee={p.eye.x+side*11.f,p.floor+23,p.eye.z+38};
        Vec ankle={p.eye.x+side*12.f,p.floor+10,p.eye.z+76};
        m.bar(hip+Vec(side*10.f,0,0),knee,8,pants);m.bar(knee,ankle,6,pants);
        m.box(ankle+Vec(-6,-6,-3),ankle+Vec(6,2,16),rubber);
        // Simplified sliding grip above 100 degrees avoids impossible arm twists.
        // A later authored hand-over-hand clip can replace this pose generator.
        float grip=(side<0?180.f:0.f)-bound(degrees,-100,100);
        Vec wrist=wheelPoint(p,grip,1,3);
        Vec shoulder={p.eye.x+side*18.f,p.eye.y-24,p.eye.z-4};
        float upper=p.family==Truck?62.f:56.f, lower=p.family==Truck?58.f:52.f;
        Vec joint=elbow(shoulder,wrist,upper,lower,side);
        m.bar(shoulder,joint,6,shirt);m.bar(joint,wrist,4.5f,shirt);
        m.bar(wrist,wheelPoint(p,grip+12,1,3),4,skin);
        m.bar(wrist,wheelPoint(p,grip,.83f,1),2.5f,skin);
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
}
