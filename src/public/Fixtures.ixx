export module Switch:Fixtures;

import std;
import Miracle;

import :Annotations;
import :Context;
import :Metadata;
import :Providers;
import :Task;

using namespace Miracle;

export namespace Switch {

template <std::meta::info Namespace>
class FixtureScope;

// NOLINTBEGIN(bugprone-reserved-identifier)
namespace detail {

template <std::meta::info Namespace>
class FixtureResolver;

template <std::meta::info Namespace, std::meta::info FixtureFunction>
auto invokeFixture(FixtureResolver<Namespace> &resolver) -> meta::ReturnObject<FixtureFunction>;

template <std::meta::info Function>
consteval auto isFixture() -> bool {
  if constexpr (not std::meta::is_function(Function))
    return false;
  else
    return ReflectedFunctionMetadata<Function>::fixtureMarkers.size() != 0;
}

template <std::meta::info FixtureFunction>
consteval auto isOnce() -> bool {
  return ReflectedFunctionMetadata<FixtureFunction>::onceMarkers.size() != 0;
}

// NOLINTBEGIN(readability-identifier-naming)
template <class>
inline constexpr bool is_task_return_v{};

template <class Value>
inline constexpr bool is_task_return_v<Task<Value>>{true};

template <class>
struct TaskValue;

template <class Value>
struct TaskValue<Task<Value>> final {
  using Type = Value;
};

template <class Type>
using TaskValueType = typename TaskValue<std::remove_cvref_t<Type>>::Type;

template <class Type>
inline constexpr bool is_value_or_const_reference_v =
    not std::is_reference_v<Type> or
    (std::is_lvalue_reference_v<Type> and std::is_const_v<std::remove_reference_t<Type>>);
// NOLINTEND(readability-identifier-naming)

template <std::meta::info Parameter>
consteval auto validateInputParameter() -> void {
  static_assert(is_value_or_const_reference_v<meta::TypeObject<Parameter>>,
      "Switch injected parameters must be values or const lvalue references.");
}

template <std::meta::info Parameter>
consteval auto validateContextParameter() -> void {
  validateInputParameter<Parameter>();
  static_assert(std::same_as<meta::TypeObject<Parameter>, Context>,
      "Switch [[= arg<\"name\">(context)]] parameters must have type Context.");
}

template <std::meta::info FixtureFunction>
consteval auto validateFixtureSignature() -> void {
  validateArgumentBindings<FixtureFunction>();
  static_assert(
      not std::same_as<meta::ReturnObject<FixtureFunction>, void>, "Switch fixtures must return a value.");
  static_assert(not is_task_return_v<meta::ReturnObject<FixtureFunction>>,
      "Switch fixtures must return a concrete value, not Task<T>.");
  static_assert(not std::is_reference_v<meta::Return<FixtureFunction>>,
      "Switch fixtures must return a concrete value, not a reference.");
  static_assert(std::constructible_from<meta::ReturnObject<FixtureFunction>, meta::Return<FixtureFunction>>,
      "Switch fixture return values must construct their declared type.");
}

template <std::meta::info FixtureFunction, class Value>
consteval auto providesFixture() -> bool {
  validateFixtureSignature<FixtureFunction>();
  return std::same_as<meta::ReturnObject<FixtureFunction>, Value>;
}

template <std::meta::info Namespace, class Value>
consteval auto fixtureCount() -> usize {
  usize count{};

  template for (constexpr std::meta::info member :
      meta::members<Namespace, meta::AccessContext::unchecked()>) {
    if constexpr (isFixture<member>()) {
      if constexpr (providesFixture<member, Value>())
        ++count;
    }
  }

  return count;
}

template <std::meta::info Namespace, class Value>
consteval auto hasFixtureFor() -> bool {
  return fixtureCount<Namespace, Value>() != 0;
}

template <std::meta::info Namespace, class Value>
consteval auto fixtureFor() -> std::meta::info {
  static_assert(fixtureCount<Namespace, Value>() == 1,
      "Switch fixture injection requires exactly one [[= fixture]] for the requested parameter type.");

  template for (constexpr std::meta::info member :
      meta::members<Namespace, meta::AccessContext::unchecked()>) {
    if constexpr (isFixture<member>()) {
      if constexpr (providesFixture<member, Value>())
        return member;
    }
  }

  return {};
}

template <std::meta::info Candidate, std::meta::info... Path>
consteval auto fixturePathContains() -> bool {
  return (false or ... or (Candidate == Path));
}

template <std::meta::info Namespace, std::meta::info FixtureFunction, std::meta::info... Path>
consteval auto validateFixtureDependencyGraph() -> void;

template <std::meta::info Namespace,
    std::meta::info FixtureFunction,
    std::meta::info Parameter,
    std::meta::info... Path>
consteval auto validateFixtureDependency() -> void {
  validateInputParameter<Parameter>();
  static_assert(argumentBindingCount<FixtureFunction, Parameter>() == 0 and
                    directPropertyCount<FixtureFunction, Parameter>() == 0,
      "Switch fixture parameters are dependencies and cannot use parameter provider annotations.");

  using Value = meta::TypeObject<Parameter>;
  constexpr usize candidateCount = fixtureCount<Namespace, Value>();

  if constexpr (candidateCount == 0) {
    static_assert(
        candidateCount != 0, "Switch fixture dependencies require one matching [[= fixture]] return type.");
  } else if constexpr (candidateCount > 1) {
    static_assert(candidateCount == 1,
        "Switch fixture dependencies are ambiguous; keep one [[= fixture]] per return type.");
  } else {
    constexpr std::meta::info dependency = fixtureFor<Namespace, Value>();
    static_assert(not fixturePathContains<dependency, Path..., FixtureFunction>(),
        "Switch fixture dependencies cannot for a cycle.");

    if constexpr (isOnce<FixtureFunction>()) {
      static_assert(
          isOnce<dependency>(), "Switch [[= once]] fixtures may depend only on other [[= once]] fixtures.");
    }

    validateFixtureDependencyGraph<Namespace, dependency, Path..., FixtureFunction>();
  }
}

template <std::meta::info Namespace, std::meta::info FixtureFunction, std::meta::info... Path>
consteval auto validateFixtureDependencyGraph() -> void {
  static_assert(not fixturePathContains<FixtureFunction, Path...>(),
      "Switch fixture dependencies cannot form a cycle.");
  validateFixtureSignature<FixtureFunction>();

  template for (constexpr std::meta::info parameter : ReflectedFunctionMetadata<FixtureFunction>::parameters)
      validateFixtureDependency<Namespace, FixtureFunction, parameter, Path...>();
}

template <std::meta::info Namespace>
consteval auto fixtureDeclarationsAreValid() -> bool {
  template for (constexpr std::meta::info member :
      meta::members<Namespace, meta::AccessContext::unchecked()>) {
    if constexpr (std::meta::is_function(member)) {
      if constexpr (isFixture<member>())
        validateFixtureDependencyGraph<Namespace, member>();

      if constexpr (isOnce<member>())
        static_assert(isFixture<member>(), "Switch [[= once]] is valid only together with [[= fixture]].");
    }
  }

  return true;
}

template <std::meta::info Function, usize ParameterIndex = 0>
consteval auto caseParameterCount() -> usize {
  if constexpr (ParameterIndex == ReflectedFunctionMetadata<Function>::parameters.size()) {
    return 0;
  } else {
    constexpr std::meta::info parameter = ReflectedFunctionMetadata<Function>::parameters[ParameterIndex];
    return (isFromCaseParameter<Function, parameter>() ? 1 : 0) +
           caseParameterCount<Function, ParameterIndex + 1>();
  }
}

template <std::meta::info Function, usize ParameterIndex = 0>
consteval auto hasContextParameter() -> bool {
  if constexpr (ParameterIndex == ReflectedFunctionMetadata<Function>::parameters.size()) {
    return false;
  } else {
    constexpr std::meta::info parameter = ReflectedFunctionMetadata<Function>::parameters[ParameterIndex];
    return isContextParameter<Function, parameter>() or hasContextParameter<Function, ParameterIndex + 1>();
  }
}

template <std::meta::info Namespace, std::meta::info Function, usize ParameterIndex = 0>
consteval auto hasAutomaticFixtureParameter() -> bool {
  if constexpr (ParameterIndex == ReflectedFunctionMetadata<Function>::parameters.size()) {
    return false;
  } else {
    constexpr std::meta::info parameter = ReflectedFunctionMetadata<Function>::parameters[ParameterIndex];
    using Value = meta::TypeObject<parameter>;

    if constexpr (isContextParameter<Function, parameter>() or isFromCaseParameter<Function, parameter>() or
                  detail::isProviderParameter<Function, parameter>()) {
      return hasAutomaticFixtureParameter<Namespace, Function, ParameterIndex + 1>();
    } else {
      return hasFixtureFor<Namespace, Value>() or
             hasAutomaticFixtureParameter<Namespace, Function, ParameterIndex + 1>();
    }
  }
}

template <std::meta::info Namespace, std::meta::info Function>
consteval auto usesLegacyCaseBinding() -> bool {
  return not hasContextParameter<Function>() and providerParameterCount<Function>() == 0 and
         caseParameterCount<Function>() == 0 and not hasAutomaticFixtureParameter<Namespace, Function>();
}

template <std::meta::info Function, usize ParameterIndex, usize CandidateIndex = 0>
consteval auto caseArgumentIndex() -> usize {
  if constexpr (CandidateIndex == ParameterIndex) {
    return 0;
  } else {
    constexpr std::meta::info parameter = ReflectedFunctionMetadata<Function>::parameters[CandidateIndex];
    return (isFromCaseParameter<Function, parameter>() ? 1 : 0) +
           caseArgumentIndex<Function, ParameterIndex, CandidateIndex + 1>();
  }
}

template <std::meta::info Namespace, std::meta::info Function, usize ParameterIndex = 0>
consteval auto hasOnceFixtureParameter() -> bool {
  if constexpr (ParameterIndex == ReflectedFunctionMetadata<Function>::parameters.size()) {
    return false;
  } else {
    constexpr std::meta::info parameter = ReflectedFunctionMetadata<Function>::parameters[ParameterIndex];
    using Value = meta::TypeObject<parameter>;

    if constexpr (hasFixtureFor<Namespace, Value>()) {
      return isOnce<fixtureFor<Namespace, Value>()>() or
             hasOnceFixtureParameter<Namespace, Function, ParameterIndex + 1>();
    } else {
      return hasOnceFixtureParameter<Namespace, Function, ParameterIndex + 1>();
    }
  }
}

} // namespace detail
// NOLINTEND(bugprone-reserved-identifier)

template <std::meta::info Namespace>
class FixtureScope final {
private:
  class FixtureEntry {
  public:
    FixtureEntry() = default;
    virtual ~FixtureEntry() = default;

