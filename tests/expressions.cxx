import std;

import Miracle;
import Switch;

using namespace Miracle;
using namespace Switch;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
namespace Tests::expressions {

auto containsHighlighted(const Vec<DiagnosticFragment> &fragments, StringView text) -> bool {
  return std::ranges::any_of(fragments, [text](const DiagnosticFragment &fragment) -> bool {
    return fragment.highlighted and fragment.text == text;
  });
}

[[ = test, = group("framework"), = tag("expressions") ]] auto compactLiteralsUseTheAssertionLocation()
    -> void {
  const auto location = std::source_location::current();

  Diagnostic diagnostic{};

  {
    constexpr u32 value{2};
    TestEnvironment environment{};
    EnvironmentBinding binding{environment};
    const Expression expression = value == 3_exp;
    check(expression, location);
    diagnostic = environment.state().diagnostics.front();
  };

  check(diagnostic.details.spans.front().location.line() == location.line());
  check(diagnostic.details.spans.front().label == "assertion"_exp);
}

[[ = test, = group("framework"), = tag("expressions") ]] auto literalOperatorsProduceExpressions() -> void {
  constexpr u32 one{1};
  constexpr u32 two{2};
  constexpr u32 three{3};

  check(two == 2_exp);
  check(two != 3_exp);
  check(one < 2_exp);
  check(three > 2_exp);
  check(two <= 2_exp);
  check(two >= 2_exp);
}

[[ = test, = group("framework"), = tag("expressions") ]] auto stringDifferencesVisualizeWhitespace() -> void {
  Vec<DiagnosticNote> notes{};

  {
    TestEnvironment environment{};
    EnvironmentBinding binding{environment};
    const auto location = std::source_location::current();

    static_cast<void>(check(eq(StringView{"Switch Test"}, "Switch  Test"_exp, location)));

    notes = environment.state().diagnostics.front().details.notes;
  };

  require(notes.size() == 2);
  check(containsHighlighted(notes.front().fragments, "∅"));
  check(containsHighlighted(notes.back().fragments, "·"));
}

[[ = test, = group("framework"), = tag("expressions") ]] auto utilityComparatorsProduceExpressions() -> void {
  TestState state{};
  Expression rangeFailure{};

  {
    TestEnvironment environment{};
    EnvironmentBinding binding{environment};

    check(neq(2, 3));
    check(less(2, 3));
    check(greater(3, 2));
    check(lessOrEqual(3, 3));
    check(greaterOrEqual(3, 3));
    check(near(1.0, 1.01, 0.1));
    check(contains(StringView{"Switch Test"}, "Test"_exp));
    check(contains(Vec<u32>{1, 2, 3}, 2_exp));
    check(near(1.0, 1.2, 0.1));
    check(contains(StringView{"Switch"}, "Test"_exp));
    rangeFailure = contains(Vec<u32>{1, 2, 3}, 4_exp);

    state = environment.state();
  }

  require(state.assertions == 10_exp);
  require(state.failedAssertions == 2_exp);
  require(state.diagnostics.size() == 2_exp);
  check(state.diagnostics.front().description() == "assertion failed"_exp);

  const auto &nearExpansion = std::get<NearExpansion>(state.diagnostics.front().details.expansion);
  check(nearExpansion.left == "1"_exp);
  check(nearExpansion.right == "1.2"_exp);
  check(nearExpansion.tolerance == "0.1"_exp);

  const auto &containsExpansion = std::get<ContainsExpansion>(state.diagnostics.back().details.expansion);
  check(containsExpansion.needle == "\"Test\""_exp);
  check(containsExpansion.container == "\"Switch\""_exp);

  check(rangeFailure.diagnostic);
  const auto &rangeExpansion = std::get<ContainsExpansion>(rangeFailure.diagnostic->details.expansion);
  check(rangeExpansion.container == "[1, 2, 3]"_exp);
  check(rangeExpansion.needle == "4"_exp);
}

[[ = test, = group("framework"), = tag("expressions") ]] auto requiredExpressionsAbortTheTest() -> void {
  TestState state{};
  bool continued{};

  {
    TestEnvironment environment{};
    EnvironmentBinding binding{environment};

    try {
      require(eq(2, 3));
      continued = true;
    } catch (const detail::TestAbort &) { // NOLINT(bugprone-empty-catch)
    }

    state = environment.state();
  }

  require(not continued);
  require(state.aborted);
  require(state.failedAssertions == 1_exp);
  check(state.diagnostics.front().description() == "requirement failed"_exp);

  const auto &expansion = std::get<BinaryExpansion>(state.diagnostics.front().details.expansion);
  check(expansion.left == "2"_exp);
  check(expansion.operatorName == "!="_exp);
  check(expansion.right == "3"_exp);
  check(state.diagnostics.front().details.spans.front().label == "requirement"_exp);
}

[[ = test, = group("framework"), = tag("expressions") ]] auto returnedExpressionsPreserveTheirDiagnostics()
    -> void {
  const TestExecution execution = run("returnsExpression", [] -> Expression { return eq(u32{2}, 3_exp); });

  require(execution.failed());
  require(execution.state.failedAssertions == 1_exp);
  require(execution.state.errors == 0_exp);
  check(execution.state.diagnostics.front().description() == "assertion failed"_exp);

  const auto &expansion = std::get<BinaryExpansion>(execution.state.diagnostics.front().details.expansion);
  require(expansion.operatorName == "!="_exp);
}

[[ = test, = group("framework"), = tag("expressions") ]] auto comparisonsExposeValuesAndLocations() -> void {
  const auto location = std::source_location::current();
  bool passed{};
  bool failed{};
  TestState state{};

  {
    constexpr u32 value{2};
    TestEnvironment environment{};
    EnvironmentBinding binding{environment};

    passed = check(eq(value, 2_exp, location));
    failed = check(eq(capture("answer", value), 3_exp, location));
    state = environment.state();
  };

  require(passed);
  require(not failed);
  require(state.assertions == 2_exp);
  require(state.failedAssertions == 1_exp);
  require(state.diagnostics.size() == 1_exp);
  check(state.diagnostics.front().description() == "assertion failed"_exp);
  check(state.diagnostics.front().details.spans.front().location.line() == location.line());
  check(state.diagnostics.front().details.spans.front().label == "assertion"_exp);

  const auto &expansion = std::get<BinaryExpansion>(state.diagnostics.front().details.expansion);
  check(expansion.left == "2"_exp);
  check(expansion.operatorName == "!="_exp);
  check(expansion.right == "3"_exp);
  check(state.diagnostics.front().details.notes.size() == 1_exp);
  check(state.diagnostics.front().details.notes.front().message == "answer: 2"_exp);
}

} // namespace Tests::expressions
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

consteval {
  discover<^^Tests::expressions>();
}
