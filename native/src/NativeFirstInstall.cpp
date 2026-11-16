// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeFirstInstall.h"
#include "json.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <objbase.h>
#include <sddl.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#ifndef GSW_FIRST_INSTALL_MANIFEST_SHA256
#error The distributor must embed the authorized bootstrap manifest SHA-256.
#endif

namespace gsw::firstinstall {
namespace {
using Json = nlohmann::json;
using Path = std::filesystem::path;
constexpr std::string_view manifestPin = GSW_FIRST_INSTALL_MANIFEST_SHA256;
constexpr std::uint64_t maxKitBytes = 512ull * 1024 * 1024;
constexpr auto launcherRelative = "bin/Gaussian Scene Workbench Launcher.exe";
#ifdef GSW_FIRST_INSTALL_FIXTURE
constexpr auto product = "Fubukisama.GaussianSceneWorkbench.UpdateFixture";
constexpr auto helperRelative = "bin/GSW Update Fixture Helper.exe";
// Leave time for readback and cleanup inside the existing 180s remote lab.
constexpr ULONGLONG helperBudgetMs = 150000;
#else
constexpr auto product = "Fubukisama.GaussianSceneWorkbench.Native";
constexpr auto helperRelative = "bin/Gaussian Scene Workbench Update Helper.exe";
// Existing Helper: up to 90s trust resolve, 30min download, then cached
// revalidation and publication. A separate outer bound also owns the child.
constexpr ULONGLONG helperBudgetMs = 35 * 60 * 1000;
#endif
struct Failure { const char *code; DWORD osError; };
[[noreturn]] void fail(const char *code, DWORD osError = ERROR_SUCCESS) { throw Failure{code, osError}; }
void need(bool condition, const char *code) { if (!condition) fail(code); }
void win(bool condition, const char *code) { if (!condition) fail(code, GetLastError()); }

class Handle {
public:
  explicit Handle(HANDLE handle = nullptr) : value(handle) {}
  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;
  Handle(Handle &&other) noexcept : value(other.value) { other.value = nullptr; }
  ~Handle() { close(); }
  void close() noexcept {
    if (*this) CloseHandle(value);
    value = nullptr;
  }
  explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
  HANDLE value;
};

bool sha(std::string_view value) {
  return value.size() == 64 && value.find_first_not_of("0123456789abcdef") == value.npos;
}
bool equivalent(std::wstring_view lhs, std::wstring_view rhs) {
  return lhs.size() == rhs.size() &&
      CompareStringOrdinal(lhs.data(), int(lhs.size()), rhs.data(), int(rhs.size()), TRUE) == CSTR_EQUAL;
}
bool under(std::wstring_view folder, std::wstring_view path) {
  return path.size() > folder.size() && equivalent(folder, path.substr(0, folder.size())) &&
      (folder.ends_with(L"\\") || path[folder.size()] == L'\\');
}
bool ordinaryComponent(std::wstring_view value) {
  if (value.empty() || value.size() > 255 || value == L"." || value == L".." ||
      value.back() == L'.' || value.back() == L' ' ||
      value.find_first_of(L"<>:\"|?*") != value.npos ||
      std::any_of(value.begin(), value.end(), [](wchar_t c) { return c < 32; })) return false;
  auto base = value.substr(0, value.find(L'.'));
  while (base.ends_with(L" ")) base.remove_suffix(1);
  for (const auto reserved : {L"CON", L"PRN", L"AUX", L"NUL", L"CONIN$", L"CONOUT$", L"CLOCK$"})
    if (equivalent(base, reserved)) return false;
  return base.size() != 4 || (!equivalent(base.substr(0, 3), L"COM") && !equivalent(base.substr(0, 3), L"LPT")) ||
      std::wstring_view(L"123456789\u00b9\u00b2\u00b3").find(base.back()) == std::wstring_view::npos;
}
std::wstring local(std::wstring value) {
  std::replace(value.begin(), value.end(), L'/', L'\\');
  need(value.size() >= 3 && value.size() < 32760 &&
      ((value.front() >= L'A' && value.front() <= L'Z') || (value.front() >= L'a' && value.front() <= L'z')) &&
      value[1] == L':' && value[2] == L'\\' && (value.size() == 3 || value.back() != L'\\'),
      "BOOTSTRAP_LOCAL_PATH_REQUIRED");
  for (const auto &part : Path(value).relative_path())
    need(ordinaryComponent(part.native()), "BOOTSTRAP_LOCAL_PATH_REQUIRED");
  // Do not let the filesystem parser silently collapse empty components.
  need(value.find(L"\\\\", 2) == value.npos, "BOOTSTRAP_LOCAL_PATH_REQUIRED");
  need(GetDriveTypeW(value.substr(0, 3).c_str()) == DRIVE_FIXED, "BOOTSTRAP_LOCAL_PATH_REQUIRED");
  return value;
}
std::wstring api(const Path &path) {
  auto value = path.native();
  std::replace(value.begin(), value.end(), L'/', L'\\');
  return L"\\\\?\\" + value;
}
Path moduleDirectory() {
  std::array<wchar_t, 32768> value{};
  const auto size = GetModuleFileNameW(nullptr, value.data(), DWORD(value.size()));
  win(size > 0 && size < value.size(), "BOOTSTRAP_IMAGE_PATH_FAILED");
  return Path(local(std::wstring(value.data(), size))).parent_path();
}
BY_HANDLE_FILE_INFORMATION exactInfo(HANDLE handle, const Path &path, bool directory) {
  BY_HANDLE_FILE_INFORMATION info{};
  win(GetFileInformationByHandle(handle, &info) != FALSE, "BOOTSTRAP_FILE_IDENTITY_FAILED");
  need(!(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
      bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory &&
      (directory || info.nNumberOfLinks == 1), "BOOTSTRAP_UNSAFE_FILE");
  std::array<wchar_t, 32768> final{};
  const auto size = GetFinalPathNameByHandleW(handle, final.data(), DWORD(final.size()), FILE_NAME_NORMALIZED);
  win(size > 0 && size < final.size(), "BOOTSTRAP_FILE_IDENTITY_FAILED");
  need(equivalent(std::wstring_view(final.data(), size), api(path)), "BOOTSTRAP_UNSAFE_FILE");
  return info;
}
Handle openDirectory(const Path &path) {
  Handle result(CreateFileW(api(path).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  win(bool(result), "BOOTSTRAP_DIRECTORY_UNAVAILABLE");
  exactInfo(result.value, path, true);
  return result;
}
std::vector<Handle> lockDirectories(Path path) {
  std::vector<Path> ancestors;
  for (;;) {
    ancestors.push_back(path);
    const auto parent = path.parent_path();
    if (parent == path) break;
    path = parent;
  }
  std::vector<Handle> held;
  for (auto item = ancestors.rbegin(); item != ancestors.rend(); ++item)
    held.push_back(openDirectory(*item));
  return held;
}
Handle readFile(const Path &path) {
  Handle opened(CreateFileW(api(path).c_str(), GENERIC_READ, FILE_SHARE_READ,
      nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  win(bool(opened), "BOOTSTRAP_INPUT_UNAVAILABLE");
  exactInfo(opened.value, path, false);
  return opened;
}
Handle newFile(const Path &path) {
  Handle created(CreateFileW(api(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  win(bool(created), "BOOTSTRAP_WRITE_FAILED");
  return created;
}
std::uint64_t length(HANDLE file) {
  LARGE_INTEGER size{};
  win(GetFileSizeEx(file, &size) && size.QuadPart >= 0, "BOOTSTRAP_INPUT_UNAVAILABLE");
  return std::uint64_t(size.QuadPart);
}
void write(HANDLE file, const void *bytes, DWORD size) {
  DWORD written = 0;
  win(WriteFile(file, bytes, size, &written, nullptr) && written == size, "BOOTSTRAP_WRITE_FAILED");
}
std::string bytes(const Path &path, DWORD limit) {
  const auto file = readFile(path);
  const auto size = length(file.value);
  need(size > 0 && size <= limit, "BOOTSTRAP_METADATA_SIZE");
  std::string data(size_t(size), '\0');
  DWORD read = 0;
  win(ReadFile(file.value, data.data(), DWORD(size), &read, nullptr) && read == size, "BOOTSTRAP_READ_FAILED");
  return data;
}

struct Sha256 {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE handle = nullptr;
  std::vector<unsigned char> data;
  Sha256() {
    need(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "BOOTSTRAP_HASH_FAILED");
    DWORD size = 0, read = 0;
    try {
      need(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size),
          sizeof(size), &read, 0) >= 0 && read == sizeof(size) && size > 0 && size <= 65536, "BOOTSTRAP_HASH_FAILED");
      data.resize(size);
      need(BCryptCreateHash(algorithm, &handle, data.data(), size, nullptr, 0, 0) >= 0, "BOOTSTRAP_HASH_FAILED");
    } catch (...) { BCryptCloseAlgorithmProvider(algorithm, 0); throw; }
  }
  ~Sha256() {
    if (handle) BCryptDestroyHash(handle);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
  }
  void add(const void *input, DWORD size) {
    need(BCryptHashData(handle, const_cast<PUCHAR>(static_cast<const unsigned char *>(input)), size, 0) >= 0,
        "BOOTSTRAP_HASH_FAILED");
  }
  std::string finish() {
    std::array<unsigned char, 32> result{};
    need(BCryptFinishHash(handle, result.data(), DWORD(result.size()), 0) >= 0, "BOOTSTRAP_HASH_FAILED");
    std::string digest;
    constexpr char hex[] = "0123456789abcdef";
    for (const auto b : result) { digest += hex[b >> 4]; digest += hex[b & 15]; }
    return digest;
  }
};
std::string hashFile(HANDLE input, HANDLE output = nullptr) {
  Sha256 sha256;
  std::array<unsigned char, 65536> block{};
  DWORD read = 0;
  for (;;) {
    win(ReadFile(input, block.data(), DWORD(block.size()), &read, nullptr) != FALSE, "BOOTSTRAP_READ_FAILED");
    if (!read) return sha256.finish();
    sha256.add(block.data(), read);
    if (output) write(output, block.data(), read);
  }
}

Json parse(std::string_view raw) {
  std::vector<std::set<std::string>> keys;
  return Json::parse(raw.begin(), raw.end(), [&](int depth, Json::parse_event_t event, Json &value) {
    need(depth <= 16, "BOOTSTRAP_METADATA_INVALID");
    if (event == Json::parse_event_t::object_start) keys.emplace_back();
    if (event == Json::parse_event_t::key)
      need(!keys.empty() && keys.back().insert(value.get<std::string>()).second, "BOOTSTRAP_METADATA_INVALID");
    if (event == Json::parse_event_t::object_end) keys.pop_back();
    return true;
  });
}
void keys(const Json &data, std::initializer_list<const char *> list) {
  need(data.is_object() && data.size() == list.size(), "BOOTSTRAP_METADATA_INVALID");
  for (const auto name : list) need(data.contains(name), "BOOTSTRAP_METADATA_INVALID");
}
bool safeAscii(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
      (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
}
Path kitRelative(const std::string &path) {
  need(path.starts_with("bin/") && path.size() > 4 && path.size() <= 512 &&
      path.back() != '/' && path.find("//") == path.npos, "BOOTSTRAP_PAYLOAD_PATH_INVALID");
  need(std::all_of(path.begin(), path.end(), [](char c) { return safeAscii(c) || c == '/' || c == ' '; }),
      "BOOTSTRAP_PAYLOAD_PATH_INVALID");
  Path relative(std::wstring(path.begin(), path.end()));
  for (const auto &part : relative) need(ordinaryComponent(part.native()), "BOOTSTRAP_PAYLOAD_PATH_INVALID");
  return relative.make_preferred();
}
struct KitFile { Path relative; std::uint64_t size; std::string digest; };
std::vector<KitFile> kitFiles(const std::string &data) {
  const auto manifest = parse(data);
  keys(manifest, {"schema", "protocol", "product", "platform", "files"});
  need(manifest.at("schema") == "gsw.native_bootstrap/1" && manifest.at("protocol").is_number_integer() &&
      manifest.at("protocol") == 2 && manifest.at("product") == product && manifest.at("platform") == "windows-x64",
      "BOOTSTRAP_MANIFEST_IDENTITY_INVALID");
  const auto &files = manifest.at("files");
  need(files.is_array() && !files.empty() && files.size() <= 256, "BOOTSTRAP_METADATA_SIZE");
  bool launcher = false, helper = false;
  std::uint64_t total = 0;
  std::set<std::string> names;
  std::vector<KitFile> result;
  for (const auto &file : files) {
    keys(file, {"path", "sha256", "size"});
    auto path = file.at("path").get<std::string>();
    auto relative = kitRelative(path);
    launcher |= path == launcherRelative;
    helper |= path == helperRelative;
    std::transform(path.begin(), path.end(), path.begin(), [](char c) { return char(c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c); });
    need(names.insert(path).second, "BOOTSTRAP_PAYLOAD_PATH_INVALID");
    auto digest = file.at("sha256").get<std::string>();
    need(sha(digest) && file.at("size").is_number_unsigned(), "BOOTSTRAP_METADATA_INVALID");
    const auto size = file.at("size").get<std::uint64_t>();
    need(size > 0 && size <= maxKitBytes - total, "BOOTSTRAP_PAYLOAD_SIZE");
    total += size;
    result.push_back({std::move(relative), size, std::move(digest)});
  }
  need(launcher && helper, "BOOTSTRAP_REQUIRED_FILE_MISSING");
  return result;
}

void requireFresh(const Path &path) {
  if (GetFileAttributesW(api(path).c_str()) != INVALID_FILE_ATTRIBUTES) fail("BOOTSTRAP_DESTINATION_OCCUPIED");
  win(GetLastError() == ERROR_FILE_NOT_FOUND, "BOOTSTRAP_DESTINATION_UNAVAILABLE");
}
std::wstring stageName() {
  GUID guid{};
  need(SUCCEEDED(CoCreateGuid(&guid)), "BOOTSTRAP_STAGING_FAILED");
  wchar_t id[40]{};
  need(StringFromGUID2(guid, id, 40) == 39, "BOOTSTRAP_STAGING_FAILED");
  return std::wstring(L".gsw-first-install-") + id;
}
struct LocalAllocation {
  HLOCAL value = nullptr;
  LocalAllocation() = default;
  explicit LocalAllocation(HLOCAL owned) : value(owned) {}
  LocalAllocation(const LocalAllocation &) = delete;
  LocalAllocation &operator=(const LocalAllocation &) = delete;
  ~LocalAllocation() { if (value) LocalFree(value); }
};
struct OwnedUserSecurity {
  LocalAllocation descriptorMemory;
  SECURITY_ATTRIBUTES attributes{};
  explicit OwnedUserSecurity(bool container, bool inheritable);
};

OwnedUserSecurity::OwnedUserSecurity(bool container, bool inheritable) {
  HANDLE rawToken = nullptr;
  win(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken) != FALSE, "BOOTSTRAP_OWNED_ACL_FAILED");
  const Handle token(rawToken);
  DWORD size = 0;
  GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
  need(size > 0 && size <= 65536, "BOOTSTRAP_OWNED_ACL_FAILED");
  std::vector<unsigned char> bytes(size);
  win(GetTokenInformation(token.value, TokenUser, bytes.data(), size, &size) != FALSE, "BOOTSTRAP_OWNED_ACL_FAILED");
  LPWSTR sid = nullptr;
  win(ConvertSidToStringSidW(reinterpret_cast<const TOKEN_USER *>(bytes.data())->User.Sid, &sid) != FALSE,
      "BOOTSTRAP_OWNED_ACL_FAILED");
  const LocalAllocation sidMemory{sid};
  // Apply only to new installer-owned objects, never to the caller's token,
  // the parent directory, an existing installation or any system object.
  const auto sddl = std::wstring(container ? L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;" : L"D:P(A;;GA;;;SY)(A;;GA;;;") + sid + L")";
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  win(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr) != FALSE,
      "BOOTSTRAP_OWNED_ACL_FAILED");
  descriptorMemory.value = descriptor;
  attributes = {sizeof(attributes), descriptor, inheritable};
}
void makePrivateStage(const Path &path) {
  // A shared parent must not make the staged trust journal or DLLs writable
  // by other ordinary accounts. The private DACL moves with the new root.
  OwnedUserSecurity access(true, false);
  win(CreateDirectoryW(api(path).c_str(), &access.attributes) != FALSE, "BOOTSTRAP_STAGING_FAILED");
}
struct Stage {
  const Path path;
  BY_HANDLE_FILE_INFORMATION identity{};
  bool owned = false;
  ~Stage() {
    if (!owned) return;
    try {
      {
        const auto held = openDirectory(path);
        const auto current = exactInfo(held.value, path, true);
        if (current.dwVolumeSerialNumber != identity.dwVolumeSerialNumber ||
            current.nFileIndexHigh != identity.nFileIndexHigh || current.nFileIndexLow != identity.nFileIndexLow) return;
        // Only descend our exact freshly created tree; never traverse a link.
        for (const auto &entry : std::filesystem::recursive_directory_iterator(Path(api(path)))) {
          const auto flags = GetFileAttributesW(entry.path().c_str());
          if (flags == INVALID_FILE_ATTRIBUTES || flags & FILE_ATTRIBUTE_REPARSE_POINT) return;
        }
      }
      std::filesystem::remove_all(Path(api(path)));
    } catch (...) {} // A failed cleanup is never permission to remove other data.
  }
};
void stageKit(const Path &source, const Path &stage, const std::vector<KitFile> &files, const std::string &manifest) {
  auto targetManifest = newFile(stage / L"bootstrap.json");
  write(targetManifest.value, manifest.data(), DWORD(manifest.size()));
  win(FlushFileBuffers(targetManifest.value) != FALSE, "BOOTSTRAP_WRITE_FAILED");
  for (const auto &file : files) {
    const auto inputPath = source / file.relative;
    const auto directories = lockDirectories(inputPath.parent_path());
    const auto input = readFile(inputPath);
    need(length(input.value) == file.size, "BOOTSTRAP_PAYLOAD_MISMATCH");
    const auto target = stage / file.relative;
    std::filesystem::create_directories(Path(api(target.parent_path())));
    const auto output = newFile(target);
    need(hashFile(input.value, output.value) == file.digest, "BOOTSTRAP_PAYLOAD_MISMATCH");
    win(FlushFileBuffers(output.value) != FALSE, "BOOTSTRAP_WRITE_FAILED");
  }
}

std::vector<wchar_t> childEnvironment(const Path &bin) {
  std::array<wchar_t, 32768> buffer{};
  const DWORD size = GetWindowsDirectoryW(buffer.data(), DWORD(buffer.size()));
  win(size > 0 && size < buffer.size(), "BOOTSTRAP_ENVIRONMENT_FAILED");
  const std::wstring windows(buffer.data(), size);
  // Do not inherit developer tools, Python, Qt plugin overrides or credentials.
  std::map<std::wstring, std::wstring> values{
    {L"PATH", bin.native() + L";" + windows + L"\\System32"},
    {L"SYSTEMROOT", windows}, {L"WINDIR", windows}};
  for (const auto name : {L"APPDATA", L"LOCALAPPDATA", L"TEMP", L"TMP", L"USERPROFILE"}) {
    const auto count = GetEnvironmentVariableW(name, buffer.data(), DWORD(buffer.size()));
    need(count < buffer.size(), "BOOTSTRAP_ENVIRONMENT_FAILED");
    if (count) values.emplace(name, std::wstring(buffer.data(), count));
  }
  std::vector<wchar_t> result;
  for (const auto &[key, value] : values) {
    const auto entry = key + L"=" + value;
    result.insert(result.end(), entry.begin(), entry.end());
    result.push_back(L'\0');
  }
  result.push_back(L'\0');
  return result;
}
struct RunningHelper {
  Handle process;
  bool finished = false;
  ~RunningHelper() {
    if (process && !finished) {
      TerminateProcess(process.value, 2); // Only the owned headless child.
      WaitForSingleObject(process.value, 5000);
    }
  }
};
Json installSignedSlot(const Path &program, DWORD &exit) {
  // Execute only the exact copied helper, not a command resolved by PATH.
  const auto executableLease = readFile(program);
  auto environment = childEnvironment(program.parent_path());
  // The client's token can carry a default DACL without generic write. Give
  // only this user/SYSTEM access to each new anonymous pipe. Supply EOF on a
  // private stdin rather than relying on opening a shared device under that
  // token. Only the three named standard-I/O handles may reach the Helper.
  OwnedUserSecurity pipeAccess(false, true);
  const auto pipe = [&](const char *code) {
    HANDLE read = nullptr, write = nullptr;
    win(CreatePipe(&read, &write, &pipeAccess.attributes, 0) != FALSE, code);
    return std::pair(Handle(read), Handle(write));
  };
  auto [output, childOutput] = pipe("BOOTSTRAP_HELPER_STDOUT_PIPE_FAILED");
  auto [errors, childError] = pipe("BOOTSTRAP_HELPER_STDERR_PIPE_FAILED");
  auto [childInput, emptyInput] = pipe("BOOTSTRAP_HELPER_STDIN_PIPE_FAILED");
  emptyInput.close();
  win(SetHandleInformation(output.value, HANDLE_FLAG_INHERIT, 0) &&
      SetHandleInformation(errors.value, HANDLE_FLAG_INHERIT, 0), "BOOTSTRAP_HELPER_HANDLE_FAILED");
  SIZE_T size = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
  need(size > 0 && size < 65536, "BOOTSTRAP_HELPER_HANDLE_FAILED");
  std::vector<unsigned char> buffer(size);
  const auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
  win(InitializeProcThreadAttributeList(list, 1, 0, &size) != FALSE, "BOOTSTRAP_HELPER_HANDLE_FAILED");
  struct Cleanup { LPPROC_THREAD_ATTRIBUTE_LIST list; ~Cleanup() { DeleteProcThreadAttributeList(list); } } cleanup{list};
  HANDLE inherited[]{childInput.value, childOutput.value, childError.value};
  win(UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
      inherited, sizeof(inherited), nullptr, nullptr) != FALSE, "BOOTSTRAP_HELPER_HANDLE_FAILED");
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = childInput.value;
  startup.StartupInfo.hStdOutput = childOutput.value;
  startup.StartupInfo.hStdError = childError.value;
  startup.lpAttributeList = list;
  // Path validation forbids quotes. Fixed arguments take no external input.
  std::wstring command = L"\"" + program.native() + L"\" --install stable";
  OwnedUserSecurity processAccess(false, false);
  PROCESS_INFORMATION info{};
  win(CreateProcessW(program.c_str(), command.data(), &processAccess.attributes, &processAccess.attributes, TRUE,
      DETACHED_PROCESS | EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
      environment.data(), program.parent_path().c_str(), &startup.StartupInfo, &info) != FALSE,
      "BOOTSTRAP_HELPER_START_FAILED");
  RunningHelper helper{Handle(info.hProcess)};
  Handle thread(info.hThread);
  childInput.close(); childOutput.close(); childError.close();
  std::string outputBytes;
  size_t errorBytes = 0;
  const auto start = GetTickCount64();
  const auto drain = [](HANDLE stream, size_t &total, size_t limit, std::string *capture) {
    // Bound each turn as well as the total; a noisy stream must not starve
    // the other stream or prevent checking the child deadline.
    for (int turn = 0; turn < 16; ++turn) {
      DWORD available = 0;
      if (!PeekNamedPipe(stream, nullptr, 0, nullptr, &available, nullptr)) {
        win(GetLastError() == ERROR_BROKEN_PIPE, "BOOTSTRAP_HELPER_READ_FAILED"); return false;
      }
      if (!available) return false;
      std::array<char, 4096> block{};
      DWORD read = 0;
      win(ReadFile(stream, block.data(), std::min<DWORD>(available, DWORD(block.size())), &read, nullptr) && read,
          "BOOTSTRAP_HELPER_READ_FAILED");
      total += read;
      need(total <= limit, "BOOTSTRAP_HELPER_OUTPUT_LIMIT");
      if (capture) capture->append(block.data(), read);
    }
    return true;
  };
  size_t outputTotal = 0;
  for (;;) {
    const bool moreOutput = drain(output.value, outputTotal, 8 * 1024 * 1024, &outputBytes);
    const bool moreErrors = drain(errors.value, errorBytes, 1024 * 1024, nullptr);
    if (helper.finished && !moreOutput && !moreErrors) break;
    if (WaitForSingleObject(helper.process.value, 20) == WAIT_OBJECT_0) helper.finished = true;
    else need(GetTickCount64() - start < helperBudgetMs, "BOOTSTRAP_HELPER_DEADLINE");
  }
  win(GetExitCodeProcess(helper.process.value, &exit) != FALSE, "BOOTSTRAP_HELPER_READ_FAILED");
  // Earlier progress messages aren't an admission. Require the actual last
  // complete marker; do not echo output, URLs, or untrusted text from it.
  while (!outputBytes.empty() && (outputBytes.back() == '\r' || outputBytes.back() == '\n')) outputBytes.pop_back();
  const auto begin = outputBytes.rfind('\n');
  const auto tail = std::string_view(outputBytes).substr(begin == outputBytes.npos ? 0 : begin + 1);
  if (tail.empty() || tail.size() > 16384) return Json::object();
  try { return parse(tail); } catch (...) { return Json::object(); }
}
bool buildId(std::string_view id) {
  return !id.empty() && id.size() <= 96 && id.back() != '.' &&
      ((id.front() >= 'A' && id.front() <= 'Z') || (id.front() >= 'a' && id.front() <= 'z') ||
       (id.front() >= '0' && id.front() <= '9')) &&
      std::all_of(id.begin(), id.end(), safeAscii) &&
      ordinaryComponent(std::wstring(id.begin(), id.end()));
}
} // namespace

Result install(const std::wstring &destination) {
  Result result;
  try {
    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    need(system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64, "BOOTSTRAP_PLATFORM_UNSUPPORTED");
    need(sha(manifestPin), "BOOTSTRAP_NOT_CONFIGURED");
    const Path target(local(destination));
    const auto parent = target.parent_path();
    need(parent != target, "BOOTSTRAP_LOCAL_PATH_REQUIRED");
    const auto parentLease = lockDirectories(parent);
    requireFresh(target);
    const auto source = moduleDirectory() / L"bootstrap";
    need(!under(source.native(), target.native()), "BOOTSTRAP_LOCAL_PATH_REQUIRED");
    const auto sourceLease = lockDirectories(source);
    const auto raw = bytes(source / L"bootstrap.json", 16384);
    Sha256 pin;
    pin.add(raw.data(), DWORD(raw.size()));
    need(pin.finish() == manifestPin, "BOOTSTRAP_MANIFEST_MISMATCH");
    const auto files = kitFiles(raw);
    Stage stage{parent / stageName()};
    makePrivateStage(stage.path);
    Json activated;
    DWORD exit = 1;
    {
      const auto opened = openDirectory(stage.path);
      stage.identity = exactInfo(opened.value, stage.path, true);
      stage.owned = true;
      stageKit(source, stage.path, files, raw);
      // Keep the staged manifest and exact DLL/EXE bytes read-only while
      // executing the fixed Helper. The location is not the public root yet.
      std::vector<Handle> verifiedKit;
      verifiedKit.push_back(readFile(stage.path / L"bootstrap.json"));
      for (const auto &file : files) {
        auto onDisk = readFile(stage.path / file.relative);
        need(length(onDisk.value) == file.size && hashFile(onDisk.value) == file.digest, "BOOTSTRAP_PAYLOAD_MISMATCH");
        verifiedKit.push_back(std::move(onDisk));
      }
      activated = installSignedSlot(stage.path / kitRelative(helperRelative), exit);
      result.helperExitCode = exit;
    }
    if (activated.is_object()) {
      const auto code = activated.value("code", std::string());
      if (code.size() <= 96 && std::all_of(code.begin(), code.end(), [](char c) {
            return safeAscii(c) && c != '.' && c != '-'; })) result.helperCode = code;
#ifdef GSW_FIRST_INSTALL_FIXTURE
      const auto trace = activated.value("fixtureDownloadTrace", Json::object());
      if (trace.is_object() && trace.contains("qtError") && trace.contains("httpStatus") && trace.contains("bytes") &&
          trace.contains("sslSupported") && trace.contains("tlsBackend") && trace["qtError"].is_number_unsigned() &&
          trace["httpStatus"].is_number_unsigned() && trace["bytes"].is_number_unsigned() &&
          trace["sslSupported"].is_boolean() && trace["tlsBackend"].is_string()) {
        const auto error = trace["qtError"].get<std::uint64_t>();
        const auto status = trace["httpStatus"].get<std::uint64_t>();
        const auto received = trace["bytes"].get<std::uint64_t>();
        const auto backend = trace["tlsBackend"].get<std::string>();
        if (error <= 999 && status <= 599 && received <= 4ull * 1024 * 1024 * 1024 &&
            (backend == "schannel" || backend == "openssl" || backend == "cert-only" ||
             backend == "securetransport" || backend == "other")) {
          result.helperDownloadTrace = true;
          result.helperQtNetworkError = unsigned(error);
          result.helperHttpStatus = unsigned(status);
          result.helperReceivedBytes = received;
          result.helperSslSupported = trace["sslSupported"].get<bool>();
          result.helperTlsBackend = backend;
        }
      }
#endif
    }
    need(exit == 0 && activated.is_object() && activated.value("schema", "") == "gsw.update_event/1" &&
        activated.value("phase", "") == "activated" && activated.value("code", "") == "NEXT_LAUNCH",
        "BOOTSTRAP_HELPER_FAILED");
    const auto pointer = parse(bytes(stage.path / L"current.json", 16384));
    keys(pointer, {"schema", "version", "executableSha256"});
    result.buildId = pointer.at("version").get<std::string>();
    result.executableSha256 = pointer.at("executableSha256").get<std::string>();
    need(pointer.at("schema") == "gsw.native_active_version/1" && buildId(result.buildId) &&
        pointer.at("version") == activated.at("buildId") && sha(result.executableSha256),
        "BOOTSTRAP_INSTALLED_IDENTITY_INVALID");
    const std::wstring build(result.buildId.begin(), result.buildId.end());
    {
      const auto exe = readFile(stage.path / L"versions" / build / L"bin/Gaussian Scene Workbench.exe");
      need(hashFile(exe.value) == result.executableSha256, "BOOTSTRAP_INSTALLED_IDENTITY_INVALID");
    }
    const auto receipt = parse(bytes(stage.path / L"updates/receipts" / (build + L".json"), 4 * 1024 * 1024));
    need(receipt.value("schema", "") == "gsw.native_activation_receipt/1" && receipt.value("mode", "") == "online" &&
        receipt.at("executableSha256") == pointer.at("executableSha256") &&
        receipt.at("offer").at("buildId") == pointer.at("version") && receipt.at("offer").at("product") == product &&
        sha(receipt.at("descriptorSha256").get<std::string>()), "BOOTSTRAP_INSTALLED_IDENTITY_INVALID");
    {
      const auto anchor = readFile(stage.path / L".update-trust-anchor");
      need(length(anchor.value) > 0, "BOOTSTRAP_INSTALLED_IDENTITY_INVALID");
    }
    // All installed-file handles are closed before renaming the root.
    // The tested stable entry re-verifies all trust and slot bytes at use.
    requireFresh(target);
    result.code = "BOOTSTRAP_COMMITTED";
    // Same-parent move, no replacement or cross-volume copy fallback.
    win(MoveFileExW(api(stage.path).c_str(), api(target).c_str(), MOVEFILE_WRITE_THROUGH) != FALSE,
        "BOOTSTRAP_COMMIT_FAILED");
    stage.owned = false;
    result.committed = true;
  } catch (const Failure &failure) { result.code = failure.code; result.systemCode = failure.osError; }
    catch (const Json::exception &) { result.code = "BOOTSTRAP_METADATA_INVALID"; }
    catch (const std::filesystem::filesystem_error &failure) {
      result.code = "BOOTSTRAP_FILE_OPERATION_FAILED"; result.systemCode = DWORD(failure.code().value());
    } catch (...) { result.code = "BOOTSTRAP_FAILED"; }
  return result;
}
} // namespace gsw::firstinstall
