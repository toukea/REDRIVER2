#pragma once
#include "cockpit_model.h"
#include <cstdint>
#include <cstdio>
namespace cockpit {
// A small original pixel atlas, generated once. Does not consume PSX VRAM slots.
// Grayscale materials are tinted per mesh; dial/radio artwork uses period colors.
inline std::vector<unsigned char> atlasPixels() {
    std::vector<unsigned char> rgba(256*256*4,255);
    auto pixel=[&](int tile,int x,int y,int r,int g,int b){
        if(x<0||y<0||x>=64||y>=64)return;
        size_t i=((tile/4*64+y)*256+tile%4*64+x)*4;
        rgba[i]=static_cast<unsigned char>(bound(float(r),0,255));rgba[i+1]=static_cast<unsigned char>(bound(float(g),0,255));rgba[i+2]=static_cast<unsigned char>(bound(float(b),0,255));
    };
    auto glyph=[](char c)->unsigned {
        switch(c){
        case '0':return 0x7b6f;case '1':return 0x2492;case '2':return 0x73e7;case '3':return 0x73cf;case '4':return 0x5bc9;
        case '5':return 0x79cf;case '6':return 0x79ef;case '7':return 0x7249;case '8':return 0x7bef;case '9':return 0x7bcf;
        case 'K':return 0x5bad;case 'M':return 0x5fed;case 'H':return 0x5bed;case 'R':return 0x7bad;case 'P':return 0x7be4;
        case 'F':return 0x79a4;case 'A':return 0x2bed;default:return 0;
        }
    };
    auto text=[&](int tile,int x,int y,const char* label){
        for(int k=0;label[k];++k){unsigned bits=glyph(label[k]);
            for(int row=0;row<5;++row)for(int col=0;col<3;++col)if(bits&(1u<<(14-row*3-col)))pixel(tile,x+k*4+col,y+row,217,215,184);
        }
    };
    for(int tile=0;tile<16;++tile)for(int y=0;y<64;++y)for(int x=0;x<64;++x){
        unsigned hash=(unsigned(x)*1973u+unsigned(y)*9277u+unsigned(tile)*26699u)*0x45d9f3bu;
        int n=int((hash>>24)&15)-7,v=128;
        switch(tile){
        case Plastic:v=218+n;break;
        case Vinyl:v=217+n+(y%12==0?-18:0);break;
        case Cloth:v=200+n+((x+y)%4<2?12:-12);break;
        case Wood:v=217+n+int(14*std::sin(x*.16f+std::sin(y*.17f)));break;
        case Metal:v=235+(y%3==0?-15:0);break;
        case Leather:v=212+n+(x%29==0?-25:0);break;
        case Carpet:v=183+n*2;break;
        case Headliner:v=231+n/2;break;
        case SpeedDial:case RpmDial:case SmallDial:case Radio:v=25;break;
        default:break;
        }
        pixel(tile,x,y,v,v,v);
    }
    for(int tile:{int(SpeedDial),int(RpmDial),int(SmallDial)}){
        for(int y=0;y<64;++y)for(int x=0;x<64;++x){
            float r=std::sqrt(float((x-32)*(x-32)+(y-32)*(y-32)));
            if(r>28 && r<31)pixel(tile,x,y,144,145,132);
            else if(r<28)pixel(tile,x,y,14,18,16);
        }
        for(int tick=0;tick<=40;++tick){
            float a=(210-tick*6)*pi/180;
            for(int rad=tick%5==0?23:26;rad<=28;++rad){
                int x=int(std::lround(32+std::cos(a)*rad)),y=int(std::lround(32-std::sin(a)*rad));
                pixel(tile,x,y,tick>34?205:216,tick>34?83:215,tick>34?47:185);
            }
        }
        for(int i=0;i<=4;++i){
            float a=(210-i*60)*pi/180;char label[8];
            std::snprintf(label,sizeof(label),"%d",tile==SpeedDial?i*50:i*2);
            int len=0;while(label[len])++len;
            text(tile,int(32+std::cos(a)*18)-len*2,int(32-std::sin(a)*18)-2,label);
        }
        text(tile,26,42,tile==SpeedDial?"KMH":"RPM");
        for(int y=49;y<54;++y)for(int x=23;x<42;++x)pixel(tile,x,y,75,81,64);
    }
    for(int y=3;y<61;++y)for(int x=3;x<61;++x){
        int v=(x<5||x>58||y<5||y>58)?141:32;
        pixel(Radio,x,y,v,v,v);
    }
    for(int y=11;y<31;++y)for(int x=11;x<53;++x){
        int v=y==22||x%6==0?130:59;pixel(Radio,x,y,v,v+4,v-12);
    }
    text(Radio,13,13,"FM");
    for(int k=0;k<5;++k)for(int y=40;y<46;++y)for(int x=10+k*9;x<16+k*9;++x)pixel(Radio,x,y,130,128,112);
    return rgba;
}
}
