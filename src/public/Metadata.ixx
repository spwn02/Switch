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
  return ArgumentTraits<ArgumentType>::name() == std::meta::identifier_of(Parameter);
}

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

template <std::meta::info Function, usize AnnotationIndex = 0>
consteval auto caseAnnotationCount() -> usize {
  constexpr usize count = static_cast<usize>(std::meta::annotations_of(Function).size());
  if constexpr (AnnotationIndex == count) {
    return 0;
  } else {
    constexpr std::meta::info annotation = std::meta::annotations_of(Function)[AnnotationIndex];
    return static_cast<usize>(isCaseAnnotation<annotation>()) +
           caseAnnotationCount<Function, AnnotationIndex + 1>();
  }
}

template <std::meta::info Function, usize Size, usize AnnotationIndex = 0>
consteval auto fillCaseAnnotations(std::array<std::meta::info, Size> &result, usize &index) -> void {
  constexpr usize count = static_cast<usize>(std::meta::annotations_of(Function).size());
  if constexpr (AnnotationIndex != count) {
    constexpr std::meta::info annotation = std::meta::annotations_of(Function)[AnnotationIndex];
    if constexpr (isCaseAnnotation<annotation>())
      result[index++] = annotation;

    fillCaseAnnotations<Function, Size, AnnotationIndex + 1>(result, index);
  }
}

template <std::meta::info Function>
consteval auto makeCaseAnnotations() -> auto {
  constexpr usize count = caseAnnotationCount<Function>();
  std::array<std::meta::info, count> result{};
  usize index{};
  fillCaseAnnotations<Function>(result, index);
  return result;
}

template <std::meta::info Function, usize AnnotationIndex = 0>
consteval auto argumentAnnotationCount() -> usize {
  constexpr usize count = static_cast<usize>(std::meta::annotations_of(Function).size());
  if constexpr (AnnotationIndex == count) {
    return 0;
  } else {
    constexpr std::meta::info annotation = std::meta::annotations_of(Function)[AnnotationIndex];
    using Annotation = meta::TypeObject<annotation>;
    return static_cast<usize>(is_argument_v<Annotation>) +
           argumentAnnotationCount<Function, AnnotationIndex + 1>();
  }
}

template <std::meta::info Function, usize Size, usize AnnotationIndex = 0>
consteval auto fillArgumentAnnotations(std::array<std::meta::info, Size> &result, usize &index) -> void {
  constexpr usize count = static_cast<usize>(std::meta::annotations_of(Function).size());
  if constexpr (AnnotationIndex != count) {
    constexpr std::meta::info annotation = std::meta::annotations_of(Function)[AnnotationIndex];
    using Annotation = meta::TypeObject<annotation>;
    if constexpr (is_argument_v<Annotation>)
      result[index++] = annotation;

    fillArgumentAnnotations<Function, Size, AnnotationIndex + 1>(result, index);
  }
}

template <std::meta::info Function>
consteval auto makeArgumentAnnotations() -> auto {
  constexpr usize count = argumentAnnotationCount<Function>();
  std::array<std::meta::info, count> result{};
  usize index{};
  fillArgumentAnnotations<Function>(result, index);
  return result;
}

template <std::meta::info Function, template <class> class Predicate, usize AnnotationIndex = 0>
consteval auto typedAnnotationCount() -> usize {
  constexpr usize count = static_cast<usize>(std::meta::annotations_of(Function).size());
  if constexpr (AnnotationIndex == count) {
    return 0;
  } else {
    constexpr std::meta::info annotation = std::meta::annotations_of(Function)[AnnotationIndex];
    using Annotation = meta::TypeObject<annotation>;
    return static_cast<usize>(Predicate<Annotation>::value) +
           typedAnnotationCount<Function, Predicate, AnnotationIndex + 1>();
  }
}

template <std::meta::info Function,
    template <class> class Predicate,
    usize Size,
    usize AnnotationIndex = 0>
