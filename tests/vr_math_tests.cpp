// Axis and pose conventions of the OpenXR view: a sign error here turns the world the wrong
// way when the head moves, so every convention is pinned by a test.
#include "../src_rebuild/utils/vr_math.h"
#include <cstdio>
#include <cmath>

static int failures = 0, checks = 0;
static void expect(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL: %s\n", what); } }
static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }
static bool near(vrm::Vec3 a, vrm::Vec3 b, float eps = 1e-4f) { return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps); }
static const float pi = 3.14159265f;

int main() {
    using namespace vrm;
    // Quaternion and matrix agree.
    Quat q = normalize(Quat(.2f, -.4f, .1f, .9f));
    Vec3 v(.3f, -1.2f, 2.f);
    expect(near(rotate(q, v), mul(toMat3(q), v)), "rotate == toMat3 * v");
    expect(near(rotate(fromMat3(toMat3(q)), v), rotate(q, v)), "fromMat3 round trip");

    // Pose algebra.
    Pose a; a.q = axisAngle(Vec3(0, 1, 0), .7f); a.p = Vec3(1, 2, 3);
    Pose b; b.q = axisAngle(Vec3(1, 0, 0), -.3f); b.p = Vec3(-.5f, .1f, 0);
    expect(near(apply(compose(a, b), v), apply(a, apply(b, v))), "compose applies right then left");
    expect(near(apply(compose(inverse(a), a), v), v), "inverse cancels");

    // Turning the head left (+yaw about tracking +Y) turns the game camera towards world -X.
    Mat3 identity = toMat3(Quat());
    Mat3 left = headWorldToCamera(identity, axisAngle(Vec3(0, 1, 0), pi / 6));
    expect(near(Vec3(left.m[2][0], left.m[2][1], left.m[2][2]), Vec3(-std::sin(pi / 6), 0, std::cos(pi / 6))), "head yaw left looks to -X");
    // Looking up (+pitch about tracking +X) looks towards game -Y (Y is down in the GTE).
    Mat3 up = headWorldToCamera(identity, axisAngle(Vec3(1, 0, 0), pi / 6));
    expect(near(Vec3(up.m[2][0], up.m[2][1], up.m[2][2]), Vec3(0, -std::sin(pi / 6), std::cos(pi / 6))), "head pitch up looks to -Y");
    // Rolling the head right (clockwise seen from behind = -Z axis... tracking -roll about +Z) keeps forward.
    Mat3 roll = headWorldToCamera(identity, axisAngle(Vec3(0, 0, 1), -pi / 8));
    expect(near(Vec3(roll.m[2][0], roll.m[2][1], roll.m[2][2]), Vec3(0, 0, 1)), "roll keeps forward");

    // Driver 2 frustum yaw convention: forward (x,z) = (-sin a, cos a).
    float yaw = frustumYaw(left);
    expect(near(-std::sin(yaw), left.m[2][0]) && near(std::cos(yaw), left.m[2][2]), "frustumYaw convention");
    Mat3 pitchedYawed = mul(transpose(toMat3(axisAngle(Vec3(1, 0, 0), .4f))), left);
    Mat3 level = levelWorldToCamera(pitchedYawed);
    expect(near(level.m[1][1], 1) && near(level.m[2][1], 0), "level matrix keeps the horizon");
    expect(near(frustumYaw(level), frustumYaw(pitchedYawed)), "level matrix keeps the heading");

    // Tracking (Y up, -Z forward) to game (Y down, Z forward).
    expect(near(trackingToGame(Vec3(1, 2, 3)), Vec3(1, -2, -3)), "trackingToGame");

    // A point 10 m straight ahead of the generation camera, seen by the same pose.
    Pose gen; Mat4 view = worldView(gen, gen, .01f);
    expect(near(transformPoint(view, Vec3(0, 0, 1000)), Vec3(0, 0, -10)), "ahead maps to -Z");
    expect(near(transformPoint(view, Vec3(0, -100, 1000)), Vec3(0, 1, -10)), "game up (-Y) maps to tracking +Y");
    // Right eye 32 mm to the right: an object ahead appears to its left.
    Pose rightEye = gen; rightEye.p = Vec3(.032f, 0, 0);
    expect(near(transformPoint(worldView(rightEye, gen, .01f), Vec3(0, 0, 1000)), Vec3(-.032f, 0, -10)), "parallax");
    // The head turned left since the frame was built: the scene moves right in view.
    Pose turned = gen; turned.q = axisAngle(Vec3(0, 1, 0), pi / 12);
    Vec3 seen = transformPoint(worldView(turned, gen, .01f), Vec3(0, 0, 1000));
    expect(seen.x > 0 && seen.z < 0, "late head turn reprojects the frame");

    // Asymmetric projection: the left edge of the field maps to NDC -1.
    Mat4 p = projectionFov(-.9f, .7f, .8f, -.85f, .05f, 1000.f);
    Vec3 edge(std::tan(-.9f), 0, -1);
    float cx = p.m[0] * edge.x + p.m[8] * edge.z, cw = p.m[11] * edge.z;
    expect(near(cx / cw, -1), "projection left edge");
    Vec3 top(0, std::tan(.8f), -1);
    float cy = p.m[5] * top.y + p.m[9] * top.z, cw2 = p.m[11] * top.z;
    expect(near(cy / cw2, 1), "projection top edge");
    float cz = p.m[10] * -.05f + p.m[14], cwz = p.m[11] * -.05f;
    expect(near(cz / cwz, -1), "near plane depth");

    // Recentring keeps only the yaw.
    Quat look = mul(axisAngle(Vec3(0, 1, 0), .6f), axisAngle(Vec3(1, 0, 0), .5f));
    Quat yo = yawOnly(look);
    Vec3 f = rotate(yo, Vec3(0, 0, -1)), fl = rotate(look, Vec3(0, 0, -1));
    expect(near(f.y, 0) && near(std::atan2(f.x, f.z), std::atan2(fl.x, fl.z)), "yawOnly");

    // Slerp end points and orthonormalisation.
    expect(near(rotate(slerp(Quat(), q, 1), v), rotate(q, v)), "slerp end");
    Mat3 noisy = toMat3(q); noisy.m[0][0] += .01f; noisy.m[2][1] -= .01f;
    Mat3 clean = orthonormalRows(noisy);
    Vec3 r0(clean.m[0][0], clean.m[0][1], clean.m[0][2]), r1(clean.m[1][0], clean.m[1][1], clean.m[1][2]), r2(clean.m[2][0], clean.m[2][1], clean.m[2][2]);
    expect(near(dot(r0, r1), 0) && near(dot(r1, r2), 0) && near(length(r0), 1), "orthonormalRows");

    std::printf("%d/%d VR maths checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
