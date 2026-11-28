// Held-out qualification controls for std::execution::task (pilot J-01).
//
// Each test carries the number of the held-out case it converts (A01 to A24).
// The fixtures are written from the case text and the draft wording alone and
// share nothing with the other task tests. The compile-time halves of A04,
// A05, A23 and A24 are configure probes (compile/test_execution_task_heldout_probes.cpp).

#include <execution>

#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace ex = std::execution;

// ---------------------------------------------------------------------------
// Shared fixtures

using events = std::vector<std::string>;

class trace {
public:
    void add(std::string event) {
        std::lock_guard lock{mutex_};
        events_.push_back(std::move(event));
    }

    events all() const {
        std::lock_guard lock{mutex_};
        return events_;
    }

    std::size_t count(const std::string& event) const {
        std::lock_guard lock{mutex_};
        std::size_t matches = 0;
        for (const auto& entry : events_) matches += entry == event ? 1 : 0;
        return matches;
    }

private:
    mutable std::mutex mutex_;
    events events_;
};

// A by-value coroutine parameter. The copy that lives in the frame is the
// only one still armed when it dies, so "D" marks the frame's destruction.
struct frame_marker {
    explicit frame_marker(trace& log, bool* dead = nullptr) : log(&log), dead(dead) {}
    frame_marker(frame_marker&& other) noexcept : log(std::exchange(other.log, nullptr)), dead(other.dead) {}
    ~frame_marker() {
        if (log) {
            if (dead) *dead = true;
            log->add("D");
        }
    }

    trace* log;
    bool* dead;
};

// A heap operation, so that a receiver can destroy it inside its completion.
struct operation_box {
    void* operation = nullptr;
    void (*destroy)(void*) noexcept = nullptr;
    int destroyed = 0;

    template<class Sender, class Receiver>
    auto& emplace(Sender&& sender, Receiver&& receiver) {
        using operation_t = ex::connect_result_t<Sender, Receiver>;
        auto* created = new operation_t(ex::connect(std::forward<Sender>(sender), std::forward<Receiver>(receiver)));
        operation = created;
        destroy = [](void* target) noexcept { delete static_cast<operation_t*>(target); };
        return *created;
    }

    void reset() noexcept {
        if (auto* target = std::exchange(operation, nullptr)) {
            destroy(target);
            ++destroyed;
        }
    }

    ~operation_box() { reset(); }
};

// A receiver that hands every completion to a sink and answers get_env with
// a copy of the environment it was given.
template<class Sink, class Env = ex::env<>>
struct sink_receiver {
    using receiver_concept = ex::receiver_tag;

    Sink* sink;
    Env environment{};

    template<class... Values>
    void set_value(Values&&... values) && noexcept {
        Sink* const target = sink;
        target->value(std::forward<Values>(values)...);
    }

    template<class Error>
    void set_error(Error&& error) && noexcept {
        Sink* const target = sink;
        target->error(std::forward<Error>(error));
    }

    void set_stopped() && noexcept {
        Sink* const target = sink;
        target->stopped();
    }

    Env get_env() const noexcept { return environment; }
};

// A sender that parks its operation in a gate until the test releases it.
struct gate {
    void* operation = nullptr;
    void (*resume)(void*) noexcept = nullptr;

    bool parked() const noexcept { return operation != nullptr; }

    void release() noexcept {
        auto* const target = std::exchange(operation, nullptr);
        auto* const entry = std::exchange(resume, nullptr);
        entry(target);
    }
};

template<class Receiver>
struct park_operation {
    using operation_state_concept = ex::operation_state_tag;

    gate* where;
    Receiver receiver;

    park_operation(gate* where, Receiver receiver) : where(where), receiver(std::move(receiver)) {}
    park_operation(park_operation&&) = delete;

    void start() & noexcept {
        where->operation = this;
        where->resume = [](void* self) noexcept {
            ex::set_value(std::move(static_cast<park_operation*>(self)->receiver));
        };
    }
};

struct park_sender {
    using sender_concept = ex::sender_tag;

    gate* where;

    template<class Self, class... Env>
    static consteval auto get_completion_signatures() -> ex::completion_signatures<ex::set_value_t()> {
        return {};
    }

    template<ex::receiver Receiver>
    park_operation<Receiver> connect(Receiver receiver) const {
        return park_operation<Receiver>(where, std::move(receiver));
    }
};

struct inline_config {
    using start_scheduler_type = ex::inline_scheduler;
};

// Membership and counts in an unordered set of completion signatures.
template<class Signatures, class Wanted>
inline constexpr bool lists_signature = false;

template<class... Listed, class Wanted>
inline constexpr bool lists_signature<ex::completion_signatures<Listed...>, Wanted> =
    (std::same_as<Listed, Wanted> || ...);

template<class Signature>
inline constexpr bool is_value_signature = false;

template<class... Arguments>
inline constexpr bool is_value_signature<ex::set_value_t(Arguments...)> = true;

template<class Signatures>
inline constexpr std::size_t value_signature_count = 0;

template<class... Listed>
inline constexpr std::size_t value_signature_count<ex::completion_signatures<Listed...>> =
    (std::size_t{0} + ... + (is_value_signature<Listed> ? 1u : 0u));

template<class Task>
using signatures_of = ex::completion_signatures_of_t<Task, ex::env<>>;

// ---------------------------------------------------------------------------
// 1 · Typed error and conversion order (A01 to A06)

struct typed_error {
    int cookie;
};

struct other_error {
    int cookie;
};

struct stage_error {
    int code;
};

struct conversion_error {
    int code;
};

// The bookkeeping of a yielded payload, kept outside the payload.
struct payload_book {
    explicit payload_book(trace& log) : log(&log) {}

    trace* log;
    // Compilers differ in whether a prvalue co_yield operand is materialized
    // with a move, so the coroutines yield a named operand as an xvalue.
    // That costs exactly one fixture move on every compiler, from the named
    // operand into yield_value's by-value parameter. It is counted apart, so
    // that "M" and `moves` are the library's own staging moves alone.
    enum class phase { building, operand_named, staging };
    phase stage = phase::building;
    int fixture_moves = 0;
    int moves = 0;
    int conversions = 0;
    int throwing_move = 0;        // 1 or 2 selects the staging move that throws
    bool conversion_throws = false;
    bool frame_dead = false;
    bool frame_dead_at_conversion = false;
    bool frame_dead_at_throwing_move = false;
    bool converted_moved_from = false;
    int cookie_at_conversion = 0;
};

template<class Target>
struct payload {
    payload(payload_book& book, int cookie) : book(&book), cookie(cookie) {}

    payload(payload&& other) : book(other.book), cookie(std::exchange(other.cookie, 0)),
                               live(std::exchange(other.live, false)) {
        if (book->stage == payload_book::phase::building) return;
        if (book->stage == payload_book::phase::operand_named) {
            // The named operand moves into the parameter of yield_value.
            book->stage = payload_book::phase::staging;
            ++book->fixture_moves;
            return;
        }
        ++book->moves;
        book->log->add("M");
        if (book->throwing_move == book->moves) {
            book->frame_dead_at_throwing_move = book->frame_dead;
            throw stage_error{30 + book->moves};
        }
    }

    operator Target() && {
        ++book->conversions;
        book->log->add("C");
        book->frame_dead_at_conversion = book->frame_dead;
        book->converted_moved_from = book->converted_moved_from || !live;
        book->cookie_at_conversion = cookie;
        if (book->conversion_throws) throw conversion_error{23};
        return Target{cookie};
    }

    payload_book* book;
    int cookie;
    bool live = true;
};

template<class... Errors>
struct error_config {
    using start_scheduler_type = ex::inline_scheduler;
    using error_types = ex::completion_signatures<ex::set_error_t(Errors)...>;
};

// Entered with the exact argument expression type the task hands to
// set_error. "R" and the counters are recorded here, at the receiver
// boundary, before anything could convert the argument: only the advertised
// type as an rvalue counts, and any other argument is an unexpected entry.
struct error_sink {
    explicit error_sink(trace& log) : log(&log) {}

    template<class... Values>
    void value(Values&&...) {
        log->add("V");
        ++value_entries;
    }

    template<class Error>
    void error(Error&& failure) {
        log->add("R");
        ++error_entries;
        if constexpr (std::same_as<Error&&, typed_error&&>) {
            ++typed_entries;
            typed_cookie = failure.cookie;
        } else if constexpr (std::same_as<Error&&, other_error&&>) {
            ++other_entries;
            other_cookie = failure.cookie;
        } else if constexpr (std::same_as<Error&&, std::exception_ptr&&>) {
            ++exception_entries;
            handler_active_at_entry = static_cast<bool>(std::current_exception());
            saved = std::move(failure);
        } else {
            ++unexpected_entries;
        }
        if (doomed) doomed->reset();
    }

    void stopped() {
        log->add("S");
        ++stopped_entries;
    }

    // Exactly one completion, on the error channel.
    int other_channels() const { return value_entries + stopped_entries + unexpected_entries; }

    trace* log;
    operation_box* doomed = nullptr;
    int value_entries = 0;
    int stopped_entries = 0;
    int error_entries = 0;
    int unexpected_entries = 0;
    int typed_entries = 0;
    int typed_cookie = 0;
    int other_entries = 0;
    int other_cookie = 0;
    int exception_entries = 0;
    bool handler_active_at_entry = false;
    std::exception_ptr saved;
};

using typed_and_exception = error_config<typed_error, std::exception_ptr>;

ex::task<void, typed_and_exception> yields_typed(frame_marker, payload_book& book, int cookie, int& after_yield) {
    ex::with_error<payload<typed_error>> named{payload<typed_error>{book, cookie}};
    book.stage = payload_book::phase::operand_named;
    co_yield std::move(named);
    ++after_yield;
}

