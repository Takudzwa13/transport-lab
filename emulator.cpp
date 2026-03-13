// emulator.cpp
// Minimal event-driven simulator for the Transport Forensics Lab.
// Provides PA2-style environment and runs a workload from A->B.
//
// Build:
//   make EMULATOR=emulator.cpp
//
// Run (example):
//   ./sim --msgs 5000 --interval 5.0 --loss 0.2 --corrupt 0.1 --delay 10 --jitter 5 --timeout 20 --win 8 --seed 1234 --out outdir
//
// Notes:
// - All time is simulated time (no wall-clock).
// - The channel models random delay, loss, and corruption.
// - A single timer per entity is supported.
// - Logs are written as CSV for student analysis.

#include "simulator.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

enum class EvType {
    FROM_LAYER5,     // generate a new msg at A
    PACKET_ARRIVAL,  // packet arrives at A or B
    TIMER_INTERRUPT  // timer fires at A or B
};

struct Event {
    double time;      // absolute simulation time
    std::uint64_t id; // unique id
    EvType type;
    int entity;       // A or B
    pkt packet;       // valid if PACKET_ARRIVAL
    msg message;      // valid if FROM_LAYER5
};

struct EventCmp {
    bool operator()(const Event& a, const Event& b) const {
        if (a.time != b.time) return a.time > b.time; // min-heap behavior
        return a.id > b.id; // FCFS for equal time
    }
};

static double g_now = 0.0;
float get_sim_time() { return static_cast<float>(g_now); }

// RNG
static std::mt19937 rng;
static std::uniform_real_distribution<double> uni01(0.0, 1.0);

// Config
static int    cfg_msgs = 2000;
static double cfg_interval = 5.0;      // time between app messages
static double cfg_loss = 0.1;
static double cfg_corrupt = 0.1;
static double cfg_base_delay = 10.0;   // base one-way delay
static double cfg_jitter = 5.0;        // additional random delay in [0, jitter]
static int    cfg_seed = 1234;
static std::string cfg_outdir = "out";
static int    cfg_payload_mode = 0;    // 0: fixed pattern, 1: include msg index

// Derived / stats
static std::uint64_t next_event_id = 1;
static std::priority_queue<Event, std::vector<Event>, EventCmp> evq;
static std::unordered_map<std::uint64_t, bool> canceled;

// timer state: one timer per entity
static bool timer_active[2] = {false, false};
static std::uint64_t timer_event_id[2] = {0, 0};

// Logging
static std::ofstream log_packets;
static std::ofstream log_events;

// Simple counters
static std::uint64_t count_A_toLayer3 = 0;
static std::uint64_t count_B_toLayer3 = 0;
static std::uint64_t count_delivered_B = 0;
static std::uint64_t count_corrupted_seen = 0;
static std::uint64_t count_dropped_loss = 0;

// Track retransmissions at A by counting sends per seq
static std::unordered_map<int, int> send_count_by_seq;

// Track per-message generation time at layer5 for rough goodput time base
static std::vector<double> msg_gen_time;

// Schedule helpers
static void schedule(Event e) {
    evq.push(e);
}

static void cancel_event(std::uint64_t id) {
    if (id != 0) canceled[id] = true;
}

static bool is_canceled(std::uint64_t id) {
    auto it = canceled.find(id);
    return it != canceled.end() && it->second;
}

static double channel_delay() {
    // base + uniform jitter
    return cfg_base_delay + uni01(rng) * cfg_jitter;
}

static pkt maybe_corrupt(pkt p) {
    if (uni01(rng) < cfg_corrupt) {
        // Flip one byte of payload or toggle ack/seq slightly (deterministic-ish)
        int idx = static_cast<int>(uni01(rng) * PAYLOAD_SIZE) % PAYLOAD_SIZE;
        p.payload[idx] = static_cast<char>(p.payload[idx] ^ 0xFF);
        // do NOT update checksum => corruption will be detected
    }
    return p;
}

