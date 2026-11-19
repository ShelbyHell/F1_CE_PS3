// HLE: sys_net (BSD sockets), cellNetCtl and the bits of sceNp that ONLINE.BIN imports, on a virtual LAN.
//
// Multiplayer is the separate executable ONLINE.BIN (f1ce_online, started by the front end's exitspawn). Its online
// framework is SCE-RT "Unity" (Medius lobby + DME peer networking, SVML menu pages in online.wad); the Medius servers
// are gone, but the same front end has a LAN mode (Multiplayer > Play LAN: UNITY CHANGEMODE forwardmode="LAN") that
// finds and plays games over the local network with plain sockets. All of its socket traffic goes through one thin
// platform layer (0x56D364..0x56F4D0), so the sockets here only need BSD semantics.
//
// As in Yuzu/Eden's multiplayer rooms, the LAN is virtual: each game gets an address on 10.13.37.0/24 and every packet
// to another member (datagrams, broadcasts and TCP-like streams) is relayed through a room server over one TCP
// connection per member. The host runs the room inside its game (port forwarding needed only there) and is a member
// like the others; f1ce_online --room runs a room alone. Without a room the game is alone on its LAN (loopback only).
//
//   F1CE_NET_MODE=host|join   host: run a room and join it; join: join the room at F1CE_NET_ADDR
//   F1CE_NET_ADDR=<host>[:port]  (join) the room's address
//   F1CE_NET_PORT=<port>      the room's TCP port (default 51015)
//   F1CE_NET_NAME=<nickname>  shown to the other members (default: player)
//   F1CE_NET_PASSWORD=<text>  optional; a room with one admits only members giving the same
//   F1CE_NET_LOG=1            logs every socket call (discovery and protocol debugging)
//
// Room protocol (between hosts only, little endian): frames {u32 length, u8 type, body}. HELLO {u32 magic, u32
// version, char name[16], char password[32]} -> WELCOME {u32 address} or REJECT {text}; MEMBERS {u32 n, n x {u32
// address, char name[16]}} on every join and leave; PACKET {u32 source, u32 destination, u8 kind, ...} in both
// directions: the room sets the source to the sender's address and forwards by destination (a broadcast address: to
// everyone else). Kinds: DGRAM {u16 sport, u16 dport, data}, SYN {u32 cid, u16 sport, u16 dport}, SYNACK {u32 cid,
// u8 ok}, DATA {u32 initiator, u32 cid, data}, FIN {u32 initiator, u32 cid}. A stream is named by its initiator's
// address and the initiator's connection id.
#include "kernel.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using hsock = SOCKET;
static void hclose(hsock s) { closesocket(s); }
#else
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
using hsock = int;
#define INVALID_SOCKET (-1)
static void hclose(hsock s) { close(s); }
#endif

void sysutil_post_event(u32 status, u32 param);   // hle_misc.cpp

