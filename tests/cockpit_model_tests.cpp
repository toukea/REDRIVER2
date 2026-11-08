#include "../src_rebuild/utils/cockpit_model.h"
#include "../src_rebuild/utils/cockpit_atlas.h"
#include <cstdio>
#include <cstdlib>
int checks=0;
void check(bool ok,const char* message){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(){
    using namespace cockpit;
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
    std::printf("PASS: %d cockpit geometry/pose/clipping checks\n",checks);
}
