import std;

import Miracle;
import Switch;

using namespace Miracle;
using namespace Switch;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
namespace Tests::fixtures {

namespace FixtureSubjects {

struct Transient final {
  u32 instance{};
};

struct Shared final {
  u32 instance{};
};

struct PerTest final {
  usize instance{};
};

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
inline usize transientCreations{};
inline usize sharedCreations{};
inline usize perTestCreations{};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

auto resetFixtureCounters() -> void {
  transientCreations = 0;
  sharedCreations = 0;
  perTestCreations = 0;
}

[[= fixture]] auto transient() -> Transient {
  const Option<Ref<const Context>> context = currentContext();
  ++transientCreations;
  return Transient{
      .instance = context ? static_cast<u32>(context->get().testCase + 1) : 0,
  };
}

[[ = fixture, = once ]] auto shared() -> Shared {
  ++sharedCreations;
  return Shared{
      .instance = 1,
  };
}

[[= fixture]] auto perTest() -> PerTest {
  return PerTest{
      .instance = ++perTestCreations,
  };
}

[[
  = test,
  = group("framework"),
  = tag("fixtures", "subjects"),
  = description("injects a context and both fixture lifetimes"),
  = Case{11},
  = Case{29}
]] auto receivesContext(const Context[[= context]] & ctx,
    u32[[= fromCase]] input,
    Transient transientValue,
    const Shared &sharedValue) -> void {
  const Option<Ref<const Context>> active = currentContext();
  const u32 expectedInput = ctx.testCase == 0 ? 11 : 29;

  require(active);
  require(ctx.name == "receivesContext"_exp);
  require(ctx.description == "injects a context and both fixture lifetimes"_exp);
  require(ctx.testCase < 2_exp);
  require(eq(input, expectedInput));
  require(eq(transientValue.instance, static_cast<u32>(ctx.testCase + 1)));
  require(sharedValue.instance == 1_exp);
  check(active->get().name == ctx.name);
  check(active->get().description == ctx.description);
  check(active->get().testCase == ctx.testCase);
}

[[ = test, = group("framework"), = tag("fixtures", "subjects") ]] auto reusesNormalFixtureWithinOneTest(
    PerTest first,
    const PerTest &second) -> void {
  require(eq(first.instance, second.instance));
}

[[ = test,
  = group("framework"),
  = tag("fixtures", "subjects"),
  = Case{11},
  = Case{29} ]] auto receivesAsyncContext(const Context[[= context]] & ctx, // NOLINT
    u32[[= fromCase]] input,
    const Transient &transientValue,           // NOLINT
    const Shared &sharedValue) -> Task<void> { // NOLINT
  co_await yield();

  const Option<Ref<const Context>> active = currentContext();
  const u32 expectedInput = ctx.testCase == 0 ? 11 : 29;

  require(active);
  require(ctx.name == "receivesAsyncContext"_exp);
  require(eq(input, expectedInput));
  require(eq(transientValue.instance, static_cast<u32>(ctx.testCase + 1)));
  require(sharedValue.instance == 1_exp);
  check(active->get().testCase == ctx.testCase);
}

} // namespace FixtureSubjects

namespace DependencySubjects {

enum class[[= debug::derive]] FixtureEvent : u8 {
  Repository,
  Connection,
  SharedGateway,
  SharedSettings,
};

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
inline usize settingsCreations{};
inline usize gatewayCreations{};
inline usize connectionCreations{};
inline usize repositoryCreations{};
inline Vec<FixtureEvent> destructions{};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

class LifetimeProbe final {
public:
  explicit LifetimeProbe(FixtureEvent event)
      : event_(event) {
  }

  ~LifetimeProbe() { // NOLINT
    destructions.push_back(event_);
  }

