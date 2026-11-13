import Foundation
import Capacitor
import Photos

// Screenshots (.png) and session recordings (.webm) from the phone, the iOS twin of
// android/.../TdFilePlugin.java (the desktop's "Ambil screenshot" and "Rekam sesi"). mobile-shim.js maps the
// desktop's file calls onto this: target() names where the file will land, open/write/close stream it into the
// app's cache, and close() hands it to the Photos library.
//
// The Android plugin uses MediaStore; iOS has no such thing, so a PNG is saved with PHPhotoLibrary. A .webm is
// not a format Photos understands, so a video is written into the app's own Documents folder instead. Only a
// bare file name with one of those two extensions is accepted.
@objc(TdFilePlugin)
public class TdFilePlugin: CAPPlugin, CAPBridgedPlugin {
    public let identifier = "TdFile"
    public let jsName = "TdFile"
    public let pluginMethods: [CAPPluginMethod] = [
        CAPPluginMethod(name: "target", returnType: CAPPluginReturnPromise),
        CAPPluginMethod(name: "open", returnType: CAPPluginReturnPromise),
        CAPPluginMethod(name: "write", returnType: CAPPluginReturnPromise),
        CAPPluginMethod(name: "close", returnType: CAPPluginReturnPromise)
    ]

    private struct Job {
        let file: URL
        let handle: FileHandle
        let name: String
        let video: Bool
    }

    private var jobs: [String: Job] = [:]
    private let lock = NSLock()

    // Partial files left by a recording the app was killed in the middle of.
    override public func load() {
        let dir = exportDir()
        if let old = try? FileManager.default.contentsOfDirectory(at: dir, includingPropertiesForKeys: nil) {
            for f in old { try? FileManager.default.removeItem(at: f) }
        }
    }

    private func exportDir() -> URL {
        let dir = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("exports", isDirectory: true)
        if !FileManager.default.fileExists(atPath: dir.path) {
            try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        }
        return dir
    }

    private func withLock<T>(_ body: () -> T) -> T {
        lock.lock()
        defer { lock.unlock() }
        return body()
    }

    // Same rule as Android: a bare file name, one of the two extensions, no leading dot.
    private static func safeName(_ raw: String?) -> String {
        var s = (raw ?? "").replacingOccurrences(of: "\\", with: "/")
        if let slash = s.lastIndex(of: "/") { s = String(s[s.index(after: slash)...]) }
        s = s.replacingOccurrences(of: "[^A-Za-z0-9._-]", with: "_", options: .regularExpression)
        if s.count > 120 { s = String(s.suffix(120)) }
        let ok = (s.hasSuffix(".png") || s.hasSuffix(".webm")) && !s.hasPrefix(".")
        return ok ? s : ""
    }

    // Where the file will end up, shown to the user. Photos has no visible path; a video lands in Documents.
    private func shownPath(name: String, video: Bool) -> String {
        if video {
            let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            return docs.appendingPathComponent(name).path
        }
        return "Photos/" + name
    }

    @objc func target(_ call: CAPPluginCall) {
        let name = TdFilePlugin.safeName(call.getString("name"))
        if name.isEmpty { call.reject("unsupported file"); return }
        call.resolve(["path": shownPath(name: name, video: name.hasSuffix(".webm"))])
    }

    @objc func open(_ call: CAPPluginCall) {
        let name = TdFilePlugin.safeName(call.getString("path"))
        if name.isEmpty { call.reject("unsupported file"); return }
        let temp = exportDir().appendingPathComponent(UUID().uuidString)
        guard FileManager.default.createFile(atPath: temp.path, contents: nil),
              let handle = try? FileHandle(forWritingTo: temp) else {
            call.reject("could not create the file")
            return
        }
        let id = UUID().uuidString
        withLock { jobs[id] = Job(file: temp, handle: handle, name: name, video: name.hasSuffix(".webm")) }
        call.resolve(["id": id])
    }

    @objc func write(_ call: CAPPluginCall) {
        let id = call.getString("id") ?? ""
        guard let job = withLock({ jobs[id] }) else { call.reject("unknown file"); return }
        let data = Data(base64Encoded: call.getString("data") ?? "", options: .ignoreUnknownCharacters) ?? Data()
        do {
            try job.handle.write(contentsOf: data)
            call.resolve()
        } catch {
            call.reject("write failed")
        }
    }

    @objc func close(_ call: CAPPluginCall) {
        let id = call.getString("id") ?? ""
        guard let job = withLock({ jobs.removeValue(forKey: id) }) else { call.reject("unknown file"); return }
        try? job.handle.close()
        let cancel = call.getBool("cancel") ?? false

        if cancel {
            try? FileManager.default.removeItem(at: job.file)
            call.resolve()
            return
        }
        if job.video {
            // Photos can't take a .webm; keep it in Documents (visible via the Files app) instead of the gallery.
            let dest = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
                .appendingPathComponent(job.name)
            try? FileManager.default.removeItem(at: dest)
            do {
                try FileManager.default.moveItem(at: job.file, to: dest)
                call.resolve()
            } catch {
                try? FileManager.default.removeItem(at: job.file)
                call.reject("could not save the recording")
            }
        } else {
            saveToPhotos(job, call)
        }
    }

    private func saveToPhotos(_ job: Job, _ call: CAPPluginCall) {
        let status = PHPhotoLibrary.authorizationStatus(for: .addOnly)
        let proceed: (PHAuthorizationStatus) -> Void = { s in
            guard s == .authorized || s == .limited else {
                try? FileManager.default.removeItem(at: job.file)
                call.reject("Akses ke Galeri ditolak")
                return
            }
            PHPhotoLibrary.shared().performChanges {
                let req = PHAssetCreationRequest.forAsset()
                req.addResource(with: .photo, fileURL: job.file, options: nil)
            } completionHandler: { success, error in
                try? FileManager.default.removeItem(at: job.file)
                if success {
                    call.resolve()
                } else {
                    call.reject(error?.localizedDescription ?? "could not save to the gallery")
                }
            }
        }
        if status == .notDetermined {
            PHPhotoLibrary.requestAuthorization(for: .addOnly) { s in proceed(s) }
        } else {
            proceed(status)
        }
    }
}
