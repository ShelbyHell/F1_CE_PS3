// The joysticks as wheelcfg::pick sees them, from SDL; shared by the game (wheel_sdl.cpp) and the launchers
// (launcher/wheel_device.cpp, launcher/wheel_tab.cpp). Call it on the thread that owns SDL's joysticks.
//
// With two or more wheel candidates (wheelcfg::wheel_like) each is opened once to read its force feedback and axes,
// which tells a Fanatec base's real device from the extra game controller it can show up with (wheelcfg::extras).
// Opening costs a DirectInput wheel 0.2-0.5 s, so the results are kept per joystick (SDL never reuses an id).
#pragma once
#include "wheel_config.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace wheelcfg {
inline std::vector<Joy> sdl_joys(const SDL_JoystickID* ids, int n) {
    std::vector<Joy> v(n);
    int candidates = 0;
    for (int i = 0; i < n; i++) {
        char guid[64];
        SDL_GUIDToString(SDL_GetJoystickGUIDForID(ids[i]), guid, sizeof(guid));
        Joy& j = v[i];
        j.guid = guid;
        j.wheel_type = SDL_GetJoystickTypeForID(ids[i]) == SDL_JOYSTICK_TYPE_WHEEL;
        j.vid = SDL_GetJoystickVendorForID(ids[i]);
        j.vendor = vendor_name(j.vid) != nullptr;
        j.gamepad = SDL_IsGamepad(ids[i]);
        candidates += wheel_like(j);
    }
    if (candidates < 2) return v;
    struct Probe { bool ffb; int axes; };
    static std::map<SDL_JoystickID, Probe> probed;
    for (int i = 0; i < n; i++) {
        if (!wheel_like(v[i])) continue;
        auto it = probed.find(ids[i]);
        if (it == probed.end()) {
            // (an open joystick is opened again by reference: closing it here keeps it open)
            SDL_Joystick* js = SDL_OpenJoystick(ids[i]);
            if (!js) continue;   // (not opened: counts as a real one)
            it = probed.emplace(ids[i], Probe{SDL_IsJoystickHaptic(js), SDL_GetNumJoystickAxes(js)}).first;
            SDL_CloseJoystick(js);
        }
        v[i].ffb = it->second.ffb;
        v[i].axes = it->second.axes;
    }
    return v;
}

// The wheel's twins: the other joysticks with its name and USB ids that are not extras. A Fanatec base can show up as
// two game controllers alike in every way (force feedback, axes, GUID; 2026-11-17 tester: two "FANATEC Wheel") of
// which only one reports input, so the twins are opened beside the wheel and the first one that reports input while
// the wheel does not takes its place. (Input: an axis moved or a button or hat changed since the joystick settled,
// 0.3 s after it was opened, so the first state a device gets does not count; a dead one never changes.)
class Twins {
    struct J {
        SDL_Joystick* js;
        std::vector<Sint16> axes;
        std::vector<bool> buttons;
        std::vector<Uint8> hats;
    };
    std::vector<J> js_;                 // [0]: the wheel (not owned)
    std::vector<SDL_JoystickID> group_;   // the wheel and its twins
    Uint64 settled_ = 0;

    static void snap(J& j) {
        const int na = SDL_GetNumJoystickAxes(j.js), nb = SDL_GetNumJoystickButtons(j.js), nh = SDL_GetNumJoystickHats(j.js);
        j.axes.resize(std::max(na, 0));
        j.buttons.resize(std::max(nb, 0));
        j.hats.resize(std::max(nh, 0));
        for (int a = 0; a < na; a++) j.axes[a] = SDL_GetJoystickAxis(j.js, a);
        for (int b = 0; b < nb; b++) j.buttons[b] = SDL_GetJoystickButton(j.js, b);
        for (int h = 0; h < nh; h++) j.hats[h] = SDL_GetJoystickHat(j.js, h);
    }
    static bool changed(const J& j) {
        for (size_t a = 0; a < j.axes.size(); a++)
            if (std::abs(SDL_GetJoystickAxis(j.js, (int)a) - j.axes[a]) > 1000) return true;
        for (size_t b = 0; b < j.buttons.size(); b++)
            if (SDL_GetJoystickButton(j.js, (int)b) != j.buttons[b]) return true;
        for (size_t h = 0; h < j.hats.size(); h++)
            if (SDL_GetJoystickHat(j.js, (int)h) != j.hats[h]) return true;
        return false;
    }

    void drop() {   // (the group stays: has() still knows the twins after the choice)
        for (size_t i = 1; i < js_.size(); i++) SDL_CloseJoystick(js_[i].js);
        js_.clear();
    }

public:
    bool empty() const { return js_.size() < 2; }
    // the wheel or one of its twins (also after the choice, until close)
    bool has(SDL_JoystickID id) const { return std::find(group_.begin(), group_.end(), id) != group_.end(); }
    // opens the twins of the wheel ids[wheel_index], which the caller has open as `wheel`; returns how many (close()
    // before SDL shuts down)
    int open(const SDL_JoystickID* ids, int n, int wheel_index, SDL_Joystick* wheel) {
        close();
        group_ = {ids[wheel_index]};
        const std::vector<Joy> joys = sdl_joys(ids, n);
        const std::vector<bool> extra = extras(joys);
        const char* name = SDL_GetJoystickNameForID(ids[wheel_index]);
        const std::string wname = name ? name : "";
        const Uint16 pid = SDL_GetJoystickProductForID(ids[wheel_index]);
        for (int i = 0; i < n; i++) {
            const char* nm = SDL_GetJoystickNameForID(ids[i]);
            if (i == wheel_index || extra[i] || joys[i].vid != joys[wheel_index].vid ||
                SDL_GetJoystickProductForID(ids[i]) != pid || wname != (nm ? nm : ""))
                continue;
            if (js_.empty()) js_.push_back({wheel});
            if (SDL_Joystick* t = SDL_OpenJoystick(ids[i])) {
                js_.push_back({t});
                group_.push_back(ids[i]);
            }
        }
        if (js_.size() < 2) close();
        settled_ = SDL_GetTicks() + 300;
        return (int)js_.size() - 1;
    }
    void close() {
        drop();
        group_.clear();
    }
    // call after each event pump: nullptr while undecided, and once the wheel reports input (the twins are closed then);
    // else the twin that reported input while the wheel did not: the caller closes the wheel and uses the twin (owned
    // by the caller now; the other twins are closed)
    SDL_Joystick* settle() {
        if (empty()) return nullptr;
        if (SDL_GetTicks() < settled_) {
            for (J& j : js_) snap(j);
            return nullptr;
        }
        if (changed(js_[0])) {
            drop();
            return nullptr;
        }
        for (size_t i = 1; i < js_.size(); i++)
            if (changed(js_[i])) {
                SDL_Joystick* t = js_[i].js;
                js_.erase(js_.begin() + i);
                drop();
                return t;
            }
        return nullptr;
    }
};
}  // namespace wheelcfg