ex::task<void, error_config<typed_error>> yields_typed_only(frame_marker, payload_book& book, int cookie,
                                                            int& after_yield) {
    ex::with_error<payload<typed_error>> named{payload<typed_error>{book, cookie}};
    book.stage = payload_book::phase::operand_named;
    co_yield std::move(named);
    ++after_yield;
}

ex::task<void, error_config<typed_error, other_error>> yields_other(frame_marker, payload_book& book, int cookie,
                                                                    int& after_yield) {
    ex::with_error<payload<other_error>> named{payload<other_error>{book, cookie}};
    book.stage = payload_book::phase::operand_named;
    co_yield std::move(named);
    ++after_yield;
}

// TM-03.2: every listed error is advertised, next to the one value signature.
static_assert(lists_signature<signatures_of<ex::task<void, typed_and_exception>>, ex::set_error_t(typed_error)>);
static_assert(lists_signature<signatures_of<ex::task<void, typed_and_exception>>,
                              ex::set_error_t(std::exception_ptr)>);
static_assert(lists_signature<signatures_of<ex::task<void, typed_and_exception>>, ex::set_value_t()>);
static_assert(!lists_signature<signatures_of<ex::task<void, error_config<typed_error>>>,
                               ex::set_error_t(std::exception_ptr)>);

TEST(ExecutionTaskHeldout, A01_TypedErrorIsStagedTwiceThenConvertedAfterTheFrame) {
    trace log;
    payload_book book{log};
    int after_yield = 0;
    error_sink sink{log};
    operation_box box;
    sink.doomed = &box;

    auto& operation = box.emplace(yields_typed(frame_marker{log, &book.frame_dead}, book, 17, after_yield),
                                  sink_receiver<error_sink>{&sink});
    ex::start(operation);

    EXPECT_EQ(log.all(), (events{"M", "M", "D", "C", "R"}));
    EXPECT_EQ(book.fixture_moves, 1);
    EXPECT_EQ(book.moves, 2);
    EXPECT_EQ(book.cookie_at_conversion, 17);
    EXPECT_TRUE(book.frame_dead_at_conversion);
    EXPECT_FALSE(book.converted_moved_from);
    EXPECT_EQ(book.conversions, 1);
    EXPECT_EQ(sink.error_entries, 1);
    EXPECT_EQ(sink.typed_entries, 1);
    EXPECT_EQ(sink.typed_cookie, 17);
    EXPECT_EQ(sink.other_channels(), 0);
    EXPECT_EQ(box.destroyed, 1);
    EXPECT_EQ(after_yield, 0);
}

TEST(ExecutionTaskHeldout, A02_ThrowingConversionReachesTheExceptionChannelOutsideItsHandler) {
    trace log;
    payload_book book{log};
    book.conversion_throws = true;
    int after_yield = 0;
    error_sink sink{log};
    operation_box box;

    auto& operation = box.emplace(yields_typed(frame_marker{log, &book.frame_dead}, book, 17, after_yield),
                                  sink_receiver<error_sink>{&sink});
    ex::start(operation);

    EXPECT_EQ(log.all(), (events{"M", "M", "D", "C", "R"}));
    EXPECT_EQ(book.fixture_moves, 1);
    EXPECT_EQ(book.moves, 2);
    EXPECT_TRUE(book.frame_dead_at_conversion);
    // The throwing conversion never hands its cookie to the receiver, so
    // the conversion's own record shows what it was given.
    EXPECT_EQ(book.conversions, 1);
    EXPECT_EQ(book.cookie_at_conversion, 17);
    EXPECT_FALSE(book.converted_moved_from);
    EXPECT_EQ(sink.error_entries, 1);
    EXPECT_EQ(sink.exception_entries, 1);
    EXPECT_FALSE(sink.handler_active_at_entry);
    EXPECT_EQ(sink.other_channels(), 0);
    EXPECT_EQ(after_yield, 0);
    ASSERT_TRUE(sink.saved);
    int code = 0;
    try {
        std::rethrow_exception(sink.saved);
    } catch (const conversion_error& failure) {
        code = failure.code;
    } catch (...) {
        code = -1;
    }
    EXPECT_EQ(code, 23);

    // The completed operation owns no frame any more.
    box.reset();
    EXPECT_EQ(log.count("D"), 1u);
    EXPECT_EQ(log.count("R"), 1u);
}

TEST(ExecutionTaskHeldout, A03_TypedOnlyListDeliversAnOrdinaryConversion) {
    trace log;
    payload_book book{log};
    int after_yield = 0;
    error_sink sink{log};
    operation_box box;

    auto& operation = box.emplace(yields_typed_only(frame_marker{log, &book.frame_dead}, book, 29, after_yield),
                                  sink_receiver<error_sink>{&sink});
    ex::start(operation);

    EXPECT_EQ(log.all(), (events{"M", "M", "D", "C", "R"}));
    EXPECT_EQ(book.fixture_moves, 1);
    EXPECT_EQ(book.moves, 2);
    EXPECT_EQ(sink.error_entries, 1);
    EXPECT_EQ(sink.typed_entries, 1);
    EXPECT_EQ(sink.typed_cookie, 29);
    EXPECT_EQ(sink.other_channels(), 0);
    EXPECT_EQ(after_yield, 0);
}

#if GTEST_HAS_DEATH_TEST
trace* death_log = nullptr;

struct exiting_sink {
    template<class... Values>
    void value(Values&&...) { std::_Exit(91); }
    template<class Error>
    void error(Error&&) { std::_Exit(92); }
    void stopped() { std::_Exit(93); }
};

[[noreturn]] void terminate_on_typed_only_conversion_failure() {
    static trace log;
    static payload_book book{log};
    static int after_yield = 0;
    static exiting_sink sink;
    book.conversion_throws = true;
    death_log = &log;
    std::set_terminate([] {
        // The trace up to the terminate marker, with no receiver entry: both
        // staging moves, the frame, one conversion. 86 is the expected exit.
        // 90 is termination at another point. The sink exits 91 to 93 if the
        // receiver is entered, and a normal return exits 94.
        std::_Exit(death_log->all() == events{"M", "M", "D", "C"} ? 86 : 90);
    });
    auto operation = ex::connect(yields_typed_only(frame_marker{log}, book, 17, after_yield),
                                 sink_receiver<exiting_sink>{&sink});
    ex::start(operation);
    std::_Exit(94);
}

TEST(ExecutionTaskHeldoutDeathTest, A03_ThrowingConversionWithoutExceptionChannelTerminates) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_EXIT(terminate_on_typed_only_conversion_failure(), ::testing::ExitedWithCode(86), "");
}
#endif

// A04 and A05, the accepted siblings at run time. The rejected forms are
// configure probes.
TEST(ExecutionTaskHeldout, A05_SoleImplicitTargetIsDeliveredOnce) {
    {
        trace log;
        payload_book book{log};
        int after_yield = 0;
        error_sink sink{log};
        operation_box box;
        auto& operation = box.emplace(yields_typed(frame_marker{log}, book, 41, after_yield),
                                      sink_receiver<error_sink>{&sink});
        ex::start(operation);
        EXPECT_EQ(sink.error_entries, 1);
        EXPECT_EQ(sink.typed_entries, 1);
        EXPECT_EQ(sink.typed_cookie, 41);
        EXPECT_EQ(sink.other_channels(), 0);
        EXPECT_EQ(book.conversions, 1);
        EXPECT_EQ(log.all(), (events{"M", "M", "D", "C", "R"}));
    }
    {
        // Two listed types, one reachable: the second one is selected.
        trace log;
        payload_book book{log};
        int after_yield = 0;
        error_sink sink{log};
        operation_box box;
        auto& operation = box.emplace(yields_other(frame_marker{log}, book, 43, after_yield),
                                      sink_receiver<error_sink>{&sink});
        ex::start(operation);
        EXPECT_EQ(sink.error_entries, 1);
        EXPECT_EQ(sink.other_entries, 1);
        EXPECT_EQ(sink.other_cookie, 43);
        EXPECT_EQ(sink.other_channels(), 0);
        EXPECT_EQ(book.conversions, 1);
        EXPECT_EQ(log.all(), (events{"M", "M", "D", "C", "R"}));
    }
}

void run_throwing_staging_move(int throwing_move, const events& expected, int expected_code) {
    trace log;
    payload_book book{log};
    book.throwing_move = throwing_move;
    int after_yield = 0;
    error_sink sink{log};
    operation_box box;

    auto& operation = box.emplace(yields_typed(frame_marker{log, &book.frame_dead}, book, 17, after_yield),
                                  sink_receiver<error_sink>{&sink});
    ex::start(operation);

    EXPECT_EQ(log.all(), expected);
    EXPECT_EQ(book.fixture_moves, 1);
    // The move threw while the frame was alive.
    EXPECT_FALSE(book.frame_dead_at_throwing_move);
    EXPECT_TRUE(book.frame_dead);
    EXPECT_EQ(book.conversions, 0);
    EXPECT_EQ(sink.error_entries, 1);
    EXPECT_EQ(sink.exception_entries, 1);
    EXPECT_EQ(sink.other_channels(), 0);
    EXPECT_EQ(after_yield, 0);
    ASSERT_TRUE(sink.saved);
    // The fixture rethrows the saved pointer after completion.
    int code = 0;
    try {
        std::rethrow_exception(sink.saved);
    } catch (const stage_error& failure) {
        code = failure.code;
    } catch (...) {
        code = -1;
    }
    EXPECT_EQ(code, expected_code);
}

TEST(ExecutionTaskHeldout, A06_FirstStagingMoveThrowsInTheLiveFrame) {
    run_throwing_staging_move(1, events{"M", "D", "R"}, 31);
}

TEST(ExecutionTaskHeldout, A06_SecondStagingMoveThrowsInTheLiveFrame) {
    run_throwing_staging_move(2, events{"M", "M", "D", "R"}, 32);
}

// ---------------------------------------------------------------------------
// 2 · Stop token identity, possibility and laziness (A07 to A14)