static void ensure_outdir() {
    fs::create_directories(cfg_outdir);
}

static void open_logs() {
    ensure_outdir();
    log_packets.open(fs::path(cfg_outdir) / "packets.csv");
    log_events.open(fs::path(cfg_outdir) / "events.csv");
    log_packets << "time,entity,dir,seq,ack,checksum,payload_first_byte\n";
    log_events << "time,type,entity,detail\n";
}

// PA2 hooks used by transport
void tolayer3(int entity, pkt packet) {
    // Log send
    if (entity == A) {
        count_A_toLayer3++;
        // approximate retransmission counting for DATA packets (acknum==0)
        if (packet.acknum == 0) send_count_by_seq[packet.seqnum] += 1;
        log_packets << std::fixed << std::setprecision(6)
                    << g_now << "," << entity << ",send,"
                    << packet.seqnum << "," << packet.acknum << "," << packet.checksum
                    << "," << (int)(unsigned char)packet.payload[0] << "\n";
    } else {
        count_B_toLayer3++;
        log_packets << std::fixed << std::setprecision(6)
                    << g_now << "," << entity << ",send,"
                    << packet.seqnum << "," << packet.acknum << "," << packet.checksum
                    << "," << (int)(unsigned char)packet.payload[0] << "\n";
    }

    // Loss
    if (uni01(rng) < cfg_loss) {
        count_dropped_loss++;
        log_events << std::fixed << std::setprecision(6)
                   << g_now << ",DROP_LOSS," << entity << ",seq=" << packet.seqnum << " ack=" << packet.acknum << "\n";
        return;
    }

    // Corrupt possibly
    pkt onwire = maybe_corrupt(packet);

    int other = (entity == A) ? B : A;
    Event e{};
    e.time = g_now + channel_delay();
    e.id = next_event_id++;
    e.type = EvType::PACKET_ARRIVAL;
    e.entity = other;
    e.packet = onwire;

    schedule(e);
}

void tolayer5(int entity, char data[PAYLOAD_SIZE]) {
    if (entity == B) {
        count_delivered_B++;
    }
    // Log delivery (payload byte 0 is enough for debugging)
    log_events << std::fixed << std::setprecision(6)
               << g_now << ",DELIVER_L5," << entity
               << ",payload0=" << (int)(unsigned char)data[0] << "\n";
}

void starttimer(int entity, float increment) {
    // Single timer per entity. If already active, ignore (classic PA2 behavior varies; this is fine for the lab).
    if (timer_active[entity]) return;

    Event e{};
    e.time = g_now + increment;
    e.id = next_event_id++;
    e.type = EvType::TIMER_INTERRUPT;
    e.entity = entity;

    timer_active[entity] = true;
    timer_event_id[entity] = e.id;
    schedule(e);

    log_events << std::fixed << std::setprecision(6)
               << g_now << ",TIMER_START," << entity << ",dt=" << increment << "\n";
}

void stoptimer(int entity) {
    if (!timer_active[entity]) return;
    cancel_event(timer_event_id[entity]);
    timer_active[entity] = false;
    timer_event_id[entity] = 0;

    log_events << std::fixed << std::setprecision(6)
               << g_now << ",TIMER_STOP," << entity << ",\n";
}

// Driver: generate app messages at A
static msg make_msg(int idx) {
    msg m{};
    // Deterministic payload pattern; can embed idx in first byte for easier trace reading.
    for (int i = 0; i < PAYLOAD_SIZE; i++) m.data[i] = static_cast<char>('a' + (idx + i) % 26);
    if (cfg_payload_mode == 1) {
        m.data[0] = static_cast<char>(idx % 256);
    }
    return m;
}

