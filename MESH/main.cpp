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

using boost::asio::ip::tcp;

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

// Main messaging functionality
int main() {
    try {
        boost::asio::io_context io_context;
        tcp::socket socket(io_context);

        unsigned short port;
        std::cout << "Welcome to M.E.S.H." << std::endl;
        std::cout << "Enter port to listen on: ";
        std::cin >> port;

        // Start the listener in a separate thread so it runs concurrently
        std::thread accept_thread(listen_for_connections, std::ref(io_context), std::ref(socket), port);

        std::string connect;
        std::cout << "Do you want to initiate a connection? (yes/no): ";
        std::cin >> connect;

        if (connect == "yes") {
            std::string host;
            std::cout << "Enter peer IP: ";
            std::cin >> host;
            
            connect_to_peer(io_context, socket, host, port);
        }

        // Wait for the accept thread to complete
        accept_thread.join();

        // Both peers can now send/receive messages
        while (true) {
            std::string message;
            std::cout << "Enter message: ";
            std::cin.ignore();  // Ignore the newline character left in the input buffer
            std::getline(std::cin, message);

            boost::asio::write(socket, boost::asio::buffer(message));

            char buffer[1024];
            boost::system::error_code error;
            size_t length = socket.read_some(boost::asio::buffer(buffer), error);

            if (!error) {
                std::cout << "Received: " << std::string(buffer, length) << std::endl;
            } else {
                std::cerr << "Error receiving message: " << error.message() << std::endl;
            }
        }
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    return 0;
}
