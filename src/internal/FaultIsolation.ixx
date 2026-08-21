module Switch:FaultIsolation;

import std;
import Miracle;

import :Execution;

using namespace Miracle;

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers, bugprone-exception-escape)
namespace Switch::detail::isolation {

inline constexpr u32 faultRecordMagic{0x4E595846};

/// Byte-exact record written directly by a native-fault handler.
struct FaultRecord final {
  Array<Byte, 4> magic{};
  u8 kind{};
  u8 signal{};
  Array<Byte, 4> code{};
  Array<Byte, 8> address{};
  Array<Byte, 8> instruction{};
  u8 symbolsAvailable{};
};

static_assert(sizeof(FaultRecord) == 27);

template <std::unsigned_integral Value, usize Size>
[[nodiscard]] constexpr auto faultBytes(Value value) noexcept -> Array<Byte, Size> {
  Array<Byte, Size> result{};
  std::ranges::for_each(std::views::indices(Size), [&](usize index) constexpr noexcept -> void {
    result.at(index) = static_cast<Byte>(value & 0xff);
    value >>= 8;
  });
  return result;
}

template <std::unsigned_integral Value, usize Size>
[[nodiscard]] constexpr auto faultValue(const Array<Byte, Size> &bytes) noexcept -> Value {
  Value result{};
  std::ranges::for_each(std::views::indices(Size), [&](usize index) constexpr noexcept -> void {
    result |= static_cast<Value>(std::to_integer<u8>(bytes.at(index))) << (index * 8);
  });
  return result;
}

[[nodiscard]] constexpr auto makeFaultRecord(const NativeFault &fault) noexcept -> FaultRecord {
  return FaultRecord{
      .magic = faultBytes<u32, 4>(faultRecordMagic),
      .kind = static_cast<u8>(fault.kind),
      .signal = static_cast<u8>(fault.signal),
      .code = faultBytes<u32, 4>(static_cast<u32>(fault.code)),
      .address = faultBytes<u64, 8>(fault.address),
      .instruction = faultBytes<u64, 8>(fault.instruction),
      .symbolsAvailable = static_cast<u8>(fault.symbolsAvailable),
  };
}

[[nodiscard]] constexpr auto decodeFaultRecord(const FaultRecord &record) noexcept -> Option<NativeFault> {
  if (faultValue<u32>(record.magic) != faultRecordMagic)
    return None;

  return NativeFault{
      .kind = static_cast<NativeFaultKind>(record.kind),
      .signal = static_cast<NativeSignal>(record.signal),
      .code = static_cast<i32>(faultValue<u32>(record.code)),
      .address = faultValue<u64>(record.address),
      .instruction = faultValue<u64>(record.instruction),
      .symbolsAvailable = record.symbolsAvailable != 0,
  };
}

/// Describes one child-process launch without exposing platform handles to Switch.
struct WorkerLaunch final {
  Path executable;
  Vec<Pair<String, String>> variables;
};

/// Describes how the worker ended from the parent's point of view.
struct WorkerOutcome final {
  bool launched{};
  i32 exitCode{};
  Option<NativeFault> fault;
  String error;
};

/// Returns the current test executable path.
[[nodiscard]] auto executablePath() -> Result<Path>;

/// Starts one isolated worker and waits for its terminal status.
[[nodiscard]] auto launchWorker(const WorkerLaunch &launch) -> WorkerOutcome;

/// Installs the platform-native crash boundary inside a worker process.
[[nodiscard]] auto installWorkerFaultHandler(const Path &faultPath) noexcept -> bool;

/// Converts a platform fault record into the public fault representation.
[[nodiscard]] auto readFaultRecord(const Path &path) noexcept -> Option<NativeFault>;

} // namespace Switch::detail::isolation
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers, bugprone-exception-escape)
