#include <iostream>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <thread>
#include <atomic>
#include <string>
#include <limits>
#include <mutex>
#include <iomanip>
#include <chrono>
#include <csignal>
#include <CoreFoundation/CoreFoundation.h>

using boost::asio::ip::tcp;
namespace ssl = boost::asio::ssl;

std::mutex cout_mutex;
std::atomic<bool> running(true);
ssl::stream<tcp::socket>* ssl_socket_ptr = nullptr;
boost::asio::io_context* io_context_ptr = nullptr;

const int USERNAME_WIDTH = 15;

// Function to get resource path for certificates
std::string get_resource_path(const std::string& filename) {
    std::string certs_directory = "certs/";
    return certs_directory + filename;
}

// Function to get timestamp
std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "[%Y-%m-%d %X] ");
    return ss.str();
}

// Signal handler
void signalHandler(int signum) {
    std::cout << "\nReceived interrupt signal (" << signum << "). Exiting chat..." << std::endl;
    running = false;
    if (io_context_ptr) {
        io_context_ptr->stop();
    }
}

// Function to get valid port
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

// Initialize SSL context
ssl::context init_ssl_context(bool is_server) {
    ssl::context ctx(is_server ? ssl::context::tlsv12_server : ssl::context::tlsv12_client);
    
    ctx.set_options(
        ssl::context::default_workarounds |
        ssl::context::no_sslv2 |
        ssl::context::no_sslv3 |
        ssl::context::single_dh_use
    );

    try {
        if (is_server) {
            ctx.use_certificate_chain_file(get_resource_path("server.crt"));
            ctx.use_private_key_file(get_resource_path("server.key"), ssl::context::pem);
            ctx.use_tmp_dh_file(get_resource_path("dh2048.pem"));
        } else {
            ctx.load_verify_file(get_resource_path("ca.crt"));
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading certificates: " << e.what() << std::endl;
        throw;
    }
    
    return ctx;
}

// Forward declarations for message handling functions
void receive_messages(ssl::stream<tcp::socket>& ssl_socket, std::atomic<bool>& running);
void send_messages(ssl::stream<tcp::socket>& ssl_socket, std::atomic<bool>& running);

// Function to listen for connections
void listen_for_connections(boost::asio::io_context& io_context, ssl::context& ssl_ctx, unsigned short port) {
    try {
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), port));
        std::cout << "Listening for incoming connections on port " << port << "..." << std::endl;

        ssl::stream<tcp::socket> ssl_socket(io_context, ssl_ctx);
        ssl_socket_ptr = &ssl_socket;

        acceptor.accept(ssl_socket.lowest_layer());
        std::cout << "Connection accepted from: " << ssl_socket.lowest_layer().remote_endpoint().address().to_string() << std::endl;

        ssl_socket.handshake(ssl::stream_base::server);
        std::cout << "SSL handshake completed successfully" << std::endl;

        std::thread receive_thread(receive_messages, std::ref(ssl_socket), std::ref(running));
        std::thread send_thread(send_messages, std::ref(ssl_socket), std::ref(running));

        receive_thread.join();
        send_thread.join();
    } catch (std::exception& e) {
        std::cerr << "Exception in listen_for_connections: " << e.what() << std::endl;
    }
}

// Function to connect to peer
void connect_to_peer(boost::asio::io_context& io_context, ssl::context& ssl_ctx,
                    const std::string& host, unsigned short port) {
    try {
        tcp::resolver resolver(io_context);
        tcp::resolver::results_type endpoints = resolver.resolve(host, std::to_string(port));

        ssl::stream<tcp::socket> ssl_socket(io_context, ssl_ctx);
        ssl_socket_ptr = &ssl_socket;

        boost::asio::connect(ssl_socket.lowest_layer(), endpoints);
        std::cout << "Connected to peer at " << host << ":" << port << std::endl;

        ssl_socket.handshake(ssl::stream_base::client);
        std::cout << "SSL handshake completed successfully" << std::endl;

        std::thread receive_thread(receive_messages, std::ref(ssl_socket), std::ref(running));
        std::thread send_thread(send_messages, std::ref(ssl_socket), std::ref(running));

        receive_thread.join();
        send_thread.join();
    } catch (std::exception& e) {
        std::cerr << "Exception in connect_to_peer: " << e.what() << std::endl;
    }
}

// Receive messages function
void receive_messages(ssl::stream<tcp::socket>& ssl_socket, std::atomic<bool>& running) {
    try {
        while (running) {
            boost::asio::streambuf buffer;
            boost::system::error_code error;
            size_t length = boost::asio::read_until(ssl_socket, buffer, '\n', error);

            if (error == boost::asio::error::eof) {
                std::cout << "Connection closed by peer." << std::endl;
                running = false;
                break;
            } else if (error) {
                throw boost::system::system_error(error);
            }

            std::string message(boost::asio::buffers_begin(buffer.data()),
                              boost::asio::buffers_begin(buffer.data()) + length);
            buffer.consume(length);

            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "\nReceived: " << message;
        }
    } catch (std::exception& e) {
        std::cerr << "Exception in receive_messages: " << e.what() << std::endl;
        running = false;
    }
}

// Send messages function
void send_messages(ssl::stream<tcp::socket>& ssl_socket, std::atomic<bool>& running) {
    try {
        while (running) {
            std::string message;
            std::getline(std::cin, message);
            message += '\n';

            if (message == "exit\n") {
                running = false;
                break;
            }

            boost::asio::write(ssl_socket, boost::asio::buffer(message));
        }
    } catch (std::exception& e) {
        std::cerr << "Exception in send_messages: " << e.what() << std::endl;
        running = false;
    }
}

int main() {
    try {
        // Register signal handler
        signal(SIGINT, signalHandler);

        boost::asio::io_context io_context;
        io_context_ptr = &io_context;

        std::cout << "Welcome to Secure MESH Chat!" << std::endl;
        std::cout << "Do you want to:\n1. Wait for a connection\n2. Connect to a peer" << std::endl;
        int choice;
        std::cin >> choice;
        std::cin.ignore();

        if (choice == 1) {
            ssl::context ssl_ctx = init_ssl_context(true);  // Server context
            unsigned short port = get_valid_port();
            listen_for_connections(io_context, ssl_ctx, port);
        } else if (choice == 2) {
            ssl::context ssl_ctx = init_ssl_context(false);  // Client context
            std::string host;
            std::cout << "Enter peer IP: ";
            std::getline(std::cin, host);
            unsigned short port = get_valid_port();
            connect_to_peer(io_context, ssl_ctx, host, port);
        }

        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    return 0;
}
