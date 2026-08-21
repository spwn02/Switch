module;

#include <cerrno>
#include <csignal>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

module Switch;

import std;
import Miracle;

import :FaultIsolation;

using namespace Miracle;

// NOLINTBEGIN
namespace Switch::detail::isolation {

namespace {

volatile std::sig_atomic_t faultDescriptor{-1};

[[nodiscard]] constexpr auto nativeSignal(int signalNumber) noexcept -> NativeSignal {
  switch (signalNumber) {
    case SIGABRT: return NativeSignal::Abort;
    case SIGBUS: return NativeSignal::BusError;
    case SIGFPE: return NativeSignal::FloatingPointException;
    case SIGILL: return NativeSignal::IllegalInstruction;
    case SIGSEGV: return NativeSignal::SegmentationFault;
    case SIGTRAP: return NativeSignal::Trap;
    default: return NativeSignal::Unknown;
  }
}

[[nodiscard]] constexpr auto signalFault(int signalNumber) noexcept -> NativeFault {
  return NativeFault{
      .kind = NativeFaultKind::Signal,
      .signal = nativeSignal(signalNumber),
      .code = 128 + signalNumber,
  };
}

auto faultHandler(int signalNumber, siginfo_t *information, void *context) noexcept -> void {
  u64 instruction{};
#if defined(__x86_64__)
  if (context != nullptr)
    instruction = static_cast<u64>(reinterpret_cast<ucontext_t *>(context)->uc_mcontext.gregs[REG_RIP]);
#elif defined(__aarch64__)
  if (context != nullptr)
    instruction = static_cast<u64>(reinterpret_cast<ucontext_t *>(context)->uc_mcontext.pc);
#endif
  const FaultRecord record = makeFaultRecord(NativeFault{
      .kind = NativeFaultKind::Signal,
      .signal = nativeSignal(signalNumber),
      .code = 128 + signalNumber,
      .address = information == nullptr ? 0 : reinterpret_cast<u64>(information->si_addr),
      .instruction = instruction,
      .symbolsAvailable = false,
  });

  if (faultDescriptor >= 0) {
    const Byte *data = reinterpret_cast<const Byte *>(std::addressof(record));
    usize remaining = sizeof(record);
    while (remaining != 0) {
      const ssize_t written = ::write(faultDescriptor, data, remaining);
      if (written > 0) {
        data += written;
        remaining -= static_cast<usize>(written);
      } else if (written < 0 and errno == EINTR) {
        continue;
      } else {
        break;
      }
    }
  }

  ::_exit(128 + signalNumber);
}

[[nodiscard]] auto faultPath(const WorkerLaunch &launch) -> Option<Path> {
  const auto variable = std::ranges::find_if(launch.variables,
      [](const Pair<String, String> &item) -> bool { return item.first == "SWITCH_TEST_WORKER_FAULT"; });
  if (variable == launch.variables.end())
    return None;

  return Path{variable->second};
}

[[nodiscard]] auto environmentBlock(const Vec<Pair<String, String>> &variables) -> Vec<String> {
  Vec<String> result{};

  if (environ != nullptr) {
    char **cursor = environ;
    while (*cursor != nullptr) {
      const StringView entry{*cursor};
      const usize separator = entry.find('=');
      const StringView name = entry.substr(0, separator);
      const bool overriden = std::ranges::any_of(
          variables, [name](const Pair<String, String> &variable) -> bool { return variable.first == name; });
      if (not overriden)
        result.emplace_back(entry);
      ++cursor;
    }
  }

  std::ranges::for_each(variables, [&result](const Pair<String, String> &variable) -> void {
    result.push_back(std::format("{}={}", variable.first, variable.second));
  });
  return result;
}

} // namespace

auto executablePath() -> Result<Path> {
  constexpr usize kb4{4096};
  Array<char, kb4> buffer{};
  const auto size = static_cast<isize>(::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1));
  if (size <= 0)
    return bail({"Switch could not resolve /proc/self/exe"});

  buffer.at(static_cast<usize>(size)) = '\0';
  return Path{buffer.data()};
}

