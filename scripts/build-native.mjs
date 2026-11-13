// Build the Windows native helpers (src/native/*.exe) before packaging.
//
//   trydesk-capture.exe  — the DXGI capture/encode helper (host + viewer)
//   trydesk-service.exe  — the Windows service (unattended access)
//   trydesk-display.exe  — installs/removes the bundled Virtual Display Driver
//
// They are compiled with the MSVC toolchain through scripts/build-capture.cmd. That only exists on Windows, and
// the .exe files are never committed (see .gitignore), so:
//   • Windows  -> build them now (packaging needs them);
//   • other OS -> skip with a note (Linux/macOS packaging does not use them; a Windows build must run on Windows).
//
// CI and local Windows builds get them automatically via `npm run build` (its "prebuild" script).

import { execFileSync } from "node:child_process";
import { existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, "..");

if (process.platform !== "win32") {
  console.log("  • build-native: not Windows, skipping the Windows helpers");
  process.exit(0);
}

const cmd = join(here, "build-capture.cmd");
if (!existsSync(cmd)) {
  console.error("  • build-native: scripts/build-capture.cmd is missing");
  process.exit(1);
}

try {
  execFileSync("cmd.exe", ["/c", cmd], { stdio: "inherit", cwd: root });
} catch (e) {
  console.error("  • build-native: native build failed. The Windows build needs Visual Studio with " +
    "\"Desktop development with C++\" (see scripts/build-capture.cmd).");
  process.exit(1);
}
