#pragma once

#include <coroutine>
#include <atomic>
#include <iostream>
#include <type_traits>

namespace vial {

// Forward declaration
class IOAwaitable;

enum TaskState : std::uint8_t {
  kAwaiting,
  kBlockedOnIO,
  kComplete
};

inline auto operator<<(std::ostream& os, TaskState state) -> std::ostream& {
    switch (state) {
        case kAwaiting: return os << "kAwaiting";
        case kBlockedOnIO: return os << "kBlockedOnIO";
        case kComplete: return os << "kComplete";
        default: return os << "Unknown(" << static_cast<int>(state) << ")";
    }
}

//! TaskBase is a type-erased base class for Task<T> used for callbacks. 
class TaskBase {
  public:
    TaskBase() = default;

    // Use clone instead. 
    TaskBase(TaskBase&) = delete;
    TaskBase(TaskBase&&) = delete;
    auto operator=(TaskBase&) -> TaskBase& = delete;
    auto operator=(TaskBase&&) -> TaskBase& = delete;

    //! Start/resume execution of the underlying coroutine.
    virtual auto run () -> TaskState = 0;

    //! Set the state of the task.
    virtual void set_state(TaskState state) = 0;

    //! Set whether the task should be deleted on completion.
    //! Should be set to true for fire and forget tasks.
    [[nodiscard]] virtual auto should_delete_on_completion() const -> bool = 0;
    virtual void delete_on_completion() = 0;

    //! Get a pointer to the awaiting coroutine.
    [[nodiscard]] virtual auto get_awaiting () const -> TaskBase* = 0;
    virtual auto clear_awaiting () -> void = 0;

    //! Get a pointer to the IOAwaitable currently suspended.
    [[nodiscard]] virtual auto get_io_awaitable () const -> IOAwaitable* = 0;
    virtual auto clear_io_awaitable () -> void = 0;

    //! Get an pointer to a clone of TaskBase (points to the same underlying coroutine)
    [[nodiscard]] virtual auto clone() const -> TaskBase* = 0;

    //! 
    [[nodiscard]] virtual auto is_enqueued () const -> bool = 0;
    virtual void set_enqueued_true () = 0;
    virtual void set_enqueued_false () = 0;

    [[nodiscard]] virtual auto get_state () const -> TaskState = 0;

    [[nodiscard]] virtual auto get_callback() const -> TaskBase* = 0;
    virtual void set_callback(TaskBase*) = 0;

    //! Destroys the underlying coroutine (this should happen on co_return).
    virtual void destroy() = 0;
    virtual void print_promise_addr() = 0;

    virtual ~TaskBase() = default;
};

//! Task<T> wraps a std::coroutine_handle to provide callback logic. 
template <typename T>
class Task : public TaskBase {
  public:
      //! Underlying heap allocated state of a coroutine.
    struct promise_type {
      public:
        using Handle = std::coroutine_handle<promise_type>;
        
        auto get_return_object() -> Task<T> { return Task{Handle::from_promise(*this)}; }

        //!
        auto initial_suspend() -> std::suspend_always { return {}; }

        //! Returning suspend_always means we must manually handle lifetimes
        auto final_suspend() noexcept -> std::suspend_always { return {}; } 

        //! On `co_return x` set state. 
        void return_value (T x) {
            result_ = std::move(x);
            state_ = kComplete;
        }

        //! Handler for unhandled exceptions. 
        void unhandled_exception() {}

        //! return reference to the task currently awaiting.
        auto get_awaiting() -> TaskBase*& { return awaiting_; }

        //! return reference to the IOAwaitable currently suspended.
        auto get_io_awaitable() -> IOAwaitable*& { return io_awaitable_; }
        
        void set_state(TaskState state) { state_ = state; }
        
        private:
          TaskState state_ = TaskState::kAwaiting;

          // Ownership of task currently awaiting. (Delete on resumption). 
          TaskBase* awaiting_ = nullptr;

          // IOAwaitable that is currently suspended (if state is kBlockedOnIO)
          IOAwaitable* io_awaitable_ = nullptr;

