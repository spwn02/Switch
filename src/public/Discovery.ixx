export module Switch:Discovery;

import std;
import Miracle;

import :Annotations;
import :Context;
import :Diagnostics;
import :Environment;
import :Execution;
import :Fixtures;
import :Metadata;
import :Policies;
import :Providers;
import :Reporting;
import :Runner;
import :Task;

using namespace Miracle;

// NOLINTBEGIN(bugprone-reserved-identifier)
export namespace Switch {

/// Includes tests declared in nested namespaces below a registered namespace.
struct Recursive final {};

inline constexpr Recursive recursive{};

/// Enables discovery of member functions when the registered scope is a class. Non-static members  must bind
/// an explicit subject value or a matching fixture provider.
struct StaticMemberFunctions final {};

inline constexpr StaticMemberFunctions staticMemberFunctions{};

/// Describes which reflected test cases participate in a list or run operation.
///
/// Include patterns us '*' and '?' globs over fully qualified identifiers. Excludes always win. tagsAll
/// requires every requested tag; tagsAny requires at least one requested tag when it is non-empty.
struct TestSelection final {
  Vec<String> include;
  Vec<String> exclude;
  Vec<String> tagsAll;
  Vec<String> tagsAny;
  Option<String> group;

  [[nodiscard]] auto matches(const TestDescriptor &descriptor) const -> bool;
};

/// Immutable compile-time metadata emitted once for every registered suite.
struct SuiteEntry final {
  using DescriptorFactory = Vec<TestDescriptor> (*)();
  using PlanFactory = void (*)(detail::RunSession &);

  StringView scope;
  std::source_location location;
  DescriptorFactory describe;
  PlanFactory plan;
};

namespace detail {

[[nodiscard]] auto filterDescriptors(Vec<TestDescriptor> descriptors, const TestSelection &selection)
    -> Vec<TestDescriptor>;

auto filterPlannedCases(RunSession &session, const TestSelection &selection) -> void;

template <class Option>
concept DiscoveryOption = std::same_as<std::remove_cvref_t<Option>, Recursive> or
                          std::same_as<std::remove_cvref_t<Option>, StaticMemberFunctions>;

template <class... Options>
struct DiscoveryConfiguration final {
  static constexpr usize recursiveCount_ =
      (usize{} + ... + (std::same_as<Options, Recursive> ? usize{1} : usize{}));
  static constexpr usize staticMemberFunctionsCount_ =
      (usize{} + ... + (std::same_as<Options, StaticMemberFunctions> ? usize{1} : usize{}));

  static_assert(recursiveCount_ <= 1, "Switch discover() accepts the reqursive option at most once.");
  static_assert(staticMemberFunctionsCount_ <= 1,
      "Switch discover() accepts the staticMemberFunctions option at most once.");

