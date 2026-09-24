// Decode the captured engine pixels independently of the app's readiness JSON.
// The dedicated fixtures contain visible light shapes/text on a black background.
import Foundation
import CoreGraphics
import ImageIO

guard CommandLine.arguments.count == 2 else {
    fputs("Usage: check_core_frame FRAME.png\n", stderr)
    exit(2)
}
let url = URL(fileURLWithPath: CommandLine.arguments[1])
guard let source = CGImageSourceCreateWithURL(url as CFURL, nil),
      let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else {
    fputs("Cannot decode the engine frame\n", stderr)
    exit(1)
}
let width = image.width, height = image.height
guard width > 0, height > 0, width <= 8192, height <= 8192 else { exit(1) }
var pixels = [UInt8](repeating: 0, count: width * height * 4)
let decoded = pixels.withUnsafeMutableBytes { bytes -> Bool in
    guard let context = CGContext(data: bytes.baseAddress, width: width, height: height,
        bitsPerComponent: 8, bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue) else { return false }
    context.draw(image, in: CGRect(x: 0, y: 0, width: CGFloat(width), height: CGFloat(height)))
    return true
}
guard decoded else { exit(1) }
var visible = 0, light = 0, darkest = 255, brightest = 0
var redLeft = 0, redRight = 0, greenLeft = 0, greenRight = 0
var rgbSums: [UInt64] = [0, 0, 0]
var redDominantPixels = 0
var minX = width, minY = height, maxX = -1, maxY = -1
for y in 0..<height {
    for x in 0..<width {
        let index = (y * width + x) * 4
        let rgb = max(Int(pixels[index]), Int(pixels[index + 1]), Int(pixels[index + 2]))
        if pixels[index + 3] > 8 {
            for channel in 0..<3 { rgbSums[channel] += UInt64(pixels[index + channel]) }
            let red = Int(pixels[index]), green = Int(pixels[index + 1]), blue = Int(pixels[index + 2])
            if red > 24 && red * 5 > green * 6 && red * 5 > blue * 6 { redDominantPixels += 1 }
        }
        if pixels[index + 3] > 240 {
            if pixels[index] > 240 && pixels[index + 1] < 10 && pixels[index + 2] < 10 {
                if x < width / 2 { redLeft += 1 } else { redRight += 1 }
            }
            if pixels[index + 1] > 240 && pixels[index] < 10 && pixels[index + 2] < 10 {
                if x < width / 2 { greenLeft += 1 } else { greenRight += 1 }
            }
        }
        darkest = min(darkest, rgb); brightest = max(brightest, rgb)
        if pixels[index + 3] > 8 { visible += 1 }
        if pixels[index + 3] > 8 && rgb > 24 {
            light += 1
            minX = min(minX, x); minY = min(minY, y)
            maxX = max(maxX, x); maxY = max(maxY, y)
        }
    }
}
let report: [String: Any] = ["width": width, "height": height,
    "visiblePixels": visible, "lightPixels": light, "minRGB": darkest, "maxRGB": brightest,
    "redLeftPixels": redLeft, "redRightPixels": redRight,
    "greenLeftPixels": greenLeft, "greenRightPixels": greenRight,
    "rgbSums": rgbSums, "redDominantPixels": redDominantPixels,
    "contentBounds": maxX >= 0 ? [minX, minY, maxX + 1, maxY + 1] : []]
let json = try JSONSerialization.data(withJSONObject: report, options: .sortedKeys)
print(String(decoding: json, as: UTF8.self))
