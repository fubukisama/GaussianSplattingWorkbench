// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>

namespace gsw::firstinstall {
struct Result {
  bool committed = false;
  std::string code, helperCode, buildId, executableSha256;
  unsigned long systemCode = 0;
  unsigned long helperExitCode = 0;
#ifdef GSW_FIRST_INSTALL_FIXTURE
  bool helperDownloadTrace = false, helperSslSupported = false;
  unsigned int helperQtNetworkError = 0, helperHttpStatus = 0;
  std::uint64_t helperReceivedBytes = 0;
  unsigned long helperSspiStatus = 0;
  bool helperContextCreation = false;
  std::string helperTlsBackend;
#endif
};

// This first-install-only entry takes a fresh local destination, not trust
// material. The independently distributed installer pins its adjacent kit at
// build time. It never upgrades/migrates/replaces another installation.
Result install(const std::wstring &destination);
} // namespace gsw::firstinstall
