#include "scheduler.hh"
#include "task.hh"
#include "io/io_awaitables.hh"
#include <thread>
#include <cassert>
#include <set>
#include <iostream>

namespace vial {

Scheduler::Scheduler(unsigned int num_workers) : num_workers_(num_workers) {
    queues_ = std::vector<std::queue<TaskBase*>>(num_workers_);
}

auto Scheduler::push_task(TaskBase* task, size_t worker_id) -> void {
    task->set_enqueued_true();
    if (queues_[worker_id].size() > kMaxLocalTasks) {
        queues_[worker_id].push(task);
    } else {
        global_queue_.push(task);
    }
}

auto Scheduler::start () -> void {
    running_ = true;
    std::vector<std::thread> workers;

    for (size_t i = 0; i < num_workers_; i++) {
        workers.emplace_back(
            &Scheduler::run_worker, this, i
        );
    }

    for (auto& i : workers) { i.join(); }

    workers.clear();
}

auto Scheduler::stop () -> void {
    running_ = false;
}

void Scheduler::run_worker(size_t worker_id) {
    auto& local_queue = queues_[worker_id];
    while (running_) {
        std::optional<TaskBase*> task_opt = local_queue.empty() ? std::nullopt : std::optional(local_queue.front());
        
        if(task_opt != std::nullopt) { local_queue.pop(); }

        while (task_opt == std::nullopt && running_) { task_opt = global_queue_.try_get(); }

        if (task_opt == std::nullopt) { continue; }

        TaskBase* task = task_opt.value();
        TaskState state = task->get_state();

        if (state != kComplete) {
            TaskBase* task_to_delete = task->get_awaiting();
            IOAwaitable* io_awaitable_to_delete = task->get_io_awaitable();
            task->clear_awaiting();
            task->clear_io_awaitable();

            state = task->run();

            if (task_to_delete != nullptr) {
                task_to_delete->destroy();
            }

            delete task_to_delete;
            delete io_awaitable_to_delete;
        }

        switch (state) {
            case kAwaiting: {
                auto *awaiting = task->get_awaiting();
                awaiting->set_callback(task);

                // Awaiting wasn't spawned. 
                if (!awaiting->is_enqueued()) {
                    this->push_task(awaiting, worker_id);
                }
            } break;

            case kBlockedOnIO: {
                // if blocked on IO, register callback with event loop
                auto *io_awaitable = task->get_io_awaitable();
                io_awaitable->register_with_event_loop([task, this, worker_id]() {
                    task->set_state(kAwaiting);
                    push_task(task->clone(), worker_id);
                });
            } break;

            case kComplete: {
                if (task->get_callback() != nullptr) {
                    push_task(task->get_callback(), worker_id);
                    // delete task;
                } else if (task->should_delete_on_completion()) {
                    // task is fire and forget, and will not be co_awaited/have a callback
                    task->destroy();
                    delete task;
                } else {
                    push_task(task, worker_id);
                }
            } break;
        }
    }
}

};