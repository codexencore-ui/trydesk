// Print one section of mobile/CHANGELOG.md, so CI can use it as the body of a GitHub Release.
//
//   node changelog.mjs            # the section for the version in the root package.json
//   node changelog.mjs v1.3.8     # the section for that version (a leading "v" is fine)
//   node changelog.mjs 1.3.8      # same
//
// The changelog is Keep a Changelog: each released version is a "## [<version>] - <date>" heading (the date is
// optional), and the section runs until the next "## [" heading. [Unreleased] is never printed: a release has a
// concrete version. If the version has no section, this exits non-zero with a clear message so the release fails
// loudly instead of publishing an empty body.
//
// It is deliberately dependency-free (plain Node) and reads the same file a human edits.

import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));

// "v1.3.8" and "1.3.8" both mean the same version; the tag may carry the v, the package.json never does.
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
  text = readFileSync(join(here, "CHANGELOG.md"), "utf8");
} catch (e) {
  console.error(`changelog: could not read CHANGELOG.md (${e.message})`);
  process.exit(1);
}

// Split on lines and collect the one whose heading is "## [<version>]" — with or without a trailing date, and
// ignoring case so "## [1.3.8]" and "## [v1.3.8]" both match. The section ends at the next version heading.
const lines = text.split(/\r?\n/);
const startRe = new RegExp(`^##\\s+\\[v?${version.replace(/\./g, "\\.")}\\]`, "i");
const anyVersionRe = /^##\s+\[/;

let start = -1;
for (let i = 0; i < lines.length; i++) if (startRe.test(lines[i])) { start = i; break; }
if (start === -1) {
  console.error(`changelog: no "## [${version}]" section in mobile/CHANGELOG.md — add it before tagging`);
  process.exit(1);
}

let end = lines.length;
for (let i = start + 1; i < lines.length; i++) if (anyVersionRe.test(lines[i])) { end = i; break; }

// The body is what is under the heading (the heading itself is the release's title). Trim blank edges and the
// link-reference definitions Keep a Changelog keeps at the bottom of the file.
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
