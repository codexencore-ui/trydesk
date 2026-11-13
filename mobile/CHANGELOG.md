# Changelog — TryDesk for iPhone and iPad

All notable changes to the iOS app (the phone viewer built with Capacitor). The desktop app, the Android app and
the signaling server have their own history; this file is only about iOS.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[semantic versioning](https://semver.org/). Versions match the desktop app: the iOS app is released under the same
number.

The release workflow (`.github/workflows/ios.yml`) reads the section whose heading is the pushed tag and uses it as
the body of the GitHub Release, so a release's notes always come from this file.

## [Unreleased]

## [1.3.8]

The first iOS release: the TryDesk viewer, a phone that controls a Windows PC.

### Added

- The iOS app itself, built with Capacitor 8 and the Swift Package Manager, sharing the desktop renderer with the
  Android app. It is a viewer only: it controls PCs running TryDesk but never shares its own screen.
- Swift twins of the three Android plugins: `TdScreen` (full screen and landscape lock during a session),
  `TdLan` (Connect by IP over TCP) and `TdFile` (screenshots and recordings into Photos / the app's Documents).
- `TdStore`, backed by the **iOS Keychain**, which keeps the access password of a device the user chose to remember.
- An opt-in **"Remember the password for this device"** tick under the password box (off by default). A saved
  password is filled in and used automatically on the next connection and on auto-reconnect.
- An **"Edit perangkat"** sheet on a device card: change its name, see, replace or remove the saved password,
  favourite it, or remove it from the list.
- A phone layer that follows iOS: a left-edge swipe in place of the Android Back button, silent taps (WKWebView has
  no `navigator.vibrate`), the Local Network permission prompt for Connect by IP, and iOS styling.
- GitHub Actions CI (`ios.yml`): a `validate` job on pull requests that only compiles, and a `release` job on tags
  and manual runs that packages an **unsigned** `.ipa` and attaches it to the GitHub Release. No Mac and no Apple
  Developer account are needed to build it.

### Changed

- In touchpad mode the red cursor marker is **off by default** (it sat on top of the remote PC's own cursor). It can
  be turned back on from Control Center → Tampilan → *Penanda kursor touchpad*.

### Fixed

- The viewer could not connect at all: the signaling server rejects a WebSocket whose origin it does not know, and
  iOS serves its bundle from `app://app.trydesk.ai.id` (its `iosScheme`) where Android uses
  `https://app.trydesk.ai.id`. `iosScheme` is now `app`, and the server accepts the phone origins explicitly.
- The Xcode build failed to compile `MainViewController` (`overriding non-open property outside of its defining
  module`); the class and its overrides are now `open`, and the `prefersHomeIndicatorAutoHidden` override was
  dropped because UIKit imports it as `public`.

### Notes

- The `.ipa` is **unsigned** and will not install by double-click: re-sign it with a free Apple ID through AltStore,
  SideStore or Sideloadly, or open the project in Xcode and archive it. App Store and TestFlight need the Apple
  Developer Program.
- The iOS app targets iOS 15 and later.

[Unreleased]: https://github.com/latiefahmad/trydesk/compare/v1.3.8...HEAD
[1.3.8]: https://github.com/latiefahmad/trydesk/releases/tag/v1.3.8
