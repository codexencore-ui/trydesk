import Foundation
import Capacitor
import Network

// "Sambung lewat IP" on the phone, the iOS twin of android/.../TdLanPlugin.java: a plain TCP socket to a TryDesk
// PC on the same network, one JSON message per line (the same thing main.js does on the desktop). The WebView
// can't do this itself: the page is https and the PC's port is not, so it would be blocked as mixed content.
// This only carries lines; app.js speaks the protocol.
//
// Events: "lan" { id, kind: "line", line } and { id, kind: "close" }.
@objc(TdLanPlugin)
public class TdLanPlugin: CAPPlugin, CAPBridgedPlugin {
    public let identifier = "TdLan"
    public let jsName = "TdLan"
    public let pluginMethods: [CAPPluginMethod] = [
        CAPPluginMethod(name: "open", returnType: CAPPluginReturnPromise),
        CAPPluginMethod(name: "write", returnType: CAPPluginReturnPromise),
        CAPPluginMethod(name: "close", returnType: CAPPluginReturnPromise)
    ]

    private static let maxLine = 64 * 1024
    private var connections: [String: Connection] = [:]
    private let lock = NSLock()

    // Tracks, off-thread, whether a plugin call is still waiting for its first answer (see `open`).
    private final class AnswerBox {
        private var pending = true
        private let lock = NSLock()
        // Returns true exactly once, for whoever answers first.
        func take() -> Bool {
            lock.lock(); defer { lock.unlock() }
            let was = pending
            pending = false
            return was
        }
    }

    // One TCP connection plus a serial queue for writes, so messages leave in the order they were sent (a
    // "disconnect" followed by close must not lose the disconnect). NWConnection is the iOS equivalent of the
    // java.net.Socket the Android plugin uses.
    private final class Connection {
        let conn: NWConnection
        let queue = DispatchQueue(label: "id.ai.trydesk.lan.write")
        var buffer = Data()
        let onLine: (String) -> Void

        init(conn: NWConnection, onLine: @escaping (String) -> Void) {
            self.conn = conn
            self.onLine = onLine
        }
    }

    @objc func open(_ call: CAPPluginCall) {
        let id = call.getString("id") ?? ""
        let host = call.getString("host") ?? ""
        // UInt16(exactly:) returns nil instead of trapping when JS sends a silly port (negative / > 65535).
        let port = UInt16(exactly: call.getInt("port") ?? 7171) ?? 7171

        // Same rule as the desktop and Android: only addresses on the local network.
        guard let addr = TdLanPlugin.resolveLocal(host) else {
            call.reject("Hanya alamat jaringan lokal (LAN) yang bisa dipakai")
            return
        }
        guard let nwPort = NWEndpoint.Port(rawValue: port) else {
            call.reject("Port tidak valid")
            return
        }

        // Replace any previous socket with this id.
        closeSocket(id)

        // TCP options: no-delay, the iOS twin of the Android plugin's setTcpNoDelay(true) (keystroke latency).
        let tcp = NWProtocolTCP.Options()
        tcp.noDelay = true
        let conn = NWConnection(host: addr, port: nwPort, using: NWParameters(tls: nil, tcp: tcp))
        let connection = Connection(conn: conn) { [weak self] line in
            self?.notifyListeners("lan", data: ["id": id, "kind": "line", "line": line])
        }
        // Tracks whether `call` still needs an answer: the connection may fail (.failed) before or after the user
        // closes it, and only the first of those should touch the call. A reference box because the escaping
        // state handler may run off-thread.
        let answered = AnswerBox()
        withLock { connections[id] = connection }

        conn.stateUpdateHandler = { [weak self] state in
            guard let self else { return }
            switch state {
            case .ready:
                if answered.take() { call.resolve(["ok": true, "id": id]) }
                self.read(connection, id: id)
            case .failed:
                if answered.take() { call.reject("Tidak bisa terhubung ke alamat ini") }
                self.closed(id)
            case .cancelled:
                if answered.take() { call.reject("Koneksi dibatalkan") }
                self.closed(id)
            default:
                break
            }
        }
        conn.start(queue: .global(qos: .userInitiated))
    }

    @objc func write(_ call: CAPPluginCall) {
        let id = call.getString("id") ?? ""
        let line = (call.getString("line") ?? "") + "\n"
        guard let connection = withLock({ connections[id] }), let data = line.data(using: .utf8) else {
            call.resolve()
            return
        }
        // isComplete: false — one line is a message on the stream, not the end of the sending context (the
        // default in the Network framework is true, which would close out the logical message per line).
        connection.queue.async { connection.conn.send(content: data, isComplete: false, completion: .contentProcessed { _ in }) }
        call.resolve()
    }

    @objc func close(_ call: CAPPluginCall) {
        closeSocket(call.getString("id") ?? "")
        call.resolve()
    }

    // ── Internals ──

    private func read(_ connection: Connection, id: String) {
        connection.conn.receive(minimumIncompleteLength: 1, maximumLength: 64 * 1024) { [weak self] data, _, isComplete, error in
            guard let self else { return }
            if let data = data, !data.isEmpty {
                connection.buffer.append(data)
                self.drainLines(connection, id: id)
            }
            if isComplete || error != nil {
                self.closed(id)
            } else {
                self.read(connection, id: id)
            }
        }
    }