consteval auto fillTypedAnnotations(std::array<std::meta::info, Size> &result, usize &index) -> void {
  constexpr usize count = static_cast<usize>(std::meta::annotations_of(Function).size());
  if constexpr (AnnotationIndex != count) {
    constexpr std::meta::info annotation = std::meta::annotations_of(Function)[AnnotationIndex];
    using Annotation = meta::TypeObject<annotation>;
    if constexpr (Predicate<Annotation>::value)
      result[index++] = annotation;

    fillTypedAnnotations<Function, Predicate, Size, AnnotationIndex + 1>(result, index);
  }
}

template <std::meta::info Function, template <class> class Predicate>
consteval auto makeTypedAnnotations() -> auto {
  constexpr usize count = typedAnnotationCount<Function, Predicate>();
  std::array<std::meta::info, count> result{};
  usize index{};
  fillTypedAnnotations<Function, Predicate>(result, index);
  return result;
}

template <std::meta::info Function, usize ParameterIndex = 0, usize AnnotationIndex = 0>
consteval auto directParameterPropertyCount() -> usize {
  constexpr usize parameterCount = static_cast<usize>(std::meta::parameters_of(Function).size());
  if constexpr (ParameterIndex == parameterCount) {
    return 0;
  } else {
    constexpr std::meta::info parameter = std::meta::parameters_of(Function)[ParameterIndex];
    constexpr usize annotationCount = static_cast<usize>(std::meta::annotations_of(parameter).size());
    if constexpr (AnnotationIndex == annotationCount) {
      return directParameterPropertyCount<Function, ParameterIndex + 1, 0>();
    } else {
      constexpr std::meta::info annotation = std::meta::annotations_of(parameter)[AnnotationIndex];
      using Annotation = meta::TypeObject<annotation>;
      return static_cast<usize>(is_supported_property_v<Annotation>) +
             directParameterPropertyCount<Function, ParameterIndex, AnnotationIndex + 1>();
    }
  }
}

template <std::meta::info Function,
    std::meta::info Annotation,
    class AnnotationType,
    usize ParameterIndex = 0>
consteval auto legacyParameterPropertyCount() -> usize {
  constexpr usize count = static_cast<usize>(std::meta::parameters_of(Function).size());
  if constexpr (ParameterIndex == count) {
    return 0;
  } else {
    constexpr std::meta::info parameter = std::meta::parameters_of(Function)[ParameterIndex];
    return static_cast<usize>(argumentTargetsParameter<parameter, AnnotationType>()) +
           legacyParameterPropertyCount<Function, Annotation, AnnotationType, ParameterIndex + 1>();
  }
}

template <std::meta::info Function, usize AnnotationIndex = 0>
consteval auto legacyParameterPropertiesCount() -> usize {
  constexpr usize count = static_cast<usize>(std::meta::annotations_of(Function).size());
  if constexpr (AnnotationIndex == count) {
    return 0;
  } else {
    constexpr std::meta::info annotation = std::meta::annotations_of(Function)[AnnotationIndex];
    using Annotation = meta::TypeObject<annotation>;
    if constexpr (is_argument_v<Annotation>) {
      return legacyParameterPropertyCount<Function, annotation, Annotation>() +
             legacyParameterPropertiesCount<Function, AnnotationIndex + 1>();
    } else {
      return legacyParameterPropertiesCount<Function, AnnotationIndex + 1>();
    }
  }
}

template <std::meta::info Function,
    usize Size,
    usize ParameterIndex = 0,
    usize AnnotationIndex = 0>