struct stop_counts {
    std::atomic<int> sources{0};
    std::atomic<int> registrations{0};
    std::atomic<int> invocations{0};
    std::atomic<int> releases{0};
    std::atomic<int> requests{0};
    // Called in the callback's constructor once the registration is armed.
    std::function<void()> after_register;

    void reset() {
        sources = 0;
        registrations = 0;
        invocations = 0;
        releases = 0;
        requests = 0;
        after_register = nullptr;
    }
};

// External storage, one set per tag. Every test uses its own tags.
template<int Tag>
stop_counts& counts() {
    static stop_counts storage;
    return storage;
}

enum class possibility { follows_state, always };

// A stop token over an in-place stop state that counts the registration,
// invocation and release of its callbacks. It is a distinct type per tag.
template<int Tag, possibility Possible, bool DefaultConstructible>
class probe_token {
public:
    template<class Function>
    class callback_type {
        struct counting {
            Function function;

            void operator()() noexcept {
                ++counts<Tag>().invocations;
                function();
            }
        };

        // Destroyed after the registration below is gone.
        struct release_mark {
            ~release_mark() { ++counts<Tag>().releases; }
        };

        static std::inplace_stop_token arm(const probe_token& token) {
            stop_counts& book = counts<Tag>();
            ++book.registrations;
            return token.inner_;
        }

        release_mark mark_;
        std::inplace_stop_callback<counting> inner_;

    public:
        template<class Initializer>
            requires std::constructible_from<Function, Initializer>
        callback_type(const probe_token& token, Initializer&& initializer)
            : inner_(arm(token), counting{Function(std::forward<Initializer>(initializer))}) {
            // The callback is installed: a request made from now on finds it.
            stop_counts& book = counts<Tag>();
            if (book.after_register) book.after_register();
        }

        callback_type(callback_type&&) = delete;
    };

    probe_token() noexcept
        requires DefaultConstructible
    = default;

    explicit probe_token(std::inplace_stop_token inner) noexcept : inner_(inner) {}

    bool stop_requested() const noexcept { return inner_.stop_requested(); }
    bool stop_possible() const noexcept {
        return Possible == possibility::always || inner_.stop_possible();
    }

    friend bool operator==(const probe_token&, const probe_token&) noexcept = default;

private:
    std::inplace_stop_token inner_;
};

// A stoppable source whose token type has no default constructor. Its
// constructions and the requests made on it are counted. The token type
// still has a disengaged value (one made from a disengaged in-place token),
// so A14 uses engaged_source below instead.
template<int Tag>
class probe_source {
public:
    using token_type = probe_token<Tag, possibility::follows_state, false>;

    probe_source() { ++counts<Tag>().sources; }

    token_type get_token() const noexcept { return token_type(inner_.get_token()); }
    bool stop_possible() const noexcept { return true; }
    bool stop_requested() const noexcept { return inner_.stop_requested(); }
    bool request_stop() noexcept {
        ++counts<Tag>().requests;
        return inner_.request_stop();
    }

private:
    std::inplace_stop_source inner_;
};

static_assert(std::stoppable_token<probe_token<0, possibility::follows_state, false>>);
static_assert(!std::default_initializable<probe_token<0, possibility::follows_state, false>>);
static_assert(std::default_initializable<probe_token<0, possibility::follows_state, true>>);

template<class Source>
struct stop_config {
    using start_scheduler_type = ex::inline_scheduler;
    using stop_source_type = Source;
};

// The receiver's environment: the token is returned by value, so the type of
// get_stop_token(get_env(rcvr)) is exactly Token.
template<class Token>
struct token_env {
    Token token;

    Token query(std::get_stop_token_t) const noexcept { return token; }
};

struct observation {
    bool requested = false;
    bool possible = false;

    friend bool operator==(const observation&, const observation&) = default;
};

template<class Token>
observation observe(const Token& token) {
    return {token.stop_requested(), token.stop_possible()};
}

struct count_sink {
    template<class... Values>
    void value(Values&&...) {
        ++values;
        if (at_entry) at_entry();
        if (doomed) doomed->reset();
    }
    template<class Error>
    void error(Error&&) { ++errors; }
    void stopped() { ++stops; }

    int values = 0;
    int errors = 0;
    int stops = 0;
    std::function<void()> at_entry;
    operation_box* doomed = nullptr;
};

template<class Token>
struct token_readings {
    std::vector<Token> tokens;
    std::vector<observation> seen;

    void record(const Token& token) {
        tokens.push_back(token);
        seen.push_back(observe(token));
    }
};

// Reads the promised token twice.
template<class Config>
ex::task<void, Config> reads_twice(token_readings<typename ex::task<void, Config>::stop_token_type>& out) {
    out.record(co_await ex::read_env(std::get_stop_token));
    out.record(co_await ex::read_env(std::get_stop_token));
}

template<int Tag>
void run_exact_non_default_token(bool requested_first) {
    using source_t = probe_source<Tag>;
    using token_t = typename source_t::token_type;
    using config_t = stop_config<source_t>;
    static_assert(std::same_as<typename ex::task<void, config_t>::stop_token_type, token_t>);

    counts<Tag>().reset();
    source_t external;
    if (requested_first) external.request_stop();
    const token_t original = external.get_token();
    token_readings<token_t> readings;
    count_sink sink;
    {
        operation_box box;
        auto& operation = box.emplace(reads_twice<config_t>(readings),
                                      sink_receiver<count_sink, token_env<token_t>>{&sink, {original}});
        ex::start(operation);
    }

    ASSERT_EQ(readings.tokens.size(), 2u);
    EXPECT_TRUE(readings.tokens[0] == original);
    EXPECT_TRUE(readings.tokens[1] == original);
    const observation expected{requested_first, true};
    EXPECT_EQ(readings.seen[0], expected);
    EXPECT_EQ(readings.seen[1], expected);
    // Only the test's own source was ever constructed, and nothing registered.
    EXPECT_EQ(counts<Tag>().sources.load(), 1);
    EXPECT_EQ(counts<Tag>().registrations.load(), 0);
    EXPECT_EQ(sink.values, 1);
}

TEST(ExecutionTaskHeldout, A07_ExactNonDefaultConstructibleTokenIsReturnedUnchanged) {
    run_exact_non_default_token<701>(false);
    run_exact_non_default_token<702>(true);
}

ex::task<observation, inline_config> child_reads(std::optional<std::inplace_stop_token>& seen) {
    auto token = co_await ex::read_env(std::get_stop_token);
    seen.emplace(token);
    co_return observe(token);
}

ex::task<void, inline_config> reads_around_child(token_readings<std::inplace_stop_token>& out,
                                                 std::optional<std::inplace_stop_token>& child_token,
                                                 observation& child_seen) {
    out.record(co_await ex::read_env(std::get_stop_token));
    child_seen = co_await child_reads(child_token);
    out.record(co_await ex::read_env(std::get_stop_token));
}

TEST(ExecutionTaskHeldout, A08_ExactDisengagedInplaceTokenStaysDisengaged) {
    static_assert(std::same_as<ex::task<void, inline_config>::stop_token_type, std::inplace_stop_token>);
    const std::inplace_stop_token original{};
    token_readings<std::inplace_stop_token> readings;
    std::optional<std::inplace_stop_token> child_token;
    observation child_seen{true, true};
    count_sink sink;
    {
        operation_box box;
        auto& operation = box.emplace(
            reads_around_child(readings, child_token, child_seen),
            sink_receiver<count_sink, token_env<std::inplace_stop_token>>{&sink, {original}});
        ex::start(operation);
    }

    ASSERT_EQ(readings.tokens.size(), 2u);
    EXPECT_TRUE(readings.tokens[0] == original);
    EXPECT_TRUE(readings.tokens[1] == original);
    EXPECT_EQ(readings.seen[0], (observation{false, false}));
    EXPECT_EQ(readings.seen[1], (observation{false, false}));
    ASSERT_TRUE(child_token.has_value());
    EXPECT_TRUE(*child_token == original);
    EXPECT_EQ(child_seen, (observation{false, false}));
    EXPECT_EQ(sink.values, 1);
}

// A09/A10/A13: an upstream token of another type that always reports
// possible, over a state the test owns.
template<int Tag>
using upstream_token = probe_token<Tag, possibility::always, false>;

template<int SourceTag>
struct lazy_readings {
    using token_t = typename probe_source<SourceTag>::token_type;

    // The two tokens issued before the request, kept across it.
    std::vector<token_t> issued;
    std::vector<observation> at_issue;
    bool second_equals_first = false;
    // Both observed after the request and before any further query.
    std::vector<observation> before_final_query;
    std::optional<observation> final_seen;
    bool final_equals_both = false;
};

// One park before the first query, one right after each of the two reads.
template<int SourceTag>
ex::task<void, stop_config<probe_source<SourceTag>>> reads_around_gates(gate& before, gate& after_first,
                                                                         gate& after_second,
                                                                         lazy_readings<SourceTag>& out) {
    co_await park_sender{&before};
    out.issued.push_back(co_await ex::read_env(std::get_stop_token));
    out.at_issue.push_back(observe(out.issued[0]));
    co_await park_sender{&after_first};
    out.issued.push_back(co_await ex::read_env(std::get_stop_token));
    out.at_issue.push_back(observe(out.issued[1]));
    out.second_equals_first = out.issued[1] == out.issued[0];
    co_await park_sender{&after_second};
    // The request was made while parked. No query may repair a stale token.
    out.before_final_query.push_back(observe(out.issued[0]));
    out.before_final_query.push_back(observe(out.issued[1]));
    auto last = co_await ex::read_env(std::get_stop_token);
    out.final_seen = observe(last);
    out.final_equals_both = last == out.issued[0] && last == out.issued[1];
    // The tokens refer into the operation, which ends with this coroutine.
    out.issued.clear();
}

template<int SourceTag>
ex::task<void, stop_config<probe_source<SourceTag>>> never_asks(int& body_runs) {
    co_await ex::just();
    ++body_runs;
}

