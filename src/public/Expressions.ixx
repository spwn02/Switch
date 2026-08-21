export module Switch:Expressions;

import std;
import Miracle;

import :Diagnostics;

using namespace Miracle;

export namespace Switch {

struct [[nodiscard]] Expression final {
  bool passed{};
  Option<Diagnostic> diagnostic;

  explicit operator bool() const noexcept {
    return passed;
  }
};

template <class Value>
struct Expected final {
  Value value;
};

template <class Value>
struct Captured final {
  StringView label;
  Ref<Value> value;
};

template <class Value>
[[nodiscard]] constexpr auto expect(Value &&value) -> Expected<std::decay_t<Value>> {
  return Expected<std::decay_t<Value>>{
      .value = std::forward<Value>(value),
  };
}

template <class Value>
[[nodiscard]] constexpr auto capture(StringView label, Value &value) -> Captured<Value> {
  return Captured<Value>{
      .label = label,
      .value = std::ref(value),
  };
}

[[nodiscard]] constexpr auto operator""_exp(unsigned long long value) -> Expected<u64> {
  return expect(static_cast<u64>(value));
}

[[nodiscard]] constexpr auto operator""_exp(long double value) -> Expected<f64> {
  return expect(static_cast<f64>(value));
}

[[nodiscard]] constexpr auto operator""_exp(char value) -> Expected<char> {
  return expect(value);
}

[[nodiscard]] constexpr auto operator""_exp(const char *value, usize size) -> Expected<StringView> {
  return expect(StringView{value, size});
}

namespace detail {

template <class Value>
constexpr auto valueOf(const Value &value) -> const Value & {
  return value; // NOLINT(bugprone-return-const-ref-from-parameter)
}

template <class Value>
constexpr auto valueOf(const Expected<Value> &expected) -> const Value & {
  return expected.value;
}

template <class Value>
constexpr auto valueOf(const Captured<Value> &captured) -> const Value & {
  return captured.value.get();
}

template <class Value>
constexpr auto labelOf(const Value & /*value*/, StringView fallback) -> StringView {
  return fallback;
}

template <class Value>
constexpr auto labelOf(const Captured<Value> &captured, StringView /*fallback*/) -> StringView {
  return captured.label;
}

// NOLINTBEGIN(readability-identifier-naming)
template <class>
inline constexpr bool is_captured_v{};

template <class Value>
inline constexpr bool is_captured_v<Captured<Value>>{true};
// NOLINTEND(readability-identifier-naming)

auto appendFragment(Vec<DiagnosticFragment> &fragments, String text, bool highlighted) -> void {
  if (text.empty())
    return;

  if (not fragments.empty() and fragments.back().highlighted == highlighted) {
    fragments.back().text.append(text);
    return;
  }

  fragments.push_back(DiagnosticFragment{
      .text = std::move(text),
      .highlighted = highlighted,
  });
}

[[nodiscard]] constexpr auto visibleCharacter(char value) -> String {
  switch (value) {
    case ' ': return "·";
    case '\t': return "⇥";
    case '\n': return "↵";
    case '\r': return "␍";
    default: return String(1, value); // NOLINT
  }
}

auto appendVisible(Vec<DiagnosticFragment> &fragments, StringView value, bool highlighted) -> void {
  std::ranges::for_each(value,
      [&](char character) -> void { appendFragment(fragments, visibleCharacter(character), highlighted); });
}

[[nodiscard]] auto visibleString(StringView value) -> String {
  Vec<DiagnosticFragment> fragments{};
  appendVisible(fragments, value, false);

  String result{};
  std::ranges::for_each(
      fragments, [&result](const DiagnosticFragment &fragment) -> void { result.append(fragment.text); });
  return result;
}

constexpr usize rangeValueLimit{16};

template <class Value>
[[nodiscard]] auto valueText(const Value &value) -> String;

template <std::ranges::forward_range Range>
[[nodiscard]] auto rangeText(const Range &range) -> String {
  String result{"["};
  usize index{};
  bool truncated{};

  std::ranges::for_each(range | std::views::take(rangeValueLimit + usize{1}),
      [&result, &index, &truncated](const auto &element) -> void {
        if (index == rangeValueLimit) {
          truncated = true;
          return;
        }

        result.append(index == 0 ? "" : ", ");
        result.append(valueText(element));
        ++index;
      });

  if (truncated)
    result.append(index == 0 ? "…" : ", …");

  result.append("]");
  return result;
}

template <class Value>
[[nodiscard]] auto valueText(const Value &value) -> String {
  using Type = std::remove_cvref_t<Value>;

  if constexpr (StringLike<Type>) {
    return std::format("\"{}\"", visibleString(value));
  } else if constexpr (std::same_as<Type, Path>) {
    return value.generic_string();
  } else if constexpr (std::ranges::forward_range<Type>) {
    return rangeText(value);
  } else if constexpr (std::formattable<Type, char>) {
    return std::format("{}", value);
  } else {
    return "<unformattable value>";
  }
}

[[nodiscard]] auto makeFailure(StringView /*description*/, Option<std::source_location> location)
    -> Diagnostic {
  Diagnostic diagnostic{};
  diagnostic.header.code = DiagnosticCode::AssertionFailed;

  if (location)
    diagnostic.addSpan(makeSpan({}, SpanKind::Primary, *location, SpanSelection::Point));

  return diagnostic;
}

template <class Value>
auto addCaptureNote(Diagnostic &diagnostic, const Value &value) -> void {
  using Type = std::remove_cvref_t<Value>;
  if constexpr (is_captured_v<Type>)
    diagnostic.addNote(std::format("{}: {}", labelOf(value, "value"), valueText(valueOf(value))));
}

template <class Left, class Right>
auto addCaptureNotes(Diagnostic &diagnostic, const Left &left, const Right &right) -> void {
  addCaptureNote(diagnostic, left);
  addCaptureNote(diagnostic, right);
}

auto addStringNote(Diagnostic &diagnostic, StringView label, StringView value, usize prefix, usize suffix)
    -> void {
  Vec<DiagnosticFragment> fragments{};
  appendFragment(fragments, std::format("{}: \"", label), false);
  appendVisible(fragments, value.substr(0, prefix), false);

  const usize mismatchSize = value.size() - prefix - suffix;
  if (mismatchSize == 0)
    appendFragment(fragments, "∅", true);
  else
    appendVisible(fragments, value.substr(prefix, mismatchSize), true);

  appendVisible(fragments, value.substr(value.size() - suffix), false);
  appendFragment(fragments, "\"", false);
  diagnostic.addNote(std::move(fragments));
}

auto addStringDifference(Diagnostic &diagnostic,
    StringView leftLabel,
    StringView left,
    StringView rightLabel,
    StringView right) -> void {
  const auto prefixMismatch = std::ranges::mismatch(left, right);
  const usize prefix = static_cast<usize>(std::ranges::distance(left.begin(), prefixMismatch.in1));
  const StringView leftTail = left.substr(prefix);
  const StringView rightTail = right.substr(prefix);
  const auto suffixMismatch =
      std::ranges::mismatch(leftTail | std::views::reverse, rightTail | std::views::reverse);
  const usize suffix = static_cast<usize>(std::ranges::distance(leftTail.rbegin(), suffixMismatch.in1));

  addStringNote(diagnostic, leftLabel, left, prefix, suffix);
  addStringNote(diagnostic, rightLabel, right, prefix, suffix);
}

template <class Left, class Right>
auto comparisonExpression(bool passed,
    StringView description,
    StringView operatorName,
    const Left &left,
    const Right &right,
    Option<std::source_location> location,
    bool highlightStringDifference = false) -> Expression {
  if (passed)
    return Expression{
        .passed = true,
    };

  Diagnostic diagnostic = makeFailure(description, location);
  const auto &leftValue = valueOf(left);
  const auto &rightValue = valueOf(right);
  diagnostic.addExpansion(BinaryExpansion{
      .left = valueText(leftValue),
      .operatorName = String{operatorName},
      .right = valueText(rightValue),
  });

  if constexpr (StringLike<std::remove_cvref_t<decltype(leftValue)>> and
                StringLike<std::remove_cvref_t<decltype(rightValue)>>) {
    if (highlightStringDifference) {
      addStringDifference(diagnostic, labelOf(left, "left"), leftValue, labelOf(right, "right"), rightValue);

    } else {
      addCaptureNotes(diagnostic, left, right);
    }
  } else {
    addCaptureNotes(diagnostic, left, right);
  }

  return Expression{
      .passed = false,
      .diagnostic = std::move(diagnostic),
  };
}

template <class Left, class Right>
auto equal(const Left &left, const Right &right, Option<std::source_location> location) -> Expression {
  const auto &leftValue = valueOf(left);
  const auto &rightValue = valueOf(right);

  if constexpr (StringLike<std::remove_cvref_t<decltype(leftValue)>> and
                StringLike<std::remove_cvref_t<decltype(rightValue)>>) {
    return comparisonExpression(StringView{leftValue} == StringView{rightValue},
        "values are not equal",
        "!=",
        left,
        right,
        std::move(location),
        true);
  } else {
    static_assert(
        requires { static_cast<bool>(leftValue == rightValue); },
        "Switch::eq requires a bool-testable equality comparison.");
    return comparisonExpression(static_cast<bool>(leftValue == rightValue),
        "values are not equal",
        "!=",
        left,
        right,
        std::move(location));
  }
}

template <class Left, class Right>
auto notEquals(const Left &left, const Right &right, Option<std::source_location> location) -> Expression {
  const auto &leftValue = valueOf(left);
  const auto &rightValue = valueOf(right);

  if constexpr (StringLike<std::remove_cvref_t<decltype(leftValue)>> and
                StringLike<std::remove_cvref_t<decltype(rightValue)>>) {
    return comparisonExpression(StringView{leftValue} != StringView{rightValue},
        "values are equal",
        "==",
        left,
        right,
        std::move(location));
  } else {
    static_assert(
        requires { static_cast<bool>(leftValue != rightValue); },
        "Switch::neq requires a bool-testable inequality comparison.");
    return comparisonExpression(static_cast<bool>(leftValue != rightValue),
        "values are equal",
        "==",
        left,
        right,
        std::move(location));
  }
}

template <class Left, class Right, class Predicate>
auto ordered(const Left &left,
    const Right &right,
    StringView description,
    StringView operatorName,
    Predicate predicate,
    Option<std::source_location> location) -> Expression {
  const auto &leftValue = valueOf(left);
  const auto &rightValue = valueOf(right);
  static_assert(
      requires { static_cast<bool>(predicate(leftValue, rightValue)); },
      "Switch ordering comparisons require a bool-testable predicate.");
  return comparisonExpression(static_cast<bool>(predicate(leftValue, rightValue)),
      description,
      operatorName,
      left,
      right,
      std::move(location));
}

} // namespace detail

template <class Left, class Right>
[[nodiscard]] auto eq(const Left &left,
    const Right &right,
    std::source_location location = std::source_location::current()) -> Expression {
  return detail::equal(left, right, location);
}

template <class Left, class Right>
[[nodiscard]] auto neq(const Left &left,
    const Right &right,
    std::source_location location = std::source_location::current()) -> Expression {
  return detail::notEquals(left, right, location);
}

template <class Left, class Right>
[[nodiscard]] auto less(const Left &left,
    const Right &right,
    std::source_location location = std::source_location::current()) -> Expression {
  return detail::ordered(
      left, right, "left value is not less than right value", "<", std::less<>{}, location);
}

template <class Left, class Right>
[[nodiscard]] auto greater(const Left &left,
    const Right &right,
    std::source_location location = std::source_location::current()) -> Expression {
  return detail::ordered(
      left, right, "left value is not greater than right value", ">", std::greater<>{}, location);
}

template <class Left, class Right>
[[nodiscard]] auto lessOrEqual(const Left &left,
    const Right &right,
    std::source_location location = std::source_location::current()) -> Expression {
  return detail::ordered(
      left, right, "left value is greater than right value", "<=", std::less_equal<>{}, location);
}

template <class Left, class Right>
[[nodiscard]] auto greaterOrEqual(const Left &left,
    const Right &right,
    std::source_location location = std::source_location::current()) -> Expression {
  return detail::ordered(
      left, right, "left value is less than right value", ">=", std::greater_equal<>{}, location);
}

template <class Left, class Right, class Tolerance>
[[nodiscard]] auto near(const Left &left,
    const Right &right,
    const Tolerance &tolerance,
    std::source_location location = std::source_location::current()) -> Expression {
  const auto &leftValue = detail::valueOf(left);
  const auto &rightValue = detail::valueOf(right);
  const auto &toleranceValue = detail::valueOf(tolerance);
  using LeftType = std::remove_cvref_t<decltype(leftValue)>;
  using RightType = std::remove_cvref_t<decltype(rightValue)>;
  using ToleranceType = std::remove_cvref_t<decltype(toleranceValue)>;
  static_assert(std::is_arithmetic_v<LeftType> and std::is_arithmetic_v<RightType> and
                    std::is_arithmetic_v<ToleranceType>,
      "Switch::near requires arithmetic values and tolerance.");

  const long double difference =
      std::fabs(static_cast<long double>(leftValue) - static_cast<long double>(rightValue));
  const auto limit = static_cast<long double>(toleranceValue);
  if (difference <= limit)
    return Expression{
        .passed = true,
    };

  Diagnostic diagnostic = detail::makeFailure("values are not within tolerance", location);
  diagnostic.addExpansion(NearExpansion{
      .left = detail::valueText(leftValue),
      .right = detail::valueText(rightValue),
      .tolerance = detail::valueText(toleranceValue),
      .difference = std::format("{}", difference),
  });
  detail::addCaptureNote(diagnostic, left);
  detail::addCaptureNote(diagnostic, right);
  detail::addCaptureNote(diagnostic, tolerance);
  return Expression{
      .passed = false,
      .diagnostic = std::move(diagnostic),
  };
}

template <class Range, class Value>
[[nodiscard]] auto contains(const Range &range,
    const Value &value,
    std::source_location location = std::source_location::current()) -> Expression {
  const auto &rangeValue = detail::valueOf(range);
  const auto &expectedValue = detail::valueOf(value);
  bool passed{};

  if constexpr (StringLike<std::remove_cvref_t<decltype(rangeValue)>> and
                StringLike<std::remove_cvref_t<decltype(expectedValue)>>) {
    passed = StringView{rangeValue}.find(StringView{expectedValue}) != StringView::npos;
  } else {
    static_assert(
        std::ranges::forward_range<std::remove_cvref_t<decltype(rangeValue)>> and requires {
          std::ranges::find(rangeValue, expectedValue);
        }, "Switch::contains requires a searchable forward range.");
    passed = std::ranges::find(rangeValue, expectedValue) != std::ranges::end(rangeValue);
  }

  if (passed)
    return Expression{
        .passed = true,
    };

  Diagnostic diagnostic = detail::makeFailure("value does not contain the expected element", location);
  diagnostic.addExpansion(ContainsExpansion{
      .needle = detail::valueText(expectedValue),
      .container = detail::valueText(rangeValue),
  });
  detail::addCaptureNote(diagnostic, range);
  detail::addCaptureNote(diagnostic, value);
  return Expression{
      .passed = false,
      .diagnostic = std::move(diagnostic),
  };
}

template <class Left, class Right>
[[nodiscard]] auto operator==(const Left &left, const Expected<Right> &right) -> Expression {
  return detail::equal(left, right, None);
}

template <class Left, class Right>
[[nodiscard]] auto operator!=(const Left &left, const Expected<Right> &right) -> Expression {
  return detail::notEquals(left, right, None);
}

template <class Left, class Right>
[[nodiscard]] auto operator<(const Left &left, const Expected<Right> &right) -> Expression {
  return detail::ordered(left, right, "left value is not less than right value", "<", std::less<>{}, None);
}

template <class Left, class Right>
[[nodiscard]] auto operator>(const Left &left, const Expected<Right> &right) -> Expression {
  return detail::ordered(
      left, right, "left value is not greater than right value", ">", std::greater<>{}, None);
}

template <class Left, class Right>
[[nodiscard]] auto operator<=(const Left &left, const Expected<Right> &right) -> Expression {
  return detail::ordered(
      left, right, "left value is greater than right value", "<=", std::less_equal<>{}, None);
}

template <class Left, class Right>
[[nodiscard]] auto operator>=(const Left &left, const Expected<Right> &right) -> Expression {
  return detail::ordered(
      left, right, "left value is less than right value", ">=", std::greater_equal<>{}, None);
}

} // namespace Switch
