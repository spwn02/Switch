module Switch;

import std;
import Miracle;

using namespace Miracle;

namespace Switch {

namespace {

inline constexpr u32 schemaVersion{3};

struct JsonFrame final {
  bool first{true};
};

class JsonWriter final {
public:
  JsonWriter(std::ostream &output, JsonReporterOptions options)
      : output_(output)
      , options_(options) {
  }

  auto beginObject() -> void {
    output_ << '{';
    frames_.push_back(JsonFrame{});
  }

  auto endObject() -> void {
    const bool hasElements = not frames_.back().first;
    frames_.pop_back();
    closeContainer('}', hasElements);
  }

  auto beginArray() -> void {
    output_ << '[';
    frames_.push_back(JsonFrame{});
  }

  auto endArray() -> void {
    const bool hasElements = not frames_.back().first;
    frames_.pop_back();
    closeContainer(']', hasElements);
  }

  template <class Function>
  auto field(StringView name, Function &&function) -> void {
    prefix();
    text(name);
    output_ << ':';
    if (options_.pretty)
      output_ << ' ';
    std::invoke(std::forward<Function>(function));
  }

  template <class Function>
  auto element(Function &&function) -> void {
    prefix();
    std::invoke(std::forward<Function>(function));
  }

  auto text(StringView value) -> void {
    constexpr StringView hexadecimal{"0123456789ABCDEF"};

    output_ << '\"';
    std::ranges::for_each(value, [this, hexadecimal](char character) -> void {
      const u8 code = static_cast<u8>(character);
      switch (code) {
        case '\"': output_ << "\\\""; return;
        case '\\': output_ << "\\\\"; return;
        case '\b': output_ << "\\b"; return;
        case '\f': output_ << "\\f"; return;
        case '\n': output_ << "\\n"; return;
        case '\r': output_ << "\\r"; return;
        case '\t': output_ << "\\t"; return;
        default: break;
      }

      // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
      if (code < 0x20) {
        output_ << "\\u00" << hexadecimal[code >> 4] << hexadecimal[code & 0x0F];
        return;
      }
      // NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

      output_ << character;
    });
    output_ << '\"';
  }

  auto boolean(bool value) -> void {
    output_ << (value ? "true" : "false");
  }

  auto nullValue() -> void {
    output_ << "null";
  }

  template <std::integral Value>
  auto number(Value value) -> void {
    output_ << std::format("{}", value);
  }

private:
  auto prefix() -> void {
    JsonFrame &frame = frames_.back();
    if (not frame.first)
      output_ << ',';

    frame.first = false;
    if (options_.pretty)
      output_ << '\n' << String(frames_.size() * options_.indentWidth, ' ');
  }

  auto closeContainer(char closing, bool hasElements) -> void {
    if (options_.pretty and hasElements)
      output_ << '\n' << String(frames_.size() * options_.indentWidth, ' ');

    output_ << closing;
  }