TEST(ExecutionTaskHeldout, A09_MismatchedTokenIsAdaptedOnFirstDemandOnly) {
    constexpr int source_tag = 901;
    constexpr int upstream_tag = 902;
    counts<source_tag>().reset();
    counts<upstream_tag>().reset();
    std::inplace_stop_source owner;
    const upstream_token<upstream_tag> upstream{owner.get_token()};
    using receiver_t = sink_receiver<count_sink, token_env<upstream_token<upstream_tag>>>;

    // The sibling never asks for a token.
    {
        int body_runs = 0;
        count_sink sink;
        operation_box box;
        auto& operation = box.emplace(never_asks<source_tag>(body_runs), receiver_t{&sink, {upstream}});
        ex::start(operation);
        box.reset();
        EXPECT_EQ(body_runs, 1);
        EXPECT_EQ(sink.values, 1);
        EXPECT_EQ(counts<source_tag>().sources.load(), 0);
        EXPECT_EQ(counts<upstream_tag>().registrations.load(), 0);
    }

    gate before;
    gate after_first;
    gate after_second;
    lazy_readings<source_tag> readings;
    count_sink sink;
    operation_box box;
    auto& operation = box.emplace(reads_around_gates<source_tag>(before, after_first, after_second, readings),
                                  receiver_t{&sink, {upstream}});
    EXPECT_EQ(counts<source_tag>().sources.load(), 0);
    EXPECT_EQ(counts<upstream_tag>().registrations.load(), 0);
    ex::start(operation);
    ASSERT_TRUE(before.parked());
    // Connected, started and suspended: nothing was adapted yet.
    EXPECT_EQ(counts<source_tag>().sources.load(), 0);
    EXPECT_EQ(counts<upstream_tag>().registrations.load(), 0);

    before.release();
    ASSERT_TRUE(after_first.parked());
    // Right after the first read: the source and the registration both exist.
    ASSERT_EQ(readings.at_issue.size(), 1u);
    EXPECT_EQ(readings.at_issue[0], (observation{false, true}));
    EXPECT_EQ(counts<source_tag>().sources.load(), 1);
    EXPECT_EQ(counts<upstream_tag>().registrations.load(), 1);

    after_first.release();
    ASSERT_TRUE(after_second.parked());
    ASSERT_EQ(readings.at_issue.size(), 2u);
    EXPECT_EQ(readings.at_issue[1], (observation{false, true}));
    EXPECT_TRUE(readings.second_equals_first);
    EXPECT_EQ(counts<source_tag>().sources.load(), 1);
    EXPECT_EQ(counts<upstream_tag>().registrations.load(), 1);
    EXPECT_EQ(counts<upstream_tag>().invocations.load(), 0);
    EXPECT_EQ(counts<source_tag>().requests.load(), 0);

    owner.request_stop();
    EXPECT_EQ(counts<upstream_tag>().invocations.load(), 1);
    EXPECT_EQ(counts<source_tag>().requests.load(), 1);
    after_second.release();
    // Both earlier tokens followed the request before the final query.
    ASSERT_EQ(readings.before_final_query.size(), 2u);
    EXPECT_EQ(readings.before_final_query[0], (observation{true, true}));
    EXPECT_EQ(readings.before_final_query[1], (observation{true, true}));
    ASSERT_TRUE(readings.final_seen.has_value());
    EXPECT_EQ(*readings.final_seen, (observation{true, true}));
    EXPECT_TRUE(readings.final_equals_both);
    EXPECT_EQ(sink.values, 1);
    box.reset();
    EXPECT_EQ(counts<source_tag>().sources.load(), 1);
    EXPECT_EQ(counts<upstream_tag>().registrations.load(), 1);
    EXPECT_EQ(counts<upstream_tag>().releases.load(), 1);
    EXPECT_EQ(counts<upstream_tag>().invocations.load(), 1);
    EXPECT_EQ(counts<source_tag>().requests.load(), 1);
}

class flag {
public:
    void set() {
        {
            std::lock_guard lock{mutex_};
            set_ = true;
        }
        ready_.notify_all();
    }

