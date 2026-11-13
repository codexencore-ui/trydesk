import Foundation
import Capacitor
import UIKit

// Full screen while a remote PC is on screen, the iOS twin of android/.../TdScreenPlugin.java:
// Android hides the status/navigation bars and locks landscape; iOS hides the status bar and restricts the
// supported orientations. Called from mobile.js with { immersive, orientation: "landscape" | "auto" }; both are
// undone when the session ends.
//
// `immersive` and `landscape` are kept in statics because iOS asks the view controller (not a plugin) whether the
// status bar is hidden and which orientations are allowed — see MainViewController.swift.
@objc(TdScreenPlugin)
public class TdScreenPlugin: CAPPlugin, CAPBridgedPlugin {
    public let identifier = "TdScreen"
    public let jsName = "TdScreen"
    public let pluginMethods: [CAPPluginMethod] = [
        CAPPluginMethod(name: "set", returnType: CAPPluginReturnPromise)
    ]

    static var immersiveActive = false
    static var landscapeLocked = false

    @objc func set(_ call: CAPPluginCall) {
        let immersive = call.getBool("immersive") ?? false
        let landscape = (call.getString("orientation") ?? "auto") == "landscape"

        DispatchQueue.main.async { [weak self] in
            // Flip the flags FIRST: MainViewController.supportedInterfaceOrientations reads landscapeLocked, and a
            // request for an orientation the view controller does not yet support fails with UISceneErrorDomain
            // code 101 ("None of the requested orientations are supported by the view controller").
            TdScreenPlugin.immersiveActive = immersive
            TdScreenPlugin.landscapeLocked = landscape

            let vc = self?.bridge?.viewController
            let mask: UIInterfaceOrientationMask = landscape ? .landscape : .portrait

            if #available(iOS 16.0, *) {
                vc?.setNeedsUpdateOfSupportedInterfaceOrientations()
                // connectedScenes is more reliable than reaching through the plugin's view (which can be off the
                // window during a transition).
                let scene = vc?.view.window?.windowScene
                    ?? UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.first
                scene?.requestGeometryUpdate(.iOS(interfaceOrientations: mask)) { error in
                    CAPLog.print("⚡️ TdScreen requestGeometryUpdate failed: \(error)")
                }
            } else {
                UIDevice.current.setValue(landscape ? UIInterfaceOrientation.landscapeRight.rawValue
                                                     : UIInterfaceOrientation.portrait.rawValue,
                                          forKey: "orientation")
                UIViewController.attemptRotationToDeviceOrientation()
            }
            // Makes MainViewController.prefersStatusBarHidden re-run and the bar appear/disappear.
            vc?.setNeedsStatusBarAppearanceUpdate()
            call.resolve()
        }
    }
}
