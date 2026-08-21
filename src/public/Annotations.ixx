export module Switch:Annotations;

import std;
import Miracle;

using namespace Miracle;

// NOLINTBEGIN(readability-identifier-naming, bugprone-reserved-identifier)
// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
export namespace Switch {

template <usize N>
consteval auto valueItem(const char (&item)[N]) -> meta::StaticString<N> {
  return meta::StaticString<N>{item};
}

template <class Item>
consteval auto valueItem(Item &&item) -> std::decay_t<Item> {
  return std::forward<Item>(item);
}

} // namespace Switch

export namespace Switch::diagnostics {

template <usize Size>
struct Message final {
  meta::StaticString<Size> message_;

  constexpr explicit Message(meta::StaticString<Size> message)
      : message_(std::move(message)) {
  }

  [[nodiscard]] constexpr auto apply() const -> StringView {
    return message_.apply();
  }
};

template <usize Size>
consteval auto message(const char (&message)[Size]) -> Message<Size> {
  return Message<Size>{Switch::valueItem(message)};
}

template <class>
inline constexpr bool is_message_v{};

template <usize Size>
inline constexpr bool is_message_v<Message<Size>>{true};

template <std::meta::info Info>
consteval auto annotationMessage() -> StringView {
  template for (constexpr std::meta::info annotation : meta::annotations<Info>) {
    using Annotation = meta::TypeObject<annotation>;

    if constexpr (is_message_v<Annotation>) {
      return std::meta::extract<Annotation>(annotation).apply();
    }
  }

  return meta::identifier<Info>;
}

template <usize Size>
struct Prefix final {
  meta::StaticString<Size> prefix_;

  constexpr explicit Prefix(meta::StaticString<Size> prefix)
      : prefix_(std::move(prefix)) {
  }

  [[nodiscard]] constexpr auto apply() const -> StringView {
    return prefix_.apply();
  }
};

template <usize Size>
consteval auto prefix(const char (&prefix)[Size]) -> Prefix<Size> {
  return Prefix<Size>{Switch::valueItem(prefix)};
}

template <class>
inline constexpr bool is_prefix_v{};

template <usize Size>
inline constexpr bool is_prefix_v<Prefix<Size>>{true};

template <std::meta::info Item>
consteval auto annotationPrefix() -> StringView {
  template for (constexpr std::meta::info annotation : meta::annotations<Item>) {
    using Annotation = meta::TypeObject<annotation>;

    if constexpr (is_prefix_v<Annotation>) {
      return std::meta::extract<Annotation>(annotation).apply();
    }
  }

  return "E";
}

} // namespace Switch::diagnostic

export namespace Switch {

/// `[[= test]]` marks a callable for Switch discovery.
///
/// The type and the inline object intentionally share the spelling: `test::only(...)` uses the type, while
/// `[[= test]]` uses the inline annotation object. The public `Test` alias preserves the marker type used by
/// reflection-based discovery.
struct test final {
  template <class Function>
  static constexpr auto only(Function &&function) -> void {
    std::forward<Function>(function).template operator()<true>();
  }
};

using Test = test;

inline constexpr test test{};

struct Fixture final {};

inline constexpr Fixture fixture{};

/// Binds an explicit subject object to a non-static member test.
template <class Value>
struct Subject final {
  Value value_;

  constexpr explicit Subject(Value value)
      : value_(std::move(value)) {
  }

  [[nodiscard]] constexpr auto value() const noexcept -> const Value & {
    return value_;
  }
};

template <class Value>
consteval auto subject(Value &&value) -> Subject<std::remove_cvref_t<Value>> {
  return Subject<std::remove_cvref_t<Value>>{std::forward<Value>(value)};
}

template <class>
inline constexpr bool is_subject_v{};

template <class Value>
inline constexpr bool is_subject_v<Subject<Value>>{true};

struct Once final {};

inline constexpr Once once{};

template <usize Size>
struct Resource final {
  meta::StaticString<Size> name_;

  constexpr explicit Resource(meta::StaticString<Size> name)
      : name_(std::move(name)) {
  }

