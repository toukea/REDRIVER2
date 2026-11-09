// OpenXR virtual reality mode for REDRIVER2 (Windows, OpenGL).
//
// The game keeps drawing one PSX frame per 30 Hz simulation step. PsyCross records the PGXP
// geometry of that frame (camera-space positions before perspective); here the recorded frame is
// replayed for each eye at the headset rate, with the pose delta between the head pose that built
// the frame and the current eye pose. Head rotation therefore stays fully reactive between
// simulation steps, without stepping the game faster (wheel FFB, replays and missions unchanged).
//
// No OpenXR SDK binary is needed: the active runtime is found like the Khronos loader does
// (XR_RUNTIME_JSON, then the ActiveRuntime registry value) and negotiated directly. An
// openxr_loader.dll placed next to the executable is used instead when present (API layers).
#include "vr_openxr.h"
#include "ini.h"
#include "wheel.h"

#include <SDL.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cstdarg>
#include <algorithm>

#include "PsyX/PsyX_public.h"
#include "PsyX/PsyX_render.h"
#include "PsyX/PsyX_globals.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <unknwn.h>
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "advapi32.lib")
#endif


#ifdef _WIN32
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#endif
#ifndef GL_DEPTH_CLAMP
#define GL_DEPTH_CLAMP 0x864F
#endif
#define XR_NO_PROTOTYPES
#include "openxr/openxr.h"
#include "openxr/openxr_platform.h"
#include "openxr/openxr_loader_negotiation.h"

extern bool (*g_wheelExtraFocus)();

namespace {

struct Config {
    int launcher = 1;               // ask "flat screen or VR" at start
    float resolutionScale = 1.f;
    int scrZ = 90;                  // GTE projection distance while head-tracked (wide culling)
    float unitsPerMetre = 135.f;    // 0 = use the imported cockpit scale of the car
    float worldScale = 1.f;
    float horizonLock = 1.f;
    vrm::Vec3 seat;
    float hudDistance = 2.6f, hudWidth = 2.6f, hudHeight = .05f;
    float cinemaDistance = 2.6f, cinemaWidth = 3.4f;
    int mapOnDashboard = 1;
    int hideWheel = 0;
    int recenterButton = -1;        // optional wheel button index (see WheelConfig)
    float nearPlane = .03f;
} cfg;

std::string iniPath = "vr.ini";
bool requested = false;
bool initialised = false;

void logInfo(const char* fmt, ...) {
    char buffer[1024]; va_list args; va_start(args, fmt); vsnprintf(buffer, sizeof(buffer), fmt, args); va_end(args);
    PsyX_Log_Info("VR: %s\n", buffer);
}
void logError(const char* fmt, ...) {
    char buffer[1024]; va_list args; va_start(args, fmt); vsnprintf(buffer, sizeof(buffer), fmt, args); va_end(args);
    PsyX_Log_Error("VR: %s\n", buffer);
}

float readFloat(ini_t* ini, const char* key, float fallback, float lo, float hi) {
    const char* s = ini ? ini_get(ini, "vr", key) : nullptr;
    if (!s) return fallback;
    char* end = nullptr; float v = std::strtof(s, &end);
    if (end == s || !std::isfinite(v)) return fallback;
    return v < lo ? lo : v > hi ? hi : v;
}

void loadConfig() {
    ini_t* ini = ini_load(iniPath.c_str());
    if (!ini) return;
    cfg.launcher = int(readFloat(ini, "launcher", 1, 0, 1));
    cfg.resolutionScale = readFloat(ini, "resolution_scale", 1, .5f, 2);
    cfg.scrZ = int(readFloat(ini, "scr_z", 90, 64, 256));
    cfg.unitsPerMetre = readFloat(ini, "units_per_metre", 135, 0, 400);
    cfg.worldScale = readFloat(ini, "world_scale", 1, .5f, 2);
    cfg.horizonLock = readFloat(ini, "horizon_lock", 1, 0, 1);
    cfg.seat = {readFloat(ini, "seat_x", 0, -.5f, .5f), readFloat(ini, "seat_y", 0, -.5f, .5f), readFloat(ini, "seat_z", 0, -.5f, .5f)};
    cfg.hudDistance = readFloat(ini, "hud_distance", 2.6f, .5f, 10);
    cfg.hudWidth = readFloat(ini, "hud_width", 2.6f, .3f, 10);
    cfg.hudHeight = readFloat(ini, "hud_height", .05f, -2, 2);
    cfg.cinemaDistance = readFloat(ini, "cinema_distance", 2.6f, .8f, 10);
    cfg.cinemaWidth = readFloat(ini, "cinema_width", 3.4f, .5f, 12);
    cfg.mapOnDashboard = int(readFloat(ini, "map_on_dashboard", 1, 0, 1));
    cfg.hideWheel = int(readFloat(ini, "hide_wheel", 0, 0, 1));
    cfg.recenterButton = int(readFloat(ini, "recenter_button", -1, -1, 63));
    cfg.nearPlane = readFloat(ini, "near_plane", .03f, .005f, .5f);
    ini_free(ini);
}

// Rewrites the seat_* values in place, keeping the user's comments.
void saveSeat() {
    FILE* f = fopen(iniPath.c_str(), "rb");
    std::vector<std::string> lines;
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) { std::string s(line); while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back(); lines.push_back(s); }
        fclose(f);
    }
    const char* keys[3] = {"seat_x", "seat_y", "seat_z"};
    float values[3] = {cfg.seat.x, cfg.seat.y, cfg.seat.z};
    for (int k = 0; k < 3; ++k) {
        char text[64]; snprintf(text, sizeof(text), "%s=%.3f", keys[k], values[k]);
        bool done = false;
        for (auto& l : lines) if (!l.compare(0, strlen(keys[k]), keys[k]) && l.size() > strlen(keys[k]) && (l[strlen(keys[k])] == '=' || l[strlen(keys[k])] == ' ')) { l = text; done = true; }
        if (!done) {
            size_t at = lines.size();
            for (size_t i = 0; i < lines.size(); ++i) if (lines[i] == "[vr]") { at = i + 1; break; }
            if (at == lines.size() && (lines.empty() || lines[0] != "[vr]")) { bool has = false; for (auto& l : lines) if (l == "[vr]") has = true; if (!has) { lines.push_back("[vr]"); at = lines.size(); } }
            lines.insert(lines.begin() + at, text);
        }
    }
    f = fopen(iniPath.c_str(), "wb");
    if (!f) return;
    for (auto& l : lines) fprintf(f, "%s\r\n", l.c_str());
    fclose(f);
}

