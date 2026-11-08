#include "../src_rebuild/utils/cockpit_model.h"
#include "../src_rebuild/utils/cockpit_atlas.h"
#include "../src_rebuild/utils/cockpit_import.h"
#include "../src_rebuild/utils/camera_preference.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
int checks=0;
void check(bool ok,const char* message){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(){
    using namespace cockpit;
    {
        const char* path="src_rebuild/bin/tests/camera-preference-test.cfg";
        std::remove(path);
        camera_preference::Preferences p;
        check(!p.load(path),"missing preference file uses defaults");
        check(p.resolve(0,false,true,3)==3,"first launch honors cockpit default");
        check(p.choose(0,false,2),"explicit first person choice");
        check(p.choose(0,true,1),"separate pedestrian choice");
        check(p.choose(1,false,0),"separate second player choice");
        check(!p.choose(0,false,6) && !p.choose(0,true,3),"scripted and pedestrian cockpit views rejected");
        check(p.save(path),"save preference to disk");
        camera_preference::Preferences restarted;
        check(restarted.load(path),"reload preference after restart");
        check(restarted.resolve(0,false,true,3)==2 && restarted.resolve(0,true,true,0)==1 && restarted.resolve(1,false,true,3)==0,"restart preserves each explicit choice");
        check(restarted.choose(0,false,3),"switch back to cockpit explicitly");
        check(restarted.resolve(0,false,false,0)==2 && restarted.resolve(0,false,true,0)==3,"disabled cockpit fallback preserves saved cockpit choice");
        check(!restarted.choose(0,false,3),"unchanged view does not trigger another write");
        FILE* bad=std::fopen(path,"wb");check(bad!=nullptr,"create invalid preference fixture");
        std::fputs("REDRIVER2_CAMERA_V1\n99 3\n-1 -1\n",bad);std::fclose(bad);
        camera_preference::Preferences invalid;invalid.load(path);
        check(invalid.resolve(0,false,true,0)==0 && invalid.resolve(0,true,true,0)==0,"invalid stored view safely falls back");
        std::remove(path);
    }
    for(int f=0;f<3;++f){
        auto p=profile(Family(f),100,100,250);
        check(p.eye.y<p.roof && p.eye.y>p.floor && p.eye.z<p.front,"eye inside cabin");
        check(p.eye.x<0 && p.wheel.z>p.eye.z,"left hand drive position and wheel in front");
        Mesh shell;cabin(shell,p);size_t base=shell.triangles.size();
        check(base>100,"nonempty cabin geometry");
        for(int a=-450;a<=450;a+=15){
            Mesh m;controls(m,p,float(a),120,4500,true);
            check(m.triangles.size()>200,"driver and steering geometry exists");
            for(const auto& t:m.triangles)for(auto v:{t.a,t.b,t.c})
                if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)){check(false,"finite pose at all steering angles");}
            float grip=180-bound(float(a),-100,100);
            check(std::fabs(length(wheelPoint(p,grip)-p.wheel)-p.radius)<.001f,"hands remain on tilted wheel rim");
        }
    }
    Vec s={0,0,0},w={30,-10,40},e=elbow(s,w,35,30,-1);
    check(std::fabs(length(e-s)-35)<.001f && std::fabs(length(w-e)-30)<.001f,"IK preserves both bone lengths");
    Vec in[3]={{-10,0,0},{10,0,20},{0,10,20}},out[5];
    int n=clipNear(in,3,out,10);check(n==4,"near crossing splits into quad");
    for(int i=0;i<n;++i)check(out[i].z>=10,"near clipping produces valid projection depths");
    for(auto& v:in)v.z=-1;check(clipNear(in,3,out,10)==0,"behind-eye triangle rejected");
    check(classify(180,500)==Truck,"large vehicle fallback is truck");
    check(classify(90,180)==TwoSeat,"short vehicle fallback is two-seat");
    check(classify(90,260)==FourSeat,"normal vehicle fallback is four-seat");
    check(classify(84,375)==FourSeat,"Chicago sedan length must not imply truck");
    check(classify(86,667)==FourSeat,"long low limousine must not imply truck");
    std::vector<Vec> body;
    for(int x:{-95,95})for(int z:{-110,55})for(int y:{-70,70})body.push_back({float(x),float(y),float(z)});
    for(int x:{-20,20})for(int z:{-10,10})body.push_back({float(x),96,float(z)});
    auto low=fitProfile(FourSeat,130,90,380,body);
    check(low.meshFitted && low.roof<80,"police beacon does not raise the driver seat");
    check(low.eye.y<low.roof && low.eye.y>low.floor,"fitted eyes remain below the physical roof");
    auto fallback=fitProfile(FourSeat,130,90,380,{});
    check(!fallback.meshFitted,"missing exterior mesh keeps usable fallback");
    for(auto& v:body){v.y+=100;v.z+=400;}
    auto high=fitProfile(Truck,160,180,650,body);
    check(high.meshFitted && high.eye.y>low.eye.y+60,"high cab gives a higher viewpoint from mesh geometry");
    ClipVertex uvIn[3]={{{-10,0,0},{0,0}},{{10,0,20},{1,0}},{{0,10,20},{1,1}}},uvOut[5];
    int uvN=clipNearUV(uvIn,3,uvOut,10);
    check(uvN==4 && std::fabs(uvOut[0].uv.u-.5f)<.001f,"near-plane intersection interpolates texture coordinates");
    auto atlas=atlasPixels();check(atlas.size()==256*256*4,"complete original material atlas generated");
    check(atlas[3]==255 && atlas[0]==128,"solid atlas tile is opaque neutral gray for PSX color modulation");
    {
        // Imported interior: synthetic file, placement, eyes and steering rotation.
        const char* path="src_rebuild/bin/tests/cockpit-test.rdc";
        FILE* f=std::fopen(path,"wb");check(f!=nullptr,"create imported cockpit fixture");
        std::fwrite("RD2C",1,4,f);unsigned v=1,n=2;std::fwrite(&v,4,1,f);std::fwrite(&n,4,1,f);
        float meta[28]={-0.9f,0,-2.6f, 0.9f,1.4f,2.6f, 1.38f,0.45f, -0.34f,0.62f,-0.23f, -0.34f,0.86f,0.32f, 0,0.5f,-0.866f, 0.15f, -0.34f,1.3f,-0.3f};
        std::fwrite(meta,sizeof(meta),1,f);unsigned char len=4;std::fwrite(&len,1,1,f);std::fwrite("test",1,4,f);
        std::vector<unsigned char> rgb(256*256*3,200);std::fwrite(rgb.data(),1,rgb.size(),f);
        for(unsigned char part=0;part<2;++part){float t[9]={-.3f,.5f,.2f,.3f,.5f,.2f,0,1,.2f};std::fwrite(t,sizeof(t),1,f);unsigned char e[10]={0,0,255,0,0,247,128,128,128,part};std::fwrite(e,1,10,f);}
        std::fclose(f);
        Imported in;check(loadImported(path,in) && in.tris.size()==2 && in.tris[1].part==1 && in.credit=="test","imported cockpit loads");
        check(in.rgba.size()==256*256*4 && in.rgba[3]==255,"imported atlas expands to RGBA");
        auto p=profile(FourSeat,100,90,260);p.roof=80;p.roofFront=40;
        Placement pl=place(in,p,100,260,0);
        check(pl.scale>50 && pl.scale<220,"interior scaled to the car footprint");
        check(std::fabs(mapPoint(pl,{0,in.roofY,in.roofFront}).y-p.roof)<.01f,"roof anchored on the fitted roof");
        Profile tall=p,small=p;applyImported(tall,in,pl,1.95f);applyImported(small,in,pl,1.60f);
        check(tall.eye.y>small.eye.y,"taller driver sits with higher eyes");
        check(tall.eye.y<=p.roof-.09f*pl.scale && small.eye.y>mapPoint(pl,in.seat).y,"eyes between seat and headliner");
        check(std::fabs(small.tilt-30)<1 && small.wheel.z>small.eye.z,"steering column tilt and position from the model");
        Vec c=small.wheel, rim=c+Vec(small.radius,0,0);
        Vec r=rotateWheel(rim,c,small.tilt,90);
        check(std::fabs(length(r-c)-small.radius)<.01f && length(r-rim)>small.radius,"wheel rotates about its column");
        check(length(rotateWheel(rim,c,small.tilt,360)-rim)<.01f,"full turn returns the rim");
        check(rotateWheel(c+Vec(0,small.radius*std::cos(small.tilt*pi/180),small.radius*std::sin(small.tilt*pi/180)),c,small.tilt,90).x>c.x+small.radius*.9f,"positive command turns the top of the wheel to the right");
        std::remove(path);
        Imported bad;FILE* g=std::fopen(path,"wb");std::fwrite("NOPE",1,4,g);std::fclose(g);
        check(!loadImported(path,bad) && !bad.valid,"invalid interior rejected");std::remove(path);
        check(!in.hasDials,"version 1 interiors have no instrument data");
        {
            FILE* h=std::fopen(path,"wb");std::fwrite("RD2C",1,4,h);unsigned v2=2,n2=1;std::fwrite(&v2,4,1,h);std::fwrite(&n2,4,1,h);
            float m2[40]={-0.9f,0,-2.6f, 0.9f,1.4f,2.6f, 1.38f,0.45f, -0.34f,0.62f,-0.23f, -0.34f,0.86f,0.32f, 0,0.5f,-0.866f, 0.15f, -0.34f,1.3f,-0.3f};
            float dials[12]={-0.29f,0.91f,0.52f,0.048f, -0.16f,0.91f,0.52f,0.048f, 0,0.3f,-0.95f, 1};std::memcpy(m2+28,dials,sizeof(dials));
            std::fwrite(m2,sizeof(m2),1,h);unsigned char l2=0;std::fwrite(&l2,1,1,h);
            std::vector<unsigned char> px(256*256*3,90);std::fwrite(px.data(),1,px.size(),h);
            float t[9]={-.3f,.5f,.2f,.3f,.5f,.2f,0,1,.2f};std::fwrite(t,sizeof(t),1,h);unsigned char e[10]={0};std::fwrite(e,1,10,h);std::fclose(h);
            Imported v;check(loadImported(path,v) && v.hasDials && v.realDials,"version 2 instrument dials load");
            Placement pl2;pl2.scale=100;
            Mesh idle,fast;needles(idle,v,pl2,0,800);needles(fast,v,pl2,180,7000);
            auto needle=[&](const Mesh& m){for(const auto& t:m.triangles)if(t.color.r==v.needle.r && t.color.g==v.needle.g)return t;return m.triangles[0];};
            check(idle.triangles.size()==fast.triangles.size() && idle.triangles.size()==14,"two needles with hubs over the painted dials");
            Profile base=profile(FourSeat,100,90,260);base.roof=80;base.roofFront=40;
            Placement pv=place(v,base,100,260,0);Profile withDials=base;applyImported(withDials,v,pv,1.80f);
            Profile noDials=base;Imported plain=v;plain.hasDials=false;applyImported(noDials,plain,pv,1.80f);
            check(std::fabs(withDials.eye.y-noDials.eye.y)<=.0601f*pv.scale,"instrument line of sight moves the eye at most 6 cm");
            Vec c0=mapPoint(pl2,v.dials[0].centre);
            check(needle(idle).b.x<c0.x && needle(fast).b.x>c0.x,"speedometer needle sweeps clockwise with speed");
            check(length(needle(idle).b-c0)<v.dials[0].radius*100 && length(needle(idle).b-c0)>v.dials[0].radius*70,"needle stays within the dial");
            check(dot(needle(idle).b-c0,cross(Vec(0,1,0),Vec(1,0,0)))*0+length(needle(idle).b-needle(idle).a)>1,"needle has a visible length");
            std::remove(path);
        }
        Imported real;if(loadImported("data/cockpits/duke69.rdc",real))check(real.tris.size()>1500 && real.radius>.1f && real.hasDials && real.realDials,"bundled Duke interior has its dials");
    }
    std::printf("PASS: %d cockpit geometry/pose/clipping checks\n",checks);
}
