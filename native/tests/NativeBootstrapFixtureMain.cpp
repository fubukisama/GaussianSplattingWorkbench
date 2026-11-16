// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeBootstrapFixtureSecurity.h"
#include "NativeFirstInstall.h"
#include <iostream>

using namespace gsw::bootstrapfixture;
int wmain(int argc, wchar_t **argv) {
  Json result{{"schema", "gsw.bootstrap_fixture_result/1"}, {"ok", false}, {"testOnly", true},
      {"production", false}, {"authenticodeClaim", false}, {"targetMutated", false}};
  try {
    need(argc == 3 && std::wstring(argv[1]) == L"--install", "BOOTSTRAP_INVALID_REQUEST");
    const auto target = localPath(argv[2]);
    const auto base = wide(GSW_BOOTSTRAP_FIXTURE_ROOT);
    need(beneath(base, target), "BOOTSTRAP_OUTSIDE_OWNED_FIXTURE");
    noLinks(target);
    HANDLE raw = nullptr;
    need(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw) != FALSE, "BOOTSTRAP_TOKEN_QUERY_FAILED");
    Handle token(raw);
    result["terminalToken"] = tokenFacts(token.value);
    need(ordinaryClient(result["terminalToken"]), "BOOTSTRAP_CLIENT_NOT_RESTRICTED");
    result["terminalEnvironment"] = clientEnvironmentFacts();
    need(result["terminalEnvironment"]["discoverableDeveloperTools"].empty() &&
        result["terminalEnvironment"]["sensitiveEnvironmentPresent"] == false, "BOOTSTRAP_CLIENT_DEVELOPER_DEPENDENCY");
    const auto installed = gsw::firstinstall::install(target);
    result["ok"] = installed.committed;
    result["targetMutated"] = installed.committed;
    result["code"] = installed.code;
    result["systemCode"] = installed.systemCode;
    result["helperCode"] = installed.helperCode;
    result["helperExitCode"] = installed.helperExitCode;
    if (installed.helperDownloadTrace) {
      result["helperDownloadTrace"] = {{"qtError", installed.helperQtNetworkError},
        {"httpStatus", installed.helperHttpStatus}, {"bytes", installed.helperReceivedBytes},
        {"sslSupported", installed.helperSslSupported}, {"tlsBackend", installed.helperTlsBackend}};
    }
    result["buildId"] = installed.buildId;
    result["executableSha256"] = installed.executableSha256;
  } catch (const Failure &error) {
    result["code"] = error.code;
    result["systemCode"] = error.systemCode;
  } catch (...) { result["code"] = "BOOTSTRAP_FIXTURE_FAILED"; }
  std::cout << result.dump(-1, ' ', true) << '\n';
  return result["ok"] == true ? 0 : 2;
}
