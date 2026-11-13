// Fetch the bundled Virtual Display Driver (MIT) and nefcon (MIT) into resources/vdd, for the Windows build.
//
// WHY A SCRIPT (not committed files)
//   The repo never commits binaries (see .gitignore: *.exe). The driver's own files (inf/dll/cat) are small and
//   are committed; the two nefcon executables are fetched here, exactly as the native helpers are built fresh.
//   Running this is idempotent: files already present with the right size are kept, so a rebuild does not re-download.
//
//   node scripts/fetch-vdd.mjs            # fetch what is missing
//   node scripts/fetch-vdd.mjs --force    # re-download everything
//
// Versions are pinned so a release is reproducible; bump them together with resources/vdd/README.md and NOTICE.

import { createWriteStream, existsSync, mkdirSync, rmSync, statSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { tmpdir } from "node:os";
import { execFileSync } from "node:child_process";
import { get as httpsGet } from "node:https";

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, "..");
const vdd = join(root, "resources", "vdd");

// Pinned sources. The driver "driver only" asset is used on purpose: the project's installer EXE hangs headless.
const VDD_TAG = "25.7.23";
const NEFCON_TAG = "v1.14.0";
const SOURCES = [
  {
    what: "Virtual Display Driver (driver only, x64)",
    tag: VDD_TAG,
    url: `https://github.com/VirtualDrivers/Virtual-Display-Driver/releases/download/${VDD_TAG}/VirtualDisplayDriver-x86.Driver.Only.zip`,
    // files this zip must provide (checked after extraction)
    expect: ["VirtualDisplayDriver/MttVDD.inf", "VirtualDisplayDriver/MttVDD.dll", "VirtualDisplayDriver/mttvdd.cat", "VirtualDisplayDriver/vdd_settings.xml"],
    // where each extracted file lands, relative to resources/vdd
    map: { "VirtualDisplayDriver/MttVDD.inf": "driver/MttVDD.inf", "VirtualDisplayDriver/MttVDD.dll": "driver/MttVDD.dll", "VirtualDisplayDriver/mttvdd.cat": "driver/mttvdd.cat", "VirtualDisplayDriver/vdd_settings.xml": "driver/vdd_settings.xml" },
    needs: ["driver/MttVDD.inf", "driver/MttVDD.dll", "driver/mttvdd.cat", "driver/vdd_settings.xml"],
  },
  {
    what: "nefcon (device-driver installer, x64)",
    tag: NEFCON_TAG,
    url: `https://github.com/nefarius/nefcon/releases/download/${NEFCON_TAG}/nefcon_v1.14.0.zip`,
    expect: ["x64/nefconw.exe", "x64/nefconc.exe"],
    map: { "x64/nefconw.exe": "nefcon/nefconw.exe", "x64/nefconc.exe": "nefcon/nefconc.exe" },
    needs: ["nefcon/nefconw.exe", "nefcon/nefconc.exe"],
  },
];

const force = process.argv.includes("--force");
const log = (m) => console.log(`  • fetch-vdd: ${m}`);

function download(url, dest) {
  return new Promise((resolve, reject) => {
    httpsGet(url, (res) => {
      if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
        res.resume();
        return resolve(download(new URL(res.headers.location, url).href, dest));   // follow redirects (GitHub -> CDN)
      }
      if (res.statusCode !== 200) { res.resume(); return reject(new Error(`HTTP ${res.statusCode} for ${url}`)); }
      const out = createWriteStream(dest);
      res.pipe(out);
      out.on("finish", () => out.close(() => resolve()));
      out.on("error", reject);
    }).on("error", reject);
  });
}

// Expand a zip with .NET's ZipFile (works on Windows PowerShell 5.1 and PowerShell 7; no module to load and no
// -Force overload differences to trip over). Using forward slashes keeps the entries intact on Windows.
function expand(zip, dir) {
  const z = zip.replace(/\\/g, "/");
  const d = dir.replace(/\\/g, "/");
  execFileSync("powershell.exe", ["-NoProfile", "-NonInteractive", "-Command",
    `Add-Type -AssemblyName System.IO.Compression.FileSystem; [System.IO.Compression.ZipFile]::ExtractToDirectory("${z}", "${d}")`], { stdio: "inherit" });
}

let failed = false;
for (const src of SOURCES) {
  const missing = src.needs.filter((p) => !existsSync(join(vdd, p)));
  if (!missing.length && !force) { log(`${src.what}: up to date`); continue; }
  log(`${src.what}: fetching ${src.tag}…`);
  const tmp = join(tmpdir(), `td-vdd-${Date.now()}-${Math.random().toString(36).slice(2)}`);
  const zip = join(tmp, "pkg.zip");
  try {
    mkdirSync(tmp, { recursive: true });
    await download(src.url, zip);
    expand(zip, tmp);
    for (const [from, to] of Object.entries(src.map)) {
      const abs = join(tmp, from);
      if (!existsSync(abs)) throw new Error(`zip is missing ${from}`);
      const dest = join(vdd, to);
      mkdirSync(dirname(dest), { recursive: true });
      execFileSync("powershell.exe", ["-NoProfile", "-NonInteractive", "-Command", `Copy-Item -LiteralPath "${abs}" -Destination "${dest}" -Force`]);
    }
    // Verify the two files that matter most, so a truncated download fails here and not at install time.
    for (const p of src.needs) {
      const f = join(vdd, p);
      if (!existsSync(f) || statSync(f).size < 1024) throw new Error(`${p} looks wrong (${existsSync(f) ? statSync(f).size : 0} bytes)`);
    }
    log(`${src.what}: ok`);
  } catch (e) {
    failed = true;
    console.error(`  • fetch-vdd: ${src.what} FAILED: ${e.message}`);
  } finally {
    rmSync(tmp, { recursive: true, force: true });
  }
}

if (failed) {
  console.error("  • fetch-vdd: some files could not be fetched. The Windows build needs them; re-run with network access.");
  process.exit(1);
}
log("done");