double seconds() { return double(SDL_GetPerformanceCounter()) / double(SDL_GetPerformanceFrequency()); }

#ifdef _WIN32
// ---------------------------------------------------------------------------------------------
// Runtime discovery and OpenXR entry points

HMODULE runtimeLibrary = nullptr;
PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr_ = nullptr;

#define XR_FUNCTIONS(X) \
    X(xrGetInstanceProperties) X(xrGetSystem) X(xrGetSystemProperties) X(xrDestroyInstance) \
    X(xrCreateSession) X(xrDestroySession) X(xrBeginSession) X(xrEndSession) X(xrRequestExitSession) \
    X(xrPollEvent) X(xrCreateReferenceSpace) X(xrDestroySpace) X(xrLocateSpace) \
    X(xrEnumerateViewConfigurationViews) X(xrEnumerateSwapchainFormats) X(xrCreateSwapchain) \
    X(xrDestroySwapchain) X(xrEnumerateSwapchainImages) X(xrAcquireSwapchainImage) \
    X(xrWaitSwapchainImage) X(xrReleaseSwapchainImage) X(xrWaitFrame) X(xrBeginFrame) X(xrEndFrame) \
    X(xrLocateViews) X(xrResultToString) X(xrGetOpenGLGraphicsRequirementsKHR)

#define XR_DECLARE(name) PFN_##name name##_ = nullptr;
XR_FUNCTIONS(XR_DECLARE)
PFN_xrCreateInstance xrCreateInstance_ = nullptr;
PFN_xrEnumerateInstanceExtensionProperties xrEnumerateInstanceExtensionProperties_ = nullptr;

std::string lastError;

std::string resultText(XrInstance instance, XrResult r) {
    char text[XR_MAX_RESULT_STRING_SIZE] = {};
    if (xrResultToString_ && instance != XR_NULL_HANDLE && XR_SUCCEEDED(xrResultToString_(instance, r, text))) return text;
    snprintf(text, sizeof(text), "XrResult %d", int(r));
    return text;
}

std::string manifestLibrary(const std::string& manifest) {
    FILE* f = fopen(manifest.c_str(), "rb");
    if (!f) return std::string();
    std::string json; char buffer[4096]; size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) json.append(buffer, n);
    fclose(f);
    size_t key = json.find("\"library_path\"");
    if (key == std::string::npos) return std::string();
    size_t q = json.find('"', json.find(':', key) + 1);
    if (q == std::string::npos) return std::string();
    std::string path;
    for (size_t i = q + 1; i < json.size() && json[i] != '"'; ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) { ++i; path += json[i] == '/' ? '/' : json[i]; }
        else path += json[i];
    }
    bool absolute = path.size() > 2 && (path[1] == ':' || (path[0] == '\\' && path[1] == '\\'));
    if (!absolute) {
        size_t slash = manifest.find_last_of("\\/");
        path = (slash == std::string::npos ? std::string() : manifest.substr(0, slash + 1)) + path;
    }
    return path;
}

