import SwiftUI

/// Miniatura dos modos de máscara da mescla (Alight Motion) — espelho de
/// BlendCutThumb.kt. Máscara (BlendMode 22): a IMAGEM só dentro do disco, o
/// xadrez da transparência fora. Excluir (23): a imagem com o disco furado.
struct BlendCutThumb: View {
    let exclude: Bool
    var body: some View {
        Canvas { context, size in
            let r = size.height * 0.40
            let disc = Path(ellipseIn: CGRect(x: size.width / 2 - r, y: size.height / 2 - r, width: r * 2, height: r * 2))
            if exclude {
                Self.picture(&context, size)
                var inner = context
                inner.clip(to: disc)
                Self.checker(&inner, size)
            } else {
                Self.checker(&context, size)
                var inner = context
                inner.clip(to: disc)
                Self.picture(&inner, size)
            }
        }
        .frame(width: 34, height: 22)
        .accessibilityLabel(AureaText.t(exclude ? "pn_blend_exclude_desc" : "pn_blend_mask_desc"))
    }

    /// Xadrez cinza da transparência.
    private static func checker(_ context: inout GraphicsContext, _ size: CGSize) {
        let cell = size.height / 5
        context.fill(Path(CGRect(origin: .zero, size: size)), with: .color(Color(red: 0.74, green: 0.74, blue: 0.74)))
        var y = 0
        while CGFloat(y) * cell < size.height {
            var x = 0
            while CGFloat(x) * cell < size.width {
                if (x + y) % 2 == 0 {
                    context.fill(Path(CGRect(x: CGFloat(x) * cell, y: CGFloat(y) * cell, width: cell, height: cell)),
                                 with: .color(Color(red: 0.54, green: 0.54, blue: 0.54)))
                }
                x += 1
            }
            y += 1
        }
    }

    /// "Foto" esquemática: céu em degradê, sol e um morro.
    private static func picture(_ context: inout GraphicsContext, _ size: CGSize) {
        let rect = CGRect(origin: .zero, size: size)
        context.fill(Path(rect), with: .linearGradient(
            Gradient(colors: [Color(red: 0.24, green: 0.61, blue: 1.0), Color(red: 1.0, green: 0.76, blue: 0.48)]),
            startPoint: .zero, endPoint: CGPoint(x: 0, y: size.height)))
        let sun = size.height * 0.16
        context.fill(Path(ellipseIn: CGRect(x: size.width * 0.72 - sun, y: size.height * 0.32 - sun, width: sun * 2, height: sun * 2)),
                     with: .color(Color(red: 1.0, green: 0.88, blue: 0.4)))
        var hill = Path()
        hill.move(to: CGPoint(x: 0, y: size.height))
        hill.addLine(to: CGPoint(x: 0, y: size.height * 0.70))
        hill.addQuadCurve(to: CGPoint(x: size.width * 0.70, y: size.height * 0.72), control: CGPoint(x: size.width * 0.35, y: size.height * 0.38))
        hill.addLine(to: CGPoint(x: size.width, y: size.height * 0.62))
        hill.addLine(to: CGPoint(x: size.width, y: size.height))
        hill.closeSubpath()
        context.fill(hill, with: .color(Color(red: 0.18, green: 0.62, blue: 0.36)))
    }
}
