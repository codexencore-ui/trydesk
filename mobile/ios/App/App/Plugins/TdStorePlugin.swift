import Foundation
import Capacitor

// The phone's small secure store, the iOS twin of android/.../TdStorePlugin.java. It holds the one secret the
// viewer app keeps: the access password of a device the user chose to remember (key "pw:<deviceId>").
//
// iOS: the Keychain, this device only, readable only while the phone is unlocked (kSecAttrAccessibleWhenUnlockedThisDeviceOnly).
// The value is encrypted by the OS; it is never written to the WebView's localStorage, so a leaked app container or
// a backup does not expose it. Android's twin uses EncryptedSharedPreferences (AES-256 under the Keystore).
//
// mobile-shim.js maps the desktop renderer's savedPassword* calls onto this: get/set/remove, one key at a time,
// plus list() of the keys that start with a given prefix (used to drop everything when the user clears passwords).
@objc(TdStorePlugin)
public class TdStorePlugin: CAPPlugin, CAPBridgedPlugin {
    public let identifier = "TdStore"
    public let jsName = "TdStore"
    public let pluginMethods: [CAPPluginMethod] = [
        CAPPluginMethod(name: "get", returnType: CAPPluginReturnPromise),
        CAPPluginMethod(name: "set", returnType: CAPPluginReturnPromise),
        CAPPluginMethod(name: "remove", returnType: CAPPluginReturnPromise),
        CAPPluginMethod(name: "list", returnType: CAPPluginReturnPromise),
    ]

    // One Keychain service for the whole app, so list() can find every key we wrote and nothing else.
    private let service = "id.ai.trydesk.ios.store"

    // Keys are our own ("pw:<id>"); reject anything long or odd so a bug can't spray Keychain items.
    private func cleanKey(_ raw: String?) -> String? {
        guard let k = raw, !k.isEmpty, k.count <= 80, k.rangeOfCharacter(from: .newlines) == nil else { return nil }
        return k
    }

    private func query(_ key: String) -> [String: Any] {
        return [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: key,
        ]
    }

    @objc func get(_ call: CAPPluginCall) {
        guard let key = cleanKey(call.getString("key")) else { call.reject("invalid key"); return }
        var q = query(key)
        q[kSecReturnData as String] = true
        q[kSecMatchLimit as String] = kSecMatchLimitOne
        var item: CFTypeRef?
        let status = SecItemCopyMatching(q as CFDictionary, &item)
        if status == errSecSuccess, let data = item as? Data, let s = String(data: data, encoding: .utf8) {
            call.resolve(["value": s])
        } else if status == errSecItemNotFound {
            call.resolve(["value": NSNull()])
        } else {
            call.reject("keychain read failed (\(status))")
        }
    }

    @objc func set(_ call: CAPPluginCall) {
        guard let key = cleanKey(call.getString("key")) else { call.reject("invalid key"); return }
        guard let value = call.getString("value") else { call.reject("missing value"); return }
        let data = Data(value.utf8)
        // Try to update an existing item first; only add when there is none (SecItemAdd would fail with a duplicate).
        let up = [kSecValueData as String: data]
        let updStatus = SecItemUpdate(query(key) as CFDictionary, up as CFDictionary)
        if updStatus == errSecSuccess { call.resolve(); return }
        if updStatus != errSecItemNotFound { call.reject("keychain update failed (\(updStatus))"); return }
        var q = query(key)
        q[kSecValueData as String] = data
        // This device only, and only while unlocked: never copied to iCloud or a Mac backup.
        q[kSecAttrAccessible as String] = kSecAttrAccessibleWhenUnlockedThisDeviceOnly
        let addStatus = SecItemAdd(q as CFDictionary, nil)
        if addStatus == errSecSuccess { call.resolve() } else { call.reject("keychain write failed (\(addStatus))") }
    }

    @objc func remove(_ call: CAPPluginCall) {
        guard let key = cleanKey(call.getString("key")) else { call.reject("invalid key"); return }
        let status = SecItemDelete(query(key) as CFDictionary)
        // Deleting something that isn't there is success, not an error: the caller wanted it gone.
        if status == errSecSuccess || status == errSecItemNotFound { call.resolve() }
        else { call.reject("keychain delete failed (\(status))") }
    }

    @objc func list(_ call: CAPPluginCall) {
        let prefix = call.getString("prefix") ?? ""
        var q: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
        ]
        q[kSecReturnAttributes as String] = true
        q[kSecMatchLimit as String] = kSecMatchLimitAll
        var items: CFTypeRef?
        let status = SecItemCopyMatching(q as CFDictionary, &items)
        if status == errSecItemNotFound { call.resolve(["keys": []]); return }
        guard status == errSecSuccess, let arr = items as? [[String: Any]] else {
            call.reject("keychain list failed (\(status))"); return
        }
        let keys = arr.compactMap { $0[kSecAttrAccount as String] as? String }.filter { $0.hasPrefix(prefix) }
        call.resolve(["keys": keys])
    }
}