  static constexpr bool recursive_{recursiveCount_ != 0};
  static constexpr bool staticMemberFunctions_{staticMemberFunctionsCount_ != 0};
};

/// Appends immutable suite metadata to the process-wide automatic catalog.
///
/// This is intentionally a runtime operation. discover() materializes a registration anchor at compile time;
/// the anchor invokes this function during ordinary static initialization before main().
auto appendRegisteredSuite(const SuiteEntry &suite) -> void;

/// Registers a scope that may need to be reconstructed by an exec'd test worker.
auto appendWorkerSuite(const SuiteEntry &suite) -> void;

template <std::meta::info Entity>
consteval auto appendQualifiedName(String &result) -> void {
  if constexpr (Entity != ^^::) {
    constexpr std::meta::info parent = std::meta::parent_of(Entity);
    if constexpr (parent != ^^::) {
      appendQualifiedName<parent>(result);
      if (not result.empty())
        result.append("::");
    }

    result.append(std::meta::identifier_of(Entity));
  }
}

template <std::meta::info Entity>
consteval auto qualifiedNameOf() -> StringView {
  if constexpr (Entity == ^^::) {
    return "<global>";
  } else {
    String result{};
    appendQualifiedName<Entity>(result);
    return StringView{std::define_static_string(StringView{result}), result.size()};
  }
}

template <std::meta::info Scope>
consteval auto scopeLocationOf() -> std::source_location {
  if constexpr (Scope == ^^::) {
    return {};
  } else {
    return std::meta::source_location_of(Scope);
  }
}

template <std::meta::info Function>
consteval auto isTest() -> bool {
  if constexpr (not std::meta::is_function(Function))
    return false;
  else
    return ReflectedFunctionMetadata<Function>::testMarkers.size() != 0;
}

template <std::meta::info Function>
consteval auto caseCount() -> usize {
  return ReflectedFunctionMetadata<Function>::cases.size();
}

template <std::meta::info Function>
consteval auto descriptionOf() -> StringView {
  if constexpr (ReflectedFunctionMetadata<Function>::descriptions.empty()) {
    return {};
  } else {
    constexpr std::meta::info annotation = ReflectedFunctionMetadata<Function>::descriptions.front();
    using Annotation = meta::TypeObject<annotation>;

    return std::meta::extract<Annotation>(annotation).apply();
  }
}

template <std::meta::info Function>
consteval auto shouldPanicCount() -> usize {
  return ReflectedFunctionMetadata<Function>::expectedPanics.size();
}

template <std::meta::info Function>
consteval auto timeoutCount() -> usize {
  return ReflectedFunctionMetadata<Function>::timeouts.size();
}

template <std::meta::info Function>
consteval auto repeatCount() -> usize {
  return ReflectedFunctionMetadata<Function>::repeats.size();
}

template <std::meta::info Function>
consteval auto warmupCount() -> usize {
  return ReflectedFunctionMetadata<Function>::warmups.size();
}

template <std::meta::info Function>
consteval auto retryCount() -> usize {
  return ReflectedFunctionMetadata<Function>::retries.size();
}

template <std::meta::info Function>
consteval auto groupCount() -> usize {
  return ReflectedFunctionMetadata<Function>::groups.size();
}

template <std::meta::info Function>
auto metadataOf() -> TestMetadata {
  static_assert(
      groupCount<Function>() <= 1, "Switch tests may declare at most one [[= group(...)]] annotation.");

  TestMetadata metadata{};
  const auto appendTag = [&metadata](StringView tagName) -> void {
    const bool alreadyPresent = std::ranges::any_of(
        metadata.tags, [tagName](const String &candidate) -> bool { return candidate == tagName; });
    if (not alreadyPresent)
      metadata.tags.emplace_back(tagName);
  };

  template for (constexpr std::meta::info annotation : ReflectedFunctionMetadata<Function>::groups) {
    using Annotation = meta::TypeObject<annotation>;

    constexpr Annotation groupAnnotation = std::meta::extract<Annotation>(annotation);
    metadata.group = String{groupAnnotation.apply()};
  }

  template for (constexpr std::meta::info annotation : ReflectedFunctionMetadata<Function>::tags) {
    using Annotation = meta::TypeObject<annotation>;

    constexpr Annotation tagAnnotation = std::meta::extract<Annotation>(annotation);
    tagAnnotation.apply([&appendTag](const auto &...tags) -> void { (appendTag(tags.apply()), ...); });
  }

  return metadata;
}

template <std::meta::info Function>
auto policyOf() -> TestPolicy {
  static_assert(shouldPanicCount<Function>() <= 1,
      "Switch tests may declare at most one [[= shouldPanic(...)]] annotation.");
  static_assert(
      timeoutCount<Function>() <= 1, "Switch tests may declare at most one [[= timeout(...)]] annotation.");
  static_assert(
      repeatCount<Function>() <= 1, "Switch tests may declare at most one [[= repeat(...)]] annotation.");
  static_assert(
      warmupCount<Function>() <= 1, "Switch tests may declare at most one [[= warmup(...)]] annotation.");
  static_assert(
      retryCount<Function>() <= 1, "Switch tests may declare at most one [[= retry(...)]] annotation.");

  constexpr bool isolated = ReflectedFunctionMetadata<Function>::isolated.size() != 0;
  constexpr bool parent = ReflectedFunctionMetadata<Function>::parents.size() != 0;
  constexpr bool trace = ReflectedFunctionMetadata<Function>::traces.size() != 0;
  static_assert(not(isolated and parent), "Switch tests cannot combine [[= isolated]] and [[= parent]]");

  TestPolicy policy{
      .trace = trace,
      .isolated = isolated,
      .parent = parent,
  };

  template for (constexpr std::meta::info annotation : ReflectedFunctionMetadata<Function>::expectedPanics) {
    using Annotation = meta::TypeObject<annotation>;

    constexpr Annotation expected = std::meta::extract<Annotation>(annotation);
    policy.expectedPanic = String{expected.apply()};
  }

  template for (constexpr std::meta::info annotation : ReflectedFunctionMetadata<Function>::timeouts) {
    constexpr auto limit = std::meta::extract<Timeout>(annotation);
    policy.timeout = std::chrono::duration_cast<std::chrono::steady_clock::duration>(limit.apply());
  }

  template for (constexpr std::meta::info annotation : ReflectedFunctionMetadata<Function>::repeats)
      policy.repeat = std::meta::extract<Repeat>(annotation).apply();

  template for (constexpr std::meta::info annotation : ReflectedFunctionMetadata<Function>::warmups)
      policy.warmup = std::meta::extract<Warmup>(annotation).apply();

  template for (constexpr std::meta::info annotation : ReflectedFunctionMetadata<Function>::retries)
      policy.retry = std::meta::extract<Retry>(annotation).apply();

  return policy;
}

template <std::meta::info Function>
auto makeTestDescriptor(usize testCase, StringView caseDescription = {}, StringView providerDescription = {})
    -> TestDescriptor {
  constexpr StringView name = meta::identifier<Function>;
  constexpr StringView qualifiedName = qualifiedNameOf<Function>();
  constexpr std::source_location location = std::meta::source_location_of(Function);
  constexpr StringView description = descriptionOf<Function>();

  String identifier{qualifiedName};

  if (not caseDescription.empty() or not providerDescription.empty()) {
    identifier.append("(");
    if (not caseDescription.empty())
      identifier.append(caseDescription);

    if (not caseDescription.empty() and not providerDescription.empty())
      identifier.append(", ");

    if (not providerDescription.empty())
      identifier.append(providerDescription);

    identifier.append(")");
  }

  TestPolicy policy = policyOf<Function>();
  TestMetadata metadata = metadataOf<Function>();

  return TestDescriptor{
      .identifier = std::move(identifier),
      .location = location,
      .name = String{name},
      .description = String{description},
      .testCase = testCase,
      .policy = std::move(policy),
      .metadata = std::move(metadata),
  };
}

template <std::meta::info Function>
auto appendProviderDescriptors(Vec<TestDescriptor> &descriptors,
    StringView caseDescription,
    usize &testCaseIndex) -> void {
  const usize providerCount = detail::forEachProviderCombination<Function>(
      [&descriptors, caseDescription, &testCaseIndex](const auto &...providerValues) -> void {
        const String providerDescription = detail::providerDescription<Function>(providerValues...);
        descriptors.push_back(
            makeTestDescriptor<Function>(testCaseIndex, caseDescription, StringView{providerDescription}));
        ++testCaseIndex;
      });

  if (providerCount != 0)
    return;

  const String providerDescription = detail::missingProviderDescription<Function>();
  descriptors.push_back(
      makeTestDescriptor<Function>(testCaseIndex, caseDescription, StringView{providerDescription}));
  ++testCaseIndex;
}

template <std::meta::info Function, std::meta::info Annotation>
auto appendCaseDescriptors(Vec<TestDescriptor> &descriptors, usize &testCaseIndex) -> void {
  using Ann = meta::TypeObject<Annotation>;
  constexpr Ann testCase = std::meta::extract<Ann>(Annotation);
  const String caseDescription = testCase.describe();

  appendProviderDescriptors<Function>(descriptors, caseDescription, testCaseIndex);
}

template <std::meta::info Function>
auto appendDescriptors(Vec<TestDescriptor> &descriptors) -> void {
  usize testCaseIndex{};

  if constexpr (caseCount<Function>() == 0) {
    appendProviderDescriptors<Function>(descriptors, {}, testCaseIndex);
    return;
  }

  template for (constexpr std::meta::info annotation : ReflectedFunctionMetadata<Function>::cases) {
    appendCaseDescriptors<Function, annotation>(descriptors, testCaseIndex);
  }
}

template <class CaseType>
[[nodiscard]] auto caseValues(const CaseType &testCase) -> auto {
  return testCase.apply([](const auto &...values) -> auto { return std::make_tuple(values...); });
}

template <std::meta::info Namespace, std::meta::info Function>
consteval auto isNonStaticMember() -> bool {
  if constexpr (std::meta::is_namespace(Namespace))
    return false;
  else
    return not std::meta::is_static_member(Function);
}

template <std::meta::info Namespace, std::meta::info Function>
consteval auto memberSubjectUsesOnceFixture() -> bool {
  if constexpr (std::meta::is_namespace(Namespace) or not isNonStaticMember<Namespace, Function>() or
                ReflectedFunctionMetadata<Function>::subjects.size() != 0) {
    return false;
  } else {
    using Subject = meta::TypeObject<std::meta::parent_of(Function)>;
    return isOnce<fixtureFor<Namespace, Subject>()>();
  }
}

template <std::meta::info Namespace, std::meta::info Function>
consteval auto hasValidMemberSubject() -> bool {
  if constexpr (not isNonStaticMember<Namespace, Function>() or
                ReflectedFunctionMetadata<Function>::subjects.size() != 0) {
    return true;
  } else {
    using Subject = meta::TypeObject<std::meta::parent_of(Function)>;
    return hasFixtureFor<Namespace, Subject>();
  }
}

template <std::meta::info Namespace, std::meta::info Function>
consteval auto invocationCapabilities() // NOLINT(readability-function-cognitive-complexity)
    -> InvocationCapabilities {
  InvocationInputs inputs{};

  if constexpr (hasContextParameter<Function>())
    inputs |= InvocationInput::Context;

  if constexpr (caseParameterCount<Function>() != 0 or
                (usesLegacyCaseBinding<Namespace, Function>() and
                    ReflectedFunctionMetadata<Function>::parameterCount != 0))
    inputs |= InvocationInput::CaseValues;

  if constexpr (providerParameterCount<Function>() != 0)
    inputs |= InvocationInput::ProviderValues;

  if constexpr (hasAutomaticFixtureParameter<Namespace, Function>())
    inputs |= InvocationInput::Fixtures;

  constexpr bool member = isNonStaticMember<Namespace, Function>();
  constexpr usize subjectCount = ReflectedFunctionMetadata<Function>::subjects.size();
  constexpr usize resourceCount = ReflectedFunctionMetadata<Function>::resources.size();
  constexpr bool parallelAttempts = ReflectedFunctionMetadata<Function>::parallelAttempts.size() != 0;

  static_assert(resourceCount <= 1, "Switch tests accept at most one [[= resource(\"lane\")]] annotation.");

  static_assert(subjectCount <= 1, "Switch member tests accept at most one [[= subject(...)]] annotation.");

  static_assert(hasValidMemberSubject<Namespace, Function>(),
      "Switch non-static member tests require [[= subject(...)]] or a matching [[= fixture]].");

  static_assert(not std::meta::is_namespace(Namespace) or not member,
      "Switch non-static member tests must be discovered from their owning class scope.");

  static_assert(not parallelAttempts or (not member and resourceCount == 0 and
                                            not hasAutomaticFixtureParameter<Namespace, Function>() and
                                            not hasOnceFixtureParameter<Namespace, Function>()),
      "Switch [[= parallelAttempts]] requires an independent case without shared fixtures, subjects, or "
      "lanes.");

  if constexpr (member and subjectCount != 0) {
    constexpr std::meta::info annotation = ReflectedFunctionMetadata<Function>::subjects.front();
    using Annotation = meta::TypeObject<annotation>;
    constexpr Annotation subjectAnnotation = std::meta::extract<Annotation>(annotation);
    using SubjectValue = std::remove_cvref_t<decltype(subjectAnnotation.value())>;
    using SubjectType = meta::TypeObject<std::meta::parent_of(Function)>;

    static_assert(std::same_as<SubjectValue, SubjectType>,
        "Switch [[= subject(...)]] must provide the member function's owning object type.");
  }

  if constexpr (member)
    inputs |= InvocationInput::Subject;

  StringView resourceLane{};
  if constexpr (resourceCount != 0) {
    constexpr std::meta::info annotation = ReflectedFunctionMetadata<Function>::resources.front();
    using Annotation = meta::TypeObject<annotation>;
    constexpr Annotation resourceAnnotation = std::meta::extract<Annotation>(annotation);
    resourceLane = resourceAnnotation.apply();
  }

  constexpr bool sharesOnceFixture =
      hasOnceFixtureParameter<Namespace, Function>() or memberSubjectUsesOnceFixture<Namespace, Function>();

  if (resourceLane.empty() and sharesOnceFixture)
    resourceLane = qualifiedNameOf<Namespace>();

  SubjectOwnership subjectOwnership = SubjectOwnership::None;

  if constexpr (member) {
    if constexpr (subjectCount != 0)
      subjectOwnership = SubjectOwnership::ExplicitValue;
    else
      subjectOwnership = SubjectOwnership::Fixture;
  }

  FixtureLifetime fixtureLifetime = FixtureLifetime::None;

  if constexpr (sharesOnceFixture)
    fixtureLifetime = FixtureLifetime::Once;
  else if constexpr (hasAutomaticFixtureParameter<Namespace, Function>() or (member and subjectCount == 0))
    fixtureLifetime = FixtureLifetime::PerAttempt;

  return InvocationCapabilities{
      .inputs = inputs,
      .subjectOwnership = subjectOwnership,
      .fixtureLifetime = fixtureLifetime,
      .sharesOnceFixture = sharesOnceFixture,
      .mutableSubject = member,
      .requiresIsolation = ReflectedFunctionMetadata<Function>::isolated.size() != 0,
      .measurementDependency = repeatCount<Function>() != 0 or warmupCount<Function>() != 0,
      .attemptParallel = parallelAttempts,
      .resourceLane = resourceLane,
  };
}

template <std::meta::info Function>
auto noProviderExecution(TestDescriptor descriptor, TimeMode timeMode) -> TestExecution {
  constexpr std::source_location location = detail::firstProviderLocation<Function>();
  return run(
      std::move(descriptor),
      [location] -> void {
        const Option<Ref<TestEnvironment>> environment = currentEnvironment();
        if (not environment)
          fatal("Switch could not report an empty provider without an active environment");

        Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::ProviderProducedNoValues, location);
        diagnostic.details.spans.front().label = "provider";
        diagnostic.addNote("no reflected parameter-provider values were produced");
        environment->get().recordError(std::move(diagnostic));
      },
      timeMode);
}

template <std::meta::info Function>
class MissingProviderInvocationFactory final : public InvocationFactory {
public:
  [[nodiscard]] auto invoke(const InvocationRequest &request) const -> TestExecution override {
    return noProviderExecution<Function>(request.descriptor.get(), request.timeMode);
  }

