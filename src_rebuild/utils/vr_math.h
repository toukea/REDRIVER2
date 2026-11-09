#pragma once
// Small, dependency-free maths used by the OpenXR view (utils/vr_openxr.cpp) and by
// the game camera (Game/C/vrview.c). Conventions:
//  * OpenXR / "tracking" space: X right, Y up, -Z forward, metres.
//  * Game camera space (GTE): X right, Y down, Z forward, game units.
//  * Matrices are column-major float[16] (OpenGL), m[col*4+row].
#include <cmath>

namespace vrm {

struct Vec3 { float x=0, y=0, z=0; Vec3() {} Vec3(float a,float b,float c):x(a),y(b),z(c){} };
struct Quat { float x=0, y=0, z=0, w=1; Quat() {} Quat(float a,float b,float c,float d):x(a),y(b),z(c),w(d){} };
struct Pose { Quat q; Vec3 p; };
struct Mat3 { float m[3][3]; };   // row-major, m[row][col]
struct Mat4 { float m[16]; };     // column-major

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x*s,a.y*s,a.z*s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline float length(Vec3 a) { return std::sqrt(dot(a,a)); }
inline Vec3 normalize(Vec3 a) { float l=length(a); return l>1e-9f?a*(1.f/l):Vec3(0,0,0); }

inline Quat normalize(Quat q) {
    float l=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if (l<1e-9f) return Quat();
    return {q.x/l,q.y/l,q.z/l,q.w/l};
}
inline Quat mul(Quat a, Quat b) {
    return { a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
             a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
             a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
             a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z };
}
inline Quat conj(Quat q) { return {-q.x,-q.y,-q.z,q.w}; }
inline Vec3 rotate(Quat q, Vec3 v) {
    Vec3 u(q.x,q.y,q.z); Vec3 t=cross(u,v)*2.f;
    return v+t*q.w+cross(u,t);
}
inline Quat axisAngle(Vec3 axis, float radians) {
    axis=normalize(axis); float s=std::sin(radians*.5f);
    return {axis.x*s,axis.y*s,axis.z*s,std::cos(radians*.5f)};
}
inline Quat slerp(Quat a, Quat b, float t) {
    float d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if (d<0) { b={-b.x,-b.y,-b.z,-b.w}; d=-d; }
    if (d>.9995f) return normalize(Quat(a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t));
    float th=std::acos(d), s=std::sin(th), wa=std::sin((1-t)*th)/s, wb=std::sin(t*th)/s;
    return {a.x*wa+b.x*wb,a.y*wa+b.y*wb,a.z*wa+b.z*wb,a.w*wa+b.w*wb};
}

// Pose algebra: a pose maps local -> parent: x' = q*x + p.
inline Pose compose(const Pose& a, const Pose& b) { Pose r; r.q=normalize(mul(a.q,b.q)); r.p=a.p+rotate(a.q,b.p); return r; }
inline Pose inverse(const Pose& a) { Pose r; r.q=conj(a.q); r.p=rotate(r.q,a.p*-1.f); return r; }
inline Vec3 apply(const Pose& a, Vec3 v) { return a.p+rotate(a.q,v); }

// Keep only the rotation about the vertical (Y up) axis: recentring and HUD anchors
// must stay level whatever the head pitch/roll was at that moment.
inline Quat yawOnly(Quat q) {
    Vec3 f=rotate(q,Vec3(0,0,-1)); f.y=0;
    if (length(f)<1e-4f) return Quat();
    f=normalize(f);
    return axisAngle(Vec3(0,1,0),std::atan2(-f.x,-f.z));
}

inline Mat3 toMat3(Quat q) {
    q=normalize(q); Mat3 r;
    float xx=q.x*q.x,yy=q.y*q.y,zz=q.z*q.z,xy=q.x*q.y,xz=q.x*q.z,yz=q.y*q.z,wx=q.w*q.x,wy=q.w*q.y,wz=q.w*q.z;
    r.m[0][0]=1-2*(yy+zz); r.m[0][1]=2*(xy-wz);   r.m[0][2]=2*(xz+wy);
    r.m[1][0]=2*(xy+wz);   r.m[1][1]=1-2*(xx+zz); r.m[1][2]=2*(yz-wx);
    r.m[2][0]=2*(xz-wy);   r.m[2][1]=2*(yz+wx);   r.m[2][2]=1-2*(xx+yy);
    return r;
}
inline Quat fromMat3(const Mat3& a) {
    const float (*m)[3]=a.m; Quat q; float t=m[0][0]+m[1][1]+m[2][2];
    if (t>0) { float s=std::sqrt(t+1.f)*2; q={(m[2][1]-m[1][2])/s,(m[0][2]-m[2][0])/s,(m[1][0]-m[0][1])/s,.25f*s}; }
    else if (m[0][0]>m[1][1] && m[0][0]>m[2][2]) { float s=std::sqrt(1.f+m[0][0]-m[1][1]-m[2][2])*2; q={.25f*s,(m[0][1]+m[1][0])/s,(m[0][2]+m[2][0])/s,(m[2][1]-m[1][2])/s}; }
    else if (m[1][1]>m[2][2]) { float s=std::sqrt(1.f+m[1][1]-m[0][0]-m[2][2])*2; q={(m[0][1]+m[1][0])/s,.25f*s,(m[1][2]+m[2][1])/s,(m[0][2]-m[2][0])/s}; }
    else { float s=std::sqrt(1.f+m[2][2]-m[0][0]-m[1][1])*2; q={(m[0][2]+m[2][0])/s,(m[1][2]+m[2][1])/s,.25f*s,(m[1][0]-m[0][1])/s}; }
    return normalize(q);
}
inline Mat3 mul(const Mat3& a, const Mat3& b) {
    Mat3 r; for(int i=0;i<3;++i)for(int j=0;j<3;++j){ r.m[i][j]=0; for(int k=0;k<3;++k) r.m[i][j]+=a.m[i][k]*b.m[k][j]; } return r;
}
inline Mat3 transpose(const Mat3& a) { Mat3 r; for(int i=0;i<3;++i)for(int j=0;j<3;++j) r.m[i][j]=a.m[j][i]; return r; }
inline Vec3 mul(const Mat3& a, Vec3 v) {
    return {a.m[0][0]*v.x+a.m[0][1]*v.y+a.m[0][2]*v.z, a.m[1][0]*v.x+a.m[1][1]*v.y+a.m[1][2]*v.z, a.m[2][0]*v.x+a.m[2][1]*v.y+a.m[2][2]*v.z};
}
// Re-orthonormalise a rotation read back from fixed point (Gram-Schmidt on rows).
inline Mat3 orthonormalRows(const Mat3& a) {
    Vec3 r0(a.m[0][0],a.m[0][1],a.m[0][2]), r2(a.m[2][0],a.m[2][1],a.m[2][2]);
    r2=normalize(r2); r0=normalize(r0-r2*dot(r0,r2)); Vec3 r1=cross(r2,r0);
    Mat3 r; r.m[0][0]=r0.x;r.m[0][1]=r0.y;r.m[0][2]=r0.z; r.m[1][0]=r1.x;r.m[1][1]=r1.y;r.m[1][2]=r1.z; r.m[2][0]=r2.x;r.m[2][1]=r2.y;r.m[2][2]=r2.z;
    return r;
}

// Game camera axes (Y down, Z forward) <-> tracking axes (Y up, -Z forward): S = diag(1,-1,-1).
inline Vec3 trackingToGame(Vec3 v) { return {v.x,-v.y,-v.z}; }
inline Mat3 trackingRotationToGame(Quat q) {   // S * R * S
    Mat3 r=toMat3(q);
    r.m[0][1]=-r.m[0][1]; r.m[0][2]=-r.m[0][2]; r.m[1][0]=-r.m[1][0]; r.m[2][0]=-r.m[2][0];
    return r;
}

// World->camera rotation of the head: head rotation H (camera-base frame) applied to a
// base world->camera matrix W: W' = H^T W.
inline Mat3 headWorldToCamera(const Mat3& baseWorldToCamera, Quat headInBase) {
    return mul(transpose(trackingRotationToGame(headInBase)),baseWorldToCamera);
}
// Driver 2 frustum convention (draw.c InitFrustrumMatrix): forward (x,z) = (-sin a, cos a).
// Returns the yaw angle in radians for a world->camera matrix (row 2 = forward).
inline float frustumYaw(const Mat3& worldToCamera) { return std::atan2(-worldToCamera.m[2][0],worldToCamera.m[2][2]); }
// Yaw-only world->camera matrix with the same forward heading (PSX RotMatrixY layout).
inline Mat3 levelWorldToCamera(const Mat3& worldToCamera) {
    float a=frustumYaw(worldToCamera), c=std::cos(a), s=std::sin(a);
    Mat3 r={{{c,0,s},{0,1,0},{-s,0,c}}};
    return r;
}

// --- 4x4 helpers for the GPU ---
inline Mat4 identity4() { Mat4 r={}; r.m[0]=r.m[5]=r.m[10]=r.m[15]=1; return r; }
inline Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r; for(int c=0;c<4;++c)for(int row=0;row<4;++row){ float s=0; for(int k=0;k<4;++k) s+=a.m[k*4+row]*b.m[c*4+k]; r.m[c*4+row]=s; } return r;
}
inline Mat4 fromPose(const Pose& p) {
    Mat3 r=toMat3(p.q); Mat4 m=identity4();
    for(int row=0;row<3;++row)for(int c=0;c<3;++c) m.m[c*4+row]=r.m[row][c];
    m.m[12]=p.p.x; m.m[13]=p.p.y; m.m[14]=p.p.z;
    return m;
}
inline Mat4 scale4(float x,float y,float z) { Mat4 m=identity4(); m.m[0]=x; m.m[5]=y; m.m[10]=z; return m; }
inline Vec3 transformPoint(const Mat4& m, Vec3 v) {
    return {m.m[0]*v.x+m.m[4]*v.y+m.m[8]*v.z+m.m[12], m.m[1]*v.x+m.m[5]*v.y+m.m[9]*v.z+m.m[13], m.m[2]*v.x+m.m[6]*v.y+m.m[10]*v.z+m.m[14]};
}
// OpenGL projection from OpenXR asymmetric field of view (angles in radians, left/down negative).
inline Mat4 projectionFov(float left, float right, float up, float down, float nearZ, float farZ) {
    float tl=std::tan(left), tr=std::tan(right), tu=std::tan(up), td=std::tan(down);
    float w=tr-tl, h=tu-td; Mat4 m={};
    m.m[0]=2/w; m.m[8]=(tr+tl)/w;
    m.m[5]=2/h; m.m[9]=(tu+td)/h;
    m.m[10]=-(farZ+nearZ)/(farZ-nearZ); m.m[14]=-2*farZ*nearZ/(farZ-nearZ);
    m.m[11]=-1;
    return m;
}
// Game camera space (GTE units / 128, as stored by PGXP) -> eye space in metres.
// gen = head pose used to build the frame, eye = current eye pose (same space).
inline Mat4 worldView(const Pose& eye, const Pose& gen, float metresPerPgxpUnit) {
    float s=metresPerPgxpUnit;
    return mul(fromPose(compose(inverse(eye),gen)),scale4(s,-s,-s));
}

} // namespace vrm
