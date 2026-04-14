#include "format.hpp"
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <atomic>
#include <signal.h>
#include <algorithm>
#include <cerrno>
#include <cstring>

typedef struct {
    int sock;
    char nickname[32];
    int authenticated;
} Client;

class ThreadSafeQueue {
private:
    std::queue<int> queue_;
    std::mutex mutex_;
    std::condition_variable cond_;
    bool shutdown_ = false;
public:
    void push(int client_fd) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(client_fd);
        cond_.notify_one();
    }
    int pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return !queue_.empty() || shutdown_; });
        if (queue_.empty()) return -1;
        int client_fd = queue_.front();
        queue_.pop();
        return client_fd;
    }
    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
        cond_.notify_all();
    }
};

void log_osi(const std::string& msg) {
    static std::mutex log_mtx;
    std::lock_guard<std::mutex> lock(log_mtx);
    std::cout << msg << std::endl;
}

void send_msg(int fd, uint8_t type, const std::string& data) {
    log_osi("[Layer 7 - Application] prepare response");
    log_osi("[Layer 6 - Presentation] serialize Message");
    log_osi("[Layer 4 - Transport] send()");
    MessageProtocol::send_message(fd, Message(type, data));
}

class ChatServer {
private:
    static constexpr int THREAD_POOL_SIZE = 10;
    std::vector<Client> clients_;
    std::mutex clients_mutex_;
    ThreadSafeQueue connection_queue_;
    std::atomic<bool> running_{true};
    std::vector<std::thread> workers_;
    std::thread acceptor_thread_;
    Socket server_socket_;

    void accept_connections() {
        while (running_) {
            int client_fd = server_socket_.accept();
            if (client_fd < 0) {
                if (!running_) break;
                if (errno == EINTR) continue;
                if (errno == EBADF || errno == EINVAL) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            connection_queue_.push(client_fd);
        }
    }

    void worker_thread() {
        while (running_) {
            int client_fd = connection_queue_.pop();
            if (!running_ || client_fd == -1) break;

            std::string address = MessageProtocol::get_address(client_fd);

            log_osi("[Layer 4 - Transport] recv()");
            log_osi("[Layer 6 - Presentation] deserialize Message");
            Message msg;
            if (!MessageProtocol::recv_message(client_fd, msg) || msg.type != MSG_AUTH) {
                log_osi("[Layer 5 - Session] auth failed (wrong msg type)");
                send_msg(client_fd, MSG_ERROR, "Send MSG_AUTH first");
                ::close(client_fd);
                continue;
            }

            std::string nick(msg.payload);
            if (nick.empty()) {
                send_msg(client_fd, MSG_ERROR, "Nickname cannot be empty");
                ::close(client_fd);
                continue;
            }

            bool taken = false;
            {
                std::lock_guard<std::mutex> lock(clients_mutex_);
                for (const auto& c : clients_) {
                    if (c.authenticated && strcmp(c.nickname, nick.c_str()) == 0) {
                        taken = true; break;
                    }
                }
            }
            if (taken) {
                send_msg(client_fd, MSG_ERROR, "Nickname already taken");
                ::close(client_fd);
                continue;
            }

            log_osi("[Layer 5 - Session] authentication success");
            Client new_cl;
            new_cl.sock = client_fd;
            strncpy(new_cl.nickname, nick.c_str(), 31);
            new_cl.nickname[31] = '\0';
            new_cl.authenticated = 1;

            {
                std::lock_guard<std::mutex> lock(clients_mutex_);
                clients_.push_back(new_cl);
            }
            std::cout << "User [" << nick << "] connected" << std::endl;
            send_msg(client_fd, MSG_SERVER_INFO, "Welcome " + nick);

            handle_client_messages(client_fd, address);
            remove_client(client_fd);
            ::close(client_fd);
        }
    }

    void handle_client_messages(int client_fd, const std::string&) {
        while (running_) {
            log_osi("[Layer 4 - Transport] recv()");
            log_osi("[Layer 6 - Presentation] deserialize Message");
            Message msg;
            if (!MessageProtocol::recv_message(client_fd, msg)) break;

            log_osi("[Layer 5 - Session] client authenticated");

            switch (msg.type) {
                case MSG_TEXT: {
                    log_osi("[Layer 7 - Application] handle MSG_TEXT");
                    std::string nick = get_client_nickname(client_fd);
                    std::string out = "[" + nick + "]: " + msg.payload;
                    broadcast_message(out, MSG_TEXT, client_fd);
                    break;
                }
                case MSG_PING: {
                    log_osi("[Layer 7 - Application] handle MSG_PING");
                    send_msg(client_fd, MSG_PONG, "PONG");
                    break;
                }
                case MSG_PRIVATE: {
                    log_osi("[Layer 7 - Application] handle MSG_PRIVATE");
                    std::string payload(msg.payload);
                    size_t pos = payload.find(':');
                    if (pos == std::string::npos) {
                        send_msg(client_fd, MSG_ERROR, "Format: target_nick:message");
                        break;
                    }
                    std::string target = payload.substr(0, pos);
                    std::string text = payload.substr(pos + 1);
                    std::string priv = "[PRIVATE][" + get_client_nickname(client_fd) + "]: " + text;

                    bool found = false;
                    int target_fd = -1;
                    {
                        std::lock_guard<std::mutex> lock(clients_mutex_);
                        for (const auto& c : clients_) {
                            if (c.authenticated && strcmp(c.nickname, target.c_str()) == 0) {
                                target_fd = c.sock; found = true; break;
                            }
                        }
                    }
                    if (found) send_msg(target_fd, MSG_PRIVATE, priv);
                    else send_msg(client_fd, MSG_ERROR, "User not found");
                    break;
                }
                case MSG_BYE: return;
                default: send_msg(client_fd, MSG_ERROR, "Unknown command");
            }
        }
    }

    void broadcast_message(const std::string& message, uint8_t type, int sender_socket) {
        log_osi("[Layer 7 - Application] broadcast message");
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (const auto& c : clients_) {
            if (c.sock != sender_socket && c.authenticated) {
                send_msg(c.sock, type, message);
            }
        }
    }

    void remove_client(int fd) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = std::find_if(clients_.begin(), clients_.end(),
            [fd](const Client& c){ return c.sock == fd; });
        if (it != clients_.end()) {
            std::cout << "User [" << it->nickname << "] disconnected" << std::endl;
            clients_.erase(it);
        }
    }

