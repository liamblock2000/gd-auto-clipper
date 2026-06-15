#pragma once
#include <string>

// Minimal obs-websocket v5 client (raw TCP + WebSocket framing + SHA-256 auth).
// Every call opens a short-lived connection, does its request, and closes. These BLOCK on network
// I/O, so always call them from a background std::thread, never the game's main thread.
namespace obsws {
    struct Config {
        std::string host;       // usually "localhost"
        int port;               // usually 4455
        std::string password;   // obs-websocket password ("" if auth disabled)
    };

    // Tell OBS to save its replay buffer (the clip). Returns true if OBS reported success, and
    // fills outPath with the saved file path (when OBS reports it).
    bool saveReplayBuffer(const Config& cfg, std::string& outPath);

    // Start the replay buffer if it isn't already running (best-effort; safe to call repeatedly).
    bool ensureReplayBufferRunning(const Config& cfg);
}
