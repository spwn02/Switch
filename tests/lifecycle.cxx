import std;

import Miracle;
import Switch;

using namespace Miracle;
using namespace Switch;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
namespace Tests::lifecycle {

using Clock = std::chrono::steady_clock;

inline constexpr auto timeoutLimit = std::chrono::milliseconds{80};
inline constexpr auto oneHour = std::chrono::hours{1};

template <class Function>
[[nodiscard]] auto runVirtually(StringView identifier,
    Function &&function,
    Option<Clock::duration> timeout = None,
    std::source_location location = std::source_location::current()) -> TestExecution {
  return run(
      TestDescriptor{
          .identifier = String{identifier},
          .location = location,
          .policy = TestPolicy{.timeout = timeout},
      },
      std::forward<Function>(function),
      TimeMode::Virtual);
}

namespace {

struct DoubleSchedule final {
  [[nodiscard]] constexpr auto await_ready() const noexcept -> bool { // NOLINT
    return false;
  }

  auto await_suspend(std::coroutine_handle<> handle) const -> void { // NOLINT
    detail::schedule(handle);
    detail::schedule(handle);
  }

  constexpr auto await_resume() const noexcept -> void { // NOLINT
  }
};

struct NeverResumes final {
  [[nodiscard]] constexpr auto await_ready() const noexcept -> bool { // NOLINT
    return false;
  }

  constexpr auto await_suspend(std::coroutine_handle<>) const noexcept -> void { // NOLINT
  }

  constexpr auto await_resume() const noexcept -> void { // NOLINT
  }
};

struct ScheduleWithoutSuspending final {
  [[nodiscard]] constexpr auto await_ready() const noexcept -> bool { // NOLINT
    return false;
  }

  [[nodiscard]] auto await_suspend(std::coroutine_handle<> handle) const -> bool { // NOLINT
    detail::schedule(handle);
    return false;
  }

