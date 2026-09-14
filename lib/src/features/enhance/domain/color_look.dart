import 'dart:math' as math;

import '../../editor/domain/aprimoramento_ia.dart';

/// Original color recipes. No Adobe or third-party preset files are required.
enum ColorLook {
  natural('Natural'),
  cinema('Cinema'),
  warm('Dourado'),
  cool('Frio'),
  vintage('Vintage'),
  vivid('Vibrante'),
  mono('Preto e branco'),
  copper('Cobre dramático'),
  cleanPortrait('Retrato limpo'),
  lilac('Cinema lilás'),
  blueMatte('Azul fosco'),
  sports('Esporte nítido'),
  electricBlue('Azul elétrico'),
  flash('Flash'),
  goldGreen('Verde e ouro');

  const ColorLook(this.label);
  final String label;
  double get vignette => switch (this) {
    copper => .38,
    lilac => .24,
    flash => .22,
    goldGreen => .12,
    _ => 0,
  };
  double get clarity => switch (this) {
    copper => .3,
    sports => .4,
    electricBlue => .28,
    flash => .2,
    goldGreen => .2,
    _ => 0,
  };

  (double, double, double) apply(
    double r,
    double g,
    double b,
    double strength,
  ) {
    final source = (r, g, b);
    final l = .2126 * r + .7152 * g + .0722 * b;
    void saturation(double s) {
      r = l + (r - l) * s;
      g = l + (g - l) * s;
      b = l + (b - l) * s;
    }

    switch (this) {
      case natural:
        break;
      case cinema:
        saturation(.88);
        final shadows = 1 - l / 255;
        r += 12 * (l / 255) - 8 * shadows;
        g += 3 * shadows;
        b += 12 * shadows - 6 * (l / 255);
      case warm:
        r += 12;
        g += 3;
        b -= 12;
      case cool:
        r -= 8;
        g += 2;
        b += 13;
      case vintage:
        saturation(.75);
        r = r * .9 + 22;
        g = g * .9 + 15;
        b = b * .8 + 18;
      case vivid:
        saturation(1.22);
        r = (r - 128) * 1.08 + 128;
        g = (g - 128) * 1.08 + 128;
        b = (b - 128) * 1.08 + 128;
      case mono:
        r = l;
        g = l;
        b = l;
      case copper:
        saturation(1.08);
        r = (r - 110) * 1.18 + 121;
        g = (g - 110) * 1.12 + 108;
        b = (b - 110) * 1.08 + 99;
      case cleanPortrait:
        saturation(.9);
        r = (r - 128) * 1.03 + 133;
        g = (g - 128) * 1.03 + 133;
        b = (b - 128) * 1.03 + 130;
      case lilac:
        saturation(.82);
        r = r * .96 + 10;
        g = g * .93 + 3;
        b = b * .97 + 15;
      case blueMatte:
        saturation(.65);
        r = r * .84 + 20;
        g = g * .86 + 23;
        b = b * .9 + 28;
      case sports:
        saturation(1.17);
        r = (r - 128) * 1.14 + 128;
        g = (g - 128) * 1.12 + 129;
        b = (b - 128) * 1.1 + 128;
      case electricBlue:
        saturation(1.2);
        r = (r - 128) * 1.1 + 121;
        g = (g - 128) * 1.1 + 129;
        b = (b - 128) * 1.16 + 148;
      case flash:
        saturation(.9);
        r = (r - 110) * 1.18 + 113;
        g = (g - 110) * 1.18 + 112;
        b = (b - 110) * 1.2 + 117;
      case goldGreen:
        saturation(1.2);
        r = r * 1.08 + 5;
        g = g * 1.06 + 6;
        b = b * .88 - 3;
    }
    final s = strength.clamp(0.0, 1.0);
    double mix(double a, double v) => (a + (v - a) * s).clamp(0.0, 255.0);
    return (mix(source.$1, r), mix(source.$2, g), mix(source.$3, b));
  }
}

class EnhanceSettings {
  const EnhanceSettings({
    this.ai = true,
    this.scale = 2,
    this.look = ColorLook.natural,
    this.strength = 1,
    this.aiStrength = 1,
    this.detail = 0,
    this.perfil = PerfilDoAprimoramento.videoReal,
    this.reducaoDeRuido = reducaoDeRuidoPadrao,
  });
  final bool ai;

  /// Qual rede: video real (padrao) ou animacao.
  final PerfilDoAprimoramento perfil;

  /// So no video real: a mistura dos dois modelos (0 preserva o grao, 1
  /// limpa forte). Ver ae_create_dni.
  final double reducaoDeRuido;

  /// Escala de SAIDA: 1 (restaurar sem ampliar), 2 ou 4. O modelo e x4; 1x
  /// e 2x sao a inferencia x4 reduzida por area (o custo e o do x4).
  final int scale;
  final ColorLook look;

  /// Intensidade do VISUAL de cor (CCs).
  final double strength;

  /// Intensidade da IA: mistura a saida da rede com o original ampliado de
  /// forma convencional. Nao e um denoise.
  final double aiStrength;

  /// Nitidez (unsharp) aplicada depois. Nao e IA.
  final double detail;

  (int, int) outputSize(int w, int h, {bool video = false}) {
    final s = ai ? scale.clamp(1, 4) : 1;
    final width = math.max(1, w * s), height = math.max(1, h * s);
    return video
        ? (math.max(2, width ~/ 2 * 2), math.max(2, height ~/ 2 * 2))
        : (width, height);
  }
}
