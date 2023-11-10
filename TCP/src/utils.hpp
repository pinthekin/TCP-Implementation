#ifndef UTILS_HPP
#define UTILS_HPP
#define uint unsigned int
#define ulong unsigned long long int

#define PAYLOAD_SIZE 2956

enum STATE {
    SLOW_START,
    CONGESTION_AVOIDANCE,
    FAST_RECOVERY
};

enum PACKET_TYPE {
    ACK,
    FIN,
    INFO,
    DUP
};

struct Packet {
    uint size;
    uint packet_index;
    char contents[PAYLOAD_SIZE];
    PACKET_TYPE type;
};

uint duplicates = 0;


#endif