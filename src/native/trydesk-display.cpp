// trydesk-display.exe — the virtual-monitor helper for TryDesk's host side (Windows).
//
// WHY THIS EXISTS
//   When no monitor is active, the GPU driver stops drawing the desktop, so DXGI Desktop Duplication
//   (src/native/trydesk-capture.cpp) gets no frames: the viewer sees a frozen (or black) picture. The fix
//   used by TeamViewer and friends is to give Windows a *virtual monitor*, which only a display driver can
//   do. TryDesk bundles one: the open-source, MIT-licensed Virtual Display Driver (IddCx), under
//   resources/vdd/driver, and installs it with nefcon (resources/vdd/nefcon, also MIT).
//
// WHAT IT DOES
//   status  — is the virtual display present? Prints JSON to stdout. Never needs admin.
//   install — trust the driver's catalog certificate (LocalMachine\TrustedPublisher) and install the device
//             node + driver via nefcon. Idempotent (--no-duplicates). Needs administrator.
//   remove  — remove the virtual display device node. Needs administrator.
//
//   Every action prints ONE JSON line to stdout, e.g. {"ok":true,"action":"install"} or
//   {"ok":false,"action":"install","error":"...","code":N}; the exit code is 0 on success, non-zero on
//   failure. main.js reads that line. All human-readable logging goes to stderr, so stdout stays parseable.
//
// USAGE
//   trydesk-display.exe status  [--dir <resources\vdd>]
//   trydesk-display.exe install [--dir <resources\vdd>]
//   trydesk-display.exe remove  [--dir <resources\vdd>]
//   --dir defaults to the folder next to this exe (resources\vdd); main.js passes the packaged path.
//
// Exit codes: 0 ok, 1 bad usage, 2 files missing, 3 needs admin, 4 nefcon failed, 5 certificate failed.
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")   // AllocateAndInitializeSid / CheckTokenMembership / FreeSid

// ─── small helpers ───────────────────────────────────────────────────────────────────────────────────

static std::string g_action = "unknown";

// Print the single JSON line main.js parses, then exit.
[[noreturn]] static void done(bool ok, int code, const char* error = nullptr) {
  std::string j = "{\"ok\":";
  j += ok ? "true" : "false";
  j += ",\"action\":\"" + g_action + "\"";
  if (error && *error) {
    // Escape the few characters that would break the JSON string.
    std::string e;
    for (const char* p = error; *p; ++p) {
      if (*p == '"' || *p == '\\') { e += '\\'; e += *p; }
      else if (*p == '\n' || *p == '\r') e += ' ';
      else e += *p;
    }
    j += ",\"error\":\"" + e + "\"";
  }
  j += ",\"code\":" + std::to_string(code) + "}\n";
  fputs(j.c_str(), stdout);
  fflush(stdout);
  ExitProcess(ok ? 0 : code);
}

static void say(const char* fmt, ...) {   // stderr only: never touches the JSON on stdout
  va_list a; va_start(a, fmt);
  vfprintf(stderr, fmt, a); fputc('\n', stderr);
  va_end(a);
}

static bool isAdmin() {
  BOOL admin = FALSE;
  PSID grp = nullptr; SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
  if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &grp)) {
    CheckTokenMembership(nullptr, grp, &admin);
    FreeSid(grp);
  }
  return admin != FALSE;
}

static std::string exeDir() {
  wchar_t buf[MAX_PATH] = L""; DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  std::string s; if (n) { char u8[MAX_PATH * 3] = ""; WideCharToMultiByte(CP_UTF8, 0, buf, -1, u8, sizeof u8, nullptr, nullptr); s = u8; }
  size_t p = s.find_last_of("\\/"); return p == std::string::npos ? "." : s.substr(0, p);
}

