#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <map>

#define A 0
#define B 1

extern void tolayer3(int, struct pkt);
extern void tolayer5(int, char data[20]);
extern void starttimer(int, float);
extern void stoptimer(int);
extern float get_sim_time();

struct msg {
    char data[20];
};

struct pkt {
    int seqnum;
    int acknum;
    int checksum;
    char payload[20];
};

int WINDOW_SIZE = 8;
float TIMEOUT = 20.0;

int A_base = 0;
int A_nextseq = 0;

std::map<int, pkt> A_window;
std::map<int, float> first_send_time;

int B_expected = 0;
std::map<int, pkt> B_buffer;

int checksum(pkt p) {
    int sum = p.seqnum + p.acknum;
    for (int i=0;i<20;i++) sum += p.payload[i];
    return sum;
}

int is_corrupt(pkt p) {
    return checksum(p) != p.checksum;
}

void send_packet(int seq, char data[20]) {
    pkt p;
    p.seqnum = seq;
    p.acknum = 0;
    memcpy(p.payload, data, 20);
    p.checksum = checksum(p);

    A_window[seq] = p;
    first_send_time[seq] = get_sim_time();
    tolayer3(A, p);
}

void A_output(struct msg message) {
    if (A_nextseq < A_base + WINDOW_SIZE) {
        send_packet(A_nextseq, message.data);
        if (A_base == A_nextseq)
            starttimer(A, TIMEOUT);
        A_nextseq++;
    }
}

void A_input(struct pkt packet) {
    if (is_corrupt(packet)) return;

    int ack = packet.acknum;
    if (ack >= A_base) {
        for (int i=A_base;i<=ack;i++)
            A_window.erase(i);

        A_base = ack + 1;

        stoptimer(A);
        if (A_base != A_nextseq)
            starttimer(A, TIMEOUT);
    }
}

void A_timerinterrupt() {
    starttimer(A, TIMEOUT);
    for (auto &entry : A_window)
        tolayer3(A, entry.second);
}

void A_init() {
    A_base = 0;
    A_nextseq = 0;
}

void B_input(struct pkt packet) {
    if (is_corrupt(packet)) return;

    if (packet.seqnum == B_expected) {
        tolayer5(B, packet.payload);
        B_expected++;
    }

    pkt ack;
    ack.seqnum = 0;
    ack.acknum = B_expected - 1;
    memset(ack.payload,0,20);
    ack.checksum = checksum(ack);
    tolayer3(B, ack);
}

void B_init() {
    B_expected = 0;
}