// Track environment (time of day and custom lighting) for the launcher's "Race environment" / "Time Trial
// environment". Hooks in tools/recomp.py HOOKS; file merge called from fs.cpp.
//  - The track loader 0x348070 sets a "dawn" flag when the session type (game mode object +0xC) is in 15..27 (time
//    trial and tests) and passes it to three constructors: the environment 0x2F22D4 (-> 0x2F1ED4, loads
//    tracks/<t>/tt_EnvironmentSettings.txt + tt_PostEffects.txt instead of EnvironmentSettings.txt + PostEffects.txt:
//    low warm sun, blue ambient, fog, god rays), the sky 0x2F5950 (fair-weather sky Skies/Dawn.ddsn instead of
//    Skies/Fair.ddsn, flag kept at +0x33) and the track model 0x2FE64C (scene variant <t>_Timetest.sg vs _Normal.sg,
//    flag at +0x4330). Every track ships both lighting sets as loose files (USRDIR/tracks/<t>/), read with cellFs.
//  - F1CE_RACE_ENV / F1CE_TT_ENV (by the session's own flag) = afternoon, sunset or the name of a preset folder of
//    F1CE_ENV_DIR (default ENVIRONMENTS). The flag of the first two constructors is overridden, so the scene (crowds,
//    garages) stays the session's own.
//  - A preset: Preset.txt "Base=afternoon|sunset" (lighting set and sky it starts from; default the session's own),
//    EnvironmentSettings.txt / PostEffects.txt with only the "Key"=value lines it changes, and optionally
//    <track folder>/EnvironmentSettings.txt / PostEffects.txt applied after those. When the game opens the base set's
//    file, it gets the track's file (or the season pack's) with those values replaced (keys it lacks appended),
//    written to a temporary file.
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

#include "kernel.h"
#include "slick_tuning.h"

namespace stdfs = std::filesystem;

namespace {
struct Choice {
    int flag = -1;      // -1: the game's own, 0: afternoon, 1: sunset
    stdfs::path preset; // empty: no value changes
};

std::string lower(std::string s) {
    for (char& ch : s) ch = (char)tolower((unsigned char)ch);
    return s;
}
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

Choice choice_for(bool dawn) {
    Choice c;
    const char* e = getenv(dawn ? "F1CE_TT_ENV" : "F1CE_RACE_ENV");
    if (!e || !*e) return c;
    if (!strcmp(e, "afternoon")) { c.flag = 0; return c; }
    if (!strcmp(e, "sunset")) { c.flag = 1; return c; }
    const char* dir = getenv("F1CE_ENV_DIR");
    c.preset = stdfs::path(dir && *dir ? dir : "ENVIRONMENTS") / stdfs::path(e);
    std::error_code ec;
    if (!stdfs::is_directory(c.preset, ec)) {
        LOG_W("environment: preset folder %s not found", PATH_STR(c.preset).c_str());
        return {};
    }
    c.flag = dawn;
    std::ifstream in(c.preset / "Preset.txt");
    for (std::string line; std::getline(in, line);) {
        size_t eq = line.find('=');
        if (eq == std::string::npos || lower(trim(line.substr(0, eq))) != "base") continue;
        std::string v = lower(trim(line.substr(eq + 1)));
        if (v == "afternoon") c.flag = 0;
        else if (v == "sunset") c.flag = 1;
        else LOG_W("environment: %s: unknown Base \"%s\"", PATH_STR(c.preset).c_str(), v.c_str());
    }
    return c;
}

std::mutex g_mutex;
Choice g_active;            // set by the environment constructor of the track being loaded
bool g_active_set = false;

// "Key"=value lines of a settings file in order; comments (//, #, ;) and blank lines dropped
using Lines = std::vector<std::pair<std::string, std::string>>;
Lines parse(const stdfs::path& p, bool& found) {
    Lines out;
    std::ifstream in(p, std::ios::binary);
    found = (bool)in;
    for (std::string line; std::getline(in, line);) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line.rfind("//", 0) == 0) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        if (key.size() >= 2 && key.front() == '"' && key.back() == '"') key = key.substr(1, key.size() - 2);
        out.emplace_back(key, trim(line.substr(eq + 1)));
    }
    return out;
}
void apply(Lines& base, const stdfs::path& p, const char* what) {
    bool found;
    Lines ch = parse(p, found);
    if (!found) return;
    size_t added = 0;
    for (auto& [k, v] : ch) {
        bool hit = false;
        for (auto& [bk, bv] : base)
            if (lower(bk) == lower(k)) { bv = v; hit = true; }
        if (!hit) { base.emplace_back(k, v); added++; }
    }
    LOG_I("environment: %s: %zu values from %s (%zu not in the track's file)", what, ch.size(), PATH_STR(p).c_str(),
          added);
}
}  // namespace