  [[nodiscard]] auto invokeCompact(const InvocationRequest &request) const -> AttemptOutcome override {
    return runCompact(
        request.descriptor.get(),
        [] -> void {
          const Option<Ref<TestEnvironment>> environment = currentEnvironment();
          if (not environment)
            fatal("Switch could not report an empty provider without an active environment");
          Diagnostic diagnostic = makeDiagnostic(
              DiagnosticCode::ProviderProducedNoValues, detail::firstProviderLocation<Function>());
          diagnostic.details.spans.front().label = "provider";
          diagnostic.addNote("no reflected parameter-provider values were produced");
          environment->get().recordError(std::move(diagnostic));
        },
        request.timeMode);
  }

  auto invokeBatch(const InvocationRequest &request, usize count, BatchExecutionContext &sink) const
      -> void override {
    runBatch(
        request.descriptor.get(),
        [] -> void {
          const Option<Ref<TestEnvironment>> environment = currentEnvironment();
          if (not environment)
            fatal("Switch could not report an empty provider without an active environment");
          environment->get().recordError(makeDiagnostic(
              DiagnosticCode::ProviderProducedNoValues, detail::firstProviderLocation<Function>()));
        },
        request.timeMode,
        count,
        sink);
  }
};

template <std::meta::info Namespace, std::meta::info Function, class CaseValues, class ProviderValues>
class ReflectedInvocationFactory final : public InvocationFactory {
public:
  ReflectedInvocationFactory(CaseValues caseValues,
      ProviderValues providerValues,
      FixtureScope<Namespace> &suiteFixtures)
      : caseValues_(std::move(caseValues))
      , providerValues_(std::move(providerValues))
      , suiteFixtures_(suiteFixtures) {
  }