  std::ostream &output_; // NOLINT
  JsonReporterOptions options_{};
  Vec<JsonFrame> frames_;
};

[[nodiscard]] constexpr auto executionStatus(const TestExecution &execution) noexcept -> StringView {
  return execution.passed() ? "passed" : "failed";
}

[[nodiscard]] constexpr auto durationNanoseconds(std::chrono::steady_clock::duration duration) noexcept
    -> i64 {
  return static_cast<i64>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
}

auto writeLocation(JsonWriter &writer, const std::source_location &location) -> void {
  writer.beginObject();
  writer.field("file", [&writer, &location] -> void {
    const char *file = location.file_name();
    writer.text(file == nullptr ? StringView{} : StringView{file});
  });
  writer.field("line", [&writer, &location] -> void { writer.number(location.line()); });
  writer.field("column", [&writer, &location] -> void { writer.number(location.column()); });
  writer.endObject();
}

auto writeLocation(JsonWriter &writer, const SourceLocationData &location) -> void {
  writer.beginObject();
  writer.field("file", [&writer, &location] -> void { writer.text(location.file); });
  writer.field("line", [&writer, &location] -> void { writer.number(location.line); });
  writer.field("column", [&writer, &location] -> void { writer.number(location.column); });
  writer.endObject();
}

auto writePosition(JsonWriter &writer, const SourcePosition &position) -> void {
  writer.beginObject();
  writer.field("line", [&writer, &position] -> void { writer.number(position.line); });
  writer.field("column", [&writer, &position] -> void { writer.number(position.column); });
  writer.endObject();
}

auto writeRange(JsonWriter &writer, const SourceRange &range) -> void {
  writer.beginObject();
  writer.field("file", [&writer, &range] -> void { writer.text(range.file.generic_string()); });
  writer.field("begin", [&writer, &range] -> void { writePosition(writer, range.begin); });
  writer.field("end", [&writer, &range] -> void { writePosition(writer, range.end); });
  writer.endObject();
}

auto writeSpan(JsonWriter &writer, const SourceSpan &span, const SourceManager &sources) -> void {
  writer.beginObject();
  writer.field("kind", [&writer, &span] -> void { writer.text(debug::enumName(span.kind)); });
  writer.field("selection", [&writer, &span] -> void { writer.text(debug::enumName(span.selection)); });
  writer.field("label", [&writer, &span] -> void { writer.text(span.label); });
  writer.field("location", [&writer, &span] -> void {
    if (span.remoteLocation)
      writeLocation(writer, *span.remoteLocation);
    else
      writeLocation(writer, span.location);
  });
  writer.field("range", [&writer, &span, &sources] -> void {
    const Option<SourceResolution> resolution = sources.resolve(span);
    if (resolution)
      writeRange(writer, resolution->range);
    else
      writer.nullValue();
  });
  writer.field("highlight", [&writer, &span, &sources] -> void {
    const Option<SourceResolution> resolution = sources.resolve(span);
    if (resolution)
      writeRange(writer, resolution->highlight);
    else
      writer.nullValue();
    ;
  });
  writer.endObject();
}

auto writeOptionalSpan(JsonWriter &writer, const Option<SourceSpan> &span, const SourceManager &sources)
    -> void {
  if (span) {
    writeSpan(writer, *span, sources);
    return;
  }

  writer.nullValue();
}

auto writeFragment(JsonWriter &writer, const DiagnosticFragment &fragment) -> void {
  writer.beginObject();
  writer.field("text", [&writer, &fragment] -> void { writer.text(fragment.text); });
  writer.field("highlighted", [&writer, &fragment] -> void { writer.boolean(fragment.highlighted); });
  writer.endObject();
}

auto writeNote(JsonWriter &writer, const DiagnosticNote &note, const SourceManager &sources) -> void {
  writer.beginObject();
  writer.field("level", [&writer, &note] -> void { writer.text(debug::enumName(note.level)); });
  writer.field("message", [&writer, &note] -> void { writer.text(note.message); });
  writer.field("span", [&writer, &note, &sources] -> void { writeOptionalSpan(writer, note.span, sources); });
  writer.field("fragments", [&writer, &note] -> void {
    writer.beginArray();
    std::ranges::for_each(note.fragments, [&writer](const DiagnosticFragment &fragment) -> void {
      writer.element([&writer, &fragment] -> void { writeFragment(writer, fragment); });
    });
    writer.endArray();
  });
  writer.endObject();
}

auto writeExpansion(JsonWriter &writer, const DiagnosticExpansion &expansion) -> void {
  std::visit(
      [&](const auto &expansion) -> void {
        using Type = std::remove_cvref_t<decltype(expansion)>;
        if constexpr (std::same_as<Type, std::monostate>) {
          writer.nullValue();
        } else if constexpr (std::same_as<Type, BinaryExpansion>) {
          const BinaryExpansion &value = expansion;
          writer.beginObject();
          writer.field("kind", [&writer] -> void { writer.text("binary"); });
          writer.field("left", [&writer, &value] -> void { writer.text(value.left); });
          writer.field("operator", [&writer, &value] -> void { writer.text(value.operatorName); });
          writer.field("right", [&writer, &value] -> void { writer.text(value.right); });
          writer.endObject();
        } else if constexpr (std::same_as<Type, ContainsExpansion>) {
          const ContainsExpansion &value = expansion;
          writer.beginObject();
          writer.field("kind", [&writer] -> void { writer.text("contains"); });
          writer.field("needle", [&writer, &value] -> void { writer.text(value.needle); });
          writer.field("container", [&writer, &value] -> void { writer.text(value.container); });
          writer.endObject();
        } else if constexpr (std::same_as<Type, NearExpansion>) {
          const NearExpansion &value = expansion;
          writer.beginObject();
          writer.field("kind", [&writer] -> void { writer.text("near"); });
          writer.field("left", [&writer, &value] -> void { writer.text(value.left); });
          writer.field("right", [&writer, &value] -> void { writer.text(value.right); });
          writer.field("tolerance", [&writer, &value] -> void { writer.text(value.tolerance); });
          writer.field("difference", [&writer, &value] -> void { writer.text(value.difference); });
          writer.endObject();
        }
      },
      expansion);
}

auto writeDiagnostic(JsonWriter &writer, const Diagnostic &diagnostic, const SourceManager &sources) -> void {
  writer.beginObject();
  writer.field("level", [&writer, &diagnostic] -> void { writer.text(debug::enumName(diagnostic.level)); });
  writer.field("code", [&writer, &diagnostic] -> void { writer.text(diagnostic.code()); });
  writer.field("description", [&writer, &diagnostic] -> void { writer.text(diagnostic.description()); });
  writer.field("spans", [&writer, &diagnostic, &sources] -> void {
    writer.beginArray();
    std::ranges::for_each(diagnostic.details.spans, [&writer, &sources](const SourceSpan &span) -> void {
      writer.element([&writer, &span, &sources] -> void { writeSpan(writer, span, sources); });
    });
    writer.endArray();
  });
  writer.field(
      "expansion", [&writer, &diagnostic] -> void { writeExpansion(writer, diagnostic.details.expansion); });
  writer.field("notes", [&writer, &diagnostic, &sources] -> void {
    writer.beginArray();
    std::ranges::for_each(diagnostic.details.notes, [&writer, &sources](const DiagnosticNote &note) -> void {
      writer.element([&writer, &note, &sources] -> void { writeNote(writer, note, sources); });
    });
    writer.endArray();
  });
  writer.field("attachments", [&writer, &diagnostic] -> void {
    writer.beginArray();
    std::ranges::for_each(
        diagnostic.details.attachments, [&writer](const DiagnosticAttachment &attachment) -> void {
          writer.element([&writer, &attachment] -> void {
            writer.beginObject();
            writer.field("name", [&writer, &attachment] -> void { writer.text(attachment.name); });
            writer.field("content", [&writer, &attachment] -> void { writer.text(attachment.content); });
            writer.endObject();
          });
        });
    writer.endArray();
  });
  writer.endObject();
}

auto writeMetadata(JsonWriter &writer, const TestMetadata &metadata) -> void {
  writer.beginObject();
  writer.field("group", [&writer, &metadata] -> void {
    if (metadata.group)
      writer.text(*metadata.group);
    else
      writer.nullValue();
  });
  writer.field("tags", [&writer, &metadata] -> void {
    writer.beginArray();
    std::ranges::for_each(metadata.tags, [&writer](const String &tag) -> void {
      writer.element([&writer, &tag] -> void { writer.text(tag); });
    });
    writer.endArray();
  });
  writer.endObject();
}

auto writePolicy(JsonWriter &writer, const TestPolicy &policy) -> void {
  writer.beginObject();
  writer.field("trace", [&writer, &policy] -> void { writer.boolean(policy.trace); });
  writer.field("isolated", [&writer, &policy] -> void { writer.boolean(policy.isolated); });
  writer.field("parent", [&writer, &policy] -> void { writer.boolean(policy.parent); });
  writer.field("expected_panic", [&writer, &policy] -> void {
    if (policy.expectedPanic)
      writer.text(*policy.expectedPanic);
    else
      writer.nullValue();
  });
  writer.field("timeout_ns", [&writer, &policy] -> void {
    if (policy.timeout)
      writer.number(durationNanoseconds(*policy.timeout));
    else
      writer.nullValue();
  });
  writer.field("repeat", [&writer, &policy] -> void { writer.number(policy.repeat); });
  writer.field("warmup", [&writer, &policy] -> void { writer.number(policy.warmup); });
  writer.field("retry", [&writer, &policy] -> void { writer.number(policy.retry); });
  writer.endObject();
}

auto writeDescriptor(JsonWriter &writer, const TestDescriptor &descriptor) -> void {
  writer.beginObject();
  writer.field("identifier", [&writer, &descriptor] -> void { writer.text(descriptor.identifier); });
  writer.field("name", [&writer, &descriptor] -> void { writer.text(descriptor.name); });
  writer.field("description", [&writer, &descriptor] -> void { writer.text(descriptor.description); });
  writer.field("test_case", [&writer, &descriptor] -> void { writer.number(descriptor.testCase); });
  writer.field("location", [&writer, &descriptor] -> void { writeLocation(writer, descriptor.location); });
  writer.field("metadata", [&writer, &descriptor] -> void { writeMetadata(writer, descriptor.metadata); });
  writer.field("policy", [&writer, &descriptor] -> void { writePolicy(writer, descriptor.policy); });
  writer.endObject();
}

auto writeState(JsonWriter &writer, const TestState &state, const SourceManager &sources) -> void {
  writer.beginObject();
  writer.field("assertions", [&writer, &state] -> void { writer.number(state.assertions); });
  writer.field("failed_assertions", [&writer, &state] -> void { writer.number(state.failedAssertions); });
  writer.field("errors", [&writer, &state] -> void { writer.number(state.errors); });
  writer.field("aborted", [&writer, &state] -> void { writer.boolean(state.aborted); });
  writer.field("diagnostics", [&writer, &state, &sources] -> void {
    writer.beginArray();
    std::ranges::for_each(state.diagnostics, [&writer, &sources](const Diagnostic &diagnostic) -> void {
      writer.element(
          [&writer, &diagnostic, &sources] -> void { writeDiagnostic(writer, diagnostic, sources); });
    });
    writer.endArray();
  });
  writer.field("traces", [&writer, &state] -> void {
    writer.beginArray();
    std::ranges::for_each(state.traces, [&writer](const TraceEvent &trace) -> void {
      writer.element([&writer, &trace] -> void {
        writer.beginObject();
        writer.field("message", [&writer, &trace] -> void { writer.text(trace.message); });
        writer.field("location", [&writer, &trace] -> void {
          if (trace.remoteLocation)
            writeLocation(writer, *trace.remoteLocation);
          else
            writeLocation(writer, trace.location);
        });
        writer.endObject();
      });
    });
    writer.endArray();
  });
  writer.endObject();
}

auto writeProfile(JsonWriter &writer, const profiling::ProfileSnapshot &profile) -> void {
  writer.beginObject();
  writer.field(
      "duration_ns", [&writer, &profile] -> void { writer.number(durationNanoseconds(profile.duration)); });
  writer.field("events", [&writer, &profile] -> void {
    writer.beginArray();
    std::ranges::for_each(profile.events, [&writer](const profiling::ProfileEvent &event) -> void {
      writer.element([&writer, &event] -> void {
        writer.beginObject();
        writer.field("name", [&writer, &event] -> void { writer.text(event.name); });
        writer.field(
            "duration_ns", [&writer, &event] -> void { writer.number(durationNanoseconds(event.duration)); });
        writer.field("depth", [&writer, &event] -> void { writer.number(event.depth); });
        writer.field("location", [&writer, &event] -> void { writeLocation(writer, event.location); });
        writer.endObject();
      });
    });
    writer.endArray();
  });
  writer.field("aggregates", [&writer, &profile] -> void {
    writer.beginArray();
    std::ranges::for_each(
        profile.aggregates, [&writer](const Pair<String, profiling::ProfileAggregate> &entry) -> void {
          writer.element([&writer, &entry] -> void {
            writer.beginObject();
            writer.field("name", [&writer, &entry] -> void { writer.text(entry.first); });
            writer.field("count", [&writer, &entry] -> void { writer.number(entry.second.count); });
            writer.field("total_ns",
                [&writer, &entry] -> void { writer.number(durationNanoseconds(entry.second.total)); });
            writer.field("minimum_ns",
                [&writer, &entry] -> void { writer.number(durationNanoseconds(entry.second.minimum)); });
            writer.field("maximum_ns",
                [&writer, &entry] -> void { writer.number(durationNanoseconds(entry.second.maximum)); });
            writer.endObject();
          });
        });
    writer.endArray();
  });
  writer.endObject();
}

auto writeResources(JsonWriter &writer, const ResourceSnapshot &resources) -> void {
  writer.beginObject();
  writer.field("active", [&writer, &resources] -> void { writer.number(resources.activeResources); });
  writer.field("peak", [&writer, &resources] -> void { writer.number(resources.peakResources); });
  writer.field("cleanup_count", [&writer, &resources] -> void { writer.number(resources.cleanupCount); });
  writer.field(
      "allocation_bytes", [&writer, &resources] -> void { writer.number(resources.allocationBytes); });
  writer.field("active_allocation_bytes",
      [&writer, &resources] -> void { writer.number(resources.activeAllocationBytes); });
  writer.field("peak_allocation_bytes",
      [&writer, &resources] -> void { writer.number(resources.peakAllocationBytes); });
  writer.field("temporary_files", [&writer, &resources] -> void { writer.number(resources.temporaryFiles); });
  writer.field("temporary_directories",
      [&writer, &resources] -> void { writer.number(resources.temporaryDirectories); });
  writer.endObject();
}

auto writeMemory(JsonWriter &writer, const Option<memory::ProcessMemorySnapshot> &memory) -> void {
  if (not memory) {
    writer.nullValue();
    return;
  }

  writer.beginObject();
  writer.field("resident_bytes", [&writer, &memory] -> void { writer.number(memory->residentBytes); });
  writer.endObject();
}

auto writeFault(JsonWriter &writer, const Option<NativeFault> &fault) -> void {
  if (not fault) {
    writer.nullValue();
    return;
  }

  writer.beginObject();
  writer.field("kind", [&writer, &fault] -> void { writer.text(debug::enumName(fault->kind)); });
  writer.field("signal", [&writer, &fault] -> void { writer.text(debug::enumName(fault->signal)); });
  writer.field("code", [&writer, &fault] -> void { writer.number(fault->code); });
  writer.field("address", [&writer, &fault] -> void { writer.number(fault->address); });
  writer.field("instruction", [&writer, &fault] -> void { writer.number(fault->instruction); });
  writer.field("symbols_available", [&writer, &fault] -> void { writer.boolean(fault->symbolsAvailable); });
  writer.endObject();
}

auto writeAttemptIndex(JsonWriter &writer, const AttemptIndex &index, bool warmup) -> void {
  writer.beginObject();
  writer.field("run_iteration", [&writer, &index] -> void { writer.number(index.runIteration); });
  writer.field("sample", [&writer, &index] -> void { writer.number(index.sample); });
  writer.field("retry", [&writer, &index] -> void { writer.number(index.retry); });
  writer.field("warmup", [&writer, &warmup] -> void { writer.boolean(warmup); });
  writer.endObject();
}

auto writeMeasurement(JsonWriter &writer, const Option<MeasurementSummary> &measurement) -> void {
  if (not measurement) {
    writer.nullValue();
    return;
  }

  writer.beginObject();
  writer.field("samples", [&writer, &measurement] -> void { writer.number(measurement->sampleCount); });
  writer.field("total_ns",
      [&writer, &measurement] -> void { writer.number(durationNanoseconds(measurement->total)); });
  writer.field("minimum_ns", [&writer, &measurement] -> void {
    measurement->distributionAvailable ? writer.number(durationNanoseconds(measurement->minimum))
                                       : writer.nullValue();
  });
  writer.field("maximum_ns", [&writer, &measurement] -> void {
    measurement->distributionAvailable ? writer.number(durationNanoseconds(measurement->maximum))
                                       : writer.nullValue();
  });
  writer.field(
      "mean_ns", [&writer, &measurement] -> void { writer.number(durationNanoseconds(measurement->mean)); });
  writer.field("first_quartile_ns", [&writer, &measurement] -> void {
    measurement->quantilesAvailable ? writer.number(durationNanoseconds(measurement->firstQuartile))
                                    : writer.nullValue();
  });
  writer.field("median_ns", [&writer, &measurement] -> void {
    measurement->quantilesAvailable ? writer.number(durationNanoseconds(measurement->median))
                                    : writer.nullValue();
  });
  writer.field("third_quartile_ns", [&writer, &measurement] -> void {
    measurement->quantilesAvailable ? writer.number(durationNanoseconds(measurement->thirdQuartile))
                                    : writer.nullValue();
  });
  writer.field("deviation_ns", [&writer, &measurement] -> void {
    measurement->distributionAvailable ? writer.number(durationNanoseconds(measurement->deviation))
                                       : writer.nullValue();
  });
  writer.field("approximate", [&writer, &measurement] -> void { writer.boolean(measurement->approximate); });
  writer.field("quantiles_available",
      [&writer, &measurement] -> void { writer.boolean(measurement->quantilesAvailable); });
  writer.endObject();
}

auto writeExecution(JsonWriter &writer, const TestExecution &execution, const SourceManager &sources)
    -> void {
  writer.beginObject();
  writer.field("identifier", [&writer, &execution] -> void { writer.text(execution.descriptor.identifier); });
  writer.field("status", [&writer, &execution] -> void { writer.text(executionStatus(execution)); });
  writer.field("duration_ns",
      [&writer, &execution] -> void { writer.number(durationNanoseconds(execution.duration)); });
  writer.field("wall_duration_ns",
      [&writer, &execution] -> void { writer.number(durationNanoseconds(execution.wallDuration)); });
  writer.field("run_seed", [&writer, &execution] -> void { writer.number(execution.runSeed); });
  writer.field("seed", [&writer, &execution] -> void { writer.number(execution.seed); });
  writer.field("iteration", [&writer, &execution] -> void { writer.number(execution.iteration); });
  writer.field("attempt",
      [&writer, &execution] -> void { writeAttemptIndex(writer, execution.attempt, execution.warmup); });
  writer.field(
      "trace_mode", [&writer, &execution] -> void { writer.text(debug::enumName(execution.traceMode)); });
  writer.field(
      "descriptor", [&writer, &execution] -> void { writeDescriptor(writer, execution.descriptor); });
  writer.field(
      "state", [&writer, &execution, &sources] -> void { writeState(writer, execution.state, sources); });
  writer.field("profile", [&writer, &execution] -> void { writeProfile(writer, execution.profile); });
  writer.field("resources", [&writer, &execution] -> void { writeResources(writer, execution.resources); });
  writer.field(
      "memory_before", [&writer, &execution] -> void { writeMemory(writer, execution.memoryBefore); });
  writer.field("memory_after", [&writer, &execution] -> void { writeMemory(writer, execution.memoryAfter); });
  writer.field("fault", [&writer, &execution] -> void { writeFault(writer, execution.fault); });
  writer.endObject();
}

auto writeCase(JsonWriter &writer, const TestCaseResult &testCase, const SourceManager &sources) -> void {
  writer.beginObject();
  writer.field("identifier", [&writer, &testCase] -> void { writer.text(testCase.descriptor.identifier); });
  writer.field(
      "status", [&writer, &testCase] -> void { writer.text(testCase.passed() ? "passed" : "failed"); });
  writer.field("passed", [&writer, &testCase] -> void { writer.boolean(testCase.passed()); });
  writer.field("failed", [&writer, &testCase] -> void { writer.boolean(testCase.failed()); });
  writer.field("descriptor", [&writer, &testCase] -> void { writeDescriptor(writer, testCase.descriptor); });
  writer.field("attempts", [&writer, &testCase, &sources] -> void {
    writer.beginArray();
    std::ranges::for_each(testCase.attempts, [&writer, &sources](const TestAttempt &attempt) -> void {
      writer.element(
          [&writer, &attempt, &sources] -> void { writeExecution(writer, attempt.execution, sources); });
    });
    writer.endArray();
  });
  writer.field(
      "measurement", [&writer, &testCase] -> void { writeMeasurement(writer, testCase.measurement); });
  writer.field(
      "recovered_timeouts", [&writer, &testCase] -> void { writer.number(testCase.recoveredTimeouts); });
  writer.field("suppressed_attempts",
      [&writer, &testCase] -> void { writer.number(testCase.suppressedAttemptCount); });
  writer.endObject();
}

auto writeSummary(JsonWriter &writer, const TestSummary &summary) -> void {
  writer.beginObject();
  writer.field(
      "status", [&writer, &summary] -> void { writer.text(summary.passed() ? "passed" : "failed"); });
  writer.field("tests", [&writer, &summary] -> void { writer.number(summary.testCount); });
  writer.field("cases", [&writer, &summary] -> void { writer.number(summary.caseCount); });
  writer.field("attempts", [&writer, &summary] -> void { writer.number(summary.attemptCount); });
  writer.field("samples", [&writer, &summary] -> void { writer.number(summary.sampleCount); });
  writer.field("retries", [&writer, &summary] -> void { writer.number(summary.retryCount); });
  writer.field("warmups", [&writer, &summary] -> void { writer.number(summary.warmupCount); });
  writer.field("recovered", [&writer, &summary] -> void { writer.number(summary.recoveredCount); });
  writer.field("passed_cases", [&writer, &summary] -> void { writer.number(summary.passedCaseCount); });
  writer.field("failed_cases", [&writer, &summary] -> void { writer.number(summary.failedCaseCount); });
  writer.field("passed", [&writer, &summary] -> void { writer.number(summary.passedCount); });
  writer.field("failed", [&writer, &summary] -> void { writer.number(summary.failedCount); });
  writer.field("assertions", [&writer, &summary] -> void { writer.number(summary.assertionCount); });
  writer.field(
      "failed_assertions", [&writer, &summary] -> void { writer.number(summary.failedAssertionCount); });
  writer.field("errors", [&writer, &summary] -> void { writer.number(summary.errorCount); });
  writer.field(
      "duration_ns", [&writer, &summary] -> void { writer.number(durationNanoseconds(summary.duration)); });
  writer.field("wall_duration_ns",
      [&writer, &summary] -> void { writer.number(durationNanoseconds(summary.wallDuration)); });
  writer.endObject();
}

/// Owns the writer and source-resolution context for one complete JSON document.
class JsonDocumentWriter final {
public:
  JsonDocumentWriter(JsonWriter &writer, const SourceManager &sources) noexcept
      : writer_(writer)
      , sources_(sources) {
  }

