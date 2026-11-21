// The goose from the NestBox logo, drawn from its outline (millimetres, y down). Its head is the menu bar icon and
// the setup window's picture, as a template image so macOS tints it for light and dark; the whole bust in paper on
// an ink tile is the app's icon, which build_app.sh renders at build time (--app-icon).
import AppKit

enum Icon {
    private struct Shape {
        let width: CGFloat, height: CGFloat
        let rings: [[CGPoint]]   // the outline, then the eye

        /// Each ring is "x,y" pairs, split over lines.
        init(width: CGFloat, height: CGFloat, _ rings: [[String]]) {
            self.width = width
            self.height = height
            self.rings = rings.map { lines in
                lines.joined(separator: " ").split(separator: " ").map { pair in
                    let xy = pair.split(separator: ",").map { CGFloat(Double($0) ?? 0) }
                    return CGPoint(x: xy[0], y: xy[1])
                }
            }
        }

        /// The shape fitted and centred in rect; flipped is true where y grows downwards.
        func path(in rect: CGRect, flipped: Bool) -> NSBezierPath {
            let k = min(rect.width / width, rect.height / height)
            let x0 = rect.midX - width * k / 2, y0 = rect.midY - height * k / 2
            let path = NSBezierPath()
            path.windingRule = .evenOdd
            for ring in rings {
                for (i, p) in ring.enumerated() {
                    let q = NSPoint(x: x0 + p.x * k, y: flipped ? y0 + p.y * k : y0 + (height - p.y) * k)
                    if i == 0 { path.move(to: q) } else { path.line(to: q) }
                }
                path.close()
            }
            return path
        }
    }

    // The head and short neck, cut to the height of the word (9.4 × 7.0 mm).
    private static let head = Shape(width: 9.386, height: 6.972, [
        [
            "5.841,6.957 6.021,6.492 6.076,6.244 6.079,6.048 6.018,5.509 6.042,5.152 6.118,4.896 6.239,4.697",
            "6.353,4.583 6.494,4.491 6.666,4.418 6.868,4.362 7.151,4.309 7.479,4.269 8.64,4.189 8.832,4.143",
            "9.012,4.052 9.144,3.941 9.258,3.791 9.33,3.638 9.376,3.454 9.385,3.262 9.362,3.087 9.302,2.898",
            "9.216,2.738 9.069,2.562 8.895,2.428 7.863,1.887 6.601,1.25 3.985,0.05 3.705,0.009 3.419,0.001",
            "3.117,0.026 2.82,0.08 2.515,0.168 2.204,0.289 1.91,0.436 1.616,0.616 1.266,0.878 0.932,1.184",
            "0.688,1.477 0.481,1.801 0.295,2.187 0.149,2.616 0.055,3.041 0.006,3.494 0.003,3.886 0.034,4.252",
            "0.103,4.627 0.209,4.986 0.369,5.358 0.585,5.71 0.824,5.991 0.968,6.092 1.084,6.198 1.214,6.374",
            "1.271,6.509 1.297,6.651 1.295,6.798 1.26,6.972 5.838,6.972",
        ],
        [
            "3.702,2.077 3.797,2.043 3.901,2.076 5.408,3.345 5.444,3.432 5.426,3.524 5.303,3.7 5.15,3.85",
            "4.955,3.977 4.74,4.062 4.53,4.1 4.3,4.097 4.068,4.046 3.879,3.963 3.705,3.845 3.54,3.68 3.422,3.502",
            "3.334,3.289 3.292,3.082 3.291,2.845 3.337,2.62 3.42,2.423 3.548,2.231",
        ],
    ])

    // The bust: the same head on its long neck (9.4 × 13.7 mm).
    private static let bust = Shape(width: 9.386, height: 13.73, [
        [
            "0.28,13.232 0.321,13.441 0.408,13.587 0.542,13.685 0.723,13.728 4.412,13.729 4.634,13.684",
            "4.73,13.632 4.808,13.56 4.888,13.415 4.918,13.25 4.915,13.102 4.846,12.545 4.831,12.145 4.856,11.667",
            "4.928,11.192 5.096,10.558 5.51,9.451 5.758,8.695 5.927,8.055 6.034,7.492 6.089,7.007 6.105,6.561",
            "6.091,6.212 6.026,5.645 6.018,5.399 6.042,5.152 6.104,4.93 6.207,4.739 6.339,4.594 6.511,4.482",
            "6.714,4.403 6.996,4.335 7.333,4.285 8.663,4.186 8.89,4.119 9.074,4.005 9.217,3.854 9.316,3.673",
            "9.373,3.473 9.385,3.262 9.353,3.049 9.277,2.844 9.155,2.655 9.004,2.505 8.856,2.405 8.404,2.167",
            "6.601,1.25 3.985,0.05 3.832,0.024 3.454,0.001 3.044,0.037 2.631,0.131 2.224,0.281 1.832,0.481",
            "1.46,0.726 1.061,1.057 0.787,1.35 0.564,1.661 0.37,2.018 0.216,2.399 0.103,2.797 0.029,3.226",
            "0.001,3.639 0.013,4.049 0.066,4.451 0.143,4.781 0.243,5.077 0.369,5.358 0.521,5.618 0.785,5.952",
            "1.527,6.677 1.687,6.871 1.824,7.08 1.923,7.291 1.986,7.501 2.018,7.71 2.021,7.955 1.996,8.227",
            "1.941,8.514 1.862,8.797 1.756,9.095 1.515,9.636 0.83,10.999 0.649,11.417 0.513,11.79 0.407,12.157",
            "0.334,12.516 0.29,12.889",
        ],
        [
            "3.702,2.077 3.797,2.043 3.901,2.076 5.408,3.345 5.444,3.432 5.426,3.524 5.303,3.7 5.15,3.85",
            "4.955,3.977 4.74,4.062 4.53,4.1 4.3,4.097 4.068,4.046 3.879,3.963 3.705,3.845 3.54,3.68 3.422,3.502",
            "3.334,3.289 3.292,3.082 3.291,2.845 3.337,2.62 3.42,2.423 3.548,2.231",
        ],
    ])

