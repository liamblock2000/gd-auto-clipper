#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Notification.hpp>
#include "obs.hpp"
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/time.h>

using namespace geode::prelude;

// GD Auto-Clipper -----------------------------------------------------------------------------
// When a run ENDS (death or clear) and matches one of your rules, the mod tells OBS to save its
// Replay Buffer, then (if ffmpeg is available) trims the saved file down to the actual run length
// + padding and names it after the level. Talks to OBS directly over obs-websocket. F4 = settings.
//
// A run COVERS A->B iff it started at or before A% AND reached at least B%. Multiple rules allowed
// (comma-separated): "0-100, 0-90" clips a clear OR a death that still reached >=90%.

struct Rule { float a; float b; };
static std::vector<Rule> g_rules;
static bool g_includePractice = false;

static float  g_startPct = 0.f, g_maxPct = 0.f;   // this attempt's start% and furthest%
static bool   g_fired = false;
static double g_runStartWall = 0.0;               // wall-clock at attempt start (for run length)
static std::string g_bufferLevel;

static const float TOL = 1.5f;                    // +-1.5% leniency on A/B thresholds

static double nowWall() {
    timeval tv; gettimeofday(&tv, nullptr);
    return (double)tv.tv_sec + (double)tv.tv_usec * 1e-6;
}
static std::string homeStr() { const char* h = getenv("HOME"); return h ? std::string(h) : std::string(); }

static void parseRules(const std::string& s) {
    std::vector<Rule> rules;
    size_t i = 0;
    while (i <= s.size()) {
        size_t comma = s.find(',', i);
        std::string tok = s.substr(i, comma == std::string::npos ? std::string::npos : comma - i);
        for (auto& c : tok) if (c == '-') c = ' ';
        float a, b;
        if (sscanf(tok.c_str(), "%f %f", &a, &b) == 2) rules.push_back({a, b});
        if (comma == std::string::npos) break;
        i = comma + 1;
    }
    g_rules = rules;
}

static void refreshSettings() {
    auto mod = Mod::get();
    parseRules(mod->getSettingValue<std::string>("rules"));
    g_includePractice = mod->getSettingValue<bool>("practice");
}

static bool matches(const Rule& r, float startPct, float reachedPct) {
    return startPct <= r.a + TOL && reachedPct >= r.b - TOL;
}

static obsws::Config obsConfig() {
    auto m = Mod::get();
    return {
        m->getSettingValue<std::string>("obs-host"),
        (int) m->getSettingValue<int64_t>("obs-port"),
        m->getSettingValue<std::string>("obs-password")
    };
}

static void ensureBufferAsync() {
    auto cfg = obsConfig();
    std::thread([cfg] { obsws::ensureReplayBufferRunning(cfg); }).detach();
}