  [[nodiscard]] auto invoke(const InvocationRequest &request) const -> TestExecution override {
    return run(
        request.descriptor.get(),
        [this](const Context &context) -> decltype(auto) {
          return detail::invokeWithFixtures<Namespace, Function>(
              context, suiteFixtures_, caseValues_, providerValues_);
        },
        request.timeMode);
  }

  [[nodiscard]] auto invokeCompact(const InvocationRequest &request) const -> AttemptOutcome override {
    return runCompact(
        request.descriptor.get(),
        [this](const Context &context) -> decltype(auto) {
          return detail::invokeWithFixtures<Namespace, Function>(
              context, suiteFixtures_, caseValues_, providerValues_);
        },
        request.timeMode);
  }

  auto invokeBatch(const InvocationRequest &request, usize count, BatchExecutionContext &sink) const
      -> void override {
    runBatch(
        request.descriptor.get(),
        [this](const Context &context) -> decltype(auto) {
          return detail::invokeWithFixtures<Namespace, Function>(
              context, suiteFixtures_, caseValues_, providerValues_);
        },
        request.timeMode,
        count,
        sink);
  }

private:
  const CaseValues caseValues_;
  const ProviderValues providerValues_;
  Ref<FixtureScope<Namespace>> suiteFixtures_;
};

template <std::meta::info Namespace,
    std::meta::info Function,
    class SubjectValue,
    class CaseValues,
    class ProviderValues>
class MemberInvocationFactory final : public InvocationFactory {
public:
  MemberInvocationFactory(SubjectValue subjectValue,
      CaseValues caseValues,
      ProviderValues providerValues,
      FixtureScope<Namespace> &suiteFixtures)
      : subject_(std::move(subjectValue))
      , caseValues_(std::move(caseValues))
      , providerValues_(std::move(providerValues))
      , suiteFixtures_(suiteFixtures) {
  }

  [[nodiscard]] auto invoke(const InvocationRequest &request) const -> TestExecution override {
    return run(
        request.descriptor.get(),
        [this](const Context &context) -> decltype(auto) {
          return detail::invokeMemberWithFixtures<Namespace, Function>(
              context, suiteFixtures_, subject_, caseValues_, providerValues_);
        },
        request.timeMode);
  }

  [[nodiscard]] auto invokeCompact(const InvocationRequest &request) const -> AttemptOutcome override {
    return runCompact(
        request.descriptor.get(),
        [this](const Context &context) -> decltype(auto) {
          return detail::invokeMemberWithFixtures<Namespace, Function>(
              context, suiteFixtures_, subject_, caseValues_, providerValues_);
        },
        request.timeMode);
  }

  auto invokeBatch(const InvocationRequest &request, usize count, BatchExecutionContext &sink) const
      -> void override {
    runBatch(
        request.descriptor.get(),
        [this](const Context &context) -> decltype(auto) {
          return detail::invokeMemberWithFixtures<Namespace, Function>(
              context, suiteFixtures_, subject_, caseValues_, providerValues_);
        },
        request.timeMode,
        count,
        sink);
  }

private:
  mutable SubjectValue subject_;
  const CaseValues caseValues_;
  const ProviderValues providerValues_;
  Ref<FixtureScope<Namespace>> suiteFixtures_;
};

template <std::meta::info Namespace, std::meta::info Function, class CaseValues, class ProviderValues>
class FixtureMemberInvocationFactory final : public InvocationFactory {
public:
  FixtureMemberInvocationFactory(CaseValues caseValues,
      ProviderValues providerValues,
      FixtureScope<Namespace> &suiteFixtures)
      : caseValues_(std::move(caseValues))
      , providerValues_(std::move(providerValues))
      , suiteFixtures_(suiteFixtures) {
  }

  [[nodiscard]] auto invoke(const InvocationRequest &request) const -> TestExecution override {
    return run(
        request.descriptor.get(),
        [this](const Context &context) -> decltype(auto) {
          return detail::invokeMemberFromFixture<Namespace, Function>(
              context, suiteFixtures_, caseValues_, providerValues_);
        },
        request.timeMode);
  }

