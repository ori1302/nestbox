// Decodes each image with macOS's QR reader (Core Image) and prints "PATH<TAB>TEXT", NO CODE or UNREADABLE.
// Usage: qr_decode FILE...
import AppKit
import CoreImage
import Foundation

let detector = CIDetector(
    ofType: CIDetectorTypeQRCode, context: nil, options: [CIDetectorAccuracy: CIDetectorAccuracyHigh])!
for path in CommandLine.arguments.dropFirst() {
    guard let image = NSImage(contentsOfFile: path), let tiff = image.tiffRepresentation, let ci = CIImage(data: tiff)
    else {
        print("\(path)\tUNREADABLE")
        continue
    }
    let found = detector.features(in: ci).compactMap { ($0 as? CIQRCodeFeature)?.messageString }
    print("\(path)\t\(found.first ?? "NO CODE")")
}