static bool fileExists(const std::string& p) {
  DWORD a = GetFileAttributesA(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// Run a command (already-quoted full command line) and wait for it. Fills `out` with stdout+stderr.
static bool runCapture(const std::string& cmdline, DWORD& exitCode, std::string& out) {
  SECURITY_ATTRIBUTES sa = { sizeof sa, nullptr, TRUE };
  HANDLE rd = nullptr, wr = nullptr;
  if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
  SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA si = {}; si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = nullptr; si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi = {};

  std::vector<char> buf(cmdline.begin(), cmdline.end()); buf.push_back('\0');
  if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    CloseHandle(rd); CloseHandle(wr); return false;
  }
  CloseHandle(wr);                              // our copy; the child keeps its own
  char chunk[4096]; DWORD got = 0;
  while (ReadFile(rd, chunk, sizeof chunk, &got, nullptr) && got) out.append(chunk, got);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code); exitCode = code;
  CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(rd);
  return true;
}

// Merge a child's stdout+stderr into one short, single-line message for the JSON error field.
static std::string firstLines(const std::string& s, size_t max = 300) {
  std::string t;
  for (char c : s) { t += (c == '\r') ? ' ' : c; if (t.size() >= max) break; }
  while (!t.empty() && (t.back() == '\n' || t.back() == ' ')) t.pop_back();
  return t;
}

// ─── paths ───────────────────────────────────────────────────────────────────────────────────────────

struct Paths {
  std::string vddDir;      // ...\resources\vdd
  std::string infFile;     // driver\MttVDD.inf
  std::string catFile;     // driver\mttvdd.cat
  std::string nefconW;     // nefcon\nefconw.exe
  bool all() { return fileExists(infFile) && fileExists(catFile) && fileExists(nefconW); }
};

static Paths resolvePaths(const std::string& dirArg) {
  std::string d = dirArg.empty() ? (exeDir() + "\\..\\..\\resources\\vdd") : dirArg;   // src\native\..\..\resources\vdd
  Paths p;
  p.vddDir = d;
  p.infFile = d + "\\driver\\MttVDD.inf";
  p.catFile = d + "\\driver\\mttvdd.cat";
  p.nefconW = d + "\\nefcon\\nefconw.exe";
  return p;
}

static std::string quote(const std::string& s) { return "\"" + s + "\""; }

// ─── status ──────────────────────────────────────────────────────────────────────────────────────────
// Ask Windows whether a display device whose hardware id contains our root id is present. We look at the
// DISPLAY class (4D36E968-...) through SetupAPI so we never depend on the driver's own tools.
#include <setupapi.h>
#include <cfgmgr32.h>
#pragma comment(lib, "setupapi.lib")

static bool virtualDisplayPresent() {
  // Enumerate present display devices; a root-enumerated VDD shows up with a device instance id like
  // ROOT\MttVDD\.... Match on "MttVDD" to stay independent of the exact root id the driver uses.
  HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"Display", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
  if (set == INVALID_HANDLE_VALUE) return false;
  SP_DEVINFO_DATA info = {}; info.cbSize = sizeof info;
  bool found = false; wchar_t id[512];
  for (DWORD i = 0; !found && SetupDiEnumDeviceInfo(set, i, &info); i++) {
    if (SetupDiGetDeviceInstanceIdW(set, &info, id, 512, nullptr)) {
      if (wcsstr(id, L"MttVDD")) found = true;
    }
  }
  SetupDiDestroyDeviceInfoList(set);
  return found;
}

// ─── install ─────────────────────────────────────────────────────────────────────────────────────────

// Trust the driver package's catalog certificate on this machine. nefcon refuses an untrusted catalog with
// "the publisher ... has not yet been established as trusted", so this must come first. Uses PowerShell's
// Import-Certificate on the .cat (a catalog is a PKCS#7 that X509Certificate2Collection can read).
static bool trustCertificate(const Paths& p, std::string& err) {
  std::string ps =
    "$ErrorActionPreference='Stop';"
    "$certs=New-Object System.Security.Cryptography.X509Certificates.X509Certificate2Collection;"
    "$certs.Import([System.IO.File]::ReadAllBytes(" + quote(p.catFile) + "));"
    "foreach($c in $certs){"
      "$f=Join-Path $env:TEMP ($c.Thumbprint+'.cer');"
      "[System.IO.File]::WriteAllBytes($f,$c.Export('Cert'));"
      "Import-Certificate -FilePath $f -CertStoreLocation 'Cert:\\LocalMachine\\TrustedPublisher' | Out-Null;"
      "Remove-Item $f -Force -ErrorAction SilentlyContinue }";
  std::string cmd = "powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command " + quote(ps);
  DWORD code = 1; std::string out;
  if (!runCapture(cmd, code, out)) { err = "could not start powershell"; return false; }
  if (code != 0) { err = "certificate import failed: " + firstLines(out); return false; }
  return true;
}

