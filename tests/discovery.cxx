import std;
import Miracle;
import Switch;

using namespace Miracle;
using namespace Switch;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
namespace Tests::discovery {

struct MemberSubject final {
  int value{};

  auto normalizes() -> void;

  auto observesConstSubject() const -> void;
  auto acceptsCase(int expected) const -> void;
};

struct FixtureMemberSubject final {
  int value{};

  static auto make() -> FixtureMemberSubject;
  auto usesFixtureSubject() -> void;
};

[[= fixture]] auto FixtureMemberSubject::make() -> FixtureMemberSubject {
  return FixtureMemberSubject{7};
}

[[ = test, = group("framework"), = tag("discovery", "member_subject") ]] auto FixtureMemberSubject::
    usesFixtureSubject() -> void {
  check(value == 7);
  value = 8;
}

consteval auto makeMemberSubject() -> MemberSubject {
  return MemberSubject{2};
}

[[ = test, = group("framework"), = tag("discovery", "member_subject"), = subject(makeMemberSubject()) ]] auto
MemberSubject::normalizes() -> void {
  require(value == 2);
  value *= 2;
  check(value == 4);
}

[[ = test, = group("framework"), = tag("discovery", "member_subject"), = subject(makeMemberSubject()) ]] auto
MemberSubject::observesConstSubject() const -> void {
  check(value == 2);
}

[[
  = test,
  = group("framework"),
  = tag("discovery", "member_subject"),
  = subject(makeMemberSubject()),
  = Case{3}
]] auto MemberSubject::acceptsCase(int expected) const -> void {
  require(value == 2);
  check(value + expected == 5);
}

namespace PassingTests {

constexpr auto fibonacci(u32 input) -> u32 {
  switch (input) {
    case 0: return 0;
    case 1: return 1;
    default: return fibonacci(input - 2) + fibonacci(input - 1);
  }
}

[[
  = test,
  = group("framework"),
  = tag("discovery", "subjects", "passing"),
  = Case{0, 0},
  = Case{1, 1},
  = Case{5, 5}
]] constexpr auto fibonacciCases(u32 input, u32 expected) -> bool {
  return fibonacci(input) == expected;
}

[[ = test, = group("framework"), = tag("discovery", "subjects", "passing") ]] auto voidCase() -> void {
  require(true);
}

auto ignored() -> bool {
  return false;
}

} // namespace PassingTests

namespace ParallelTests {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline std::atomic<usize> active{};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline std::atomic<usize> peak{};

auto reset() -> void {
  active.store(0, std::memory_order_relaxed);
  peak.store(0, std::memory_order_relaxed);
}

auto observePeak(usize value) -> void {
  usize observed = peak.load(std::memory_order_relaxed);
  while (observed < value and not peak.compare_exchange_weak(observed, value, std::memory_order_relaxed)) {
    // pass
  }
}

[[
  = test,
  = group("framework"),
  = tag("discovery", "subjects", "parallel"),
  = Case{1},
  = Case{2},
  = Case{3},
  = Case{4}
]] auto runsConcurrently(u32 /*ignored*/) -> Task<void> {
  const usize current = active.fetch_add(1, std::memory_order_relaxed) + 1;
  observePeak(current);
  const auto cleanup = std::scope_exit([] -> void { active.fetch_sub(1, std::memory_order_relaxed); });

  co_await yield();
  std::this_thread::sleep_for(std::chrono::milliseconds{10});
  require(current <= 2_exp);
}

} // namespace ParallelTests

namespace ParallelFixtureTests {

inline constexpr u32 expectedSharedValue{69};
inline constexpr auto oneHour = std::chrono::hours{1};

struct SharedFixture final {
  u32 value{};
};

struct CaseFixture final {
  usize instance{};
  u32 sharedValue{};
};

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
inline std::atomic<usize> sharedCreations{};
inline std::atomic<usize> caseCreations{};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

auto reset() -> void {
  sharedCreations.store(0, std::memory_order_relaxed);
  caseCreations.store(0, std::memory_order_relaxed);
}

[[ = fixture, = once ]] auto sharedFixture() -> SharedFixture {
  sharedCreations.fetch_add(1, std::memory_order_relaxed);
  std::this_thread::sleep_for(std::chrono::milliseconds{10});
  return SharedFixture{
      .value = expectedSharedValue,
  };
}

[[= fixture]] auto caseFixture(const SharedFixture &shared) -> CaseFixture {
  return CaseFixture{
      .instance = caseCreations.fetch_add(1, std::memory_order_relaxed) + 1,
      .sharedValue = shared.value,
  };
}

[[
  = test,
  = group("framework"),
  = tag("discovery", "subjects", "parallel", "fixtures"),
  = Case{1},
  = Case{2},
  = Case{3},
  = Case{4}
]] auto resolvesFixtureScopesInParallel(u32[[= fromCase]] expectedCase,
    const SharedFixture &shared,                         // NOLINT
    const CaseFixture &caseFixtureValue) -> Task<void> { // NOLINT
  co_await sleepFor(oneHour);

  const Option<Ref<const Context>> context = currentContext();
  require(context);
  require(shared.value == expectedSharedValue);
  require(caseFixtureValue.sharedValue == expectedSharedValue);
  require(caseFixtureValue.instance != 0_exp);
  check(context->get().testCase + 1 == expectedCase);
}

} // namespace ParallelFixtureTests

namespace CrossSuiteState {

inline constexpr auto overlapTimeout = std::chrono::seconds{1};
inline constexpr auto overlapWindow = std::chrono::milliseconds{10};

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
inline std::atomic<usize> active{};
inline std::atomic<usize> peak{};
inline std::atomic<usize> expectedWorkers{};
inline std::atomic<usize> leftFixtureCreations{};
inline std::atomic<usize> rightFixtureCreations{};
inline std::atomic<usize> fixtureDestructions{};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

class LifetimeProbe final {
public:
  LifetimeProbe() = default;

