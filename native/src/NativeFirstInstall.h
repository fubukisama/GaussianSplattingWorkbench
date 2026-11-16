// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>

namespace gsw::firstinstall {
struct Result {
  bool committed = false;
  std::string code, helperCode, buildId, executableSha256;
  unsigned long systemCode = 0;
  unsigned long helperExitCode = 0;
};

// This first-install-only entry takes a fresh local destination, not trust
// material. The independently distributed installer pins its adjacent kit at
// build time. It never upgrades/migrates/replaces another installation.
Result install(const std::wstring &destination);
} // namespace gsw::firstinstall
