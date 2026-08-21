module Switch;

import std;
import Miracle;

import :Discovery;
import :FaultIsolation;
import :Worker;
import :Session;

using namespace Miracle;

namespace Switch {

namespace {

[[nodiscard]] auto locationComesBefore(const std::source_location &left, const std::source_location &right)
    -> bool {
  const StringView leftFile{left.file_name()};
  const StringView rightFile{right.file_name()};

  if (leftFile != rightFile)
    return leftFile < rightFile;

  if (left.line() != right.line())
    return left.line() < right.line();

  return left.column() < right.column();
}

[[nodiscard]] auto suiteComesBefore(const SuiteEntry &left, const SuiteEntry &right) -> bool {
  if (left.scope != right.scope)
    return left.scope < right.scope;

  return locationComesBefore(left.location, right.location);
}

class SuiteCatalog final {
public:
  auto append(const SuiteEntry &suite) -> void {
    const std::scoped_lock lock{mutex_};
    suites_.emplace(suite);
  }

  [[nodiscard]] auto snapshot() const -> Vec<SuiteEntry> {
    const std::scoped_lock lock{mutex_};
    Vec<SuiteEntry> result = suites_ | std::ranges::to<Vec<SuiteEntry>>();
    std::ranges::sort(result, suiteComesBefore);
    return result;
  }

private:
  mutable std::mutex mutex_;
  Hive<SuiteEntry> suites_;
};

[[nodiscard]] auto catalog() -> SuiteCatalog & {
  static SuiteCatalog catalog_{};
  return catalog_;
}

[[nodiscard]] auto workerCatalog() -> SuiteCatalog & {
  static SuiteCatalog catalog_{};
  return catalog_;
}

[[nodiscard]] auto registeredSuites() -> Vec<SuiteEntry> {
  return catalog().snapshot();
}

[[nodiscard]] auto registeredWorkerSuites() -> Vec<SuiteEntry> {
  return workerCatalog().snapshot();
}

[[nodiscard]] auto globMatches(StringView pattern, StringView value) -> bool {
  usize patternIndex{};
  usize valueIndex{};
  Option<usize> starIndex{};
  Option<usize> restartIndex{};

  while (valueIndex < value.size()) {
    if (patternIndex < pattern.size() and
        (pattern[patternIndex] == '?' or pattern[patternIndex] == value[valueIndex])) {
      ++patternIndex;
      ++valueIndex;
    } else if (patternIndex < pattern.size() and pattern[patternIndex] == '*') {
      starIndex = patternIndex++;
      restartIndex = valueIndex;
    } else if (starIndex) {
      patternIndex = *starIndex + 1;
      valueIndex = ++*restartIndex;
    } else {
      return false;
    }
  }

  while (patternIndex < pattern.size() and pattern[patternIndex] == '*')
    ++patternIndex;

  return patternIndex == pattern.size();
}

[[nodiscard]] auto matchesAny(const Vec<String> &patterns, StringView value) -> bool {
  return std::ranges::any_of(
      patterns, [value](const String &pattern) -> bool { return globMatches(pattern, value); });
}

[[nodiscard]] auto hasTag(const TestDescriptor &descriptor, StringView tag) -> bool {
  return std::ranges::contains(descriptor.metadata.tags, tag);
}

// auto validateUniqueIdentifiers(const Vec<TestDescriptor> &descriptors) -> void {
//   const auto duplicate =
//       std::ranges::adjacent_find(descriptors, std::ranges::equal_to{}, &TestDescriptor::identifier);
//   if (duplicate == descriptors.end())
//     return;
//
//   throw std::logic_error{
//       std::format("Switch discovered duplicate test identifier: {}", duplicate->identifier)};
// }

} // namespace

namespace detail {

auto filterDescriptors(Vec<TestDescriptor> descriptors, const TestSelection &selection)
    -> Vec<TestDescriptor> {
  std::erase_if(descriptors,
      [&selection](const TestDescriptor &descriptor) -> bool { return not selection.matches(descriptor); });
  return descriptors;
}

auto filterPlannedCases(detail::RunSession &session, const TestSelection &selection) -> void {
  session.erasePlannedCases([&selection](const detail::PlannedCase &plannedCase) -> bool {
    return not selection.matches(plannedCase.descriptor());
  });
}

[[nodiscard]] auto describeSuites(const Vec<SuiteEntry> &suites) -> Vec<TestDescriptor> {
  Vec<TestDescriptor> descriptors{};
  std::ranges::for_each(suites, [&descriptors](const SuiteEntry &suite) -> void {
    descriptors.append_range(suite.describe() | std::views::as_rvalue);
  });
  std::ranges::sort(descriptors, {}, &TestDescriptor::identifier);
  // validateUniqueIdentifiers(descriptors);
  return descriptors;
}

auto appendRegisteredSuite(const SuiteEntry &suite) -> void {
  catalog().append(suite);
}

auto appendWorkerSuite(const SuiteEntry &suite) -> void {
  workerCatalog().append(suite);
}

} // namespace detail

auto TestSelection::matches(const TestDescriptor &descriptor) const -> bool {
  if (not include.empty() and not matchesAny(include, descriptor.identifier))
    return false;

  if (matchesAny(exclude, descriptor.identifier))
    return false;

  if (group and (not descriptor.metadata.group or *descriptor.metadata.group != *group))
    return false;

  if (not std::ranges::all_of(
          tagsAll, [&descriptor](const String &tag) -> bool { return hasTag(descriptor, tag); }))
    return false;

  return tagsAny.empty() or std::ranges::any_of(tagsAny,
                                [&descriptor](const String &tag) -> bool { return hasTag(descriptor, tag); });
}

auto discover() -> Vec<TestDescriptor> {
  return detail::describeSuites(registeredSuites());
}

auto list(TestSelection selection) -> Vec<TestDescriptor> { // NOLINT
  return detail::filterDescriptors(detail::describeSuites(registeredSuites()), selection);
}

auto runAllDetailed(RunOptions options) -> Vec<TestExecution> {
  return runAllDetailed({}, options);
}

auto runAllDetailed(TestSelection selection, RunOptions selectedOptions) -> Vec<TestExecution> { // NOLINT
  RunOptions options = selectedOptions;
  const Option<detail::WorkerRequest> worker = detail::consumeWorkerRequest();
  if (worker)
    static_cast<void>(detail::isolation::installWorkerFaultHandler(worker->faultPath));
  Vec<SuiteEntry> suites = registeredSuites();
  if (worker) {
    suites.append_range(registeredWorkerSuites() | std::views::as_rvalue);
    std::ranges::stable_sort(suites, suiteComesBefore);
  }
  detail::RunSession session{};

  std::ranges::for_each(suites, [&session](const SuiteEntry &suite) -> void { suite.plan(session); });

  if (worker) {
    detail::executeWorkerCase(session, *worker, options);
    // A worker has already persisted its result journal. Exit before the caller's normal main() reporter
    // can render an empty parent-side run or write to the parent's terminal.
    std::exit(0); // NOLINT(concurrency-mt-unsafe)
  }

  detail::filterPlannedCases(session, selection);

  Vec<TestExecution> executions = detail::executePlannedCases(session, options);
  std::ranges::stable_sort(executions, {}, [](const TestExecution &execution) -> const String & {
    return execution.descriptor.identifier;
  });
  return executions;
}

auto runAll(RunOptions options) -> RunReport {
  return runAll({}, options);
}

auto runAll(TestSelection selection, RunOptions options) -> RunReport {
  const Option<detail::WorkerRequest> worker = detail::consumeWorkerRequest();
  if (worker)
    static_cast<void>(detail::isolation::installWorkerFaultHandler(worker->faultPath));

  detail::RunSession session{};
  Vec<SuiteEntry> suites = registeredSuites();
  if (worker) {
    suites.append_range(registeredWorkerSuites() | std::views::as_rvalue);
    std::ranges::stable_sort(suites, suiteComesBefore);
  }
  std::ranges::for_each(suites, [&session](const SuiteEntry &suite) -> void { suite.plan(session); });

  if (worker) {
    detail::executeWorkerCase(session, *worker, options);
    std::exit(0); // NOLINT(concurrency-mt-unsafe)
  }

  detail::filterPlannedCases(session, selection);
  RunAccumulator accumulator{options.retention,
      options.maxRetainedFailures,
      SelectionMetadata{
          .include = std::move(selection.include),
          .exclude = std::move(selection.exclude),
          .tagsAll = std::move(selection.tagsAll),
          .tagsAny = std::move(selection.tagsAny),
          .group = std::move(selection.group),
      },
      options.captureTiming != CapturePolicy::None};
  static_cast<void>(detail::executePlannedCases(session, options, accumulator));
  return std::move(accumulator).finish();
}

auto runAll(Reporter &reporter, std::ostream &output, RunOptions options) -> RunReport {
  return runAll(reporter, output, {}, options);
}

auto runAll(Reporter &reporter,
    std::ostream &output,
    TestSelection selection,
    RunOptions options) -> RunReport {
  const Option<detail::WorkerRequest> worker = detail::consumeWorkerRequest();
  if (worker)
    static_cast<void>(detail::isolation::installWorkerFaultHandler(worker->faultPath));

  detail::RunSession session{};
  Vec<SuiteEntry> suites = registeredSuites();
  if (worker) {
    suites.append_range(registeredWorkerSuites() | std::views::as_rvalue);
    std::ranges::stable_sort(suites, suiteComesBefore);
  }
  std::ranges::for_each(suites, [&session](const SuiteEntry &suite) -> void { suite.plan(session); });
  if (worker) {
    detail::executeWorkerCase(session, *worker, options);
    std::exit(0); // NOLINT(concurrency-mt-unsafe)
  }
  detail::filterPlannedCases(session, selection);
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
  accumulator.setCompletionObserver([&reporter](const TestCaseResult &testCase) {
    reporter.consumeLive(testCase);
  });
  static_cast<void>(detail::executePlannedCases(session, options, accumulator));
  RunReport report = std::move(accumulator).finish();
  reporter.finishLive(report);
  return report;
}

} // namespace Switch
