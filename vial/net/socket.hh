#pragma once

#include <unistd.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <span>
#include "../core/task.hh"
#include "../core/io/io_awaitables.hh"
#include "../core/io/io_event_loop.hh"

namespace vial::net {

//! Socket wrapper providing Go-like blocking semantics with coroutines
class Socket {
  public:
    //! Default constructor - invalid socket
    Socket() = default;
    
    //! Construct Socket from existing file descriptor
    explicit Socket(int fd) : fd_(fd) {
        if (fd_ >= 0) {
            // Make socket non-blocking
            int flags = fcntl(fd_, F_GETFL, 0);
            fcntl(fd_, F_SETFL, flags | O_NONBLOCK); // NOLINT
            
            // Register with IOEventLoop
            IOEventLoop::instance().register_fd(fd_);
        }
    }
    
    //! Move constructor
    Socket(Socket&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }
    
    //! Move assignment
    auto operator=(Socket&& other) noexcept -> Socket& {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
    
    //! Disable copy
    Socket(const Socket&) = delete;
    auto operator=(const Socket&) -> Socket& = delete;
    
    //! Destructor closes socket
    ~Socket() { 
        if (fd_ >= 0) {
            IOEventLoop::instance().unregister_fd(fd_);
        }
        close();
    }
    
    //! Read data from socket - suspends if no data available
    [[nodiscard]] auto read(std::span<std::byte> buffer) const -> Task<ssize_t>;
    
    //! Write data to socket - suspends if write would block
    [[nodiscard]] auto write(std::span<const std::byte> data) const -> Task<ssize_t>;
    
    //! Accept incoming connection - suspends if no connections are pending
    [[nodiscard]] auto accept() const -> Task<Socket>;
    
    //! Check if socket is valid
    [[nodiscard]] auto is_valid() const noexcept -> bool {
        return fd_ >= 0;
    }
    
    //! Get underlying file descriptor
    [[nodiscard]] auto fd() const noexcept -> int {
        return fd_;
    }
    
  private:
    void close() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }
    
    int fd_ = -1;
};

//! Create a listening socket bound to host:port
auto listen(const char* host, int port) -> Socket;

} // namespace vial