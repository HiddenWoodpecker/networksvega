#include "format.hpp"
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <cerrno>

class ChatClient {
private:
    Socket socket_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{true};
    std::thread receive_thread_;
    std::mutex cout_mutex_;
    std::string nickname_{"client"};
    struct termios old_tio_;
    bool terminal_configured_ = false;

public:
    ChatClient() = default;
    ~ChatClient() { stop(); }
    void setNickname(const std::string& nickname) { nickname_ = nickname; }

    bool connect(const std::string& server_ip, int port) {
        if (socket_.get_fd() != -1) socket_.close();
        connected_ = false;
        if (!socket_.create()) return false;
        if (!socket_.connect(server_ip, port)) {
            socket_.close(); return false;
        }

        Message auth(MSG_AUTH, nickname_);
        if (!MessageProtocol::send_message(socket_.get_fd(), auth)) {
            socket_.close(); return false;
        }

        Message resp;
        if (!MessageProtocol::recv_message(socket_.get_fd(), resp)) {
            socket_.close(); return false;
        }
        if (resp.type == MSG_ERROR) {
            std::cout << "[SERVER] ERROR: " << resp.payload << "\n";
            socket_.close(); return false;
        }

        connected_ = true;
        if (!receive_thread_.joinable()) {
            receive_thread_ = std::thread(&ChatClient::receive_thread_func, this);
        }
        return true;
    }

    void run() {
        struct termios new_tio;
        tcgetattr(STDIN_FILENO, &old_tio_);
        new_tio = old_tio_;
        new_tio.c_lflag &= ~(ICANON | ECHO);
        new_tio.c_cc[VMIN]  = 1;
        new_tio.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
        terminal_configured_ = true;

        {
            std::lock_guard<std::mutex> lock(cout_mutex_);
            std::cout << "========================================\n";
            std::cout << "Connected! Commands:\n";
            std::cout << "  /quit - disconnect\n";
            std::cout << "  /ping - ping server\n";
            std::cout << "  /w <nick> <message> - private message\n";
            std::cout << "========================================\n> " << std::flush;
        }

        std::string input_buffer;
        char ch;
        while (running_) {
            if (!connected_) {
                std::lock_guard<std::mutex> lock(cout_mutex_);
                std::cout << "\r\033[K[!] Connection lost. Reconnecting in 2s...\n> " << std::flush;
                std::this_thread::sleep_for(std::chrono::seconds(2));
                if (connect("127.0.0.1", PORT)) {
                    std::cout << "Reconnected!\n> " << std::flush;
                }
                continue;
            }
            ssize_t n = read(STDIN_FILENO, &ch, 1);
            if (n > 0) {
                std::lock_guard<std::mutex> lock(cout_mutex_);
                if (ch == '\n' || ch == '\r') {
                    std::cout << "\r\033[K";
                    if (!input_buffer.empty()) {
                        if (input_buffer == "/quit") { send_bye(); running_ = false; break; }
                        else if (input_buffer == "/ping") { send_ping(); std::cout << "[→] PING sent\n"; }
                        else if (input_buffer.substr(0, 3) == "/w ") {
                            size_t sp = input_buffer.find(' ', 3);
                            if (sp == std::string::npos) std::cout << "Usage: /w <nick> <message>\n";
                            else {
                                std::string target = input_buffer.substr(3, sp - 3);
                                std::string text = input_buffer.substr(sp + 1);
                                Message priv(MSG_PRIVATE, target + ":" + text);
                                MessageProtocol::send_message(socket_.get_fd(), priv);
                            }
                        }
                        else { send_message(input_buffer); }
                        input_buffer.clear();
                    }
                    std::cout << "> " << std::flush;
                } else if (ch == 127 || ch == 8) {
                    if (!input_buffer.empty()) { input_buffer.pop_back(); std::cout << "\b \b" << std::flush; }
                } else if (ch >= 32) {
                    input_buffer += ch; std::cout << ch << std::flush;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        restore_terminal();
    }

    void stop() {
        running_ = false;
        if (connected_) send_bye();
        if (receive_thread_.joinable()) receive_thread_.join();
        restore_terminal();
    }

private:
    void restore_terminal() {
        if (terminal_configured_) {
            tcsetattr(STDIN_FILENO, TCSANOW, &old_tio_);
            terminal_configured_ = false;
        }
    }

    void receive_thread_func() {
        while (running_) {
            if (!connected_) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }
            Message msg;
            bool ok = MessageProtocol::recv_message(socket_.get_fd(), msg);
            if (!ok) {
                if (connected_) {
                    connected_ = false; socket_.close();
                    std::lock_guard<std::mutex> lock(cout_mutex_);
                    std::cout << "\r\033[Server disconnected\n> " << std::flush;
                }
                continue;
            }
            std::lock_guard<std::mutex> lock(cout_mutex_);
            std::cout << "\r\033[K";
            if (msg.type == MSG_TEXT || msg.type == MSG_SERVER_INFO || msg.type == MSG_PRIVATE) {
                std::cout << msg.payload << "\n";
            } else if (msg.type == MSG_PONG) {
                std::cout << " PONG received\n";
            } else if (msg.type == MSG_ERROR) {
                std::cout << "[SERVER] ERROR: " << msg.payload << "\n";
            }
            std::cout << "> " << std::flush;
        }
    }

    void send_message(const std::string& text) {
        if (!connected_) return;
        if (!MessageProtocol::send_message(socket_.get_fd(), Message(MSG_TEXT, text))) {
            connected_ = false; socket_.close();
            std::lock_guard<std::mutex> lock(cout_mutex_);
            std::cout << "\r\033[Send failed\n> " << std::flush;
        }
    }
    void send_ping() {
        if (!connected_) return;
        if (!MessageProtocol::send_message(socket_.get_fd(), Message(MSG_PING, ""))) {
            connected_ = false; socket_.close();
        }
    }
    void send_bye() {
        if (!connected_) return;
        MessageProtocol::send_message(socket_.get_fd(), Message(MSG_BYE, ""));
        socket_.close(); connected_ = false;
    }
};

std::unique_ptr<ChatClient> client;
std::atomic<bool> running(true);
void signal_handler(int) { if (client) client->stop(); running = false; }

int main() {
    signal(SIGINT, signal_handler); signal(SIGTERM, signal_handler);

    std::string nick;
    std::cout << "Enter your nickname: ";
    std::getline(std::cin, nick);
    if (nick.empty()) { std::cerr << "Nickname required\n"; return 1; }

    client = std::make_unique<ChatClient>();
    client->setNickname(nick);

    while (running) {
        std::cout << "Connecting to server...\n";
        if (client->connect("127.0.0.1", PORT)) { client->run(); break; }
        std::cout << "Connection failed. Retrying in 2s...\n";
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    std::cout << "Client shutdown complete\n";
    return 0;
}
