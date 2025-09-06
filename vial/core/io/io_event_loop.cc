#include "io_event_loop.hh"
#include <iostream>
#include <unistd.h>
#include <array>
#include <utility>

namespace vial {

IOEventLoop::IOEventLoop() : epoll_fd_(epoll_create1(EPOLL_CLOEXEC)) {
    if (epoll_fd_ == -1) {
        std::cout << "[IOEventLoop] Failed to create epoll fd" << std::endl;
        // TODO: Better error handling
    } else {
        std::cout << "[IOEventLoop] Created epoll fd " << epoll_fd_ << std::endl;
    }
}

IOEventLoop::~IOEventLoop() {
    running_ = false;
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
        std::cout << "[IOEventLoop] Closed epoll fd" << std::endl;
    }
}

auto IOEventLoop::instance() -> IOEventLoop& {
    static IOEventLoop instance_;
    return instance_;
}

void IOEventLoop::register_fd(int fd) {
    if (registered_fds_.contains(fd)) {
        std::cout << "[IOEventLoop] fd " << fd << " already registered" << std::endl;
        return;
    }
    
    // Add fd to epoll for both read and write events (level-triggered)
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLOUT;  // Level-triggered read/write
    ev.data.fd = fd;
    
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
        std::cout << "[IOEventLoop] Failed to add fd " << fd << " to epoll" << std::endl;
        return;
    }
    
    registered_fds_.insert(fd);
}

void IOEventLoop::unregister_fd(int fd) {
    if (!registered_fds_.contains(fd)) {
        std::cout << "[IOEventLoop] fd " << fd << " not registered" << std::endl;
        return;
    }
    
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    registered_fds_.erase(fd);
}

void IOEventLoop::register_read_callback(int fd, std::function<void()> callback) {
    if (read_callbacks_.contains(fd)) {
        // TODO: Do this better, queue read waiters somehow maybe?
        std::cout << "[IOEventLoop] WARNING - fd " << fd << " already has a read waiter!" << std::endl;
        return;
    }
    
    read_callbacks_[fd] = std::move(callback);
}

void IOEventLoop::register_write_callback(int fd, std::function<void()> callback) {
    if (write_callbacks_.contains(fd)) {
        // TODO: Do this better, queue write waiters somehow maybe?
        std::cout << "[IOEventLoop] WARNING - fd " << fd << " already has a write waiter!" << std::endl;
        return;
    }
    
    write_callbacks_[fd] = std::move(callback);
}

void IOEventLoop::run() {
    running_ = true;
    
    constexpr int max_events = 64;
    std::array<epoll_event, max_events> events{};
    
    while (running_) {
        constexpr int timeout_ms = 50;
        int num_events = epoll_wait(epoll_fd_, events.data(), max_events, timeout_ms);
        
        if (num_events == -1) {
            std::cout << "[IOEventLoop] epoll_wait failed" << std::endl;
            break;
        }
        
        if (num_events == 0) {
            // Timeout - continue loop to check running_ flag
            continue;
        }
         
        for (int i = 0; i < num_events; i++) {
            int fd = events.at(i).data.fd;
            uint32_t event_flags = events.at(i).events;
             
            // Handle read events
            if ((event_flags & EPOLLIN) != 0) {
                if (auto it = read_callbacks_.find(fd); it != read_callbacks_.end()) {
                    // execute callback
                    auto callback = it->second;
                    read_callbacks_.erase(it);
                    callback();
                }
            }
            
            // Handle write events
            if ((event_flags & EPOLLOUT) != 0) {
                if (auto it = write_callbacks_.find(fd); it != write_callbacks_.end()) { 
                    // execute callback
                    auto callback = it->second;
                    write_callbacks_.erase(it);
                    callback();
                }
            }
        }
    }
    
    std::cout << "[IOEventLoop] Event loop stopped" << std::endl;
}

void IOEventLoop::stop() {
    std::cout << "[IOEventLoop] Stop requested" << std::endl;
    running_ = false;
}

} // namespace vial