  [[nodiscard]] auto invokeCompact(const InvocationRequest &request) const -> AttemptOutcome override {
    return runCompact(
        request.descriptor.get(),
        [this](const Context &context) -> decltype(auto) {
          return detail::invokeMemberFromFixture<Namespace, Function>(
              context, suiteFixtures_, caseValues_, providerValues_);
        },
        request.timeMode);
  }

  auto invokeBatch(const InvocationRequest &request, usize count, BatchExecutionContext &sink) const
      -> void override {
    runBatch(
        request.descriptor.get(),
        [this](const Context &context) -> decltype(auto) {
          return detail::invokeMemberFromFixture<Namespace, Function>(
              context, suiteFixtures_, caseValues_, providerValues_);
        },
        request.timeMode,
        count,
        sink);
  }

private:
  const CaseValues caseValues_;
  const ProviderValues providerValues_;
  Ref<FixtureScope<Namespace>> suiteFixtures_;
};

template <std::meta::info Namespace, class CaseValues>
struct ProviderWorkContext final {
  Ref<detail::RunSession> session;
  Ref<FixtureScope<Namespace>> suiteFixtures;
  Ref<const CaseValues> caseValues;
  StringView caseDescription;
  Ref<usize> testCaseIndex;

  explicit ProviderWorkContext(detail::RunSession &session,
      FixtureScope<Namespace> &suiteFixtures,
      const CaseValues &caseValues,
      StringView caseDescription,
      usize &testCaseIndex)
      : session(session)
      , suiteFixtures(suiteFixtures)
      , caseValues(caseValues)
      , caseDescription(caseDescription)
      , testCaseIndex(testCaseIndex) {
  }
};

template <std::meta::info Namespace, std::meta::info Function, class CaseValues>
auto appendProviderWorkItems(const ProviderWorkContext<Namespace, CaseValues> &context) -> void {
  const usize providerCount =
      detail::forEachProviderCombination<Function>([&context](const auto &...providerValues) -> void {
        const String providerDescription = detail::providerDescription<Function>(providerValues...);
        auto providerTuple = std::make_tuple(providerValues...);
        const TestDescriptor descriptor =
            makeTestDescriptor<Function>(context.testCaseIndex, context.caseDescription, providerDescription);
        using ProviderTuple = std::remove_cvref_t<decltype(providerTuple)>;

        if constexpr (isNonStaticMember<Namespace, Function>()) {
          if constexpr (ReflectedFunctionMetadata<Function>::subjects.size() != 0) {
            constexpr std::meta::info annotation = ReflectedFunctionMetadata<Function>::subjects.front();
            using Annotation = meta::TypeObject<annotation>;
            constexpr Annotation subjectAnnotation = std::meta::extract<Annotation>(annotation);
            using SubjectValue = std::remove_cvref_t<decltype(subjectAnnotation.value())>;
            using Factory =
                MemberInvocationFactory<Namespace, Function, SubjectValue, CaseValues, ProviderTuple>;

            context.session.get().appendPlannedCase(detail::PlannedCase{descriptor,
                invocationCapabilities<Namespace, Function>(),
                std::make_shared<Factory>(subjectAnnotation.value(),
                    context.caseValues,
                    std::move(providerTuple),
                    context.suiteFixtures.get())});
          } else {
            using Factory = FixtureMemberInvocationFactory<Namespace, Function, CaseValues, ProviderTuple>;

            context.session.get().appendPlannedCase(detail::PlannedCase{descriptor,
                invocationCapabilities<Namespace, Function>(),
                std::make_shared<Factory>(
                    context.caseValues, std::move(providerTuple), context.suiteFixtures.get())});
          }
        } else {
          using Factory = ReflectedInvocationFactory<Namespace, Function, CaseValues, ProviderTuple>;

          context.session.get().appendPlannedCase(detail::PlannedCase{descriptor,
              invocationCapabilities<Namespace, Function>(),
              std::make_shared<Factory>(
                  context.caseValues, std::move(providerTuple), context.suiteFixtures.get())});
        }
        ++context.testCaseIndex;
      });

  if (providerCount != 0)
    return;

  const String providerDescription = detail::missingProviderDescription<Function>();
  const TestDescriptor descriptor =
      makeTestDescriptor<Function>(context.testCaseIndex, context.caseDescription, providerDescription);
  context.session.get().appendPlannedCase(detail::PlannedCase{descriptor,
      invocationCapabilities<Namespace, Function>(),
      std::make_shared<MissingProviderInvocationFactory<Function>>()});
  ++context.testCaseIndex;
}

template <std::meta::info Namespace, std::meta::info Function, std::meta::info Annotation>
auto appendCaseWorkItems(detail::RunSession &session,
    FixtureScope<Namespace> &suiteFixtures,
    usize &testCaseIndex) -> void {
  using Ann = meta::TypeObject<Annotation>;
  constexpr Ann testCase = std::meta::extract<Ann>(Annotation);
  const String caseDescription = testCase.describe();
  const auto caseValueTuple = caseValues(testCase);

  appendProviderWorkItems<Namespace, Function>(
      ProviderWorkContext<Namespace, std::remove_cvref_t<decltype(caseValueTuple)>>{
          session, suiteFixtures, caseValueTuple, caseDescription, testCaseIndex});
}

template <std::meta::info Namespace, std::meta::info Function>
auto appendWorkItems(detail::RunSession &session, FixtureScope<Namespace> &suiteFixtures) -> void {
  usize testCaseIndex{};

  if constexpr (caseCount<Function>() == 0) {
    const Tuple<> caseValues{};
    appendProviderWorkItems<Namespace, Function>(
        ProviderWorkContext<Namespace, std::remove_cvref_t<decltype(caseValues)>>{
            session, suiteFixtures, caseValues, {}, testCaseIndex});
    return;
  }

  template for (constexpr std::meta::info annotation : ReflectedFunctionMetadata<Function>::cases) {
    appendCaseWorkItems<Namespace, Function, annotation>(session, suiteFixtures, testCaseIndex);
  }
}

template <std::meta::info Scope, std::meta::info Function, class Configuration>
consteval auto isDiscoveredTest() -> bool {
  return isTest<Function>();
}

template <std::meta::info Scope, class Configuration>
auto appendScopeDescriptors(Vec<TestDescriptor> &descriptors) -> void {
  template for (constexpr std::meta::info member : meta::members<Scope, meta::AccessContext::unchecked()>) {
    if constexpr (std::meta::is_function(member) and isDiscoveredTest<Scope, member, Configuration>()) {
      appendDescriptors<member>(descriptors);
    }

    if constexpr (Configuration::recursive_ and std::meta::is_namespace(member))
      appendScopeDescriptors<member, Configuration>(descriptors);
  }
}

template <std::meta::info FixtureNamespace, std::meta::info Scope, class Configuration>
auto appendScopeWorkItems(detail::RunSession &session, FixtureScope<FixtureNamespace> &suiteFixtures)
    -> void {
  template for (constexpr std::meta::info member : meta::members<Scope, meta::AccessContext::unchecked()>) {
    if constexpr (std::meta::is_function(member) and isDiscoveredTest<Scope, member, Configuration>()) {
      appendWorkItems<FixtureNamespace, member>(session, suiteFixtures);
    }

    if constexpr (Configuration::recursive_ and std::meta::is_namespace(member))
      appendScopeWorkItems<FixtureNamespace, member, Configuration>(session, suiteFixtures);
  }
}

template <std::meta::info Function, usize ParameterIndex = 0>
consteval auto hasDynamicProviders() -> bool {
  if constexpr (ParameterIndex == ReflectedFunctionMetadata<Function>::parameterCount) {
    return false;
  } else {
    constexpr std::meta::info parameter =
        ReflectedFunctionMetadata<Function>::template parameter<ParameterIndex>();
    if constexpr (isProviderParameter<Function, parameter>() and
                  providerKindOf<Function, parameter>() == ProviderKind::Files)
      return true;

    return hasDynamicProviders<Function, ParameterIndex + 1>();
  }
}

template <std::meta::info Scope, class Configuration>
consteval auto scopeHasDynamicProviders() -> bool {
  bool dynamic{};

  template for (constexpr std::meta::info member : meta::members<Scope, meta::AccessContext::unchecked()>) {
    if constexpr (std::meta::is_function(member) and isDiscoveredTest<Scope, member, Configuration>()) {
      if constexpr (hasDynamicProviders<member>())
        dynamic = true;
    }

    if constexpr (Configuration::recursive_ and std::meta::is_namespace(member))
      dynamic = dynamic or scopeHasDynamicProviders<member, Configuration>();
  }

  return dynamic;
}

template <std::meta::info Scope, class Configuration>
[[nodiscard]] auto cachedScopeDescriptors() -> const Vec<TestDescriptor> & {
  static_assert(not scopeHasDynamicProviders<Scope, Configuration>());

  static const Vec<TestDescriptor> descriptors_ = [] -> Vec<TestDescriptor> {
    Vec<TestDescriptor> result{};
    appendScopeDescriptors<Scope, Configuration>(result);
    return result;
  }();
  return descriptors_;
}

template <std::meta::info Scope, class Configuration>
[[nodiscard]] auto describeScope() -> Vec<TestDescriptor> {
  if constexpr (scopeHasDynamicProviders<Scope, Configuration>()) {
    Vec<TestDescriptor> descriptors{};
    appendScopeDescriptors<Scope, Configuration>(descriptors);
    return descriptors;
  } else {
    return cachedScopeDescriptors<Scope, Configuration>();
  }
}

template <std::meta::info Scope, class Configuration>
[[nodiscard]] auto listScope(const TestSelection &selection) -> Vec<TestDescriptor> {
  return filterDescriptors(describeScope<Scope, Configuration>(), selection);
}

template <std::meta::info Scope, class Configuration>
auto appendScopePlan(detail::RunSession &session) -> void {
  static_assert(detail::fixtureDeclarationsAreValid<Scope>());

  detail::SuiteState &suite = session.appendSuite(qualifiedNameOf<Scope>());
  if constexpr (not scopeHasDynamicProviders<Scope, Configuration>())
    session.reservePlannedCases(cachedScopeDescriptors<Scope, Configuration>().size());
  FixtureScope<Scope> &suiteFixtures = suite.template emplace<FixtureScope<Scope>>();
  appendScopeWorkItems<Scope, Scope, Configuration>(session, suiteFixtures);
}

template <std::meta::info Scope, class Configuration>
[[nodiscard]] auto runScope(const TestSelection &selection, RunOptions options) -> Vec<TestExecution> {
  detail::RunSession session{};
  appendScopePlan<Scope, Configuration>(session);
  filterPlannedCases(session, selection);
  return detail::executePlannedCases(session, options);
}

template <std::meta::info Scope, class Configuration>
[[nodiscard]] auto runScopeReport(const TestSelection &selection, RunOptions options) -> RunReport {
  detail::RunSession session{};
  appendScopePlan<Scope, Configuration>(session);
  filterPlannedCases(session, selection);
  RunAccumulator accumulator{options.retention,
      options.maxRetainedFailures,
      SelectionMetadata{
          .include = selection.include,
          .exclude = selection.exclude,
          .tagsAll = selection.tagsAll,
          .tagsAny = selection.tagsAny,
          .group = selection.group,
      },
      options.captureTiming != CapturePolicy::None};
  static_cast<void>(detail::executePlannedCases(session, options, accumulator));
  return std::move(accumulator).finish();
}

template <std::meta::info Scope, class Configuration>
[[nodiscard]] auto runScopeLive(Reporter &reporter,
    std::ostream &output,
    const TestSelection &selection,
    RunOptions options) -> RunReport {
  detail::RunSession session{};
  appendScopePlan<Scope, Configuration>(session);
  filterPlannedCases(session, selection);
  RunAccumulator accumulator{options.retention,
      options.maxRetainedFailures,
      SelectionMetadata{
          .include = selection.include,
          .exclude = selection.exclude,
          .tagsAll = selection.tagsAll,
          .tagsAny = selection.tagsAny,
          .group = selection.group,
      },
      options.captureTiming != CapturePolicy::None};
  reporter.beginLive(output, options.captureTiming != CapturePolicy::None);
  accumulator.setCompletionObserver(
      [&reporter](const TestCaseResult &testCase) { reporter.consumeLive(testCase); });
  static_cast<void>(detail::executePlannedCases(session, options, accumulator));
  RunReport report = std::move(accumulator).finish();
  reporter.finishLive(report);
  return report;
}

template <std::meta::info Scope, class Configuration>
inline constinit const SuiteEntry suiteEntry{
    .scope = qualifiedNameOf<Scope>(),
    .location = scopeLocationOf<Scope>(),
    .describe = &describeScope<Scope, Configuration>,
    .plan = &appendScopePlan<Scope, Configuration>,
};

template <std::meta::info Scope, class Configuration>
struct SuiteRegistration final {
  SuiteRegistration() {
    appendRegisteredSuite(suiteEntry<Scope, Configuration>);
  }
};

template <std::meta::info Scope, class Configuration>
inline SuiteRegistration<Scope, Configuration> suiteRegistration{}; // NOLINT

template <std::meta::info Scope, class Configuration>
struct WorkerSuiteRegistration final {
  WorkerSuiteRegistration() {
    appendWorkerSuite(suiteEntry<Scope, Configuration>);
  }
};

template <std::meta::info Scope, class Configuration>
inline WorkerSuiteRegistration<Scope, Configuration> workerSuiteRegistration{}; // NOLINT

template <std::meta::info Scope, class Configuration>
consteval auto materializeRegistration() -> void {
  static_cast<void>(&suiteRegistration<Scope, Configuration>);
}

template <std::meta::info Scope, class Configuration>
consteval auto materializeWorkerRegistration() -> void {
  static_cast<void>(&workerSuiteRegistration<Scope, Configuration>);
}

template <std::meta::info Scope, class Configuration>
consteval auto registerSuite() -> void {
  static_assert(&suiteEntry<Scope, Configuration> != nullptr);
}

} // namespace detail