namespace {
// ------------------------------------------------------------------ constants
constexpr u32 kSubnet = 0x0A0D2500;   // 10.13.37.0/24
constexpr u32 kBroadcast = kSubnet | 0xFF;
constexpr u32 kLoopback = 0x7F000001;
constexpr u32 kMagic = 0x45433146;    // "F1CE"
constexpr u32 kVersion = 1;
constexpr u16 kDefaultPort = 51015;

enum : u8 { F_HELLO = 1, F_WELCOME, F_REJECT, F_MEMBERS, F_PACKET };
enum : u8 { K_DGRAM = 1, K_SYN, K_SYNACK, K_DATA, K_FIN };

// sys_net errno values (BSD)
enum : s32 {
    E_BADF = 9, E_INVAL = 22, E_PIPE = 32, E_WOULDBLOCK = 35, E_INPROGRESS = 36, E_ALREADY = 37, E_NOTSOCK = 38,
    E_DESTADDRREQ = 39, E_MSGSIZE = 40, E_PROTOTYPE = 41, E_NOPROTOOPT = 42, E_AFNOSUPPORT = 47, E_ADDRINUSE = 48,
    E_NETUNREACH = 51, E_CONNRESET = 54, E_ISCONN = 56, E_NOTCONN = 57, E_TIMEDOUT = 60, E_CONNREFUSED = 61,
};

const bool g_log = getenv("F1CE_NET_LOG") && atoi(getenv("F1CE_NET_LOG")) != 0;
#define NLOG(...) do { if (g_log) LOG_I("net: " __VA_ARGS__); } while (0)

std::string ip_str(u32 a) {
    char b[20];
    snprintf(b, sizeof(b), "%u.%u.%u.%u", a >> 24, (a >> 16) & 255, (a >> 8) & 255, a & 255);
    return b;
}
bool is_broadcast(u32 a) { return a == 0xFFFFFFFF || a == kBroadcast; }

// ------------------------------------------------------------------ host socket helpers (room transport)
void net_startup() {
#ifdef _WIN32
    static bool done = [] { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }();
    (void)done;
#endif
}
bool send_all(hsock s, const u8* p, size_t n) {
    while (n) {
        const int k = ::send(s, (const char*)p, (int)std::min<size_t>(n, 1 << 20), 0);
        if (k <= 0) return false;
        p += k, n -= (size_t)k;
    }
    return true;
}
bool recv_all(hsock s, u8* p, size_t n) {
    while (n) {
        const int k = ::recv(s, (char*)p, (int)n, 0);
        if (k <= 0) return false;
        p += k, n -= (size_t)k;
    }
    return true;
}
void put_le32(std::vector<u8>& v, u32 x) { for (int i = 0; i < 4; i++) v.push_back((u8)(x >> (8 * i))); }
void put_le16(std::vector<u8>& v, u16 x) { v.push_back((u8)x); v.push_back((u8)(x >> 8)); }
u32 get_le32(const u8* p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
u16 get_le16(const u8* p) { return (u16)(p[0] | p[1] << 8); }
void put_str(std::vector<u8>& v, const std::string& s, size_t n) {
    for (size_t i = 0; i < n; i++) v.push_back(i < s.size() && i + 1 < n ? (u8)s[i] : 0);
}
std::string get_str(const u8* p, size_t n) { return std::string((const char*)p, strnlen((const char*)p, n)); }
// a frame: u32 length (type + body), u8 type, body
std::vector<u8> frame(u8 type) { return {0, 0, 0, 0, type}; }
bool send_frame(hsock s, std::vector<u8>& f) {
    const u32 n = (u32)f.size() - 4;
    for (int i = 0; i < 4; i++) f[i] = (u8)(n >> (8 * i));
    return send_all(s, f.data(), f.size());
}
bool recv_frame(hsock s, u8& type, std::vector<u8>& body) {
    u8 h[5];
    if (!recv_all(s, h, 5)) return false;
    const u32 n = get_le32(h);
    if (n < 1 || n > (16u << 20)) return false;
    type = h[4];
    body.resize(n - 1);
    return body.empty() || recv_all(s, body.data(), body.size());
}
hsock tcp_connect(const std::string& host, u16 port) {
    net_startup();
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) || !res) return INVALID_SOCKET;
    hsock s = INVALID_SOCKET;
    for (addrinfo* a = res; a; a = a->ai_next) {
        s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        if (::connect(s, a->ai_addr, (int)a->ai_addrlen) == 0) break;
        hclose(s);
        s = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    if (s != INVALID_SOCKET) {
        int one = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
    }
    return s;
}

// ------------------------------------------------------------------ room server
struct RoomMember {
    hsock s;
    u32 addr = 0;
    std::string name;
    std::mutex send_m;
};
struct Room {
    std::mutex m;
    std::vector<std::shared_ptr<RoomMember>> members;
    std::string password;
} g_room;

void room_send(RoomMember& to, std::vector<u8> f) {
    std::lock_guard lk(to.send_m);
    send_frame(to.s, f);
}
void room_members_changed() {   // (g_room.m held)
    std::vector<u8> f = frame(F_MEMBERS);
    put_le32(f, (u32)g_room.members.size());
    for (auto& m : g_room.members) put_le32(f, m->addr), put_str(f, m->name, 16);
    for (auto& m : g_room.members) room_send(*m, f);
}
void room_member(hsock s) {
    auto me = std::make_shared<RoomMember>();
    me->s = s;
    u8 type;
    std::vector<u8> body;
    if (!recv_frame(s, type, body) || type != F_HELLO || body.size() < 56 || get_le32(&body[0]) != kMagic) {
        hclose(s);
        return;
    }
    auto reject = [&](const char* why) {
        std::vector<u8> f = frame(F_REJECT);
        put_str(f, why, 64);
        send_frame(s, f);
        hclose(s);
    };
    if (get_le32(&body[4]) != kVersion) return reject("different f1ce version");
    me->name = get_str(&body[8], 16);
    if (!g_room.password.empty() && get_str(&body[24], 32) != g_room.password) return reject("wrong password");
    {
        std::lock_guard lk(g_room.m);
        for (u32 a = 1; a < 255 && !me->addr; a++)
            if (std::none_of(g_room.members.begin(), g_room.members.end(), [&](auto& m) { return m->addr == (kSubnet | a); }))
                me->addr = kSubnet | a;
        if (!me->addr) return reject("room full");
        std::vector<u8> f = frame(F_WELCOME);
        put_le32(f, me->addr);
        room_send(*me, f);
        g_room.members.push_back(me);
        LOG_I("room: %s joined as %s (%zu members)", me->name.c_str(), ip_str(me->addr).c_str(), g_room.members.size());
        room_members_changed();
    }
    while (recv_frame(s, type, body)) {
        if (type != F_PACKET || body.size() < 9) continue;
        const u32 dst = get_le32(&body[4]);
        body[0] = (u8)me->addr, body[1] = (u8)(me->addr >> 8), body[2] = (u8)(me->addr >> 16), body[3] = (u8)(me->addr >> 24);
        std::vector<u8> f = frame(F_PACKET);
        f.insert(f.end(), body.begin(), body.end());
        std::vector<std::shared_ptr<RoomMember>> to;
        {
            std::lock_guard lk(g_room.m);
            for (auto& m : g_room.members)
                if (m != me && (is_broadcast(dst) || m->addr == dst)) to.push_back(m);
        }
        if (to.empty() && body[8] == K_SYN && body.size() >= 13) {   // nobody there: refuse the connection
            std::vector<u8> r = frame(F_PACKET);
            put_le32(r, dst), put_le32(r, me->addr), r.push_back(K_SYNACK);
            r.insert(r.end(), body.begin() + 9, body.begin() + 13);
            r.push_back(0);
            room_send(*me, r);
        }
        for (auto& m : to) room_send(*m, f);
    }
    std::lock_guard lk(g_room.m);
    g_room.members.erase(std::remove(g_room.members.begin(), g_room.members.end(), me), g_room.members.end());
    LOG_I("room: %s (%s) left (%zu members)", me->name.c_str(), ip_str(me->addr).c_str(), g_room.members.size());
    room_members_changed();
    hclose(s);
}
bool room_start(u16 port, const std::string& password) {
    net_startup();
    g_room.password = password;
    hsock ls = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) return false;
    int one = 1;
#ifndef _WIN32
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(ls, (sockaddr*)&a, sizeof(a)) || ::listen(ls, 16)) {
        LOG_E("room: cannot listen on TCP port %u (in use?)", port);
        hclose(ls);
        return false;
    }
    LOG_I("room: listening on TCP port %u%s", port, password.empty() ? "" : " (password)");
    std::thread([ls] {
        for (;;) {
            hsock s = ::accept(ls, nullptr, nullptr);
            if (s == INVALID_SOCKET) continue;
            int one = 1;
            setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
            std::thread(room_member, s).detach();
        }
    }).detach();
    return true;
}

