import CoreWLAN

enum MacWiFi {
    /// The network the Mac is on. macOS hides it from apps without Location access: then nil.
    static var currentSSID: String? { CWWiFiClient.shared().interface()?.ssid() }

    static var isOn5GHz: Bool { CWWiFiClient.shared().interface()?.wlanChannel()?.channelBand == .band5GHz }
}