  auto write(const RunReport &report) -> void {
    JsonWriter &writer = writer_.get();
    const SourceManager &sources = sources_;
    const TestSummary summary = Reporter::summarize(report);

    writer.beginObject();
    writer.field("schema_version", [&writer] -> void { writer.number(schemaVersion); });
    writer.field("framework", [&writer] -> void { writer.text("Switch"); });
    writer.field("kind", [&writer] -> void { writer.text("test_run"); });
    writer.field(
        "status", [&writer, &summary] -> void { writer.text(summary.passed() ? "passed" : "failed"); });
    writer.field("run_seed", [&writer, &report] -> void {
      if (report.runSeed)
        writer.number(*report.runSeed);
      else
        writer.nullValue();
    });
    writer.field("retention", [&writer, &report] -> void { writer.text(debug::enumName(report.retention)); });
    writer.field(
        "retained_attempt_count", [&writer, &report] -> void { writer.number(report.retainedAttemptCount); });
    writer.field("suppressed_attempt_count",
        [&writer, &report] -> void { writer.number(report.suppressedAttemptCount); });
    writer.field("selection", [&writer, &report] -> void {
      writer.beginObject();
      writer.field("include", [&writer, &report] -> void {
        writer.beginArray();
        std::ranges::for_each(report.selection.include, [&writer](const String &value) -> void {
          writer.element([&writer, &value] -> void { writer.text(value); });
        });
        writer.endArray();
      });
      writer.field("exclude", [&writer, &report] -> void {
        writer.beginArray();
        std::ranges::for_each(report.selection.exclude, [&writer](const String &value) -> void {
          writer.element([&writer, &value] -> void { writer.text(value); });
        });
        writer.endArray();
      });
      writer.field("tags_all", [&writer, &report] -> void {
        writer.beginArray();
        std::ranges::for_each(report.selection.tagsAll, [&writer](const String &value) -> void {
          writer.element([&writer, &value] -> void { writer.text(value); });
        });
        writer.endArray();
      });
      writer.field("tags_any", [&writer, &report] -> void {
        writer.beginArray();
        std::ranges::for_each(report.selection.tagsAny, [&writer](const String &value) -> void {
          writer.element([&writer, &value] -> void { writer.text(value); });
        });
        writer.endArray();
      });
      writer.field("group", [&writer, &report] -> void {
        if (report.selection.group)
          writer.text(*report.selection.group);
        else
          writer.nullValue();
      });
      writer.endObject();
    });
    writer.field("summary", [&writer, &summary] -> void { writeSummary(writer, summary); });
    writer.field("cases", [&writer, &report, &sources] -> void {
      writer.beginArray();
      std::ranges::for_each(report.cases, [&writer, &sources](const TestCaseResult &testCase) -> void {
        writer.element([&writer, &testCase, &sources] -> void { writeCase(writer, testCase, sources); });
      });
      writer.endArray();
    });
    writer.field("tests", [&writer, &report, &sources] -> void {
      writer.beginArray();
      const auto flattenedAttempts =
          report.cases |
          std::views::transform(
              [](const TestCaseResult &testCase) -> const Vec<TestAttempt> & { return testCase.attempts; }) |
          std::views::join;
      std::ranges::for_each(flattenedAttempts, [&writer, &sources](const TestAttempt &attempt) -> void {
        writer.element(
            [&writer, &attempt, &sources] -> void { writeExecution(writer, attempt.execution, sources); });
      });
      writer.endArray();
    });
    writer.endObject();
  }