static void schedule_layer5_msgs() {
    msg_gen_time.resize(cfg_msgs);
    for (int i = 0; i < cfg_msgs; i++) {
        Event e{};
        e.time = i * cfg_interval;
        e.id = next_event_id++;
        e.type = EvType::FROM_LAYER5;
        e.entity = A;
        e.message = make_msg(i);
        schedule(e);
        msg_gen_time[i] = e.time;
    }
}

static std::string evtype_name(EvType t) {
    switch (t) {
        case EvType::FROM_LAYER5: return "FROM_LAYER5";
        case EvType::PACKET_ARRIVAL: return "PACKET_ARRIVAL";
        case EvType::TIMER_INTERRUPT: return "TIMER_INTERRUPT";
    }
    return "UNKNOWN";
}

static void usage(const char* prog) {
    std::cerr <<
    "Usage: " << prog << " [options]\n"
    "Options:\n"
    "  --msgs N            number of application messages from A (default 2000)\n"
    "  --interval T        inter-arrival time between messages (default 5.0)\n"
    "  --loss P            packet loss probability in [0,1] (default 0.1)\n"
    "  --corrupt P         packet corruption probability in [0,1] (default 0.1)\n"
    "  --delay D           base one-way delay (default 10)\n"
    "  --jitter J          additional random one-way delay in [0,J] (default 5)\n"
    "  --win W             sender window size (default 8)\n"
    "  --timeout T         sender timeout (default 20)\n"
    "  --seed S            RNG seed (default 1234)\n"
    "  --out DIR           output directory (default out)\n"
    "  --payload-mode M    0=letters, 1=first byte is msg index mod 256 (default 0)\n"
    "  -h, --help          show this help\n";
}

static void parse_args(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto need = [&](const char* opt) {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << opt << "\n";
                std::exit(2);
            }
            return std::string(argv[++i]);
        };
        if (a == "--msgs") cfg_msgs = std::stoi(need("--msgs"));
        else if (a == "--interval") cfg_interval = std::stod(need("--interval"));
        else if (a == "--loss") cfg_loss = std::stod(need("--loss"));
        else if (a == "--corrupt") cfg_corrupt = std::stod(need("--corrupt"));
        else if (a == "--delay") cfg_base_delay = std::stod(need("--delay"));
        else if (a == "--jitter") cfg_jitter = std::stod(need("--jitter"));
        else if (a == "--win") WINDOW_SIZE = std::stoi(need("--win"));
        else if (a == "--timeout") TIMEOUT = std::stof(need("--timeout"));
        else if (a == "--seed") cfg_seed = std::stoi(need("--seed"));
        else if (a == "--out") cfg_outdir = need("--out");
        else if (a == "--payload-mode") cfg_payload_mode = std::stoi(need("--payload-mode"));
        else if (a == "-h" || a == "--help") { usage(argv[0]); std::exit(0); }
        else {
            std::cerr << "Unknown option: " << a << "\n";
            usage(argv[0]);
            std::exit(2);
        }
    }

    // Basic validation
    auto in01 = [](double x){ return x >= 0.0 && x <= 1.0; };
    if (!in01(cfg_loss) || !in01(cfg_corrupt)) {
        std::cerr << "loss and corrupt must be in [0,1]\n";
        std::exit(2);
    }
    if (cfg_msgs <= 0 || cfg_interval <= 0.0) {
        std::cerr << "msgs must be >0 and interval must be >0\n";
        std::exit(2);
    }
    if (WINDOW_SIZE <= 0 || TIMEOUT <= 0.0f) {
        std::cerr << "win and timeout must be >0\n";
        std::exit(2);
    }
}