          // To be added back to queue on completion.
          TaskBase* callback_ = nullptr;

          //! Whether the task should be deleted on completion.
          //! Should be set to true for fire and forget tasks.
          std::atomic<bool> delete_on_completion_ = false;
          
          std::atomic<bool> enqueued_ = false;

          T result_{};
          
        friend Task<T>;
    };

    //! Is the (outside) task ready?
    [[nodiscard]] auto await_ready () const noexcept -> bool {
      return false;
    }

    //! Handler for co_await where `this` is the task begin awaited upon. 
    /*! Suspicious...
    */
    template <typename S>
    void await_suspend(std::coroutine_handle<S> awaitee) noexcept {
      // Propogated to workers to enqueue the awaited upon task. 
      auto& awaitee_awaiting = awaitee.promise().get_awaiting();   
      awaitee_awaiting = this->clone(); // `this` might be lost, but `this` might also be stack allocated.
    }

    //! Handler for returning a value 
    /*!
    Code Example:
      Task<T> foo ();
      ...

      co_await foo(); // the value here is the return value of await_resume();
    */
    auto await_resume() noexcept -> T {
      return std::move(handle_.promise().result_);
    }

    //! Construct a Task from a coroutine handle.
    explicit Task(const typename promise_type::Handle coroutine) : handle_{coroutine} {}

    //! Copy constructor.
    Task (const Task& other) : handle_{other.handle_} {}

    //! Move constructor.
    Task (const Task&& other)  noexcept : handle_{other.handle_} {} 

    //! Copy assignment operator.
    auto operator=(const Task& other) -> Task& {
      handle_ = other.handle_;
      return *this;
    }

    //! Move assignment operator.
    auto operator=(Task&& other) noexcept -> Task& {
      handle_ = other.handle_;
      return *this;
    }

    ~Task() override = default;

    //! Virtual clone. 
    [[nodiscard]] auto clone () const -> TaskBase* override {
      return new Task<T>{*this}; // NOLINT
    }

     auto run() -> TaskState override {
      handle_.resume();
      return { handle_.promise().state_ };
    }

    void set_state(TaskState state) override {
      handle_.promise().state_ = state;
    }

    void delete_on_completion() override {
      handle_.promise().delete_on_completion_.store(true, std::memory_order_acquire);
    }

    [[nodiscard]] auto should_delete_on_completion() const -> bool override {
      return handle_.promise().delete_on_completion_.load(std::memory_order_release);
    }

    [[nodiscard]] auto get_awaiting () const -> TaskBase* override {
      return handle_.promise().awaiting_;
    }

    auto clear_awaiting () -> void override {
      handle_.promise().awaiting_ = nullptr;
    }

    [[nodiscard]] auto get_io_awaitable () const -> IOAwaitable* override {
      return handle_.promise().io_awaitable_;
    }

    auto clear_io_awaitable () -> void override {
      handle_.promise().io_awaitable_ = nullptr;
    }

    [[nodiscard]] auto is_enqueued () const -> bool override {
      return this->handle_.promise().enqueued_.load(std::memory_order_release);
    }

     void set_enqueued_true () override {
      this->handle_.promise().enqueued_.store(true, std::memory_order_acquire);
    }

     void set_enqueued_false () override {
      this->handle_.promise().enqueued_.store(false, std::memory_order_acquire);
    }

    //!
    [[nodiscard]] auto get_state () const -> TaskState override {
      return this->handle_.promise().state_;
    }

    //!
    void set_callback (TaskBase* x) override {
      this->handle_.promise().callback_ = x;
    }

    //! 
    [[nodiscard]] auto get_callback () const -> TaskBase* override {
      return this->handle_.promise().callback_;
    }

    //!
    void print_promise_addr() override {
      std::cout << handle_.address() << std::endl;
    }

    //! 
    void destroy () override {
      handle_.destroy();
    }

