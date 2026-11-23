// SPDX-License-Identifier: GPL-3.0-or-later
// Only compiled by the standalone hosted replay, never part of a package.
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <wct.h>
#include <atomic>
#include <cstdio>
#include <cwchar>
#include <initializer_list>

namespace {
// Fixed runner-owned location. The allowlist never logs paths, arguments,
// handles, window content, registry data, or security tokens.
void trace(const char *event, DWORD value = 0, DWORD error = 0, DWORD detail = 0) {
  const DWORD saved = GetLastError();
  HANDLE log = CreateFileW(L"D:\\gsw-preview-qa\\evidence\\setup-handoff-trace.jsonl",
      FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (log != INVALID_HANDLE_VALUE) {
    char text[200];
    const int count = sprintf_s(text,
      "{\"event\":\"%s\",\"tickMs\":%llu,\"waiterPid\":%lu,\"value\":%lu,\"error\":%lu,\"detail\":%lu}\n",
      event, GetTickCount64(), GetCurrentProcessId(), value, error, detail);
    DWORD written = 0;
    if (count > 0) WriteFile(log, text, DWORD(count), &written, nullptr);
    CloseHandle(log);
  }
  SetLastError(saved);
}

std::atomic_bool shellReturned{false};
HANDLE setupChild{};

// Passive sampling while the API call is pending; never follow waits through
// external processes or turn on privileges. Failure to sample is not a gate.
DWORD WINAPI observeLaunch(void *data) {
  const auto thread = DWORD(reinterpret_cast<ULONG_PTR>(data));
  for (DWORD delay : {5000UL, 25000UL, 120000UL}) {
    Sleep(delay);
    if (shellReturned.load()) break;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
      PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
      DWORD children = 0;
      for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        if (entry.th32ParentProcessID == GetCurrentProcessId()) ++children;
      }
      CloseHandle(snapshot);
      trace("launch_pending_children", children);
    }
    if (HWCT session = OpenThreadWaitChainSession(0, nullptr)) {
      WAITCHAIN_NODE_INFO nodes[WCT_MAX_NODE_COUNT]{};
      DWORD count = WCT_MAX_NODE_COUNT;
      BOOL cycle = FALSE;
      if (GetThreadWaitChain(session, 0, 0, thread, &count, nodes, &cycle)) {
        for (DWORD i = 0; i < count; ++i)
          trace("launch_pending_wait_node", DWORD(nodes[i].ObjectType), DWORD(nodes[i].ObjectStatus), i);
      } else {
        trace("launch_pending_wait_unavailable", 0, GetLastError());
      }
      CloseThreadWaitChainSession(session);
    }
  }
  return 0;
}
BOOL tracedShell(SHELLEXECUTEINFOW *info) {
  const DWORD before = GetLastError();
  trace("windows_launch_enter", info->fMask, 0, GetCurrentThreadId());
  auto observer = CreateThread(nullptr, 0, observeLaunch,
      reinterpret_cast<void *>(ULONG_PTR(GetCurrentThreadId())), 0, nullptr);
  if (observer) CloseHandle(observer);
  SetLastError(before);
  const BOOL ok = ShellExecuteExW(info);
  const DWORD saved = GetLastError();
  shellReturned = true;
  setupChild = info->hProcess;
  trace("windows_launch_return", ok != FALSE, ok ? 0 : saved, setupChild ? GetProcessId(setupChild) : 0);
  SetLastError(saved);
  return ok;
}
DWORD tracedWait(HANDLE object, DWORD timeout) {
  const bool installer = setupChild && object == setupChild;
  trace(installer ? "setup_child_wait_enter" : "original_process_wait_enter", timeout);
  const DWORD result = WaitForSingleObject(object, timeout), error = GetLastError();
  trace(installer ? "setup_child_wait_return" : "original_process_wait_return",
        result, result == WAIT_FAILED ? error : 0);
  SetLastError(error);
  return result;
}
BOOL tracedExitCode(HANDLE object, LPDWORD value) {
  const BOOL ok = GetExitCodeProcess(object, value);
  const DWORD error = GetLastError();
  trace("setup_child_exit_code", ok ? *value : 0, ok ? 0 : error);
  SetLastError(error);
  return ok;
}
HANDLE tracedOpen(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES attributes,
                  DWORD disposition, DWORD flags, HANDLE from) {
  const auto file = CreateFileW(path, access, share, attributes, disposition, flags, from);
  const DWORD error = GetLastError();
  if (std::wcsstr(path, L"\\setup-result-"))
    trace("receipt_open", file != INVALID_HANDLE_VALUE, file == INVALID_HANDLE_VALUE ? error : 0);
  SetLastError(error);
  return file;
}
BOOL tracedWrite(HANDLE f, LPCVOID data, DWORD size, LPDWORD bytes, LPOVERLAPPED overlap) {
  const BOOL ok = WriteFile(f, data, size, bytes, overlap);
  const DWORD error = GetLastError();
  trace("receipt_write", ok != FALSE, ok ? 0 : error, ok && bytes ? *bytes : 0);
  SetLastError(error);
  return ok;
}
BOOL tracedFlush(HANDLE f) {
  const BOOL ok = FlushFileBuffers(f);
  const DWORD error = GetLastError();
  trace("receipt_flush", ok != FALSE, ok ? 0 : error);
  SetLastError(error);
  return ok;
}
BOOL tracedRename(LPCWSTR src, LPCWSTR dest, DWORD flags) {
  const BOOL ok = MoveFileExW(src, dest, flags);
  const DWORD error = GetLastError();
  trace("receipt_publish", ok != FALSE, ok ? 0 : error);
  SetLastError(error);
  return ok;
}
} // namespace

// Run the exact source, arguments, trust decisions, and timeouts. Only observe
// the results of these specific calls in this test translation unit.
#define ShellExecuteExW tracedShell
#define WaitForSingleObject tracedWait
#define GetExitCodeProcess tracedExitCode
#define CreateFileW tracedOpen
#define WriteFile tracedWrite
#define FlushFileBuffers tracedFlush
#define MoveFileExW tracedRename
#include "../src/NativePreviewSetupWaiter.cpp"
