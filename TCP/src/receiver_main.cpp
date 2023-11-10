/* 
 * File:   receiver_main.c
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
#include "utils.hpp"
#include <algorithm>
#include <vector>
#include <iostream>
#include <fstream>

#define MAX_QUEUE_SIZE 1000

struct sockaddr_in si_me, si_other;
int s, slen;

void diep(char *s) {
    perror(s);
    exit(1);
}



void reliablyReceive(unsigned short int myUDPport, char* destinationFile) {
    slen = sizeof (si_other);
    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep("socket");

    memset((char *) &si_me, 0, sizeof (si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(myUDPport);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    printf("Now binding\n");
    if (bind(s, (struct sockaddr*) &si_me, sizeof (si_me)) == -1)
        diep("bind");

	/* Now receive data and send acknowledgements */
    std::unordered_map<uint, Packet*> packet_queue;

    std::ofstream output;
    output.open(destinationFile, std::ios::trunc);
    output.close();
    output.open(destinationFile, std::ios::app);

    uint current_index = 0;
    while(true) {
        Packet* received_data = new Packet();
        ssize_t status;
        do {
            status = recvfrom(s, received_data, sizeof(Packet), 0, (sockaddr*)&si_other, (socklen_t*)&slen);
        } while (status == -1 && (errno == EAGAIN || errno == EWOULDBLOCK));
        // std::cout << current_index << std::endl;
        // if (current_index % 1000 == 0) {
        //     std::cout << received_data->packet_index << " " << current_index << std::endl;
        //     std::cout << current_index << std::endl;
        // }
        packet_queue[received_data->packet_index] = received_data;
        // std::cout << received_data.packet_index << " " << current_index << std::endl;
        // std::cout << received_data.type << std::endl;

        if (received_data->type == INFO) {
            // std::cout << "SEND DUP" << std::endl;
            if (received_data->packet_index < current_index) {
                // std::cout << received_data.packet_index << " " << current_index << std::endl;
                // std::cout << received_data.packet_index << std::endl;
                // std::cout << current_index << std::endl;
                auto it = packet_queue.find(received_data->packet_index);
                if (it != packet_queue.end()) {
                    // delete it->second;
                    packet_queue.erase(it);
                }
                Packet dup_ack;
                dup_ack.packet_index = received_data->packet_index;
                dup_ack.size = received_data->size;
                dup_ack.type = DUP;
                do {
                    status = sendto(s, &dup_ack, sizeof(dup_ack), 0, (sockaddr*)&si_other, (socklen_t)slen);
                } while (status == -1 && (errno == EAGAIN || errno == EWOULDBLOCK));
                
                // std::cout << "SENDING DUP" << std::endl;
            } else if (received_data->packet_index == current_index) {
                // Write to end of file
                // send ack
                // std::cout << "FIND" << std::endl;
                
                do {
                    Packet* current_packet = packet_queue[current_index];
                    output.write((const char*)current_packet->contents, current_packet->size);
                    
                    Packet ack;
                    ack.packet_index = received_data->packet_index;
                    ack.size = received_data->size;
                    ack.type = ACK;

                    do {
                        status = sendto(s, &ack, sizeof(ack), 0, (sockaddr*)&si_other, (socklen_t)slen);
                    } while (status == -1 && (errno == EAGAIN || errno == EWOULDBLOCK));
                    
                    // delete packet_queue.find(current_index)->second;
                    packet_queue.erase(current_index);
                    current_index++;
                } while (packet_queue.find(current_index) != packet_queue.end());
                
            } else {
                // std::cout << "FIN" << std::endl;
                if (packet_queue.size() < MAX_QUEUE_SIZE) {
                    // packet_queue[received_data->packet_index] = received_data;
                    // Packet ack;
                    // ack.packet_index = received_data->packet_index;
                    // ack.size = received_data->size;
                    // ack.type = ACK;
                    // sendto(s, &ack, sizeof(ack), 0, (sockaddr*)&si_other, (socklen_t)slen);
                    packet_queue[received_data->packet_index] = received_data;
                }
                // Packet ack;
                // ack.packet_index = received_data.packet_index;
                // ack.size = received_data.size;
                // ack.type = ACK;
                // sendto(s, &ack, sizeof(ack), 0, (sockaddr*)&si_other, (socklen_t)slen);
                // packet_queue[received_data.packet_index] = &received_data;
            }
        } else if (received_data->type == FIN) {
            // Packet ack;
            // ack.packet_index = received_data.packet_index;
            // ack.size = sizeof(PACKET_TYPE);
            // ack.type = FIN;
            // sendto(s, &ack, sizeof(ack), 0, (sockaddr*)&si_other, (socklen_t)slen);
            // delete received_data;
            break;
        }
    }

    output.close();
    close(s);
	printf("%s received.", destinationFile);
    return;
}

/*
 * 
 */
int main(int argc, char** argv) {

    unsigned short int udpPort;

    if (argc != 3) {
        fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
        exit(1);
    }

    udpPort = (unsigned short int) atoi(argv[1]);

    reliablyReceive(udpPort, argv[2]);
}

