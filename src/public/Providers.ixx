export module Switch:Providers;

import std;
import Miracle;

import :Annotations;
import :Context;
import :Metadata;

using namespace Miracle;

export namespace Switch {

struct FileQuery final {
  String pattern;
  Vec<String> excludes;
  bool includeDotFiles{};
};

[[nodiscard]] auto matchesGlob(StringView value, StringView pattern) -> bool;

[[nodiscard]] auto findFiles(const FileQuery &query) -> Vec<Path>;

// NOLINTBEGIN(readability-identifier-naming, bugprone-reserved-identifier)
namespace detail {

enum class[[= debug::derive]] ProviderKind : u8 {
  None,
  Values,
  Files,
};

template <std::meta::info Function, std::meta::info Parameter>
consteval auto validateArgumentBinding() -> void {
  constexpr usize bindingCount = argumentBindingCount<Function, Parameter>();
  constexpr usize directCount = directPropertyCount<Function, Parameter>();
  constexpr usize contextCount = argumentPropertyCount<Function, Parameter, IsContextParameter>();
  constexpr usize caseCount = argumentPropertyCount<Function, Parameter, IsFromCaseParameter>();
  constexpr usize valuesCount = argumentPropertyCount<Function, Parameter, IsValues>();
  constexpr usize filesCount = argumentPropertyCount<Function, Parameter, IsFiles>();

  static_assert(
      bindingCount <= 1, "Switch parameters may have at most one [[= arg<\"name\">(...)]] binding.");
  static_assert(bindingCount == 0 or directCount == 0,
      "Switch parameters cannot combine a legacy [[= arg<\"name\">(...)]] binding with direct parameter "
      "annotations.");
  static_assert(contextCount <= 1, "Switch parameters may contain [[= context]] only once.");
  static_assert(caseCount <= 1, "Switch parameters may contain [[= fromCase]] only once.");
  static_assert(valuesCount + filesCount <= 1,
      "Switch parameters may contain one provider: [[= values(...)]] or [[= files(...)]].");
  static_assert(contextCount + caseCount + valuesCount + filesCount <= 1,
      "Switch parameters may select one input source: [[= context]], [[= fromCase]], [[= "
      "values(...)]], or [[= files(...)]].");
}

template <std::meta::info Function>
consteval auto validateArgumentBindings() -> void {
  template for (constexpr std::meta::info annotation : ReflectedFunctionMetadata<Function>::arguments) {
    using Annotation = meta::TypeObject<annotation>;

    if constexpr (is_argument_v<Annotation>) {
      static_assert(ArgumentTraits<Annotation>::propertiesAreSupported(),
          "Switch parameter accepts only [[= context]], [[= fromCase]], [[= values(...)]], "
          "[[= files(...)]], [[= exclude(...)]], and [[= includeDotFiles]].");
      static_assert(argumentTargetsFunctionParameter<Function, Annotation>(),
          "Switch [[= arg<\"name\">(...)]] must name a parameter of its test function.");
    }
  }

  template for (constexpr std::meta::info parameter : ReflectedFunctionMetadata<Function>::parameters) {
    validateArgumentBinding<Function, parameter>();
  }
}

template <std::meta::info Function, std::meta::info Parameter>
consteval auto providerCount() -> usize {
  return argumentPropertyCount<Function, Parameter, IsValues>() +
         argumentPropertyCount<Function, Parameter, IsFiles>();
}

template <std::meta::info Function, std::meta::info Parameter>
consteval auto providerKindOf() -> ProviderKind {
  if constexpr (parameterSource<Function, Parameter>() == ParameterSource::Values)
    return ProviderKind::Values;
  else if constexpr (parameterSource<Function, Parameter>() == ParameterSource::Files)
    return ProviderKind::Files;
  else
    return ProviderKind::None;
}

template <std::meta::info Function, std::meta::info Parameter>
consteval auto isProviderParameter() -> bool {
  return parameterSource<Function, Parameter>() == ParameterSource::Values or
         parameterSource<Function, Parameter>() == ParameterSource::Files;
}

template <std::meta::info Function, std::meta::info Parameter>
consteval auto isContextParameter() -> bool {
  return parameterSource<Function, Parameter>() == ParameterSource::Context;
}

template <std::meta::info Function, std::meta::info Parameter>
consteval auto isFromCaseParameter() -> bool {
  return parameterSource<Function, Parameter>() == ParameterSource::Case;
}

template <std::meta::info Function, std::meta::info Parameter>
consteval auto hasFileModifiers() -> bool {
  return argumentPropertyCount<Function, Parameter, IsExclude>() != 0 or
         argumentPropertyCount<Function, Parameter, IsIncludeDotFiles>() != 0;
}

template <std::meta::info Function, std::meta::info Parameter, template <class> class Predicate>
consteval auto argumentProperty() -> auto {
  if constexpr (argumentPropertyCount<Function, Parameter, Predicate>() != 0) {
    if constexpr (directPropertyCountFor<Function, Parameter, Predicate>() != 0) {
      template for (constexpr ParameterProperty property :
          ReflectedFunctionMetadata<Function>::parameterProperties) {
        if constexpr (property.parameter == Parameter and not property.legacy) {
          using Annotation = meta::TypeObject<property.annotation>;

          if constexpr (Predicate<Annotation>::value)
            return std::meta::extract<Annotation>(property.annotation);
        }
      }
    } else {
      template for (constexpr std::meta::info annotation : ReflectedFunctionMetadata<Function>::arguments) {
        using Annotation = meta::TypeObject<annotation>;

        if constexpr (argumentTargetsParameter<Parameter, Annotation>() and
                      ArgumentTraits<Annotation>::template contains<Predicate>()) {
          constexpr Annotation argument = std::meta::extract<Annotation>(annotation);
          constexpr usize index = ArgumentTraits<Annotation>::template firstIndex<Predicate>();
          return argument.template property<index>();
        }
      }
    }

    std::unreachable();
  } else {
    static_assert(meta::always_false_v<meta::TypeObject<Parameter>>,
        "Switch could not find the requested parameter annotation or legacy arg property.");
  }
}

template <std::meta::info Function, std::meta::info Parameter>
consteval auto valuesAnnotation() -> auto {
  return argumentProperty<Function, Parameter, IsValues>();
}

template <std::meta::info Function, std::meta::info Parameter>
consteval auto filesAnnotation() -> auto {
  return argumentProperty<Function, Parameter, IsFiles>();
}

template <std::meta::info Parameter, class ValueList>
inline constexpr bool values_constructible_v{};

template <std::meta::info Parameter, class... ValueTypes>
inline constexpr bool values_constructible_v<Parameter, Values<ValueTypes...>> =
    (std::constructible_from<meta::TypeObject<Parameter>, ValueTypes> and ...);

template <std::meta::info Function, std::meta::info Parameter>
consteval auto validateProviderParameter() -> void {
  validateArgumentBinding<Function, Parameter>();

  if constexpr (providerKindOf<Function, Parameter>() == ProviderKind::Values) {
    using ValueList = std::remove_cvref_t<decltype(valuesAnnotation<Function, Parameter>())>;
    static_assert(ValueList::size_ != 0, "Switch [[= values(...)]] requires at least one value.");
    static_assert(values_constructible_v<Parameter, ValueList>,
        "Switch [[= values(...)]] contains a value incompatible with its parameter type.");
    static_assert(not hasFileModifiers<Function, Parameter>(),
        "Switch [[= exclude(...)]] and [[= includeDotFiles]] require [[= files(...)]] on the same "
        "parameter.");
  } else if constexpr (providerKindOf<Function, Parameter>() == ProviderKind::Files) {
    static_assert(std::same_as<meta::TypeObject<Parameter>, Path>,
        "Switch [[= files(...)]] bindings must target a Path parameter.");
  } else {
    static_assert(not hasFileModifiers<Function, Parameter>(),
        "Switch [[= exclude(...)]] and [[= includeDotFiles]] require [[= files(...)]] on the same "
        "parameter.");
  }
}

template <std::meta::info Function, usize ParameterIndex = 0>
consteval auto validateProviderParameters() -> void {
  if constexpr (ParameterIndex < ReflectedFunctionMetadata<Function>::parameters.size()) {
    constexpr std::meta::info parameter = ReflectedFunctionMetadata<Function>::parameters[ParameterIndex];
    validateProviderParameter<Function, parameter>();
    validateProviderParameters<Function, ParameterIndex + 1>();
  }
}

template <std::meta::info Function, std::meta::info Parameter, class Callback>
constexpr auto forEachValues(Callback &&callback) -> void {
  constexpr auto valueList = valuesAnnotation<Function, Parameter>();
  valueList.apply([&callback](const auto &...items) -> void {
    (std::invoke(std::forward<Callback>(callback), meta::TypeObject<Parameter>(items)), ...);
  });
}

struct FilePropertyAppender final {
  Ref<FileQuery> query;