    void wait() {
        std::unique_lock lock{mutex_};
        ready_.wait(lock, [this] { return set_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    bool set_ = false;
};

template<int SourceTag>
struct overlap_readings {
    using token_t = typename probe_source<SourceTag>::token_type;
    std::optional<token_t> first;
    std::optional<observation> first_on_return;
    std::optional<token_t> second;
};

template<int SourceTag>
ex::task<void, stop_config<probe_source<SourceTag>>> reads_then_parks(gate& before, gate& after,
                                                                       overlap_readings<SourceTag>& out) {
    co_await park_sender{&before};
    out.first.emplace(co_await ex::read_env(std::get_stop_token));
    out.first_on_return = observe(*out.first);
    co_await park_sender{&after};
    out.second.emplace(co_await ex::read_env(std::get_stop_token));
}

TEST(ExecutionTaskHeldout, A10_RequestBeforeFirstQueryIsVisibleOnReturn) {
    constexpr int source_tag = 1001;
    constexpr int upstream_tag = 1002;
    counts<source_tag>().reset();
    counts<upstream_tag>().reset();
    std::inplace_stop_source owner;
    owner.request_stop();
    const upstream_token<upstream_tag> upstream{owner.get_token()};

    gate before;
    gate after;
    overlap_readings<source_tag> readings;
    count_sink sink;
    operation_box box;
    auto& operation = box.emplace(
        reads_then_parks<source_tag>(before, after, readings),
        sink_receiver<count_sink, token_env<upstream_token<upstream_tag>>>{&sink, {upstream}});
    ex::start(operation);
    before.release();
    ASSERT_TRUE(readings.first_on_return.has_value());
    EXPECT_EQ(*readings.first_on_return, (observation{true, true}));
    after.release();
    ASSERT_TRUE(readings.second.has_value());
    EXPECT_TRUE(*readings.first == *readings.second);
    EXPECT_EQ(sink.values, 1);
    box.reset();
    EXPECT_EQ(counts<upstream_tag>().invocations.load(), 1);
    EXPECT_EQ(counts<upstream_tag>().registrations.load(), 1);
    EXPECT_EQ(counts<source_tag>().sources.load(), 1);
    EXPECT_EQ(counts<source_tag>().requests.load(), 1);
}

// Which of two events of the querying thread came first.
class query_progress {
public:
    void paused_in_registration() { mark(inside_); }
    void query_thread_returned() { mark(returned_); }

    // True if the registration paused. False if the querying thread ran on
    // to its next suspension without ever pausing in a registration.
    bool wait_for_pause() {
        std::unique_lock lock{mutex_};
        changed_.wait(lock, [this] { return inside_ || returned_; });
        return inside_;
    }

private:
    void mark(bool& what) {
        {
            std::lock_guard lock{mutex_};
            what = true;
        }
        changed_.notify_all();
    }

    std::mutex mutex_;
    std::condition_variable changed_;
    bool inside_ = false;
    bool returned_ = false;
};

// The first query runs on one thread and pauses in the constructor of the
// upstream callback, after the callback is installed and before the query
// can publish its answer. The request is made from another thread during
// that pause. With join_request_first the request has returned, and its
// delivery is checked, while the constructor is still paused.
void run_request_during_registration(bool join_request_first) {
    constexpr int source_tag = 1003;
    constexpr int upstream_tag = 1004;
    counts<source_tag>().reset();
    counts<upstream_tag>().reset();
    std::inplace_stop_source owner;
    const upstream_token<upstream_tag> upstream{owner.get_token()};

    gate before;
    gate after;
    overlap_readings<source_tag> readings;
    count_sink sink;
    operation_box box;
    auto& operation = box.emplace(
        reads_then_parks<source_tag>(before, after, readings),
        sink_receiver<count_sink, token_env<upstream_token<upstream_tag>>>{&sink, {upstream}});
    ex::start(operation);
    ASSERT_TRUE(before.parked());
    // Nothing was adapted by connect and start. The hook is armed only now.
    ASSERT_EQ(counts<upstream_tag>().registrations.load(), 0);

    query_progress progress;
    flag resume_registration;
    counts<upstream_tag>().after_register = [&] {
        progress.paused_in_registration();
        resume_registration.wait();
    };

    std::thread querying{[&] {
        before.release();
        progress.query_thread_returned();
    }};
    if (!progress.wait_for_pause()) {
        querying.join();
        ADD_FAILURE() << "the first query returned without registering a callback on the upstream token";
        // Disarm the hook, so that a late registration cannot block the cleanup.
        counts<upstream_tag>().after_register = nullptr;
        if (after.parked()) after.release();
        return;
    }
    // The callback is installed and the first query has not returned.
    EXPECT_FALSE(readings.first.has_value());
    std::thread requesting{[&] { owner.request_stop(); }};
    if (join_request_first) {
        requesting.join();
        // Delivered during the pause, by the requesting thread: one
        // invocation of the forwarder and one request on the task's source.
        EXPECT_EQ(counts<upstream_tag>().invocations.load(), 1);
        EXPECT_EQ(counts<source_tag>().requests.load(), 1);
        EXPECT_EQ(counts<upstream_tag>().registrations.load(), 1);
        EXPECT_EQ(counts<source_tag>().sources.load(), 1);
        EXPECT_FALSE(readings.first.has_value());
    }
    resume_registration.set();
    querying.join();
    if (requesting.joinable()) requesting.join();

    // Both actions have completed.
    ASSERT_TRUE(readings.first.has_value());
    EXPECT_EQ(observe(*readings.first), (observation{true, true}));
    if (join_request_first) {
        ASSERT_TRUE(readings.first_on_return.has_value());
        EXPECT_EQ(*readings.first_on_return, (observation{true, true}));
    }
    ASSERT_TRUE(after.parked());
    after.release();
    ASSERT_TRUE(readings.second.has_value());
    EXPECT_TRUE(*readings.first == *readings.second);
    EXPECT_EQ(sink.values, 1);
    box.reset();
    EXPECT_EQ(counts<upstream_tag>().invocations.load(), 1);
    EXPECT_EQ(counts<upstream_tag>().registrations.load(), 1);
    EXPECT_EQ(counts<upstream_tag>().releases.load(), 1);
    EXPECT_EQ(counts<source_tag>().sources.load(), 1);
    EXPECT_EQ(counts<source_tag>().requests.load(), 1);
}

TEST(ExecutionTaskHeldout, A10_RequestDeliveredWhileRegistrationIsPaused) {
    run_request_during_registration(true);
}

TEST(ExecutionTaskHeldout, A10_RequestRacingTheResumedRegistration) {
    for (int round = 0; round < 50; ++round) {
        run_request_during_registration(false);
        if (::testing::Test::HasFailure()) break;
    }
}

// A11: an upstream token whose possibility follows its state and whose
// default value is disengaged, as std::stop_token.
template<class Upstream>
void run_disengaged_dynamic_upstream(const Upstream& upstream, int* registrations) {
    static_assert(!std::same_as<Upstream, std::inplace_stop_token>);
    token_readings<std::inplace_stop_token> readings;
    count_sink sink;
    {
        operation_box box;
        auto& operation = box.emplace(reads_twice<inline_config>(readings),
                                      sink_receiver<count_sink, token_env<Upstream>>{&sink, {upstream}});
        ex::start(operation);
    }
    ASSERT_EQ(readings.tokens.size(), 2u);
    EXPECT_TRUE(readings.tokens[0] == std::inplace_stop_token{});
    EXPECT_TRUE(readings.tokens[1] == std::inplace_stop_token{});
    EXPECT_EQ(readings.seen[0], (observation{false, false}));
    EXPECT_EQ(readings.seen[1], (observation{false, false}));
    EXPECT_EQ(sink.values, 1);
    if (registrations) EXPECT_EQ(*registrations, 0);
}

template<class Token>
concept usable_upstream = std::stoppable_token<Token>;

// Dependent, so that the body is not instantiated where the host library's
// std::stop_token is not a stoppable_token of this implementation.
template<class Token>
bool run_literal_std_stop_token() {
    if constexpr (usable_upstream<Token>) {
        run_disengaged_dynamic_upstream(Token{}, nullptr);
        return true;
    } else {
        return false;
    }
}

TEST(ExecutionTaskHeldout, A11_DisengagedStdStopTokenLiteral) {
#if defined(__cpp_lib_jthread)
    if (!run_literal_std_stop_token<std::stop_token>()) {
        GTEST_SKIP() << "the host library's std::stop_token has no callback_type, so it is not a "
                        "stoppable_token and cannot be an upstream token";
    }
#else
    GTEST_SKIP() << "the host library has no std::stop_token";
#endif
}

TEST(ExecutionTaskHeldout, A11_DisengagedDynamicTokenOfTheSameShape) {
    constexpr int tag = 1101;
    counts<tag>().reset();
    using dynamic_token = probe_token<tag, possibility::follows_state, true>;
    static_assert(std::is_nothrow_default_constructible_v<dynamic_token>);
    static_assert(!std::unstoppable_token<dynamic_token>);
    int registrations = -1;
    {
        token_readings<std::inplace_stop_token> readings;
        count_sink sink;
        operation_box box;
        auto& operation = box.emplace(
            reads_twice<inline_config>(readings),
            sink_receiver<count_sink, token_env<dynamic_token>>{&sink, {dynamic_token{}}});
        ex::start(operation);
        box.reset();
        ASSERT_EQ(readings.tokens.size(), 2u);
        EXPECT_TRUE(readings.tokens[0] == std::inplace_stop_token{});
        EXPECT_TRUE(readings.tokens[1] == std::inplace_stop_token{});
        EXPECT_EQ(readings.seen[0], (observation{false, false}));
        EXPECT_EQ(readings.seen[1], (observation{false, false}));
        EXPECT_EQ(sink.values, 1);
        registrations = counts<tag>().registrations.load();
    }
    EXPECT_EQ(registrations, 0);
}

struct wait_readings {
    bool requested;
    bool possible;
    // 1: the task's start scheduler is the waiting run loop's. 2: it is an
    // erased inline scheduler.
    int start_scheduler;

    friend bool operator==(const wait_readings&, const wait_readings&) = default;
};

ex::task<wait_readings> reads_under_sync_wait(ex::task_scheduler waiting) {
    auto token = co_await ex::read_env(std::get_stop_token);
    static_assert(std::same_as<decltype(token), std::inplace_stop_token>);
    auto current = co_await ex::read_env(ex::get_start_scheduler);
    static_assert(std::same_as<decltype(current), ex::task_scheduler>);
    co_return wait_readings{token.stop_requested(), token.stop_possible(),
                            (current == waiting ? 1 : 0) +
                                (current == ex::task_scheduler{ex::inline_scheduler{}} ? 2 : 0)};
}

TEST(ExecutionTaskHeldout, A12_DefaultTaskUnderSyncWaitSeesADisengagedToken) {
    // The wait's own start scheduler is read from its environment, erased
    // and handed to the task, which compares it with the one it resolved.
    auto result = std::this_thread::sync_wait(
        ex::let_value(ex::read_env(ex::get_start_scheduler), [](auto waiting) {
            static_assert(!std::same_as<decltype(waiting), ex::inline_scheduler>);
            return reads_under_sync_wait(ex::task_scheduler{waiting});
        }));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), (wait_readings{false, false, 1}));
}

ex::task<int, inline_config> reads_once_then_returns(frame_marker, observation& seen) {
    auto token = co_await ex::read_env(std::get_stop_token);
    seen = observe(token);
    co_return 5;
}

TEST(ExecutionTaskHeldout, A13_RegistrationIsGoneWhenTheReceiverIsEntered) {
    constexpr int upstream_tag = 1301;
    counts<upstream_tag>().reset();
    std::inplace_stop_source owner;
    const upstream_token<upstream_tag> upstream{owner.get_token()};

    trace log;
    observation seen;
    count_sink sink;
    operation_box box;
    sink.doomed = &box;
    int registrations_at_entry = -1;
    int releases_at_entry = -1;
    std::size_t frames_dead_at_entry = 99;
    sink.at_entry = [&] {
        registrations_at_entry = counts<upstream_tag>().registrations.load();
        releases_at_entry = counts<upstream_tag>().releases.load();
        frames_dead_at_entry = log.count("D");
    };
    auto& operation = box.emplace(
        reads_once_then_returns(frame_marker{log}, seen),
        sink_receiver<count_sink, token_env<upstream_token<upstream_tag>>>{&sink, {upstream}});
    ex::start(operation);

    EXPECT_EQ(seen, (observation{false, true}));
    EXPECT_EQ(registrations_at_entry, 1);
    EXPECT_EQ(releases_at_entry, 1);
    EXPECT_EQ(frames_dead_at_entry, 1u);
    EXPECT_EQ(box.destroyed, 1);

    // The callback has returned and the operation is gone. The external
    // owner requests now.
    owner.request_stop();
    EXPECT_EQ(counts<upstream_tag>().invocations.load(), 0);
    EXPECT_EQ(sink.values, 1);
    EXPECT_EQ(sink.errors + sink.stops, 0);
    EXPECT_EQ(log.count("D"), 1u);
}

// A14: a source whose token type has no disengaged value at all. Only the
// source can make a token, from its own stop state, and stop_possible() is
// true unconditionally.
template<int Tag>
class engaged_source;

template<int Tag>
class engaged_token {
public:
    template<class Function>
    class callback_type {
        std::inplace_stop_callback<Function> inner_;

    public:
        template<class Initializer>
            requires std::constructible_from<Function, Initializer>
        callback_type(const engaged_token& token, Initializer&& initializer)
            : inner_(token.inner_, std::forward<Initializer>(initializer)) {}

        callback_type(callback_type&&) = delete;
    };

    bool stop_requested() const noexcept { return inner_.stop_requested(); }
    bool stop_possible() const noexcept { return true; }

    friend bool operator==(const engaged_token&, const engaged_token&) noexcept = default;

private:
    friend class engaged_source<Tag>;
    explicit engaged_token(std::inplace_stop_token inner) noexcept : inner_(inner) {}

    std::inplace_stop_token inner_;
};

template<int Tag>
class engaged_source {
public:
    using token_type = engaged_token<Tag>;

    engaged_source() { ++counts<Tag>().sources; }