// ------------------------------------------------------------------ virtual sockets
struct Datagram { u32 src; u16 sport; std::vector<u8> data; };
struct VSock {
    int type = 2;                 // 1 stream, 2 datagram
    u16 port = 0;                 // bound port (0: none)
    bool nbio = false, broadcast = false;
    s32 so_error = 0;
    u64 rcvtimeo = 0, sndtimeo = 0;   // microseconds, 0: none
    // datagram
    std::deque<Datagram> rxq;
    u32 def_addr = 0;             // connect()ed datagram peer
    u16 def_port = 0;
    // stream
    enum State { IDLE, LISTENING, CONNECTING, CONNECTED, REFUSED } state = IDLE;
    std::deque<int> backlog;      // accepted, not yet taken
    u32 init_addr = 0, cid = 0;   // stream name
    u32 peer = 0;
    u16 peer_port = 0;
    std::deque<u8> rx;
    bool peer_fin = false, shut_wr = false;
};
struct Net {
    std::mutex m;
    std::condition_variable cv;
    std::map<int, std::shared_ptr<VSock>> socks;
    std::map<std::pair<u32, u32>, int> streams;   // (initiator, cid) -> fd
    int next_fd = 3;
    u16 next_port = 49152;
    u32 next_cid = 1;
    u32 addr = kSubnet | 1;    // this game's address
    bool in_room = false;
    std::string members;       // for the log
    hsock room = INVALID_SOCKET;
    std::mutex room_send_m;
} g;

std::shared_ptr<VSock> sock(int fd) {   // (g.m held)
    auto it = g.socks.find(fd);
    return it == g.socks.end() ? nullptr : it->second;
}
bool port_used(int type, u16 port) {   // (g.m held)
    for (auto& [fd, s] : g.socks)
        if (s->type == type && s->port == port) return true;
    return false;
}
u16 ephemeral(int type) {   // (g.m held)
    for (int i = 0; i < 16384; i++) {
        const u16 p = g.next_port;
        g.next_port = g.next_port == 65535 ? 49152 : g.next_port + 1;
        if (!port_used(type, p)) return p;
    }
    return 0;
}

// a packet arriving for this game (from the room, or sent to itself); src/dst: virtual addresses
void deliver(u32 src, u32 dst, u8 kind, const u8* p, size_t n);
void send_packet(u32 dst, u8 kind, const std::vector<u8>& body) {
    const bool local = dst == g.addr || dst == kLoopback;
    if (local || is_broadcast(dst)) deliver(g.addr, dst, kind, body.data(), body.size());
    if (local) return;
    std::lock_guard lk(g.room_send_m);
    if (g.room == INVALID_SOCKET) {
        if (kind == K_SYN && body.size() >= 4) {   // no LAN: refused
            std::vector<u8> r(body.begin(), body.begin() + 4);
            r.push_back(0);
            deliver(dst, g.addr, K_SYNACK, r.data(), r.size());
        }
        return;
    }
    std::vector<u8> f = frame(F_PACKET);
    put_le32(f, g.addr), put_le32(f, dst), f.push_back(kind);
    f.insert(f.end(), body.begin(), body.end());
    send_frame(g.room, f);
}

void deliver(u32 src, u32 dst, u8 kind, const u8* p, size_t n) {
    std::vector<std::pair<u32, std::vector<u8>>> replies;   // sent after the lock
    {
        std::lock_guard lk(g.m);
        switch (kind) {
        case K_DGRAM: {
            if (n < 4) break;
            const u16 sport = get_le16(p), dport = get_le16(p + 2);
            for (auto& [fd, s] : g.socks)
                if (s->type == 2 && s->port == dport) {
                    s->rxq.push_back({src, sport, std::vector<u8>(p + 4, p + n)});
                    if (s->rxq.size() > 256) s->rxq.pop_front();
                    if (!is_broadcast(dst)) break;
                }
            break;
        }
        case K_SYN: {
            if (n < 8) break;
            const u32 cid = get_le32(p);
            const u16 sport = get_le16(p + 4), dport = get_le16(p + 6);
            std::shared_ptr<VSock> l;
            for (auto& [fd, s] : g.socks)
                if (s->type == 1 && s->state == VSock::LISTENING && s->port == dport) l = s;
            std::vector<u8> r;
            put_le32(r, cid);
            r.push_back(l && l->backlog.size() < 32);
            if (r.back()) {
                auto c = std::make_shared<VSock>();
                c->type = 1, c->port = dport, c->state = VSock::CONNECTED;
                c->init_addr = src, c->cid = cid, c->peer = src, c->peer_port = sport;
                const int fd = g.next_fd++;
                g.socks[fd] = c;
                g.streams[{src, cid}] = fd;
                l->backlog.push_back(fd);
                NLOG("connection from %s:%u to port %u (fd %d)", ip_str(src).c_str(), sport, dport, fd);
            }
            replies.push_back({src, r});
            break;
        }
        case K_SYNACK: {
            if (n < 5) break;
            auto it = g.streams.find({g.addr, get_le32(p)});
            if (it == g.streams.end()) break;
            if (auto s = sock(it->second); s && s->state == VSock::CONNECTING) {
                s->state = p[4] ? VSock::CONNECTED : VSock::REFUSED;
                if (!p[4]) s->so_error = E_CONNREFUSED;
                NLOG("fd %d %s", it->second, p[4] ? "connected" : "refused");
            }
            break;
        }
        case K_DATA:
        case K_FIN: {
            if (n < 8) break;
            auto it = g.streams.find({get_le32(p), get_le32(p + 4)});
            if (it == g.streams.end()) break;
            if (auto s = sock(it->second)) {
                if (kind == K_DATA) s->rx.insert(s->rx.end(), p + 8, p + n);
                else s->peer_fin = true;
            }
            break;
        }
        }
    }
    g.cv.notify_all();
    for (auto& [to, body] : replies) send_packet(to, K_SYNACK, body);
}