  LifetimeProbe(const LifetimeProbe &) = delete;
  auto operator=(const LifetimeProbe &) -> LifetimeProbe & = delete;
  LifetimeProbe(LifetimeProbe &&) noexcept = delete;
  auto operator=(LifetimeProbe &&) noexcept -> LifetimeProbe & = delete;

private:
  FixtureEvent event_{};
};

[[nodiscard]] auto makeProbe(FixtureEvent event) -> std::shared_ptr<LifetimeProbe> {
  return std::make_shared<LifetimeProbe>(event);
}

struct SharedSettings final {
  u32 revision{};
  std::shared_ptr<LifetimeProbe> lifetime;
};

struct SharedGateway final {
  u32 settingsRevision{};
  std::shared_ptr<LifetimeProbe> lifetime;
};

struct Connection final {
  usize instance{};
  u32 settingsRevision{};
  std::shared_ptr<LifetimeProbe> lifetime;
};

struct Repository final {
  usize connectionInstance{};
  u32 settingsRevision{};
  std::shared_ptr<LifetimeProbe> lifetime;
};

auto resetDependencyCounters() -> void {
  settingsCreations = 0;
  gatewayCreations = 0;
  connectionCreations = 0;
  repositoryCreations = 0;
  destructions.clear();
}

[[ = fixture, = once ]] auto sharedSettings() -> SharedSettings {
  ++settingsCreations;
  return SharedSettings{
      .revision = 69,
      .lifetime = makeProbe(FixtureEvent::SharedSettings),
  };
}

[[ = fixture, = once ]] auto sharedGateway(const SharedSettings &settings) -> SharedGateway {
  ++gatewayCreations;
  return SharedGateway{
      .settingsRevision = settings.revision,
      .lifetime = makeProbe(FixtureEvent::SharedGateway),
  };
}

[[= fixture]] auto connection(const SharedGateway &gateway) -> Connection {
  return Connection{
      .instance = ++connectionCreations,
      .settingsRevision = gateway.settingsRevision,
      .lifetime = makeProbe(FixtureEvent::Connection),
  };
}

[[= fixture]] auto repository(const Connection &connectionValue, const SharedSettings &settings)
    -> Repository {
  ++repositoryCreations;
  return Repository{
      .connectionInstance = connectionValue.instance,
      .settingsRevision = settings.revision,
      .lifetime = makeProbe(FixtureEvent::Repository),
  };
}

[[ = test,
  = group("framework"),
  = tag("fixtures", "subjects"),
  = Case{0},
  = Case{1} ]] auto receivesACompleteFixtureGraph(u32[[= fromCase]] expectedTestCase,
    const Repository &repositoryValue,
    const Connection &connectionValue,
    const SharedGateway &gateway,
    const SharedSettings &settings) -> void {
  const Option<Ref<const Context>> active = currentContext();

  require(active);
  require(settings.revision == 69_exp);
  require(gateway.settingsRevision == settings.revision);
  require(connectionValue.settingsRevision == settings.revision);
  require(repositoryValue.settingsRevision == settings.revision);
  require(repositoryValue.connectionInstance == connectionValue.instance);
  check(active->get().testCase == expectedTestCase);
}

[[ = test, = group("framework"), = tag("fixtures", "subjects") ]] auto receivesAResolvedDependency(
    const Connection &connectionValue,
    const SharedGateway &gateway) -> void {
  require(connectionValue.settingsRevision == gateway.settingsRevision);
}

[[ = test, = group("framework"), = tag("fixtures", "subjects") ]] auto retainsFixtureDependenciesAcrossAwait(
    const Repository &repositoryValue,              // NOLINT
    const Connection &connectionValue,              // NOLINT
    const SharedSettings &settings) -> Task<void> { // NOLINT
  co_await yield();

  require(repositoryValue.settingsRevision == settings.revision);
  check(repositoryValue.connectionInstance == connectionValue.instance);
}

} // namespace DependencySubjects

[[ = test, = group("framework"), = tag("fixtures") ]] auto directRunInjectsContext() -> void {
  const Option<Ref<const Context>> outer = currentContext();
  const auto location = std::source_location::current();
  const TestExecution execution = run(
      TestDescriptor{
          .identifier = "directContext",
          .location = location,
      },
      [location](const Context &context) -> void {
        const Option<Ref<const Context>> active = currentContext();

        require(context.name == "directContext"_exp);
        require(context.description.empty());
        require(context.testCase == 0_exp);
        require(context.location.line() == location.line());
        require(active);
        check(std::addressof(active->get()) == std::addressof(context));
      });

  require(outer);
  require(execution.passed());
  require(execution.descriptor.name == "directContext"_exp);
  require(execution.state.assertions == 6_exp);
  require(currentContext());
  check(currentContext()->get().name == outer->get().name);
}

[[ = test, = group("framework"), = tag("fixtures") ]] auto reflectedInjectionUsesFixtureScopes() -> void {
  FixtureSubjects::resetFixtureCounters();

  const Vec<TestExecution> firstRun = runAllDetailed<^^FixtureSubjects>(RunOptions{
      .isolation = CrashIsolation::InProcess,
  });
  const Vec<TestExecution> secondRun = runAllDetailed<^^FixtureSubjects>(RunOptions{
      .isolation = CrashIsolation::InProcess,
  });
  const auto passed = [](const TestExecution &execution) -> bool { return execution.passed(); };

  require(firstRun.size() == 5_exp);
  require(secondRun.size() == 5_exp);
  require(std::ranges::all_of(firstRun, passed));
  require(std::ranges::all_of(secondRun, passed));
  require(FixtureSubjects::transientCreations == 8_exp);
  require(FixtureSubjects::sharedCreations == 2_exp);
  require(FixtureSubjects::perTestCreations == 2_exp);
  check(firstRun.front().descriptor.name == "receivesContext"_exp);
  check(firstRun.front().descriptor.description == "injects a context and both fixture lifetimes"_exp);
  check(firstRun.front().descriptor.testCase == 0_exp);
  check(firstRun[1].descriptor.testCase == 1_exp);
  check(firstRun[2].descriptor.name == "reusesNormalFixtureWithinOneTest"_exp);
  check(firstRun[3].descriptor.name == "receivesAsyncContext"_exp);
}

[[ = test, = group("framework"), = tag("fixtures") ]] auto reflectedFixtureDependenciesRespectScopeLifetimes()
    -> void {
  using DependencySubjects::FixtureEvent;

  constexpr Array<FixtureEvent, 9> expectedDestructions{
      FixtureEvent::Repository,
      FixtureEvent::Connection,
      FixtureEvent::Repository,
      FixtureEvent::Connection,
      FixtureEvent::Connection,
      FixtureEvent::Repository,
      FixtureEvent::Connection,
      FixtureEvent::SharedGateway,
      FixtureEvent::SharedSettings,
  };
  const auto passed = [](const TestExecution &execution) -> bool { return execution.passed(); };

  DependencySubjects::resetDependencyCounters();
  const Vec<TestExecution> firstRun = runAllDetailed<^^DependencySubjects>(RunOptions{
      .isolation = CrashIsolation::InProcess,
  });

  require(firstRun.size() == 4_exp);
  require(std::ranges::all_of(firstRun, passed));
  require(DependencySubjects::settingsCreations == 1_exp);
  require(DependencySubjects::gatewayCreations == 1_exp);
  require(DependencySubjects::connectionCreations == 4_exp);
  require(DependencySubjects::repositoryCreations == 3_exp);
  require(std::ranges::equal(DependencySubjects::destructions, expectedDestructions));

  DependencySubjects::destructions.clear();
  const Vec<TestExecution> secondRun = runAllDetailed<^^DependencySubjects>(RunOptions{
      .isolation = CrashIsolation::InProcess,
  });

  require(secondRun.size() == 4_exp);
  require(std::ranges::all_of(secondRun, passed));
  require(DependencySubjects::settingsCreations == 2_exp);
  require(DependencySubjects::gatewayCreations == 2_exp);
  require(DependencySubjects::connectionCreations == 8_exp);
  require(DependencySubjects::repositoryCreations == 6_exp);
  check(std::ranges::equal(DependencySubjects::destructions, expectedDestructions));
}

} // namespace Tests::fixtures
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

consteval {
  discover<^^Tests::fixtures>();
}