    token_type get_token() const noexcept { return token_type(inner_.get_token()); }
    bool stop_possible() const noexcept { return true; }
    bool stop_requested() const noexcept { return inner_.stop_requested(); }
    bool request_stop() noexcept {
        ++counts<Tag>().requests;
        return inner_.request_stop();
    }

private:
    std::inplace_stop_source inner_;
};

static_assert(std::stoppable_token<engaged_token<0>>);
static_assert(!std::unstoppable_token<engaged_token<0>>);
static_assert(!std::default_initializable<engaged_token<0>>);
static_assert(!std::constructible_from<engaged_token<0>, std::inplace_stop_token>);

template<int SourceTag, class Env>
void run_engaged_only(Env environment, observation expected, int expected_sources, int expected_requests) {
    using config_t = stop_config<engaged_source<SourceTag>>;
    using token_t = engaged_token<SourceTag>;
    static_assert(std::same_as<typename ex::task<void, config_t>::stop_token_type, token_t>);
    counts<SourceTag>().reset();
    token_readings<token_t> readings;
    count_sink sink;
    {
        operation_box box;
        auto& operation = box.emplace(reads_twice<config_t>(readings),
                                      sink_receiver<count_sink, Env>{&sink, std::move(environment)});
        ex::start(operation);
        ASSERT_EQ(readings.tokens.size(), 2u);
        EXPECT_EQ(readings.seen[0], expected);
        EXPECT_EQ(readings.seen[1], expected);
        // Compared while the operation, and with it the source, is alive.
        EXPECT_TRUE(readings.tokens[0] == readings.tokens[1]);
        EXPECT_EQ(counts<SourceTag>().sources.load(), expected_sources);
        // A request reaches the source only if the upstream made one.
        EXPECT_EQ(counts<SourceTag>().requests.load(), expected_requests);
    }
    EXPECT_EQ(sink.values, 1);
}

TEST(ExecutionTaskHeldout, A14_EngagedOnlySourceUnderAnAbsentStopQuery) {
    // No stop query: the upstream is never_stop_token.
    run_engaged_only<1401>(ex::env<>{}, observation{false, true}, 1, 0);
}

TEST(ExecutionTaskHeldout, A14_EngagedOnlySourceUnderADisengagedDynamicToken) {
    constexpr int upstream_tag = 1403;
    counts<upstream_tag>().reset();
    using dynamic_token = probe_token<upstream_tag, possibility::follows_state, true>;
    run_engaged_only<1402>(token_env<dynamic_token>{dynamic_token{}}, observation{false, true}, 1, 0);
    EXPECT_EQ(counts<upstream_tag>().registrations.load(), 0);
}

TEST(ExecutionTaskHeldout, A14_EngagedOnlySourceUnderARequestedUpstream) {
    constexpr int upstream_tag = 1405;
    counts<upstream_tag>().reset();
    using dynamic_token = probe_token<upstream_tag, possibility::follows_state, true>;
    std::inplace_stop_source owner;
    owner.request_stop();
    run_engaged_only<1404>(token_env<dynamic_token>{dynamic_token{owner.get_token()}},
                           observation{true, true}, 1, 1);
    EXPECT_EQ(counts<upstream_tag>().registrations.load(), 1);
    EXPECT_EQ(counts<upstream_tag>().invocations.load(), 1);
}

// ---------------------------------------------------------------------------
// 3 · Reference results (A15 to A17, A24)

// The caller's referent. It cannot be copied or moved, and it records its
// destruction outside itself, so its lifetime can be checked before any of
// its members is read.
struct item {
    item(int value, int* destroyed) : value(value), destroyed(destroyed) {}
    item(const item&) = delete;
    item& operator=(const item&) = delete;
    ~item() { ++*destroyed; }

    int value;
    int* destroyed;
};

static_assert(!std::move_constructible<item>);

// The literal task<T> of the cases has the default environment, whose start
// scheduler is the erased one. The directly connected receiver supplies an
// inline scheduler through its own environment.
using inline_start = ex::prop<ex::get_start_scheduler_t, ex::inline_scheduler>;

inline_start make_inline_start() {
    return ex::prop{ex::get_start_scheduler, ex::inline_scheduler{}};
}

// Entered with the exact argument expression type the task hands to
// set_value. Everything is recorded at entry. A mutable item is then
// modified, and only after that is the operation destroyed.
struct reference_sink {
    template<class Value>
    void value(Value&& argument) {
        category.emplace_back(name_of<Value&&>());
        address = std::addressof(argument);
        frames_dead_at_entry = log ? log->count("D") : 0;
        if (referent_destroyed) referent_destroyed_at_entry = *referent_destroyed;
        if constexpr (std::same_as<Value&&, item&>) {
            argument.value += mutate;
        }
        if (doomed) doomed->reset();
    }

    template<class Error>
    void error(Error&&) { category.push_back("error"); }
    void stopped() { category.push_back("stopped"); }

    template<class Given>
    static const char* name_of() {
        if constexpr (std::same_as<Given, item&>) return "item&";
        else if constexpr (std::same_as<Given, const item&>) return "const item&";
        else if constexpr (std::same_as<Given, item&&>) return "item&&";
        else if constexpr (std::same_as<Given, const item&&>) return "const item&&";
        else if constexpr (std::same_as<Given, int&>) return "int&";
        else if constexpr (std::same_as<Given, const int&>) return "const int&";
        else if constexpr (std::same_as<Given, int&&>) return "int&&";
        else if constexpr (std::same_as<Given, const int&&>) return "const int&&";
        else return "another type";
    }

    std::vector<std::string> category;
    const void* address = nullptr;
    std::size_t frames_dead_at_entry = 99;
    int mutate = 0;
    const int* referent_destroyed = nullptr;
    int referent_destroyed_at_entry = -1;
    trace* log = nullptr;
    operation_box* doomed = nullptr;
};

using reference_receiver = sink_receiver<reference_sink, inline_start>;

template<class Task, class Wanted>
inline constexpr bool sole_value_signature =
    lists_signature<signatures_of<Task>, Wanted> && value_signature_count<signatures_of<Task>> == 1;

ex::task<item&> returns_lvalue(frame_marker, item* external) {
    co_return *external;
}

ex::task<item&&> returns_rvalue_after(frame_marker, item* external, gate& delay) {
    co_await park_sender{&delay};
    co_return std::move(*external);
}

ex::task<const item&> returns_const_lvalue(frame_marker, const item* external) {
    co_return *external;
}

ex::task<const item&&> returns_const_rvalue(frame_marker, const item* external) {
    co_return std::move(*external);
}

ex::task<const int&> returns_const_int(frame_marker, int* external) {
    co_return *external;
}

static_assert(sole_value_signature<ex::task<item&>, ex::set_value_t(item&)>);
static_assert(sole_value_signature<ex::task<item&&>, ex::set_value_t(item&&)>);
static_assert(sole_value_signature<ex::task<const item&>, ex::set_value_t(const item&)>);
static_assert(sole_value_signature<ex::task<const item&&>, ex::set_value_t(const item&&)>);
static_assert(sole_value_signature<ex::task<const int&>, ex::set_value_t(const int&)>);

TEST(ExecutionTaskHeldout, A15_LvalueReferenceResultKeepsTheCallersObject) {
    int destroyed = 0;
    auto external = std::make_unique<item>(10, &destroyed);
    trace log;
    reference_sink sink;
    sink.log = &log;
    sink.mutate = 7;
    sink.referent_destroyed = &destroyed;
    operation_box box;
    sink.doomed = &box;

    auto& operation = box.emplace(returns_lvalue(frame_marker{log}, external.get()),
                                  reference_receiver{&sink, make_inline_start()});
    ex::start(operation);

    // The receiver recorded its entry, mutated the item and then deleted the
    // operation. The caller looks at its object only now.
    EXPECT_EQ(sink.category, (std::vector<std::string>{"item&"}));
    EXPECT_EQ(sink.frames_dead_at_entry, 1u);
    EXPECT_EQ(sink.referent_destroyed_at_entry, 0);
    EXPECT_EQ(sink.address, external.get());
    EXPECT_EQ(box.destroyed, 1);
    ASSERT_EQ(destroyed, 0);
    EXPECT_EQ(external->value, 17);
}

TEST(ExecutionTaskHeldout, A16_RvalueReferenceResultIsDeliveredAsAnRvalueOfTheCallersObject) {
    int destroyed = 0;
    auto external = std::make_unique<item>(20, &destroyed);
    trace log;
    reference_sink sink;
    sink.log = &log;
    sink.referent_destroyed = &destroyed;
    gate delay;
    operation_box box;

    auto& operation = box.emplace(returns_rvalue_after(frame_marker{log}, external.get(), delay),
                                  reference_receiver{&sink, make_inline_start()});
    ex::start(operation);
    ASSERT_TRUE(delay.parked());
    EXPECT_TRUE(sink.category.empty());
    delay.release();

    EXPECT_EQ(sink.category, (std::vector<std::string>{"item&&"}));
    EXPECT_EQ(sink.address, external.get());
    EXPECT_EQ(sink.frames_dead_at_entry, 1u);
    // Alive at delivery, with the frame gone and the operation still held.
    EXPECT_EQ(sink.referent_destroyed_at_entry, 0);
    EXPECT_EQ(log.count("D"), 1u);
    ASSERT_EQ(destroyed, 0);
    EXPECT_EQ(external->value, 20);
    // Alive after the operation is destroyed.
    box.reset();
    EXPECT_EQ(log.count("D"), 1u);
    ASSERT_EQ(destroyed, 0);
    EXPECT_EQ(external->value, 20);
    // Destroyed once, by the caller.
    external.reset();
    EXPECT_EQ(destroyed, 1);
}

TEST(ExecutionTaskHeldout, A17_ConstReferenceResultsKeepConstAndCategory) {
    int destroyed = 0;
    {
        const auto external = std::make_unique<const item>(30, &destroyed);
        {
            trace log;
            reference_sink sink;
            sink.log = &log;
            sink.referent_destroyed = &destroyed;
            operation_box box;
            auto& operation = box.emplace(returns_const_lvalue(frame_marker{log}, external.get()),
                                          reference_receiver{&sink, make_inline_start()});
            ex::start(operation);
            EXPECT_EQ(sink.category, (std::vector<std::string>{"const item&"}));
            EXPECT_EQ(sink.address, external.get());
            EXPECT_EQ(sink.frames_dead_at_entry, 1u);
            EXPECT_EQ(sink.referent_destroyed_at_entry, 0);
        }
        {
            trace log;
            reference_sink sink;
            sink.log = &log;
            sink.referent_destroyed = &destroyed;
            operation_box box;
            auto& operation = box.emplace(returns_const_rvalue(frame_marker{log}, external.get()),
                                          reference_receiver{&sink, make_inline_start()});
            ex::start(operation);
            EXPECT_EQ(sink.category, (std::vector<std::string>{"const item&&"}));
            EXPECT_EQ(sink.address, external.get());
            EXPECT_EQ(sink.frames_dead_at_entry, 1u);
            EXPECT_EQ(sink.referent_destroyed_at_entry, 0);
        }
        ASSERT_EQ(destroyed, 0);
        EXPECT_EQ(external->value, 30);
    }
    EXPECT_EQ(destroyed, 1);
}

TEST(ExecutionTaskHeldout, A24_ConstIntReferenceResultIsTheCallersObject) {
    auto external = std::make_unique<int>(42);
    trace log;
    reference_sink sink;
    sink.log = &log;
    operation_box box;
    sink.doomed = &box;

    auto& operation = box.emplace(returns_const_int(frame_marker{log}, external.get()),
                                  reference_receiver{&sink, make_inline_start()});
    ex::start(operation);

    // Exactly const int&: a mutable int& at the same address is not it.
    EXPECT_EQ(sink.category, (std::vector<std::string>{"const int&"}));
    EXPECT_EQ(sink.address, external.get());
    EXPECT_EQ(sink.frames_dead_at_entry, 1u);
    EXPECT_EQ(box.destroyed, 1);
    EXPECT_EQ(*external, 42);
}

// ---------------------------------------------------------------------------
// 4 · Start-scheduler priority (A18 to A22; A23 is a configure probe)

// Allocation counts by allocator identity.
struct allocation_book {
    std::map<int, int> allocated;
    std::map<int, int> released;
};

allocation_book& allocations() {
    static allocation_book book;
    return book;
}

template<class T>
struct id_allocator {
    using value_type = T;

