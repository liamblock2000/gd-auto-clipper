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

using namespace geode::prelude;

// GD Auto-Clipper -----------------------------------------------------------------------------
// When a run ENDS (death or clear), the mod checks it against your rules and, if it matched, tells
// OBS to save its Replay Buffer -- a clip of the moment, with game audio + mic. Talks to OBS
// directly over obs-websocket (no helper app, no ffmpeg). Press F4 in-game for settings.
//
// A run COVERS A->B iff it started at or before A% AND reached at least B%. Multiple rules are
// allowed (comma-separated), so "0-100, 0-90" clips a clear, OR a death that still reached >=90%.

struct Rule { float a; float b; };
static std::vector<Rule> g_rules;
static bool g_includePractice = false;

static float g_startPct = 0.f, g_maxPct = 0.f;   // this attempt's start% and furthest%
static bool  g_fired = false;                    // one clip per attempt
static std::string g_bufferLevel;                // level we've already told OBS to start buffering for

static const float TOL = 0.5f;

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

static void fireClip() {
    auto cfg = obsConfig();
    int padAfter = (int) Mod::get()->getSettingValue<int64_t>("pad-after");
    std::thread([cfg, padAfter] {
        if (padAfter > 0) std::this_thread::sleep_for(std::chrono::seconds(padAfter));
        bool ok = obsws::saveReplayBuffer(cfg);
        log::info("[Clipper] SaveReplayBuffer -> {}", ok);
        geode::queueInMainThread([ok] {
            // small, short toast at the bottom -- never blocks gameplay even if it pops mid-run
            Notification::create(
                ok ? "Clip saved" : "Clip failed (OBS?)",
                ok ? NotificationIcon::Success : NotificationIcon::Error,
                1.2f
            )->show();
        });
    }).detach();
}

// at the end of a run, clip ONCE if any rule matched the furthest % reached
static void evaluateAndClip(float startPct, float reachedMax) {
    if (g_fired) return;
    for (auto& r : g_rules) {
        if (matches(r, startPct, reachedMax)) { fireClip(); g_fired = true; return; }
    }
}

class $modify(ClipperPL, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        refreshSettings();
        g_startPct = this->getCurrentPercent();
        g_maxPct   = g_startPct;
        g_fired    = false;
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        // keep OBS's replay buffer running; kick it once per level entry
        if (m_level) {
            std::string lvl = std::string(m_level->m_levelName);
            if (lvl != g_bufferLevel) { g_bufferLevel = lvl; ensureBufferAsync(); }
        }
        float p = this->getCurrentPercent();
        if (p > g_maxPct) g_maxPct = p;            // track furthest %; don't fire mid-run
    }

    // run ended by DEATH -> evaluate (handles "0-90 in case I die")
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        float p = this->getCurrentPercent();
        if (p > g_maxPct) g_maxPct = p;
        if (g_includePractice || !m_isPracticeMode) evaluateAndClip(g_startPct, g_maxPct);
        PlayLayer::destroyPlayer(player, object);
    }

    // run ended by CLEAR -> evaluate at 100%
    void levelComplete() {
        if (g_includePractice || !m_isPracticeMode) evaluateAndClip(g_startPct, 100.f);
        PlayLayer::levelComplete();
    }
};

// F4 -> open this mod's settings panel (rules / practice / padding / OBS connection)
class $modify(ClipperKeys, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(cocos2d::enumKeyCodes key, bool down, bool repeat, double ts) {
        if (down && !repeat && key == cocos2d::KEY_F4) {
            geode::openSettingsPopup(Mod::get());
            return true;
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, ts);
    }
};
