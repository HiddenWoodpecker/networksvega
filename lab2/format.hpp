
#ifndef FORMAT
#define FORMAT
#include <stdint.h>
#include <fcntl.h>
#include <iostream>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdint.h>
#include <netinet/in.h>

#define MAX_PAYLOAD 1024
#define PORT 8080

typedef struct
{
uint32_t length;
uint8_t type;
char payload[MAX_PAYLOAD];
} Message;

enum MessageType
{ 
MSG_HELLO = 1,
MSG_WELCOME = 2,
MSG_TEXT = 3,
MSG_PING= 4,
MSG_PONG = 5,
MSG_BYE = 6
} ;


int recv_message(int sock, uint32_t *length, uint8_t *type, char *payload) {
    if (recv(sock, length, sizeof(*length), 0) <= 0) return -1;
    *length = ntohl(*length);
    if (recv(sock, type, 1, 0) <= 0) return -1;
    if (*length > 1) {
        if (recv(sock, payload, *length - 1, 0) <= 0) return -1;
        payload[*length - 1] = '\0';
    } else {
        payload[0] = '\0';
    }
    
    return 0;
}

void send_message(int sock, uint32_t length, uint8_t type, const char *payload) {
    uint32_t net_len = htonl(length);
    send(sock, &net_len, sizeof(net_len), 0);
    send(sock, &type, 1, 0);
    if (length > 1) {
        send(sock, payload, length - 1, 0);
    }
}

void format_addr(const struct sockaddr_in *addr, char *out, size_t out_size) {
	char ip[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
	snprintf(out, out_size, "%s:%d", ip, ntohs(addr->sin_port));
}
#endif
