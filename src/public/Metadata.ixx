export module Switch:Metadata;

import std;
import Miracle;

import :Annotations;
import :Context;

using namespace Miracle;

// NOLINTBEGIN(bugprone-reserved-identifier, readability-identifier-naming)
export namespace Switch::detail {

template <class>
struct IsValues final : std::false_type {};

template <class... Items>
struct IsValues<Values<Items...>> final : std::true_type {};

template <class>
struct IsFiles final : std::false_type {};

template <usize Size>
struct IsFiles<Files<Size>> final : std::true_type {};

template <class>
struct IsExclude final : std::false_type {};

template <usize Size>
struct IsExclude<Exclude<Size>> final : std::true_type {};

template <class>
struct IsIncludeDotFiles final : std::false_type {};

template <>
struct IsIncludeDotFiles<IncludeDotFiles> final : std::true_type {};

template <class>
struct IsContextParameter final : std::false_type {};

template <>
struct IsContextParameter<ContextParameter> final : std::true_type {};

template <class>
struct IsFromCaseParameter final : std::false_type {};

template <>
struct IsFromCaseParameter<FromCase> final : std::true_type {};

template <class Property>
inline constexpr bool is_supported_property_v =
    IsValues<Property>::value or IsFiles<Property>::value or IsExclude<Property>::value or
    IsIncludeDotFiles<Property>::value or IsContextParameter<Property>::value or
    IsFromCaseParameter<Property>::value;

template <class Type>
struct IsDescription final : std::bool_constant<is_description_v<Type>> {};

template <class Type>
struct IsShouldPanic final : std::bool_constant<is_should_panic_v<Type>> {};

template <class Type>
struct IsGroup final : std::bool_constant<is_group_v<Type>> {};

template <class Type>
struct IsTag final : std::bool_constant<is_tag_v<Type>> {};

template <class Type>
struct IsTimeout final : std::bool_constant<std::same_as<Type, Timeout>> {};

template <class Type>
struct IsRepeat final : std::bool_constant<std::same_as<Type, Repeat>> {};

template <class Type>
struct IsWarmup final : std::bool_constant<std::same_as<Type, Warmup>> {};

template <class Type>
struct IsRetry final : std::bool_constant<std::same_as<Type, Retry>> {};

template <class Type>
struct IsTrace final : std::bool_constant<std::same_as<Type, Trace>> {};

template <class Type>
struct IsIsolated final : std::bool_constant<std::same_as<Type, Isolated>> {};

template <class Type>
struct IsParent final : std::bool_constant<std::same_as<Type, Parent>> {};

template <class Type>
struct IsTestMarker final : std::bool_constant<std::same_as<Type, Test>> {};

template <class Type>
struct IsFixtureMarker final : std::bool_constant<std::same_as<Type, Fixture>> {};

template <class Type>
struct IsOnceMarker final : std::bool_constant<std::same_as<Type, Once>> {};

template <class Type>
struct IsSubject final : std::bool_constant<is_subject_v<Type>> {};

template <class Type>
struct IsResource final : std::bool_constant<is_resource_v<Type>> {};

template <class Type>
struct IsParallelAttempts final : std::bool_constant<std::same_as<Type, ParallelAttempts>> {};

template <template <class> class Predicate, usize Index, class... Properties>
struct FirstPropertyIndex;

template <template <class> class Predicate, usize Index, class First, class... Rest>
struct FirstPropertyIndex<Predicate, Index, First, Rest...>
    : std::conditional_t<Predicate<First>::value,
          std::integral_constant<usize, Index>,
          FirstPropertyIndex<Predicate, Index + 1, Rest...>> {};

template <class>
struct ArgumentTraits;

template <ArgumentName Name, class... Properties>
struct ArgumentTraits<Argument<Name, Properties...>> final {
  [[nodiscard]] static constexpr auto name() -> StringView {
    return Argument<Name, Properties...>::name();
  }

  template <template <class> class Predicate>
  [[nodiscard]] static consteval auto count() -> usize {
    return (static_cast<usize>(Predicate<Properties>::value) + ... + usize{});
  }

  template <template <class> class Predicate>
  [[nodiscard]] static consteval auto contains() -> bool {
    return count<Predicate>() != 0;
  }

  template <template <class> class Predicate>
  [[nodiscard]] static consteval auto firstIndex() -> usize {
    return FirstPropertyIndex<Predicate, 0, Properties...>::value;
  }