  [[nodiscard]] constexpr auto apply() const -> StringView {
    return name_.apply();
  }
};

template <usize Size>
consteval auto resource(const char (&name)[Size]) -> Resource<Size> {
  return Resource<Size>(valueItem(name));
}

template <class>
inline constexpr bool is_resource_v{};

template <usize Size>
inline constexpr bool is_resource_v<Resource<Size>>{true};

struct ParallelAttempts final {};

inline constexpr ParallelAttempts parallelAttempts{};

struct ContextParameter final {};

inline constexpr ContextParameter context{};

struct FromCase final {};

inline constexpr FromCase fromCase{};

struct Trace final {};

inline constexpr Trace trace{};

/// Forces the annotated test case through the process-per-case fault boundary.
///
/// This is useful when a run globally uses `CrashIsolation::InProcess`, but one subject may experience
/// aborts, invalid memory access, illegal instructions, or another native fault.
struct Isolated final {};

inline constexpr Isolated isolated{};

/// Keeps the annotated orchestration test in the parent process.
///
/// This is intended for tests that launch nested `runAll()` calls. It prevents the orchestration test itself
/// from consuming the worker boundary before it can launch and inspect isolated child cases.
struct Parent final {};

inline constexpr Parent parent{};

struct IncludeDotFiles final {};

inline constexpr IncludeDotFiles includeDotFiles{};

template <meta::StaticString Name>
struct ArgumentName final {
  [[nodiscard]] static constexpr auto apply() -> StringView {
    return Name.apply();
  }
};

template <class... Items>
struct AnnotationItems;

template <>
struct AnnotationItems<> final {};

template <class First, class... Rest>
struct AnnotationItems<First, Rest...> final {
  First first_;
  AnnotationItems<Rest...> rest_;

  constexpr explicit AnnotationItems(First first, Rest... rest)
      : first_(std::move(first))
      , rest_(std::move(rest)...) {
  }
};

template <usize Index, class First, class... Rest>
[[nodiscard]] constexpr auto annotationItem(const AnnotationItems<First, Rest...> &items) -> const auto & {
  if constexpr (Index == 0)
    return items.first_;
  else
    return annotationItem<Index - 1>(items.rest_);
}

template <class Function, class... Items, usize... Indices>
constexpr auto applyAnnotationItems(const AnnotationItems<Items...> &items,
    Function &&function,
    std::index_sequence<Indices...> /*unused*/) -> decltype(auto) {
  return std::invoke(std::forward<Function>(function), annotationItem<Indices>(items)...);
}

template <ArgumentName Name, class... Properties>
class Argument final {
public:
  constexpr explicit Argument(Properties... properties)
      : properties_(std::move(properties)...) {
  }

  [[nodiscard]] static constexpr auto name() -> StringView {
    return Name.apply();
  }

  template <usize Index>
  [[nodiscard]] constexpr auto property() const -> const auto & {
    return annotationItem<Index>(properties_);
  }

  template <class Function>
  constexpr auto apply(Function &&function) const -> decltype(auto) {
    return applyAnnotationItems(
        properties_, std::forward<Function>(function), std::index_sequence_for<Properties...>{});
  }

  AnnotationItems<Properties...> properties_;
};

template <class>
inline constexpr bool is_argument_v{};

template <ArgumentName Name, class... Properties>
inline constexpr bool is_argument_v<Argument<Name, Properties...>>{true};

template <ArgumentName Name>
class ArgumentBuilder final {
public:
  template <class... Properties>
  consteval auto operator()(Properties... properties) const -> Argument<Name, std::decay_t<Properties>...> {
    return Argument<Name, std::decay_t<Properties>...>{std::move(properties)...};
  }
};

template <meta::StaticString Name>
inline constexpr ArgumentBuilder<ArgumentName<Name>{}> arg{};

template <class... Items>
struct Values final {
  static constexpr usize size_{sizeof...(Items)};

  constexpr explicit Values(Items... items)
      : items_(std::move(items)...) {
  }

  template <class Function>
  constexpr auto apply(Function &&function) const -> decltype(auto) {
    return applyAnnotationItems(
        items_, std::forward<Function>(function), std::index_sequence_for<Items...>{});
  }

  AnnotationItems<Items...> items_;
};

template <class... Items>
consteval auto values(Items &&...items) -> Values<decltype(valueItem(std::forward<Items>(items)))...> {
  return Values<decltype(valueItem(std::forward<Items>(items)))...>{valueItem(std::forward<Items>(items))...};
}

// NOLINTBEGIN(readability-identifier-naming)
template <class>
inline constexpr bool is_values_v{};

template <class... Items>
inline constexpr bool is_values_v<Values<Items...>>{true};
// NOLINTEND(readability-identifier-naming)

/// Stores annotation-safe values and materializes Container when a Case invokes its test.
template <class Container, class... Items>
struct ContainerCase final {
  constexpr explicit ContainerCase(Items... items)
      : items_(std::move(items)...) {
  }