    FixtureEntry(const FixtureEntry &) = default;
    auto operator=(const FixtureEntry &) -> FixtureEntry & = default;
    FixtureEntry(FixtureEntry &&) noexcept = default;
    auto operator=(FixtureEntry &&) noexcept -> FixtureEntry & = default;
  };

  template <class Value>
  class FixtureBox final : public FixtureEntry {
  public:
    template <class... Arguments>
    explicit FixtureBox(Arguments &&...arguments)
        : value_(std::forward<Arguments>(arguments)...) {
    }

    [[nodiscard]] auto value() noexcept -> Value & {
      return value_;
    }

  private:
    Value value_;
  };

  enum class[[= debug::derive]] FixtureState : u8 {
    Empty,
    Constructing,
    Ready,
  };

  struct FixtureSlot final {
    std::condition_variable ready;
    std::thread::id owner;
    UPtr<FixtureEntry> value;
    FixtureState state{};
  };

  template <class Value>
  [[nodiscard]] static auto fixtureValue(FixtureSlot &fixture) -> Value & {
    // The type-index key is derived from Value at both insertion and lookup.
    return static_cast<FixtureBox<Value> &>(*fixture.value).value();
  }

public:
  FixtureScope() = default;

  ~FixtureScope() {
    std::lock_guard lock{mutex_};
    std::ranges::for_each(
        std::views::reverse(constructionOrder_), [this](const std::type_index &key) -> void {
          const auto found = values_.find(key);
          if (found != values_.end())
            found->second->value.reset();
        });
  }

