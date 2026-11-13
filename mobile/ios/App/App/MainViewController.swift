import UIKit
import Capacitor

// The app's view controller. Capacitor's default is CAPBridgeViewController; we subclass it only so the status
// bar and the allowed orientations follow the TdScreen plugin (full screen + landscape while a remote PC is on
// screen, released when the session ends). The plugin itself cannot answer these — iOS asks the view controller.
//
// The class and the overrides are `open`: CAPBridgeViewController and UIViewController declare these members
// `open`, and Swift refuses to override an `open` member from another module at a lower access level.
open class MainViewController: CAPBridgeViewController {

    override open var prefersStatusBarHidden: Bool {
        return TdScreenPlugin.immersiveActive
    }

    // NOTE: no `prefersHomeIndicatorAutoHidden` override. UIKit imports it as a plain `public` property (not
    // `open`), and Swift refuses to override a non-open member from another module — so this overlay cannot drive
    // it. The home indicator just behaves normally; the status bar and the landscape lock are what matter here.

    override open var supportedInterfaceOrientations: UIInterfaceOrientationMask {
        // Portrait by default (the phone UI is portrait-first); TdScreen unlocks landscape during a session.
        return TdScreenPlugin.landscapeLocked ? .landscape : .portrait
    }

    // Registers the TryDesk plugins (the Swift twins of the Android Td*Plugin.java files). This is the
    // documented hook for in-app plugins: CAPBridgeViewController.loadView() creates the bridge and then calls
    // capacitorDidLoad(), before the WebView loads the app — registering later (e.g. in viewDidLoad) would race
    // the first JS call. The wiring lives here so it sits next to the Android MainActivity.
    override open func capacitorDidLoad() {
        bridge?.registerPluginInstance(TdScreenPlugin())
        bridge?.registerPluginInstance(TdLanPlugin())
        bridge?.registerPluginInstance(TdFilePlugin())
        bridge?.registerPluginInstance(TdStorePlugin())
    }
}
