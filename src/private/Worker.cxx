module Switch;

import std;
import Miracle;

import :Diagnostics;
import :Environment;
import :Execution;
import :Policies;
import :Task;
import :Worker;

using namespace Miracle;

namespace Switch::detail {

namespace {

enum class RecordKind : u8 {
  Started = 1,
  Execution = 2,
  Completed = 3,
  Compact = 4,
};

constexpr u32 journalMagic{0x4E59584A};

class WireWriter final {
public:
  template <class Value>
  auto pod(Value value) -> void {
    const usize offset = data_.size();
    data_.resize(offset + sizeof(Value));
    std::memcpy(data_.data() + offset, std::addressof(value), sizeof(Value)); // NOLINT
  }

  auto string(StringView value) -> void {
    pod(static_cast<u64>(value.size()));
    data_.append(value);
  }

  [[nodiscard]] auto bytes() const noexcept -> StringView {
    return data_;
  }

private:
  String data_;
};

class WireReader final {
public:
  explicit WireReader(StringView data) noexcept
      : data_(data) {
  }

  template <class Value>
  [[nodiscard]] auto pod() -> Value {
    if (offset_ + sizeof(Value) > data_.size()) {
      valid_ = false;
      return {};
    }

    Value value{};
    std::memcpy(std::addressof(value), data_.data() + offset_, sizeof(Value));
    offset_ += sizeof(Value);
    return value;
  }

  [[nodiscard]] auto string() -> String {
    const u64 size = pod<u64>();
    if (not valid_ or size > data_.size() - offset_) {
      valid_ = false;
      return {};
    }

    String value{data_.substr(offset_, static_cast<usize>(size))};
    offset_ += static_cast<usize>(size);
    return value;
  }

  [[nodiscard]] auto valid() const noexcept -> bool {
    return valid_;
  }

  [[nodiscard]] auto remaining() const noexcept -> usize {
    return data_.size() - std::min(offset_, data_.size());
  }