auto launchWorker(const WorkerLaunch &launch) -> WorkerOutcome {
  Vec<String> environment = environmentBlock(launch.variables);
  Vec<char *> environmentPointers{};
  environmentPointers.reserve(environment.size() + 1);
  std::ranges::for_each(environment,
      [&environmentPointers](String &entry) -> void { environmentPointers.push_back(entry.data()); });
  environmentPointers.push_back(nullptr);

  String executable = launch.executable.string();
  Array<char *, 2> arguments{executable.data(), nullptr};
  const pid_t child = ::fork();
  if (child < 0)
    return WorkerOutcome{
        .error = "fork() failed while starting a Switch worker",
    };

  if (child == 0) {
    ::execve(executable.c_str(), arguments.data(), environmentPointers.data());
    ::_exit(127);
  }

  int status{};
  pid_t waited{};
  do {
    waited = ::waitpid(child, std::addressof(status), 0);
  } while (waited < 0 and errno == EINTR);

  if (waited < 0)
    return WorkerOutcome{
        .launched = true,
        .error = "waitpid() failed while observing a Switch worker",
    };

  WorkerOutcome result{
      .launched = true,
  };
  if (WIFSIGNALED(status)) {
    result.fault = signalFault(WTERMSIG(status));
  } else if (WIFEXITED(status)) {
    result.exitCode = WEXITSTATUS(status);
    if (result.exitCode != 0) {
      const int signalNumber = result.exitCode - 128;
      if (nativeSignal(signalNumber) != NativeSignal::Unknown)
        result.fault = signalFault(signalNumber);
      else if (result.exitCode == 127)
        result.error = "execve() failed while starting a Switch worker";
      else
        result.fault = NativeFault{
            .kind = NativeFaultKind::Terminated,
            .code = result.exitCode,
        };
    }
  } else {
    result.fault = NativeFault{
        .kind = NativeFaultKind::Terminated,
        .code = status,
    };
  }

  if (const Option<Path> path = faultPath(launch)) {
    if (const Option<NativeFault> fault = readFaultRecord(*path))
      result.fault = fault;
  }

  return result;
}

auto installWorkerFaultHandler(const Path &path) noexcept -> bool {
  try {
    const String filename{path};
    const int descriptor = ::open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (descriptor < 0)
      return false;

    const int previous = faultDescriptor;
    faultDescriptor = descriptor;
    if (previous >= 0)
      static_cast<void>(::close(previous));
    struct sigaction action{};
    action.sa_sigaction = &faultHandler;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(std::addressof(action.sa_mask));

    constexpr Array<int, 6> signals{SIGSEGV, SIGILL, SIGABRT, SIGFPE, SIGBUS, SIGTRAP};
    const bool installed = std::ranges::all_of(signals, [&action](int signalNumber) -> bool {
      return ::sigaction(signalNumber, std::addressof(action), nullptr) == 0;
    });
    if (not installed) {
      static_cast<void>(::close(faultDescriptor));
      faultDescriptor = -1;
    }
    return installed;
  } catch (...) {
    return false;
  }
}

auto readFaultRecord(const Path &path) noexcept -> Option<NativeFault> {
  const String filename{path};
  const int descriptor = ::open(filename.c_str(), O_RDONLY);
  if (descriptor < 0)
    return None;

  const auto closeDescriptor =
      std::scope_exit([descriptor] noexcept -> void { static_cast<void>(::close(descriptor)); });
  FaultRecord record{};
  Byte *data = reinterpret_cast<Byte *>(std::addressof(record));
  usize remaining = sizeof(record);
  while (remaining != 0) {
    const ssize_t count = ::read(descriptor, data, remaining);
    if (count > 0) {
      data += count;
      remaining -= static_cast<usize>(count);
    } else if (count < 0 and errno == EINTR) {
      continue;
    } else {
      return None;
    }
  }

  Option<NativeFault> fault = decodeFaultRecord(record);
  if (fault and fault->instruction != 0) {
    Dl_info symbol{};
    fault->symbolsAvailable =
        ::dladdr(reinterpret_cast<void *>(fault->instruction), std::addressof(symbol)) != 0;
  }
  return fault;
}

} // namespace Switch::detail::isolation
// NOLINTEND
