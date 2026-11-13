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
//   status  — is the virtual display present, and does its config file exist? Prints JSON. Never needs admin.
//   install — trust the driver's catalog certificate (LocalMachine\TrustedPublisher), install the device node +
//             driver via nefcon, and drop our vdd_settings.xml (60 Hz) into C:\VirtualDisplayDriver so the display
//             does not sit at the driver's 30 Hz default. Idempotent (--no-duplicates). Needs administrator.
//   settings— only (re)deploy vdd_settings.xml and restart the device; for fixing a driver installed earlier.
//   remove  — remove the virtual display device node. Needs administrator.
//
//   Every action prints ONE JSON line to stdout, e.g. {"ok":true,"action":"install"} or
//   {"ok":false,"action":"install","error":"...","code":N}; the exit code is 0 on success, non-zero on
//   failure. main.js reads that line. All human-readable logging goes to stderr, so stdout stays parseable.
//
// USAGE
//   trydesk-display.exe status   [--dir <resources\vdd>]
//   trydesk-display.exe install  [--dir <resources\vdd>]
//   trydesk-display.exe settings [--dir <resources\vdd>]
//   trydesk-display.exe remove   [--dir <resources\vdd>]
//   --dir defaults to the folder next to this exe (resources\vdd); main.js passes the packaged path.
//
// Exit codes: 0 ok, 1 bad usage, 2 files missing, 3 needs admin, 4 nefcon failed, 5 certificate failed,
//             6 settings deployment failed.
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
  std::string settings;    // driver\vdd_settings.xml
  bool all() { return fileExists(infFile) && fileExists(catFile) && fileExists(nefconW); }
};

static Paths resolvePaths(const std::string& dirArg) {
  std::string d = dirArg.empty() ? (exeDir() + "\\..\\..\\resources\\vdd") : dirArg;   // src\native\..\..\resources\vdd
  Paths p;
  p.vddDir = d;
  p.infFile = d + "\\driver\\MttVDD.inf";
  p.catFile = d + "\\driver\\mttvdd.cat";
  p.nefconW = d + "\\nefcon\\nefconw.exe";
  p.settings = d + "\\driver\\vdd_settings.xml";
  return p;
}

// The driver reads its settings from a FIXED folder, C:\VirtualDisplayDriver (confpath in the driver's
// source; overridable by the VDDPATH registry key), never from where the INF lives. If that file is absent
// the driver uses built-in defaults whose refresh rate is 30 Hz, and Desktop Duplication then delivers at
// most 30 frames a second: the remote looks choppy. So a correct install also drops our settings there.
static const char* kConfigDir = "C:\\VirtualDisplayDriver";

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

// Copy vdd_settings.xml into the driver's fixed config folder (C:\VirtualDisplayDriver) and point VDDPATH at
// it, so the driver reads our resolution/refresh list (60 Hz) instead of its 30 Hz built-in defaults. Without
// this the display refreshes at 30 Hz and the remote looks choppy even though TryDesk asks for 60 fps.
// Idempotent: an existing file is overwritten only when the content differs, so a re-run does not touch the
// device. Returns false (with `err`) only on a real failure; a missing bundled file is not fatal.
static bool deploySettings(const Paths& p, std::string& err) {
  if (!fileExists(p.settings)) { say("no vdd_settings.xml bundled; leaving the driver defaults"); return true; }
  std::string ps =
    "$ErrorActionPreference='Stop';"
    "$dir='C:\\VirtualDisplayDriver';"
    "New-Item -ItemType Directory -Force -Path $dir | Out-Null;"
    "$src=" + quote(p.settings) + ";"
    "$dst=Join-Path $dir 'vdd_settings.xml';"
    // Only rewrite (and, below, pause the device) when the content actually changed: a no-op re-run stays instant.
    "$same=$false;"
    "if (Test-Path $dst) { $same = ((Get-FileHash $dst).Hash -eq (Get-FileHash $src).Hash) };"
    "if (-not $same) {"
      "Copy-Item -LiteralPath $src -Destination $dst -Force;"
      // The driver loads the XML at start; disabling and re-enabling the device makes it re-read the file.
      "$dev = Get-PnpDevice -Class Display -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -like '*MttVDD*' -and $_.Status -eq 'OK' } | Select-Object -First 1;"
      "if ($dev) { Disable-PnpDevice -InstanceId $dev.InstanceId -Confirm:$false; Start-Sleep -Milliseconds 800; Enable-PnpDevice -InstanceId $dev.InstanceId -Confirm:$false }"
    "};"
    // Record the folder so the driver (and future tools) resolve it even if it moves.
    "New-Item -Path 'HKLM:\\SOFTWARE\\MikeTheTech\\VirtualDisplayDriver' -Force | Out-Null;"
    "New-ItemProperty -Path 'HKLM:\\SOFTWARE\\MikeTheTech\\VirtualDisplayDriver' -Name 'VDDPATH' -Value $dir -PropertyType String -Force | Out-Null";
  std::string cmd = "powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command " + quote(ps);
  DWORD code = 1; std::string out;
  if (!runCapture(cmd, code, out)) { err = "could not start powershell"; return false; }
  if (code != 0) { err = "vdd_settings.xml deployment failed: " + firstLines(out); return false; }
  say("vdd_settings.xml deployed to %s", kConfigDir);
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
    // Never needs admin and must always answer, even before anything is installed. "settings" tells whether the
    // driver's config folder already has our file: false means the display is likely stuck at its 30 Hz default.
    bool present = virtualDisplayPresent();
    bool cfg = fileExists(std::string(kConfigDir) + "\\vdd_settings.xml");
    fprintf(stdout, "{\"ok\":true,\"action\":\"status\",\"installed\":%s,\"settings\":%s,\"files\":%s}\n",
            present ? "true" : "false", cfg ? "true" : "false", p.all() ? "true" : "false");
    fflush(stdout);
    return 0;
  }

  if (g_action != "install" && g_action != "remove" && g_action != "settings") {
    say("usage: trydesk-display.exe status | install | remove | settings [--dir <resources\\vdd>]");
    done(false, 1, "unknown action");
  }

  if (g_action != "settings" && !p.all()) {
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
  // "settings" only fixes the config of an already-installed driver (e.g. one installed before our 60 Hz
  // file existed): no nefcon, no certificate, just the file and a device restart.
  if (g_action == "settings") {
    if (!deploySettings(p, err)) { say("%s", err.c_str()); done(false, 6, err.c_str()); }
    done(true, 0);
  }
  if (g_action == "install") {
    if (!trustCertificate(p, err)) { say("%s", err.c_str()); done(false, 5, err.c_str()); }
    if (!installDriver(p, err)) { say("%s", err.c_str()); done(false, 4, err.c_str()); }
    // Put our resolution/refresh settings where the driver reads them (60 Hz, not its 30 Hz defaults), then
    // let it re-read them. A failure here is reported but does not undo the install: the display still works,
    // just at the default rate.
    std::string cfgErr;
    if (!deploySettings(p, cfgErr)) say("warning: %s", cfgErr.c_str());
    // Give Windows a moment to bring the new display up, so a caller that follows immediately (start a
    // session) sees it. Not required for correctness.
    Sleep(1500);
    done(true, 0);
  }

  // remove
  if (!removeDriver(p, err)) { say("%s", err.c_str()); done(false, 4, err.c_str()); }
  done(true, 0);
}