  ~LifetimeProbe() { // NOLINT
    fixtureDestructions.fetch_add(1, std::memory_order_relaxed);
  }

  LifetimeProbe(const LifetimeProbe &) = delete ("LifetimeProbe is test-only state.");
  auto operator=(const LifetimeProbe &) -> LifetimeProbe & = delete ("LifetimeProbe is test-only state.");
  LifetimeProbe(LifetimeProbe &&) = delete ("LifetimeProbe is test-only state.");
  auto operator=(LifetimeProbe &&) -> LifetimeProbe & = delete ("LifetimeProbe is test-only state.");
};

auto reset(usize workerCount) -> void {
  active.store(0, std::memory_order_relaxed);
  peak.store(0, std::memory_order_relaxed);
  expectedWorkers.store(workerCount, std::memory_order_relaxed);
  leftFixtureCreations.store(0, std::memory_order_relaxed);
  rightFixtureCreations.store(0, std::memory_order_relaxed);
  fixtureDestructions.store(0, std::memory_order_relaxed);
}

auto observePeak(usize value) -> void {
  usize observed = peak.load(std::memory_order_relaxed);
  while (observed < value and not peak.compare_exchange_weak(observed, value, std::memory_order_relaxed)) {
    // pass
  }
}

auto runConcurrently() -> void {
  const usize current = active.fetch_add(1, std::memory_order_relaxed) + 1;
  observePeak(current);
  const auto cleanup = std::scope_exit([] -> void { active.fetch_sub(1, std::memory_order_relaxed); });
  const auto deadline = std::chrono::steady_clock::now() + overlapTimeout;

  while (active.load(std::memory_order_relaxed) < expectedWorkers.load(std::memory_order_relaxed) and
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }

  std::this_thread::sleep_for(overlapWindow);
}

} // namespace CrossSuiteState

namespace CrossSuiteLeft {

struct Fixture final {
  std::shared_ptr<CrossSuiteState::LifetimeProbe> lifetime;
};

[[ = fixture, = once ]] auto fixture() -> Fixture {
  CrossSuiteState::leftFixtureCreations.fetch_add(1, std::memory_order_relaxed);
  return Fixture{
      .lifetime = std::make_shared<CrossSuiteState::LifetimeProbe>(),
  };
}

[[ = test, = group("framework"), = tag("discovery", "subjects", "cross_suite", "left") ]] auto
runsInLeftSuite(const Fixture &fixtureValue) -> void {
  require(fixtureValue.lifetime);
  CrossSuiteState::runConcurrently();
}

} // namespace CrossSuiteLeft

namespace CrossSuiteRight {

struct Fixture final {
  std::shared_ptr<CrossSuiteState::LifetimeProbe> lifetime;
};

[[ = fixture, = once ]] auto fixture() -> Fixture {
  CrossSuiteState::rightFixtureCreations.fetch_add(1, std::memory_order_relaxed);
  return Fixture{
      .lifetime = std::make_shared<CrossSuiteState::LifetimeProbe>(),
  };
}

[[ = test, = group("framework"), = tag("discovery", "subjects", "cross_suite", "right") ]] auto
runsInRightSuite(const Fixture &fixtureValue) -> void {
  require(fixtureValue.lifetime);
  CrossSuiteState::runConcurrently();
}

} // namespace CrossSuiteRight

namespace FailingTests {

[[
  = test,
  = group("framework"),
  = tag("discovery", "subjects", "failing"),
  = Case{2, 2},
  = Case{3, 2}
]] constexpr auto identityCases(u32 input, u32 expected) -> bool {
  return input == expected;
}

} // namespace FailingTests

namespace SelectionSubjects {

[[ = test, = group("math"), = tag("unit", "fast") ]] constexpr auto adds() -> void {
  require(1U + 1 == 2_exp);
}

[[ = test, = group("math"), = tag("unit", "slow") ]] auto multiplies() -> void {
  require(2U * 3 == 6_exp);
}

[[ = test, = group("filesystem"), = tag("integration") ]] auto reads() -> void {
  require(true);
}

} // namespace SelectionSubjects

[[ = test, = group("framework"), = tag("discovery") ]] auto discoversEachDeclarativeCase() -> void {
  constexpr usize expectedCases{4};
  const Vec<TestDescriptor> descriptors = describe<^^PassingTests>();

  require(eq(descriptors.size(), expectedCases));
  check(descriptors.front().identifier == "Tests::discovery::PassingTests::fibonacciCases(0, 0)"_exp);
  check(descriptors[1].identifier == "Tests::discovery::PassingTests::fibonacciCases(1, 1)"_exp);
  check(descriptors[2].identifier == "Tests::discovery::PassingTests::fibonacciCases(5, 5)"_exp);
  check(descriptors.back().identifier == "Tests::discovery::PassingTests::voidCase"_exp);
}

[[ = test, = group("framework"), = tag("discovery") ]] auto dispatchesParameterisedTests() -> void {
  constexpr usize expectedCases{4};
  constexpr usize expectedAssertions{4};
  const Vec<TestExecution> executions =
      runAllDetailed<^^PassingTests>(RunOptions{.isolation = CrashIsolation::InProcess});
  RunAccumulator accumulator{RetentionPolicy::All};
  for (const TestExecution &execution : executions)
    accumulator.append(execution);
  const TestSummary summary = Reporter::summarize(std::move(accumulator).finish());

  check(executions.size() == expectedCases);
  check(summary.testCount == expectedCases);
  check(summary.passedCount == expectedCases);
  check(summary.assertionCount == expectedAssertions);
  check(summary.passed());
}

[[ = test, = group("framework"), = tag("discovery") ]] auto preservesCaseIdentityForFailures() -> void {
  constexpr usize expectedCases{2};
  constexpr usize expectedPassed{1};
  constexpr usize expectedFailed{1};
  const Vec<TestExecution> executions =
      runAllDetailed<^^FailingTests>(RunOptions{.isolation = CrashIsolation::InProcess});
  RunAccumulator accumulator{RetentionPolicy::All};
  for (const TestExecution &execution : executions)
    accumulator.append(execution);
  const TestSummary summary = Reporter::summarize(std::move(accumulator).finish());

  require(executions.size() == expectedCases);
  require(summary.passedCount == expectedPassed);
  require(summary.failedCount == expectedFailed);
  require(executions.back().failed());
  check(executions.back().descriptor.identifier == "Tests::discovery::FailingTests::identityCases(3, 2)"_exp);
  check(executions.back().state.diagnostics.front().description() == "test returned false"_exp);
}

[[ = test, = group("framework"), = tag("discovery") ]] auto dispatchesIndependentCasesInParallel() -> void {
  constexpr usize workerCount{2};
  constexpr usize expectedCases{4};
  ParallelTests::reset();
  const Vec<TestExecution> executions = runAllDetailed<^^ParallelTests>(RunOptions{
      .threads = workerCount,
      .isolation = CrashIsolation::InProcess,
  });
  RunAccumulator accumulator{RetentionPolicy::All};
  for (const TestExecution &execution : executions)
    accumulator.append(execution);
  const TestSummary summary = Reporter::summarize(std::move(accumulator).finish());

  require(executions.size() == expectedCases);
  require(summary.passedCount == expectedCases);
  check(ParallelTests::peak.load(std::memory_order_relaxed) >= workerCount);
  check(executions.front().descriptor.identifier == "Tests::discovery::ParallelTests::runsConcurrently(1)");
  check(executions.back().descriptor.identifier == "Tests::discovery::ParallelTests::runsConcurrently(4)");
}

[[ = test, = group("framework"), = tag("discovery") ]] auto
preservesFixtureScopesAndVirtualTimeAcrossParallelCases() -> void {
  constexpr usize workerCount{4};
  constexpr usize expectedCases{4};
  const auto passed = [](const TestExecution &execution) -> bool { return execution.passed(); };

  ParallelFixtureTests::reset();
  const Vec<TestExecution> executions = runAllDetailed<^^ParallelFixtureTests>(RunOptions{
      .threads = workerCount,
      .timeMode = TimeMode::Virtual,
      .isolation = CrashIsolation::InProcess,
  });

  require(executions.size() == expectedCases);
  require(std::ranges::all_of(executions, passed));
  require(ParallelFixtureTests::sharedCreations.load(std::memory_order_relaxed) == 1_exp);
  require(ParallelFixtureTests::caseCreations.load(std::memory_order_relaxed) == expectedCases);
  require(std::ranges::all_of(executions, [](const TestExecution &execution) -> bool {
    return execution.duration == ParallelFixtureTests::oneHour;
  }));
  check(executions.front().descriptor.identifier ==
        "Tests::discovery::ParallelFixtureTests::resolvesFixtureScopesInParallel(1)"_exp);
  check(executions.back().descriptor.identifier ==
        "Tests::discovery::ParallelFixtureTests::resolvesFixtureScopesInParallel(4)"_exp);
}

[[ = test, = group("framework"), = tag("discovery") ]] auto
flattensIndependentSuitesAndUsesAutomaticWorkerCount() -> void {
  constexpr usize expectedCases{2};
  const usize available = std::max(static_cast<usize>(std::thread::hardware_concurrency()), usize{1});
  const usize expectedWorkers = std::min(available, expectedCases);
  const auto passed = [](const TestExecution &execution) -> bool { return execution.passed(); };
  Vec<TestExecution> executions{};

  CrossSuiteState::reset(expectedWorkers);
  {
    detail::RunSession session{};
    detail::suiteEntry<^^CrossSuiteLeft, detail::DiscoveryConfiguration<>>.plan(session);
    detail::suiteEntry<^^CrossSuiteRight, detail::DiscoveryConfiguration<>>.plan(session);
    executions = detail::executePlannedCases(
        session, RunOptions{.threads = 0, .isolation = CrashIsolation::InProcess});

    require(executions.size() == expectedCases);
    require(std::ranges::all_of(executions, passed));
    require(CrossSuiteState::leftFixtureCreations.load(std::memory_order_relaxed) == 1_exp);
    require(CrossSuiteState::rightFixtureCreations.load(std::memory_order_relaxed) == 1_exp);
    require(CrossSuiteState::fixtureDestructions.load(std::memory_order_relaxed) == 0_exp);
  }

  require(CrossSuiteState::peak.load(std::memory_order_relaxed) == expectedWorkers);
  require(CrossSuiteState::fixtureDestructions.load(std::memory_order_relaxed) == expectedCases);
  check(executions.front().descriptor.identifier == "Tests::discovery::CrossSuiteLeft::runsInLeftSuite"_exp);
  check(executions.back().descriptor.identifier == "Tests::discovery::CrossSuiteRight::runsInRightSuite"_exp);
}

[[ = test, = group("framework"), = tag("discovery") ]] auto selectsTestsByQualifiedNameGroupAndTags()
    -> void {
  constexpr usize expectedDescriptors{3};
  const TestSelection fastMath{
      .include = {"Tests::discovery::SelectionSubjects::*"},
      .tagsAll = {"unit", "fast"},
      .group = String{"math"},
  };
  const TestSelection excluded{
      .include = {"Tests::discovery::SelectionSubjects::*"},
      .exclude = {"*adds"},
      .tagsAny = {"fast", "slow"},
  };
  const Vec<TestDescriptor> all = list<^^SelectionSubjects>();
  const Vec<TestDescriptor> selected = list<^^SelectionSubjects>(fastMath);
  const Vec<TestDescriptor> withoutAdds = list<^^SelectionSubjects>(excluded);
  const Vec<TestExecution> executions =
      runAllDetailed<^^SelectionSubjects>(fastMath, RunOptions{.isolation = CrashIsolation::InProcess});
  const RunReport selectedReport = detail::runScopeReport<^^SelectionSubjects, detail::DiscoveryConfiguration<>>(fastMath,
      RunOptions{.isolation = CrashIsolation::InProcess});

  require(all.size() == expectedDescriptors);
  require(selected.size() == 1_exp);
  require(withoutAdds.size() == 1_exp);
  require(executions.size() == 1_exp);
  require(selectedReport.selection.tagsAll == fastMath.tagsAll);
  check(selectedReport.selection.tagsAny.empty());
  check(selected.front().identifier == "Tests::discovery::SelectionSubjects::adds"_exp);
  check(selected.front().metadata.group == String{"math"});
  check(selected.front().metadata.tags == Vec<String>{"unit", "fast"});
  check(withoutAdds.front().identifier == "Tests::discovery::SelectionSubjects::multiplies"_exp);
  check(executions.front().descriptor.identifier == "Tests::discovery::SelectionSubjects::adds"_exp);
}

} // namespace Tests::discovery
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

consteval {
  discover<^^Tests::discovery>();
  discover<^^Tests::discovery::MemberSubject>();
  discover<^^Tests::discovery::FixtureMemberSubject>();
}