// ------------------------------------------------------------------ joining a room
void room_reader(hsock s) {
    u8 type;
    std::vector<u8> body;
    while (recv_frame(s, type, body)) {
        if (type == F_PACKET && body.size() >= 9) {
            deliver(get_le32(&body[0]), get_le32(&body[4]), body[8], body.data() + 9, body.size() - 9);
        } else if (type == F_MEMBERS && body.size() >= 4) {
            std::string list;
            const u32 n = get_le32(&body[0]);
            for (u32 i = 0; i < n && 4 + 20 * (i + 1) <= body.size(); i++)
                list += (i ? ", " : "") + get_str(&body[4 + 20 * i + 4], 16) + " (" + ip_str(get_le32(&body[4 + 20 * i])) + ")";
            LOG_I("net: room members: %s", list.c_str());
            std::lock_guard lk(g.m);
            g.members = list;
        }
    }
    LOG_W("net: lost the connection to the room");
    {
        std::lock_guard lk(g.room_send_m);
        hclose(g.room);
        g.room = INVALID_SOCKET;
    }
    std::lock_guard lk(g.m);
    g.in_room = false;
    for (auto& [fd, v] : g.socks)   // streams to other members end
        if (v->type == 1 && v->peer && v->peer != g.addr && v->peer != kLoopback && v->state == VSock::CONNECTED)
            v->peer_fin = true, v->so_error = E_CONNRESET;
    g.cv.notify_all();
}
void join_room(const std::string& host, u16 port, const std::string& name, const std::string& password) {
    hsock s = tcp_connect(host, port);
    if (s == INVALID_SOCKET) {
        LOG_E("net: cannot reach the room at %s:%u", host.c_str(), port);
        return;
    }
    std::vector<u8> f = frame(F_HELLO);
    put_le32(f, kMagic), put_le32(f, kVersion), put_str(f, name, 16), put_str(f, password, 32);
    u8 type;
    std::vector<u8> body;
    if (!send_frame(s, f) || !recv_frame(s, type, body) || type != F_WELCOME || body.size() < 4) {
        LOG_E("net: the room at %s:%u refused us%s%s", host.c_str(), port, type == F_REJECT ? ": " : "",
              type == F_REJECT ? get_str(body.data(), body.size()).c_str() : "");
        hclose(s);
        return;
    }
    {
        std::lock_guard lk(g.m);
        g.addr = get_le32(&body[0]);
        g.in_room = true;
    }
    g.room = s;
    LOG_I("net: joined the room at %s:%u as %s (%s)", host.c_str(), port, ip_str(g.addr).c_str(), name.c_str());
    std::thread(room_reader, s).detach();
}

std::string env_or(const char* name, const char* def) {
    const char* e = getenv(name);
    return e && *e ? e : def;
}
void net_init_once() {
    static std::once_flag once;
    std::call_once(once, [] {
        const std::string mode = env_or("F1CE_NET_MODE", "");
        const u16 port = (u16)atoi(env_or("F1CE_NET_PORT", "51015").c_str());
        const std::string name = env_or("F1CE_NET_NAME", "player"), password = env_or("F1CE_NET_PASSWORD", "");
        if (mode == "host") {
            if (room_start(port, password)) join_room("127.0.0.1", port, name, password);
        } else if (mode == "join") {
            std::string host = env_or("F1CE_NET_ADDR", "");
            u16 p = port;
            if (const size_t c = host.rfind(':'); c != std::string::npos) p = (u16)atoi(host.c_str() + c + 1), host.resize(c);
            if (host.empty()) LOG_E("net: F1CE_NET_MODE=join needs F1CE_NET_ADDR=<host>[:port]");
            else join_room(host, p, name, password);
        } else {
            LOG_I("net: no room (F1CE_NET_MODE unset): the LAN has only this game");
        }
    });
}

// ------------------------------------------------------------------ guest side helpers
thread_local u32 t_errno_ea = 0;
u32 errno_ea() {
    if (!t_errno_ea) t_errno_ea = mem::alloc(4, 4);
    return t_errno_ea;
}
s32 fail(s32 e) {
    st32(errno_ea(), (u32)e);
    return -1;
}
// sockaddr_in {u8 len, u8 family, u16 port, u32 addr, u8 zero[8]}; port and address in network order = guest order
bool read_addr(u32 ea, u32 len, u32& addr, u16& port) {
    if (!ea || len < 8) return false;
    port = ld16(ea + 2);
    addr = ld32(ea + 4);
    return true;
}
void write_addr(u32 ea, u32 len_ea, u32 addr, u16 port) {
    if (!ea) return;
    if (len_ea && ld32(len_ea) < 16) return;
    st8(ea, 16), st8(ea + 1, 2), st16(ea + 2, port), st32(ea + 4, addr);
    st32(ea + 8, 0), st32(ea + 12, 0);
    if (len_ea) st32(len_ea, 16);
}
u32 to_local(u32 a) { return a == 0 || a == kLoopback ? g.addr : a; }   // INADDR_ANY / loopback as a destination

// waits (g.m held via lk) until ready() or the timeout (0: none; nbio callers do not wait at all)
template <class F> bool wait_until(std::unique_lock<std::mutex>& lk, u64 timeout_us, F ready) {
    if (!timeout_us) {
        g.cv.wait(lk, ready);
        return true;
    }
    return g.cv.wait_for(lk, std::chrono::microseconds(timeout_us), ready);
}
bool readable(const VSock& s) {
    if (s.type == 2) return !s.rxq.empty();
    if (s.state == VSock::LISTENING) return !s.backlog.empty();
    return !s.rx.empty() || s.peer_fin || s.state == VSock::REFUSED;
}
bool writable(const VSock& s) { return s.type == 2 || s.state == VSock::CONNECTED || s.state == VSock::REFUSED; }
}  // namespace

// ------------------------------------------------------------------ sys_net
// sys_net_initialize_network_ex(param {memory, memory_size, flags})
HLE_FUNC(sys_net_initialize_network_ex) {
    net_init_once();
    RET(CELL_OK);
}
HLE_FUNC(sys_net_finalize_network) { RET(CELL_OK); }
HLE_FUNC(sys_net_free_thread_context) { RET(CELL_OK); }
HLE_FUNC(_sys_net_errno_loc) { RET(errno_ea()); }