  [[nodiscard]] auto apply() const -> Container {
    return applyAnnotationItems(
        items_,
        []<class... Values>(const Values &...values) -> Container { return Container{values...}; },
        std::index_sequence_for<Items...>{});
  }

  [[nodiscard]] auto describe() const -> String {
    String result{"["};
    bool first{true};
    applyAnnotationItems(
        items_,
        [&result, &first]<class... Values>(const Values &...values) -> void {
          const auto append = [&result, &first]<class Value>(const Value &value) -> void {
            result.append(first ? "" : ", ");
            if constexpr (requires { value.apply(); })
              result.append(value.apply());
            else
              result.append(std::format("{}", value));
            first = false;
          };
          (append(values), ...);
        },
        std::index_sequence_for<Items...>{});
    result.append("]");
    return result;
  }

  AnnotationItems<Items...> items_;
};

template <class Container, class... Items>
consteval auto container(Items &&...items)
    -> ContainerCase<Container, decltype(valueItem(std::forward<Items>(items)))...> {
  return ContainerCase<Container, decltype(valueItem(std::forward<Items>(items)))...>{
      valueItem(std::forward<Items>(items))...};
}

template <class>
inline constexpr bool is_container_case_v{};

template <class Container, class... Items>
inline constexpr bool is_container_case_v<ContainerCase<Container, Items...>>{true};

template <class Value>
constexpr auto materializeCaseValue(const Value &value) -> decltype(auto) {
  if constexpr (is_container_case_v<std::remove_cvref_t<Value>>)
    return value.apply();
  else
    return (value);
}

template <usize Size>
struct Files final {
  meta::StaticString<Size> files_;

  explicit constexpr Files(meta::StaticString<Size> files)
      : files_(std::move(files)) {
  }

  [[nodiscard]] constexpr auto apply() const -> StringView {
    return files_.apply();
  }
};

template <usize Size>
consteval auto files(const char (&files)[Size]) -> Files<Size> {
  return Files<Size>{valueItem(files)};
}

template <class>
inline constexpr bool is_files_v{};

template <usize Size>
inline constexpr bool is_files_v<Files<Size>>{true};

template <usize Size>
struct Exclude final {
  meta::StaticString<Size> exclude_;

  explicit constexpr Exclude(meta::StaticString<Size> exclude)
      : exclude_(std::move(exclude)) {
  }

  [[nodiscard]] constexpr auto apply() const -> StringView {
    return exclude_.apply();
  }
};

template <usize Size>
consteval auto exclude(const char (&pattern)[Size]) -> Exclude<Size> {
  return Exclude<Size>{valueItem(pattern)};
}

template <class>
inline constexpr bool is_exclude_v{};

template <usize Size>
inline constexpr bool is_exclude_v<Exclude<Size>>{true};

template <usize Size>
struct ShouldPanic final {
  meta::StaticString<Size> message_;

  explicit constexpr ShouldPanic(meta::StaticString<Size> message)
      : message_(std::move(message)) {
  }

  [[nodiscard]] constexpr auto apply() const -> StringView {
    return message_.apply();
  }
};

template <usize Size>
consteval auto shouldPanic(const char (&message)[Size]) -> ShouldPanic<Size> {
  return ShouldPanic<Size>{valueItem(message)};
}

consteval auto shouldPanic() -> ShouldPanic<1> {
  return ShouldPanic<1>{valueItem("")};
}

template <class>
inline constexpr bool is_should_panic_v{};

template <usize Size>
inline constexpr bool is_should_panic_v<ShouldPanic<Size>>{true};

struct Timeout final {
  long long nanoseconds{};

  [[nodiscard]] constexpr auto apply() const noexcept -> std::chrono::nanoseconds {
    return std::chrono::nanoseconds{nanoseconds};
  }
};

template <class Rep, class Period>
consteval auto timeout(std::chrono::duration<Rep, Period> duration) -> Timeout {
  return Timeout{
      .nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count(),
  };
}

/// Declares the number of measured samples for one logical test case.
struct Repeat final {
  usize count{1};

  [[nodiscard]] constexpr auto apply() const noexcept -> usize {
    return count;
  }
};

consteval auto repeat(usize count) -> Repeat {
  if (count == 0)
    fatal("Switch repeat() requires at least one measured sample.");
  return Repeat{.count = count};
}

/// Declares the number of unreported warmup attempts before measured samples.
struct Warmup final {
  usize count{1};

