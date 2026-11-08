#pragma once
// Imported cockpit interiors (.rdc), produced by tools/cockpit/convert_cockpit.py
// from freely licensed models. One 256x256 atlas, 8-bit UVs, PSX style.
// File frame: X right, Y up, Z forward, metres. Profiles use Driver model units.
#include "cockpit_model.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>

namespace cockpit {
struct ImportedTri { Vec p[3]; unsigned char uv[6]; Color color; unsigned char part; }; // part 1 = steering wheel
struct Imported {
    Vec lo, hi, seat, wheel, axis, eye;
    float roofY=0, roofFront=0, radius=0;
    struct Dial { Vec centre; float radius=0; } dials[2];  // speedometer, tachometer
    Vec dialNormal;  // towards the driver
    bool hasDials=false, realDials=false;
    std::string credit;
    std::vector<unsigned char> rgba;      // 256*256*4
    std::vector<ImportedTri> tris;
    bool valid=false;
};
inline bool loadImported(const char* path, Imported& out) {
    out=Imported();
    FILE* f=std::fopen(path,"rb"); if(!f) return false;
    bool ok=true;
    auto rd=[&](void* p,size_t n){ if(ok && std::fread(p,1,n,f)!=n) ok=false; };
    char magic[4]; uint32_t version=0,count=0; rd(magic,4); rd(&version,4); rd(&count,4);
    ok=ok && !std::memcmp(magic,"RD2C",4) && (version==1 || version==2) && count>0 && count<=20000;
    float meta[40]={}; rd(meta,(version>=2?40:28)*sizeof(float));
    unsigned char len=0; rd(&len,1); char credit[256]={}; rd(credit,len);
    std::vector<unsigned char> rgb(256*256*3); if(ok) rd(rgb.data(),rgb.size());
    if(ok){
        out.lo={meta[0],meta[1],meta[2]}; out.hi={meta[3],meta[4],meta[5]};
        out.roofY=meta[6]; out.roofFront=meta[7];
        out.seat={meta[8],meta[9],meta[10]}; out.wheel={meta[11],meta[12],meta[13]};
        out.axis={meta[14],meta[15],meta[16]}; out.radius=meta[17]; out.eye={meta[18],meta[19],meta[20]};
        out.credit.assign(credit,len);
        if(version>=2){
            for(int i=0;i<2;++i){ out.dials[i].centre={meta[28+i*4],meta[29+i*4],meta[30+i*4]}; out.dials[i].radius=meta[31+i*4]; }
            out.dialNormal=unit({meta[36],meta[37],meta[38]}); out.realDials=meta[39]>.5f;
            out.hasDials=out.dials[0].radius>.01f && out.dials[0].radius<.15f && out.dials[1].radius>.01f && out.dials[1].radius<.15f;
        }
        out.rgba.resize(256*256*4);
        for(int i=0;i<256*256;++i){ out.rgba[i*4]=rgb[i*3]; out.rgba[i*4+1]=rgb[i*3+1]; out.rgba[i*4+2]=rgb[i*3+2]; out.rgba[i*4+3]=255; }
        out.tris.resize(count);
        for(auto& t:out.tris){
            float v[9]; unsigned char extra[10];
            rd(v,sizeof(v)); rd(extra,sizeof(extra)); if(!ok) break;
            for(int k=0;k<3;++k) t.p[k]={v[k*3],v[k*3+1],v[k*3+2]};
            std::memcpy(t.uv,extra,6); t.color={extra[6],extra[7],extra[8]}; t.part=extra[9];
            for(float x:v) if(!std::isfinite(x) || std::fabs(x)>50) ok=false;
        }
    }
    std::fclose(f);
    // Sanity: a real car, a wheel in front of the seat, a plausible wheel size.
    ok=ok && out.hi.z-out.lo.z>1.5f && out.hi.x-out.lo.x>.8f && out.radius>.08f && out.radius<.35f && out.wheel.z>out.seat.z;
    out.valid=ok;
    if(!ok){ out.tris.clear(); out.rgba.clear(); }
    return ok;
}
// Driver = source * scale + offset. The source is scaled to the car's footprint
// and anchored on the fitted roof and windscreen header of the Driver model.
struct Placement { float scale=100; Vec offset; };
inline Placement place(const Imported& m, const Profile& p, float halfWidth, float halfLength, float scaleOverride) {
    Placement pl;
    float sx=2*halfWidth/std::max(.5f,m.hi.x-m.lo.x), sz=2*halfLength/std::max(1.f,m.hi.z-m.lo.z);
    pl.scale=scaleOverride>0?scaleOverride:bound((sx+sz)*.5f,50,220);
    pl.offset={-(m.lo.x+m.hi.x)*.5f*pl.scale, p.roof-m.roofY*pl.scale, p.roofFront-m.roofFront*pl.scale};
    return pl;
}
inline Vec mapPoint(const Placement& pl, Vec v) { return v*pl.scale+pl.offset; }
// Seat, wheel and eyes come from the imported cabin. The eye height follows the
// driver's stature: seated eye height is about 0.355 of stature above the cushion
// for a reclined car seat (0.64 m for a 1.80 m adult).
inline void applyImported(Profile& p, const Imported& m, const Placement& pl, float statureMetres) {
    Vec seat=mapPoint(pl,m.seat), wheel=mapPoint(pl,m.wheel);
    float seated=.355f*bound(statureMetres,1.4f,2.1f)*pl.scale;
    p.wheel=wheel; p.radius=m.radius*pl.scale;
    p.tilt=bound(std::atan2(m.axis.y,-m.axis.z)*180/pi,0,65);
    p.floor=seat.y-30;  // the procedural hips sit 30 units above "floor"
    p.eye={wheel.x, std::min(seat.y+seated,p.roof-.10f*pl.scale), seat.z-.07f*pl.scale};
}
// Live instruments over the imported cluster. The source textures are too small
// to read at PSX resolution, so each dial gets a bright face, 30-degree ticks, a
// long needle and a hub: 240 degree sweep, 200 km/h and 8000 rpm. Driver units.
inline void needles(Mesh& m, const Imported& in, const Placement& pl, float speedKmh, float rpm) {
    if(!in.hasDials) return;
    Vec n=in.dialNormal, up=unit(Vec(0,1,0)-n*n.y), right=unit(cross(up,n));
    if(right.x<0) right=right*-1;
    const float values[2]={speedKmh/200, rpm/8000};
    const Color face={232,222,190}, ink={34,30,26}, red={235,96,36};
    m.material=Solid;
    auto at=[&](Vec c,float r,float deg,float lift){ float a=deg*pi/180; return mapPoint(pl,c+n*lift+(right*std::cos(a)+up*std::sin(a))*r); };
    for(int i=0;i<2;++i){
        const auto& d=in.dials[i]; float r=d.radius*.92f; Vec c=d.centre;
        for(int k=0;k<16;++k) m.tri(mapPoint(pl,c+n*.001f),at(c,r,k*22.5f+22.5f,.001f),at(c,r,k*22.5f,.001f),face);
        for(int k=0;k<=8;++k){ float deg=210-k*30.f, w=k%2?1.2f:2.2f;
            m.tri(at(c,r*.97f,deg-w,.002f),at(c,r*.97f,deg+w,.002f),at(c,r*(k%2?.84f:.76f),deg,.002f),ink); }
        float deg=210-bound(values[i],0,1)*240, a=deg*pi/180;
        Vec dir=right*std::cos(a)+up*std::sin(a), side=right*-std::sin(a)+up*std::cos(a), h=c+n*.003f;
        float w=std::max(.003f,r*.1f);
        m.tri(mapPoint(pl,h-dir*(r*.2f)+side*w),mapPoint(pl,h+dir*(r*.9f)),mapPoint(pl,h-dir*(r*.2f)-side*w),red);
        for(int k=0;k<6;++k) m.tri(mapPoint(pl,h+n*.001f),at(c,w*1.8f,k*60.f+60,.004f),at(c,w*1.8f,k*60.f,.004f),ink);
    }
}
// Steering rotation about the column, matching wheelPoint(): positive command is
// clockwise as seen by the driver.
inline Vec rotateWheel(Vec v, Vec centre, float tiltDegrees, float degrees) {
    float t=tiltDegrees*pi/180, a=-degrees*pi/180;
    Vec ex(1,0,0), up(0,std::cos(t),std::sin(t)), n(0,std::sin(t),-std::cos(t));
    Vec d=v-centre; float x=dot(d,ex), y=dot(d,up), z=dot(d,n);
    float x2=x*std::cos(a)-y*std::sin(a), y2=x*std::sin(a)+y*std::cos(a);
    return centre+ex*x2+up*y2+n*z;
}
}