/// Registers Scope in the process-wide automatic suite catalog.
///
/// Invoke this only from a file-scope conteval block:
///
/// ```
/// consteval {
///   discover<^^MyTests>(recursive);
/// }
/// ```
///
/// The immediate invocation materializes one registration anchor for this Scope/options specialization. Its
/// ordinary static initialization appends the immutable SuiteEntry before main(). The translation unit
/// containing discover() must be linked into the test executable.
template <std::meta::info Scope, detail::DiscoveryOption... Options>
consteval auto discover(Options... /*unused*/) -> void {
  using Configuration = detail::DiscoveryConfiguration<std::remove_cvref_t<Options>...>;
  detail::materializeRegistration<Scope, Configuration>();
}
/// Returns one descriptor per Case/provider combination in declaration order.
template <std::meta::info Scope, detail::DiscoveryOption... Options>
[[nodiscard]]
auto describe(Options... /*unused*/) -> Vec<TestDescriptor> {
  using Configuration = detail::DiscoveryConfiguration<std::remove_cvref_t<Options>...>;
  static_assert(std::meta::is_namespace(Scope) or Configuration::staticMemberFunctions_,
      "Switch class-member discovery requires the staticMemberFunctions options.");
  return detail::describeScope<Scope, Configuration>();
}

/// Lists one reflected scope after applying metadata and qualified-name selection.
template <std::meta::info Scope>
[[nodiscard]] auto list(TestSelection selection = {}) -> Vec<TestDescriptor> {
  static_assert(std::meta::is_namespace(Scope), "Provided Scope should be a namespace.");
  return detail::listScope<Scope, detail::DiscoveryConfiguration<>>(selection);
}