    std::string get_client_nickname(int fd) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = std::find_if(clients_.begin(), clients_.end(),
            [fd](const Client& c){ return c.sock == fd; });
        return (it != clients_.end()) ? std::string(it->nickname) : "unknown";
    }

public:
    ChatServer() = default;
    ~ChatServer() { stop(); }

    bool start(int port) {
        if (!server_socket_.create()) { std::cerr << "Failed to create socket\n"; return false; }
        if (!server_socket_.bind(port)) { std::cerr << "Failed to bind socket\n"; return false; }
        if (!server_socket_.listen(100)) { std::cerr << "Failed to listen\n"; return false; }
        std::cout << "Server listening on port " << port << "\n";
        std::cout << "Creating thread pool with " << THREAD_POOL_SIZE << " threads...\n";
        for (int i = 0; i < THREAD_POOL_SIZE; ++i) {
            workers_.emplace_back(&ChatServer::worker_thread, this);
        }
        std::cout << "Thread pool created. Waiting for connections...\n";
        acceptor_thread_ = std::thread(&ChatServer::accept_connections, this);
        return true;
    }

    void stop() {
        if (!running_.exchange(false)) return;
        std::cout << "\nShutting down server...\n";
        connection_queue_.shutdown();
        server_socket_.close();
        if (acceptor_thread_.joinable()) acceptor_thread_.join();
        for (auto& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto& client : clients_) {
            ::shutdown(client.sock, SHUT_RDWR);
            ::close(client.sock);
        }
        clients_.clear();
        std::cout << "Server shutdown complete\n";
    }
};

std::unique_ptr<ChatServer> server;
std::atomic<bool> running(true);
void signal_handler(int) {
    if (server) server->stop();
    running = false;
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    server = std::make_unique<ChatServer>();
    if (!server->start(PORT)) return 1;
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return 0;
}
