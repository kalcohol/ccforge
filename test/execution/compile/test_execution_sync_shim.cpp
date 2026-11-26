#include <cpp26/execution/detail/sync_shim.hpp>

namespace sync_ops = std::execution::__forge_sync;

#if defined(__clang__) && !defined(FORGE_EXECUTION_SYNC_SHIM_NO_ANNOTATIONS)
static_assert(FORGE_EXECUTION_SYNC_ANNOTATIONS_ACTIVE == 1);
#else
static_assert(FORGE_EXECUTION_SYNC_ANNOTATIONS_ACTIVE == 0);
#endif

struct shared_state {
    sync_ops::mutex gate;
    sync_ops::condition_variable available;
    bool ready FORGE_EXECUTION_SYNC_GUARDED_BY(gate) = false;

    void publish() {
        {
            sync_ops::unique_lock lock{gate};
            make_ready();
        }
        available.notify_one();
    }

    void make_ready() FORGE_EXECUTION_SYNC_REQUIRES(gate) { ready = true; }

    void wait_for_publication(sync_ops::steady_clock::time_point deadline) {
        sync_ops::unique_lock lock{gate};
        while (!ready) {
            if (available.wait_until(lock, deadline) == std::cv_status::timeout) return;
        }
        available.wait(lock, [] { return true; });
        available.notify_all();
    }

    bool read() {
        // Exercise try-acquire without treating a spurious failure as a bad read.
        if (!gate.try_lock()) {
            sync_ops::unique_lock lock{gate};
            return ready;
        }
        bool observed = ready;
        gate.unlock();
        return observed;
    }

#if defined(FORGE_EXECUTION_SYNC_SHIM_NO_ANNOTATIONS)
    // Intentional compile-only control. The override must erase requirements
    // even when Clang checks this translation unit with -Werror=thread-safety.
    void no_annotation_control() { make_ready(); }
#endif
};

int main() {
    shared_state state;
    sync_ops::atomic<int> started{0};
    sync_ops::thread worker{[&] {
        started.fetch_add(1);
        state.publish();
    }};
    state.wait_for_publication(sync_ops::steady_clock::now() + std::chrono::seconds{1});
    worker.join();
    return started.load() == 1 && state.read() ? 0 : 1;
}
