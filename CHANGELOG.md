# Changelog — TryDesk for Windows, macOS and Linux

All notable changes to the desktop app (the program that shares or is controlled). The iOS app has its own file
(`mobile/CHANGELOG.md`) and the Android app its own release history; this one is only about the desktop.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[semantic versioning](https://semver.org/).

The release workflow (`.github/workflows/release.yml`, tags only) reads the section whose heading is the pushed tag
and uses it as the body of the GitHub Release, so the notes always come from this file. A tag with no matching
section fails the job on purpose.

## [Unreleased]

## [1.3.9]

The first stage of "the remote keeps working when the host's monitor is off": TryDesk now bundles a virtual monitor.

### Added

- **The remote keeps working when the host has no active monitor.** Windows stops drawing the desktop when no
  monitor is attached or it is asleep, so a capture tool has nothing to send and the viewer sees a frozen or black
  picture — the same problem AnyDesk answers with a physical HDMI "headless" plug. TryDesk now bundles the
  open-source **Virtual Display Driver** (an IddCx virtual monitor, the approach TeamViewer uses) and installs it
  during Setup, so the PC has a screen to draw to and the remote keeps working. Local-file capture and the input
  path are unchanged.
- The driver is installed **automatically by the Setup version** (one UAC prompt, in the same flow as the Windows
  service) and removed on uninstall. Nothing else to configure.
- Settings → **Saat menjadi host** → **Monitor virtual**: shows whether it is installed, with **Pasang monitor
  virtual** / **Hapus monitor virtual** buttons for the Portable build or a later manual install.
- `nefcon` is used to install the driver (the Windows driver installer; `pnputil` cannot install this kind of
  device node). Both it and the driver are MIT-licensed, and their licences ship with the app.

### Notes

- Only the Windows build packages the driver, and only for **x64** (ARM64 is not installed automatically).
- Before a **major GPU/chipset driver update**, remove the virtual monitor first; if the display gets confused,
  boot into Safe Mode and remove it. This matches the driver project's own advice.
- The driver needs the **Microsoft Visual C++ Redistributable**.
- A kernel-level display driver may be flagged by antivirus/EDR on some PCs.

[Unreleased]: https://github.com/latiefahmad/trydesk/compare/v1.3.9...HEAD
[1.3.9]: https://github.com/latiefahmad/trydesk/releases/tag/v1.3.9
