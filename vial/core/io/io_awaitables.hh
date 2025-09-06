#pragma once

#include <coroutine>
#include "io_event_loop.hh"
#include "../task.hh"
#include <poll.h>

namespace vial {

class IOAwaitable {
  public:
    IOAwaitable() = default;
    IOAwaitable(const IOAwaitable&) = delete;
    IOAwaitable(IOAwaitable&&) = delete;
    auto operator=(const IOAwaitable&) -> IOAwaitable& = delete;
    auto operator=(IOAwaitable&&) -> IOAwaitable& = delete;

    virtual ~IOAwaitable() = default;

    [[nodiscard]] virtual auto clone() const -> IOAwaitable* = 0;

    virtual void register_with_event_loop(std::function<void()> callback) = 0;
};

//! Awaitable that suspends until file descriptor is ready for reading
struct WaitForRead : IOAwaitable {
    int fd;
    
    explicit WaitForRead(int file_descriptor) : fd(file_descriptor) {}
    
    [[nodiscard]] auto await_ready()  noexcept -> bool {
        // if there is data to read, don't suspend
        struct pollfd pfd = {fd, POLLIN, 0};
        int ret = poll(&pfd, 1, 0);
        if (ret == -1) {
            std::cerr << "[WaitForRead] poll failed" << std::endl;
            return false;
        }
        return ret > 0;
    }
    
    template <typename S>
    void await_suspend(std::coroutine_handle<S> handle) noexcept { 
        handle.promise().set_state(TaskState::kBlockedOnIO);
        handle.promise().get_io_awaitable() = this->clone();
    }

    void await_resume() noexcept {}

    [[nodiscard]] auto clone() const -> IOAwaitable* override {
        return new WaitForRead(fd);
    }

    void register_with_event_loop(std::function<void()> callback) override {
        IOEventLoop::instance().register_read_callback(fd, callback);
    }
};

//! Awaitable that suspends until file descriptor is ready for writing
struct WaitForWrite : IOAwaitable {
    int fd;
    
    explicit WaitForWrite(int file_descriptor) : fd(file_descriptor) {}
    
    [[nodiscard]] auto await_ready()  noexcept -> bool {
        // if there is space to write, don't suspend
        struct pollfd pfd = {fd, POLLOUT, 0};
        int ret = poll(&pfd, 1, 0);
        if (ret == -1) {
            std::cerr << "[WaitForWrite] poll failed" << std::endl;
            return false;
        }
        return ret > 0;
    }
    
    template <typename S>
    void await_suspend(std::coroutine_handle<S> handle) noexcept {
        handle.promise().set_state(TaskState::kBlockedOnIO);
        handle.promise().get_io_awaitable() = this->clone();
    }
    
    void await_resume() noexcept {}

    [[nodiscard]] auto clone() const -> IOAwaitable* override {
        return new WaitForWrite(fd);
    }

    void register_with_event_loop(std::function<void()> callback) override {
        IOEventLoop::instance().register_write_callback(fd, callback);
    }
};

} // namespace vial