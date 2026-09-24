// Run on the macOS builder against the actual compiled .app before packaging.
// Reading Info.plist directly is insufficient: Foundation must recognize the
// bundle as well. A custom root Resources directory breaks that recognition.
import Foundation
import CoreText
import ImageIO

func require(_ condition: Bool, _ message: String) {
    if !condition {
        FileHandle.standardError.write(Data(("BUNDLE ERROR: " + message + "\n").utf8))
        exit(1)
    }
}
require(CommandLine.arguments.count == 2, "Expected path to compiled Aurea.app")
let url = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
let fm = FileManager.default
require(!fm.fileExists(atPath: url.appendingPathComponent("Resources").path), "iOS app bundles cannot contain a custom root Resources directory")
guard let bundle = Bundle(url: url) else { require(false, "Foundation cannot open the app bundle"); exit(1) }
require(bundle.bundleIdentifier == "com.aurea.aurea", "Foundation did not resolve the Aurea bundle identifier")
require(bundle.object(forInfoDictionaryKey: "CFBundlePackageType") as? String == "APPL", "Bundle is not an application")
require(bundle.executableURL.map { fm.isExecutableFile(atPath: $0.path) } == true, "Foundation could not resolve an executable app binary")
require((bundle.object(forInfoDictionaryKey: "UIAppFonts") as? [String])?.contains("CupertinoIcons.ttf") == true,
        "Official icon font is not registered in UIAppFonts")
guard let fontURL = bundle.url(forResource: "CupertinoIcons", withExtension: "ttf"),
      let descriptors = CTFontManagerCreateFontDescriptorsFromURL(fontURL as CFURL) as? [CTFontDescriptor],
      descriptors.contains(where: { CTFontDescriptorCopyAttribute($0, kCTFontNameAttribute) as? String == "CupertinoIcons" }) else {
    require(false, "Official CupertinoIcons font is missing or unreadable"); exit(1)
}
for name in ["animacao", "efeitos", "curva", "legenda"] {
    guard let preset = bundle.url(forResource: name, withExtension: "json", subdirectory: "presets"),
          let data = try? Data(contentsOf: preset),
          let list = (try? JSONSerialization.jsonObject(with: data)) as? [[String: Any]], !list.isEmpty else {
        require(false, "Bundled preset cannot be resolved: \(name)"); exit(1)
    }
}
require((bundle.object(forInfoDictionaryKey: "UIAppFonts") as? [String])?.contains("Fonts/Roboto-Regular.ttf") == true,
        "Android reference UI font is not registered in UIAppFonts")
guard let robotoURL = bundle.url(forResource: "Roboto-Regular", withExtension: "ttf", subdirectory: "Fonts"),
      let roboto = CTFontManagerCreateFontDescriptorsFromURL(robotoURL as CFURL) as? [CTFontDescriptor],
      roboto.contains(where: { CTFontDescriptorCopyAttribute($0, kCTFontNameAttribute) as? String == "Roboto-Regular" }) else {
    require(false, "Android reference font is missing or unreadable"); exit(1)
}
guard let photo = bundle.url(forResource: "previa_efeitos", withExtension: "jpg"),
      let source = CGImageSourceCreateWithURL(photo as CFURL, nil),
      CGImageSourceCreateImageAtIndex(source, 0, nil) != nil else {
    require(false, "Official effect preview photo is missing or unreadable"); exit(1)
}
print("Foundation validated bundle, icon font, preview photo and all four preset catalogs")
