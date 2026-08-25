module;

#include <windows.h>

module Switch;

import std;
import Miracle;

using namespace Miracle;

// NOLINTBEGIN
namespace Switch::detail::isolation {

namespace {

HANDLE faultHandle{INVALID_HANDLE_VALUE};

LONG WINAPI faultHandler(EXCEPTION_POINTERS *information) noexcept {
  const FaultRecord record = makeFaultRecord(NativeFault{
      .kind = NativeFaultKind::StructuredException,
      .code = information == nullptr or information->ExceptionRecord == nullptr
                  ? 0
                  : static_cast<i32>(information->ExceptionRecord->ExceptionCode),
      .address = information == nullptr or information->ExceptionRecord == nullptr
                     ? 0
                     : reinterpret_cast<u64>(information->ExceptionRecord->ExceptionAddress),
      .instruction = 0,
      .symbolsAvailable = false,
  });

  if (faultHandle != INVALID_HANDLE_VALUE) {
    DWORD written{};
    static_cast<void>(
        ::WriteFile(faultHandle, std::addressof(record), sizeof(record), std::addressof(written), nullptr));
    static_cast<void>(::FlushFileBuffers(faultHandle));
  }

  const UINT code = information == nullptr or information->ExceptionRecord == nullptr
                        ? static_cast<UINT>(0xC0000001)
                        : information->ExceptionRecord->ExceptionCode;
  ::TerminateProcess(::GetCurrentProcess(), code);
  return EXCEPTION_EXECUTE_HANDLER;
}

[[nodiscard]] auto environmentBlock(const Vec<Pair<String, String>> &variables) -> Vec<char> {
  Vec<char> result{};
  const auto appendEntry = [&result](StringView entry) -> void {
    result.insert(result.end(), entry.begin(), entry.end());
    result.push_back('\0');
  };

  if (LPCH original = ::GetEnvironmentStringsA()) {
    const char *cursor = original;
    while (*cursor != '\0') {
      const StringView entry{cursor};
      const usize separator = entry.find('=');
      const StringView name = entry.substr(0, separator);
      const bool overridden = std::ranges::any_of(
          variables, [name](const Pair<String, String> &variable) -> bool { return variable.first == name; });
      if (not overridden)
        appendEntry(entry);
      cursor += entry.size() + 1;
    }
    static_cast<void>(::FreeEnvironmentStringsA(original));
  }

  std::ranges::for_each(variables, [&appendEntry](const Pair<String, String> &variable) -> void {
    appendEntry(std::format("{}={}", variable.first, variable.second));
  });
  result.push_back('\0');
  return result;
}

} // namespace

auto executablePath() -> Result<Path> {
  Array<char, 32768> buffer{};
  const DWORD size = ::GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (size == 0 or size >= buffer.size())
    return bail{"Switch could not resolve the current executable path"};

  buffer[size] = '\0';
  return Path{buffer.data()};
}

auto launchWorker(const WorkerLaunch &launch) -> WorkerOutcome {
  Vec<char> environment = environmentBlock(launch.variables);
  String command = std::format("\"{}\"", launch.executable.string());
  STARTUPINFOA startup{};
  startup.cb = static_cast<DWORD>(sizeof(startup));
  PROCESS_INFORMATION process{};

  const BOOL created = ::CreateProcessA(launch.executable.string().c_str(),
      command.data(),
      nullptr,
      nullptr,
      FALSE,
      CREATE_NO_WINDOW,
      environment.data(),
      nullptr,
      std::addressof(startup),
      std::addressof(process));
  if (not created)
    return WorkerOutcome{.error = "CreateProcessA() failed while starting a Switch worker"};

  static_cast<void>(::WaitForSingleObject(process.hProcess, INFINITE));
  DWORD exitCode{};
  static_cast<void>(::GetExitCodeProcess(process.hProcess, std::addressof(exitCode)));
  static_cast<void>(::CloseHandle(process.hThread));
  static_cast<void>(::CloseHandle(process.hProcess));

  WorkerOutcome result{
      .launched = true,
      .exitCode = static_cast<i32>(exitCode),
  };
  if (exitCode != 0) {
    result.fault = NativeFault{
        .kind = NativeFaultKind::StructuredException,
        .code = static_cast<i32>(exitCode),
    };
  }

  const auto variable = std::ranges::find_if(launch.variables,
      [](const Pair<String, String> &item) -> bool { return item.first == "SWITCH_TEST_WORKER_FAULT"; });
  if (variable != launch.variables.end()) {
    if (const Option<NativeFault> fault = readFaultRecord(Path{variable->second}))
      result.fault = fault;
  }

  return result;
}

auto installWorkerFaultHandler(const Path &path) noexcept -> bool {
  try {
    const String filename{path};
    if (faultHandle != INVALID_HANDLE_VALUE)
      static_cast<void>(::CloseHandle(faultHandle));
    faultHandle = ::CreateFileA(filename.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (faultHandle == INVALID_HANDLE_VALUE)
      return false;

    static_cast<void>(::SetUnhandledExceptionFilter(&faultHandler));
    return true;
  } catch (...) {
    return false;
  }
}

auto readFaultRecord(const Path &path) noexcept -> Option<NativeFault> {
  try {
    std::ifstream input{path, std::ios::binary};
    if (not input)
      return None;

    FaultRecord record{};
    input.read(reinterpret_cast<char *>(std::addressof(record)), sizeof(record));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(record)))
      return None;

    return decodeFaultRecord(record);
  } catch (...) {
    return None;
  }
}

} // namespace Switch::detail::isolation
// NOLINTEND