  template <class Property>
  constexpr auto operator()(const Property &property) const -> void {
    using Type = std::remove_cvref_t<Property>;

    if constexpr (is_exclude_v<Type>)
      query.get().excludes.emplace_back(property.apply());
    else if constexpr (std::same_as<Type, IncludeDotFiles>)
      query.get().includeDotFiles = true;
  }
};

template <std::meta::info Function, std::meta::info Parameter>
constexpr auto fileQuery() -> FileQuery {
  constexpr auto files = filesAnnotation<Function, Parameter>();
  FileQuery query{
      .pattern = String{files.apply()},
  };

  template for (constexpr ParameterProperty property :
      ReflectedFunctionMetadata<Function>::parameterProperties) {
    if constexpr (property.parameter == Parameter) {
      using Annotation = meta::TypeObject<property.annotation>;

      if constexpr (property.legacy) {
        constexpr Annotation argument = std::meta::extract<Annotation>(property.annotation);
        argument.apply(FilePropertyAppender{query});
      } else if constexpr (is_exclude_v<Annotation> or std::same_as<Annotation, IncludeDotFiles>) {
        FilePropertyAppender{query}(std::meta::extract<Annotation>(property.annotation));
      }
    }
  }

  return query;
}

template <std::meta::info Function, std::meta::info Parameter, class Callback>
constexpr auto forEachFiles(Callback &&callback) -> void {
  const Vec<Path> paths = findFiles(fileQuery<Function, Parameter>());
  std::ranges::for_each(paths, [&callback](const Path &path) constexpr -> void {
    std::invoke(std::forward<Callback>(callback), path);
  });
}

template <std::meta::info Function, std::meta::info Parameter, class Callback>
constexpr auto forEachProviderValue(Callback &&callback) -> void {
  validateProviderParameter<Function, Parameter>();

  if constexpr (providerKindOf<Function, Parameter>() == ProviderKind::Values)
    forEachValues<Function, Parameter>(std::forward<Callback>(callback));
  else if constexpr (providerKindOf<Function, Parameter>() == ProviderKind::Files)
    forEachFiles<Function, Parameter>(std::forward<Callback>(callback));
  else
    static_assert(meta::always_false_v<meta::TypeObject<Parameter>>,
        "Switch attempted to enumerate a parameter without a provider binding.");
}

template <std::meta::info Function, usize ParameterIndex = 0>
consteval auto providerParameterCount() -> usize {
  if constexpr (ParameterIndex == ReflectedFunctionMetadata<Function>::parameters.size()) {
    return 0;
  } else {
    constexpr std::meta::info parameter = ReflectedFunctionMetadata<Function>::parameters[ParameterIndex];
    return (isProviderParameter<Function, parameter>() ? 1 : 0) +
           providerParameterCount<Function, ParameterIndex + 1>();
  }
}

template <std::meta::info Function, usize ParameterIndex, usize CandidateIndex = 0>
consteval auto providerArgumentIndex() -> usize {
  if constexpr (CandidateIndex == ParameterIndex) {
    return 0;
  } else {
    constexpr std::meta::info parameter = ReflectedFunctionMetadata<Function>::parameters[CandidateIndex];
    return (isProviderParameter<Function, parameter>() ? 1 : 0) +
           providerArgumentIndex<Function, ParameterIndex, CandidateIndex + 1>();
  }
}

template <std::meta::info Function, usize ProviderIndex, usize ParameterIndex = 0>
consteval auto providerParameterIndex() -> usize {
  static_assert(ParameterIndex < ReflectedFunctionMetadata<Function>::parameters.size(),
      "Switch provider parameter index is outside the reflected function signature.");

  constexpr std::meta::info parameter = ReflectedFunctionMetadata<Function>::parameters[ParameterIndex];
  if constexpr (isProviderParameter<Function, parameter>()) {
    if constexpr (ProviderIndex == 0)
      return ParameterIndex;
    else
      return providerParameterIndex<Function, ProviderIndex - 1, ParameterIndex + 1>();
  } else {
    return providerParameterIndex<Function, ProviderIndex, ParameterIndex + 1>();
  }
}

template <std::meta::info Function, usize ParameterIndex = 0>
consteval auto firstProviderLocation() -> std::source_location {
  if constexpr (ParameterIndex == ReflectedFunctionMetadata<Function>::parameters.size()) {
    return std::meta::source_location_of(Function);
  } else {
    constexpr std::meta::info parameter = ReflectedFunctionMetadata<Function>::parameters[ParameterIndex];
    if constexpr (isProviderParameter<Function, parameter>())
      return std::meta::source_location_of(parameter);
    else
      return firstProviderLocation<Function, ParameterIndex + 1>();
  }
}

template <std::meta::info Function, usize ParameterIndex, class Callback, class... Values>
auto forEachProviderCombinationImpl(Callback &&callback, usize &count, const Values &...values) -> void {
  if constexpr (ParameterIndex == ReflectedFunctionMetadata<Function>::parameters.size()) {
    std::invoke(std::forward<Callback>(callback), values...);
    ++count;
  } else {
    constexpr std::meta::info parameter = ReflectedFunctionMetadata<Function>::parameters[ParameterIndex];

    if constexpr (isProviderParameter<Function, parameter>()) {
      forEachProviderValue<Function, parameter>([&callback, &count, &values...](const auto &value) -> void {
        forEachProviderCombinationImpl<Function, ParameterIndex + 1>(
            std::forward<Callback>(callback), count, values..., value);
      });
    } else {
      forEachProviderCombinationImpl<Function, ParameterIndex + 1>(
          std::forward<Callback>(callback), count, values...);
    }
  }
}

template <std::meta::info Function, class Callback>
auto forEachProviderCombination(Callback &&callback) -> usize {
  validateArgumentBindings<Function>();
  validateProviderParameters<Function>();

  usize count{};
  forEachProviderCombinationImpl<Function, 0>(std::forward<Callback>(callback), count);
  return count;
}

template <class Value>
[[nodiscard]] auto providerValueText(const Value &value) -> String {
  using Type = std::remove_cvref_t<Value>;

  if constexpr (StringLike<Type>) {
    return std::format("\"{}\"", StringView{value});
  } else if constexpr (std::same_as<Type, Path>) {
    return value.generic_string();
  } else if constexpr (OptionalLike<Type>) {
    return value ? providerValueText(*value) : String{"None"};
  } else if constexpr (std::formattable<Type, char>) {
    return std::format("{}", value);
  } else {
    return "<unformattable>";
  }
}

template <std::meta::info Function, usize ProviderIndex = 0, class Tuple>
auto appendProviderDescription(String &result, const Tuple &values) -> void {
  if constexpr (ProviderIndex < std::tuple_size_v<std::remove_cvref_t<Tuple>>) {
    constexpr usize parameterIndex = providerParameterIndex<Function, ProviderIndex>();
    constexpr std::meta::info parameter = ReflectedFunctionMetadata<Function>::parameters[parameterIndex];
    constexpr StringView name = meta::identifier<parameter>;

    if (not result.empty())
      result.append(", ");

    result.append(name.empty() ? "value" : name);
    result.append("=");
    result.append(providerValueText(std::get<ProviderIndex>(values)));
    appendProviderDescription<Function, ProviderIndex + 1>(result, values);
  }
}

template <std::meta::info Function, class... Values>
[[nodiscard]] auto providerDescription(const Values &...values) -> String {
  const auto valueTuple = std::forward_as_tuple(values...);
  String result{};
  appendProviderDescription<Function>(result, valueTuple);
  return result;
}

template <std::meta::info Function, usize ProviderIndex = 0>
auto appendMissingProviderDescription(String &result) -> void {
  if constexpr (ProviderIndex < providerParameterCount<Function>()) {
    constexpr usize parameterIndex = providerParameterIndex<Function, ProviderIndex>();
    constexpr std::meta::info parameter = ReflectedFunctionMetadata<Function>::parameters[parameterIndex];
    constexpr StringView name = meta::identifier<parameter>;

    if (not result.empty())
      result.append(", ");

    result.append(name.empty() ? "value" : name);
    result.append("=<no values>");
    appendMissingProviderDescription<Function, ProviderIndex + 1>(result);
  }
}

template <std::meta::info Function>
[[nodiscard]] auto missingProviderDescription() -> String {
  String result{};
  appendMissingProviderDescription<Function>(result);
  return result;
}

} // namespace
// NOLINTEND(readability-identifier-naming, bugprone-reserved-identifier)

} // namespace Switch