// socket(family, type, protocol): SOCK_STREAM 1, SOCK_DGRAM 2 (SOCK_DGRAM_P2P 6 as datagrams)
HLE_FUNC(socket) {
    const s32 family = (s32)ARG(0), type = (s32)ARG(1);
    if (family != 2) RET(fail(E_AFNOSUPPORT));
    if (type != 1 && type != 2 && type != 6) RET(fail(E_PROTOTYPE));
    std::lock_guard lk(g.m);
    auto s = std::make_shared<VSock>();
    s->type = type == 1 ? 1 : 2;
    const int fd = g.next_fd++;
    g.socks[fd] = s;
    NLOG("socket(%d) -> %d", type, fd);
    RET(fd);
}
HLE_FUNC(socketclose) {
    const int fd = (int)ARG(0);
    std::shared_ptr<VSock> s;
    {
        std::lock_guard lk(g.m);
        s = sock(fd);
        if (!s) RET(fail(E_BADF));
        g.socks.erase(fd);
        for (int b : s->backlog) g.socks.erase(b);
        if (s->cid) g.streams.erase({s->init_addr, s->cid});
    }
    NLOG("socketclose(%d)", fd);
    if (s->type == 1 && s->state == VSock::CONNECTED && !s->shut_wr) {
        std::vector<u8> b;
        put_le32(b, s->init_addr), put_le32(b, s->cid);
        send_packet(s->peer, K_FIN, b);
    }
    g.cv.notify_all();
    RET(0);
}
// bind(fd, addr, len)
HLE_FUNC(bind) {
    u32 addr;
    u16 port;
    if (!read_addr((u32)ARG(1), (u32)ARG(2), addr, port)) RET(fail(E_INVAL));
    std::lock_guard lk(g.m);
    auto s = sock((int)ARG(0));
    if (!s) RET(fail(E_BADF));
    if (s->port) RET(fail(E_INVAL));
    if (!port) port = ephemeral(s->type);
    else if (port_used(s->type, port)) RET(fail(E_ADDRINUSE));
    s->port = port;
    NLOG("bind(%d, %s:%u)", (int)ARG(0), ip_str(addr).c_str(), port);
    RET(0);
}
HLE_FUNC(listen) {
    std::lock_guard lk(g.m);
    auto s = sock((int)ARG(0));
    if (!s) RET(fail(E_BADF));
    if (s->type != 1) RET(fail(E_INVAL));
    if (!s->port) s->port = ephemeral(1);
    s->state = VSock::LISTENING;
    NLOG("listen(%d) port %u", (int)ARG(0), s->port);
    RET(0);
}
// accept(fd, addr, len*)
HLE_FUNC(accept) {
    std::unique_lock lk(g.m);
    auto s = sock((int)ARG(0));
    if (!s) RET(fail(E_BADF));
    if (s->state != VSock::LISTENING) RET(fail(E_INVAL));
    if (s->backlog.empty()) {
        if (s->nbio) RET(fail(E_WOULDBLOCK));
        if (!wait_until(lk, s->rcvtimeo, [&] { return !s->backlog.empty() || !sock((int)ARG(0)); }) || s->backlog.empty())
            RET(fail(E_WOULDBLOCK));
    }
    const int fd = s->backlog.front();
    s->backlog.pop_front();
    if (auto a = sock(fd)) write_addr((u32)ARG(1), (u32)ARG(2), a->peer, a->peer_port);
    NLOG("accept(%d) -> %d", (int)ARG(0), fd);
    RET(fd);
}
// connect(fd, addr, len)
HLE_FUNC(connect) {
    u32 addr;
    u16 port;
    if (!read_addr((u32)ARG(1), (u32)ARG(2), addr, port)) RET(fail(E_INVAL));
    addr = to_local(addr);
    const int fd = (int)ARG(0);
    std::unique_lock lk(g.m);
    auto s = sock(fd);
    if (!s) RET(fail(E_BADF));
    NLOG("connect(%d, %s:%u)", fd, ip_str(addr).c_str(), port);
    if (s->type == 2) {
        s->def_addr = addr, s->def_port = port;
        if (!s->port) s->port = ephemeral(2);
        RET(0);
    }
    if (s->state == VSock::CONNECTED) RET(fail(E_ISCONN));
    if (s->state == VSock::CONNECTING) RET(fail(E_ALREADY));
    if (s->state == VSock::REFUSED) { s->state = VSock::IDLE; RET(fail(E_CONNREFUSED)); }
    if (addr != g.addr && (addr & 0xFFFFFF00) != kSubnet) RET(fail(E_NETUNREACH));   // (no internet on the LAN)
    if (!s->port) s->port = ephemeral(1);
    s->state = VSock::CONNECTING;
    s->init_addr = g.addr, s->cid = g.next_cid++, s->peer = addr, s->peer_port = port;
    g.streams[{s->init_addr, s->cid}] = fd;
    std::vector<u8> b;
    put_le32(b, s->cid), put_le16(b, s->port), put_le16(b, port);
    lk.unlock();
    send_packet(addr, K_SYN, b);
    lk.lock();
    if (s->nbio) {
        if (s->state == VSock::CONNECTED) RET(0);
        RET(fail(E_INPROGRESS));
    }
    wait_until(lk, 5000000, [&] { return s->state != VSock::CONNECTING; });
    if (s->state == VSock::CONNECTED) RET(0);
    if (s->state == VSock::CONNECTING) RET(fail(E_TIMEDOUT));
    s->so_error = 0;
    RET(fail(E_CONNREFUSED));
}
HLE_FUNC(shutdown) {
    std::unique_lock lk(g.m);
    auto s = sock((int)ARG(0));
    if (!s) RET(fail(E_BADF));
    const s32 how = (s32)ARG(1);   // 0 read, 1 write, 2 both
    if (s->type == 1 && s->state == VSock::CONNECTED && how != 0 && !s->shut_wr) {
        s->shut_wr = true;
        std::vector<u8> b;
        put_le32(b, s->init_addr), put_le32(b, s->cid);
        const u32 peer = s->peer;
        lk.unlock();
        send_packet(peer, K_FIN, b);
    }
    RET(0);
}