  FixtureScope(const FixtureScope &) = delete ("FixtureScope owns mutex state.");
  auto operator=(const FixtureScope &) -> FixtureScope & = delete ("FixtureScope owns mutex state.");
  FixtureScope(FixtureScope &&) noexcept = delete ("FixtureScope owns mutex state.");
  auto operator=(FixtureScope &&) noexcept -> FixtureScope & = delete ("FixtureScope owns mutex state.");

private:
  template <class Value>
  [[nodiscard]] auto get(detail::FixtureResolver<Namespace> &resolver) -> Value &;

  friend class detail::FixtureResolver<Namespace>;

  std::mutex mutex_;
  FlatMap<std::type_index, UPtr<FixtureSlot>> values_;
  Vec<std::type_index> constructionOrder_;
};

namespace detail {

/// Selects the owning scope from the dependency's lifetime rather than from the requesting fixture.
template <std::meta::info Namespace>
class FixtureResolver final {
public:
  FixtureResolver(FixtureScope<Namespace> &testFixtures, FixtureScope<Namespace> &suiteFixtures)
      : testFixtures_(testFixtures)
      , suiteFixtures_(suiteFixtures) {
  }

  template <class Value>
  [[nodiscard]] auto resolve() -> Value & {
    constexpr std::meta::info fixtureFunction = fixtureFor<Namespace, Value>();

    if constexpr (isOnce<fixtureFunction>())
      return suiteFixtures_.template get<Value>(*this);
    else
      return testFixtures_.template get<Value>(*this);
  }

private:
  FixtureScope<Namespace> &testFixtures_;  // NOLINT
  FixtureScope<Namespace> &suiteFixtures_; // NOLINT
};

template <std::meta::info Namespace, std::meta::info FixtureFunction, usize ParameterIndex>
auto fixtureDependencyArgument(FixtureResolver<Namespace> &resolver) -> decltype(auto) {
  constexpr std::meta::info parameter =
      ReflectedFunctionMetadata<FixtureFunction>::parameters[ParameterIndex];
  using Value = meta::TypeObject<parameter>;
  return resolver.template resolve<Value>();
}

template <std::meta::info Namespace, std::meta::info FixtureFunction>
auto invokeFixture(FixtureResolver<Namespace> &resolver) -> meta::ReturnObject<FixtureFunction> {
  constexpr usize parameterCount = ReflectedFunctionMetadata<FixtureFunction>::parameters.size();

  return withIndices<parameterCount>([&]<usize... Indices>(std::integral_constant<usize, Indices>...)
                                         -> meta::ReturnObject<FixtureFunction> {
    return [:FixtureFunction:](fixtureDependencyArgument<Namespace, FixtureFunction, Indices>(resolver)...);
  });
}

template <std::meta::info Namespace, class Value>
auto fixtureArgument(FixtureResolver<Namespace> &resolver) -> decltype(auto) {
  return resolver.template resolve<Value>();
}

} // namespace detail

template <std::meta::info Namespace>
template <class Value>
auto FixtureScope<Namespace>::get(detail::FixtureResolver<Namespace> &resolver) -> Value & {
  constexpr std::meta::info fixtureFunction = detail::fixtureFor<Namespace, Value>();
  const std::type_index key{typeid(Value)};
  FixtureSlot *slot{};

  {
    std::unique_lock lock{mutex_};
    const auto found = values_.find(key);
    if (found == values_.end()) {
      const auto [entry, _] = values_.emplace(key, std::make_unique<FixtureSlot>());
      slot = entry->second.get();
    } else {
      slot = found->second.get();
    }

    while (slot->state == FixtureState::Constructing) {
      if (slot->owner == std::this_thread::get_id())
        fatal("Switch detected recursive fixture construction.");

      slot->ready.wait(lock, [slot] -> bool { return slot->state != FixtureState::Constructing; });
    }

    if (slot->state == FixtureState::Ready)
      return fixtureValue<Value>(*slot);

    slot->state = FixtureState::Constructing;
    slot->owner = std::this_thread::get_id();
  }

  try {
    UPtr<FixtureEntry> fixture =
        std::make_unique<FixtureBox<Value>>(detail::invokeFixture<Namespace, fixtureFunction>(resolver));

    std::lock_guard lock{mutex_};
    constructionOrder_.push_back(key);
    slot->value = std::move(fixture);
    slot->owner = {};
    slot->state = FixtureState::Ready;
  } catch (...) {
    {
      std::lock_guard lock{mutex_};
      slot->owner = {};
      slot->state = FixtureState::Empty;
    }

    slot->ready.notify_all();
    throw;
  }

  slot->ready.notify_all();
  return fixtureValue<Value>(*slot);
}

namespace detail {

template <std::meta::info Namespace,
    std::meta::info Function,
    usize ParameterIndex,
    class CaseValues,
    class ProviderValues>
auto bindArgument(const Context &context,
    FixtureResolver<Namespace> &fixtures,
    const CaseValues &caseValues,
    const ProviderValues &providerValues) -> decltype(auto) {
  constexpr std::meta::info parameter = ReflectedFunctionMetadata<Function>::parameters[ParameterIndex];
  using Value = meta::TypeObject<parameter>;
  constexpr usize caseValueCount = std::tuple_size_v<std::remove_cvref_t<CaseValues>>;
  constexpr usize providerValueCount = std::tuple_size_v<std::remove_cvref_t<ProviderValues>>;
  validateProviderParameter<Function, parameter>();

  if constexpr (isProviderParameter<Function, parameter>()) {
    static_assert(
        not isContextParameter<Function, parameter>() and not isFromCaseParameter<Function, parameter>(),
        "Switch provider parameters cannot also use [[= arg<\"name\">(...)]].");
    validateInputParameter<parameter>();
    constexpr usize index = providerArgumentIndex<Function, ParameterIndex>();
    if constexpr (index < providerValueCount) {
      return (std::get<index>(providerValues));
    } else {
      static_assert(meta::always_false_v<Value>,
          "Switch did not receive enough provider values for its reflected test parameters.");
    }
  } else if constexpr (isContextParameter<Function, parameter>()) {
    validateContextParameter<parameter>();
    return (context);
  } else if constexpr (isFromCaseParameter<Function, parameter>()) {
    validateInputParameter<parameter>();
    constexpr usize index = caseArgumentIndex<Function, ParameterIndex>();
    if constexpr (index < caseValueCount) {
      return (std::get<index>(caseValues));
    } else {
      static_assert(meta::always_false_v<Value>,
          "Switch did not receive enough Case values for [[= arg<\"name\">(fromCase)]] parameters.");
    }
  } else if constexpr (hasFixtureFor<Namespace, Value>()) {
    validateInputParameter<parameter>();
    return fixtureArgument<Namespace, Value>(fixtures);
  } else if constexpr (usesLegacyCaseBinding<Namespace, Function>()) {
    validateInputParameter<parameter>();
    if constexpr (ParameterIndex < caseValueCount) {
      return (std::get<ParameterIndex>(caseValues));
    } else {
      static_assert(meta::always_false_v<Value>,
          "Switch could not bind a legacy Case parameter. Add [[= arg<\"name\">(fromCase) for injected "
          "tests.");
    }
  } else {
    static_assert(meta::always_false_v<Value>,
        "Switch could not bind this parameter. Use an [[= arg<\"name\">]] provider, [[= context]], or a "
        "fixture return type.");
  }
}

template <std::meta::info Namespace, std::meta::info Function, class CaseValues, class ProviderValues>
constexpr auto invokeTest(const Context &context,
    FixtureResolver<Namespace> &fixtures,
    const CaseValues &caseValues,
    const ProviderValues &providerValues) -> decltype(auto) {
  constexpr usize parameterCount = ReflectedFunctionMetadata<Function>::parameters.size();
  constexpr bool usesLegacyBinding = usesLegacyCaseBinding<Namespace, Function>();
  constexpr usize expectedCaseValues = usesLegacyBinding ? parameterCount : caseParameterCount<Function>();
  constexpr usize expectedProviderValues = providerParameterCount<Function>();
  static_assert(std::tuple_size_v<std::remove_cvref_t<CaseValues>> == expectedCaseValues,
      "Switch [[= Case]] value count does not match the test's case-bound parameters.");
  static_assert(std::tuple_size_v<std::remove_cvref_t<ProviderValues>> == expectedProviderValues,
      "Switch provider value count does not match the reflected test parameters.");

  return withIndices<parameterCount>(
      [&]<usize... Indices>(std::integral_constant<usize, Indices>...) constexpr -> decltype(auto) {
        return [:Function:](
            bindArgument<Namespace, Function, Indices>(context, fixtures, caseValues, providerValues)...);
      });
}
template <std::meta::info Namespace,
    std::meta::info Function,
    class Subject,
    class CaseValues,
    class ProviderValues>
constexpr auto invokeMemberTest(const Context &context,
    FixtureResolver<Namespace> &fixtures,
    Subject &subject,
    const CaseValues &caseValues,
    const ProviderValues &providerValues) -> decltype(auto) {
  constexpr usize parameterCount = ReflectedFunctionMetadata<Function>::parameters.size();
  constexpr bool usesLegacyBinding = usesLegacyCaseBinding<Namespace, Function>();
  constexpr usize expectedCaseValues = usesLegacyBinding ? parameterCount : caseParameterCount<Function>();
  constexpr usize expectedProviderValues = providerParameterCount<Function>();
  static_assert(std::tuple_size_v<std::remove_cvref_t<CaseValues>> == expectedCaseValues,
      "Switch [[= Case]] value count does not match the member test's case-bound parameters.");
  static_assert(std::tuple_size_v<std::remove_cvref_t<ProviderValues>> == expectedProviderValues,
      "Switch provider value count does not match the member test's reflected parameters.");

  return withIndices<parameterCount>(
      [&]<usize... Indices>(std::integral_constant<usize, Indices>...) constexpr -> decltype(auto) {
        return std::invoke(&[:Function:],
            subject,
            bindArgument<Namespace, Function, Indices>(context, fixtures, caseValues, providerValues)...);
      });
}

//// Invokes a coroutine test while retaining every injected value through its final suspension point. In
/// particular, const-reference fixture parameters must remain valid after the test's first co_await.
template <std::meta::info Namespace, std::meta::info Function, class CaseValues, class ProviderValues>
auto invokeAsyncTest(const Context &context, // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters)
    FixtureScope<Namespace> &suiteFixtures,  // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters)
    CaseValues caseValues,
    ProviderValues providerValues) -> Task<TaskValueType<meta::ReturnObject<Function>>> {
  FixtureScope<Namespace> testFixtures{};
  FixtureResolver<Namespace> fixtures{testFixtures, suiteFixtures};

  if constexpr (std::same_as<TaskValueType<meta::ReturnObject<Function>>, void>) {
    co_await invokeTest<Namespace, Function>(context, fixtures, caseValues, providerValues);
    co_return;
  } else {
    co_return co_await invokeTest<Namespace, Function>(context, fixtures, caseValues, providerValues);
  }
}

template <std::meta::info Namespace,
    std::meta::info Function,
    class Subject,
    class CaseValues,
    class ProviderValues>
auto invokeAsyncMemberTest(
    const Context &context,                 // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters)
    FixtureScope<Namespace> &suiteFixtures, // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters)
    Subject &subject,                       // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters)
    CaseValues caseValues,
    ProviderValues providerValues) -> Task<TaskValueType<meta::ReturnObject<Function>>> {
  FixtureScope<Namespace> testFixtures{};
  FixtureResolver<Namespace> fixtures{testFixtures, suiteFixtures};

  if constexpr (std::same_as<TaskValueType<meta::ReturnObject<Function>>, void>) {
    co_await invokeMemberTest<Namespace, Function>(context, fixtures, subject, caseValues, providerValues);
    co_return;
  } else {
    co_return co_await invokeMemberTest<Namespace, Function>(
        context, fixtures, subject, caseValues, providerValues);
  }
}

template <std::meta::info Namespace, std::meta::info Function, class CaseValues, class ProviderValues>
auto invokeAsyncFromFixture(
    const Context &context,                 // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters)
    FixtureScope<Namespace> &suiteFixtures, // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters)
    CaseValues caseValues,
    ProviderValues providerValues) -> Task<TaskValueType<meta::ReturnObject<Function>>> {
  FixtureScope<Namespace> testFixtures{};
  FixtureResolver<Namespace> fixtures{testFixtures, suiteFixtures};

  if constexpr (std::same_as<TaskValueType<meta::ReturnObject<Function>>, void>) {
    co_await invokeWithFixtures<Namespace, Function>(context, suiteFixtures, caseValues, providerValues);
    co_return;
  } else {
    co_return co_await invokeWithFixtures<Namespace, Function>(
        context, suiteFixtures, caseValues, providerValues);
  }
}

template <std::meta::info Namespace, std::meta::info Function, class CaseValues, class ProviderValues>
auto invokeAsyncMemberFromFixture(
    const Context &context,                 // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters)
    FixtureScope<Namespace> &suiteFixtures, // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters)
    CaseValues caseValues,
    ProviderValues providerValues) -> Task<TaskValueType<meta::ReturnObject<Function>>> {
  using Subject = meta::TypeObject<std::meta::parent_of(Function)>;
  FixtureScope<Namespace> testFixtures{};
  FixtureResolver<Namespace> fixtures{testFixtures, suiteFixtures};
  Subject &subject = fixtures.template resolve<Subject>();

  if constexpr (std::same_as<TaskValueType<meta::ReturnObject<Function>>, void>) {
    co_await invokeMemberTest<Namespace, Function>(context, fixtures, subject, caseValues, providerValues);
    co_return;
  } else {
    co_return co_await invokeMemberTest<Namespace, Function>(
        context, fixtures, subject, caseValues, providerValues);
  }
}

template <std::meta::info Namespace, std::meta::info Function, class CaseValues, class ProviderValues>
auto invokeMemberFromFixture(const Context &context,
    FixtureScope<Namespace> &suiteFixtures,
    const CaseValues &caseValues,
    const ProviderValues &providerValues) -> decltype(auto) {
  using Subject = meta::TypeObject<std::meta::parent_of(Function)>;
  using Return = meta::ReturnObject<Function>;

  if constexpr (is_task_return_v<std::remove_cvref_t<Return>>) {
    static_assert(not std::is_lvalue_reference_v<Return>,
        "Switch asynchronous member tests must return Task<T> by value.");
    return invokeAsyncMemberFromFixture<Namespace, Function>(
        context, suiteFixtures, caseValues, providerValues);
  } else {
    FixtureScope<Namespace> testFixtures{};
    FixtureResolver<Namespace> fixtures{testFixtures, suiteFixtures};
    Subject &subject = fixtures.template resolve<Subject>();
    return invokeMemberTest<Namespace, Function>(context, fixtures, subject, caseValues, providerValues);
  }
}

/// Keeps synchronous invocation allocation-free, while delegating coroutine tests to invokeAsyncTest so their
/// injected references remain valid.
template <std::meta::info Namespace, std::meta::info Function, class CaseValues, class ProviderValues>
auto invokeWithFixtures(const Context &context,
    FixtureScope<Namespace> &suiteFixtures,
    const CaseValues &caseValues,
    const ProviderValues &providerValues) -> decltype(auto) {
  using Return = meta::ReturnObject<Function>;

  if constexpr (is_task_return_v<std::remove_cvref_t<Return>>) {
    static_assert(not std::is_lvalue_reference_v<Return>,
        "Switch asynchronous test functions must return Task<T> by value.");
    return invokeAsyncTest<Namespace, Function>(context, suiteFixtures, caseValues, providerValues);
  } else {
    FixtureScope<Namespace> testFixtures{};
    FixtureResolver<Namespace> fixtures{testFixtures, suiteFixtures};
    return invokeTest<Namespace, Function>(context, fixtures, caseValues, providerValues);
  }
}

template <std::meta::info Namespace,
    std::meta::info Function,
    class Subject,
    class CaseValues,
    class ProviderValues>
auto invokeMemberWithFixtures(const Context &context,
    FixtureScope<Namespace> &suiteFixtures,
    Subject &subject,
    const CaseValues &caseValues,
    const ProviderValues &providerValues) -> decltype(auto) {
  using Return = meta::ReturnObject<Function>;

  if constexpr (is_task_return_v<std::remove_cvref_t<Return>>) {
    static_assert(not std::is_lvalue_reference_v<Return>,
        "Switch asynchronous member tests must return Task<T> by value.");
    return invokeAsyncMemberTest<Namespace, Function>(
        context, suiteFixtures, subject, caseValues, providerValues);
  } else {
    FixtureScope<Namespace> testFixtures{};
    FixtureResolver<Namespace> fixtures{testFixtures, suiteFixtures};
    return invokeMemberTest<Namespace, Function>(context, fixtures, subject, caseValues, providerValues);
  }
}

} // namespace detail

} // namespace Switch
