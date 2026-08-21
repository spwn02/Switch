export module Switch:Session;

import std;
import Miracle;

import :Execution;
import :Task;

using namespace Miracle;

export namespace Switch::detail {

/// Describes the input bindings retained by one immutable invocation factory.
enum class[[= bitflags]] InvocationInput : u8 {
  Context = 1 << 0,
  CaseValues = 1 << 1,
  ProviderValues = 1 << 2,
  Fixtures = 1 << 3,
  Subject = 1 << 4,
};

using InvocationInputs = InvocationInput;

enum class SubjectOwnership : u8 {
  None,
  ExplicitValue,
  Fixture,
};

enum class FixtureLifetime : u8 {
  None,
  PerAttempt,
  Once,
};

/// Declares the lifetime and scheduler contraints of one immutable invocation factory.
struct InvocationCapabilities final {
  InvocationInputs inputs{};
  SubjectOwnership subjectOwnership{SubjectOwnership::None};
  FixtureLifetime fixtureLifetime{FixtureLifetime::None};
  bool sharesOnceFixture{};
  bool mutableSubject{};
  bool requiresIsolation{};
  bool measurementDependency{};
  bool attemptParallel{};
  StringView resourceLane;

  [[nodiscard]] auto has(InvocationInput input) const noexcept -> bool {
    return ::Miracle::has(inputs, input);
  }

  /// Returns whether repeated physical attempts may share a scheduler worker concurrently.
  ///
  /// Discovery normally rejects invalid annotation combinations at compile time. Keeping the complete
  /// predicate here also prevents runtime scheduling from bypassing one capability rule.
  [[nodiscard]] constexpr auto allowsParallelAttempts() const noexcept -> bool {
    return attemptParallel and not sharesOnceFixture and not mutableSubject and not measurementDependency and
           resourceLane.empty() and not requiresIsolation;
  }
};

/// Carries the per-attempt execution mode into an immutable invocation factory.
struct InvocationRequest final {
  Ref<const TestDescriptor> descriptor;
  TimeMode timeMode{TimeMode::Real};

  explicit InvocationRequest(const TestDescriptor &descriptor, TimeMode timeMode = TimeMode::Real)
      : descriptor(descriptor)
      , timeMode(timeMode) {
  }
};

/// Type-erased, immutable execution entry point for one expanded test case.
///
/// The factory owns no runner state. Concrete factories retain only immutable Case/provider values and a
/// non-owning reference to the suite fixture scope owned by RunSession.
class InvocationFactory {
public:
  virtual ~InvocationFactory() noexcept = default;

  InvocationFactory(const InvocationFactory &) = delete (
      "InvocationFactory owns immutable invocation state and cannot be copied.");
  auto operator=(const InvocationFactory &) -> InvocationFactory & = delete (
      "InvocationFactory owns immutable invocation state and cannot be copied.");
  InvocationFactory(InvocationFactory &&) noexcept = delete (
      "InvocationFactory owns immutable invocation state and cannot be copied.");
  auto operator=(InvocationFactory &&) noexcept -> InvocationFactory & = delete (
      "InvocationFactory owns immutable invocation state and cannot be copied.");

  [[nodiscard]] virtual auto invoke(const InvocationRequest &request) const -> TestExecution = 0;

  [[nodiscard]] virtual auto invokeCompact(const InvocationRequest &request) const -> AttemptOutcome {
    return makeAttemptOutcome(invoke(request), false);
  }

  virtual auto invokeBatch(const InvocationRequest &request, usize count, BatchExecutionContext &sink) const
      -> void {
    std::ranges::for_each(std::views::indices(count), [&](usize) -> void {
      if (sink.failed())
        return;
      AttemptOutcome outcome = invokeCompact(request);
      ++sink.completed;
      sink.assertions += outcome.assertions;
      sink.failedAssertions += outcome.failedAssertions;
      sink.errors += outcome.errors;
      if (outcome.passed)
        ++sink.passed;
      else
        sink.firstFailure = std::move(outcome.failure);
    });
  }

protected:
  InvocationFactory() noexcept = default;
};

/// One fully expanded reflected Case/provider combination ready for independent execution.
///
/// A planned case is move-only because it owns its immutable factory. Once materialized in a RunSession, its
/// descriptor, capabilities, and factory are observed through const accessors only.
class PlannedCase final {
public:
  explicit PlannedCase(TestDescriptor descriptor,
      InvocationCapabilities capabilities,
      std::shared_ptr<const InvocationFactory> factory)
      : descriptor_(std::move(descriptor))
      , capabilities_(capabilities)
      , factory_(std::move(factory)) {
  }

  PlannedCase(const PlannedCase &) = default;
  auto operator=(const PlannedCase &) -> PlannedCase & = default;
  PlannedCase(PlannedCase &&) noexcept = default;
  auto operator=(PlannedCase &&) noexcept -> PlannedCase & = default;
  ~PlannedCase() noexcept = default;

