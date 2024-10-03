//
//  main.cpp
//  MESH
//  Messaging Encryption for Secure Hosts
//
//  Created by Conor McDonald on 30/09/2024.
//
// step 1. include the libraries
//          include iostream, asio, threading (openssl and wxwidgets later)
// step 2. listen and connect function
//          be able to listen for connections and establish a connection, user-defined port
// step 3. introduction for CLI tool
//          input for IP and send message

#include <iostream>
#include <boost/asio.hpp>
#include <thread>
#include <atomic>
#include <string>
#include <limits>
#include <mutex>
#include <iomanip>  // Include for std::setw
#include <chrono>   // Include for timestamp functionality

using boost::asio::ip::tcp;

std::mutex cout_mutex;

const int USERNAME_WIDTH = 15; // Fixed width for username

// Function to get current timestamp
std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "[%Y-%m-%d %X] ");
    return ss.str();
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

// Function to handle receiving messages
void receive_messages(tcp::socket& socket, std::atomic<bool>& running, const std::string& username) {
    try {
        while (running) {
            boost::asio::streambuf buffer;
            boost::system::error_code error;
            size_t length = boost::asio::read_until(socket, buffer, '\n', error);

            if (!error) {
                std::string message(boost::asio::buffers_begin(buffer.data()), boost::asio::buffers_begin(buffer.data()) + length);
                buffer.consume(length);

                std::lock_guard<std::mutex> lock(cout_mutex);
                // Print the received message on a new line
                std::cout << "\n" << message;
                std::cout.flush();

                // Prompt for the next message with the receiver's username and timestamp
                std::cout << get_timestamp() << std::setw(USERNAME_WIDTH) << std::left << username << "Message: ";
                std::cout.flush();
            } else {
                std::cerr << "Error receiving message: " << error.message() << std::endl;
                running = false;
                break;
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

            if (message == "exit") {
                running = false;
                break;
            }

            // Trim leading and trailing whitespace
            message.erase(0, message.find_first_not_of(" \t\n\r\f\v"));
            message.erase(message.find_last_not_of(" \t\n\r\f\v") + 1);

            if (socket.is_open() && !message.empty()) {
                std::ostringstream oss;
                // Add the "Message: " prefix to the message before sending
                oss << get_timestamp() << std::setw(USERNAME_WIDTH) << std::left << username << "Message: " << message << "\n";
                std::string timestamped_message = oss.str();
                boost::asio::write(socket, boost::asio::buffer(timestamped_message));
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

// Main messaging functionality
int main() {
    try {
        boost::asio::io_context io_context;

        std::string username;
        std::cout << "Welcome to M.E.S.H." << std::endl;
        std::cout << "Enter your username: ";
        std::getline(std::cin, username);

        unsigned short listen_port;
        std::cout << "Enter port to listen on: ";
        std::cin >> listen_port;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');  // Clear input buffer

        // Create two sockets: one for listening, one for connecting
        tcp::socket listen_socket(io_context);
        tcp::socket connect_socket(io_context);

        // Start the listener in a separate thread
        std::thread accept_thread(listen_for_connections, std::ref(io_context), std::ref(listen_socket), listen_port);

        // Wait a moment to ensure the listening message is displayed
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        std::string connect;
        std::cout << "Do you want to initiate a connection? (yes/no): ";
        std::getline(std::cin, connect);

        std::atomic<bool> running(true);
        std::thread receive_thread;
        std::thread send_thread;

        if (connect == "yes") {
            std::string host;
            std::cout << "Enter peer IP: ";
            std::getline(std::cin, host);

            unsigned short connect_port;
            std::cout << "Enter peer port: ";
            std::cin >> connect_port;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');  // Clear input buffer

            connect_to_peer(io_context, connect_socket, host, connect_port);

            // Start send and receive threads for the connection we initiated
            receive_thread = std::thread(receive_messages, std::ref(connect_socket), std::ref(running), std::ref(username));
            send_thread = std::thread(send_messages, std::ref(connect_socket), std::ref(running), std::ref(username));
        } else {
            // Wait for the accept thread to complete
            accept_thread.join();

            // Start send and receive threads for the accepted connection
            receive_thread = std::thread(receive_messages, std::ref(listen_socket), std::ref(running), std::ref(username));
            send_thread = std::thread(send_messages, std::ref(listen_socket), std::ref(running), std::ref(username));
        }

        // Wait for the threads to complete
        if (receive_thread.joinable()) {
            receive_thread.join();
        }
        if (send_thread.joinable()) {
            send_thread.join();
        }

        // Clean up
        if (connect_socket.is_open()) {
            connect_socket.close();
        }
        if (listen_socket.is_open()) {
            listen_socket.close();
        }

    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    return 0;
}