  [[nodiscard]] auto count(usize minimumElementSize = 1) -> usize {
    const u64 value = pod<u64>();
    if (not valid_ or minimumElementSize == 0 or value > remaining() / minimumElementSize) {
      valid_ = false;
      return 0;
    }
    return static_cast<usize>(value);
  }

private:
  StringView data_;
  usize offset_{};
  bool valid_{true};
};

[[nodiscard]] auto sourceLocationData(std::source_location location) -> SourceLocationData {
  return SourceLocationData{
      .file = location.file_name() != nullptr ? location.file_name() : String{},
      .function = location.function_name() != nullptr ? location.function_name() : String{},
      .line = location.line(),
      .column = location.column(),
  };
}

auto writeLocation(WireWriter &writer, std::source_location location) -> void {
  const SourceLocationData data = sourceLocationData(location);
  writer.string(data.file);
  writer.string(data.function);
  writer.pod(static_cast<u64>(data.line));
  writer.pod(static_cast<u64>(data.column));
}

[[nodiscard]] auto readLocation(WireReader &reader) -> SourceLocationData {
  return SourceLocationData{
      .file = reader.string(),
      .function = reader.string(),
      .line = static_cast<usize>(reader.pod<u64>()),
      .column = static_cast<usize>(reader.pod<u64>()),
  };
}

auto writeStringOption(WireWriter &writer, const Option<String> &value) -> void {
  writer.pod(static_cast<u8>(value.has_value()));
  if (value)
    writer.string(*value);
}

[[nodiscard]] auto readStringOption(WireReader &reader) -> Option<String> {
  if (reader.pod<u8>() == 0)
    return None;

  return reader.string();
}

auto writeSourceLocationOption(WireWriter &writer, const Option<SourceLocationData> &value) -> void {
  writer.pod(static_cast<u8>(value.has_value()));
  if (value) {
    writer.string(value->file);
    writer.string(value->function);
    writer.pod(static_cast<u64>(value->line));
    writer.pod(static_cast<u64>(value->column));
  }
}

[[nodiscard]] auto readSourceLocationOption(WireReader &reader) -> Option<SourceLocationData> {
  if (reader.pod<u8>() == 0)
    return None;

  return readLocation(reader);
}

auto writePolicy(WireWriter &writer, const TestPolicy &policy) -> void {
  writer.pod(static_cast<u8>(policy.trace));
  writer.pod(static_cast<u8>(policy.isolated));
  writer.pod(static_cast<u8>(policy.parent));
  writeStringOption(writer, policy.expectedPanic);
  writer.pod(static_cast<u8>(policy.timeout.has_value()));
  if (policy.timeout)
    writer.pod(static_cast<i64>(policy.timeout->count()));
  writer.pod(static_cast<u64>(policy.repeat));
  writer.pod(static_cast<u64>(policy.warmup));
  writer.pod(static_cast<u64>(policy.retry));
}

[[nodiscard]] auto readPolicy(WireReader &reader) -> TestPolicy {
  TestPolicy policy{
      .trace = reader.pod<u8>() != 0,
      .isolated = reader.pod<u8>() != 0,
      .parent = reader.pod<u8>() != 0,
      .expectedPanic = readStringOption(reader),
  };
  if (reader.pod<u8>() != 0)
    policy.timeout = std::chrono::steady_clock::duration{reader.pod<i64>()};
  policy.repeat = static_cast<usize>(reader.pod<u64>());
  policy.warmup = static_cast<usize>(reader.pod<u64>());
  policy.retry = static_cast<usize>(reader.pod<u64>());
  return policy;
}

auto writeMetadata(WireWriter &writer, const TestMetadata &metadata) -> void {
  writeStringOption(writer, metadata.group);
  writer.pod(static_cast<u64>(metadata.tags.size()));
  std::ranges::for_each(metadata.tags, [&writer](const String &tag) -> void { writer.string(tag); });
}

[[nodiscard]] auto readMetadata(WireReader &reader) -> TestMetadata {
  TestMetadata metadata{.group = readStringOption(reader)};
  const usize count = reader.count(sizeof(u64));
  std::ranges::for_each(std::views::indices(count),
      [&reader, &metadata](usize) -> void { metadata.tags.push_back(reader.string()); });
  return metadata;
}

auto writeDescriptor(WireWriter &writer, const TestDescriptor &descriptor) -> void {
  writer.string(descriptor.identifier);
  writer.string(descriptor.name);
  writer.string(descriptor.description);
  writer.pod(static_cast<u64>(descriptor.testCase));
  writeLocation(writer, descriptor.location);
  writePolicy(writer, descriptor.policy);
  writeMetadata(writer, descriptor.metadata);
}

[[nodiscard]] auto readDescriptor(WireReader &reader, const TestDescriptor &fallback) -> TestDescriptor {
  const String identifier = reader.string();
  const String name = reader.string();
  const String description = reader.string();
  const auto testCase = static_cast<usize>(reader.pod<u64>());
  static_cast<void>(readLocation(reader));
  return TestDescriptor{
      .identifier = identifier.empty() ? fallback.identifier : identifier,
      .location = fallback.location,
      .name = name.empty() ? fallback.name : name,
      .description = description,
      .testCase = testCase,
      .policy = readPolicy(reader),
      .metadata = readMetadata(reader),
  };
}

auto writeSpan(WireWriter &writer, const SourceSpan &span) -> void {
  writeLocation(writer, span.location);
  writer.pod(static_cast<u8>(span.selection));
  writer.pod(static_cast<u8>(span.kind));
  writer.string(span.label);
  writeSourceLocationOption(writer, span.remoteLocation);
}

[[nodiscard]] auto readSpan(WireReader &reader, std::source_location fallback) -> SourceSpan {
  const SourceLocationData location = readLocation(reader);
  const auto selection = static_cast<SpanSelection>(reader.pod<u8>());
  const auto kind = static_cast<SpanKind>(reader.pod<u8>());
  const String label = reader.string();
  const Option<SourceLocationData> remoteLocation = readSourceLocationOption(reader);
  return SourceSpan{
      .location = fallback,
      .selection = selection,
      .kind = kind,
      .label = label,
      .remoteLocation = remoteLocation ? remoteLocation : Option<SourceLocationData>{location},
  };
}

auto writeNote(WireWriter &writer, const DiagnosticNote &note) -> void {
  writer.pod(static_cast<u8>(note.level));
  writer.string(note.message);
  writer.pod(static_cast<u8>(note.span.has_value()));
  if (note.span)
    writeSpan(writer, *note.span);
  writer.pod(static_cast<u64>(note.fragments.size()));
  std::ranges::for_each(note.fragments, [&writer](const DiagnosticFragment &fragment) -> void {
    writer.string(fragment.text);
    writer.pod(static_cast<u8>(fragment.highlighted));
  });
}

[[nodiscard]] auto readNote(WireReader &reader, std::source_location fallback) -> DiagnosticNote {
  DiagnosticNote note{
      .level = static_cast<DiagnosticLevel>(reader.pod<u8>()),
      .message = reader.string(),
  };
  if (reader.pod<u8>() != 0)
    note.span = readSpan(reader, fallback);

  const usize count = reader.count(sizeof(u64) + sizeof(u8));
  std::ranges::for_each(std::views::indices(count), [&reader, &note](usize) -> void {
    note.fragments.push_back(DiagnosticFragment{
        .text = reader.string(),
        .highlighted = reader.pod<u8>() != 0,
    });
  });
  return note;
}

auto writeExpansion(WireWriter &writer, const DiagnosticExpansion &expansion) -> void {
  writer.pod(static_cast<u8>(expansion.index()));
  std::visit(
      [&writer](const auto &value) -> void {
        using Type = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Type, BinaryExpansion>) {
          writer.string(value.left);
          writer.string(value.operatorName);
          writer.string(value.right);
        } else if constexpr (std::same_as<Type, ContainsExpansion>) {
          writer.string(value.needle);
          writer.string(value.container);
        } else if constexpr (std::same_as<Type, NearExpansion>) {
          writer.string(value.left);
          writer.string(value.right);
          writer.string(value.tolerance);
          writer.string(value.difference);
        }
      },
      expansion);
}

[[nodiscard]] auto readExpansion(WireReader &reader) -> DiagnosticExpansion {
  switch (reader.pod<u8>()) {
    case 1:
      return BinaryExpansion{
          .left = reader.string(),
          .operatorName = reader.string(),
          .right = reader.string(),
      };
    case 2:
      return ContainsExpansion{
          .needle = reader.string(),
          .container = reader.string(),
      };
    case 3:
      return NearExpansion{
          .left = reader.string(),
          .right = reader.string(),
          .tolerance = reader.string(),
          .difference = reader.string(),
      };
    default: return std::monostate{};
  }
}

auto writeDiagnostic(WireWriter &writer, const Diagnostic &diagnostic) -> void {
  writer.pod(static_cast<u8>(diagnostic.level));
  writer.pod(static_cast<u8>(diagnostic.header.code));
  writeStringOption(writer, diagnostic.header.descriptionOverride);

  writer.pod(static_cast<u64>(diagnostic.details.spans.size()));
  std::ranges::for_each(
      diagnostic.details.spans, [&writer](const SourceSpan &span) -> void { writeSpan(writer, span); });

  writer.pod(static_cast<u64>(diagnostic.details.notes.size()));
  std::ranges::for_each(
      diagnostic.details.notes, [&writer](const DiagnosticNote &note) -> void { writeNote(writer, note); });

  writer.pod(static_cast<u64>(diagnostic.details.attachments.size()));
  std::ranges::for_each(
      diagnostic.details.attachments, [&writer](const DiagnosticAttachment &attachment) -> void {
        writer.string(attachment.name);
        writer.string(attachment.content);
      });
  writeExpansion(writer, diagnostic.details.expansion);
}

[[nodiscard]] auto readDiagnostic(WireReader &reader, std::source_location fallback) -> Diagnostic {
  Diagnostic diagnostic{
      .level = static_cast<DiagnosticLevel>(reader.pod<u8>()),
      .header =
          DiagnosticHeader{
              .code = static_cast<DiagnosticCode>(reader.pod<u8>()),
              .descriptionOverride = readStringOption(reader),
          },
  };

  const usize spanCount = reader.count(sizeof(u64));
  std::ranges::for_each(std::views::indices(spanCount), [&reader, &diagnostic, fallback](usize) -> void {
    diagnostic.details.spans.push_back(readSpan(reader, fallback));
  });

  const usize noteCount = reader.count(sizeof(u8) + sizeof(u64) + sizeof(u8) + sizeof(u64));
  std::ranges::for_each(std::views::indices(noteCount), [&reader, &diagnostic, fallback](usize) -> void {
    diagnostic.details.notes.push_back(readNote(reader, fallback));
  });

  const usize attachmentCount = reader.count(2 * sizeof(u64));
  std::ranges::for_each(std::views::indices(attachmentCount), [&reader, &diagnostic](usize) -> void {
    diagnostic.details.attachments.push_back(DiagnosticAttachment{
        .name = reader.string(),
        .content = reader.string(),
    });
  });
  diagnostic.details.expansion = readExpansion(reader);
  return diagnostic;
}

auto writeState(WireWriter &writer, const TestState &state) -> void {
  writer.pod(static_cast<u64>(state.assertions));
  writer.pod(static_cast<u64>(state.failedAssertions));
  writer.pod(static_cast<u64>(state.errors));
  writer.pod(static_cast<u8>(state.aborted));
  writer.pod(static_cast<u64>(state.diagnostics.size()));
  std::ranges::for_each(state.diagnostics,
      [&writer](const Diagnostic &diagnostic) -> void { writeDiagnostic(writer, diagnostic); });
  writer.pod(static_cast<u64>(state.traces.size()));
  std::ranges::for_each(state.traces, [&writer](const TraceEvent &trace) -> void {
    writer.string(trace.message);
    writeLocation(writer, trace.location);
    writeSourceLocationOption(writer, trace.remoteLocation);
  });
}

[[nodiscard]] auto readState(WireReader &reader, std::source_location fallback) -> TestState {
  TestState state{
      .assertions = static_cast<usize>(reader.pod<u64>()),
      .failedAssertions = static_cast<usize>(reader.pod<u64>()),
      .errors = static_cast<usize>(reader.pod<u64>()),
      .aborted = reader.pod<u8>() != 0,
  };
  const usize diagnosticCount = reader.count(sizeof(u8) + sizeof(u8) + sizeof(u8) + (3 * sizeof(u64)));
  std::ranges::for_each(std::views::indices(diagnosticCount), [&reader, &state, fallback](usize) -> void {
    state.diagnostics.push_back(readDiagnostic(reader, fallback));
  });
  const usize traceCount = reader.count((2 * sizeof(u64)) + sizeof(u8));
  std::ranges::for_each(std::views::indices(traceCount), [&reader, &state, fallback](usize) -> void {
    const String message = reader.string();
    const SourceLocationData location = readLocation(reader);
    const Option<SourceLocationData> remoteLocation = readSourceLocationOption(reader);
    state.traces.push_back(TraceEvent{
        .message = message,
        .location = fallback,
        .remoteLocation = remoteLocation ? remoteLocation : Option<SourceLocationData>{location},
    });
  });
  return state;
}

auto writeProfile(WireWriter &writer, const profiling::ProfileSnapshot &profile) -> void {
  writer.pod(static_cast<i64>(profile.duration.count()));
  writer.pod(static_cast<u64>(profile.events.size()));
  std::ranges::for_each(profile.events, [&writer](const profiling::ProfileEvent &event) -> void {
    writer.string(event.name);
    writer.pod(static_cast<i64>(event.duration.count()));
    writeLocation(writer, event.location);
    writer.pod(static_cast<u64>(event.depth));
    writer.pod(static_cast<u8>(event.kind));
  });
  writer.pod(static_cast<u64>(profile.aggregates.size()));
  std::ranges::for_each(
      profile.aggregates, [&writer](const Pair<String, profiling::ProfileAggregate> &entry) -> void {
        writer.string(entry.first);
        writer.pod(static_cast<u64>(entry.second.count));
        writer.pod(static_cast<i64>(entry.second.total.count()));
        writer.pod(static_cast<i64>(entry.second.minimum.count()));
        writer.pod(static_cast<i64>(entry.second.maximum.count()));
      });
}

[[nodiscard]] auto readProfile(WireReader &reader, std::source_location fallback)
    -> profiling::ProfileSnapshot {
  profiling::ProfileSnapshot profile{
      .duration = std::chrono::steady_clock::duration{reader.pod<i64>()},
  };
  const usize eventCount = reader.count((3 * sizeof(u64)) + sizeof(u8));
  std::ranges::for_each(std::views::indices(eventCount), [&reader, &profile, fallback](usize) -> void {
    String name = reader.string();
    const auto duration = std::chrono::steady_clock::duration{reader.pod<i64>()};
    static_cast<void>(readLocation(reader));
    profile.events.push_back(profiling::ProfileEvent{
        .name = std::move(name),
        .duration = duration,
        .location = fallback,
        .depth = static_cast<usize>(reader.pod<u64>()),
        .kind = static_cast<profiling::EventKind>(reader.pod<u8>()),
    });
  });
  const usize aggregateCount = reader.count(5 * sizeof(u64));
  std::ranges::for_each(std::views::indices(aggregateCount), [&reader, &profile](usize) -> void {
    String name = reader.string();
    profiling::ProfileAggregate aggregate{
        .count = static_cast<usize>(reader.pod<u64>()),
        .total = std::chrono::steady_clock::duration{reader.pod<i64>()},
        .minimum = std::chrono::steady_clock::duration{reader.pod<i64>()},
        .maximum = std::chrono::steady_clock::duration{reader.pod<i64>()},
    };
    profile.aggregates.emplace(std::move(name), aggregate);
  });
  return profile;
}

auto writeResources(WireWriter &writer, const ResourceSnapshot &resources) -> void {
  writer.pod(static_cast<u64>(resources.activeResources));
  writer.pod(static_cast<u64>(resources.peakResources));
  writer.pod(static_cast<u64>(resources.cleanupCount));
  writer.pod(static_cast<u64>(resources.allocationBytes));
  writer.pod(static_cast<u64>(resources.activeAllocationBytes));
  writer.pod(static_cast<u64>(resources.peakAllocationBytes));
  writer.pod(static_cast<u64>(resources.temporaryFiles));
  writer.pod(static_cast<u64>(resources.temporaryDirectories));
}

[[nodiscard]] auto readResources(WireReader &reader) -> ResourceSnapshot {
  return ResourceSnapshot{
      .activeResources = static_cast<usize>(reader.pod<u64>()),
      .peakResources = static_cast<usize>(reader.pod<u64>()),
      .cleanupCount = static_cast<usize>(reader.pod<u64>()),
      .allocationBytes = static_cast<usize>(reader.pod<u64>()),
      .activeAllocationBytes = static_cast<usize>(reader.pod<u64>()),
      .peakAllocationBytes = static_cast<usize>(reader.pod<u64>()),
      .temporaryFiles = static_cast<usize>(reader.pod<u64>()),
      .temporaryDirectories = static_cast<usize>(reader.pod<u64>()),
  };
}

auto writeMemory(WireWriter &writer, const Option<memory::ProcessMemorySnapshot> &memory) -> void {
  writer.pod(static_cast<u8>(memory.has_value()));
  if (memory)
    writer.pod(static_cast<u64>(memory->residentBytes));
}

[[nodiscard]] auto readMemory(WireReader &reader) -> Option<memory::ProcessMemorySnapshot> {
  if (reader.pod<u8>() == 0)
    return None;

  return memory::ProcessMemorySnapshot{.residentBytes = static_cast<usize>(reader.pod<u64>())};
}

auto writeExecution(WireWriter &writer, const TestExecution &execution) -> void {
  writeDescriptor(writer, execution.descriptor);
  writeState(writer, execution.state);
  writer.pod(static_cast<i64>(execution.duration.count()));
  writer.pod(static_cast<i64>(execution.wallDuration.count()));
  writeProfile(writer, execution.profile);
  writeResources(writer, execution.resources);
  writeMemory(writer, execution.memoryBefore);
  writeMemory(writer, execution.memoryAfter);
  writer.pod(execution.runSeed);
  writer.pod(execution.seed);
  writer.pod(static_cast<u64>(execution.iteration));
  writer.pod(static_cast<u64>(execution.attempt.runIteration));
  writer.pod(static_cast<u64>(execution.attempt.sample));
  writer.pod(static_cast<u64>(execution.attempt.retry));
  writer.pod(static_cast<u8>(execution.warmup));
  writer.pod(static_cast<u8>(execution.traceMode));
  writer.pod(static_cast<u8>(execution.fault.has_value()));
  if (execution.fault) {
    writer.pod(static_cast<u8>(execution.fault->kind));
    writer.pod(static_cast<u8>(execution.fault->signal));
    writer.pod(execution.fault->code);
    writer.pod(execution.fault->address);
    writer.pod(execution.fault->instruction);
    writer.pod(static_cast<u8>(execution.fault->symbolsAvailable));
  }
}

[[nodiscard]] auto readExecution(WireReader &reader, const TestDescriptor &fallback) -> TestExecution {
  TestExecution execution{
      .descriptor = readDescriptor(reader, fallback),
  };
  if (not reader.valid())
    throw std::runtime_error{"invalid worker descriptor payload"};
  execution.state = readState(reader, fallback.location);
  if (not reader.valid())
    throw std::runtime_error{"invalid worker state payload"};
  execution.duration = std::chrono::steady_clock::duration{reader.pod<i64>()};
  execution.wallDuration = std::chrono::steady_clock::duration{reader.pod<i64>()};
  execution.profile = readProfile(reader, fallback.location);
  if (not reader.valid())
    throw std::runtime_error{"invalid worker profile payload"};
  execution.resources = readResources(reader);
  execution.memoryBefore = readMemory(reader);
  execution.memoryAfter = readMemory(reader);
  execution.runSeed = reader.pod<u64>();
  execution.seed = reader.pod<u64>();
  execution.iteration = static_cast<usize>(reader.pod<u64>());
  execution.attempt = AttemptIndex{
      .runIteration = static_cast<usize>(reader.pod<u64>()),
      .sample = static_cast<usize>(reader.pod<u64>()),
      .retry = static_cast<usize>(reader.pod<u64>()),
  };
  execution.warmup = reader.pod<u8>() != 0;
  execution.traceMode = static_cast<TraceMode>(reader.pod<u8>());
  if (reader.pod<u8>() != 0) {
    execution.fault = NativeFault{
        .kind = static_cast<NativeFaultKind>(reader.pod<u8>()),
        .signal = static_cast<NativeSignal>(reader.pod<u8>()),
        .code = reader.pod<i32>(),
        .address = reader.pod<u64>(),
        .instruction = reader.pod<u64>(),
        .symbolsAvailable = reader.pod<u8>() != 0,
    };
  }
  return execution;
}

auto writeRecord(std::ofstream &output, RecordKind kind, StringView payload, bool flush = true) -> bool {
  try {
    const u32 magic = journalMagic;
    const u8 type = static_cast<u8>(kind);
    const u64 size = payload.size();
    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
    output.write(reinterpret_cast<const char *>(std::addressof(magic)), sizeof(magic));
    output.write(reinterpret_cast<const char *>(std::addressof(type)), sizeof(type));
    output.write(reinterpret_cast<const char *>(std::addressof(size)), sizeof(size));
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
    output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    if (flush)
      output.flush();
    return static_cast<bool>(output);
  } catch (...) {
    return false;
  }
}

[[nodiscard]] auto parseU64(StringView value) -> Option<u64> {
  u64 result{};
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
  if (error != std::errc{} or end != value.data() + value.size())
    return None;
  return result;
}

[[nodiscard]] auto environmentValue(const char *name) -> Option<String> {
  const char *value = std::getenv(name); // NOLINT(concurrency-mt-unsafe)
  if (value == nullptr)
    return None;
  return String{value};
}

[[nodiscard]] auto requiredEnvironmentValue(const char *name) -> Option<String> {
  Option<String> value = environmentValue(name);
  if (not value or value->empty())
    return None;
  return value;
}

} // namespace