  [[nodiscard]] static consteval auto propertiesAreSupported() -> bool {
    return (is_supported_property_v<Properties> and ... and true);
  }
};

template <std::meta::info Parameter, class ArgumentType>
consteval auto argumentTargetsParameter() -> bool {
  return ArgumentTraits<ArgumentType>::name() == meta::identifier<Parameter>;
}

template <std::meta::info Parameter, class ArgumentType>
inline constexpr bool argument_targets_parameter_v = argumentTargetsParameter<Parameter, ArgumentType>();

/// Identifies the source used to bind one reflected test parameter.
enum class ParameterSource : u8 {
  Automatic,
  Context,
  Case,
  Values,
  Files,
};

/// Stores one direct parameter annotation or one legacy function-level argument binding.
struct ParameterProperty final {
  std::meta::info parameter{};
  std::meta::info annotation{};
  bool legacy{};
};

template <std::meta::info Annotation>
consteval auto isCaseAnnotation() -> bool {
  using namespace std::meta;

  const info type = dealias(type_of(Annotation));
  return has_template_arguments(type) and template_of(type) == ^^Case;
}

template <std::meta::info Annotation>
inline constexpr bool is_case_annotation_v = isCaseAnnotation<Annotation>();

template <std::meta::info Function>
consteval auto makeCaseAnnotations() -> auto {
  Vec<std::meta::info> result{};

  template for (constexpr std::meta::info annotation : meta::annotations<Function>) {
    if constexpr (is_case_annotation_v<annotation>)
      result.emplace_back(annotation);
  }

  return std::define_static_array(result);
}

template <std::meta::info Function>
consteval auto makeArgumentAnnotations() -> auto {
  Vec<std::meta::info> result{};

  template for (constexpr std::meta::info annotation : meta::annotations<Function>) {
    using Annotation = meta::TypeObject<annotation>;

    if constexpr (is_argument_v<Annotation>)
      result.emplace_back(annotation);
  }

  return std::define_static_array(result);
}

template <std::meta::info Function, template <class> class Predicate>
consteval auto makeTypedAnnotations() -> auto {
  Vec<std::meta::info> result{};

  template for (constexpr std::meta::info annotation : meta::annotations<Function>) {
    using Annotation = meta::TypeObject<annotation>;

    if constexpr (Predicate<Annotation>::value)
      result.emplace_back(annotation);
  }

  return std::define_static_array(result);
}

template <std::meta::info Function>
consteval auto makeParameterProperties() -> auto {
  Vec<ParameterProperty> result{};

  template for (constexpr std::meta::info parameter : meta::parameters<Function>) {
    template for (constexpr std::meta::info annotation : meta::annotations<parameter>) {
      using Annotation = meta::TypeObject<annotation>;

      if constexpr (is_supported_property_v<Annotation>)
        result.emplace_back(parameter, annotation, false);
    }
  }

  template for (constexpr std::meta::info annotation : meta::annotations<Function>) {
    using Annotation = meta::TypeObject<annotation>;

    if constexpr (is_argument_v<Annotation>) {
      template for (constexpr std::meta::info parameter : meta::parameters<Function>) {
        if constexpr (argumentTargetsParameter<parameter, Annotation>()) {
          result.emplace_back(parameter, annotation, true);
        }
      }
    }
  }

  return std::define_static_array(result);
}

/// Caches all normalized reflection inputs for one function.
///
/// Reflection APIs are queried here exactly once per function. Providers, fixtures, and discovery consume
/// these stable arrays instead of repeatedly asking the compiler for the same function annotations and
/// parameters. `ParameterProperty` unifies direct annotations with legacy `arg<...>` data.
template <std::meta::info Function>
struct ReflectedFunctionMetadata final {
  static constexpr auto &annotations = meta::annotations<Function>;
  static constexpr auto &parameters = meta::parameters<Function>;
  static constexpr auto cases = makeCaseAnnotations<Function>();
  static constexpr auto arguments = makeArgumentAnnotations<Function>();
  static constexpr auto descriptions = makeTypedAnnotations<Function, IsDescription>();
  static constexpr auto expectedPanics = makeTypedAnnotations<Function, IsShouldPanic>();
  static constexpr auto timeouts = makeTypedAnnotations<Function, IsTimeout>();
  static constexpr auto repeats = makeTypedAnnotations<Function, IsRepeat>();
  static constexpr auto warmups = makeTypedAnnotations<Function, IsWarmup>();
  static constexpr auto retries = makeTypedAnnotations<Function, IsRetry>();
  static constexpr auto groups = makeTypedAnnotations<Function, IsGroup>();
  static constexpr auto tags = makeTypedAnnotations<Function, IsTag>();
  static constexpr auto traces = makeTypedAnnotations<Function, IsTrace>();
  static constexpr auto isolated = makeTypedAnnotations<Function, IsIsolated>();
  static constexpr auto parents = makeTypedAnnotations<Function, IsParent>();
  static constexpr auto testMarkers = makeTypedAnnotations<Function, IsTestMarker>();
  static constexpr auto fixtureMarkers = makeTypedAnnotations<Function, IsFixtureMarker>();
  static constexpr auto onceMarkers = makeTypedAnnotations<Function, IsOnceMarker>();
  static constexpr auto subjects = makeTypedAnnotations<Function, IsSubject>();
  static constexpr auto resources = makeTypedAnnotations<Function, IsResource>();
  static constexpr auto parallelAttempts = makeTypedAnnotations<Function, IsParallelAttempts>();
  static constexpr auto parameterProperties = makeParameterProperties<Function>();
};

template <std::meta::info Function, std::meta::info Parameter>
consteval auto directPropertyCount() -> usize {
  usize count{};

  template for (constexpr ParameterProperty property :
      ReflectedFunctionMetadata<Function>::parameterProperties) {
    if constexpr (property.parameter == Parameter and not property.legacy)
      ++count;
  }

  return count;
}

template <std::meta::info Function, std::meta::info Parameter>
consteval auto argumentBindingCount() -> usize {
  usize count{};

  template for (constexpr std::meta::info annotation : ReflectedFunctionMetadata<Function>::arguments) {
    using Annotation = meta::TypeObject<annotation>;

    if constexpr (argumentTargetsParameter<Parameter, Annotation>())
      ++count;
  }

  return count;
}

template <std::meta::info Function, class ArgumentType>
consteval auto argumentTargetsFunctionParameter() -> bool {
  template for (constexpr std::meta::info parameter : ReflectedFunctionMetadata<Function>::parameters) {
    if constexpr (argumentTargetsParameter<parameter, ArgumentType>())
      return true;
  }

  return false;
}

template <std::meta::info Function, std::meta::info Parameter, template <class> class Predicate>
consteval auto directPropertyCountFor() -> usize {
  usize count{};

  template for (constexpr ParameterProperty property :
      ReflectedFunctionMetadata<Function>::parameterProperties) {
    if constexpr (property.parameter == Parameter and not property.legacy) {
      using Annotation = meta::TypeObject<property.annotation>;

      if constexpr (Predicate<Annotation>::value)
        ++count;
    }
  }

  return count;
}

template <std::meta::info Function, std::meta::info Parameter, template <class> class Predicate>
consteval auto legacyPropertyCount() -> usize {
  usize count{};

  template for (constexpr std::meta::info annotation : ReflectedFunctionMetadata<Function>::arguments) {
    using Annotation = meta::TypeObject<annotation>;

    if constexpr (argumentTargetsParameter<Parameter, Annotation>() and
                  ArgumentTraits<Annotation>::template contains<Predicate>())
      count += ArgumentTraits<Annotation>::template count<Predicate>();
  }

  return count;
}

template <std::meta::info Function, std::meta::info Parameter, template <class> class Predicate>
consteval auto argumentPropertyCount() -> usize {
  return directPropertyCountFor<Function, Parameter, Predicate>() +
         legacyPropertyCount<Function, Parameter, Predicate>();
}

template <std::meta::info Function, std::meta::info Parameter>
consteval auto parameterSource() -> ParameterSource {
  if constexpr (std::same_as<meta::TypeObject<Parameter>, Context> or
                argumentPropertyCount<Function, Parameter, IsContextParameter>() != 0)
    return ParameterSource::Context;
  else if constexpr (argumentPropertyCount<Function, Parameter, IsFromCaseParameter>() != 0)
    return ParameterSource::Case;
  else if constexpr (argumentPropertyCount<Function, Parameter, IsValues>() != 0)
    return ParameterSource::Values;
  else if constexpr (argumentPropertyCount<Function, Parameter, IsFiles>() != 0)
    return ParameterSource::Files;
  else
    return ParameterSource::Automatic;
}

} // namespace Switch::detail
// NOLINTEND(bugprone-reserved-identifier, readability-identifier-naming)
