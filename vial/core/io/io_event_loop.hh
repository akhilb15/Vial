#pragma once

#include <coroutine>
#include <functional>
#include <unordered_set>
#include <unordered_map>
#include <sys/epoll.h>

namespace vial {

//! IOEventLoop manages IO events and resumes waiting coroutines when IO is ready.
class IOEventLoop { // NOLINT
  public:
    IOEventLoop();
    ~IOEventLoop();
    
    void register_fd(int fd);
    void unregister_fd(int fd);
    void register_read_callback(int fd, std::function<void()> callback);
    void register_write_callback(int fd, std::function<void()> callback);
    void run();
    void stop();
    
    // Singleton access (for now)
    static auto instance() -> IOEventLoop&;
    
  private:
    std::unordered_set<int> registered_fds_;
    
    std::unordered_map<int, std::function<void()>> read_callbacks_;
    std::unordered_map<int, std::function<void()>> write_callbacks_;
    
    int epoll_fd_ = -1;
    bool running_ = false;
};

} // namespace vial