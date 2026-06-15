#include "obs.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/base64.hpp>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <span>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>

using namespace geode::prelude;

namespace {

// ---------- SHA-256 (public-domain style implementation) ----------------------------------------
struct Sha256 {
    uint32_t h[8];
    uint64_t bitlen;
    uint8_t  buf[64];
    size_t   buflen;

    static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

    void init() {
        h[0] = 0x6a09e667; h[1] = 0xbb67ae85; h[2] = 0x3c6ef372; h[3] = 0xa54ff53a;
        h[4] = 0x510e527f; h[5] = 0x9b05688c; h[6] = 0x1f83d9ab; h[7] = 0x5be0cd19;
        bitlen = 0; buflen = 0;
    }

    void block(const uint8_t* p) {
        static const uint32_t k[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
        };
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = (p[i*4] << 24) | (p[i*4+1] << 16) | (p[i*4+2] << 8) | p[i*4+3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
            uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }

    void update(const uint8_t* p, size_t n) {
        bitlen += (uint64_t)n * 8;
        while (n) {
            size_t take = 64 - buflen;
            if (take > n) take = n;
            memcpy(buf + buflen, p, take);
            buflen += take; p += take; n -= take;
            if (buflen == 64) { block(buf); buflen = 0; }
        }
    }

    void finish(uint8_t out[32]) {
        uint64_t bits = bitlen;                 // original length, captured before padding
        uint8_t one = 0x80; update(&one, 1);
        uint8_t zero = 0x00;
        while (buflen != 56) update(&zero, 1);
        uint8_t lb[8];
        for (int i = 0; i < 8; i++) lb[i] = (uint8_t)(bits >> (56 - i*8));
        update(lb, 8);
        for (int i = 0; i < 8; i++) {
            out[i*4]   = (uint8_t)(h[i] >> 24);
            out[i*4+1] = (uint8_t)(h[i] >> 16);
            out[i*4+2] = (uint8_t)(h[i] >> 8);
            out[i*4+3] = (uint8_t)(h[i]);
        }
    }
};

std::vector<uint8_t> sha256(const std::string& s) {
    Sha256 c; c.init();
    c.update(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    std::vector<uint8_t> out(32); c.finish(out.data()); return out;
}

std::string b64(const std::vector<uint8_t>& d) {
    return geode::utils::base64::encode(
        std::span<const uint8_t>(d.data(), d.size()),
        geode::utils::base64::Base64Variant::Normal);   // standard base64 + padding
}

// obs-websocket auth: base64(sha256( base64(sha256(password+salt)) + challenge ))
std::string computeAuth(const std::string& password, const std::string& salt, const std::string& challenge) {
    std::string secret = b64(sha256(password + salt));
    return b64(sha256(secret + challenge));
}

// ---------- tiny string helpers ----------------------------------------------------------------
std::string extract(const std::string& s, const std::string& key) {
    auto p = s.find(key);
    if (p == std::string::npos) return "";
    p += key.size();
    auto e = s.find('"', p);
    if (e == std::string::npos) return "";
    return s.substr(p, e - p);
}

// ---------- raw socket + WebSocket framing ------------------------------------------------------
bool sendAll(int s, const uint8_t* p, size_t n) {
    while (n) { ssize_t r = send(s, p, n, 0); if (r <= 0) return false; p += r; n -= (size_t)r; }
    return true;
}
bool recvAll(int s, uint8_t* p, size_t n) {
    while (n) { ssize_t r = recv(s, p, n, 0); if (r <= 0) return false; p += r; n -= (size_t)r; }
    return true;
}

std::string wsKey() {
    timeval tv; gettimeofday(&tv, nullptr);
    static unsigned ctr = 0; ctr++;
    uint8_t b[16];
    uint64_t seed = (uint64_t)tv.tv_sec * 1000000ull + (uint64_t)tv.tv_usec + (uint64_t)ctr * 2654435761u;
    for (int i = 0; i < 16; i++) {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        b[i] = (uint8_t)(seed >> 33);
    }
    return geode::utils::base64::encode(std::span<const uint8_t>(b, 16), geode::utils::base64::Base64Variant::Normal);
}

bool wsSendText(int s, const std::string& payload) {
    std::vector<uint8_t> f;
    f.push_back(0x81);                                   // FIN + text opcode
    size_t n = payload.size();
    uint8_t mask[4] = { 0x21, 0x43, 0x65, 0x87 };
    if (n < 126) {
        f.push_back(0x80 | (uint8_t)n);
    } else if (n < 65536) {
        f.push_back(0x80 | 126);
        f.push_back((uint8_t)((n >> 8) & 0xff));
        f.push_back((uint8_t)(n & 0xff));
    } else {
        f.push_back(0x80 | 127);
        for (int i = 7; i >= 0; i--) f.push_back((uint8_t)((n >> (i*8)) & 0xff));
    }
    for (int i = 0; i < 4; i++) f.push_back(mask[i]);
    for (size_t i = 0; i < n; i++) f.push_back((uint8_t)payload[i] ^ mask[i % 4]);
    return sendAll(s, f.data(), f.size());
}

// read one text/binary message (skipping ping/pong), assumes single-frame messages (true for obs-ws)
bool wsRecv(int s, std::string& out) {
    while (true) {
        uint8_t hdr[2];
        if (!recvAll(s, hdr, 2)) return false;
        uint8_t opcode = hdr[0] & 0x0f;
        bool masked = (hdr[1] & 0x80) != 0;
        uint64_t len = hdr[1] & 0x7f;
        if (len == 126) {
            uint8_t e[2]; if (!recvAll(s, e, 2)) return false;
            len = ((uint64_t)e[0] << 8) | e[1];
        } else if (len == 127) {
            uint8_t e[8]; if (!recvAll(s, e, 8)) return false;
            len = 0; for (int i = 0; i < 8; i++) len = (len << 8) | e[i];
        }
        uint8_t mk[4] = {0,0,0,0};
        if (masked && !recvAll(s, mk, 4)) return false;
        std::string payload; payload.resize((size_t)len);
        if (len && !recvAll(s, reinterpret_cast<uint8_t*>(payload.data()), (size_t)len)) return false;
        if (masked) for (uint64_t i = 0; i < len; i++) payload[i] ^= mk[i % 4];
        if (opcode == 0x8) return false;                 // close
        if (opcode == 0x9 || opcode == 0xA) continue;    // ping / pong -> ignore
        out = payload; return true;                       // text/binary/continuation
    }
}

int connectWS(const std::string& host, int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0)
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);  // "localhost" / non-numeric -> loopback
    timeval tv{}; tv.tv_sec = 5; tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(s, (sockaddr*)&addr, sizeof(addr)) < 0) { close(s); return -1; }