    id_allocator() noexcept = default;
    explicit id_allocator(int id) noexcept : id(id) {}
    template<class U>
    id_allocator(const id_allocator<U>& other) noexcept : id(other.id) {}

    T* allocate(std::size_t count) {
        ++allocations().allocated[id];
        return std::allocator<T>{}.allocate(count);
    }

    void deallocate(T* pointer, std::size_t count) noexcept {
        ++allocations().released[id];
        std::allocator<T>{}.deallocate(pointer, count);
    }

    template<class U>
    friend bool operator==(const id_allocator& left, const id_allocator<U>& right) noexcept {
        return left.id == right.id;
    }

    int id = -1;
};

using byte_allocator = id_allocator<std::byte>;

// Row k uses frame Fk, ordinary Ok, scheduler-specific Ak, incoming Lk and
// constructor default Dk.
constexpr int frame_id(int row) { return row * 10 + 1; }
constexpr int ordinary_id(int row) { return row * 10 + 2; }
constexpr int scheduler_allocator_id(int row) { return row * 10 + 3; }
constexpr int incoming_id(int row) { return row * 10 + 4; }
constexpr int default_id(int row) { return row * 10 + 5; }

template<class Scheduler>
struct inline_schedule_sender {
    using sender_concept = ex::sender_tag;

    template<class Receiver>
    struct operation {
        using operation_state_concept = ex::operation_state_tag;

        Receiver receiver;

        explicit operation(Receiver receiver) : receiver(std::move(receiver)) {}
        operation(operation&&) = delete;

        void start() & noexcept { ex::set_value(std::move(receiver)); }
    };

    struct attributes {
        Scheduler scheduler;

        template<class... Env>
        Scheduler query(ex::get_completion_scheduler_t<ex::set_value_t>, Env&&...) const noexcept {
            return scheduler;
        }
    };

    Scheduler scheduler;

    template<class Self, class... Env>
    static consteval auto get_completion_signatures() -> ex::completion_signatures<ex::set_value_t()> {
        return {};
    }

    template<ex::receiver Receiver>
    operation<Receiver> connect(Receiver receiver) const {
        return operation<Receiver>(std::move(receiver));
    }

    attributes get_env() const noexcept { return {scheduler}; }
};

// The scheduler the receiver's environment offers: another type than the
// task's start scheduler.
struct incoming_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    int resource = 0;

    inline_schedule_sender<incoming_scheduler> schedule() const noexcept { return {*this}; }
    ex::forward_progress_guarantee query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    friend bool operator==(const incoming_scheduler&, const incoming_scheduler&) noexcept = default;
};

struct seed_t {};

enum : unsigned {
    with_both = 1u,
    with_scheduler = 2u,
    with_allocator = 4u,
    with_default = 8u,
    with_self_and_allocator = 16u,
};

template<int Row>
events& constructor_log() {
    static events log;
    return log;
}

// The start scheduler archetype. Mask selects which of the draft's
// initializers exist. Each records its overload and keeps the identity of the
// resource it was built from. Copies record nothing.
template<unsigned Mask, int Row>
struct start_archetype {
    using scheduler_concept = ex::scheduler_tag;

    start_archetype(seed_t, int resource) noexcept : resource(resource) {}

    start_archetype() noexcept
        requires (static_cast<bool>(Mask & with_default))
        : resource(default_id(Row)) {
        constructor_log<Row>().push_back("S()");
    }

    start_archetype(const incoming_scheduler& incoming, const byte_allocator& allocator) noexcept
        requires (static_cast<bool>(Mask & with_both))
        : resource(incoming.resource), allocator(allocator.id) {
        constructor_log<Row>().push_back("S(s,a)");
    }

    start_archetype(const start_archetype& same, const byte_allocator& allocator) noexcept
        requires (static_cast<bool>(Mask & with_self_and_allocator))
        : resource(same.resource), allocator(allocator.id) {
        constructor_log<Row>().push_back("S(self,a)");
    }

    explicit start_archetype(const incoming_scheduler& incoming) noexcept
        requires (static_cast<bool>(Mask & with_scheduler))
        : resource(incoming.resource) {
        constructor_log<Row>().push_back("S(s)");
    }

    explicit start_archetype(const byte_allocator& allocator) noexcept
        requires (static_cast<bool>(Mask & with_allocator))
        : resource(allocator.id), allocator(allocator.id) {
        constructor_log<Row>().push_back("S(a)");
    }

    inline_schedule_sender<start_archetype> schedule() const noexcept { return {*this}; }
    ex::forward_progress_guarantee query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }

    friend bool operator==(const start_archetype& left, const start_archetype& right) noexcept {
        return left.resource == right.resource && left.allocator == right.allocator;
    }

    int resource = 0;
    int allocator = 0;
};

struct selection_counts {
    int start_scheduler = 0;
    int scheduler_allocator = 0;
    int ordinary_allocator = 0;
};

// The receiver's environment of a row. Each query counts its evaluations.
template<int Row, class Offered, bool HasScheduler, bool HasSchedulerAllocator>
struct selection_env {
    selection_counts* counted;
    Offered offered;

    Offered query(ex::get_start_scheduler_t) const noexcept
        requires HasScheduler
    {
        ++counted->start_scheduler;
        return offered;
    }

    byte_allocator query(std::get_scheduler_allocator_t) const noexcept
        requires HasSchedulerAllocator
    {
        ++counted->scheduler_allocator;
        return byte_allocator{scheduler_allocator_id(Row)};
    }

    byte_allocator query(std::get_allocator_t) const noexcept {
        ++counted->ordinary_allocator;
        return byte_allocator{ordinary_id(Row)};
    }
};

template<class Scheduler>
struct selection_config {
    using start_scheduler_type = Scheduler;
    using allocator_type = byte_allocator;
};

template<class Scheduler>
struct selection_readings {
    int resumes = 0;
    selection_counts at_entry;
    std::optional<Scheduler> scheduler;
    int ordinary = 0;
};

// Reads back the start scheduler and the ordinary allocator.
template<class Scheduler>
ex::task<void, selection_config<Scheduler>> reads_selection(std::allocator_arg_t, byte_allocator,
                                                            const selection_counts& counted,
                                                            selection_readings<Scheduler>& out) {
    ++out.resumes;
    out.at_entry = counted;
    out.scheduler.emplace(co_await ex::read_env(ex::get_start_scheduler));
    out.ordinary = (co_await ex::read_env(std::get_allocator)).id;
}

// A compiler may elide a coroutine frame allocation, and a test cannot see
// whether it did (the G1 rule of the conformance ledger). So the frame
// identity is charged zero or one allocation, whatever was allocated was
// released to it, and no other identity was charged anything.
void expect_frame_charge(int frame, std::initializer_list<int> others, int charged_before = -1) {
    const int allocated = allocations().allocated[frame];
    EXPECT_GE(allocated, 0);
    EXPECT_LE(allocated, 1);
    EXPECT_EQ(allocations().released[frame], allocated);
    for (const int other : others) {
        EXPECT_EQ(allocations().allocated[other], 0) << "identity " << other;
        EXPECT_EQ(allocations().released[other], 0) << "identity " << other;
    }
    // No other identity was charged either, except one the fixture itself
    // charged before the task was made.
    for (const auto& [identity, count] : allocations().allocated) {
        if (identity != frame && identity != charged_before) EXPECT_EQ(count, 0) << "identity " << identity;
    }
}

template<int Row, class Scheduler, class Env>
selection_readings<Scheduler> run_selection(Env environment, selection_counts& counted) {
    constructor_log<Row>().clear();
    allocations() = {};
    selection_readings<Scheduler> readings;
    count_sink sink;
    {
        operation_box box;
        auto& operation = box.emplace(
            reads_selection<Scheduler>(std::allocator_arg, byte_allocator{frame_id(Row)}, counted, readings),
            sink_receiver<count_sink, Env>{&sink, std::move(environment)});
        ex::start(operation);
    }
    EXPECT_EQ(sink.values, 1);
    EXPECT_EQ(readings.resumes, 1);
    expect_frame_charge(frame_id(Row), {ordinary_id(Row), scheduler_allocator_id(Row), incoming_id(Row),
                                        default_id(Row), byte_allocator{}.id});
    return readings;
}

