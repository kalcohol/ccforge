# P2300 senders/receivers under <execution>. <execution> always exists for
# C++17 parallel policies, so it is not a discriminator; probe for P2300 API.
check_cxx_source_compiles("
    #include <execution>
    #include <concepts>
    #include <exception>
    #include <optional>
    #include <stop_token>
    #include <tuple>
    #include <utility>

    #if !defined(__cpp_lib_senders) || __cpp_lib_senders < 202506L
    #error incomplete senders feature surface
    #endif

    struct callback_fn {
        void operator()() noexcept {}
    };

    struct probe_receiver {
        using receiver_concept = std::execution::receiver_tag;

        std::execution::env<> get_env() const noexcept { return {}; }
        void set_value(int) && noexcept {}
        void set_error(std::exception_ptr) && noexcept {}
        void set_stopped() && noexcept {}
    };

    struct probe_apply_tag {};

    struct probe_apply_domain {
        template<class Sender>
        int apply_sender(probe_apply_tag, Sender&&, int offset) const noexcept {
            return offset;
        }
    };

    int main(int argc, char**) {
        std::inplace_stop_source stop_source;
        auto stop_token = stop_source.get_token();
        std::inplace_stop_callback callback(stop_token, callback_fn{});
        static_assert(std::stoppable_token<decltype(stop_token)>);
        static_assert(std::unstoppable_token<std::never_stop_token>);
        using get_env_cpo = std::execution::get_env_t;
        const get_env_cpo* get_env_probe = &std::execution::get_env;
        (void)get_env_probe;
        static_assert(std::execution::receiver<probe_receiver>);
        static_assert(std::same_as<
            decltype(std::execution::get_env(
                std::declval<const probe_receiver&>())),
            std::execution::env<>>);

        std::execution::run_loop loop;
        auto scheduler = loop.get_scheduler();
        static_assert(std::execution::scheduler<decltype(scheduler)>);
        auto scheduled = std::execution::schedule(scheduler);
        static_assert(std::execution::sender_in<
            decltype(scheduled), std::execution::env<>>);
        static_assert(std::same_as<
            std::execution::completion_signatures_of_t<
                decltype(scheduled), std::execution::env<>>,
            std::execution::completion_signatures<
                std::execution::set_value_t()>>);
        using sender_marker = std::execution::sender_tag;
        using scheduler_marker = std::execution::scheduler_tag;
        using receiver_marker = std::execution::receiver_tag;
        using operation_marker = std::execution::operation_state_tag;
        sender_marker* sender_marker_probe = nullptr;
        scheduler_marker* scheduler_marker_probe = nullptr;
        receiver_marker* receiver_marker_probe = nullptr;
        operation_marker* operation_marker_probe = nullptr;
        (void)sender_marker_probe;
        (void)scheduler_marker_probe;
        (void)receiver_marker_probe;
        (void)operation_marker_probe;
        using await_adaptor_cpo =
            std::execution::get_await_completion_adaptor_t;
        const await_adaptor_cpo* await_adaptor_probe =
            &std::execution::get_await_completion_adaptor;
        (void)await_adaptor_probe;

        auto initial = std::execution::just(1);
        auto&& transformed = std::execution::transform_sender(
            initial, std::execution::env<>{});
        auto applied = std::execution::apply_sender(
            probe_apply_domain{}, probe_apply_tag{}, transformed, 3);
        if (applied != 3) {
            return 2;
        }
        static_assert(std::same_as<
            std::execution::completion_signatures_of_t<
                decltype(initial), std::execution::env<>>,
            std::execution::completion_signatures<
                std::execution::set_value_t(int)>>);
        auto operation = std::execution::connect(
            std::execution::just(0), probe_receiver{});
        static_assert(std::execution::operation_state<decltype(operation)>);
        std::execution::start(operation);
        auto pipeline = std::execution::when_all(
            std::execution::then(
                std::execution::starts_on(
                    scheduler, std::move(initial)),
                [](int value) { return value + 1; }),
            std::execution::continues_on(
                std::execution::just(2), scheduler));
        using signatures = std::execution::completion_signatures_of_t<
            decltype(pipeline), std::execution::env<>>;
        static_assert(std::execution::sender_in<
            decltype(pipeline), std::execution::env<>>);
        using result_type = decltype(
            std::this_thread::sync_wait(std::move(pipeline)));
        static_assert(std::same_as<
            result_type, std::optional<std::tuple<int, int>>>);

        if (argc == 0) {
            auto result = std::this_thread::sync_wait(std::move(pipeline));
            return result ? 0 : 1;
        }
        return 0;
    }
" FORGE_SENDERS_FULL)
check_cxx_source_compiles("
    #include <execution>
    #if !defined(__cpp_lib_senders)
    #error no senders feature macro
    #endif
    int main() { return 0; }
" FORGE_SENDERS_PARTIAL_MACRO)
check_cxx_source_compiles("
    #include <execution>
    int main() {
        auto s = std::execution::just(1);
        (void)s;
        return 0;
    }
" FORGE_SENDERS_PARTIAL_FACTORY)
check_cxx_source_compiles("
    #include <execution>
    using probe = std::execution::set_value_t;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_SENDERS_PARTIAL_SET_VALUE)
check_cxx_source_compiles("
    #include <execution>
    using probe = std::execution::set_error_t;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_SENDERS_PARTIAL_SET_ERROR)
check_cxx_source_compiles("
    #include <execution>
    using probe = std::execution::set_stopped_t;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_SENDERS_PARTIAL_SET_STOPPED)
check_cxx_source_compiles("
    #include <execution>
    using probe = std::execution::connect_t;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_SENDERS_PARTIAL_CONNECT)
check_cxx_source_compiles("
    #include <execution>
    using probe = std::execution::start_t;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_SENDERS_PARTIAL_START)
check_cxx_source_compiles("
    #include <execution>
    using probe = std::execution::get_env_t;
    int main() {
        const probe* p = &std::execution::get_env;
        (void)p;
        return 0;
    }
" FORGE_SENDERS_PARTIAL_GET_ENV)
check_cxx_source_compiles("
    #include <execution>
    using probe = std::execution::get_await_completion_adaptor_t;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_SENDERS_PARTIAL_AWAIT_ADAPTOR)
check_cxx_source_compiles("
    #include <execution>
    int main() {
        auto sender = std::execution::just(1);
        auto&& transformed = std::execution::transform_sender(
            sender, std::execution::env<>{});
        (void)transformed;
        return 0;
    }
" FORGE_SENDERS_PARTIAL_TRANSFORM_SENDER)
check_cxx_source_compiles("
    #include <execution>
    struct probe_tag {};
    struct probe_domain {
        template<class Sender>
        int apply_sender(probe_tag, Sender&&) const noexcept { return 0; }
    };
    int main() {
        auto sender = std::execution::just(1);
        return std::execution::apply_sender(
            probe_domain{}, probe_tag{}, sender);
    }
" FORGE_SENDERS_PARTIAL_APPLY_SENDER)
check_cxx_source_compiles("
    #include <execution>
    using probe = std::execution::sender_tag;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_SENDERS_PARTIAL_SENDER_MARKER)
check_cxx_source_compiles("
    #include <execution>
    using probe = std::execution::scheduler_tag;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_SENDERS_PARTIAL_SCHEDULER_MARKER)
check_cxx_source_compiles("
    #include <execution>
    using probe = std::execution::receiver_tag;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_SENDERS_PARTIAL_RECEIVER_MARKER)
check_cxx_source_compiles("
    #include <execution>
    using probe = std::execution::operation_state_tag;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_SENDERS_PARTIAL_OPERATION_MARKER)
set(FORGE_SENDERS_PARTIAL FALSE)
if(FORGE_SENDERS_PARTIAL_MACRO
        OR FORGE_SENDERS_PARTIAL_FACTORY
        OR FORGE_SENDERS_PARTIAL_SET_VALUE
        OR FORGE_SENDERS_PARTIAL_SET_ERROR
        OR FORGE_SENDERS_PARTIAL_SET_STOPPED
        OR FORGE_SENDERS_PARTIAL_CONNECT
        OR FORGE_SENDERS_PARTIAL_START
        OR FORGE_SENDERS_PARTIAL_GET_ENV
        OR FORGE_SENDERS_PARTIAL_AWAIT_ADAPTOR
        OR FORGE_SENDERS_PARTIAL_TRANSFORM_SENDER
        OR FORGE_SENDERS_PARTIAL_APPLY_SENDER
        OR FORGE_SENDERS_PARTIAL_SENDER_MARKER
        OR FORGE_SENDERS_PARTIAL_SCHEDULER_MARKER
        OR FORGE_SENDERS_PARTIAL_RECEIVER_MARKER
        OR FORGE_SENDERS_PARTIAL_OPERATION_MARKER)
    set(FORGE_SENDERS_PARTIAL TRUE)
endif()
_forge_decide("std::execution (P2300 senders/receivers)" SENDERS FORGE_SENDERS_FULL FORGE_SENDERS_PARTIAL)
