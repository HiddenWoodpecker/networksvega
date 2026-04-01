#include "format.hpp"
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

class ChatClient {
private:
    Socket socket_;
    std::atomic<bool> connected_;
    std::atomic<bool> running_;
    std::thread receive_thread_;
    std::mutex socket_mutex_;
    std::string nickname_;
    
public:
    ChatClient() : connected_(false), running_(true) {}
    
    ~ChatClient() {
        stop();
    }
    
    bool connect(const std::string& server_ip, int port) {
        if (!socket_.create()) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }
        
        if (!socket_.connect(server_ip, port)) {
            std::cerr << "Failed to connect to server" << std::endl;
            return false;
        }
        
        nickname_ = "client";
        Message hello(MSG_HELLO, nickname_);
        if (!MessageProtocol::send_message(socket_.get_fd(), hello)) {
            std::cerr << "Failed to send HELLO" << std::endl;
            return false;
        }
        
        Message welcome;
        if (!MessageProtocol::recv_message(socket_.get_fd(), welcome)) {
            std::cerr << "Failed to receive WELCOME" << std::endl;
            return false;
        }
        
        if (welcome.type != MSG_WELCOME) {
            std::cerr << "Expected WELCOME, got " << (int)welcome.type << std::endl;
            return false;
        }
        
        std::cout << welcome.payload << std::endl;
        connected_ = true;
        
        receive_thread_ = std::thread(&ChatClient::receive_thread_func, this);
        
        return true;
    }
    
    void run() {
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "Connected! Commands:" << std::endl;
        std::cout << "  /quit - disconnect" << std::endl;
        std::cout << "  /ping - ping server" << std::endl;
        std::cout << "  any text - broadcast to all clients" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "> " << std::flush;
        
        std::string input_buffer;
        
        while (running_) {
            if (!connected_) {
                std::cout << "\n[!] Connection lost. Reconnecting in 2 seconds..." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(2));
                
                if (reconnect()) {
                    std::cout << "[✓] Reconnected successfully!" << std::endl;
                    std::cout << "> " << std::flush;
                }
                continue;
            }
            
            char ch;
            ssize_t n = read(STDIN_FILENO, &ch, 1);
            
            if (n > 0) {
                if (ch == '\n') {
                    if (!input_buffer.empty()) {
                        if (input_buffer == "/quit") {
                            send_bye();
                            running_ = false;
                            break;
                        }
                        else if (input_buffer == "/ping") {
                            send_ping();
                            std::cout << "[→] PING sent" << std::endl;
                        }
                        else if (!input_buffer.empty()) {
                            send_message(input_buffer);
                        }
                        input_buffer.clear();
                    }
                    std::cout << "> " << std::flush;
                    if (!input_buffer.empty()) {
                        input_buffer.pop_back();
                        std::cout << "\b \b" << std::flush;
                    }
                    input_buffer += ch;
                    std::cout << ch << std::flush;
                }
            } else if (n == -1 && errno != EAGAIN) {
                break;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        fcntl(STDIN_FILENO, F_SETFL, flags);
        std::cout << std::endl;
    }
    
    void stop() {
        running_ = false;
        
        if (connected_) {
            send_bye();
        }
        
        if (receive_thread_.joinable()) {
            receive_thread_.join();
        }
        
        socket_.close();
    }
    
private:
    void receive_thread_func() {
        while (running_) {
            if (connected_) {
                Message msg;
                
                {
                    std::lock_guard<std::mutex> lock(socket_mutex_);
                    if (!MessageProtocol::recv_message(socket_.get_fd(), msg)) {
                        if (connected_) {
                            std::cout << "\n[!] Connection to server lost" << std::endl;
                            connected_ = false;
                        }
                        continue;
                    }
                }
                
                if (msg.type == MSG_TEXT) {
                    
                    std::cout << "\r\033[K";  
                    std::cout << msg.payload << std::endl;
                    std::cout << "> " << std::flush;
                } else if (msg.type == MSG_PONG) {
                    std::cout << "\r\033[K";  
                    std::cout << "[←] PONG received" << std::endl;
                    std::cout << "> " << std::flush;
                } else if (msg.type == MSG_WELCOME) {
                    std::cout << "\r\033[K";  
                    std::cout << "[←] " << msg.payload << std::endl;
                    std::cout << "> " << std::flush;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    bool reconnect() {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        
        socket_.close();
        
        if (!socket_.create()) {
            return false;
        }
        
        if (!socket_.connect("127.0.0.1", PORT)) {
            return false;
        }
        
        
        Message hello(MSG_HELLO, nickname_);
        if (!MessageProtocol::send_message(socket_.get_fd(), hello)) {
            return false;
        }
        
        Message welcome;
        if (!MessageProtocol::recv_message(socket_.get_fd(), welcome)) {
            return false;
        }
        
        if (welcome.type != MSG_WELCOME) {
            return false;
        }
        
        connected_ = true;
        return true;
    }
    
    void send_message(const std::string& text) {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        if (connected_) {
            Message msg(MSG_TEXT, text);
            if (MessageProtocol::send_message(socket_.get_fd(), msg)) {
            } else {
                std::cerr << "\n[!] Failed to send message" << std::endl;
                connected_ = false;
            }
        }
    }
    
    void send_ping() {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        if (connected_) {
            Message msg(MSG_PING, "");
            if (!MessageProtocol::send_message(socket_.get_fd(), msg)) {
                std::cerr << "\n[!] Failed to send PING" << std::endl;
                connected_ = false;
            }
        }
    }
    
    void send_bye() {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        if (connected_) {
            Message msg(MSG_BYE, "");
            MessageProtocol::send_message(socket_.get_fd(), msg);
            connected_ = false;
        }
    }
};

std::unique_ptr<ChatClient> client;
std::atomic<bool> running(true);

void signal_handler(int) {
    std::cout << "\n\nExiting..." << std::endl;
    if (client) {
        client->stop();
    }
    running = false;
}

int main() {
    signal(SIGINT, signal_handler);
    
    client = std::make_unique<ChatClient>();
    
    std::cout << "========================================" << std::endl;
    std::cout << "Chat Client v1.0" << std::endl;
    std::cout << "========================================" << std::endl;
    
    while (running) {
        std::cout << "Connecting to server..." << std::endl;
        if (client->connect("127.0.0.1", PORT)) {
            client->run();
            break;
        } else {
            std::cout << "Connection failed. Retrying in 2 seconds..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }
    
    std::cout << "Client shutdown complete" << std::endl;
    return 0;
}