  auto writeList(Span<const TestDescriptor> descriptors) -> void {
    JsonWriter &writer = writer_;

    writer.beginObject();
    writer.field("schema_version", [&writer] -> void { writer.number(schemaVersion); });
    writer.field("framework", [&writer] -> void { writer.text("Switch"); });
    writer.field("kind", [&writer] -> void { writer.text("test_list"); });
    writer.field("count", [&writer, &descriptors] -> void { writer.number(descriptors.size()); });
    writer.field("tests", [&writer, &descriptors] -> void {
      writer.beginArray();
      std::ranges::for_each(descriptors, [&writer](const TestDescriptor &descriptor) -> void {
        writer.element([&writer, &descriptor] -> void { writeDescriptor(writer, descriptor); });
      });
      writer.endArray();
    });
    writer.endObject();
  }

private:
  Ref<JsonWriter> writer_;
  Ref<const SourceManager> sources_;
};

} // namespace

JsonReporter::JsonReporter(JsonReporterOptions options)
    : options_(options) {
}

auto JsonReporter::addRoot(Path root) -> void {
  roots_.push_back(std::move(root));
}

auto JsonReporter::report(const RunReport &report, std::ostream &output) const -> void {
  if (options_.showProgress)
    std::cerr << "JSON started\n";
  const SourceManager sources{roots_};
  JsonWriter writer{output, options_};
  JsonDocumentWriter document{writer, sources};
  document.write(report);
  if (options_.pretty)
    output << '\n';
  if (options_.showProgress)
    std::cerr << "JSON finished\n";
}

auto JsonReporter::render(const RunReport &report) const -> String {
  std::ostringstream output{};
  this->report(report, output);
  return output.str();
}

auto JsonReporter::reportList(Span<const TestDescriptor> descriptors, std::ostream &output) const -> void {
  JsonWriter writer{output, options_};
  const SourceManager sources{roots_};
  JsonDocumentWriter document{writer, sources};
  document.writeList(descriptors);
  if (options_.pretty)
    output << '\n';
}

auto JsonReporter::renderList(Span<const TestDescriptor> descriptors) const -> String {
  std::ostringstream output{};
  reportList(descriptors, output);
  return output.str();
}

} // namespace Switch