bool loadRuntime() {
    if (xrGetInstanceProcAddr_) return true;
    // 1. The official loader, when shipped next to the game (enables API layers such as OpenXR Toolkit).
    runtimeLibrary = LoadLibraryA("openxr_loader.dll");
    if (runtimeLibrary) {
        xrGetInstanceProcAddr_ = (PFN_xrGetInstanceProcAddr)GetProcAddress(runtimeLibrary, "xrGetInstanceProcAddr");
        if (xrGetInstanceProcAddr_) { logInfo("using openxr_loader.dll"); return true; }
        FreeLibrary(runtimeLibrary); runtimeLibrary = nullptr;
    }
    // 2. The active runtime of this process' bitness, as the Khronos loader finds it.
    std::string manifest;
    if (const char* env = getenv("XR_RUNTIME_JSON")) manifest = env;
    if (manifest.empty()) {
        char value[1024] = {}; DWORD size = sizeof(value);
        if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Khronos\\OpenXR\\1", "ActiveRuntime", RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS)
            manifest = value;
    }
    if (manifest.empty()) { lastError = "aucun runtime OpenXR actif (Meta Horizon Link, SteamVR ou Virtual Desktop)"; return false; }
    std::string library = manifestLibrary(manifest);
    if (library.empty()) { lastError = "manifeste OpenXR illisible : " + manifest; return false; }
    runtimeLibrary = LoadLibraryExA(library.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!runtimeLibrary) { lastError = "impossible de charger " + library; return false; }
    auto negotiate = (PFN_xrNegotiateLoaderRuntimeInterface)GetProcAddress(runtimeLibrary, "xrNegotiateLoaderRuntimeInterface");
    if (!negotiate) { lastError = "runtime OpenXR sans xrNegotiateLoaderRuntimeInterface : " + library; return false; }
    XrNegotiateLoaderInfo info = {};
    info.structType = XR_LOADER_INTERFACE_STRUCT_LOADER_INFO;
    info.structVersion = XR_LOADER_INFO_STRUCT_VERSION;
    info.structSize = sizeof(info);
    info.minInterfaceVersion = 1;
    info.maxInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
    info.minApiVersion = XR_MAKE_VERSION(1, 0, 0);
    info.maxApiVersion = XR_MAKE_VERSION(1, 0x3ff, 0xfff);
    XrNegotiateRuntimeRequest request = {};
    request.structType = XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST;
    request.structVersion = XR_RUNTIME_INFO_STRUCT_VERSION;
    request.structSize = sizeof(request);
    if (XR_FAILED(negotiate(&info, &request)) || !request.getInstanceProcAddr) { lastError = "négociation refusée par " + library; return false; }
    xrGetInstanceProcAddr_ = request.getInstanceProcAddr;
    logInfo("runtime %s (%s)", manifest.c_str(), library.c_str());
    return true;
}

// ---------------------------------------------------------------------------------------------
// Session state

XrInstance instance = XR_NULL_HANDLE;
XrSystemId systemId = XR_NULL_SYSTEM_ID;
XrSession session = XR_NULL_HANDLE;
XrSpace localSpace = XR_NULL_HANDLE, viewSpace = XR_NULL_HANDLE;
XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
bool running = false, userExit = false;
int64_t colorFormat = 0;

struct Target {
    XrSwapchain swapchain = XR_NULL_HANDLE;
    std::vector<XrSwapchainImageOpenGLKHR> images;
    int width = 0, height = 0;
    GLuint fbo = 0, depth = 0;
};
Target eyes[2], cinema;
GLuint cinemaCopy = 0, cinemaCopyFbo = 0; int cinemaCopyW = 0, cinemaCopyH = 0;

vrm::Pose lastHead, origin, genHead;
bool haveHead = false, centred = false;
float genMetresPerUnit = 1.f / 135;   // game units -> metres, for the frame being shown
bool tracked = false, trackedShown = false, pacedSwap = false;
double simClock = 0, lastTrackedSwap = -10;

struct Stats { int swaps = 0, trackedSwaps = 0, frames = 0, rendered = 0, eyes = 0, failedLocate = 0; double since = 0; } stats;

bool mapValid = false; float mapRect[4]; vrm::Vec3 mapCentre, mapRight, mapUp;
float frameMapRect[4]; vrm::Vec3 frameMapCentre, frameMapRight, frameMapUp; bool frameMapValid = false;

vrm::Pose toPose(const XrPosef& p) { vrm::Pose r; r.q = {p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w}; r.p = {p.position.x, p.position.y, p.position.z}; return r; }
XrPosef toXrPose(const vrm::Pose& p) { XrPosef r; r.orientation = {p.q.x, p.q.y, p.q.z, p.q.w}; r.position = {p.p.x, p.p.y, p.p.z}; return r; }

bool check(XrResult r, const char* what) {
    if (XR_SUCCEEDED(r)) return true;
    lastError = std::string(what) + " : " + resultText(instance, r);
    logError("%s", lastError.c_str());
    return false;
}

