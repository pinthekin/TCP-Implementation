/* 
 * File:   sender_main.c
 * Author: 
 *
 * Created on 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <chrono>
#include <cmath>
#include <map>
#include <algorithm>
#include <fcntl.h>
#include <iostream>
#include "utils.hpp"

#define TIMEOUT 800
#define INITIAL_SST 512
#define INITIAL_CW 25

struct sockaddr_in si_other;
int s, slen;
STATE current_state = SLOW_START;
uint dupack = 0;
float cw = INITIAL_CW;
float sst = INITIAL_SST;
auto last_ack_time = std::chrono::high_resolution_clock::now();

void diep(char *s) {
    perror(s);
    exit(1);
}

// Returns if there is a timeout and makes adjustments to global vars. Call every iteration of loop
bool timeout() {
    // Get the current time point within the loop
    auto current_time = std::chrono::high_resolution_clock::now();

    // Calculate the elapsed time in microseconds
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(current_time - last_ack_time);

    // Check if 200 microseconds have passed
    if (elapsed.count() >= TIMEOUT) {
        sst = std::max((float)INITIAL_SST, cw/2);
        cw = INITIAL_CW;
        dupack = 0;
        current_state = SLOW_START;
        return true;
    }

    return false;
}

// Only call when there is a new ack
void handle_new_ack() {
    dupack = 0;
    if (current_state == SLOW_START) {
        if (cw >= sst) {
            current_state = CONGESTION_AVOIDANCE;
        }
        cw += 1;
    } else if (current_state == CONGESTION_AVOIDANCE) {
        cw += (1.0/floor(cw));
    } else if (current_state == FAST_RECOVERY) {
        cw = sst;
        current_state = CONGESTION_AVOIDANCE;
    }
}

// Only call when there is a dupack
void handle_dup_ack() {
    if (current_state == FAST_RECOVERY) {
        cw += 1;
    } else if (dupack >= 3) {
        sst = std::max((float)INITIAL_SST, cw/2);
        cw = sst + 3;
        current_state = FAST_RECOVERY;
    } else {
        dupack++;
    }
}


void reliablyTransfer(char* hostname, unsigned short int hostUDPport, char* filename, unsigned long long int bytesToTransfer) {
    //Open the file
    FILE *fp;
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        printf("Could not open file to send.");
        exit(1);
    }

	/* Determine how many bytes to transfer */

    slen = sizeof (si_other);

    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep("socket");

    memset((char *) &si_other, 0, sizeof (si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(hostUDPport);
    if (inet_aton(hostname, &si_other.sin_addr) == 0) {
        fprintf(stderr, "inet_aton() failed\n");
        exit(1);
    }

    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);

	/* Send data and receive acknowledgements on s*/
    uint current_packet_number = 0;
    std::map<uint, Packet*> packets_sent;
    ulong num_bytes_sent = 0;
    ulong num_bytes_left = bytesToTransfer;
    ulong num_bytes_processed = bytesToTransfer;

    while (num_bytes_processed > 0 || packets_sent.size()) {
        // std::cout << current_packet_number << std::endl;
        if (packets_sent.size() < cw) {
            // Read from file equal to min(num_bytes_to_send, payload_size)
            // Construct packet, add to map
            // send packet
            Packet* to_send = new Packet();
            ulong packet_size = std::min(num_bytes_left, (ulong)PAYLOAD_SIZE);
            
            fread(to_send->contents, 1, packet_size, fp);
            to_send->packet_index = current_packet_number;
            to_send->size = packet_size;
            to_send->type = INFO;
            ssize_t status;
            do {
                status = sendto(s, to_send, sizeof(Packet), 0, (sockaddr*)&si_other, sizeof(si_other));
            } while (status == -1 && (errno == EAGAIN || errno == EWOULDBLOCK));
            
            
            num_bytes_sent += packet_size;
            num_bytes_left -= packet_size;
            // std::cout << to_send.type << std::endl;
            packets_sent[current_packet_number] = to_send;
            current_packet_number++;
            // std::cout << packets_sent.size() << std::endl;
        }
        Packet received_ack;
        Packet* oldest_packet = packets_sent.begin()->second;
        // std::cout << oldest_packet->packet_index << std::endl;
        ssize_t status;
        do {
            status = recvfrom(s, &received_ack, sizeof(Packet), 0, (sockaddr*)&si_other, (socklen_t*)&slen);
        } while (status == -1 && (errno == EAGAIN || errno == EWOULDBLOCK));
        
        if (status != -1) {
            if (received_ack.type == DUP) {
                auto it = packets_sent.find(received_ack.packet_index);
                if (it != packets_sent.end()) {
                    // delete it->second;
                    packets_sent.erase(it);
                    num_bytes_processed -= received_ack.size;
                }
                handle_dup_ack();
            } else if (received_ack.packet_index >= oldest_packet->packet_index) {
                last_ack_time = std::chrono::high_resolution_clock::now();
                auto it = packets_sent.find(received_ack.packet_index);
                if (it != packets_sent.end()) {
                    // delete it->second;
                    packets_sent.erase(it);
                    num_bytes_processed -= received_ack.size;
                }
                handle_new_ack();
            }
        }


        if (timeout()) {
            // std::cout << oldest_packet->type << std::endl;
            auto it = packets_sent.begin();
            for (int i = 0; i < 32 && it != packets_sent.end(); i++) {
                oldest_packet = it->second;
                last_ack_time = std::chrono::high_resolution_clock::now();
                sendto(s, oldest_packet, sizeof(Packet), 0, (sockaddr*)&si_other, sizeof(si_other));
            }
        }

        // Read socket for ack. Depending on type of ack, handle differently
            // If new_ack, remove from num_bytes_to_send and packets_sent and call handle new_ack. If the packet is the earliest packet sent, restart timeout
            // If dupack call handle dupack
        // If timeout, retransmit first packet
    }
    // Send 5 fin packets
    Packet fin_ack;
    fin_ack.type = FIN;
    for (int i = 0; i < 10; i++) {
        sendto(s, &fin_ack, sizeof(Packet), 0, (sockaddr*)&si_other, sizeof(si_other));
    }
    fclose(fp);
    printf("Closing the socket\n");
    close(s);
    return;

}

/*
 * 
 */
int main(int argc, char** argv) {

    unsigned short int udpPort;
    unsigned long long int numBytes;

    if (argc != 5) {
        fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
        exit(1);
    }
    udpPort = (unsigned short int) atoi(argv[2]);
    numBytes = atoll(argv[4]);



    reliablyTransfer(argv[1], udpPort, argv[3], numBytes);


    return (EXIT_SUCCESS);
}


