export module Switch:Policies;

import std;
import Miracle;

using namespace Miracle;

export namespace Switch {

/// Chooses which test environments capture trace events for a run.
///
/// ForcedFailures captures every case but renders its trace only with a failure. ForcedAll additionally
/// renders traces for passing cases. The test-level [[= trace]] annotation is always honored.
enum class[[= debug::derive]] TraceMode : u8 {
  Annotations[[= debug::rename("annotations")]],
  ForcedFailures[[= debug::rename("forced_failures")]],
  ForcedAll[[= debug::rename("forced_all")]],
};

struct TestPolicy final {
  bool trace{};
  /// Forces this test through a fresh process-per-case worker even when the run uses in-process execution.
  bool isolated{};
  /// Keeps this test in the parent process so it can orchestrate nested isolated runs.
  bool parent{};
  Option<String> expectedPanic;
  Option<std::chrono::steady_clock::duration> timeout;
  usize repeat{1};
  usize warmup{};
  usize retry{};
};

/// Internal exception used by panic(). The runner turns it into a structured diagnostic, while shouldPanic()
/// can recognize it as an expected outcome.
class TestPanic final : public std::exception {
public:
  TestPanic(String message, std::source_location location) noexcept;

  [[nodiscard]] auto message() const noexcept -> StringView;

  [[nodiscard]] auto location() const noexcept -> std::source_location;

  [[nodiscard]] auto what() const noexcept -> const char * override;

private:
  String message_;
  std::source_location location_;
};

[[noreturn]] auto panic(StringView message, std::source_location location = std::source_location::current())
    -> void;

} // namespace Switch
