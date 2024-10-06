#include <iostream>
#include <boost/asio.hpp>
#include <thread>
#include <atomic>
#include <string>
#include <limits>
#include <mutex>
#include <iomanip>
#include <chrono>
#include <csignal>  // For signal handling

using boost::asio::ip::tcp;

std::mutex cout_mutex;
std::atomic<bool> running(true);  // Global atomic flag to control the running state
tcp::socket* socketPtr = nullptr; // Global pointer for socket to clean up on exit
boost::asio::io_context* io_context_ptr = nullptr; // Global pointer for io_context

const int USERNAME_WIDTH = 15;

// Function to get current timestamp
std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "[%Y-%m-%d %X] ");
    return ss.str();
}

// Signal handler for Ctrl + C
void signalHandler(int signum) {
    std::cout << "\nReceived interrupt signal (" << signum << "). Exiting chat..." << std::endl;
    running = false; // Set running to false to stop threads
    if (socketPtr && socketPtr->is_open()) {
        std::cout << "Closing connection..." << std::endl;
        boost::system::error_code ec;
        socketPtr->shutdown(tcp::socket::shutdown_both, ec);
        if (ec) {
            std::cerr << "Error shutting down connection: " << ec.message() << std::endl;
        }
        socketPtr->close(ec);
        if (ec) {
            std::cerr << "Error closing connection: " << ec.message() << std::endl;
        } else {
            std::cout << "Connection closed successfully." << std::endl;
        }
    }
    if (io_context_ptr) {
        io_context_ptr->stop(); // Stop the io_context
    }
    std::cout << "Cleaning up resources..." << std::endl;
}

// Function to listen for connections
void listen_for_connections(boost::asio::io_context& io_context, tcp::socket& socket, unsigned short port) {
    try {
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), port));
        std::cout << "Listening for incoming connections on port " << port << "..." << std::endl;
        acceptor.accept(socket);
        std::cout << "Connection accepted from: " << socket.remote_endpoint().address().to_string() << std::endl;
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}

// Function to initiate a connection to another peer
void connect_to_peer(boost::asio::io_context& io_context, tcp::socket& socket, const std::string& host, unsigned short port) {
    try {
        tcp::resolver resolver(io_context);
        tcp::resolver::results_type endpoints = resolver.resolve(host, std::to_string(port));
        boost::asio::connect(socket, endpoints);
        std::cout << "Connected to peer at " << host << ":" << port << std::endl;
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}

void receive_messages(tcp::socket& socket, std::atomic<bool>& running, const std::string& username) {
    try {
        while (running) {
            boost::asio::streambuf buffer;
            boost::system::error_code error;
            size_t length = boost::asio::read_until(socket, buffer, '\n', error);

            if (error == boost::asio::error::eof) {
                std::cout << "Connection closed by peer." << std::endl;
                running = false;
                break;
            } else if (error) {
                throw boost::system::system_error(error);
            }

            std::string message(boost::asio::buffers_begin(buffer.data()), boost::asio::buffers_begin(buffer.data()) + length);
            buffer.consume(length);

            message.erase(0, message.find_first_not_of(" \t\n\r\f\v"));
            message.erase(message.find_last_not_of(" \t\n\r\f\v") + 1);

            if (!message.empty()) {
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cout << std::endl;
                std::cout << message << std::endl;
                std::cout.flush();

                // Check if the message indicates that the chat has ended
                if (message.find("Chat ended") != std::string::npos) {
                    std::cout << "The other peer has ended the chat. Closing connection..." << std::endl;
                    running = false;
                    break;
                }
            }

            if (running) {
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cout << get_timestamp() << std::setw(USERNAME_WIDTH) << std::left << username << "Message: ";
                std::cout.flush();
            }
        }
    } catch (std::exception& e) {
        std::cerr << "Exception in receive_messages: " << e.what() << std::endl;
        running = false;
    }
}

void send_messages(tcp::socket& socket, std::atomic<bool>& running, const std::string& username) {
    try {
        while (running) {
            std::string message;
            {
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cout << get_timestamp() << std::setw(USERNAME_WIDTH) << std::left << username << "Message: ";
                std::cout.flush();
            }
            std::getline(std::cin, message);

            if (message == "exit" || !running) {
                running = false;
                // Send a special message indicating the chat has ended
                std::ostringstream oss;
                oss << get_timestamp() << std::setw(USERNAME_WIDTH) << std::left << username << "Message: " << "Chat ended\n";
                std::string end_message = oss.str();
                boost::system::error_code ec;
                boost::asio::write(socket, boost::asio::buffer(end_message), ec);
                if (ec) {
                    std::cerr << "Error sending end message: " << ec.message() << std::endl;
                }
                break; // Break out of the loop to close the connection
            }

            message.erase(0, message.find_first_not_of(" \t\n\r\f\v"));
            message.erase(message.find_last_not_of(" \t\n\r\f\v") + 1);

            if (socket.is_open() && !message.empty()) {
                std::ostringstream oss;
                oss << get_timestamp() << std::setw(USERNAME_WIDTH) << std::left << username << "Message: " << message << "\n";
                std::string timestamped_message = oss.str();
                boost::system::error_code ec;
                boost::asio::write(socket, boost::asio::buffer(timestamped_message), ec);
                if (ec) {
                    std::cerr << "Error sending message: " << ec.message() << std::endl;
                    running = false;
                    break;
                }
            } else if (!socket.is_open()) {
                std::cerr << "Connection is closed. Unable to send message." << std::endl;
                running = false;
                break;
            }
        }
    } catch (std::exception& e) {
        std::cerr << "Exception in send_messages: " << e.what() << std::endl;
        running = false;
    }
}

unsigned short get_valid_port() {
    unsigned short port;
    while (true) {
        std::cout << "Enter a port number (1024-65535): ";
        std::cin >> port;
        if (std::cin.fail() || port < 1024 || port > 65535) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "Invalid port number. Please try again." << std::endl;
        } else {
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            return port;
        }
    }
}