WorkerJournal::WorkerJournal(const Path &path) noexcept {
  try {
    output_ = std::make_unique<std::ofstream>(path, std::ios::binary | std::ios::trunc);
    ready_ = output_->is_open();
  } catch (...) {
    ready_ = false;
  }
}

WorkerJournal::~WorkerJournal() noexcept {
  if (output_ != nullptr) {
    try {
      output_->flush();
      output_->close();
    } catch (...) { // NOLINT(bugprone-empty-catch)
    }
  }
}

auto WorkerJournal::ready() const noexcept -> bool {
  return ready_;
}

auto WorkerJournal::setBuffered(bool buffered) noexcept -> void {
  buffered_ = buffered;
  recordsSinceFlush_ = 0;
}

auto WorkerJournal::attemptStarted(AttemptIndex attempt, bool warmup) noexcept -> void {
  if (not ready_)
    return;

  try {
    WireWriter writer{};
    writer.pod(static_cast<u64>(attempt.runIteration));
    writer.pod(static_cast<u64>(attempt.sample));
    writer.pod(static_cast<u64>(attempt.retry));
    writer.pod(static_cast<u8>(warmup));
    ready_ = writeRecord(*output_, RecordKind::Started, writer.bytes(), not buffered_);
  } catch (...) {
    ready_ = false;
  }
}

