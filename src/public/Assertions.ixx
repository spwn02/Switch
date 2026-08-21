export module Switch:Assertions;

import std;
import Miracle;

import :Diagnostics;
import :Environment;
import :Expressions;

using namespace Miracle;

export namespace Switch {

namespace detail {

template <class Condition>
concept BoolTestable = requires(Condition &&condition) {
  { static_cast<bool>(std::forward<Condition>(condition)) } -> std::same_as<bool>;
};

[[nodiscard]] auto activeEnvironment() -> TestEnvironment & {
  const Option<Ref<TestEnvironment>> environment = currentEnvironment();
  if (not environment)
    fatal("Switch assertions require an active test environment");

  return environment->get();
}

template <bool AbortOnFailure>
auto recordFailure(Diagnostic diagnostic, std::source_location location) -> bool {
  TestEnvironment &environment = activeEnvironment();
  auto primary = std::ranges::find_if(diagnostic.details.spans, isPrimarySpan);
  if (primary == diagnostic.details.spans.end()) {
    diagnostic.addSpan(makeSpan({}, SpanKind::Primary, location, SpanSelection::EnclosingExpression));
    primary = std::prev(diagnostic.details.spans.end());
  }

  if (primary->label.empty())
    primary->label = AbortOnFailure ? "requirement" : "assertion";

  if constexpr (AbortOnFailure) {
    if (not diagnostic.header.descriptionOverride)
      diagnostic.header.descriptionOverride = "requirement failed";
  }

  environment.recordFailure(std::move(diagnostic));
  if constexpr (AbortOnFailure) {
    environment.abort();
    throw TestAbort{};
  }

  return false;
}

template <bool AbortOnFailure, BoolTestable Condition>
auto evaluate(Condition &&condition, std::source_location location) -> bool {
  TestEnvironment &environment = activeEnvironment();
  if (static_cast<bool>(std::forward<Condition>(condition))) {
    environment.recordPass();
    return true;
  }

  Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::AssertionFailed, location);
  if (not diagnostic.details.spans.empty())
    diagnostic.details.spans.front().selection = SpanSelection::EnclosingExpression;
  return recordFailure<AbortOnFailure>(std::move(diagnostic), location);
}

template <bool AbortOnFailure>
auto evaluate(Expression expression, std::source_location location) -> bool {
  TestEnvironment &environment = activeEnvironment();
  if (expression.passed) {
    environment.recordPass();
    return true;
  }

  if (expression.diagnostic)
    return recordFailure<AbortOnFailure>(std::move(*expression.diagnostic), location);

  Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::AssertionFailed);
  if (not diagnostic.details.spans.empty())
    diagnostic.details.spans.front().selection = SpanSelection::EnclosingExpression;
  return recordFailure<AbortOnFailure>(std::move(diagnostic), location);
}

} // namespace detail

/// Records a failed diagnostic and lets the current test continue.
auto check(Expression expression, std::source_location location = std::source_location::current()) -> bool {
  return detail::evaluate<false>(std::move(expression), location);
}

/// Records a failed diagnostic and lets the current test continue.
template <detail::BoolTestable Condition>
auto check(Condition &&condition, std::source_location location = std::source_location::current()) -> bool {
  return detail::evaluate<false>(std::forward<Condition>(condition), location);
}

/// Records a failed diagnostic and aborts only the current test.
auto require(Expression expression, std::source_location location = std::source_location::current()) -> void {
  static_cast<void>(detail::evaluate<true>(std::move(expression), location));
}

/// Records a failed diagnostic and aborts only the current test.
template <detail::BoolTestable Condition>
auto require(Condition &&condition, std::source_location location = std::source_location::current()) -> bool {
  return detail::evaluate<true>(std::forward<Condition>(condition), location);
}

} // namespace Switch