namespace {
// sendto/send: data to (addr, port), or the stream's peer
s32 do_send(PPUContext& c, int fd, u32 buf, u32 len, s32 flags, bool to, u32 addr, u16 port) {
    std::unique_lock lk(g.m);
    auto s = sock(fd);
    if (!s) return fail(E_BADF);
    std::vector<u8> data(len);
    if (len) memcpy(data.data(), gmem(buf), len);
    if (s->type == 2) {
        if (!to) {
            if (!s->def_port) return fail(E_DESTADDRREQ);
            addr = s->def_addr, port = s->def_port;
        }
        addr = to_local(addr);
        if (!s->port) s->port = ephemeral(2);
        if (addr == 0xFFFFFFFF) addr = kBroadcast;
        std::vector<u8> b;
        put_le16(b, s->port), put_le16(b, port);
        b.insert(b.end(), data.begin(), data.end());
        NLOG("sendto(%d, %u bytes, %s:%u) from port %u", fd, len, ip_str(addr).c_str(), port, s->port);
        lk.unlock();
        if (addr == kBroadcast || addr == g.addr || (addr & 0xFFFFFF00) == kSubnet) send_packet(addr, K_DGRAM, b);
        return (s32)len;
    }
    if (s->state != VSock::CONNECTED) return fail(s->state == VSock::REFUSED ? E_CONNREFUSED : E_NOTCONN);
    if (s->shut_wr) return fail(E_PIPE);
    const u32 peer = s->peer, init = s->init_addr, cid = s->cid;
    lk.unlock();
    for (u32 off = 0; off < len || len == 0; off += 16384) {
        std::vector<u8> b;
        put_le32(b, init), put_le32(b, cid);
        b.insert(b.end(), data.begin() + off, data.begin() + std::min(len, off + 16384));
        send_packet(peer, K_DATA, b);
        if (!len) break;
    }
    (void)flags;
    return (s32)len;
}
// recvfrom/recv; flags: MSG_PEEK 2, MSG_DONTWAIT 0x80
s32 do_recv(PPUContext& c, int fd, u32 buf, u32 len, s32 flags, u32 addr_ea, u32 len_ea) {
    std::unique_lock lk(g.m);
    auto s = sock(fd);
    if (!s) return fail(E_BADF);
    if (!readable(*s)) {
        if (s->nbio || (flags & 0x80)) return fail(E_WOULDBLOCK);
        if (!wait_until(lk, s->rcvtimeo, [&] { return readable(*s) || !sock(fd); }) || !sock(fd)) return fail(E_WOULDBLOCK);
    }
    if (s->type == 2) {
        Datagram& d = s->rxq.front();
        const u32 n = std::min<u32>(len, (u32)d.data.size());
        if (n) memcpy(gmem(buf), d.data.data(), n);
        write_addr(addr_ea, len_ea, d.src, d.sport);
        NLOG("recvfrom(%d) %zu bytes from %s:%u", fd, d.data.size(), ip_str(d.src).c_str(), d.sport);
        if (!(flags & 2)) s->rxq.pop_front();
        return (s32)n;
    }
    if (s->state == VSock::REFUSED) { s->so_error = 0; return fail(E_CONNREFUSED); }
    const u32 n = std::min<u32>(len, (u32)s->rx.size());
    std::copy(s->rx.begin(), s->rx.begin() + n, gmem(buf));
    if (!(flags & 2)) s->rx.erase(s->rx.begin(), s->rx.begin() + n);
    write_addr(addr_ea, len_ea, s->peer, s->peer_port);
    if (!n && s->so_error) return fail(s->so_error);
    return (s32)n;   // 0: the peer closed
}
}  // namespace

// sendto(fd, buf, len, flags, addr, addrlen)
HLE_FUNC(sendto) {
    u32 addr = 0;
    u16 port = 0;
    const bool to = read_addr((u32)ARG(4), (u32)ARG(5), addr, port);
    RET(do_send(c, (int)ARG(0), (u32)ARG(1), (u32)ARG(2), (s32)ARG(3), to, addr, port));
}
HLE_FUNC(send) { RET(do_send(c, (int)ARG(0), (u32)ARG(1), (u32)ARG(2), (s32)ARG(3), false, 0, 0)); }
// recvfrom(fd, buf, len, flags, addr, addrlen*)
HLE_FUNC(recvfrom) { RET(do_recv(c, (int)ARG(0), (u32)ARG(1), (u32)ARG(2), (s32)ARG(3), (u32)ARG(4), (u32)ARG(5))); }
HLE_FUNC(recv) { RET(do_recv(c, (int)ARG(0), (u32)ARG(1), (u32)ARG(2), (s32)ARG(3), 0, 0)); }

// getsockname(fd, addr, len*)
HLE_FUNC(getsockname) {
    std::lock_guard lk(g.m);
    auto s = sock((int)ARG(0));
    if (!s) RET(fail(E_BADF));
    write_addr((u32)ARG(1), (u32)ARG(2), g.addr, s->port);
    RET(0);
}
// setsockopt(fd, level, name, value, len): SOL_SOCKET 0xFFFF
HLE_FUNC(setsockopt) {
    std::lock_guard lk(g.m);
    auto s = sock((int)ARG(0));
    if (!s) RET(fail(E_BADF));
    const s32 level = (s32)ARG(1), name = (s32)ARG(2);
    const u32 v = (u32)ARG(3), len = (u32)ARG(4);
    const s32 val = v && len >= 4 ? (s32)ld32(v) : 0;
    NLOG("setsockopt(%d, %x, %x, %d)", (int)ARG(0), level, name, val);
    if (level == 0xFFFF) {
        if (name == 0x1100) s->nbio = val != 0;                    // SO_NBIO
        else if (name == 0x20) s->broadcast = val != 0;            // SO_BROADCAST
        else if (name == 0x1006 && v && len >= 16) s->rcvtimeo = ld64(v) * 1000000 + ld64(v + 8);   // SO_RCVTIMEO
        else if (name == 0x1005 && v && len >= 16) s->sndtimeo = ld64(v) * 1000000 + ld64(v + 8);   // SO_SNDTIMEO
    }
    RET(0);   // others (buffers, reuse, keepalive, TCP_NODELAY, IP options): accepted
}
// getsockopt(fd, level, name, value, len*)
HLE_FUNC(getsockopt) {
    std::lock_guard lk(g.m);
    auto s = sock((int)ARG(0));
    if (!s) RET(fail(E_BADF));
    const s32 level = (s32)ARG(1), name = (s32)ARG(2);
    const u32 v = (u32)ARG(3), len_ea = (u32)ARG(4);
    s32 out = 0;
    if (level == 0xFFFF) {
        if (name == 0x1007) out = s->so_error, s->so_error = 0;   // SO_ERROR
        else if (name == 0x1008) out = s->type;                   // SO_TYPE
        else if (name == 0x1001 || name == 0x1002) out = 65536;   // SO_SNDBUF / SO_RCVBUF
        else if (name == 0x1100) out = s->nbio;
        else if (name == 0x20) out = s->broadcast;
    }
    if (v) st32(v, (u32)out);
    if (len_ea) st32(len_ea, 4);
    RET(0);
}