auto WorkerJournal::attemptCompleted(const TestExecution &execution) noexcept -> void {
  if (not ready_)
    return;

  try {
    WireWriter writer{};
    writeExecution(writer, execution);
    ready_ = writeRecord(*output_, RecordKind::Execution, writer.bytes(), not buffered_);
  } catch (...) {
    ready_ = false;
  }
}

auto WorkerJournal::attemptCompleted(const AttemptOutcome &outcome) noexcept -> void {
  if (not ready_)
    return;

  try {
    ++compactPassCount_;
    compactAssertionCount_ += outcome.assertions;
    compactDuration_ += outcome.duration;
    compactWallDuration_ += outcome.wallDuration;
  } catch (...) {
    ready_ = false;
  }
}

auto WorkerJournal::batchCompleted(const BatchExecutionContext &batch) noexcept -> void {
  compactPassCount_ += batch.passed;
  compactAssertionCount_ += batch.assertions;
  compactDuration_ += batch.duration;
  compactWallDuration_ += batch.wallDuration;
  compactMinimumDuration_ = batch.minimumDuration;
  compactMaximumDuration_ = batch.maximumDuration;
  compactMeanDuration_ = batch.meanDuration;
  compactVariableAccumulator_ = batch.variableAccumulator;
  compactTimingSamples_ = batch.timingSamples;
  // Quantiles are finalized by the child batch and are safe to transfer as scalar values.
  compactFirstQuartile_ = batch.firstQuartile;
  compactMedian_ = batch.median;
  compactThirdQuartile_ = batch.thirdQuartile;
  compactQuantilesAvailable_ = batch.quantilesAvailable;
  compactQuantilesApproximate_ = batch.quantilesApproximate;
  if (batch.firstFailure)
    attemptCompleted(*batch.firstFailure);
}