  constexpr auto await_resume() const noexcept -> void { // NOLINT
  }
};

auto taskWithPendingWork() -> Task<void> {
  co_await ScheduleWithoutSuspending{};
}

auto completedTask() -> Task<void> {
  co_return;
}

template <class Value>
auto driveDirectly(Task<Value> &task, detail::RunLoop &runLoop) -> detail::TaskDriveResult {
  return detail::drive(
      task,
      runLoop,
      [](std::coroutine_handle<> handle) -> void { handle.resume(); },
      [] -> void {},
      [] noexcept -> Option<detail::RunLoop::TimePoint> { return None; });
}

} // namespace

[[ = test, = group("framework"), = tag("lifecycle") ]] auto
doesNotResumeADoublyScheduledCoroutineBeforeItsTimer() -> void {
  constexpr usize expectedCompletions{1};
  usize completions{};
  const TestExecution execution = runVirtually("double schedule", [&completions] -> Task<void> { // NOLINT
    co_await DoubleSchedule{};
    co_await sleepFor(oneHour);
    ++completions;
  });

  require(execution.passed());
  require(eq(completions, expectedCompletions));
  require(execution.duration == oneHour);
  check(execution.state.diagnostics.empty());
}

[[ = test, = group("framework"), = tag("lifecycle") ]] auto reportsAStrandedAsyncTask() -> void {
  const TestExecution execution = runVirtually("stranded", [] -> Task<void> { co_await NeverResumes{}; });

  require(execution.failed());
  require(execution.state.errors == 1_exp);
  require(execution.state.diagnostics.size() == 1_exp);
  const Diagnostic &diagnostic = execution.state.diagnostics.front();
  require(diagnostic.details.notes.size() == 2_exp);
  check(diagnostic.header.code == DiagnosticCode::TaskStranded);
  check(diagnostic.code() == "E016"_exp);
  check(diagnostic.description() == "asynchronous task was stranded"_exp);
  check(diagnostic.details.spans.front().label == "async task"_exp);
  check(diagnostic.details.notes.front().message.contains("without scheduling"));
  check(diagnostic.details.notes.back().level == DiagnosticLevel::Help);
}

[[ = test, = group("framework"), = tag("lifecycle") ]] auto
reportsOnlyTheTimeoutWhenCancellationCannotResumeATask() -> void {
  const TestExecution execution = runVirtually(
      "cancelled stranded", [] -> Task<void> { co_await NeverResumes{}; }, std::chrono::milliseconds{0});

  require(execution.failed());
  require(execution.state.errors == 1_exp);
  require(execution.state.diagnostics.size() == 1_exp);
  check(execution.state.diagnostics.front().header.code == DiagnosticCode::TimeoutExceeded);
}

[[ = test, = group("framework"), = tag("lifecycle") ]] auto reportsAndCleansPendingWorkAfterATaskCompletes()
    -> void {
  const TestExecution invalidExecution =
      runVirtually("pending work", [] -> Task<void> { co_await ScheduleWithoutSuspending{}; });
  const TestExecution followingExecution = runVirtually("following", [] -> void { require(true); });

  require(invalidExecution.failed());
  require(invalidExecution.state.errors == 1_exp);
  require(invalidExecution.state.diagnostics.size() == 1_exp);
  const Diagnostic &diagnostic = invalidExecution.state.diagnostics.front();
  require(diagnostic.header.code == DiagnosticCode::TaskStranded);
  check(diagnostic.details.notes.front().message.contains("remained"));
  check(followingExecution.passed());
  check(detail::currentRunLoop() == nullptr);
}

[[ = test, = group("framework"), = tag("lifecycle") ]] auto clearsRunLoopTicketsWhenDrivingFails() -> void {
  detail::RunLoop runLoop{TimeMode::Virtual};
  Task<void> pending = taskWithPendingWork();
  const detail::TaskDriveResult result = driveDirectly(pending, runLoop);

  require(result.status == detail::TaskDriveStatus::PendingWork);
  require(result.pendingWork == 1_exp);
  require(runLoop.pendingWorkCount() == 0_exp);

  Task<void> following = completedTask();
  driveDirectly(following, runLoop);
  std::move(following).takeResult();

  require(runLoop.pendingWorkCount() == 0_exp);
  check(detail::currentRunLoop() == nullptr);
}

[[ = test, = group("framework"), = tag("lifecycle") ]] auto restoresTaskLocalBindingsAfterAsyncAbort()
    -> void {
  bool cleaned{};
  const Option<Ref<TestEnvironment>> outerEnvironment = currentEnvironment();
  const Option<Ref<const Context>> outerContext = currentContext();
  require(outerEnvironment);
  require(outerContext);

  const TestExecution execution = runVirtually("async abort", [&cleaned] -> Task<void> { // NOLINT
    const auto cleanup = std::scope_exit([&cleaned] -> void { cleaned = true; });
    co_await yield();
    require(false);
  });

  require(cleaned);
  require(execution.failed());
  require(execution.state.aborted);
  require(currentEnvironment());
  require(currentContext());
  require(detail::currentRunLoop() == nullptr);
  check(std::addressof(currentEnvironment()->get()) == std::addressof(outerEnvironment->get()));
  check(std::addressof(currentContext()->get()) == std::addressof(outerContext->get()));
}

[[ = test, = group("framework"), = tag("lifecycle") ]] auto runsCancellationCleanupExactlyOnce() -> void {
  constexpr usize expectedCleanupCalls{1};
  usize cleanupCalls{};
  bool observedStop{};
  const TestExecution execution = runVirtually(
      "cancellation cleanup",
      [&cleanupCalls, &observedStop](const Context &context) -> Task<void> { // NOLINT
        const auto cleanup = std::scope_exit([&cleanupCalls] -> void { ++cleanupCalls; });
        co_await sleepFor(oneHour);
        observedStop = context.stopToken.stop_requested();
        require(observedStop);
      },
      timeoutLimit);

  require(eq(cleanupCalls, expectedCleanupCalls));
  require(observedStop);
  require(execution.failed());
  require(execution.duration == timeoutLimit);
  require(execution.state.failedAssertions == 0_exp);
  require(execution.state.errors == 1_exp);
  require(execution.state.diagnostics.size() == 1_exp);
  check(execution.state.diagnostics.front().header.code == DiagnosticCode::TimeoutExceeded);
}

} // namespace Tests::lifecycle
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

consteval {
  discover<^^Tests::lifecycle>();
}