consteval auto fillDirectParameterProperties(std::array<ParameterProperty, Size> &result, usize &index)
    -> void {
  constexpr usize parameterCount = static_cast<usize>(std::meta::parameters_of(Function).size());
  if constexpr (ParameterIndex != parameterCount) {
    constexpr std::meta::info parameter = std::meta::parameters_of(Function)[ParameterIndex];
    constexpr usize annotationCount = static_cast<usize>(std::meta::annotations_of(parameter).size());
    if constexpr (AnnotationIndex == annotationCount) {
      fillDirectParameterProperties<Function, Size, ParameterIndex + 1, 0>(result, index);
    } else {
      constexpr std::meta::info annotation = std::meta::annotations_of(parameter)[AnnotationIndex];
      using Annotation = meta::TypeObject<annotation>;
      if constexpr (is_supported_property_v<Annotation>)
        result[index++] = ParameterProperty{parameter, annotation, false};

      fillDirectParameterProperties<Function, Size, ParameterIndex, AnnotationIndex + 1>(result, index);
    }
  }
}

template <std::meta::info Function,
    std::meta::info Annotation,
    class AnnotationType,
    usize Size,
    usize ParameterIndex = 0>
consteval auto fillLegacyParameterBinding(std::array<ParameterProperty, Size> &result, usize &index) -> void {
  constexpr usize count = static_cast<usize>(std::meta::parameters_of(Function).size());
  if constexpr (ParameterIndex != count) {
    constexpr std::meta::info parameter = std::meta::parameters_of(Function)[ParameterIndex];
    if constexpr (argumentTargetsParameter<parameter, AnnotationType>())
      result[index++] = ParameterProperty{parameter, Annotation, true};

    fillLegacyParameterBinding<Function, Annotation, AnnotationType, Size, ParameterIndex + 1>(result, index);
  }
}

template <std::meta::info Function, usize Size, usize AnnotationIndex = 0>
consteval auto fillLegacyParameterProperties(std::array<ParameterProperty, Size> &result, usize &index)
    -> void {
  constexpr usize count = static_cast<usize>(std::meta::annotations_of(Function).size());
  if constexpr (AnnotationIndex != count) {
    constexpr std::meta::info annotation = std::meta::annotations_of(Function)[AnnotationIndex];
    using Annotation = meta::TypeObject<annotation>;
    if constexpr (is_argument_v<Annotation>)
      fillLegacyParameterBinding<Function, annotation, Annotation>(result, index);

    fillLegacyParameterProperties<Function, Size, AnnotationIndex + 1>(result, index);
  }
}

template <std::meta::info Function>
consteval auto makeParameterProperties() -> auto {
  constexpr usize count = directParameterPropertyCount<Function>() + legacyParameterPropertiesCount<Function>();
  std::array<ParameterProperty, count> result{};
  usize index{};
  fillDirectParameterProperties<Function>(result, index);
  fillLegacyParameterProperties<Function>(result, index);
  return result;
}

/// Caches all normalized reflection inputs for one function.
///
/// GCC cannot currently persist consumer-owned reflection ranges through
/// `define_static_array`. Exact-size `std::array` values built by indexed
/// consteval queries preserve the same normalized metadata interface without
/// materializing a compiler-owned reflection range across module boundaries.
template <std::meta::info Function>
struct ReflectedFunctionMetadata final {
  static constexpr usize parameterCount = static_cast<usize>(std::meta::parameters_of(Function).size());

  template <usize Index>
  [[nodiscard]] static consteval auto parameter() -> std::meta::info {
    static_assert(Index < parameterCount);
    return std::meta::parameters_of(Function)[Index];
  }
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

template <std::meta::info Function, class ArgumentType, usize ParameterIndex = 0>
consteval auto argumentTargetsFunctionParameter() -> bool {
  constexpr usize count = static_cast<usize>(std::meta::parameters_of(Function).size());
  if constexpr (ParameterIndex == count) {
    return false;
  } else {
    constexpr std::meta::info parameter = std::meta::parameters_of(Function)[ParameterIndex];
    if constexpr (argumentTargetsParameter<parameter, ArgumentType>())
      return true;

    return argumentTargetsFunctionParameter<Function, ArgumentType, ParameterIndex + 1>();
  }
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