static void run_sim() {
    open_logs();

    // Init RNG + protocol
    rng.seed(static_cast<std::uint32_t>(cfg_seed));
    A_init();
    B_init();

    schedule_layer5_msgs();

    // Main loop
    while (!evq.empty()) {
        Event e = evq.top(); evq.pop();
        if (is_canceled(e.id)) continue;

        g_now = e.time;

        if (e.type == EvType::FROM_LAYER5) {
            log_events << std::fixed << std::setprecision(6)
                       << g_now << ",FROM_LAYER5," << e.entity << ",\n";
            A_output(e.message);
        } else if (e.type == EvType::PACKET_ARRIVAL) {
            // count corruption at arrival (for sanity checks)
            // (We do not know "true corruption" except via checksum mismatch.)
            // We'll let transport detect it; but we can also record a mismatch here.
            // We don't have access to transport checksum func, so approximate by re-checking in transport isn't possible here.
            log_packets << std::fixed << std::setprecision(6)
                        << g_now << "," << e.entity << ",recv,"
                        << e.packet.seqnum << "," << e.packet.acknum << "," << e.packet.checksum
                        << "," << (int)(unsigned char)e.packet.payload[0] << "\n";

            if (e.entity == A) {
                A_input(e.packet);
            } else {
                B_input(e.packet);
            }
        } else if (e.type == EvType::TIMER_INTERRUPT) {
            timer_active[e.entity] = false;
            timer_event_id[e.entity] = 0;
            log_events << std::fixed << std::setprecision(6)
                       << g_now << ",TIMER_FIRE," << e.entity << ",\n";
            if (e.entity == A) A_timerinterrupt();
            // (B timer not used by mystery transport)
        }
    }

    // Summaries
    const double sim_end = g_now;
    std::ofstream summary(fs::path(cfg_outdir) / "summary.txt");
    summary << "seed=" << cfg_seed << "\n";
    summary << "msgs=" << cfg_msgs << " interval=" << cfg_interval << "\n";
    summary << "loss=" << cfg_loss << " corrupt=" << cfg_corrupt << "\n";
    summary << "delay=" << cfg_base_delay << " jitter=" << cfg_jitter << "\n";
    summary << "win=" << WINDOW_SIZE << " timeout=" << TIMEOUT << "\n";
    summary << "sim_end=" << sim_end << "\n";
    summary << "A_tlayer3=" << count_A_toLayer3 << "\n";
    summary << "B_tlayer3=" << count_B_toLayer3 << "\n";
    summary << "delivered_B=" << count_delivered_B << "\n";
    summary << "dropped_loss=" << count_dropped_loss << "\n";

    // retransmissions estimate: total sends minus unique seq sends (data only)
    std::uint64_t unique_data = 0;
    std::uint64_t total_data_sends = 0;
    for (const auto& kv : send_count_by_seq) {
        unique_data += 1;
        total_data_sends += kv.second;
    }
    std::uint64_t retrans = (total_data_sends >= unique_data) ? (total_data_sends - unique_data) : 0;
    summary << "data_unique_seq_sent=" << unique_data << "\n";
    summary << "data_total_sends=" << total_data_sends << "\n";
    summary << "data_retransmissions_est=" << retrans << "\n";

    // crude goodput/throughput estimates
    // goodput: delivered_B * payload_size / sim_time
    // throughput: A_tlayer3 (data+retrans) * payload_size / sim_time (approx; includes ACKs too if payload size differs)
    const double goodput_bps = (sim_end > 0) ? (count_delivered_B * PAYLOAD_SIZE * 8.0 / sim_end) : 0.0;
    const double send_bps = (sim_end > 0) ? (count_A_toLayer3 * PAYLOAD_SIZE * 8.0 / sim_end) : 0.0;
    summary << std::fixed << std::setprecision(3);
    summary << "goodput_bps_approx=" << goodput_bps << "\n";
    summary << "sender_send_bps_approx=" << send_bps << "\n";

    std::cout << "Simulation finished. Output in: " << cfg_outdir << "\n";
    std::cout << "Delivered to B (layer5): " << count_delivered_B << " messages\n";
    std::cout << "Approx goodput (bps): " << goodput_bps << "\n";
    std::cout << "Approx sender send rate (bps): " << send_bps << "\n";
}

int main(int argc, char** argv) {
    parse_args(argc, argv);
    run_sim();
    return 0;
}