static std::string slugify(const std::string& name) {
    std::string slug; bool us = false;
    for (char c : name) {
        char d = (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
        bool ok = (d >= 'a' && d <= 'z') || (d >= '0' && d <= '9');
        if (ok) { slug += d; us = false; } else if (!us) { slug += '_'; us = true; }
    }
    while (!slug.empty() && slug.back() == '_') slug.pop_back();
    return slug.empty() ? std::string("level") : slug;
}

// find an ffmpeg binary: the configured path, else common locations. "" if none found.
static std::string findFfmpeg(const std::string& configured) {
    if (!configured.empty() && access(configured.c_str(), X_OK) == 0) return configured;
    std::string home = homeStr();
    const std::string cands[] = {
        home + "/bin/ffmpeg", "/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg", "/usr/bin/ffmpeg"
    };
    for (auto& c : cands) if (access(c.c_str(), X_OK) == 0) return c;
    return "";
}

static std::string dirOf(const std::string& p) {
    auto pos = p.find_last_of('/');
    return pos == std::string::npos ? std::string(".") : p.substr(0, pos);
}

static std::string makeOutName(const std::string& src, const std::string& level, int a, int b) {
    timeval tv; gettimeofday(&tv, nullptr);
    char name[512];
    snprintf(name, sizeof(name), "%s/%s_%d-%d_%ld.mp4",
             dirOf(src).c_str(), slugify(level).c_str(), a, b, (long)tv.tv_sec);
    return name;
}

// trim the LAST `want` seconds of src into out via ffmpeg (-sseof). Returns true on success.
static bool trimClip(const std::string& ff, const std::string& src, double want, const std::string& out) {
    char cmd[2600];
    snprintf(cmd, sizeof(cmd),
        "\"%s\" -y -sseof -%.2f -i \"%s\" -c:v libx264 -preset veryfast -crf 18 "
        "-c:a aac -movflags +faststart \"%s\" >/dev/null 2>&1",
        ff.c_str(), want, src.c_str(), out.c_str());
    return std::system(cmd) == 0;
}

static void fireClip(std::string levelName, float startPct, float endPct, double runDuration) {
    auto cfg = obsConfig();
    auto mod = Mod::get();
    int padBefore = (int) mod->getSettingValue<int64_t>("pad-before");
    int padAfter  = (int) mod->getSettingValue<int64_t>("pad-after");
    std::string ffSetting = mod->getSettingValue<std::string>("ffmpeg-path");
    int a = (int)(startPct + 0.5f), b = (int)(endPct + 0.5f);
    std::thread([=] {
        if (padAfter > 0) std::this_thread::sleep_for(std::chrono::seconds(padAfter));
        std::string path;
        bool ok = obsws::saveReplayBuffer(cfg, path);
        std::string finalPath = path;
        bool trimmed = false;
        if (ok && !path.empty()) {
            std::string ff = findFfmpeg(ffSetting);
            if (!ff.empty() && runDuration > 0.5) {
                double want = runDuration + (double)padBefore + (double)padAfter;
                std::string out = makeOutName(path, levelName, a, b);
                if (trimClip(ff, path, want, out)) {
                    finalPath = out; trimmed = true;
                    ::remove(path.c_str());          // drop the big full-buffer original
                }
            }
        }
        log::info("[Clipper] clip ok={} trimmed={} -> {}", ok, trimmed, finalPath);
        geode::queueInMainThread([ok, finalPath, trimmed] {
            std::string msg = ok
                ? (std::string(trimmed ? "Clip saved -> " : "Clip saved (full buffer) -> ") + finalPath)
                : std::string("Clip failed (OBS?)");
            Notification::create(msg, ok ? NotificationIcon::Success : NotificationIcon::Error,
                                 ok ? 3.0f : 1.5f)->show();
        });
    }).detach();
}

// at the end of a run, clip ONCE if any rule matched the furthest % reached
static void evaluateAndClip(const std::string& levelName, float startPct, float reachedMax, double runDuration) {
    if (g_fired) return;
    for (auto& r : g_rules) {
        if (matches(r, startPct, reachedMax)) {
            fireClip(levelName, startPct, reachedMax, runDuration);
            g_fired = true;
            return;
        }
    }
}

class $modify(ClipperPL, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        refreshSettings();
        g_startPct = this->getCurrentPercent();
        g_maxPct   = g_startPct;
        g_fired    = false;
        g_runStartWall = nowWall();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (m_level) {
            std::string lvl = std::string(m_level->m_levelName);
            if (lvl != g_bufferLevel) { g_bufferLevel = lvl; ensureBufferAsync(); }
        }
        float p = this->getCurrentPercent();
        if (p > g_maxPct) g_maxPct = p;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        float p = this->getCurrentPercent();
        if (p > g_maxPct) g_maxPct = p;
        if (g_includePractice || !m_isPracticeMode) {
            std::string lvl = m_level ? std::string(m_level->m_levelName) : std::string("level");
            evaluateAndClip(lvl, g_startPct, g_maxPct, nowWall() - g_runStartWall);
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void levelComplete() {
        if (g_includePractice || !m_isPracticeMode) {
            std::string lvl = m_level ? std::string(m_level->m_levelName) : std::string("level");
            evaluateAndClip(lvl, g_startPct, 100.f, nowWall() - g_runStartWall);
        }
        PlayLayer::levelComplete();
    }
};

// F4 -> open this mod's settings panel
class $modify(ClipperKeys, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(cocos2d::enumKeyCodes key, bool down, bool repeat, double ts) {
        if (down && !repeat && key == cocos2d::KEY_F4) {
            geode::openSettingsPopup(Mod::get());
            return true;
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, ts);
    }
};
