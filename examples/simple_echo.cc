#include "vial/net/socket.hh"
#include "vial/async_main.hh"
#include <iostream>
#include <csignal>

using vial::Task;

auto handle_client(vial::net::Socket client) -> Task<int> { 
    constexpr size_t buffer_size = 1024;
    std::array<std::byte, buffer_size> buffer{};
    
    while (true) {
        auto bytes_read = co_await client.read(buffer);
        if (bytes_read <= 0) {
            std::cout << "[fd:" << client.fd() << "] Client disconnected" << std::endl;
            break;
        }

        std::cout << "[fd:" << client.fd() << "] Echoing " << bytes_read << " bytes" << std::endl;

        auto bytes_written = co_await client.write(std::span<const std::byte>(buffer.data(), bytes_read));
        if (bytes_written != bytes_read) {
            std::cerr << "[fd:" << client.fd() << "] Write failed" << std::endl;
            break;
        }
    }
    
    co_return 0;
}

auto echo_server(int port) -> Task<int> {
    auto listener = vial::net::listen("0.0.0.0", port);
    if (!listener.is_valid()) {
        std::cerr << "Failed to create listening socket" << std::endl;
        co_return -1;
    }
    
    std::cout << "[listener fd:" << listener.fd() << "] Server listening on port " << port << std::endl;
    
    while (true) {
        auto client = co_await listener.accept();
        if (!client.is_valid()) {
            std::cerr << "[listener fd:" << listener.fd() << "] Failed to accept connection" << std::endl;
            continue;
        }
        
        std::cout << "[fd:" << client.fd() << "] New client connected" << std::endl;
        vial::fire_and_forget( handle_client(std::move(client)) );
    }
    
    co_return 0;
}

auto vial::async_main() -> Task<int> {
    std::signal(SIGINT, [](int) {
        std::cout << "SIGINT received, exiting..." << std::endl;
        vial::shutdown_and_exit();
    });

    constexpr int port = 8080;
    co_await echo_server(port);
    co_return 0;
}