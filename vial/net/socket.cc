#include "socket.hh"
#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <cstring>

namespace vial::net {

auto Socket::read(std::span<std::byte> buffer) const -> Task<ssize_t> {
    co_await WaitForRead{fd_};
    co_return ::read(fd_, buffer.data(), buffer.size());
}

auto Socket::write(std::span<const std::byte> data) const -> Task<ssize_t> {
    co_await WaitForWrite{fd_};
    co_return ::write(fd_, data.data(), data.size());
}

auto Socket::accept() const -> Task<Socket> {
    co_await WaitForRead{fd_};
    co_return Socket{::accept(fd_, nullptr, nullptr)};
}

auto listen(const char* host, int port) -> Socket {
    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return Socket{-1};
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to set socket options: " << strerror(errno) << std::endl;
        ::close(server_fd);
        return Socket{-1};
    }
    
    // Bind to address
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (host == nullptr || strcmp(host, "0.0.0.0") == 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_aton(host, &addr.sin_addr) == 0) {
            std::cerr << "Invalid host address: " << host << std::endl;
            ::close(server_fd);
            return Socket{-1};
        }
    }
    
    if (bind(server_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) { // NOLINT
        std::cerr << "Failed to bind socket to " << host << ":" << port << " - " << strerror(errno) << std::endl;
        ::close(server_fd);
        return Socket{-1};
    }
    
    // Listen for connections
    const int backlog = 10;
    if (::listen(server_fd, backlog) < 0) {
        std::cerr << "Failed to listen on socket: " << strerror(errno) << std::endl;
        ::close(server_fd);
        return Socket{-1};
    }
    
    return Socket{server_fd};
}

} // namespace vial::net