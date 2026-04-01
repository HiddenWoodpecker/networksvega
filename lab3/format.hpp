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

constexpr int MAX_PAYLOAD = 1024;
constexpr int PORT = 8080;

enum MessageType : uint8_t {
    MSG_HELLO = 1,
    MSG_WELCOME = 2,
    MSG_TEXT = 3,
    MSG_PING = 4,
    MSG_PONG = 5,
    MSG_BYE = 6
};

struct Message {
    uint32_t length;
    uint8_t type;
    char payload[MAX_PAYLOAD];
    
    Message() : length(0), type(0) {
        memset(payload, 0, MAX_PAYLOAD);
    }
    
    Message(uint8_t t, const std::string& data) : type(t) {
        size_t data_len = std::min(data.length(), (size_t)MAX_PAYLOAD - 1);
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
    
    ~Socket() {
        if (sockfd != -1) {
            ::close(sockfd);  
        }
    }
    
    bool create() {
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        return sockfd != -1;
    }
    
    bool bind(int port) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
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
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
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
    
    Socket(Socket&& other) noexcept : sockfd(other.sockfd) {
        other.sockfd = -1;
    }
    
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            close();
            sockfd = other.sockfd;
            other.sockfd = -1;
        }
        return *this;
    }
    
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
};

class MessageProtocol {
public:
    static bool send_message(int sockfd, const Message& msg) {
        uint32_t net_len = htonl(msg.length);
        
        if (::send(sockfd, &net_len, sizeof(net_len), 0) != sizeof(net_len))
            return false;
        if (::send(sockfd, &msg.type, 1, 0) != 1)
            return false;
        if (msg.length > 1) {
            if (::send(sockfd, msg.payload, msg.length - 1, 0) != (ssize_t)(msg.length - 1))
                return false;
        }
        return true;
    }
    
    static bool recv_message(int sockfd, Message& msg) {
        if (::recv(sockfd, &msg.length, sizeof(msg.length), 0) != sizeof(msg.length))
            return false;
        
        msg.length = ntohl(msg.length);
        
        if (::recv(sockfd, &msg.type, 1, 0) != 1)
            return false;
        
        if (msg.length > 1) {
            if (::recv(sockfd, msg.payload, msg.length - 1, 0) != (ssize_t)(msg.length - 1))
                return false;
            msg.payload[msg.length - 1] = '\0';
        } else {
            msg.payload[0] = '\0';
        }
        
        return true;
    }
    
    static std::string get_address(int sockfd) {
        struct sockaddr_in addr;
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