    // Emits every complete line in the buffer; a line longer than maxLine drops the connection (same as Android).
    private func drainLines(_ connection: Connection, id: String) {
        while let nl = connection.buffer.firstIndex(of: 0x0A) {
            let lineData = connection.buffer.subdata(in: 0..<nl)
            connection.buffer.removeSubrange(0...nl)
            if lineData.count >= TdLanPlugin.maxLine {
                closeSocket(id)
                return
            }
            if let line = String(data: lineData, encoding: .utf8) {
                connection.onLine(line)
            }
        }
        if connection.buffer.count >= TdLanPlugin.maxLine {
            closeSocket(id)
        }
    }

    private func closed(_ id: String) {
        guard let connection = withLock({ connections.removeValue(forKey: id) }) else { return }
        connection.conn.cancel()
        notifyListeners("lan", data: ["id": id, "kind": "close"])
    }

    private func closeSocket(_ id: String) {
        guard let connection = withLock({ connections.removeValue(forKey: id) }) else { return }
        // Graceful close: end the send side, let the queued writes flush (the Android plugin waits 500 ms after
        // shutdownOutput), then cancel. Without the small delay an in-flight "disconnect" line could be lost.
        connection.queue.async {
            connection.conn.send(content: nil, contentContext: .finalMessage, isComplete: true, completion: .contentProcessed { _ in })
            connection.queue.asyncAfter(deadline: .now() + 0.5) { connection.conn.cancel() }
        }
    }

    private func withLock<T>(_ body: () -> T) -> T {
        lock.lock()
        defer { lock.unlock() }
        return body()
    }

    // 10/8, 172.16/12, 192.168/16, loopback, link-local, 100.64/10 (CGNAT VPNs), IPv6 ULA / link-local.
    private static func resolveLocal(_ host: String) -> NWEndpoint.Host? {
        let trimmed = host.trimmingCharacters(in: .whitespaces)
        if trimmed.isEmpty { return nil }

        // IPv6 literal in brackets, e.g. [fe80::1]
        var bare = trimmed
        if bare.hasPrefix("["), bare.hasSuffix("]") { bare = String(bare.dropFirst().dropLast()) }

        if let v4 = IPv4Address(bare) {
            return isLocalV4(v4) ? NWEndpoint.Host(bare) : nil
        }
        if let v6 = IPv6Address(bare) {
            return isLocalV6(v6) ? NWEndpoint.Host(bare) : nil
        }
        // A hostname: resolve it, then apply the same local-only rule.
        var hints = addrinfo(ai_flags: AI_ADDRCONFIG, ai_family: AF_UNSPEC, ai_socktype: SOCK_STREAM,
                             ai_protocol: 0, ai_addrlen: 0, ai_canonname: nil, ai_addr: nil, ai_next: nil)
        var res: UnsafeMutablePointer<addrinfo>?
        guard getaddrinfo(bare, nil, &hints, &res) == 0, let first = res else { return nil }
        defer { freeaddrinfo(res) }
        var node: UnsafeMutablePointer<addrinfo>? = first
        while let info = node {
            // Advance BEFORE any early return, so the loop still walks the list correctly.
            node = info.pointee.ai_next
            guard let sa = info.pointee.ai_addr else { continue }
            if sa.pointee.sa_family == sa_family_t(AF_INET) {
                let ok = sa.withMemoryRebound(to: sockaddr_in.self, capacity: 1) { sin -> Bool in
                    let bytes = withUnsafeBytes(of: sin.pointee.sin_addr) { Array($0) }
                    guard let a = IPv4Address(Data(bytes)) else { return false }
                    return isLocalV4(a)
                }
                if ok { return NWEndpoint.Host(bare) }
            } else if sa.pointee.sa_family == sa_family_t(AF_INET6) {
                let ok = sa.withMemoryRebound(to: sockaddr_in6.self, capacity: 1) { sin6 -> Bool in
                    let bytes = withUnsafeBytes(of: sin6.pointee.sin6_addr) { Array($0) }
                    guard let a = IPv6Address(Data(bytes)) else { return false }
                    return isLocalV6(a)
                }
                if ok { return NWEndpoint.Host(bare) }
            }
        }
        return nil
    }

    private static func isLocalV4(_ a: IPv4Address) -> Bool {
        let b = [UInt8](a.rawValue)
        guard b.count == 4 else { return false }
        if b[0] == 127 { return true }                                  // loopback
        if b[0] == 10 { return true }                                   // 10/8
        if b[0] == 172, (16...31).contains(Int(b[1])) { return true }   // 172.16/12
        if b[0] == 192, b[1] == 168 { return true }                     // 192.168/16
        if b[0] == 169, b[1] == 254 { return true }                     // link-local
        if b[0] == 100, (64...127).contains(Int(b[1])) { return true }  // 100.64/10 CGNAT VPNs
        return false
    }

    private static func isLocalV6(_ a: IPv6Address) -> Bool {
        let b = [UInt8](a.rawValue)
        guard b.count == 16 else { return false }
        if b[0...14].allSatisfy({ $0 == 0 }) && b[15] == 1 { return true }   // ::1 loopback
        if b[0] == 0xfe, (b[1] & 0xc0) == 0x80 { return true }              // fe80::/10 link-local
        if (b[0] & 0xfe) == 0xfc { return true }                            // fc00::/7 ULA
        return false
    }
}
