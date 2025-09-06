#include "core/task.hh"
#include "core/scheduler.hh"
#include "core/io/io_event_loop.hh"

#include <cassert>

namespace vial {
    Scheduler scheduler{}; //NOLINT
    std::thread io_thread; //NOLINT

    auto _graceful_shutdown() -> void {
        vial::scheduler.stop();
        vial::IOEventLoop::instance().stop();
        vial::io_thread.join();
    }

    auto shutdown_and_exit() -> void {
        _graceful_shutdown();
        std::exit(0);
    }

    extern auto async_main() -> Task<int>;

    auto _launch_async_main() -> Task<int> {
        auto result = co_await async_main();
        _graceful_shutdown();
        co_return result;
    }

    template <typename T>
    auto spawn(Task<T> task) -> Task<T> {
        return scheduler.spawn_task(task);
    }

    template <typename T>
    auto fire_and_forget(Task<T> task) -> void {
        scheduler.fire_and_forget(task);
    }
}

auto main () -> int {
    vial::io_thread = std::thread([]() {
        vial::IOEventLoop::instance().run();
    });

    vial::scheduler.fire_and_forget( vial::_launch_async_main() );
    vial::scheduler.start();
    return 1;
}