// 0x2F22D4 (environment settings), 0x2F5950 (skies), both called by the track loader 0x348070 in that order:
// r5 = dawn lighting (time trial / test sessions)
bool environment_hook(PPUContext& c, bool env) {
    std::lock_guard lk(g_mutex);
    if (env) {
        const bool is_tt = (c.r[5] != 0);
        slick::set_time_trial_session(is_tt);
        g_active = choice_for(is_tt);
        g_active_set = true;
    }
    if (!g_active_set || g_active.flag < 0) return false;
    if (env && (u32)c.r[5] != (u32)g_active.flag)
        LOG_I("environment: %s lighting instead of %s", g_active.flag ? "sunset" : "afternoon",
              c.r[5] ? "sunset" : "afternoon");
    c.r[5] = g_active.flag;
    return false;
}
bool environment_env_hook(PPUContext& c) { return environment_hook(c, true); }
bool environment_sky_hook(PPUContext& c) { return environment_hook(c, false); }

// fs.cpp, before opening a game file for reading: `src` is the file to read (the track's own or the season pack's),
// `host` its path in USRDIR. Returns the merged file to read instead, or empty.
std::string environment_override(const std::string& host, const std::string& src) {
    const stdfs::path hp = stdfs::path(host);
    std::string name = lower(PATH_STR(hp.filename()));
    if (name.rfind("tt_", 0) == 0) name = name.substr(3);
    if (name != "environmentsettings.txt" && name != "posteffects.txt") return {};
    const std::string track = lower(PATH_STR(hp.parent_path().filename()));
    if (lower(PATH_STR(hp.parent_path().parent_path().filename())) != "tracks") return {};
    Choice ch;
    {
        std::lock_guard lk(g_mutex);
        if (!g_active_set || g_active.preset.empty()) return {};
        ch = g_active;
    }
    const char* file = name == "posteffects.txt" ? "PostEffects.txt" : "EnvironmentSettings.txt";
    bool found;
    Lines lines = parse(stdfs::path(src), found);
    if (!found) return {};
    apply(lines, ch.preset / file, file);
    // per-track files: the folder name as in USRDIR/tracks, any case
    std::error_code ec;
    for (const auto& e : stdfs::directory_iterator(ch.preset, ec))
        if (e.is_directory(ec) && lower(PATH_STR(e.path().filename())) == track) apply(lines, e.path() / file, file);

    stdfs::path out = stdfs::temp_directory_path(ec) / "f1ce_env";
    stdfs::create_directories(out, ec);
    out /= track + "_" + lower(file);
    std::ofstream o(out, std::ios::binary | std::ios::trunc);
    for (auto& [k, v] : lines) o << '"' << k << "\"=" << v << "\r\n";
    if (!o) {
        LOG_W("environment: cannot write %s", PATH_STR(out).c_str());
        return {};
    }
    LOG_I("environment: preset \"%s\" -> %s", PATH_STR(ch.preset.filename()).c_str(), PATH_STR(out).c_str());
    return PATH_STR(out);
}
