import std;

import Miracle;
import Switch;

using namespace Miracle;
using namespace Switch;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
namespace Tests::virtualTime {

using Clock = std::chrono::steady_clock;

inline constexpr auto timeoutLimit = std::chrono::milliseconds{80};

template <class Function>
[[nodiscard]] auto runVirtually(StringView identifier,
    Function &&function,
    Option<Clock::duration> timeout = None) -> TestExecution {
  return run(
      TestDescriptor{
          .identifier = String{identifier},
          .policy = TestPolicy{.timeout = timeout},
      },
      std::forward<Function>(function),
      TimeMode::Virtual);
}

// NOLINTBEGIN(readability-identifier-naming)
class TimerProbe final {
public:
  class promise_type;
  using Handle = std::coroutine_handle<promise_type>;

  class promise_type final {
  public:
    [[nodiscard]] auto get_return_object() noexcept -> TimerProbe {
      return TimerProbe{Handle::from_promise(*this)};
    }

    [[nodiscard]] static constexpr auto initial_suspend() noexcept -> std::suspend_always {
      return {};
    }

    [[nodiscard]] static constexpr auto final_suspend() noexcept -> std::suspend_always {
      return {};
    }

    auto return_void() noexcept -> void {
    }

    [[noreturn]] auto unhandled_exception() noexcept -> void { // NOLINT
      std::terminate();
    }
  };

  ~TimerProbe() noexcept {
    reset();
  }

  TimerProbe(const TimerProbe &) = delete ("TimerProbe owns a coroutine handle.");
  auto operator=(const TimerProbe &) -> TimerProbe & = delete ("TimerProbe owns a coroutine handle.");

  TimerProbe(TimerProbe &&other) noexcept
      : handle_(std::exchange(other.handle_, {})) {
  }

  auto operator=(TimerProbe &&other) noexcept -> TimerProbe & {
    if (this == std::addressof(other))
      return *this;

    reset();
    handle_ = std::exchange(other.handle_, {});
    return *this;
  }

  [[nodiscard]] auto handle() const noexcept -> std::coroutine_handle<> {
    return handle_;
  }

private:
  explicit TimerProbe(Handle handle) noexcept
      : handle_(handle) {
  }

  auto reset() noexcept -> void {
    if (handle_)
      handle_.destroy();

    handle_ = {};
  }

  Handle handle_;
};
// NOLINTEND(readability-identifier-naming)

auto recordTimer(Vec<usize> &order, usize identifier) -> TimerProbe { // NOLINT
  order.push_back(identifier);
  co_return;
}

auto oneHourSleep() -> Task<void> {
  co_await sleepFor(std::chrono::hours{1});
  require(true);
}

auto nestedChild() -> Task<u32> {
  co_await sleepFor(std::chrono::seconds{2});
  co_return 42;
}

auto nestedParent() -> Task<void> {
  const u32 value = co_await nestedChild();
  require(value == 42);
}

namespace runnerProbe {

[[ = test, = group("framework"), = tag("virtual_time", "subjects") ]] auto oneHourTask() -> Task<void> {
  co_await sleepFor(std::chrono::hours{1});
  require(true);
}

} // namespace runnerProbe

[[ = test, = group("framework"), = tag("virtual_time") ]] auto equalDeadlineTimersKeepSchedulingOrder()
    -> void {
  detail::RunLoop runLoop{TimeMode::Virtual};
  Vec<usize> order{};
  TimerProbe first = recordTimer(order, 1);
  TimerProbe second = recordTimer(order, 2);
  const detail::RunLoop::TimePoint deadline{std::chrono::seconds{1}};

  runLoop.scheduleAt(first.handle(), deadline);
  runLoop.scheduleAt(second.handle(), deadline);

  require(runLoop.waitForWork(None) == detail::RunLoop::WaitResult::TimerReady);

  const auto resumeNext = [&runLoop] -> void {
    const Option<std::coroutine_handle<>> handle = runLoop.dequeue();
    require(handle);
    handle->resume();
  };

  resumeNext();
  resumeNext();

  require(order == Vec<usize>{1, 2});
}

[[ = test, = group("framework"), = tag("virtual_time") ]] auto virtualTimeCompletesOneHourWithoutWaiting()
    -> void {
  const Clock::time_point started = Clock::now();
  const TestExecution execution = runVirtually("one hour virtual wait", oneHourSleep);
  const Clock::duration wallElapsed = Clock::now() - started;

  require(execution.passed());
  require(execution.duration == std::chrono::hours{1});
  require(wallElapsed < std::chrono::seconds{1});
}

[[ = test, = group("framework"), = tag("virtual_time") ]] auto virtualTimeFlowsThroughNestedTasks() -> void {
  const TestExecution execution = runVirtually("nested virtual task", nestedParent);

  require(execution.passed());
  require(eq(execution.duration, std::chrono::seconds{2}));
}

[[ = test, = group("framework"), = tag("virtual_time") ]] auto virtualTimeoutWinsTimerTie() -> void {
  bool observedStop{};
  const TestExecution execution = runVirtually(
      "timer timeout tie",
      [&observedStop](const Context &context) -> Task<void> { // NOLINT
        co_await sleepFor(timeoutLimit);
        observedStop = context.stopToken.stop_requested();
        require(observedStop);
      },
      timeoutLimit);

  require(observedStop);
  require(execution.failed());
  require(execution.duration == timeoutLimit);
  require(execution.state.failedAssertions == 0);
  require(execution.state.errors == 1);
  require(execution.state.diagnostics.size() == 1);
  require(execution.state.diagnostics.front().header.code == DiagnosticCode::TimeoutExceeded);
}

[[ = test, = group("framework"), = tag("virtual_time") ]] auto virtualTimeoutRunsCoroutineCleanup() -> void {
  bool cleanupRan{};
  bool observedStop{};
  const TestExecution execution = runVirtually(
      "cancellation cleanup",
      [&cleanupRan, &observedStop](const Context &context) -> Task<void> { // NOLINT
        const auto cleanup = std::scope_exit([&cleanupRan] -> void { cleanupRan = true; });
        co_await sleepFor(std::chrono::hours{1});
        observedStop = context.stopToken.stop_requested();
        require(observedStop);
      },
      timeoutLimit);

  require(cleanupRan);
  require(observedStop);
  require(execution.failed());
  require(execution.duration == timeoutLimit);
  require(execution.state.errors == 1);
}

[[ = test, = group("framework"), = tag("virtual_time") ]] auto runOptionsPropagateVirtualTime() -> void {
  const Clock::time_point started = Clock::now();
  const Vec<TestExecution> executions = runAllDetailed<^^Tests::virtualTime::runnerProbe>(RunOptions{
      .timeMode = TimeMode::Virtual,
      .isolation = CrashIsolation::InProcess,
  });
  const Clock::duration wallElapsed = Clock::now() - started;

  require(executions.size() == 1);
  require(executions.front().passed());
  require(executions.front().duration == std::chrono::hours{1});
  require(wallElapsed < std::chrono::seconds{1});
}

} // namespace Tests::virtualTime
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

consteval {
  discover<^^Tests::virtualTime>();
}