// socketpoll(fds {s32 fd, s16 events, s16 revents}[], nfds, timeout ms (-1: forever))
HLE_FUNC(socketpoll) {
    const u32 fds = (u32)ARG(0), n = (u32)ARG(1);
    const s32 timeout = (s32)ARG(2);
    std::unique_lock lk(g.m);
    auto scan = [&] {
        s32 ready = 0;
        for (u32 i = 0; i < n; i++) {
            const u32 e = fds + 8 * i;
            const s32 fd = (s32)ld32(e);
            const u16 ev = ld16(e + 4);
            u16 rev = 0;
            if (fd >= 0) {
                auto s = sock(fd);
                if (!s) rev = 0x20;   // POLLNVAL
                else {
                    if ((ev & (0x01 | 0x40)) && readable(*s)) rev |= ev & (0x01 | 0x40);   // POLLIN / POLLRDNORM
                    if ((ev & 0x04) && writable(*s)) rev |= 0x04;                          // POLLOUT
                    if (s->state == VSock::REFUSED || s->so_error) rev |= 0x08;           // POLLERR
                    if (s->type == 1 && s->peer_fin) rev |= 0x10;                         // POLLHUP
                }
            }
            st16(e + 6, rev);
            ready += rev != 0;
        }
        return ready;
    };
    s32 r = scan();
    if (!r && timeout != 0) {
        auto pred = [&] { return scan() != 0; };
        if (timeout < 0) g.cv.wait(lk, pred);
        else g.cv.wait_for(lk, std::chrono::milliseconds(timeout), pred);
        r = scan();
    }
    RET(r);
}
// socketselect(nfds, read, write, except, timeout {s64 sec, s64 usec} or 0: forever); fd_set: 32 words, bit fd&31
// of word fd>>5
HLE_FUNC(socketselect) {
    const s32 nfds = std::min<s32>((s32)ARG(0), 1024);
    const u32 sets[3] = {(u32)ARG(1), (u32)ARG(2), (u32)ARG(3)}, tv = (u32)ARG(4);
    std::vector<u32> in[3];
    for (int k = 0; k < 3; k++)
        if (sets[k]) for (int w = 0; w < 32; w++) in[k].push_back(ld32(sets[k] + 4 * w));
    std::unique_lock lk(g.m);
    u32 out[3][32];
    auto scan = [&] {
        s32 ready = 0;
        memset(out, 0, sizeof(out));
        for (s32 fd = 0; fd < nfds; fd++) {
            const u32 bit = 1u << (fd & 31);
            auto s = sock(fd);
            for (int k = 0; k < 3; k++) {
                if (!sets[k] || !(in[k][fd >> 5] & bit)) continue;
                const bool r = !s || (k == 0 ? readable(*s) : k == 1 ? writable(*s) : s->state == VSock::REFUSED);
                if (r) out[k][fd >> 5] |= bit, ready++;
            }
        }
        return ready;
    };
    s32 r = scan();
    const u64 us = tv ? ld64(tv) * 1000000 + ld64(tv + 8) : ~0ull;
    if (!r && us) {
        auto pred = [&] { return scan() != 0; };
        if (us == ~0ull) g.cv.wait(lk, pred);
        else g.cv.wait_for(lk, std::chrono::microseconds(us), pred);
        r = scan();
    }
    for (int k = 0; k < 3; k++)
        if (sets[k]) for (int w = 0; w < 32; w++) st32(sets[k] + 4 * w, out[k][w]);
    RET(r);
}

// inet_aton(text, in_addr*) -> 1 valid, 0 not
HLE_FUNC(inet_aton) {
    const std::string s = gstr((u32)ARG(0), 64);
    unsigned a, b, c2, d;
    char tail;
    if (sscanf(s.c_str(), "%u.%u.%u.%u%c", &a, &b, &c2, &d, &tail) != 4 || a > 255 || b > 255 || c2 > 255 || d > 255) RET(0);
    if (ARG(1)) st32((u32)ARG(1), a << 24 | b << 16 | c2 << 8 | d);
    RET(1);
}
// gethostbyname(name) -> hostent {u32 h_name, s32 h_addrtype, s32 h_length, u32 h_aliases, u32 h_addr_list}: only
// dotted addresses resolve (the LAN has no name server; the Medius servers the online mode looks up are gone)
HLE_FUNC(gethostbyname) {
    const std::string s = gstr((u32)ARG(0), 256);
    LOG_I("net: gethostbyname(%s)", s.c_str());
    unsigned a, b, c2, d;
    char tail;
    if (sscanf(s.c_str(), "%u.%u.%u.%u%c", &a, &b, &c2, &d, &tail) != 4) RET(0);
    static u32 h = 0;
    if (!h) h = mem::alloc(64, 16);
    st32(h + 32, a << 24 | b << 16 | c2 << 8 | d);   // the address
    st32(h + 40, h + 32), st32(h + 44, 0);           // h_addr_list
    st32(h + 48, 0);                                 // h_aliases
    gstr_write(h + 52, "", 4);
    st32(h, h + 52), st32(h + 4, 2), st32(h + 8, 4), st32(h + 12, h + 48), st32(h + 16, h + 40);
    RET(h);
}