    std::string req =
        "GET / HTTP/1.1\r\nHost: " + host + ":" + std::to_string(port) +
        "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + wsKey() +
        "\r\nSec-WebSocket-Version: 13\r\n\r\n";
    if (!sendAll(s, reinterpret_cast<const uint8_t*>(req.data()), req.size())) { close(s); return -1; }

    std::string resp; uint8_t c;
    while (resp.find("\r\n\r\n") == std::string::npos) {
        if (recv(s, &c, 1, 0) <= 0) { close(s); return -1; }
        resp.push_back((char)c);
        if (resp.size() > 8192) { close(s); return -1; }
    }
    if (resp.find(" 101") == std::string::npos) { close(s); return -1; }
    return s;
}

// connect, identify (with auth if required), send one request, return true if result==true
bool doRequest(const obsws::Config& cfg, const std::string& requestType) {
    int s = connectWS(cfg.host, cfg.port);
    if (s < 0) { log::warn("[Clipper] OBS: connect failed ({}:{})", cfg.host, cfg.port); return false; }

    std::string hello;
    if (!wsRecv(s, hello)) { close(s); return false; }            // op 0: Hello

    std::string identify;
    if (hello.find("\"authentication\"") != std::string::npos) {
        if (cfg.password.empty()) {
            log::warn("[Clipper] OBS auth required but no password set (F4 -> OBS password)");
            close(s); return false;
        }
        std::string salt = extract(hello, "\"salt\":\"");
        std::string challenge = extract(hello, "\"challenge\":\"");
        std::string auth = computeAuth(cfg.password, salt, challenge);
        identify = "{\"op\":1,\"d\":{\"rpcVersion\":1,\"authentication\":\"" + auth + "\"}}";
    } else {
        identify = "{\"op\":1,\"d\":{\"rpcVersion\":1}}";
    }
    if (!wsSendText(s, identify)) { close(s); return false; }

    std::string ident;
    if (!wsRecv(s, ident)) { close(s); return false; }            // op 2: Identified
    if (ident.find("\"op\":2") == std::string::npos) {
        log::warn("[Clipper] OBS identify failed: {}", ident);
        close(s); return false;
    }

    std::string req =
        "{\"op\":6,\"d\":{\"requestType\":\"" + requestType +
        "\",\"requestId\":\"clip\",\"requestData\":{}}}";
    if (!wsSendText(s, req)) { close(s); return false; }

    std::string resp;
    if (!wsRecv(s, resp)) { close(s); return false; }             // op 7: RequestResponse
    close(s);
    return resp.find("\"result\":true") != std::string::npos;
}

} // anonymous namespace

namespace obsws {
    bool saveReplayBuffer(const Config& cfg) {
        return doRequest(cfg, "SaveReplayBuffer");
    }
    bool ensureReplayBufferRunning(const Config& cfg) {
        // StartReplayBuffer is a no-op (harmless error) if already running, so this is safe to spam.
        return doRequest(cfg, "StartReplayBuffer");
    }
}