  [[nodiscard]] auto descriptor() const noexcept -> const TestDescriptor & {
    return descriptor_;
  }

  [[nodiscard]] auto capabilities() const noexcept -> const InvocationCapabilities & {
    return capabilities_;
  }

  [[nodiscard]] auto invoke(const InvocationRequest &request) const -> TestExecution {
    return factory_->invoke(request);
  }

  [[nodiscard]] auto invokeCompact(const InvocationRequest &request) const -> AttemptOutcome {
    return factory_->invokeCompact(request);
  }

  auto invokeBatch(const InvocationRequest &request, usize count, BatchExecutionContext &sink) const -> void {
    factory_->invokeBatch(request, count, sink);
  }

private:
  TestDescriptor descriptor_;
  InvocationCapabilities capabilities_;
  std::shared_ptr<const InvocationFactory> factory_;
};

/// Owns opaque suite-local state while its PlannedCase values execute.
///
/// Discovery materializes each suite's FixtureScope here. The storage is type-erased because every
/// reflected suite has a distinct FixtureScope<Scope> type, while RunSession schedules all of their cases
/// together.
class SuiteState final {
private:
  class Storage {
  public:
    virtual ~Storage() noexcept = default;

    Storage(const Storage &) = delete ("SuiteState storage has unique ownership.");
    auto operator=(const Storage &) -> Storage & = delete ("SuiteState storage has unique ownership.");
    Storage(Storage &&) noexcept = delete ("SuiteState storage has unique ownership.");
    auto operator=(Storage &&) noexcept -> Storage & = delete ("SuiteState storage has unique ownership.");

  protected:
    explicit Storage() = default;
  };

  template <class Value>
  class ValueStorage final : public Storage {
  public:
    template <class... Arguments>
    explicit ValueStorage(Arguments &&...arguments)
        : value_(std::forward<Arguments>(arguments)...) {
    }

    [[nodiscard]] auto value() noexcept -> Value & {
      return value_;
    }

  private:
    Value value_;
  };

public:
  explicit SuiteState(StringView scope) noexcept
      : scope_(scope) {
  }
  ~SuiteState() noexcept = default;

  SuiteState(const SuiteState &) = delete ("SuiteState owns fixture lifetime.");
  auto operator=(const SuiteState &) -> SuiteState & = delete ("SuiteState owns fixture lifetime.");
  SuiteState(SuiteState &&) noexcept = delete ("SuiteState owns fixture lifetime.");
  auto operator=(SuiteState &&) noexcept -> SuiteState & = delete ("SuiteState owns fixture lifetime.");

  [[nodiscard]] auto scope() const noexcept -> StringView {
    return scope_;
  }

  template <class Value, class... Args>
  [[nodiscard]] auto emplace(Args &&...args) -> Value & {
    auto storage = std::make_unique<ValueStorage<Value>>(std::forward<Args>(args)...);
    Value &value = storage->value();
    storage_ = std::move(storage);
    return value;
  }

private:
  StringView scope_;
  UPtr<Storage> storage_;
};

/// Collects every selected suite and its independently schedulable cases for one run.
///
/// Hive preserves SuiteState addresses as discovery appends suites. PlannedCase stays contiguous for
/// indexed worker dispatch. The member order deliberately destroys cases before suite fixture state.
class RunSession final {
public:
  RunSession() = default;
  ~RunSession() noexcept = default;

  RunSession(const RunSession &) = delete ("RunSession owns execution state.");
  auto operator=(const RunSession &) -> RunSession & = delete ("RunSession owns execution state.");
  RunSession(RunSession &&) noexcept = delete ("RunSession owns execution state.");
  auto operator=(RunSession &&) noexcept -> RunSession & = delete ("RunSession owns execution state.");

  [[nodiscard]] auto appendSuite(StringView scope) -> SuiteState & {
    return *suites_.emplace(scope);
  }

  auto appendPlannedCase(PlannedCase plannedCase) -> void {
    plannedCases_.emplace_back(std::move(plannedCase));
  }

  /// Reserves contiguous storage before the reflected executable plan is materialized.
  auto reservePlannedCases(usize count) -> void {
    plannedCases_.reserve(count);
  }

  /// Drops the planned cases matched by the predicate, keeping the survivors contiguous and ordered.
  template <class Predicate>
  auto erasePlannedCases(Predicate &&predicate) -> void {
    std::erase_if(plannedCases_, std::forward<Predicate>(predicate));
  }

  [[nodiscard]] auto takePlannedCases() -> Vec<PlannedCase> {
    return std::move(plannedCases_);
  }

private:
  Hive<SuiteState> suites_;
  Vec<PlannedCase> plannedCases_;
};

} // namespace Switch::detail
