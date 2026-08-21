module Switch;

import std;
import Miracle;

import :Diagnostics;
import :Environment;
import :Execution;
import :Policies;

using namespace Miracle;

namespace Switch {

namespace {

[[nodiscard]] constexpr auto isPanicDiagnostic(const Diagnostic &diagnostic) noexcept -> bool {
  return diagnostic.header.code == DiagnosticCode::TestPanicked or
         diagnostic.header.code == DiagnosticCode::UnhandledException or
         diagnostic.header.code == DiagnosticCode::TestReturnedError;
}

[[nodiscard]] constexpr auto containsText(const DiagnosticNote &note, StringView expected) noexcept -> bool {
  return note.message.contains(expected) or
         std::ranges::any_of(
             note.fragments, [expected](const DiagnosticFragment &fragment) constexpr noexcept -> bool {
               return fragment.text.contains(expected);
             });
}

[[nodiscard]] constexpr auto diagnosticMatches(const Diagnostic &diagnostic, StringView expected) noexcept
    -> bool {
  if (not isPanicDiagnostic(diagnostic))
    return false;

  if (expected.empty() or diagnostic.description().contains(expected))
    return true;

  if (std::ranges::any_of(
          diagnostic.details.notes, [expected](const DiagnosticNote &note) constexpr noexcept -> bool {
            return containsText(note, expected);
          })) {
    return true;
  }

  return std::ranges::any_of(diagnostic.details.attachments,
      [expected](const DiagnosticAttachment &attachment) constexpr noexcept -> bool {
        return attachment.name.contains(expected) or attachment.content.contains(expected);
      });
}

[[nodiscard]] constexpr auto matchingPanicIndex(const TestState &state, StringView expected) noexcept
    -> Option<usize> {
  const auto matching = std::ranges::find_if(
      state.diagnostics, [expected](const Diagnostic &diagnostic) constexpr noexcept -> bool {
        return diagnosticMatches(diagnostic, expected);
      });
  if (matching == state.diagnostics.end())
    return None;

  return static_cast<usize>(std::ranges::distance(state.diagnostics.begin(), matching));
}

[[nodiscard]] auto durationText(std::chrono::steady_clock::duration duration) -> String {
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
  if (milliseconds.count() != 0)
    return std::format("{} ms", milliseconds.count());

  const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration);
  return std::format("{} μs", microseconds.count());
}

[[nodiscard]] auto expectedPanicDiagnostic(StringView expected, std::source_location location) -> Diagnostic {
  Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::ExpectedPanicNotObserved, location);
  diagnostic.details.spans.front().label = "expected panic";
  diagnostic.details.spans.front().selection = SpanSelection::Declaration;
  if (expected.empty())
    diagnostic.addNote("expected the test to panic");
  else
    diagnostic.addNote(std::format("expected a panic containing: {}", expected));

  return diagnostic;
}

[[nodiscard]] auto timeoutDiagnostic(std::chrono::steady_clock::duration limit,
    std::chrono::steady_clock::duration elapsed,
    std::source_location location) -> Diagnostic {
  Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::TimeoutExceeded, location);
  diagnostic.details.spans.front().label = "timeout";
  diagnostic.details.spans.front().selection = SpanSelection::Declaration;
  diagnostic.addNote(std::format("limit: {}", durationText(limit)));
  diagnostic.addNote(std::format("elapsed: {}", durationText(elapsed)));
  return diagnostic;
}

} // namespace

auto TestExecution::passed() const noexcept -> bool {
  return state.passed();
}

auto TestExecution::failed() const noexcept -> bool {
  return state.failed();
}

auto makeAttemptOutcome(TestExecution execution, bool retainExecution) -> AttemptOutcome {
  AttemptOutcome outcome{
      .descriptor = execution.descriptor,
      // The outcome owns its descriptor; do not store a view into the local execution copy here.
      .identifier = {},
      .attempt = execution.attempt,
      .duration = execution.duration,
      .wallDuration = execution.wallDuration,
      .assertions = execution.state.assertions,
      .failedAssertions = execution.state.failedAssertions,
      .errors = execution.state.errors,
      .runSeed = execution.runSeed,
      .seed = execution.seed,
      .iteration = execution.iteration,
      .warmup = execution.warmup,
      .passed = execution.passed(),
  };
  if (retainExecution or not outcome.passed)
    outcome.failure = std::move(execution);
  if (outcome.failure)
    outcome.timeout = std::ranges::any_of(
        outcome.failure->state.diagnostics, [](const Diagnostic &diagnostic) constexpr noexcept -> bool {
          return diagnostic.header.code == DiagnosticCode::TimeoutExceeded;
        });
  return outcome;
}

