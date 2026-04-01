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

class Client {
public:
    int socket;
    std::string address;
    std::string nickname;
    bool active;
    
    Client(int sock, const std::string& addr, const std::string& name)
        : socket(sock), address(addr), nickname(name), active(true) {}
};

class ThreadSafeQueue {
private:
    std::queue<int> queue_;
    std::mutex mutex_;
    std::condition_variable cond_;
    
public:
    void push(int client_fd) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(client_fd);
        cond_.notify_one();
    }
    
    int pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return !queue_.empty(); });
        int client_fd = queue_.front();
        queue_.pop();
        return client_fd;
    }
};

class ChatServer {
private:
    static constexpr int THREAD_POOL_SIZE = 10;
    static constexpr int MAX_CLIENTS = 100;
    
    std::vector<std::unique_ptr<Client>> clients_;
    std::mutex clients_mutex_;
    ThreadSafeQueue connection_queue_;
    std::atomic<bool> running_;
    std::vector<std::thread> workers_;
    Socket server_socket_;
    
public:
    ChatServer() : running_(true) {}
    
    ~ChatServer() {
        stop();
    }
    
    bool start(int port) {
        if (!server_socket_.create()) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }
        
        if (!server_socket_.bind(port)) {
            std::cerr << "Failed to bind socket" << std::endl;
            return false;
        }
        
        if (!server_socket_.listen(MAX_CLIENTS)) {
            std::cerr << "Failed to listen" << std::endl;
            return false;
        }
        
        std::cout << "Server listening on port " << port << std::endl;
        std::cout << "Creating thread pool with " << THREAD_POOL_SIZE << " threads..." << std::endl;
        
        for (int i = 0; i < THREAD_POOL_SIZE; ++i) {
            workers_.emplace_back(&ChatServer::worker_thread, this);
        }
        
        std::cout << "Thread pool created. Waiting for connections..." << std::endl;
        
        accept_connections();
        
        return true;
    }
    
    void stop() {
        running_ = false;
        
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_.clear();
        }
        
        server_socket_.close();
        std::cout << "Server shutdown complete" << std::endl;
    }
    
private:
    void accept_connections() {
        while (running_) {
            int client_fd = server_socket_.accept();
            if (client_fd < 0) {
                if (running_) {
                    std::cerr << "Accept failed" << std::endl;
                }
                continue;
            }
            
            std::cout << "New connection accepted, adding to queue" << std::endl;
            connection_queue_.push(client_fd);
        }
    }
    
    void worker_thread() {
        while (running_) {
            int client_fd = connection_queue_.pop();
            
            if (!running_) break;
            
            std::string address = MessageProtocol::get_address(client_fd);
            
            Message msg;
            if (!MessageProtocol::recv_message(client_fd, msg)) {
                std::cout << "Handshake failed for " << address << std::endl;
                close(client_fd);
                continue;
            }
            
            if (msg.type != MSG_HELLO) {
                std::cout << "Handshake failed for " << address 
                          << ": expected MSG_HELLO" << std::endl;
                close(client_fd);
                continue;
            }
            
            std::string nickname(msg.payload);
            std::cout << "Client " << address << " connected as " << nickname << std::endl;
            
            std::string welcome_msg = "Welcome to the chat, " + nickname + "!";
            Message welcome(MSG_WELCOME, welcome_msg);
            MessageProtocol::send_message(client_fd, welcome);
            
            add_client(client_fd, address, nickname);
            
            handle_client_messages(client_fd, address);
            
            remove_client(client_fd);
            close(client_fd);
        }
    }
    
void handle_client_messages(int client_fd, const std::string& address) {
    while (true) {
        Message msg;
        if (!MessageProtocol::recv_message(client_fd, msg)) {
            std::cout << "Client " << address << " disconnected" << std::endl;
            break;
        }
        
        std::cout << "Received message type: " << (int)msg.type << " from " << address << std::endl;
        
        switch (msg.type) {
            case MSG_TEXT: {
                std::string nickname = get_client_nickname(client_fd);
                std::cout << "[" << nickname << "]: " << msg.payload << std::endl;
                
                std::string formatted = "[" + nickname + "]: " + msg.payload;
                broadcast_message(formatted, MSG_TEXT, client_fd);
                break;
            }
                
            case MSG_PING: {
                std::cout << "PING from " << address << ", sending PONG" << std::endl;
                Message pong(MSG_PONG, "PONG");
                MessageProtocol::send_message(client_fd, pong);
                break;
            }
                
            case MSG_BYE: {
                std::cout << "BYE from " << address << std::endl;
                Message bye(MSG_BYE, "BYE");
                MessageProtocol::send_message(client_fd, bye);
                return;
            }
                
            default:
                std::cout << "Unknown message type " << (int)msg.type 
                          << " from " << address << std::endl;
                break;
        }
    }
} 
    void broadcast_message(const std::string& message, uint8_t type, int sender_socket) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        
        for (auto& client : clients_) {
            if (client->socket != sender_socket && client->active) {
                Message msg(type, message);
                MessageProtocol::send_message(client->socket, msg);
            }
        }
    }
    
    void add_client(int socket, const std::string& address, const std::string& nickname) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        
        auto client = std::make_unique<Client>(socket, address, nickname);
        clients_.push_back(std::move(client));
        
        std::cout << "Client " << nickname << " [" << address 
                  << "] added to list" << std::endl;
    }
    
    void remove_client(int socket) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        
        auto it = std::find_if(clients_.begin(), clients_.end(),
            [socket](const auto& client) { return client->socket == socket; });
        
        if (it != clients_.end()) {
            std::cout << "Client " << (*it)->nickname << " removed from list" << std::endl;
            clients_.erase(it);
        }
    }
    
    std::string get_client_nickname(int socket) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        
        auto it = std::find_if(clients_.begin(), clients_.end(),
            [socket](const auto& client) { return client->socket == socket; });
        
        if (it != clients_.end()) {
            return (*it)->nickname;
        }
        
        return "unknown";
    }
};

std::unique_ptr<ChatServer> server;
std::atomic<bool> running(true);

void signal_handler(int) {
    std::cout << "\nShutting down server..." << std::endl;
    if (server) {
        server->stop();
    }
    running = false;
}

int main() {
    signal(SIGINT, signal_handler);
    
    server = std::make_unique<ChatServer>();
    
    if (!server->start(PORT)) {
        return 1;
    }
    
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return 0;
}