auto WorkerJournal::complete() noexcept -> void {
  if (not ready_)
    return;

  WireWriter writer{};
  writer.pod(static_cast<u64>(compactPassCount_));
  writer.pod(static_cast<u64>(compactAssertionCount_));
  writer.pod(static_cast<i64>(compactDuration_.count()));
  writer.pod(static_cast<i64>(compactWallDuration_.count()));
  writer.pod(static_cast<i64>(compactMinimumDuration_.count()));
  writer.pod(static_cast<i64>(compactMaximumDuration_.count()));
  writer.pod(compactMeanDuration_);
  writer.pod(compactVariableAccumulator_);
  writer.pod(static_cast<u64>(compactTimingSamples_));
  writer.pod(static_cast<i64>(compactFirstQuartile_.count()));
  writer.pod(static_cast<i64>(compactMedian_.count()));
  writer.pod(static_cast<i64>(compactThirdQuartile_.count()));
  writer.pod(compactQuantilesAvailable_);
  writer.pod(compactQuantilesApproximate_);
  ready_ = writeRecord(*output_, RecordKind::Completed, writer.bytes(), true);
}

auto consumeWorkerRequest() -> Option<WorkerRequest> {
  static bool consumed_{};
  if (consumed_)
    return None;

  const Option<String> marker = environmentValue("SWITCH_TEST_WORKER");
  if (not marker or *marker != "1")
    return None;

  consumed_ = true;
  const Option<String> resultPath = requiredEnvironmentValue("SWITCH_TEST_WORKER_RESULT");
  const Option<String> faultPath = requiredEnvironmentValue("SWITCH_TEST_WORKER_FAULT");
  const Option<String> identifier = requiredEnvironmentValue("SWITCH_TEST_WORKER_IDENTIFIER");
  const Option<String> plannedCase = requiredEnvironmentValue("SWITCH_TEST_WORKER_CASE");
  const Option<String> runIteration = requiredEnvironmentValue("SWITCH_TEST_WORKER_ITERATION");
  const Option<String> repeat = requiredEnvironmentValue("SWITCH_TEST_WORKER_REPEAT");
  const Option<String> runSeed = requiredEnvironmentValue("SWITCH_TEST_WORKER_SEED");
  const Option<String> timeMode = requiredEnvironmentValue("SWITCH_TEST_WORKER_TIME");
  const Option<String> traceMode = requiredEnvironmentValue("SWITCH_TEST_WORKER_TRACE");
  const Option<String> captureMemory = requiredEnvironmentValue("SWITCH_TEST_WORKER_MEMORY");
  const Option<String> captureProfile = requiredEnvironmentValue("SWITCH_TEST_WORKER_PROFILE");
  const Option<String> captureTiming = requiredEnvironmentValue("SWITCH_TEST_WORKER_TIMING");

  if (not resultPath or not faultPath or not identifier or not plannedCase or not runIteration or not repeat or
      not runSeed or not timeMode or not traceMode or not captureMemory or not captureProfile or not captureTiming)
    return WorkerRequest{};

  const Option<u64> plannedCaseValue = parseU64(*plannedCase);
  const Option<u64> runIterationValue = parseU64(*runIteration);
  const Option<u64> repeatValue = parseU64(*repeat);
  const Option<u64> runSeedValue = parseU64(*runSeed);
  const Option<u64> timeModeValue = parseU64(*timeMode);
  const Option<u64> traceModeValue = parseU64(*traceMode);
  if (not plannedCaseValue.has_value() or not runIterationValue.has_value() or not repeatValue.has_value() or
      not runSeedValue.has_value() or
      not timeModeValue.has_value() or not traceModeValue.has_value())
    return WorkerRequest{};

  return WorkerRequest{
      .resultPath = Path{*resultPath},
      .faultPath = Path{*faultPath},
      .identifier = *identifier,
      .plannedCase = static_cast<usize>(*plannedCaseValue),
      .runIteration = static_cast<usize>(*runIterationValue),
      .repeat = static_cast<usize>(*repeatValue),
      .runSeed = *runSeedValue,
      .timeMode = static_cast<TimeMode>(*timeModeValue),
      .traceMode = static_cast<TraceMode>(*traceModeValue),
      .captureMemory = *captureMemory == "1",
      .captureProfile = *captureProfile == "1",
      .captureTiming = *captureTiming == "1" ? CapturePolicy::PerAttempt : CapturePolicy::None,
  };
}

