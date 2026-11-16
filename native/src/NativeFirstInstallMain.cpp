// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeFirstInstall.h"
#include "json.hpp"
#include <iostream>
#include <string_view>

int wmain(int argc, wchar_t **argv) {
  using Json = nlohmann::json;
  gsw::firstinstall::Result result;
  if (argc == 3 && std::wstring_view(argv[1]) == L"--install") {
    result = gsw::firstinstall::install(argv[2]);
  } else {
    result.code = "BOOTSTRAP_INVALID_REQUEST";
  }
  const Json output{{"schema", "gsw.native_first_install/1"}, {"ok", result.committed},
    {"code", result.code}, {"systemCode", result.systemCode}, {"helperCode", result.helperCode},
    {"helperExitCode", result.helperExitCode},
    {"buildId", result.buildId}, {"executableSha256", result.executableSha256}};
  std::cout << output.dump() << '\n';
  return result.committed ? 0 : 2;
}