  private:
    mutable typename promise_type::Handle handle_;
};

// Template specialization for Task<void>
template <>
class Task<void> : public TaskBase {
  public:
    struct promise_type {
      public:
        using Handle = std::coroutine_handle<promise_type>;
        
        auto get_return_object() -> Task<void> { return Task{Handle::from_promise(*this)}; }

        auto initial_suspend() -> std::suspend_always { return {}; } // NOLINT

        auto final_suspend() noexcept -> std::suspend_always { return {}; }  // NOLINT

        void return_void() {
            state_ = kComplete;
        }

        void unhandled_exception() {}

        auto get_awaiting() -> TaskBase*& { return awaiting_; }

        auto get_io_awaitable() -> IOAwaitable*& { return io_awaitable_; }
        
        void set_state(TaskState state) { state_ = state; }
        
        TaskState state_ = TaskState::kAwaiting;
        TaskBase* awaiting_ = nullptr;
        IOAwaitable* io_awaitable_ = nullptr;
        TaskBase* callback_ = nullptr;
        std::atomic<bool> delete_on_completion_ = false;
        std::atomic<bool> enqueued_ = false;
          
        friend Task<void>;
    };

    [[nodiscard]] auto await_ready () const noexcept -> bool { // NOLINT
      return false;
    }

    template <typename S>
    void await_suspend(std::coroutine_handle<S> awaitee) noexcept {
      auto& awaitee_awaiting = awaitee.promise().get_awaiting();   
      awaitee_awaiting = this->clone();
    }

    void await_resume() noexcept {
      // No value to return for void tasks
    }

    explicit Task(const typename promise_type::Handle coroutine) : handle_{coroutine} {}

    Task (const Task& other) : handle_{other.handle_} {}

    Task (const Task&& other)  noexcept : handle_{other.handle_} {} 

    auto operator=(const Task& other) -> Task& {
      handle_ = other.handle_;
      return *this;
    }

    auto operator=(Task&& other) noexcept -> Task& {
      handle_ = other.handle_;
      return *this;
    }

    ~Task() override = default;

    [[nodiscard]] auto clone () const -> TaskBase* override {
      return new Task<void>{*this};
    }

     auto run() -> TaskState override {
      handle_.resume();
      return handle_.promise().state_;
    }

    void set_state(TaskState state) override {
      handle_.promise().state_ = state;
    }

    void delete_on_completion() override {
      handle_.promise().delete_on_completion_.store(true, std::memory_order_acquire);
    }

    [[nodiscard]] auto should_delete_on_completion() const -> bool override {
      return handle_.promise().delete_on_completion_.load(std::memory_order_release);
    }

    [[nodiscard]] auto get_awaiting () const -> TaskBase* override {
      return handle_.promise().awaiting_;
    }

    auto clear_awaiting () -> void override {
      handle_.promise().awaiting_ = nullptr;
    }

    [[nodiscard]] auto get_io_awaitable () const -> IOAwaitable* override {
      return handle_.promise().io_awaitable_;
    }

    auto clear_io_awaitable () -> void override {
      handle_.promise().io_awaitable_ = nullptr;
    }

    [[nodiscard]] auto is_enqueued () const -> bool override {
      return this->handle_.promise().enqueued_.load(std::memory_order_release);
    }

     void set_enqueued_true () override {
      this->handle_.promise().enqueued_.store(true, std::memory_order_acquire);
    }

     void set_enqueued_false () override {
      this->handle_.promise().enqueued_.store(false, std::memory_order_acquire);
    }

    [[nodiscard]] auto get_state () const -> TaskState override {
      return this->handle_.promise().state_;
    }

    void set_callback (TaskBase* x) override {
      this->handle_.promise().callback_ = x;
    }

    [[nodiscard]] auto get_callback () const -> TaskBase* override {
      return this->handle_.promise().callback_;
    }

    void print_promise_addr() override {
      std::cout << handle_.address() << std::endl;
    }

    void destroy () override {
      handle_.destroy();
    }

  private:
    mutable typename promise_type::Handle handle_;
};

} // namespace vial