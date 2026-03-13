// mystery_transport.cpp
// Black-box transport implementation for the "Transport Protocol Forensics Lab".
// IMPORTANT (student distribution): DO NOT MODIFY THIS FILE.
//
// This intentionally exhibits suboptimal behavior under congestion:
// - Cumulative ACKs only (no SACK)
// - Single timer at sender
// - On timeout, retransmits ALL outstanding packets (GBN-like timeout behavior)
// - Receiver delivers strictly in-order, ACKs cumulatively
//
// The goal of the assignment is to diagnose these behaviors, not to "fix" them.

#include "simulator.hpp"
#include <cstring>
#include <map>

// Tunables (set by emulator before A_init/B_init)
int   WINDOW_SIZE = 8;
float TIMEOUT     = 20.0f;

// ---------------- Sender (A) state ----------------
static int A_base = 0;
static int A_nextseq = 0;

// Outstanding packets by seq
static std::map<int, pkt> A_window;

// ---------------- Receiver (B) state ----------------
static int B_expected = 0;

// ---------------- Utilities ----------------
static int compute_checksum(const pkt& p) {
    int sum = p.seqnum + p.acknum;
    for (int i = 0; i < PAYLOAD_SIZE; i++) sum += (unsigned char)p.payload[i];
    return sum;
}
static bool is_corrupt(const pkt& p) {
    return compute_checksum(p) != p.checksum;
}

static pkt make_data_pkt(int seq, const msg& m) {
    pkt p{};
    p.seqnum = seq;
    p.acknum = 0;
    std::memcpy(p.payload, m.data, PAYLOAD_SIZE);
    p.checksum = compute_checksum(p);
    return p;
}
static pkt make_ack_pkt(int acknum) {
    pkt p{};
    p.seqnum = 0;
    p.acknum = acknum;
    std::memset(p.payload, 0, PAYLOAD_SIZE);
    p.checksum = compute_checksum(p);
    return p;
}

// ---------------- Required callbacks ----------------
void A_init() {
    A_base = 0;
    A_nextseq = 0;
    A_window.clear();
}

void B_init() {
    B_expected = 0;
}

void A_output(msg message) {
    // If window has space, send immediately; otherwise drop at layer-5 boundary
    // (intentional simplification: students will observe load sensitivity).
    if (A_nextseq < A_base + WINDOW_SIZE) {
        pkt p = make_data_pkt(A_nextseq, message);
        A_window[A_nextseq] = p;

        tolayer3(A, p);

        // Single timer: track oldest unACKed packet (A_base)
        if (A_base == A_nextseq) {
            starttimer(A, TIMEOUT);
        }
        A_nextseq++;
    } else {
        // intentional: no sender-side app buffering
        // (forces visible collapse earlier, which is useful for the lab)
    }
}

void A_input(pkt packet) {
    if (is_corrupt(packet)) return;

    const int ack = packet.acknum;

    if (ack >= A_base) {
        // cumulative ACK: remove everything <= ack
        for (int s = A_base; s <= ack; s++) {
            A_window.erase(s);
        }
        A_base = ack + 1;

        // timer management
        stoptimer(A);
        if (A_base != A_nextseq) {
            starttimer(A, TIMEOUT);
        }
    }
}

void A_timerinterrupt() {
    // Timeout retransmits ALL outstanding packets (GBN-like behavior)
    starttimer(A, TIMEOUT);
    for (const auto& kv : A_window) {
        tolayer3(A, kv.second);
    }
}

void B_input(pkt packet) {
    if (is_corrupt(packet)) {
        // On corruption, re-ACK last in-order delivered
        pkt ackp = make_ack_pkt(B_expected - 1);
        tolayer3(B, ackp);
        return;
    }

    if (packet.seqnum == B_expected) {
        tolayer5(B, packet.payload);
        B_expected++;
    }
    // cumulative ACK for last in-order delivered
    pkt ackp = make_ack_pkt(B_expected - 1);
    tolayer3(B, ackp);
}
