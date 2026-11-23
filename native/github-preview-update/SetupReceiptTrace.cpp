// SPDX-License-Identifier: GPL-3.0-or-later
// Hosted diagnostic only. Unwind our own ShellExecuteExW thread. Publish
// resolved symbol names only; no dumps, addresses, arguments or window input.
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <dbghelp.h>
#include <atomic>
#include <array>
#include <cstring>
#include <string>

namespace {
struct LaunchThread {
  DWORD id;
  ULONG_PTR low, high;
};
LaunchThread launch{};
std::atomic_bool shellReturned{false};
using Frames = std::array<DWORD64, 48>;

size_t unwind(CONTEXT &ctx, Frames &frames) {
  size_t count = 0;
  // A leaf syscall stub can have no pdata entry. Read only its ABI return
  // slot, inside the known stack of this test process. Nothing is exported.
  __try {
    while (count < frames.size() && ctx.Rip && ctx.Rsp >= launch.low &&
           ctx.Rsp <= launch.high - sizeof(DWORD64)) {
      frames[count] = count == 0 ? ctx.Rip : ctx.Rip - 1;
      ++count;
      const auto previousSp = ctx.Rsp;
      DWORD64 base = 0;
      if (const auto entry = RtlLookupFunctionEntry(ctx.Rip, &base, nullptr)) {
        void *handlerData = nullptr;
        DWORD64 frame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx.Rip, entry, &ctx, &handlerData, &frame, nullptr);
      } else {
        ctx.Rip = *reinterpret_cast<const DWORD64 *>(ctx.Rsp);
        ctx.Rsp += sizeof(DWORD64);
      }
      if (ctx.Rsp <= previousSp) break;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    // Keep any already unwound frames, never probe outside this thread again.
  }
  return count;
}

size_t capturePendingFrames(Frames &frames) {
  HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION,
                             FALSE, launch.id);
  if (!thread) return 0;
  size_t count = 0;
  if (GetProcessIdOfThread(thread) == GetCurrentProcessId()) {
    const auto wasSuspended = SuspendThread(thread);
    if (wasSuspended != DWORD(-1)) {
      if (!wasSuspended && !shellReturned.load()) {
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (GetThreadContext(thread, &ctx)) count = unwind(ctx, frames);
        SecureZeroMemory(&ctx, sizeof(ctx));
      }
      ResumeThread(thread);
    }
  }
  CloseHandle(thread);
  return count;
}

bool allowedName(const char *s, const char *punctuation, size_t max) {
  size_t count = 0;
  for (; *s; ++s) {
    if (++count > max) return false;
    const char c = *s;
    if (!(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') &&
        !(c >= '0' && c <= '9') && !std::strchr(punctuation, c)) return false;
  }
  return count != 0;
}

void writeSymbolNames(const Frames &frames, size_t count, const wchar_t *output) {
  const HANDLE process = GetCurrentProcess();
  SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
  if (!SymInitializeW(process, L"D:\\gsw-preview-qa\\symbols", TRUE)) return;
  std::string lines;
  for (size_t i = 0; i < count; ++i) {
    alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
    auto *sym = reinterpret_cast<SYMBOL_INFO *>(storage);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = MAX_SYM_NAME;
    DWORD64 displacement = 0;
    if (!SymFromAddr(process, frames[i], &displacement, sym)) continue;
    IMAGEHLP_MODULE64 module{};
    module.SizeOfStruct = sizeof(module);
    if (!SymGetModuleInfo64(process, frames[i], &module)) continue;
    // DbgHelp can return the nearest unrelated export without a public PDB.
    if (module.SymType != SymPdb && module.SymType != SymDia &&
        displacement != 0 && (sym->Size == 0 || displacement >= sym->Size)) continue;
    if (allowedName(module.ModuleName, "_-. ", 63) && allowedName(sym->Name, "_-. :$@<>,?~*+[]'", 500))
      lines += std::string(module.ModuleName) + "!" + sym->Name + "\n";
  }
  SymCleanup(process);
  if (lines.empty()) return;
  HANDLE log = CreateFileW(output, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (log != INVALID_HANDLE_VALUE) {
    DWORD wrote = 0;
    WriteFile(log, lines.data(), DWORD(lines.size()), &wrote, nullptr);
    CloseHandle(log);
  }
}

DWORD WINAPI observeLaunch(void *) {
  Sleep(7000);
  if (shellReturned.load()) return 0;
  Frames frames{};
  const size_t count = capturePendingFrames(frames);
  if (count) writeSymbolNames(frames, count, L"D:\\gsw-preview-qa\\evidence\\shell-stack-1.txt");
  SecureZeroMemory(frames.data(), sizeof(frames));
  Sleep(18000);
  if (!shellReturned.load()) {
    const size_t second = capturePendingFrames(frames);
    if (second) writeSymbolNames(frames, second, L"D:\\gsw-preview-qa\\evidence\\shell-stack-2.txt");
    SecureZeroMemory(frames.data(), sizeof(frames));
  }
  return 0;
}

BOOL observeShell(SHELLEXECUTEINFOW *info) {
  const DWORD before = GetLastError();
  launch.id = GetCurrentThreadId();
  GetCurrentThreadStackLimits(&launch.low, &launch.high);
  if (HANDLE thread = CreateThread(nullptr, 0, observeLaunch, nullptr, 0, nullptr)) CloseHandle(thread);
  SetLastError(before);
  const BOOL result = ShellExecuteExW(info);
  const DWORD after = GetLastError();
  shellReturned = true;
  SetLastError(after);
  return result;
}
} // namespace

#define ShellExecuteExW observeShell
#include "../src/NativePreviewSetupWaiter.cpp"