    private static let paper = NSColor(srgbRed: 0xF3 / 255.0, green: 0xEC / 255.0, blue: 0xDD / 255.0, alpha: 1)
    private static let ink = NSColor(srgbRed: 0x1D / 255.0, green: 0x24 / 255.0, blue: 0x33 / 255.0, alpha: 1)

    /// The head, height points tall; the menu bar's is 15.
    static func image(height: CGFloat = 15, template: Bool = true) -> NSImage {
        let size = NSSize(width: (head.width / head.height * height).rounded(.up), height: height)
        let img = NSImage(size: size, flipped: true) { rect in
            NSColor.black.setFill()
            head.path(in: rect, flipped: true).fill()
            return true
        }
        img.isTemplate = template
        img.accessibilityDescription = "NestBox"
        return img
    }

    /// The app's icon, side pixels square: the bust in paper on an ink tile, in the tile size, corners and shadow of
    /// macOS app icons (an 824 tile on a 1024 canvas). A light goose on a dark tile stays clear when macOS darkens
    /// icons for its dark icon style.
    static func appIcon(side: Int) -> NSBitmapImageRep? {
        guard let rep = bitmap(width: side, height: side) else { return nil }
        let s = CGFloat(side) / 1024
        draw(into: rep) {
            let tile = NSRect(x: 100 * s, y: 100 * s, width: 824 * s, height: 824 * s)
            NSGraphicsContext.saveGraphicsState()
            let shadow = NSShadow()
            shadow.shadowColor = NSColor.black.withAlphaComponent(0.28)
            shadow.shadowOffset = NSSize(width: 0, height: -10 * s)
            shadow.shadowBlurRadius = 24 * s
            shadow.set()
            ink.setFill()
            NSBezierPath(roundedRect: tile, xRadius: 185 * s, yRadius: 185 * s).fill()
            NSGraphicsContext.restoreGraphicsState()
            paper.setFill()
            let frame = tile.insetBy(dx: 120 * s, dy: 100 * s).offsetBy(dx: 16 * s, dy: -4 * s)
            bust.path(in: frame, flipped: false).fill()
        }
        return rep
    }

    /// Writes the iconset that iconutil turns into AppIcon.icns: 16 to 512 points, at 1x and 2x.
    static func writeAppIconSet(_ folder: String) -> Bool {
        let dir = URL(fileURLWithPath: folder)
        guard (try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)) != nil else {
            return false
        }
        for points in [16, 32, 128, 256, 512] {
            for (scale, suffix) in [(1, ""), (2, "@2x")] {
                guard let data = appIcon(side: points * scale)?.representation(using: .png, properties: [:]),
                      (try? data.write(to: dir.appendingPathComponent("icon_\(points)x\(points)\(suffix).png"))) != nil
                else { return false }
            }
        }
        return true
    }

    /// Writes the menu bar icon at its real size and enlarged, and the app icon, side by side on white
    /// (--render-icon).
    static func writePNG(_ path: String) -> Bool {
        let scale: CGFloat = 2, gap: CGFloat = 16
        let real = image(), large = image(height: 96, template: false)
        guard let app = appIcon(side: 256) else { return false }
        let appSide: CGFloat = 128
        let w = real.size.width + gap + large.size.width + gap + appSide, h = appSide
        guard let rep = bitmap(width: Int(w * scale), height: Int(h * scale)) else { return false }
        rep.size = NSSize(width: w, height: h)
        draw(into: rep) {
            NSColor.white.setFill()
            NSRect(x: 0, y: 0, width: w, height: h).fill()
            real.draw(in: NSRect(x: 0, y: h - real.size.height, width: real.size.width, height: real.size.height))
            large.draw(in: NSRect(x: real.size.width + gap, y: (h - large.size.height) / 2,
                                  width: large.size.width, height: large.size.height))
            app.draw(in: NSRect(x: w - appSide, y: 0, width: appSide, height: appSide))
        }
        guard let data = rep.representation(using: .png, properties: [:]) else { return false }
        return (try? data.write(to: URL(fileURLWithPath: path))) != nil
    }

    private static func bitmap(width: Int, height: Int) -> NSBitmapImageRep? {
        NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: width, pixelsHigh: height, bitsPerSample: 8,
                         samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
                         bytesPerRow: 0, bitsPerPixel: 0)
    }

    private static func draw(into rep: NSBitmapImageRep, _ body: () -> Void) {
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
        body()
        NSGraphicsContext.restoreGraphicsState()
    }
}