/// Lists one reflected scope with explicit discovery options and selection.
template <std::meta::info Scope, detail::DiscoveryOption... Options>
  requires(sizeof...(Options) != 0)
[[nodiscard]] auto list(TestSelection selection, Options... /*unused*/) -> Vec<TestDescriptor> {
  using Configuration = detail::DiscoveryConfiguration<std::remove_cvref_t<Options>...>;
  static_assert(std::meta::is_namespace(Scope) or Configuration::staticMemberFunctions_,
      "Switch class-member discovery requires the staticMemberFunctions options.");
  return detail::listScope<Scope, Configuration>(selection);
}

/// Executes all reflected tests and their declarative Case/provider annotations.
template <std::meta::info Namespace>
[[nodiscard]]
auto runAllDetailed(RunOptions options = {}) -> Vec<TestExecution> {
  detail::materializeWorkerRegistration<Namespace, detail::DiscoveryConfiguration<>>();
  return detail::runScope<Namespace, detail::DiscoveryConfiguration<>>({}, options);
}

/// Executes one reflected namespace after selecting expanded cases.
template <std::meta::info Namespace>
[[nodiscard]]
auto runAllDetailed(TestSelection selection, RunOptions options = {}) -> Vec<TestExecution> {
  detail::materializeWorkerRegistration<Namespace, detail::DiscoveryConfiguration<>>();
  return detail::runScope<Namespace, detail::DiscoveryConfiguration<>>(selection, options);
}

/// Executes one reflected scope with explicit discovery options.
template <std::meta::info Scope, detail::DiscoveryOption... Options>
  requires(sizeof...(Options) != 0)
[[nodiscard]]
auto runAllDetailed(Options... /*unused*/) -> Vec<TestExecution> {
  using Configuration = detail::DiscoveryConfiguration<std::remove_cvref_t<Options>...>;
  static_assert(std::meta::is_namespace(Scope) or Configuration::staticMemberFunctions_,
      "Switch class-member discovery requires the staticMemberFunctions options.");
  return detail::runScope<Scope, Configuration>({}, {});
}

/// Executes one reflected scope with explicit discovery and runner options.
template <std::meta::info Scope, detail::DiscoveryOption... Options>
  requires(sizeof...(Options) != 0)
[[nodiscard]]
auto runAllDetailed(RunOptions options, Options... /*unused*/) -> Vec<TestExecution> {
  using Configuration = detail::DiscoveryConfiguration<std::remove_cvref_t<Options>...>;
  static_assert(std::meta::is_namespace(Scope) or Configuration::staticMemberFunctions_,
      "Switch class-member discovery requires the staticMemberFunctions options.");
  return detail::runScope<Scope, Configuration>({}, options);
}

/// Executes one reflected scope with explicit discovery options and selection.
template <std::meta::info Scope, detail::DiscoveryOption... Options>
  requires(sizeof...(Options) != 0)
[[nodiscard]] auto runAllDetailed(TestSelection selection, Options... /*unused*/) -> Vec<TestExecution> {
  using Configuration = detail::DiscoveryConfiguration<std::remove_cvref_t<Options>...>;
  static_assert(std::meta::is_namespace(Scope) or Configuration::staticMemberFunctions_,
      "Switch class-member discovery requires the staticMemberFunctions options.");
  return detail::runScope<Scope, Configuration>(selection, {});
}

