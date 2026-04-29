#ifndef FORMAT_HPP
#define FORMAT_HPP

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

constexpr int MAX_PAYLOAD = 1024;
constexpr int PORT = 8080;

enum MessageType : uint8_t {
    MSG_HELLO = 1,
    MSG_WELCOME = 2,
    MSG_TEXT = 3,
    MSG_PING = 4,
    MSG_PONG = 5,
    MSG_BYE = 6,
    MSG_AUTH = 7,        
    MSG_PRIVATE = 8,     
    MSG_ERROR = 9,       
    MSG_SERVER_INFO = 10
};

struct Message {
    uint32_t length;
    uint8_t type;
    char payload[MAX_PAYLOAD];

    Message() : length(0), type(0) {
        memset(payload, 0, MAX_PAYLOAD);
    }

    Message(uint8_t t, const std::string& data) : type(t) {
        size_t data_len = std::min(data.length(), (size_t)(MAX_PAYLOAD - 1));
        length = data_len + 1; 
        memcpy(payload, data.c_str(), data_len);
        payload[data_len] = '\0';
    }
};

class Socket {
private:
    int sockfd;
public:
    Socket() : sockfd(-1) {}
    ~Socket() { close(); }

    bool create() {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd == -1) return false;
        return true;
    }
    bool bind(int port) {
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        int opt = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        return ::bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == 0;
    }

    bool listen(int backlog = 10) {
        return ::listen(sockfd, backlog) == 0;
    }

    int accept() {
        return ::accept(sockfd, nullptr, nullptr);
    }

    bool connect(const std::string& ip, int port) {
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
        return ::connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == 0;
    }

    int get_fd() const { return sockfd; }

    void close() {
        if (sockfd != -1) {
            ::close(sockfd);
            sockfd = -1;
        }
    }

    Socket(Socket&& other) noexcept : sockfd(other.sockfd) { other.sockfd = -1; }
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) { close(); sockfd = other.sockfd; other.sockfd = -1; }
        return *this;
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
};

class MessageProtocol {
private:
    static bool recv_all(int sockfd, void* buf, size_t len) {
        size_t received = 0;
        while (received < len) {
            ssize_t n = ::recv(sockfd, (char*)buf + received, len - received, 0);
            if (n < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            if (n == 0) return false;
            received += n;
        }
        return true;
    }

    static bool send_all(int sockfd, const void* buf, size_t len) {
        size_t sent = 0;
        while (sent < len) {
            ssize_t n = ::send(sockfd, (const char*)buf + sent, len - sent, 0);
            if (n < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            if (n == 0) return false;
            sent += n;
        }
        return true;
    }

public:
    static bool send_message(int sockfd, const Message& msg) {
        uint32_t net_len = htonl(msg.length);
        if (!send_all(sockfd, &net_len, sizeof(net_len))) return false;
        if (!send_all(sockfd, &msg.type, 1)) return false;
        if (msg.length > 1) {
            if (!send_all(sockfd, msg.payload, msg.length - 1)) return false;
        }
        return true;
    }

    static bool recv_message(int sockfd, Message& msg) {
        if (!recv_all(sockfd, &msg.length, sizeof(msg.length))) return false;
        msg.length = ntohl(msg.length);
        if (msg.length > MAX_PAYLOAD + 1) return false;

        if (!recv_all(sockfd, &msg.type, 1)) return false;
        if (msg.length > 1) {
            if (!recv_all(sockfd, msg.payload, msg.length - 1)) return false;
            msg.payload[msg.length - 1] = '\0';
        } else {
            msg.payload[0] = '\0';
        }
        return true;
    }

    static std::string get_address(int sockfd) {
        struct sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        char ip[INET_ADDRSTRLEN];
        if (getpeername(sockfd, (struct sockaddr*)&addr, &len) == 0) {
            inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
            char result[64];
            snprintf(result, sizeof(result), "%s:%d", ip, ntohs(addr.sin_port));
            return std::string(result);
        }
        return "unknown";
    }
};

#endif