auto readWorkerJournal(const Path &path, const TestDescriptor &fallback) -> WorkerJournalResult {
  std::ifstream input{path, std::ios::binary};
  if (not input)
    return {};

  const String data{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  WorkerJournalResult result{};

  // Decode records with a byte cursor so a partially flushed final record is ignored without affecting
  // earlier attempts.
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  usize offset{};
  while (offset + sizeof(u32) + sizeof(u8) + sizeof(u64) <= data.size()) {
    u32 magic{};
    u8 type{};
    u64 payloadSize{};
    std::memcpy(std::addressof(magic), data.data() + offset, sizeof(magic));
    offset += sizeof(magic);
    std::memcpy(std::addressof(type), data.data() + offset, sizeof(type));
    offset += sizeof(type);
    std::memcpy(std::addressof(payloadSize), data.data() + offset, sizeof(payloadSize));
    offset += sizeof(payloadSize);

    if (magic != journalMagic or payloadSize > data.size() - offset)
      break;

    WireReader payloadReader{StringView{data}.substr(offset, static_cast<usize>(payloadSize))};
    offset += static_cast<usize>(payloadSize);
    const auto kind = static_cast<RecordKind>(type);
    if (kind == RecordKind::Started) {
      result.activeAttempt = AttemptIndex{
          .runIteration = static_cast<usize>(payloadReader.pod<u64>()),
          .sample = static_cast<usize>(payloadReader.pod<u64>()),
          .retry = static_cast<usize>(payloadReader.pod<u64>()),
      };
      result.activeWarmup = payloadReader.pod<u8>() != 0;
      if (not payloadReader.valid())
        break;
    } else if (kind == RecordKind::Execution) {
      TestExecution execution = readExecution(payloadReader, fallback);
      if (not payloadReader.valid())
        break;

      result.executions.push_back(std::move(execution));
      result.activeAttempt = None;
      result.activeWarmup = false;
    } else if (kind == RecordKind::Compact) {
      const AttemptIndex attempt{
          .runIteration = static_cast<usize>(payloadReader.pod<u64>()),
          .sample = static_cast<usize>(payloadReader.pod<u64>()),
          .retry = static_cast<usize>(payloadReader.pod<u64>()),
      };
      const bool warmup = payloadReader.pod<u8>() != 0;
      const auto duration = std::chrono::steady_clock::duration{payloadReader.pod<i64>()};
      const auto wallDuration = std::chrono::steady_clock::duration{payloadReader.pod<i64>()};
      const usize assertions = static_cast<usize>(payloadReader.pod<u64>());
      const usize failedAssertions = static_cast<usize>(payloadReader.pod<u64>());
      const usize errors = static_cast<usize>(payloadReader.pod<u64>());
      const u64 runSeed = payloadReader.pod<u64>();
      const u64 seed = payloadReader.pod<u64>();
      const usize iteration = static_cast<usize>(payloadReader.pod<u64>());
      const bool passed = payloadReader.pod<u8>() != 0;
      static_cast<void>(payloadReader.pod<u8>()); // timeout is represented by diagnostics in detailed records.
      TestExecution execution{
          .descriptor = fallback,
          .duration = duration,
          .wallDuration = wallDuration,
          .runSeed = runSeed,
          .seed = seed,
          .iteration = iteration,
          .attempt = attempt,
          .warmup = warmup,
      };
      execution.state.assertions = assertions;
      execution.state.failedAssertions = failedAssertions;
      execution.state.errors = errors;
      if (not passed)
        execution.state.errors = std::max(execution.state.errors, usize{1});
      if (not payloadReader.valid())
        break;

      result.executions.push_back(std::move(execution));
      result.activeAttempt = None;
      result.activeWarmup = false;
    } else if (kind == RecordKind::Completed) {
      result.compactPassCount = static_cast<usize>(payloadReader.pod<u64>());
      result.compactAssertionCount = static_cast<usize>(payloadReader.pod<u64>());
      result.compactDuration = std::chrono::steady_clock::duration{payloadReader.pod<i64>()};
      result.compactWallDuration = std::chrono::steady_clock::duration{payloadReader.pod<i64>()};
      result.compactMinimumDuration = std::chrono::steady_clock::duration{payloadReader.pod<i64>()};
      result.compactMaximumDuration = std::chrono::steady_clock::duration{payloadReader.pod<i64>()};
      result.compactMeanDuration = payloadReader.pod<long double>();
      result.compactVariableAccumulator = payloadReader.pod<long double>();
      result.compactTimingSamples = static_cast<usize>(payloadReader.pod<u64>());
      result.compactFirstQuartile = std::chrono::steady_clock::duration{payloadReader.pod<i64>()};
      result.compactMedian = std::chrono::steady_clock::duration{payloadReader.pod<i64>()};
      result.compactThirdQuartile = std::chrono::steady_clock::duration{payloadReader.pod<i64>()};
      result.compactQuantilesAvailable = payloadReader.pod<bool>();
      result.compactQuantilesApproximate = payloadReader.pod<bool>();
      result.completed = true;
    }
  }
  // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

  return result;
}

auto workerProtocolDiagnostic(StringView message, std::source_location location) -> Diagnostic {
  Diagnostic diagnostic = makeDiagnostic(DiagnosticCode::WorkerLaunchFailed, location);
  diagnostic.details.spans.front().label = "worker protocol";
  diagnostic.details.spans.front().selection = SpanSelection::Declaration;
  diagnostic.addNote(String{message});
  return diagnostic;
}

} // namespace Switch::detail