int main() {
    try {
        // Register the signal handler for SIGINT (Ctrl + C)
        signal(SIGINT, signalHandler);

        boost::asio::io_context io_context;
        io_context_ptr = &io_context; // Set the global pointer for io_context

        std::string username;
        std::cout << "Welcome to M.E.S.H., the secure peer-to-peer messaging app!" << std::endl;
        std::cout << "Enter your username: ";
        std::getline(std::cin, username);

        int choice;
        std::cout << "Do you want to:\n1. Wait for a connection from another peer (Listener)\n2. Connect to a peer (Initiator)" << std::endl;
        std::cout << "Enter your choice (1 or 2): ";
        std::cin >> choice;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        std::thread receive_thread;
        std::thread send_thread;

        tcp::socket socket(io_context);
        socketPtr = &socket; // Set the global pointer for socket

        if (choice == 1) {
            std::cout << "You've chosen to wait for a connection." << std::endl;
            unsigned short listen_port = get_valid_port();
            std::thread accept_thread(listen_for_connections, std::ref(io_context), std::ref(socket), listen_port);
            accept_thread.join();
        } else if (choice == 2) {
            std::string host;
            std::cout << "You've chosen to connect to a peer." << std::endl;
            std::cout << "Enter peer IP (or 'localhost' if testing locally): ";
            std::getline(std::cin, host);
            unsigned short connect_port = get_valid_port();
            connect_to_peer(io_context, socket, host, connect_port);
        } else {
            std::cerr << "Invalid choice, please restart the program." << std::endl;
            return 1;
        }

        receive_thread = std::thread(receive_messages, std::ref(socket), std::ref(running), std::ref(username));
        send_thread = std::thread(send_messages, std::ref(socket), std::ref(running), std::ref(username));

        io_context.run(); // Run the io_context in the main thread

        if (receive_thread.joinable()) {
            receive_thread.join();
        }
        if (send_thread.joinable()) {
            send_thread.join();
        }

        if (socket.is_open()) {
            boost::system::error_code ec;
            socket.shutdown(tcp::socket::shutdown_both, ec);
            if (ec) {
                std::cerr << "Error shutting down socket: " << ec.message() << std::endl;
            }
            socket.close(ec);
            if (ec) {
                std::cerr << "Error closing socket: " << ec.message() << std::endl;
            }
        }

        std::cout << "Chat session ended. Goodbye!" << std::endl;

    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    return 0;
}
