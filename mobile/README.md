# TryDesk for Android + iOS (viewer)

Control a Windows PC from an Android or iOS phone. The phone app is a **viewer only**: it connects to PCs running
TryDesk, but it never shares its own screen (incoming requests are refused with a clear message).

## How it is built

One code base: the phone app runs the **desktop renderer unchanged** (`src/renderer/index.html`, `app.js`,
`style.css`) inside a WebView (Capacitor 8). `build-www.mjs` copies it into `mobile/www/` and adds three files
from `mobile/web/`:

| File | What it does |
|------|--------------|
| `mobile-shim.js` | Stands in for Electron's `window.electronAPI` (loaded before `app.js`). Viewer calls work; every host-side call (capture, input injection, local files) answers "not available". |
| `mobile.js` | The phone layer (loaded after `app.js`): touch → mouse (touchpad / direct touch, two-finger scroll and right click, pinch zoom), phone keyboard (text is diffed, so autocorrect and predictions type correctly), special-keys bar with sticky Ctrl/Alt/Shift/Win, floating toolbar, Back button (Android) / edge-swipe (iOS), screen wake lock, viewer-only rules. |
| `mobile.css` | Phone layout over the desktop design tokens (bottom-sheet Control Center, full-screen settings). |

`build-www.mjs` also writes `www/version.js` with `window.TD_MOBILE = { version, platform }`. The platform is
`"android"` by default and `"ios"` when you pass `--platform=ios`; `mobile.js` reads it for the few places the
two systems differ (haptics, the Back gesture, the aliases and the WebView label shown in Tentang).

Typing from the phone uses the host action `{ action: "type", text }` (Unicode `SendInput` in
`src/input-simulator.js`), so the PC must run TryDesk 1.1.0 or newer.

The app is served from a host of ours inside the WebView (`capacitor.config.json` → `server.hostname`), and the
signaling server accepts that origin besides the desktop app. The scheme differs per platform — Android uses
`androidScheme: "https"` (`https://app.trydesk.ai.id`), iOS uses `iosScheme: "app"` (`app://app.trydesk.ai.id`)
because iOS cannot use `http(s)` there. The signaling server's origin check (`server/signaling-server.js`)
must keep accepting both: a wrong scheme makes every connection fail with a WebSocket handshake 403 that looks
exactly like the server being offline.

## Build

Requirements: Node.js 22.12+, JDK 21, Android SDK with `platforms;android-36`, `build-tools;36.1.0`,
`platform-tools` (set `ANDROID_HOME` or keep the SDK in `%LOCALAPPDATA%\Android\Sdk`).

```bash
cd mobile
npm install
npm run apk:debug      # debug APK (WebView debugging on)  -> android/app/build/outputs/apk/debug/
npm run apk:release    # release APK                       -> android/app/build/outputs/apk/release/
```

`scripts/buat-rilis.ps1` builds the release APK automatically and puts it on the downloads page as
`TryDesk-Android-<version>.apk` (size + SHA-256 stamped like the Windows builds).

### Release signing

The signing key is **never** inside the repo. Gradle reads
`%USERPROFILE%\.trydesk\android-signing.properties` (or the file named by `TRYDESK_ANDROID_SIGNING`):

```properties
storeFile=C:/Users/<you>/.trydesk/trydesk-android-release.jks
storePassword=...
keyAlias=trydesk
keyPassword=...
```

Without that file `assembleRelease` produces an unsigned APK. Back the key up: every update must be signed
with the same key, or Android refuses to install it over the old version.

## iOS build

The iOS app is the same `www` bundle with Swift twins of the three Android plugins
(`mobile/ios/App/App/Plugins/Td*Plugin.swift`) and a `MainViewController.swift` that answers the status-bar and
orientation questions iOS asks the view controller instead of a plugin.

The `ios/` Xcode project **is committed** (it uses Swift Package Manager, so it can be generated and synced on
Windows with Capacitor 8 — no CocoaPods, no Mac needed to prepare it). Generate/sync it with:

```bash
cd mobile
npm run www:ios     # node build-www.mjs --platform=ios
npm run sync:ios    # + npx cap sync ios
npm run open:ios    # + cap open ios   (macOS only)
```

Building and running the app needs **Xcode 26+ on macOS** (iOS 15+ target). GitHub Actions does it for you via
`.github/workflows/ios.yml`:

- **Pull requests** — a `validate` job compiles the app (Debug, unsigned) so a broken Swift or plugin change fails
  the PR instead of the release. It uploads nothing.
- **Tags (`v*`) and manual runs** — a `release` job does the same build in Release, packages the **unsigned**
  `.ipa` (`TryDesk-<version>-ios-unsigned.ipa`, a `Payload/App.app` zip) and attaches it to the GitHub Release.
- Both jobs run on `macos-26`, which defaults to Xcode 26 (`macos-15` still defaults to Xcode 16.4).
- The shared build steps live in `.github/actions/ios-build/action.yml`, so the PR check and the release build
  compile exactly the same thing.

### iOS signing

An unsigned `.ipa` is **not installable by double-click** — iOS rejects unsigned apps. It is the input for:

- **Sideloading tools** (AltStore / SideStore / Sideloadly): re-sign with a free Apple ID; the signature lasts
  7 days and must be refreshed.
- **Xcode**: open `ios/App/App.xcodeproj`, pick your team under *Signing & Capabilities*, then *Product →
  Archive* and export. A free Apple ID works for your own devices.

For App Store / TestFlight / Ad Hoc distribution you need the **Apple Developer Program (US$99/year)** and a
Distribution certificate + provisioning profile. To build that in CI, add the base64 `.p12` and
`.mobileprovision` as repository secrets and pass them to `xcodebuild` (or use `fastlane match`); nothing in the
workspace assumes a paid account, so it keeps working unsigned until then.

### What differs on iOS

- **Haptics** (`navigator.vibrate`) do not exist in WKWebView, so the taps are silent (`haptic()` in `mobile.js`).
- **No Back button**: a left-edge swipe runs the same "close what's open" handler the Android Back button uses.
- **Local network**: connecting to a PC by IP triggers iOS's Local Network permission prompt
  (`NSLocalNetworkUsageDescription` in `Info.plist`); the user must allow it or `Sambung lewat IP` fails.
- **Recordings**: a `.webm` is not a Photos format, so a session recording is saved to the app's Documents
  folder instead of the gallery. Screenshots (`.png`) go to Photos.

## Test without a phone

- Debug APK in the Android Emulator: `adb install -r app-debug.apk`, then
  `adb forward tcp:9444 localabstract:webview_devtools_remote_<pid>` and drive it over Chrome DevTools.
- Real phone: enable USB debugging, `adb install`, and inspect it in `chrome://inspect`.
