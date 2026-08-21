export module Switch:Context;

import std;
import Miracle;

import :Resources;

using namespace Miracle;

export namespace Switch {

/// Controls timing detail captured for each execution.
enum class[[= debug::derive]] CapturePolicy : u8 {
  None[[= debug::rename("none")]],
  PerAttempt[[= debug::rename("per_attempt")]],
};

struct Context final {
  // NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members)
  const StringView name;
  const StringView description;
  const usize testCase;
  TestResources &resources;
  /// Deterministic per-case seed derived from the run seed, case position, and repetition.
  const u64 seed{};
  /// Zero-based repeat index for this execution.
  const usize iteration{};
  const usize sample{};
  const usize retry{};
  const bool warmup{};
  /// Becomes requested once the test reaches its coroutine timeout boundary.
  const std::stop_token stopToken;
  const std::source_location location;
  // NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)
};

namespace detail {

/// Per-invocation settings bound by the runner before it enters a reflected test.
///
/// ContextBinding re-establishes the public Context around coroutine resumes. This smaller binding instead
/// carries runner-owned setup values used only while constructing that Context and environment.
struct InvocationSettings final {
  u64 seed{};
  usize iteration{};
  usize sample{};
  usize retry{};
  bool warmup{};
  bool forceTrace{};
  bool captureMemory{};
  bool captureProfile{true};
  CapturePolicy captureTiming{CapturePolicy::PerAttempt};
};

class InvocationBinding final {
public:
  explicit InvocationBinding(const InvocationSettings &settings) noexcept;
  ~InvocationBinding() noexcept;

  InvocationBinding(const InvocationBinding &) = delete (
      "InvocationBinding owns a pointer to previous thread-local invocation settings.");
  auto operator=(const InvocationBinding &) -> InvocationBinding & = delete (
      "InvocationBinding owns a pointer to previous thread-local invocation settings.");
  InvocationBinding(InvocationBinding &&) noexcept = delete (
      "InvocationBinding owns a pointer to previous thread-local invocation settings.");
  auto operator=(InvocationBinding &&) noexcept -> InvocationBinding & = delete (
      "InvocationBinding owns a pointer to previous thread-local invocation settings.");

private:
  const InvocationSettings *previous_{};
};

[[nodiscard]] auto currentInvocationSettings() noexcept -> InvocationSettings;

} // namespace detail

/// Binds the immutable public execution context to the current thread.
///
/// Coroutine scheduling will establish this binding around every resume, alongside EnvironmentBinding.
class ContextBinding final {
public:
  explicit ContextBinding(const Context &context) noexcept;
  ~ContextBinding() noexcept;

  ContextBinding(const ContextBinding &) = delete (
      "ContextBinding owns a pointer to a previous thread-local context");
  auto operator=(const ContextBinding &)
      -> ContextBinding & = delete ("ContextBinding owns a pointer to a previous thread-local context");
  ContextBinding(ContextBinding &&) = delete (
      "ContextBinding owns a pointer to a previous thread-local context");
  auto operator=(ContextBinding &&)
      -> ContextBinding & = delete ("ContextBinding owns a pointer to a previous thread-local context");

private:
  const Context *previous_{};
};

[[nodiscard]] auto currentContext() noexcept -> Option<Ref<const Context>>;

} // namespace Miracle