  [[nodiscard]] constexpr auto apply() const noexcept -> usize {
    return count;
  }
};

consteval auto warmup(usize count) -> Warmup {
  return Warmup{.count = count};
}

/// Declares the number of additional timeout-only attempts for each measured sample.
struct Retry final {
  usize count{1};

  [[nodiscard]] constexpr auto apply() const noexcept -> usize {
    return count;
  }
};

consteval auto retry(usize count) -> Retry {
  return Retry{.count = count};
}

template <usize Size>
struct Description final {
  meta::StaticString<Size> description_;

  explicit constexpr Description(meta::StaticString<Size> description)
      : description_(std::move(description)) {
  }

  [[nodiscard]] constexpr auto apply() const -> StringView {
    return description_.apply();
  }
};

template <usize Size>
consteval auto description(const char (&description)[Size]) -> Description<Size> {
  return Description<Size>{valueItem(description)};
}

template <class>
inline constexpr bool is_description_v{};

template <usize Size>
inline constexpr bool is_description_v<Description<Size>>{true};

/// Assigns one presentation-oriented group to a test.
template <usize Size>
struct Group final {
  meta::StaticString<Size> name_;

  constexpr explicit Group(meta::StaticString<Size> name)
      : name_(std::move(name)) {
  }

  [[nodiscard]] constexpr auto apply() const -> StringView {
    return name_.apply();
  }
};

template <usize Size>
consteval auto group(const char (&name)[Size]) -> Group<Size> {
  return Group<Size>{valueItem(name)};
}

template <class>
inline constexpr bool is_group_v{};

template <usize Size>
inline constexpr bool is_group_v<Group<Size>>{true};

/// Assigns one or more arbitrary labels to a test for selection and reporting.
template <class... Names>
struct Tags final {
  static constexpr usize size_{sizeof...(Names)};
  AnnotationItems<Names...> names_;

  constexpr explicit Tags(Names... names)
      : names_(std::move(names)...) {
  }

  template <class Function>
  constexpr auto apply(Function &&function) const -> decltype(auto) {
    return applyAnnotationItems(
        names_, std::forward<Function>(function), std::index_sequence_for<Names...>{});
  }
};

template <usize... Sizes>
  requires(sizeof...(Sizes) != 0)
consteval auto tag(const char (&...names)[Sizes]) -> Tags<meta::StaticString<Sizes>...> {
  return Tags<meta::StaticString<Sizes>...>{valueItem(names)...};
}

template <class>
inline constexpr bool is_tag_v{};

template <class... Names>
inline constexpr bool is_tag_v<Tags<Names...>>{true};

template <usize Count, class Function>
constexpr auto withIndices(Function &&function) -> decltype(auto) {
  return [&]<usize... indices>(std::index_sequence<indices...>) -> decltype(auto) {
    return std::forward<Function>(function)(std::integral_constant<usize, indices>{}...);
  }(std::make_index_sequence<Count>{});
}

template <class... Values>
struct Case final {
  struct Storage;

  consteval {
    std::meta::define_aggregate(^^Storage,
        {
            std::meta::data_member_spec(^^Values)...});
  }

  static constexpr auto members_ = meta::nsMembers<^^Storage, meta::AccessContext::unchecked()>;

  Storage storage_;

  constexpr explicit Case(Values... values)
      : storage_{std::move(values)...} {
  }

  template <class Function>
  constexpr auto apply(Function &&function) const -> decltype(auto) {
    return withIndices<sizeof...(Values)>([this, &function](auto... indices) -> decltype(auto) {
      return std::invoke(
          std::forward<Function>(function), materializeCaseValue(storage_.[:members_[indices]:])...);
    });
  }

  [[nodiscard]]
  auto describe() const -> String {
    String result{};
    bool first{true};

    apply([&]<class... Types>(const Types &...values) -> void {
      const auto append = [&]<class Type>(const Type &value) -> void {
        result.append(first ? "" : ", ");
        if constexpr (requires { value.describe(); })
          result.append(value.describe());
        else if constexpr (requires { value.apply(); })
          result.append(value.apply());
        else
          result.append(std::format("{}", value));
        first = false;
      };
      (append(values), ...);
    });
    return result;
  }
};

template <class... Values>
Case(Values &&...) -> Case<decltype(valueItem(std::declval<Values>()))...>;

} // namespace Switch
// NOLINTEND(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
// NOLINTEND(readability-identifier-naming, bugprone-reserved-identifier)
