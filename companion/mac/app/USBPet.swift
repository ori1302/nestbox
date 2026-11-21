import Foundation
import IOKit

enum USBPet {
    /// The serial ports of plugged-in USB devices whose product name is "NestBox".
    static func ports() -> [String] {
        var iterator: io_iterator_t = 0
        let matching = IOServiceMatching("IOSerialBSDClient")
        guard IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator) == KERN_SUCCESS else { return [] }
        defer { IOObjectRelease(iterator) }
        var ports: [String] = []
        while case let service = IOIteratorNext(iterator), service != 0 {
            defer { IOObjectRelease(service) }
            if productName(of: service) == "NestBox", let path = calloutDevice(of: service) { ports.append(path) }
        }
        return ports.sorted()
    }

    // The name lives on the USB device, an ancestor of the serial service.
    private static func productName(of service: io_object_t) -> String? {
        let options = IOOptionBits(kIORegistryIterateRecursively | kIORegistryIterateParents)
        return ["USB Product Name", "kUSBProductString"].lazy.compactMap {
            IORegistryEntrySearchCFProperty(service, kIOServicePlane, $0 as CFString, kCFAllocatorDefault, options)
                as? String
        }.first
    }

    private static func calloutDevice(of service: io_object_t) -> String? {
        IORegistryEntryCreateCFProperty(service, "IOCalloutDevice" as CFString, kCFAllocatorDefault, 0)?
            .takeRetainedValue() as? String
    }
}
