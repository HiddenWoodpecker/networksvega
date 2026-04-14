#include "format.hpp"
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <atomic>
#include <memory>
#include <signal.h>
#include <algorithm>
#include <cerrno>

class Client {
public:
    int socket;
    std::string address;
    std::string nickname;
    std::atomic<bool> active;
    Client(int sock, const std::string& addr, const std::string& name)
        : socket(sock), address(addr), nickname(name), active(true) {}
};

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

class ChatServer {
private:
    static constexpr int THREAD_POOL_SIZE = 10;
    static constexpr int MAX_CLIENTS = 100;
    std::vector<std::unique_ptr<Client>> clients_;
    std::mutex clients_mutex_;
    ThreadSafeQueue connection_queue_;
    std::atomic<bool> running_{true};
    std::vector<std::thread> workers_;
    std::thread acceptor_thread_;
    Socket server_socket_;

public:
    ChatServer() = default;
    ~ChatServer() { stop(); }

    bool start(int port) {
        if (!server_socket_.create()) { std::cerr << "Failed to create socket\n"; return false; }
        if (!server_socket_.bind(port)) { std::cerr << "Failed to bind socket\n"; return false; }
        if (!server_socket_.listen(MAX_CLIENTS)) { std::cerr << "Failed to listen\n"; return false; }

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
            client->active = false;
            ::shutdown(client->socket, SHUT_RDWR);
            ::close(client->socket);
        }
        clients_.clear();
        std::cout << "Server shutdown complete\n";
    }

private:
    void accept_connections() {
        while (running_) {
            int client_fd = server_socket_.accept();
            if (client_fd < 0) {
                if (!running_) break;
                if (errno == EINTR) continue;
                if (errno == EBADF || errno == EINVAL) break;
                std::cerr << "Accept failed: " << strerror(errno) << "\n";
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
            Message msg;
            
            if (!MessageProtocol::recv_message(client_fd, msg) || msg.type != MSG_HELLO) {
                std::cout << "Handshake failed for " << address << "\n";
                ::close(client_fd);
                continue;
            }

            std::string nickname(msg.payload);
            std::cout << "Client " << address << " connected as " << nickname << "\n";
            
            Message welcome(MSG_WELCOME, "Welcome to the chat, " + nickname + "!");
            MessageProtocol::send_message(client_fd, welcome);
            
            add_client(client_fd, address, nickname);
            handle_client_messages(client_fd, address);
            remove_client(client_fd);
            
            ::shutdown(client_fd, SHUT_RDWR);
            ::close(client_fd);
        }
    }

    void handle_client_messages(int client_fd, const std::string& address) {
        while (running_) {
            Message msg;
            if (!MessageProtocol::recv_message(client_fd, msg)) {
                std::cout << "Client " << address << " disconnected\n";
                break;
            }
            switch (msg.type) {
                case MSG_TEXT: {
                    // std::string nickname = get_client_nickname(client_fd);
                    std::string formatted = "[" + address + "]: " + msg.payload;
                    broadcast_message(formatted, MSG_TEXT, client_fd);
                    break;
                }
                case MSG_PING: {
                    Message pong(MSG_PONG, "PONG");
                    MessageProtocol::send_message(client_fd, pong);
                    break;
                }
                case MSG_BYE:
                    return;
                default: break;
            }
        }
    }

    void broadcast_message(const std::string& message, uint8_t type, int sender_socket) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto& client : clients_) {
            if (client->socket != sender_socket && client->active) {
                Message msg(type, message);
                if (!MessageProtocol::send_message(client->socket, msg)) {
                    client->active = false;
                }
            }
        }
    }

    void add_client(int socket, const std::string& address, const std::string& nickname) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.push_back(std::make_unique<Client>(socket, address, nickname));
        std::cout << "[+] Client " << nickname << " [" << address << "] online. Total: " << clients_.size() << "\n";
    }

    void remove_client(int socket) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = std::find_if(clients_.begin(), clients_.end(),
                               [socket](const auto& c) { return c->socket == socket; });
        if (it != clients_.end()) {
            std::cout << "[-] Client " << (*it)->nickname << " left.\n";
            clients_.erase(it);
        }
    }

    std::string get_client_nickname(int socket) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = std::find_if(clients_.begin(), clients_.end(),
                               [socket](const auto& c) { return c->socket == socket; });
        return (it != clients_.end()) ? (*it)->nickname : "unknown";
    }
};

std::unique_ptr<ChatServer> server;
std::atomic<bool> running(true);

void signal_handler(int) {
    std::cout << "\nSignal received. Shutting down...\n";
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
