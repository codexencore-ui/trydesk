// Print one section of the root CHANGELOG.md (the desktop app's), so CI can use it as the body of a GitHub Release.
//
//   node scripts/changelog.mjs            # the section for the version in package.json
//   node scripts/changelog.mjs v1.3.8     # a leading "v" is fine
//   node scripts/changelog.mjs 1.3.8
//
// Same rules as mobile/changelog.mjs (see there for the notes): Keep a Changelog headings, [Unreleased] is never
// printed, and a missing or empty section exits non-zero so a release fails loudly instead of publishing nothing.
// The only difference is which file it reads: this one is the desktop app's changelog.

import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const normalize = (v) => String(v || "").trim().replace(/^v/i, "");

let version = normalize(process.argv[2]);
if (!version) {
  try {
    version = normalize(JSON.parse(readFileSync(join(here, "..", "package.json"), "utf8")).version);
  } catch (e) {
    console.error(`changelog: could not read the version from package.json (${e.message})`);
    process.exit(1);
  }
}
if (!version) {
  console.error("changelog: no version given and package.json has none");
  process.exit(1);
}

let text;
try {
  text = readFileSync(join(here, "..", "CHANGELOG.md"), "utf8");
} catch (e) {
  console.error(`changelog: could not read CHANGELOG.md (${e.message})`);
  process.exit(1);
}

const lines = text.split(/\r?\n/);
const startRe = new RegExp(`^##\\s+\\[v?${version.replace(/\./g, "\\.")}\\]`, "i");
const anyVersionRe = /^##\s+\[/;

let start = -1;
for (let i = 0; i < lines.length; i++) if (startRe.test(lines[i])) { start = i; break; }
if (start === -1) {
  console.error(`changelog: no "## [${version}]" section in CHANGELOG.md — add it before tagging`);
  process.exit(1);
}

let end = lines.length;
for (let i = start + 1; i < lines.length; i++) if (anyVersionRe.test(lines[i])) { end = i; break; }

const body = lines
  .slice(start + 1, end)
  .filter((l) => !/^\[[^\]]+\]:\s+https?:/.test(l))
  .join("\n")
  .trim();

if (!body) {
  console.error(`changelog: the "## [${version}]" section is empty`);
  process.exit(1);
}

process.stdout.write(body + "\n");
