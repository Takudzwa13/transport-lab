// simulator.hpp
// Minimal PA2-style event-driven network emulator interface.
//
// The black-box transport (mystery_transport.cpp) expects these definitions and hooks.
// Students should NOT edit mystery_transport.cpp, but they MAY add instrumentation in emulator.cpp
// (as allowed by the assignment).

#pragma once

#include <cstdint>

constexpr int A = 0;
constexpr int B = 1;
constexpr int PAYLOAD_SIZE = 20;

// Message from layer 5 (application)
struct msg {
    char data[PAYLOAD_SIZE];
};

// Packet on the wire
struct pkt {
    int seqnum;
    int acknum;
    int checksum;
    char payload[PAYLOAD_SIZE];
};

// Hooks provided by emulator (implemented in emulator.cpp)
void tolayer3(int entity, pkt packet);
void tolayer5(int entity, char data[PAYLOAD_SIZE]);
void starttimer(int entity, float increment);
void stoptimer(int entity);
float get_sim_time();

// Callbacks implemented by transport (mystery_transport.cpp)
void A_init();
void A_output(msg message);
void A_input(pkt packet);
void A_timerinterrupt();

void B_init();
void B_input(pkt packet);

// Transport tunables defined by mystery_transport.cpp; emulator sets them.
extern int   WINDOW_SIZE;
extern float TIMEOUT;
