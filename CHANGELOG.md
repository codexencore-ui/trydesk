# Changelog — TryDesk for Windows, macOS and Linux

All notable changes to the desktop app (the program that shares or is controlled). The iOS app has its own file
(`mobile/CHANGELOG.md`) and the Android app its own release history; this one is only about the desktop.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[semantic versioning](https://semver.org/).

The release workflow (`.github/workflows/release.yml`, tags only) reads the section whose heading is the pushed tag
and uses it as the body of the GitHub Release, so the notes always come from this file. A tag with no matching
section fails the job on purpose.

## [Unreleased]

## [1.4.0]

Fixes found while testing the virtual monitor on a PC whose GPU has no video encoder, plus an always-attached installer.

### Fixed

- **The virtual monitor forced 30 Hz**, so the remote looked choppy even though TryDesk asked for 60 fps. Every
  resolution in the driver's settings now runs at **60 Hz**.
- **The driver never read TryDesk's settings file.** It only loads `C:\VirtualDisplayDriver\vdd_settings.xml` (the
  driver's fixed `confpath`, or the `VDDPATH` registry key), never the folder the driver was installed into, so it
  fell back to its own defaults. The install now drops the file there, records `VDDPATH` and restarts the display;
  a new **"Perbaiki kehalusan"** button in Settings fixes an already-installed driver the same way.
- **A failed screen share said only "failed to share screen".** When the Windows service (SYSTEM) cannot capture
  because the GPU has no hardware H.264 encoder — the service's only path — the viewer now gets that exact reason
  and the PC here gets a message naming the fix, instead of a silent generic failure.
- Settings → **Keamanan** → the service note explains the SYSTEM limitation: a PC without a hardware H.264 encoder
  should run TryDesk without the service.

### Added

- Settings → **Saat menjadi host** → **Monitor virtual** → **Perbaiki kehalusan (60 Hz)**: shown when the driver is
  installed but its settings file is missing or old, so the fix is one click.

### Notes

- The Windows installers are now attached to the GitHub Release as soon as the build finishes, signed or not, so a
  release is never left without a downloadable `.exe` (and the in-app updater always has something to point at).

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

[Unreleased]: https://github.com/latiefahmad/trydesk/compare/v1.4.0...HEAD
[1.4.0]: https://github.com/latiefahmad/trydesk/releases/tag/v1.4.0
[1.3.9]: https://github.com/latiefahmad/trydesk/releases/tag/v1.3.9
