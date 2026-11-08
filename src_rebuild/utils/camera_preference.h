#pragma once
#include <cstdio>

// User choices only: scripted/replay cameras must never be recorded here.
namespace camera_preference {
struct Preferences {
    int car[2] = {-1, -1};
    int foot[2] = {-1, -1};
    static bool valid(int view, bool onFoot) {
        return view >= 0 && view <= (onFoot ? 2 : 3);
    }
    bool load(const char* path) {
        FILE* file = std::fopen(path, "rb");
        if (!file) return false;
        int c0, f0, c1, f1;
        int count = std::fscanf(file, "REDRIVER2_CAMERA_V1 %d %d %d %d", &c0, &f0, &c1, &f1);
        std::fclose(file);
        if (count != 4) return false;
        int c[] = {c0, c1}, f[] = {f0, f1};
        for (int i = 0; i < 2; ++i) {
            car[i] = valid(c[i], false) ? c[i] : -1;
            foot[i] = valid(f[i], true) ? f[i] : -1;
        }
        return true;
    }
    bool save(const char* path) const {
        FILE* file = std::fopen(path, "wb");
        if (!file) return false;
        bool ok = std::fprintf(file, "REDRIVER2_CAMERA_V1\n%d %d\n%d %d\n", car[0], foot[0], car[1], foot[1]) > 0;
        return std::fclose(file) == 0 && ok;
    }
    bool choose(int player, bool onFoot, int view) {
        if (player < 0 || player >= 2 || !valid(view, onFoot)) return false;
        int& value = onFoot ? foot[player] : car[player];
        if (value == view) return false;
        value = view;
        return true;
    }
    int resolve(int player, bool onFoot, bool cockpitEnabled, int fallback) const {
        if (player < 0 || player >= 2) return fallback;
        int value = onFoot ? foot[player] : car[player];
        if (!valid(value, onFoot)) value = fallback;
        // A temporary fallback must not erase the user's cockpit choice.
        return value == 3 && !cockpitEnabled ? 2 : value;
    }
};
}
