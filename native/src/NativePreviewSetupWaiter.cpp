// SPDX-License-Identifier: GPL-3.0-or-later
// Static-CRT handoff deliberately runs outside Setup's owned file list. Only
// inherited kernel handles are accepted. No feed URL, shell command, installer
// path, target path or arbitrary installer options can be supplied.
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <cwctype>
#include <cstdio>
#include <string>
#include <vector>

namespace {
std::wstring parent(const std::wstring &path) { return path.substr(0, path.find_last_of(L'\\')); }
std::wstring leaf(const std::wstring &path) { return path.substr(path.find_last_of(L'\\') + 1); }
bool same(const std::wstring &a, const std::wstring &b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }
bool absent(const std::wstring &p) {
  if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return false;
  const auto e = GetLastError(); return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND;
}
HANDLE handle(const wchar_t *s) {
  if (!*s) return nullptr;
  for (auto p = s; *p; ++p) if (*p < L'0' || *p > L'9') return nullptr;
  return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(_wcstoui64(s, nullptr, 10)));
}
void recordExit(const std::wstring &cache, const std::wstring &hash, DWORD code) {
  const auto result = cache + L"\\setup-result-" + hash + L".json";
  const auto tmp = result + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
  HANDLE out = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (out == INVALID_HANDLE_VALUE) return;
  char data[150]; const int size = sprintf_s(data, "{\"schema\":\"gsw.github_preview_setup_result/1\",\"exitCode\":%lu}", code);
  DWORD wrote = 0;
  const bool ok = size > 0 && WriteFile(out, data, DWORD(size), &wrote, nullptr) && wrote == DWORD(size) && FlushFileBuffers(out);
  CloseHandle(out);
  if (!ok || !MoveFileExW(tmp.c_str(), result.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) DeleteFileW(tmp.c_str());
}
}
int wmain(int argc, wchar_t **argv) {
  if (argc != 5 || wcscmp(argv[1], L"--handoff")) return 2;
  HANDLE file = handle(argv[2]), app = handle(argv[3]), helper = handle(argv[4]);
  if (!file || !helper || GetFileType(file) != FILE_TYPE_DISK) return 2;
  wchar_t buffer[32768]{};
  if (!GetModuleFileNameW(nullptr, buffer, 32768)) return 2;
  const auto cache = parent(buffer), root = parent(cache);
  if (!same(leaf(cache), L".gsw-github-preview")) return 2;
  const auto n = GetFinalPathNameByHandleW(file, buffer, 32768, FILE_NAME_NORMALIZED);
  if (!n || n >= 32768) return 2;
  std::wstring path(buffer, n);
  if (path.compare(0, 4, L"\\\\?\\") == 0) path.erase(0, 4);
  const auto name = leaf(path);
  if (!same(parent(path), cache) || name.size() != 68 || name.substr(64) != L".exe" ||
      name.substr(0,64).find_first_not_of(L"0123456789abcdef") != std::wstring::npos) return 2;
  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(file, &info) || info.nNumberOfLinks != 1 ||
      (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) return 2;
  HANDLE lock = CreateFileW((cache + L"\\setup-handoff.lock").c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (lock == INVALID_HANDLE_VALUE) return 2;
  if ((app && WaitForSingleObject(app, 60000) != WAIT_OBJECT_0) ||
      WaitForSingleObject(helper, 60000) != WAIT_OBJECT_0) return 2;
  if (!absent(root + L"\\current.json") || !absent(root + L"\\bootstrap.json") ||
      !absent(root + L"\\.update-trust-anchor") || !absent(root + L"\\updates\\trust")) return 2;
  if (!GetPrivateProfileStringW(L"Installation", L"AppId", L"", buffer, 32768, (root + L"\\gsw-installation.ini").c_str()) ||
      wcscmp(buffer, L"Fubukisama.GaussianSceneWorkbench.Native")) return 2;
  if (root.find(L'"') != std::wstring::npos) return 2;
  const std::wstring args = L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /NOCLOSEAPPLICATIONS /NORESTARTAPPLICATIONS /CURRENTUSER /DIR=\"" + root + L"\"";
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
  SHELLEXECUTEINFOW run{}; run.cbSize = sizeof(run); run.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
  run.lpFile = path.c_str(); run.lpParameters = args.c_str(); run.lpVerb = L"open"; run.nShow = SW_SHOWNORMAL;
  // No elevation, execution-policy override, or suppression of Windows security UI.
  if (!ShellExecuteExW(&run) || !run.hProcess) { recordExit(cache, name.substr(0,64), GetLastError()); return 2; }
  if (WaitForSingleObject(run.hProcess, 15 * 60 * 1000) != WAIT_OBJECT_0) return 2;
  DWORD exitCode = 2; GetExitCodeProcess(run.hProcess, &exitCode);
  recordExit(cache, name.substr(0,64), exitCode);
  CloseHandle(run.hProcess); CloseHandle(lock); CoUninitialize();
  return exitCode == 0 ? 0 : 2;
}
