module Switch;

import std;
import Miracle;

using namespace Miracle;

namespace Switch::detail::isolation {

auto executablePath() -> Result<Path> {
  return bail({"Switch process isolation is unavailable on this platform"});
}

auto launchWorker(const WorkerLaunch & /*ignored*/) -> WorkerOutcome {
  return WorkerOutcome{
      .fault =
          NativeFault{
              .kind = NativeFaultKind::IsolationUnavailable,
          },
      .error = "Switch process isolation is unavailable on this platform",
  };
}

auto installWorkerFaultHandler(const Path & /*ignored*/) noexcept -> bool {
  return false;
}

auto readFaultRecord(const Path & /*ignored*/) noexcept -> Option<NativeFault> {
  return None;
}

} // namespace Switch::detail::isolation