static bool installDriver(const Paths& p, std::string& err) {
  // devcon-compatible verb: creates the ROOT device node and installs the INF in one go. --no-duplicates
  // keeps it idempotent (a re-run updates the driver instead of adding a second monitor); it is also what
  // makes an update over an existing install safe.
  std::string cmd = quote(p.nefconW) + " install " + quote(p.infFile) + " \"root\\MttVDD\" --no-duplicates";
  DWORD code = 1; std::string out;
  if (!runCapture(cmd, code, out)) { err = "could not start nefcon"; return false; }
  // 0 = success, 3010 (ERROR_SUCCESS_REBOOT_REQUIRED) = success, reboot pending. Both are fine here.
  if (code != 0 && code != 3010) { err = "nefcon install failed (" + std::to_string(code) + "): " + firstLines(out); return false; }
  say("nefcon install ok (exit %lu)", (unsigned long)code);
  return true;
}

static bool removeDriver(const Paths& p, std::string& err) {
  // Remove every present device matching the hardware id; the driver package stays in the store (harmless,
  // and it means a later install is instant). Absent already counts as success.
  std::string cmd = quote(p.nefconW) + " remove \"root\\MttVDD\"";
  DWORD code = 1; std::string out;
  if (!runCapture(cmd, code, out)) { err = "could not start nefcon"; return false; }
  if (code != 0 && code != 3010) { err = "nefcon remove failed (" + std::to_string(code) + "): " + firstLines(out); return false; }
  return true;
}

// ─── main ────────────────────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
  std::string dir;
  // argv[1] is the action; --dir may follow.
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--dir" && i + 1 < argc) { dir = argv[++i]; continue; }
    if (g_action == "unknown" && !a.empty() && a[0] != '-') { g_action = a; continue; }
  }

  Paths p = resolvePaths(dir);

  if (g_action == "status") {
    // Never needs admin and must always answer, even before anything is installed.
    bool present = virtualDisplayPresent();
    fprintf(stdout, "{\"ok\":true,\"action\":\"status\",\"installed\":%s,\"files\":%s}\n",
            present ? "true" : "false", p.all() ? "true" : "false");
    fflush(stdout);
    return 0;
  }

  if (g_action != "install" && g_action != "remove") {
    say("usage: trydesk-display.exe status | install | remove [--dir <resources\\vdd>]");
    done(false, 1, "unknown action");
  }

  if (!p.all()) {
    // A missing file is a packaging problem, not a user one: report which one.
    std::string missing = !fileExists(p.infFile) ? p.infFile : !fileExists(p.catFile) ? p.catFile : p.nefconW;
    say("missing file: %s", missing.c_str());
    done(false, 2, ("missing file: " + missing).c_str());
  }

  if (!isAdmin()) {
    // main.js runs this elevated (it can print a UAC prompt first); if we got here, elevate failed.
    say("administrator privileges required");
    done(false, 3, "administrator privileges required");
  }

  std::string err;
  if (g_action == "install") {
    if (!trustCertificate(p, err)) { say("%s", err.c_str()); done(false, 5, err.c_str()); }
    if (!installDriver(p, err)) { say("%s", err.c_str()); done(false, 4, err.c_str()); }
    // Give Windows a moment to bring the new display up, so a caller that follows immediately (start a
    // session) sees it. Not required for correctness.
    Sleep(1500);
    done(true, 0);
  }

  // remove
  if (!removeDriver(p, err)) { say("%s", err.c_str()); done(false, 4, err.c_str()); }
  done(true, 0);
}