TEST(ExecutionTaskHeldout, A18_Arm1_ExactSchedulerIsTakenAsIs) {
    constexpr int row = 1;
    using scheduler_t = start_archetype<with_self_and_allocator | with_both | with_scheduler | with_allocator |
                                            with_default, row>;
    static_assert(ex::scheduler<scheduler_t>);
    const scheduler_t original{seed_t{}, incoming_id(row)};
    // The query answers S const&.
    struct exact_env {
        selection_counts* counted;
        const scheduler_t* offered;

        const scheduler_t& query(ex::get_start_scheduler_t) const noexcept {
            ++counted->start_scheduler;
            return *offered;
        }
        byte_allocator query(std::get_scheduler_allocator_t) const noexcept {
            ++counted->scheduler_allocator;
            return byte_allocator{scheduler_allocator_id(row)};
        }
        byte_allocator query(std::get_allocator_t) const noexcept {
            ++counted->ordinary_allocator;
            return byte_allocator{ordinary_id(row)};
        }
    };
    static_assert(std::same_as<decltype(ex::get_start_scheduler(std::declval<exact_env&>())), const scheduler_t&>);

    selection_counts counted;
    auto readings = run_selection<row, scheduler_t>(exact_env{&counted, &original}, counted);

    ASSERT_TRUE(readings.scheduler.has_value());
    EXPECT_TRUE(*readings.scheduler == original);
    EXPECT_EQ(readings.scheduler->resource, incoming_id(row));
    EXPECT_EQ(readings.scheduler->allocator, 0);
    EXPECT_TRUE(constructor_log<row>().empty());
    EXPECT_EQ(readings.at_entry.start_scheduler, 1);
    EXPECT_EQ(readings.at_entry.scheduler_allocator, 0);
    EXPECT_EQ(counted.scheduler_allocator, 0);
    EXPECT_EQ(readings.ordinary, ordinary_id(row));
}

// A scheduler too large for any small-object buffer, so that its erasure is
// backed by an allocation.
struct large_inline_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    int resource = 0;
    char padding[512] = {};

    inline_schedule_sender<large_inline_scheduler> schedule() const noexcept { return {*this}; }
    ex::forward_progress_guarantee query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    friend bool operator==(const large_inline_scheduler& left, const large_inline_scheduler& right) noexcept {
        return left.resource == right.resource;
    }
};

// The default start scheduler type, with an allocator identity of its own.
struct erased_config {
    using allocator_type = byte_allocator;
};

static_assert(std::same_as<ex::task<void, erased_config>::start_scheduler_type, ex::task_scheduler>);

struct erased_readings {
    std::optional<ex::task_scheduler> scheduler;
    selection_counts at_entry;
    int ordinary = 0;
};

ex::task<void, erased_config> reads_erased(std::allocator_arg_t, byte_allocator, const selection_counts& counted,
                                           erased_readings& out) {
    out.at_entry = counted;
    out.scheduler.emplace(co_await ex::read_env(ex::get_start_scheduler));
    out.ordinary = (co_await ex::read_env(std::get_allocator)).id;
}

TEST(ExecutionTaskHeldout, A18_Arm1_ErasedSchedulerByValueKeepsItsBackingAllocation) {
    constexpr int row = 18;
    allocations() = {};
    selection_counts counted;
    erased_readings readings;
    int backing = 0;
    {
        const ex::task_scheduler original{large_inline_scheduler{incoming_id(row)},
                                          byte_allocator{incoming_id(row)}};
        backing = allocations().allocated[incoming_id(row)];
        ASSERT_GE(backing, 1);

        struct erased_env {
            selection_counts* counted;
            ex::task_scheduler offered;

            ex::task_scheduler query(ex::get_start_scheduler_t) const noexcept {
                ++counted->start_scheduler;
                return offered;
            }
            byte_allocator query(std::get_scheduler_allocator_t) const noexcept {
                ++counted->scheduler_allocator;
                return byte_allocator{scheduler_allocator_id(row)};
            }
            byte_allocator query(std::get_allocator_t) const noexcept {
                ++counted->ordinary_allocator;
                return byte_allocator{ordinary_id(row)};
            }
        };

        count_sink sink;
        {
            operation_box box;
            auto& operation = box.emplace(
                reads_erased(std::allocator_arg, byte_allocator{frame_id(row)}, counted, readings),
                sink_receiver<count_sink, erased_env>{&sink, {&counted, original}});
            ex::start(operation);
        }
        EXPECT_EQ(sink.values, 1);
        ASSERT_TRUE(readings.scheduler.has_value());
        EXPECT_TRUE(*readings.scheduler == original);
        EXPECT_EQ(readings.at_entry.start_scheduler, 1);
        EXPECT_EQ(counted.scheduler_allocator, 0);
        EXPECT_EQ(readings.ordinary, ordinary_id(row));
        // No second erasure: nothing more was allocated by the backing
        // allocator, and nothing by the scheduler-specific or ordinary one.
        EXPECT_EQ(allocations().allocated[incoming_id(row)], backing);
        EXPECT_EQ(allocations().released[incoming_id(row)], 0);
        expect_frame_charge(frame_id(row), {ordinary_id(row), scheduler_allocator_id(row), byte_allocator{}.id},
                            incoming_id(row));
    }
    // The original, the environment's copy and the operation are gone. The
    // scheduler the task read is the sole owner of the original's block, so
    // the block is still live. A deep copy would have let it go by now.
    ASSERT_TRUE(readings.scheduler.has_value());
    EXPECT_EQ(allocations().allocated[incoming_id(row)], backing);
    EXPECT_EQ(allocations().released[incoming_id(row)], 0);
    EXPECT_TRUE(*readings.scheduler == large_inline_scheduler{incoming_id(row)});
    // Releasing that owner frees the block.
    readings.scheduler.reset();
    EXPECT_EQ(allocations().released[incoming_id(row)], backing);
}

TEST(ExecutionTaskHeldout, A19_Arm2_SchedulerAndSchedulerAllocator) {
    constexpr int row = 2;
    using scheduler_t = start_archetype<with_both | with_scheduler | with_allocator | with_default, row>;
    using env_t = selection_env<row, incoming_scheduler, true, true>;
    selection_counts counted;
    auto readings = run_selection<row, scheduler_t>(env_t{&counted, incoming_scheduler{incoming_id(row)}}, counted);

    EXPECT_EQ(constructor_log<row>(), (events{"S(s,a)"}));
    ASSERT_TRUE(readings.scheduler.has_value());
    EXPECT_EQ(readings.scheduler->resource, incoming_id(row));
    EXPECT_EQ(readings.scheduler->allocator, scheduler_allocator_id(row));
    EXPECT_EQ(readings.at_entry.start_scheduler, 1);
    EXPECT_EQ(readings.at_entry.scheduler_allocator, 1);
    EXPECT_EQ(readings.at_entry.ordinary_allocator, 0);
    EXPECT_EQ(readings.ordinary, ordinary_id(row));
}

TEST(ExecutionTaskHeldout, A20_Arm3_SchedulerAlone) {
    constexpr int row = 3;
    using scheduler_t = start_archetype<with_scheduler | with_allocator | with_default, row>;
    using env_t = selection_env<row, incoming_scheduler, true, true>;
    selection_counts counted;
    auto readings = run_selection<row, scheduler_t>(env_t{&counted, incoming_scheduler{incoming_id(row)}}, counted);

    EXPECT_EQ(constructor_log<row>(), (events{"S(s)"}));
    ASSERT_TRUE(readings.scheduler.has_value());
    EXPECT_EQ(readings.scheduler->resource, incoming_id(row));
    EXPECT_EQ(readings.at_entry.start_scheduler, 1);
    EXPECT_EQ(counted.scheduler_allocator, 0);
    EXPECT_EQ(readings.at_entry.ordinary_allocator, 0);
    EXPECT_EQ(readings.ordinary, ordinary_id(row));
}

TEST(ExecutionTaskHeldout, A21_Arm4_AllocatorAloneFallsBackToTheOrdinaryMember) {
    constexpr int row = 4;
    using scheduler_t = start_archetype<with_allocator | with_default, row>;
    {
        // No scheduler-allocator member: the query falls back to get_allocator.
        using env_t = selection_env<row, incoming_scheduler, false, false>;
        selection_counts counted;
        auto readings = run_selection<row, scheduler_t>(env_t{&counted, {}}, counted);
        EXPECT_EQ(constructor_log<row>(), (events{"S(a)"}));
        ASSERT_TRUE(readings.scheduler.has_value());
        EXPECT_EQ(readings.scheduler->resource, ordinary_id(row));
        EXPECT_NE(readings.scheduler->resource, frame_id(row));
        EXPECT_NE(readings.scheduler->resource, default_id(row));
        EXPECT_EQ(readings.at_entry.ordinary_allocator, 1);
        EXPECT_EQ(readings.ordinary, ordinary_id(row));
    }
    {
        using env_t = selection_env<row, incoming_scheduler, false, true>;
        selection_counts counted;
        auto readings = run_selection<row, scheduler_t>(env_t{&counted, {}}, counted);
        EXPECT_EQ(constructor_log<row>(), (events{"S(a)"}));
        ASSERT_TRUE(readings.scheduler.has_value());
        EXPECT_EQ(readings.scheduler->resource, scheduler_allocator_id(row));
        EXPECT_EQ(readings.at_entry.scheduler_allocator, 1);
        EXPECT_EQ(readings.at_entry.ordinary_allocator, 0);
        EXPECT_EQ(readings.ordinary, ordinary_id(row));
    }
}

TEST(ExecutionTaskHeldout, A22_Arm5_DefaultConstructionEvaluatesNoQuery) {
    constexpr int row = 5;
    using scheduler_t = start_archetype<with_default, row>;
    using env_t = selection_env<row, incoming_scheduler, true, true>;
    selection_counts counted;
    auto readings = run_selection<row, scheduler_t>(env_t{&counted, incoming_scheduler{incoming_id(row)}}, counted);

    EXPECT_EQ(constructor_log<row>(), (events{"S()"}));
    ASSERT_TRUE(readings.scheduler.has_value());
    EXPECT_EQ(readings.scheduler->resource, default_id(row));
    EXPECT_EQ(counted.start_scheduler, 0);
    EXPECT_EQ(counted.scheduler_allocator, 0);
    EXPECT_EQ(readings.at_entry.ordinary_allocator, 0);
    EXPECT_EQ(readings.resumes, 1);
    EXPECT_EQ(readings.ordinary, ordinary_id(row));
}

} // namespace