// ------------------------------------------------------------------ cellNetCtl
// Wired and connected, with this game's LAN address.
HLE_FUNC(cellNetCtlInit) { net_init_once(); RET(CELL_OK); }
HLE_FUNC(cellNetCtlTerm) { RET(CELL_OK); }
HLE_FUNC(cellNetCtlAddHandler) {   // (handler, arg, hid*): the state never changes, so it is never called
    if (ARG(2)) st32((u32)ARG(2), 1);
    RET(CELL_OK);
}
HLE_FUNC(cellNetCtlDelHandler) { RET(CELL_OK); }
// cellNetCtlGetInfo(code, info*): CellNetCtlInfo union
HLE_FUNC(cellNetCtlGetInfo) {
    net_init_once();
    const u32 code = (u32)ARG(0), info = (u32)ARG(1);
    u32 addr;
    {
        std::lock_guard lk(g.m);
        addr = g.addr;
    }
    static std::set<u32> logged;   // (LINK is polled every frame)
    if (logged.insert(code).second) NLOG("cellNetCtlGetInfo(%u)", code);
    switch (code) {
    case 1: st32(info, 0); break;                                           // DEVICE: wired
    case 2: {                                                               // ETHER_ADDR
        const u8 mac[8] = {0x02, 0xF1, 0xCE, 0x13, 0x37, (u8)addr, 0, 0};
        memcpy(gmem(info), mac, 8);
        break;
    }
    case 3: st32(info, 1500); break;                                        // MTU
    case 4: st32(info, 1); break;                                           // LINK: connected
    case 5: st32(info, 1); break;                                           // LINK_TYPE: auto
    case 13: st32(info, 2); break;                                          // IP_CONFIG: static
    case 16: gstr_write(info, ip_str(addr), 16); break;                     // IP_ADDRESS
    case 17: gstr_write(info, "255.255.255.0", 16); break;                  // NETMASK
    case 18: gstr_write(info, ip_str(kSubnet | 254), 16); break;           // DEFAULT_ROUTE
    case 19: case 20: gstr_write(info, "0.0.0.0", 16); break;               // DNS
    case 21: case 24: st32(info, 0); break;                                 // HTTP proxy, UPnP: off
    default: memset(gmem(info), 0, 16); break;
    }
    RET(CELL_OK);
}
// The network start dialog: nothing to show, the connection is up. LOADED and FINISHED at once, UNLOADED on unload
// (CELL_SYSUTIL_NET_CTL_NETSTART_LOADED 0x801, FINISHED 0x802, UNLOADED 0x803).
HLE_FUNC(cellNetCtlNetStartDialogLoadAsync) {
    net_init_once();
    sysutil_post_event(0x801, 0);
    sysutil_post_event(0x802, 0);
    RET(CELL_OK);
}
// (result {u32 size, s32 result}*)
HLE_FUNC(cellNetCtlNetStartDialogUnloadAsync) {
    if (ARG(0)) st32((u32)ARG(0) + 4, 0);
    sysutil_post_event(0x803, 0);
    RET(CELL_OK);
}

// ------------------------------------------------------------------ sceNp
// Never signed in to PSN: Play Online fails at sign-in (its servers are gone), LAN does not use NP.
namespace {
constexpr s32 kNpNotSignedIn = (s32)0x8002AA0A;   // SCE_NP_ERROR_INVALID_STATE family: no NP id while offline
}
HLE_FUNC(sceNpInit) { RET(CELL_OK); }
HLE_FUNC(sceNpTerm) { RET(CELL_OK); }
HLE_FUNC(sceNpManagerGetStatus) {   // (status*): SCE_NP_MANAGER_STATUS_OFFLINE
    if (ARG(0)) st32((u32)ARG(0), (u32)-1);
    RET(CELL_OK);
}
HLE_FUNC(sceNpManagerRegisterCallback) { RET(CELL_OK); }
HLE_FUNC(sceNpManagerGetNpId) { RET(kNpNotSignedIn); }
HLE_FUNC(sceNpManagerGetNetworkTime) { RET(kNpNotSignedIn); }
HLE_FUNC(sceNpManagerRequestTicket) { RET(kNpNotSignedIn); }
HLE_FUNC(sceNpManagerGetTicket) { RET(kNpNotSignedIn); }
HLE_FUNC(sceNpManagerGetTicketParam) { RET(kNpNotSignedIn); }
HLE_FUNC(sceNpManagerGetEntitlementIdList) { RET(0); }   // (count of entitlements)
HLE_FUNC(sceNpManagerGetEntitlementById) { RET(kNpNotSignedIn); }
HLE_FUNC(sceNpManagerGetContentRatingFlag) {   // (flag*, age*): no restriction
    if (ARG(0)) st32((u32)ARG(0), 0);
    if (ARG(1)) st32((u32)ARG(1), 99);
    RET(CELL_OK);
}
HLE_FUNC(sceNpManagerGetChatRestrictionFlag) {
    if (ARG(0)) st32((u32)ARG(0), 0);
    RET(CELL_OK);
}
HLE_FUNC(sceNpBasicRegisterHandler) { RET(CELL_OK); }
HLE_FUNC(sceNpBasicUnregisterHandler) { RET(CELL_OK); }
HLE_FUNC(sceNpBasicSetPresence) { RET(CELL_OK); }
HLE_FUNC(sceNpBasicAddPlayersHistory) { RET(CELL_OK); }
HLE_FUNC(sceNpBasicAddFriend) { RET(kNpNotSignedIn); }
HLE_FUNC(sceNpBasicGetEvent) { RET((s32)0x8002A61C); }   // SCE_NP_BASIC_ERROR_NO_EVENT
HLE_FUNC(sceNpBasicGetBlockListEntryCount) {
    if (ARG(0)) st32((u32)ARG(0), 0);
    RET(CELL_OK);
}
HLE_FUNC(sceNpBasicGetBlockListEntry) { RET(kNpNotSignedIn); }
HLE_FUNC(sceNpUtilCmpNpId) {   // (id1*, id2*): 0 equal (the 16-byte handle decides)
    RET(memcmp(gmem((u32)ARG(0)), gmem((u32)ARG(1)), 16) ? (s32)0x8002AA0B : 0);
}

// f1ce_online --room: a room without a game (loader.cpp)
[[noreturn]] void net_room_main() {
    const u16 port = (u16)atoi(env_or("F1CE_NET_PORT", "51015").c_str());
    if (!room_start(port, env_or("F1CE_NET_PASSWORD", ""))) process_exit(1);
    for (;;) std::this_thread::sleep_for(std::chrono::hours(1));
}
