//
//  main.cpp
//  MESH
//  Messaging Encrpytion for Secure Hosts
//
//  Created by Conor McDonald on 30/09/2024.
//
// step 1. include the libraries
//          include iostream, asio, threading (openssl and wxwidgets later)
// step 2. listen and connect function
//          be able to listen for connections and establish a connection, hardcode port: 1994, resolver for ip -> name
// step 3. introduction for CLI interface
//

#include <iostream>
#include <boost/asio.hpp>
#include <thread>

using boost::asio::ip::tcp;

//function to listen for connections
void listen_for_connections(boost::asio::io_context& io_context, tcp::socket& socket) {
    try {
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 1994)); //hardcoded port
        std::cout << "Listening for incoming connections on port 1994..." << std::endl;
        acceptor.accept(socket);
        std::cout << "Connection accepted from: " << socket.remote_endpoint().address().to_string() << std::endl;
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}

//function to initiate a connection to another peer
void connect_to_peer(boost::asio::io_context& io_context, tcp::socket& socket, const std::string& host, const std::string& port) {
    try {
        tcp::resolver resolver(io_context);
        tcp::resolver::results_type endpoints = resolver.resolve(host, port); // Hostname to IP address
        boost::asio::connect(socket, endpoints);
        std::cout << "Connected to peer at " << host << ":" << port << std::endl;
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}

int main() {
    try {
        boost::asio::io_context io_context;
        tcp::socket socket(io_context);

        // Start the listener in a separate thread so it runs concurrently
        std::thread accept_thread(listen_for_connections, std::ref(io_context), std::ref(socket));

        std::string connect;
        std::cout << "Welcome to M.E.S.H." << std::endl;
        std::cout << "Do you want to initiate a connection? (yes/no): ";
        std::cin >> connect;

        if (connect == "yes") {
            std::string host, port;
            std::cout << "Enter peer IP: ";
            std::cin >> host;
            std::cout << "Enter peer port: ";
            std::cin >> port;
            
            connect_to_peer(io_context, socket, host, port);
        }

        //wait for the accept thread to complete
        accept_thread.join();

        //both peers can now send/receive messages
        while (true) {
            std::string message;
            std::cout << "Enter message: ";
            std::cin.ignore();  //std::cin only prints firstword, use getline and ignore newline
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