/// Executes one reflected scope with explicit discovery, selection, and runner options.
template <std::meta::info Scope, detail::DiscoveryOption... Options>
[[nodiscard]] auto runAllDetailed(TestSelection selection, RunOptions options, Options... /*unused*/)
    -> Vec<TestExecution> {
  using Configuration = detail::DiscoveryConfiguration<std::remove_cvref_t<Options>...>;
  static_assert(std::meta::is_namespace(Scope) or Configuration::staticMemberFunctions_,
      "Switch class-member discovery requires the staticMemberFunctions option.");
  return detail::runScope<Scope, Configuration>(selection, options);
}

/// Describes every suite registered by file-scope discover<^^Scope>() calls.
[[nodiscard]] auto discover() -> Vec<TestDescriptor>;

/// Lists every registered suite after applying metadata and qualified-name selection.
[[nodiscard]] auto list(TestSelection selection = {}) -> Vec<TestDescriptor>;

/// Executes every suite registered by file-scope discover<^^Scope>() calls.
[[nodiscard]] auto runAllDetailed(RunOptions options = {}) -> Vec<TestExecution>;

/// Executes selected cases from every file-scope registered suite..
[[nodiscard]] auto runAllDetailed(TestSelection selection, RunOptions options = {}) -> Vec<TestExecution>;

/// Executes every registered suite and returns a bounded aggregate report.
[[nodiscard]] auto runAll(RunOptions options = {}) -> RunReport;

/// Executes selected registered cases and returns a bounded aggregate report.
[[nodiscard]] auto runAll(TestSelection selection, RunOptions options = {}) -> RunReport;
[[nodiscard]] auto runAll(Reporter &reporter, std::ostream &output, RunOptions options = {}) -> RunReport;
[[nodiscard]] auto runAll(Reporter &reporter,
    std::ostream &output,
    TestSelection selection,
    RunOptions options = {}) -> RunReport;

template <std::meta::info Namespace>
[[nodiscard]] auto runAll(RunOptions options = {}) -> RunReport {
  return detail::runScopeReport<Namespace, detail::DiscoveryConfiguration<>>({}, options);
}

template <std::meta::info Namespace>
[[nodiscard]] auto runAll(TestSelection selection, RunOptions options = {}) -> RunReport {
  return detail::runScopeReport<Namespace, detail::DiscoveryConfiguration<>>(selection, options);
}

template <std::meta::info Namespace>
[[nodiscard]] auto runAll(Reporter &reporter, std::ostream &output, RunOptions options = {}) -> RunReport {
  return detail::runScopeLive<Namespace, detail::DiscoveryConfiguration<>>(reporter, output, {}, options);
}

template <std::meta::info Namespace>
[[nodiscard]] auto runAll(Reporter &reporter,
    std::ostream &output,
    TestSelection selection,
    RunOptions options = {}) -> RunReport {
  return detail::runScopeLive<Namespace, detail::DiscoveryConfiguration<>>(
      reporter, output, selection, options);
}

template <std::meta::info Scope, detail::DiscoveryOption... Options>
  requires(sizeof...(Options) != 0)
[[nodiscard]] auto runAll(Reporter &reporter, std::ostream &output, Options... /*unused*/) -> RunReport {
  using Configuration = detail::DiscoveryConfiguration<std::remove_cvref_t<Options>...>;
  return detail::runScopeLive<Scope, Configuration>(reporter, output, {}, {});
}

template <std::meta::info Scope, detail::DiscoveryOption... Options>
  requires(sizeof...(Options) != 0)
[[nodiscard]] auto runAll(Reporter &reporter,
    std::ostream &output,
    RunOptions runOptions,
    Options... /*unused*/) -> RunReport {
  using Configuration = detail::DiscoveryConfiguration<std::remove_cvref_t<Options>...>;
  return detail::runScopeLive<Scope, Configuration>(reporter, output, {}, runOptions);
}

template <std::meta::info Scope, detail::DiscoveryOption... Options>
  requires(sizeof...(Options) != 0)
[[nodiscard]] auto runAll(Reporter &reporter,
    std::ostream &output,
    TestSelection selection,
    Options... /*unused*/) -> RunReport {
  using Configuration = detail::DiscoveryConfiguration<std::remove_cvref_t<Options>...>;
  return detail::runScopeLive<Scope, Configuration>(reporter, output, selection, {});
}

template <std::meta::info Scope, detail::DiscoveryOption... Options>
[[nodiscard]] auto runAll(Reporter &reporter,
    std::ostream &output,
    TestSelection selection,
    RunOptions runOptions,
    Options... /*unused*/) -> RunReport {
  using Configuration = detail::DiscoveryConfiguration<std::remove_cvref_t<Options>...>;
  return detail::runScopeLive<Scope, Configuration>(reporter, output, selection, runOptions);
}

template <std::meta::info Scope, detail::DiscoveryOption... Options>
  requires(sizeof...(Options) != 0)
[[nodiscard]] auto runAll(Options... /*unused*/) -> RunReport {
  using Configuration = detail::DiscoveryConfiguration<std::remove_cvref_t<Options>...>;
  return detail::runScopeReport<Scope, Configuration>({}, {});
}

template <std::meta::info Scope, detail::DiscoveryOption... Options>
  requires(sizeof...(Options) != 0)
[[nodiscard]] auto runAll(RunOptions options, Options... /*unused*/) -> RunReport {
  using Configuration = detail::DiscoveryConfiguration<std::remove_cvref_t<Options>...>;
  return detail::runScopeReport<Scope, Configuration>({}, options);
}

template <std::meta::info Scope, detail::DiscoveryOption... Options>
  requires(sizeof...(Options) != 0)
[[nodiscard]] auto runAll(TestSelection selection, Options... /*unused*/) -> RunReport {
  using Configuration = detail::DiscoveryConfiguration<std::remove_cvref_t<Options>...>;
  return detail::runScopeReport<Scope, Configuration>(selection, {});
}

template <std::meta::info Scope, detail::DiscoveryOption... Options>
[[nodiscard]] auto runAll(TestSelection selection, RunOptions options, Options... /*unused*/) -> RunReport {
  using Configuration = detail::DiscoveryConfiguration<std::remove_cvref_t<Options>...>;
  return detail::runScopeReport<Scope, Configuration>(selection, options);
}

} // namespace Switch
// NOLINTEND(bugprone-reserved-identifier)