auto detail::returnedErrorDiagnostic(const Error &error, std::source_location location) -> Diagnostic {
  Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::TestReturnedError, location);
  diagnostic.details.spans.front().label = "test return";
  diagnostic.details.spans.front().selection = SpanSelection::Declaration;

  if (error.messages.empty()) {
    diagnostic.addNote("test returned an error without a message");
    return diagnostic;
  }

  std::ranges::for_each(error.messages, [&diagnostic](const Error::Message &message) constexpr -> void {
    const String text = message.message.empty() ? String{"error"} : message.message;
    diagnostic.addNote(
        text, DiagnosticLevel::Note, makeSpan("error origin", SpanKind::Secondary, message.location));
  });
  return diagnostic;
}

auto detail::unhandledExceptionDiagnostic(String message, std::source_location location) -> Diagnostic {
  Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::UnhandledException, location);
  diagnostic.details.spans.front().label = "test body";
  diagnostic.details.spans.front().selection = SpanSelection::Declaration;
  diagnostic.addNote(std::format("exception: {}", message));
  return diagnostic;
}

auto detail::panickedDiagnostic(String message, std::source_location location) -> Diagnostic {
  Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::TestPanicked, location);
  diagnostic.details.spans.front().label = "panic";
  diagnostic.addNote(std::format("panic: {}", message));
  return diagnostic;
}

auto detail::taskLifecycleDiagnostic(const TaskLifecycleError &error, std::source_location location)
    -> Diagnostic {
  Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::TaskStranded, location);
  diagnostic.details.spans.front().label = "async task";
  diagnostic.details.spans.front().selection = SpanSelection::Declaration;

  switch (error.failure()) {
    case TaskLifecycleFailure::Empty:
      diagnostic.addNote("the coroutine task was empty and could not be executed");
      break;
    case TaskLifecycleFailure::Stranded:
      diagnostic.addNote("the coroutine suspended without scheduling further work");
      diagnostic.addNote(
          "use yield(), sleepFor(), or an awaitable that resumes through the active test run loop",
          DiagnosticLevel::Help);
      break;
    case TaskLifecycleFailure::PendingWork:
      diagnostic.addNote(
          std::format("{} scheduled continuation(s) remained after the task completed", error.pendingWork()));
      diagnostic.addNote(
          "an awaitable must not schedule a coroutine that it does not suspend", DiagnosticLevel::Help);
      break;
  }

  return diagnostic;
}

auto detail::nativeFaultDiagnostic(const NativeFault &fault, std::source_location location) -> Diagnostic {
  const DiagnosticCode code = fault.kind == NativeFaultKind::IsolationUnavailable
                                  ? DiagnosticCode::WorkerLaunchFailed
                                  : DiagnosticCode::NativeFault;
  Diagnostic diagnostic = makeDiagnostic(code, location);
  diagnostic.details.spans.front().label = "worker boundary";
  diagnostic.details.spans.front().selection = SpanSelection::Declaration;

  if (fault.kind == NativeFaultKind::IsolationUnavailable) {
    diagnostic.addNote("the requested process-per-case worker backend is unavailable");
    return diagnostic;
  }

  if (fault.kind == NativeFaultKind::Signal) {
    diagnostic.addNote(
        std::format("worker terminated with signal code {} ({})", fault.code, debug::enumName(fault.signal)));
  } else {
    diagnostic.addNote(
        std::format("worker terminated with {} code {}", debug::enumName(fault.kind), fault.code));
  }

  if (fault.address != 0)
    diagnostic.addNote(std::format("fault address: 0x{:x}", fault.address));

  if (fault.instruction != 0)
    diagnostic.addNote(std::format("instruction address: 0x{:x}", fault.instruction));

  if (not fault.symbolsAvailable)
    diagnostic.addNote("symbols unavailable; native stack frames were not resolved", DiagnosticLevel::Help);

  return diagnostic;
}

auto detail::applyPolicy(const PolicyApplication &application) -> void {
  const TestPolicy &policy = application.policy.get();
  TestEnvironment &environment = application.environment.get();

  if (policy.expectedPanic) {
    const Option<usize> index = matchingPanicIndex(environment.state(), *policy.expectedPanic);
    if (index) {
      environment.recordTrace(std::format("expected panic observed: {}", *policy.expectedPanic));
      environment.acceptExpectedPanic(*index);
    } else {
      environment.recordError(expectedPanicDiagnostic(*policy.expectedPanic, application.location));
    }
  }

  const bool timeoutReached =
      policy.timeout and
      (application.retry
              ? application.cancelled or application.timeoutTriggered or application.elapsed > *policy.timeout
              : environment.stopRequested() or application.elapsed >= *policy.timeout);
  if (timeoutReached)
    environment.recordError(timeoutDiagnostic(*policy.timeout, application.elapsed, application.location));
}

} // namespace Switch