bool createInstance() {
    if (instance != XR_NULL_HANDLE) return true;
    if (!loadRuntime()) return false;
    xrGetInstanceProcAddr_(XR_NULL_HANDLE, "xrCreateInstance", (PFN_xrVoidFunction*)&xrCreateInstance_);
    xrGetInstanceProcAddr_(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties", (PFN_xrVoidFunction*)&xrEnumerateInstanceExtensionProperties_);
    if (!xrCreateInstance_ || !xrEnumerateInstanceExtensionProperties_) { lastError = "runtime OpenXR incomplet"; return false; }

    uint32_t count = 0;
    xrEnumerateInstanceExtensionProperties_(nullptr, 0, &count, nullptr);
    std::vector<XrExtensionProperties> extensions(count, {XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties_(nullptr, count, &count, extensions.data());
    bool opengl = false;
    for (auto& e : extensions) if (!strcmp(e.extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME)) opengl = true;
    if (!opengl) { lastError = "le runtime OpenXR actif ne propose pas OpenGL (XR_KHR_opengl_enable)"; return false; }

    const char* enabled[] = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo create = {XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy(create.applicationInfo.applicationName, "REDRIVER2");
    create.applicationInfo.applicationVersion = 1;
    strcpy(create.applicationInfo.engineName, "PsyCross");
    create.applicationInfo.engineVersion = 1;
    create.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    create.enabledExtensionCount = 1;
    create.enabledExtensionNames = enabled;
    if (!check(xrCreateInstance_(&create, &instance), "xrCreateInstance")) return false;

#define XR_LOAD(name) xrGetInstanceProcAddr_(instance, #name, (PFN_xrVoidFunction*)&name##_);
    XR_FUNCTIONS(XR_LOAD)
#undef XR_LOAD
    XrInstanceProperties props = {XR_TYPE_INSTANCE_PROPERTIES};
    if (xrGetInstanceProperties_ && XR_SUCCEEDED(xrGetInstanceProperties_(instance, &props)))
        logInfo("instance %s %d.%d.%d", props.runtimeName, XR_VERSION_MAJOR(props.runtimeVersion), XR_VERSION_MINOR(props.runtimeVersion), XR_VERSION_PATCH(props.runtimeVersion));
    return true;
}

void destroyTarget(Target& t) {
    if (t.swapchain != XR_NULL_HANDLE) xrDestroySwapchain_(t.swapchain);
    if (t.fbo) glDeleteFramebuffers(1, &t.fbo);
    if (t.depth) glDeleteRenderbuffers(1, &t.depth);
    t = Target();
}

bool createTarget(Target& t, int width, int height, bool depth) {
    XrSwapchainCreateInfo info = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    info.format = colorFormat;
    info.sampleCount = 1;
    info.width = width; info.height = height;
    info.faceCount = 1; info.arraySize = 1; info.mipCount = 1;
    if (!check(xrCreateSwapchain_(session, &info, &t.swapchain), "xrCreateSwapchain")) return false;
    uint32_t count = 0;
    xrEnumerateSwapchainImages_(t.swapchain, 0, &count, nullptr);
    t.images.assign(count, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
    xrEnumerateSwapchainImages_(t.swapchain, count, &count, (XrSwapchainImageBaseHeader*)t.images.data());
    t.width = width; t.height = height;
    glGenFramebuffers(1, &t.fbo);
    if (depth) {
        glGenRenderbuffers(1, &t.depth);
        glBindRenderbuffer(GL_RENDERBUFFER, t.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }
    return true;
}

void destroySession() {
    for (auto& e : eyes) destroyTarget(e);
    destroyTarget(cinema);
    if (cinemaCopyFbo) { glDeleteFramebuffers(1, &cinemaCopyFbo); cinemaCopyFbo = 0; }
    if (cinemaCopy) { glDeleteTextures(1, &cinemaCopy); cinemaCopy = 0; }
    if (viewSpace != XR_NULL_HANDLE) xrDestroySpace_(viewSpace);
    if (localSpace != XR_NULL_HANDLE) xrDestroySpace_(localSpace);
    if (session != XR_NULL_HANDLE) xrDestroySession_(session);
    viewSpace = localSpace = XR_NULL_HANDLE; session = XR_NULL_HANDLE;
    running = false; haveHead = false; centred = false; sessionState = XR_SESSION_STATE_UNKNOWN;
    GR_VR_EnableRecording(0);
    GR_VR_SetSpriteDepth(0);
    PsyX_EnableSwapInterval(1);
    GR_VR_InvalidateCaches();
}

bool createSession() {
    if (session != XR_NULL_HANDLE) return true;
    if (!createInstance()) return false;
    XrSystemGetInfo sys = {XR_TYPE_SYSTEM_GET_INFO};
    sys.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrResult r = xrGetSystem_(instance, &sys, &systemId);
    if (r == XR_ERROR_FORM_FACTOR_UNAVAILABLE) { lastError = "casque non détecté : lance Meta Horizon Link, SteamVR ou Virtual Desktop et connecte le casque"; return false; }
    if (!check(r, "xrGetSystem")) return false;
    XrSystemProperties sp = {XR_TYPE_SYSTEM_PROPERTIES};
    if (XR_SUCCEEDED(xrGetSystemProperties_(instance, systemId, &sp))) logInfo("headset: %s", sp.systemName);

    XrGraphicsRequirementsOpenGLKHR req = {XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
    if (!check(xrGetOpenGLGraphicsRequirementsKHR_(instance, systemId, &req), "xrGetOpenGLGraphicsRequirementsKHR")) return false;
    GLint major = 0, minor = 0; glGetIntegerv(GL_MAJOR_VERSION, &major); glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (XR_MAKE_VERSION(major, minor, 0) < req.minApiVersionSupported)
        logError("OpenGL %d.%d is older than the runtime minimum %d.%d; trying anyway", major, minor,
            XR_VERSION_MAJOR(req.minApiVersionSupported), XR_VERSION_MINOR(req.minApiVersionSupported));

    XrGraphicsBindingOpenGLWin32KHR binding = {XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
    binding.hDC = wglGetCurrentDC();
    binding.hGLRC = wglGetCurrentContext();
    XrSessionCreateInfo create = {XR_TYPE_SESSION_CREATE_INFO};
    create.next = &binding;
    create.systemId = systemId;
    if (!check(xrCreateSession_(instance, &create, &session), "xrCreateSession")) return false;

    XrReferenceSpaceCreateInfo space = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    space.poseInReferenceSpace.orientation.w = 1;
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    if (!check(xrCreateReferenceSpace_(session, &space, &localSpace), "xrCreateReferenceSpace(LOCAL)")) { destroySession(); return false; }
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (!check(xrCreateReferenceSpace_(session, &space, &viewSpace), "xrCreateReferenceSpace(VIEW)")) { destroySession(); return false; }

    uint32_t count = 0;
    xrEnumerateSwapchainFormats_(session, 0, &count, nullptr);
    std::vector<int64_t> formats(count);
    xrEnumerateSwapchainFormats_(session, count, &count, formats.data());
    // sRGB storage with GL_FRAMEBUFFER_SRGB off: the PSX colours are written untouched and the
    // compositor decodes them as sRGB, exactly like the desktop window shows them.
    const int64_t preferred[] = {GL_SRGB8_ALPHA8, GL_RGBA8};
    colorFormat = 0;
    for (int64_t p : preferred) { for (int64_t f : formats) if (f == p) { colorFormat = p; break; } if (colorFormat) break; }
    if (!colorFormat && !formats.empty()) colorFormat = formats[0];

    XrViewConfigurationView views[2] = {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
    count = 2;
    if (!check(xrEnumerateViewConfigurationViews_(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &count, views), "xrEnumerateViewConfigurationViews")) { destroySession(); return false; }
    for (int i = 0; i < 2; ++i) {
        int w = int(views[i].recommendedImageRectWidth * cfg.resolutionScale), h = int(views[i].recommendedImageRectHeight * cfg.resolutionScale);
        w = std::max(64, std::min(w, int(views[i].maxImageRectWidth))); h = std::max(64, std::min(h, int(views[i].maxImageRectHeight)));
        if (!createTarget(eyes[i], w, h, true)) { destroySession(); return false; }
    }
    logInfo("eyes %dx%d, format 0x%x", eyes[0].width, eyes[0].height, int(colorFormat));
    origin = vrm::Pose();
    userExit = false;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Rendering

void bindTarget(Target& t, GLuint texture) {
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    if (t.depth) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, t.depth);
    glViewport(0, 0, t.width, t.height);
}

GLuint acquire(Target& t) {
    uint32_t index = 0;
    XrSwapchainImageAcquireInfo ai = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (XR_FAILED(xrAcquireSwapchainImage_(t.swapchain, &ai, &index))) return 0;
    XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wi.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage_(t.swapchain, &wi);
    return t.images[index].image;
}
void release(Target& t) {
    XrSwapchainImageReleaseInfo ri = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage_(t.swapchain, &ri);
}

// Desktop mirror: centre crop of the left eye at the window aspect.
void mirror(const Target& eye) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, eye.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    float windowAspect = float(g_windowWidth) / float(std::max(1, g_windowHeight));
    int sw = eye.width, sh = int(eye.width / windowAspect);
    if (sh > eye.height) { sh = eye.height; sw = int(eye.height * windowAspect); }
    int sx = (eye.width - sw) / 2, sy = (eye.height - sh) / 2;
    glBlitFramebuffer(sx, sy, sx + sw, sy + sh, 0, 0, g_windowWidth, g_windowHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

void drawEye(int i, const XrView& view) {
    Target& t = eyes[i];
    GLuint image = acquire(t);
    if (!image) return;
    bindTarget(t, image);
    GR_SetScissorState(0);
    glDepthMask(GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClearDepth(1);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_DEPTH_CLAMP);   // the sky dome lies far beyond any useful far plane

    vrm::Pose eye = toPose(view.pose);
    vrm::Mat4 proj = vrm::projectionFov(view.fov.angleLeft, view.fov.angleRight, view.fov.angleUp, view.fov.angleDown, cfg.nearPlane, 4000.f);

    // PGXP stores camera-space positions in game units / 128.
    vrm::Mat4 world = vrm::worldView(eye, genHead, genMetresPerUnit * 128.f);
    GR_VR_SetPassUniforms(1, world.m, proj.m);
    GR_VR_SetClip(0, nullptr);
    GR_VR_DrawRecorded(1);

    // 2D overlay: the minimap on the dashboard screen, everything else on a panel ahead of the car.
    glDepthMask(GL_FALSE);
    if (frameMapValid) {
        float x0 = frameMapRect[0], y0 = frameMapRect[1], x1 = frameMapRect[2], y1 = frameMapRect[3];
        float hw = (x1 - x0) * .5f, hh = (y1 - y0) * .5f, cx = (x0 + x1) * .5f, cy = (y0 + y1) * .5f;
        vrm::Mat4 plane = vrm::identity4();   // NDC (x,y) -> game camera units
        plane.m[0] = frameMapRight.x / hw; plane.m[1] = frameMapRight.y / hw; plane.m[2] = frameMapRight.z / hw;
        plane.m[4] = frameMapUp.x / hh;    plane.m[5] = frameMapUp.y / hh;    plane.m[6] = frameMapUp.z / hh;
        vrm::Vec3 c = frameMapCentre - frameMapRight * (cx / hw) - frameMapUp * (cy / hh);
        plane.m[12] = c.x; plane.m[13] = c.y; plane.m[14] = c.z;
        vrm::Mat4 mapView = vrm::mul(vrm::worldView(eye, genHead, genMetresPerUnit), plane);
        GR_VR_SetPassUniforms(2, mapView.m, proj.m);
        GR_VR_SetClip(1, frameMapRect);
        GR_VR_DrawRecorded(2);
    }
    vrm::Pose hud; hud.p = {0, cfg.hudHeight, -cfg.hudDistance};
    hud = vrm::compose(origin, hud);
    float halfW = cfg.hudWidth * .5f, halfH = halfW * float(g_windowHeight) / float(std::max(1, g_windowWidth));
    vrm::Mat4 hudView = vrm::mul(vrm::fromPose(vrm::compose(vrm::inverse(eye), hud)), vrm::scale4(halfW, halfH, 1));
    GR_VR_SetPassUniforms(2, hudView.m, proj.m);
    GR_VR_SetClip(frameMapValid ? -1 : 0, frameMapRect);
    GR_VR_DrawRecorded(2);
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_CLAMP);

    if (i == 0) mirror(t);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    release(t);
}

void copyWindowForCinema() {
    if (!cinemaCopy || cinemaCopyW != g_windowWidth || cinemaCopyH != g_windowHeight) {
        if (!cinemaCopy) { glGenTextures(1, &cinemaCopy); glGenFramebuffers(1, &cinemaCopyFbo); }
        cinemaCopyW = g_windowWidth; cinemaCopyH = g_windowHeight;
        glBindTexture(GL_TEXTURE_2D, cinemaCopy);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, cinemaCopyW, cinemaCopyH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
        GR_VR_InvalidateCaches();
        glBindFramebuffer(GL_FRAMEBUFFER, cinemaCopyFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, cinemaCopy, 0);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, cinemaCopyFbo);
    glBlitFramebuffer(0, 0, g_windowWidth, g_windowHeight, 0, 0, cinemaCopyW, cinemaCopyH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool drawCinema(XrCompositionLayerQuad& quad) {
    if (cinema.swapchain == XR_NULL_HANDLE && !createTarget(cinema, 1280, 720, false)) return false;
    GLuint image = acquire(cinema);
    if (!image) return false;
    bindTarget(cinema, image);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, cinemaCopyFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, cinema.fbo);
    glBlitFramebuffer(0, 0, cinemaCopyW, cinemaCopyH, 0, 0, cinema.width, cinema.height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    release(cinema);
    vrm::Pose screen; screen.p = {0, 0, -cfg.cinemaDistance};
    screen = vrm::compose(origin, screen);
    quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    quad.space = localSpace;
    quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain = cinema.swapchain;
    quad.subImage.imageRect = {{0, 0}, {cinema.width, cinema.height}};
    quad.pose = toXrPose(screen);
    quad.size = {cfg.cinemaWidth, cfg.cinemaWidth * float(cinemaCopyH) / float(std::max(1, cinemaCopyW))};
    return true;
}

void renderFrame(bool showTracked, XrTime& displayTime, XrDuration& period) {
    XrFrameWaitInfo wi = {XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState fs = {XR_TYPE_FRAME_STATE};
    if (XR_FAILED(xrWaitFrame_(session, &wi, &fs))) return;
    XrFrameBeginInfo bi = {XR_TYPE_FRAME_BEGIN_INFO};
    if (XR_FAILED(xrBeginFrame_(session, &bi))) return;
    displayTime = fs.predictedDisplayTime; period = fs.predictedDisplayPeriod;

    XrSpaceLocation head = {XR_TYPE_SPACE_LOCATION};
    if (XR_SUCCEEDED(xrLocateSpace_(viewSpace, localSpace, fs.predictedDisplayTime, &head)) &&
        (head.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
        lastHead = toPose(head.pose); haveHead = true;
        if (!centred) { origin.q = vrm::yawOnly(lastHead.q); origin.p = lastHead.p; centred = true; }   // seated start
    }

    XrCompositionLayerProjection projection = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    XrCompositionLayerProjectionView projViews[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    XrCompositionLayerQuad quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    const XrCompositionLayerBaseHeader* layers[1];
    uint32_t layerCount = 0;

    stats.frames++;
    if (fs.shouldRender) {
        stats.rendered++;
        if (showTracked) {
            XrViewLocateInfo li = {XR_TYPE_VIEW_LOCATE_INFO};
            li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            li.displayTime = fs.predictedDisplayTime;
            li.space = localSpace;
            XrViewState vs = {XR_TYPE_VIEW_STATE};
            XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
            uint32_t count = 0;
            if (XR_SUCCEEDED(xrLocateViews_(session, &li, &vs, 2, &count, views)) && count == 2 &&
                (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
                stats.eyes++;
                for (int i = 0; i < 2; ++i) {
                    drawEye(i, views[i]);
                    projViews[i].pose = views[i].pose;
                    projViews[i].fov = views[i].fov;
                    projViews[i].subImage.swapchain = eyes[i].swapchain;
                    projViews[i].subImage.imageRect = {{0, 0}, {eyes[i].width, eyes[i].height}};
                }
                GR_VR_SetPassUniforms(0, nullptr, nullptr);
                projection.space = localSpace;
                projection.viewCount = 2;
                projection.views = projViews;
                layers[layerCount++] = (const XrCompositionLayerBaseHeader*)&projection;
            } else stats.failedLocate++;
        } else if (cinemaCopy && drawCinema(quad)) {
            layers[layerCount++] = (const XrCompositionLayerBaseHeader*)&quad;
        }
        glViewport(0, 0, g_windowWidth, g_windowHeight);
    }

    XrFrameEndInfo ei = {XR_TYPE_FRAME_END_INFO};
    ei.displayTime = fs.predictedDisplayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount = layerCount;
    ei.layers = layerCount ? layers : nullptr;
    xrEndFrame_(session, &ei);
}

void pollEvents() {
    if (instance == XR_NULL_HANDLE) return;
    XrEventDataBuffer event = {XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent_(instance, &event) == XR_SUCCESS) {
        switch (event.type) {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            auto* e = (XrEventDataSessionStateChanged*)&event;
            sessionState = e->state;
            logInfo("session state %d", int(sessionState));
            if (sessionState == XR_SESSION_STATE_READY && session != XR_NULL_HANDLE) {
                XrSessionBeginInfo bi = {XR_TYPE_SESSION_BEGIN_INFO};
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (check(xrBeginSession_(session, &bi), "xrBeginSession")) {
                    running = true;
                    GR_VR_EnableRecording(1);
                    PsyX_EnableSwapInterval(0);   // the headset paces the frames, not the monitor
                    simClock = seconds();
                }
            } else if (sessionState == XR_SESSION_STATE_STOPPING) {
                xrEndSession_(session);
                running = false;
                GR_VR_EnableRecording(0);
                PsyX_EnableSwapInterval(1);
            } else if (sessionState == XR_SESSION_STATE_EXITING || sessionState == XR_SESSION_STATE_LOSS_PENDING) {
                destroySession();
                logInfo(userExit ? "back to the flat screen" : "headset session ended");
            }
            break;
        }
        case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
            origin = vrm::Pose();   // the runtime recentred LOCAL itself (long press on the headset button)
            break;
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            destroySession();
            break;
        default: break;
        }
        event = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}
#endif // _WIN32

// ---------------------------------------------------------------------------------------------
// Keyboard: Ctrl+Shift+V toggles VR / flat screen, F9 recentres, numeric keypad moves the seat.

void recenter() {
    if (!haveHead) return;
    origin.q = vrm::yawOnly(lastHead.q);
    origin.p = lastHead.p;
    logInfo("recentred");
}

void toggleVR();

void handleKeys() {
    static bool toggleWas = false, recenterWas = false, buttonWas = false;
    static double seatRepeat = 0, seatChanged = 0;
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    SDL_Keymod mod = SDL_GetModState();
    bool toggle = keys[SDL_SCANCODE_V] && (mod & KMOD_CTRL) && (mod & KMOD_SHIFT);
    if (toggle && !toggleWas) toggleVR();
    toggleWas = toggle;

    bool button = cfg.recenterButton >= 0 && Wheel_RawButton(cfg.recenterButton);
    bool rc = keys[SDL_SCANCODE_F9] || button;
    if (rc && !recenterWas) recenter();
    recenterWas = rc;
    (void)buttonWas;

    double now = seconds();
    vrm::Vec3 d;
    const float step = .01f;
    if (keys[SDL_SCANCODE_KP_8]) d.z += step;
    if (keys[SDL_SCANCODE_KP_2]) d.z -= step;
    if (keys[SDL_SCANCODE_KP_6]) d.x += step;
    if (keys[SDL_SCANCODE_KP_4]) d.x -= step;
    if (keys[SDL_SCANCODE_KP_9]) d.y += step;
    if (keys[SDL_SCANCODE_KP_3]) d.y -= step;
    bool reset = keys[SDL_SCANCODE_KP_5];
    if ((d.x || d.y || d.z || reset) && now >= seatRepeat) {
        cfg.seat = reset ? vrm::Vec3() : cfg.seat + d;
        cfg.seat.x = std::max(-.5f, std::min(.5f, cfg.seat.x));
        cfg.seat.y = std::max(-.5f, std::min(.5f, cfg.seat.y));
        cfg.seat.z = std::max(-.5f, std::min(.5f, cfg.seat.z));
        seatRepeat = now + .12;
        seatChanged = now;
        logInfo("seat %.2f %.2f %.2f m", cfg.seat.x, cfg.seat.y, cfg.seat.z);
    }
    if (seatChanged > 0 && now - seatChanged > 1.0) { saveSeat(); seatChanged = 0; }
}

void swapHook() {
    handleKeys();
#ifdef _WIN32
    pollEvents();
    bool showTracked = tracked;
    tracked = false;
    frameMapValid = mapValid; mapValid = false;
    if (frameMapValid) { memcpy(frameMapRect, mapRect, sizeof(mapRect)); frameMapCentre = mapCentre; frameMapRight = mapRight; frameMapUp = mapUp; }
    GR_VR_SetSpriteDepth(0);
    if (!running) return;

    if (!showTracked || GR_VR_RecordedVertices(1) == 0) {
        showTracked = false;
        copyWindowForCinema();
    } else lastTrackedSwap = seconds();
    trackedShown = showTracked;

    // One simulation step lasts 1/30 s: present as many headset frames as fit before the next one.
    double now = seconds();
    if (showTracked) {
        simClock += 1.0 / 30.0;
        if (simClock < now - .1 || simClock > now + .2) simClock = now + 1.0 / 30.0;
    }
    for (int frame = 0; frame < 8 && running; ++frame) {
        XrTime displayTime = 0; XrDuration period = 0;
        double before = seconds();
        renderFrame(showTracked, displayTime, period);
        if (!showTracked) break;
        double p = period > 0 ? double(period) * 1e-9 : 1.0 / 90.0;
        if (before + p >= simClock) break;
        pollEvents();
    }
    pacedSwap = showTracked;
    stats.swaps++; if (showTracked) stats.trackedSwaps++;
    if (seconds() - stats.since > 5) {
        logInfo("%d swaps (%d head-tracked), %d headset frames, %d rendered, %d stereo, %d locate failures, %d world / %d 2D vertices",
            stats.swaps, stats.trackedSwaps, stats.frames, stats.rendered, stats.eyes, stats.failedLocate,
            GR_VR_RecordedVertices(1), GR_VR_RecordedVertices(0));
        stats = Stats(); stats.since = seconds();
    }
    GR_VR_InvalidateCaches();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g_windowWidth, g_windowHeight);
#endif
}

bool startVR(bool showErrors) {
#ifdef _WIN32
    lastError.clear();
    if (createSession()) { requested = true; logInfo("session created"); return true; }
    logError("unavailable: %s", lastError.c_str());
    if (showErrors) {
        std::string text = "Le mode VR n'a pas pu démarrer :\n\n" + lastError + "\n\nLe jeu continue en mode écran plat. Ctrl+Maj+V pour réessayer.";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "REDRIVER2 - Réalité virtuelle", text.c_str(), nullptr);
    }
#else
    if (showErrors) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "REDRIVER2", "Le mode VR n'est disponible que sous Windows.", nullptr);
#endif
    requested = false;
    return false;
}

void toggleVR() {
#ifdef _WIN32
    if (session != XR_NULL_HANDLE) {
        userExit = true;
        if (running) xrRequestExitSession_(session);
        else destroySession();
        requested = false;
        logInfo("leaving VR (Ctrl+Shift+V)");
    } else {
        logInfo("entering VR (Ctrl+Shift+V)");
        startVR(true);
    }
#endif
}

bool hasFocus() {
#ifdef _WIN32
    return running && sessionState == XR_SESSION_STATE_FOCUSED;
#else
    return false;
#endif
}

} // namespace

void VR_Configure(int argc, char** argv) {
    loadConfig();
    int choice = -1;   // 0 flat, 1 VR
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-vr")) choice = 1;
        else if (!strcmp(argv[i], "-flat")) choice = 0;
    }
#ifdef _WIN32
    if (choice < 0 && cfg.launcher) {
        const SDL_MessageBoxButtonData buttons[] = {
            {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Écran plat"},
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Réalité virtuelle (OpenXR)"},
        };
        SDL_MessageBoxData box = {SDL_MESSAGEBOX_INFORMATION, nullptr, "REDRIVER2",
            "Comment veux-tu jouer ?\n\n"
            "Réalité virtuelle : lance d'abord Meta Horizon Link, SteamVR ou Virtual Desktop.\n"
            "En jeu : Ctrl+Maj+V repasse en écran plat (ou revient en VR), F9 recentre la vue,\n"
            "pavé numérique 8/2/4/6/9/3 règle la position du siège, 5 la remet à zéro.",
            2, buttons, nullptr};
        int pressed = 0;
        if (SDL_ShowMessageBox(&box, &pressed) == 0) choice = pressed == 1 ? 1 : 0;
    }
#endif
    requested = choice == 1;
    g_cfg_vrModernGL = requested ? 1 : 0;
}

void VR_Init() {
    initialised = true;
    g_cfg_vrSwapHook = swapHook;
    g_wheelExtraFocus = hasFocus;
    if (requested) startVR(true);
}

void VR_Shutdown() {
#ifdef _WIN32
    if (initialised) {
        destroySession();
        if (instance != XR_NULL_HANDLE && xrDestroyInstance_) xrDestroyInstance_(instance);
        instance = XR_NULL_HANDLE;
    }
#endif
    g_cfg_vrSwapHook = nullptr;
    g_wheelExtraFocus = nullptr;
}

bool VR_Requested() { return requested; }
bool VR_Active() {
#ifdef _WIN32
    return running && haveHead;
#else
    return false;
#endif
}
// True once per presented head-tracked frame: the headset loop has already waited for the
// step's duration, so the game must not wait again (and must not run ahead without a swap).
bool VR_PacesGame() {
    if (!VR_Active() || !pacedSwap) return false;
    pacedSwap = false;
    return true;
}
bool VR_InputFocused() { return hasFocus(); }

float VR_UnitsPerMetre(float cockpitUnitsPerMetre) {
    float upm = cfg.unitsPerMetre > 0 ? cfg.unitsPerMetre : (cockpitUnitsPerMetre > 0 ? cockpitUnitsPerMetre : 135.f);
    return upm / cfg.worldScale;
}

bool VR_BeginTrackedFrame(float unitsPerMetre, VRHead* head) {
    if (!VR_Active()) return false;
    genHead = lastHead;
    genMetresPerUnit = 1.f / std::max(1.f, unitsPerMetre);
    tracked = true;
    GR_VR_SetSpriteDepth(1);
    vrm::Pose rel = vrm::compose(vrm::inverse(origin), lastHead);
    head->q = rel.q;
    head->p = rel.p;
    return true;
}

int VR_ScrZ() { return cfg.scrZ; }
float VR_HorizonLock() { return cfg.horizonLock; }
vrm::Vec3 VR_SeatOffset() { return cfg.seat; }
bool VR_HideWheel() { return VR_Active() && cfg.hideWheel; }
bool VR_MapOnDashboard() { return VR_Active() && cfg.mapOnDashboard; }

void VR_SetMapScreen(const float rect[4], vrm::Vec3 centre, vrm::Vec3 halfRight, vrm::Vec3 halfUp) {
    memcpy(mapRect, rect, sizeof(mapRect));
    mapCentre = centre; mapRight = halfRight; mapUp = halfUp;
    mapValid = true;
}
