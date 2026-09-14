import 'dart:math' as math;

/// COLORING DE EDIT — as contas de referencia em CPU.
///
/// O desenho roda no shader (`shaders/effects_v2.frag`, modos 37 a 43).
/// Estas funcoes sao a MESMA conta em Dart, por pixel, e servem a duas
/// coisas: o teste compara o shader com elas pixel a pixel, e o caminho
/// sem shader (aparelho sem filtro de fragmento) usa a parte linear
/// delas como matriz de cor.
///
/// Todas recebem e devolvem RGB nao pre-multiplicado em 0..1.
///
/// Referencias publicas (as da Adobe nao sao publicadas):
///   * Color Balance: mascaras por luminosidade do GIMP
///     (gimpoperationcolorbalance), as mesmas do FFmpeg vf_colorbalance;
///   * Selective Color: FFmpeg vf_selectivecolor;
///   * Kelvin -> RGB: aproximacao de Tanner Helland;
///   * Soft Light, Overlay, Color, Luminosity: W3C Compositing Level 1.
typedef Rgb = ({double r, double g, double b});

double _c01(double v) => v.isNaN ? 0 : v.clamp(0.0, 1.0);

const _luma709 = (r: .2126, g: .7152, b: .0722);

double _lum709(Rgb c) =>
    c.r * _luma709.r + c.g * _luma709.g + c.b * _luma709.b;

// ------------------------------------------------------------ W3C

double lumW3c(Rgb c) => .3 * c.r + .59 * c.g + .11 * c.b;

Rgb clipColor(Rgb c) {
  final l = lumW3c(c);
  final n = math.min(c.r, math.min(c.g, c.b));
  final x = math.max(c.r, math.max(c.g, c.b));
  var r = c.r, g = c.g, b = c.b;
  if (n < 0) {
    final k = l / math.max(l - n, 1e-5);
    r = l + (r - l) * k;
    g = l + (g - l) * k;
    b = l + (b - l) * k;
  }
  if (x > 1) {
    final k = (1 - l) / math.max(x - l, 1e-5);
    r = l + (r - l) * k;
    g = l + (g - l) * k;
    b = l + (b - l) * k;
  }
  return (r: r, g: g, b: b);
}

Rgb setLum(Rgb c, double l) {
  final d = l - lumW3c(c);
  return clipColor((r: c.r + d, g: c.g + d, b: c.b + d));
}

double softLight(double cb, double cs) {
  final d = cb <= .25 ? ((16 * cb - 12) * cb + 4) * cb : math.sqrt(cb);
  return cs <= .5 ? cb - (1 - 2 * cs) * cb * (1 - cb) : cb + (2 * cs - 1) * (d - cb);
}

double overlay(double cb, double cs) =>
    cb <= .5 ? 2 * cb * cs : 1 - 2 * (1 - cb) * (1 - cs);

// ------------------------------------------------------- HSL

({double h, double s, double l}) rgbToHsl(Rgb c) {
  final mx = math.max(c.r, math.max(c.g, c.b));
  final mn = math.min(c.r, math.min(c.g, c.b));
  final l = (mx + mn) / 2;
  final d = mx - mn;
  if (d <= 1e-5) return (h: 0, s: 0, l: l);
  final s = l > .5 ? d / math.max(2 - mx - mn, 1e-5) : d / math.max(mx + mn, 1e-5);
  double h;
  if (mx == c.r) {
    h = (c.g - c.b) / d + (c.g < c.b ? 6 : 0);
  } else if (mx == c.g) {
    h = (c.b - c.r) / d + 2;
  } else {
    h = (c.r - c.g) / d + 4;
  }
  return (h: h / 6, s: s, l: l);
}

double _rampa(double h, double n) {
  final v = ((h * 6 + n) % 6 - 3).abs() - 1;
  return v.clamp(0.0, 1.0);
}

Rgb hslToRgb(double h, double s, double l) {
  final ch = (1 - (2 * l - 1).abs()) * s;
  return (
    r: l + ch * (_rampa(h, 0) - .5),
    g: l + ch * (_rampa(h, 4) - .5),
    b: l + ch * (_rampa(h, 2) - .5),
  );
}

// --------------------------------------------------- COLOR BALANCE

double _balanco(double v, double l, double s, double m, double h) {
  const a = 4.0, b = .333, escala = .7;
  s *= ((b - l) * a + .5).clamp(0.0, 1.0) * escala;
  m *= ((l - b) * a + .5).clamp(0.0, 1.0) *
      ((1 - l - b) * a + .5).clamp(0.0, 1.0) *
      escala;
  h *= ((l + b - 1) * a + .5).clamp(0.0, 1.0) * escala;
  return _c01(v + s + m + h);
}

/// Valores em -100..100, como na ficha.
Rgb colorBalance(
  Rgb c, {
  double shadowRed = 0,
  double shadowGreen = 0,
  double shadowBlue = 0,
  double midtoneRed = 0,
  double midtoneGreen = 0,
  double midtoneBlue = 0,
  double highlightRed = 0,
  double highlightGreen = 0,
  double highlightBlue = 0,
  bool preserveLuminosity = false,
}) {
  final mx = math.max(c.r, math.max(c.g, c.b));
  final mn = math.min(c.r, math.min(c.g, c.b));
  final l = (mx + mn) / 2;
  final o = (
    r: _balanco(c.r, l, shadowRed / 100, midtoneRed / 100, highlightRed / 100),
    g: _balanco(
      c.g,
      l,
      shadowGreen / 100,
      midtoneGreen / 100,
      highlightGreen / 100,
    ),
    b: _balanco(
      c.b,
      l,
      shadowBlue / 100,
      midtoneBlue / 100,
      highlightBlue / 100,
    ),
  );
  if (!preserveLuminosity) return o;
  final hsl = rgbToHsl(o);
  return hslToRgb(hsl.h, hsl.s, l);
}

// -------------------------------------------------- SELECTIVE COLOR

/// [faixa]: 0 vermelhos, 1 amarelos, 2 verdes, 3 cianos, 4 azuis,
/// 5 magentas, 6 brancos, 7 neutros, 8 pretos. Ajustes em -100..100.
Rgb selectiveColor(
  Rgb c, {
  required int faixa,
  double cyan = 0,
  double magenta = 0,
  double yellow = 0,
  double black = 0,
  bool relative = true,
}) {
  final mx = math.max(c.r, math.max(c.g, c.b));
  final mn = math.min(c.r, math.min(c.g, c.b));
  final md = c.r + c.g + c.b - mx - mn;
  final (bool dentro, double escala) = switch (faixa) {
    0 => (c.r >= mx, mx - md),
    1 => (c.b <= mn, md - mn),
    2 => (c.g >= mx, mx - md),
    3 => (c.r <= mn, md - mn),
    4 => (c.b >= mx, mx - md),
    5 => (c.g <= mn, md - mn),
    6 => (mn > .5, 2 * mn - 1),
    7 => (mx > 0 && mn < 1, 1 - ((2 * mx - 1).abs() + (2 * mn - 1).abs()) / 2),
    _ => (mx < .5, 1 - 2 * mx),
  };
  if (!dentro || escala <= 0) return c;
  final k = black / 100;
  double ajuste(double v, double a) {
    var res = (-1 - a) * k - a;
    if (relative) res *= 1 - v;
    return res.clamp(-v, 1 - v) * escala;
  }

  return (
    r: _c01(c.r + ajuste(c.r, cyan / 100)),
    g: _c01(c.g + ajuste(c.g, magenta / 100)),
    b: _c01(c.b + ajuste(c.b, yellow / 100)),
  );
}

// --------------------------------------------------- CHANNEL MIXER

/// A matriz 4x5 de cor (linhas R, G, B, A) do Channel Mixer, com o
/// deslocamento em 0..255 como o `ColorFilter.matrix` pede. E exata: a
/// conta e linear.
List<double> channelMixerMatrix({
  required List<double> vermelho, // rr, rg, rb, rc em %
  required List<double> verde,
  required List<double> azul,
  bool monocromatico = false,
}) {
  List<double> linha(List<double> v) => [
    for (var i = 0; i < 3; i++) v[i].clamp(-200.0, 200.0) / 100,
    0,
    v[3].clamp(-200.0, 200.0) / 100 * 255,
  ];
  final r = linha(vermelho);
  final g = monocromatico ? r : linha(verde);
  final b = monocromatico ? r : linha(azul);
  return [...r, ...g, ...b, 0, 0, 0, 1, 0];
}

// ------------------------------------------------ PHOTO FILTER / K

/// Kelvin -> RGB (Tanner Helland), 0..1.
Rgb kelvinToRgb(double kelvin) {
  final t = kelvin.clamp(1000.0, 40000.0) / 100;
  final r = t <= 66
      ? 1.0
      : (329.698727446 * math.pow(t - 60, -0.1332047592) / 255).clamp(0.0, 1.0);
  final g = t <= 66
      ? ((99.4708025861 * math.log(t) - 161.1195681661) / 255).clamp(0.0, 1.0)
      : (288.1221695283 * math.pow(t - 60, -0.0755148492) / 255).clamp(0.0, 1.0);
  final b = t >= 66
      ? 1.0
      : t <= 19
      ? 0.0
      : ((138.5177312231 * math.log(t - 10) - 305.0447927307) / 255).clamp(
          0.0,
          1.0,
        );
  return (r: r.toDouble(), g: g.toDouble(), b: b.toDouble());
}

/// Ganho por canal que leva a luz de 6500 K para [kelvin], com
/// luminancia 1 (esquentar ou esfriar nao escurece).
Rgb kelvinGain(double kelvin) {
  final alvo = kelvinToRgb(kelvin);
  final base = kelvinToRgb(6500);
  var g = (
    r: alvo.r / math.max(base.r, 1e-4),
    g: alvo.g / math.max(base.g, 1e-4),
    b: alvo.b / math.max(base.b, 1e-4),
  );
  final y = math.max(_lum709(g), 1e-4);
  g = (r: g.r / y, g: g.g / y, b: g.b / y);
  return g;
}

double _paraLinear(double c) => c <= .04045
    ? c / 12.92
    : math.pow((c + .055) / 1.055, 2.4).toDouble();

double _paraSrgb(double c) {
  c = math.max(c, 0);
  return c <= .0031308
      ? c * 12.92
      : 1.055 * math.pow(c, 1 / 2.4).toDouble() - .055;
}

Rgb photoFilter(
  Rgb c, {
  required bool temperatura,
  required double densidade, // %
  required double kelvin,
  required Rgb cor,
  bool preservarLuminosidade = true,
}) {
  final d = (densidade / 100).clamp(0.0, 1.0);
  if (!temperatura) {
    final m = (
      r: c.r + (c.r * cor.r - c.r) * d,
      g: c.g + (c.g * cor.g - c.g) * d,
      b: c.b + (c.b * cor.b - c.b) * d,
    );
    return preservarLuminosidade ? setLum(m, lumW3c(c)) : m;
  }
  final g = kelvinGain(kelvin);
  final lin = (
    r: _paraLinear(_c01(c.r)),
    g: _paraLinear(_c01(c.g)),
    b: _paraLinear(_c01(c.b)),
  );
  var w = (
    r: lin.r + (lin.r * g.r - lin.r) * d,
    g: lin.g + (lin.g * g.g - lin.g) * d,
    b: lin.b + (lin.b * g.b - lin.b) * d,
  );
  if (preservarLuminosidade) {
    final k = _lum709(lin) / math.max(_lum709(w), 1e-4);
    w = (r: w.r * k, g: w.g * k, b: w.b * k);
  }
  return (r: _paraSrgb(w.r), g: _paraSrgb(w.g), b: _paraSrgb(w.b));
}

// --------------------------------------------------- GRADIENT MAP

Rgb gradientMap(
  Rgb c, {
  required Rgb sombra,
  required Rgb meio,
  required Rgb luz,
  int modo = 1,
  double opacidade = 100,
  bool meioTom = true,
  double pontoMedio = 50,
}) {
  final y = (.299 * c.r + .587 * c.g + .114 * c.b).clamp(0.0, 1.0);
  final mid = (pontoMedio / 100).clamp(.05, .95);
  Rgb mistura(Rgb a, Rgb b, double t) => (
    r: a.r + (b.r - a.r) * t,
    g: a.g + (b.g - a.g) * t,
    b: a.b + (b.b - a.b) * t,
  );
  final g = meioTom
      ? (y < mid
            ? mistura(sombra, meio, y / mid)
            : mistura(meio, luz, (y - mid) / (1 - mid)))
      : mistura(sombra, luz, y);
  final Rgb b = switch (modo) {
    1 => (r: softLight(c.r, g.r), g: softLight(c.g, g.g), b: softLight(c.b, g.b)),
    2 => (r: overlay(c.r, g.r), g: overlay(c.g, g.g), b: overlay(c.b, g.b)),
    3 => (r: c.r * g.r, g: c.g * g.g, b: c.b * g.b),
    4 => (
      r: c.r + g.r - c.r * g.r,
      g: c.g + g.g - c.g * g.g,
      b: c.b + g.b - c.b * g.b,
    ),
    5 => setLum(g, lumW3c(c)),
    6 => setLum(c, lumW3c(g)),
    _ => g,
  };
  final o = (opacidade / 100).clamp(0.0, 1.0);
  return mistura(c, (r: _c01(b.r), g: _c01(b.g), b: _c01(b.b)), o);
}

// ---------------------------------------------- BRIGHTNESS/CONTRAST

/// Matriz exata do Brightness & Contrast (as duas contas sao lineares).
List<double> brightnessContrastMatrix(double brilho, double contraste) {
  final br = (brilho / 100).clamp(-1.0, 1.0);
  final ct = (contraste / 100).clamp(-1.0, 3.0);
  final a = br >= 0 ? 1 - br : 1 + br;
  final b = br >= 0 ? br : 0.0;
  final k = 1 + ct;
  final ganho = a * k;
  final desloc = ((b - .5) * k + .5) * 255;
  return [
    ganho, 0, 0, 0, desloc, //
    0, ganho, 0, 0, desloc,
    0, 0, ganho, 0, desloc,
    0, 0, 0, 1, 0,
  ];
}

Rgb brightnessContrast(Rgb c, double brilho, double contraste) {
  final br = (brilho / 100).clamp(-1.0, 1.0);
  final ct = (contraste / 100).clamp(-1.0, 3.0);
  double canal(double v) {
    final v1 = br >= 0 ? v + br * (1 - v) : v * (1 + br);
    return _c01((v1 - .5) * (1 + ct) + .5);
  }

  return (r: canal(c.r), g: canal(c.g), b: canal(c.b));
}

// ------------------------------------------------------ COLOR TUNE

/// O vetor de cor de uma roda: matiz em graus, saturacao em %, com
/// luminancia zero (girar a matiz tinge sem clarear).
Rgb tuneWheel(double matizGraus, double saturacao) {
  final h = (matizGraus % 360) / 360;
  final k = (r: _rampa(h, 0), g: _rampa(h, 4), b: _rampa(h, 2));
  final y = _lum709(k);
  final s = (saturacao / 100).clamp(0.0, 1.0);
  return (r: s * (k.r - y), g: s * (k.g - y), b: s * (k.b - y));
}

class RodaDeCor {
  const RodaDeCor({this.matiz = 0, this.saturacao = 0, this.luminancia = 0});
  final double matiz, saturacao, luminancia;
}

Rgb colorTune(
  Rgb c, {
  RodaDeCor lift = const RodaDeCor(),
  RodaDeCor gamma = const RodaDeCor(),
  RodaDeCor gain = const RodaDeCor(),
  RodaDeCor offset = const RodaDeCor(),
}) {
  double canal(double v, int i) {
    double comp(Rgb w) => switch (i) {
      0 => w.r,
      1 => w.g,
      _ => w.b,
    };
    var x =
        v + .5 * (offset.luminancia + comp(tuneWheel(offset.matiz, offset.saturacao)));
    x *= 1 + gain.luminancia + comp(tuneWheel(gain.matiz, gain.saturacao));
    x += .5 * (lift.luminancia + comp(tuneWheel(lift.matiz, lift.saturacao))) * (1 - x);
    final expoente = math
        .pow(2, -(gamma.luminancia + comp(tuneWheel(gamma.matiz, gamma.saturacao))))
        .toDouble();
    return _c01(math.pow(math.max(x, 0), expoente).toDouble());
  }

  return (r: canal(c.r, 0), g: canal(c.g, 1), b: canal(c.b, 2));
}

/// A PARTE LINEAR do Color Tune como matriz (offset, gain e lift saem
/// exatos; o gamma nao cabe numa matriz e fica de fora). So para o
/// caminho sem shader.
List<double> colorTuneMatrix({
  required RodaDeCor lift,
  required RodaDeCor gain,
  required RodaDeCor offset,
}) {
  final wo = tuneWheel(offset.matiz, offset.saturacao);
  final wg = tuneWheel(gain.matiz, gain.saturacao);
  final wl = tuneWheel(lift.matiz, lift.saturacao);
  List<double> linha(int i, double o, double g, double l) {
    // x = ((v + .5*O) * G) ; x = x*(1 - .5L) + .5L
    final escala = g * (1 - .5 * l);
    final desloc = (.5 * o * g * (1 - .5 * l) + .5 * l) * 255;
    return [
      for (var j = 0; j < 3; j++) j == i ? escala : 0.0,
      0,
      desloc,
    ];
  }

  return [
    ...linha(0, offset.luminancia + wo.r, 1 + gain.luminancia + wg.r, lift.luminancia + wl.r),
    ...linha(1, offset.luminancia + wo.g, 1 + gain.luminancia + wg.g, lift.luminancia + wl.g),
    ...linha(2, offset.luminancia + wo.b, 1 + gain.luminancia + wg.b, lift.luminancia + wl.b),
    0, 0, 0, 1, 0,
  ];
}

/// Matriz aproximada do Color Balance para o caminho sem shader: cada
/// faixa pesa o que pesaria num cinza medio.
List<double> colorBalanceMatrix({
  required List<double> sombras, // r, g, b em -100..100
  required List<double> meios,
  required List<double> altas,
  bool preservarLuminosidade = false,
}) {
  double t(double v) => v.clamp(-100.0, 100.0);
  final bruto = [
    for (var i = 0; i < 3; i++)
      (t(sombras[i]) * .2 + t(meios[i]) * .7 + t(altas[i]) * .2) / 100 * .7,
  ];
  // Preservar: tira do deslocamento a parte que clareia ou escurece,
  // fica so a que tinge.
  final y = preservarLuminosidade
      ? bruto[0] * .2126 + bruto[1] * .7152 + bruto[2] * .0722
      : 0.0;
  double d(int i) => (bruto[i] - y) * 255;
  return [
    1, 0, 0, 0, d(0), //
    0, 1, 0, 0, d(1),
    0, 0, 1, 0, d(2),
    0, 0, 0, 1, 0,
  ];
}

/// Matriz do Photo Filter para o caminho sem shader: o ganho por canal
/// (cor ou Kelvin) na densidade pedida, sem a preservacao de brilho.
List<double> photoFilterMatrix({
  required bool temperatura,
  required double densidade,
  required double kelvin,
  required Rgb cor,
}) {
  final d = (densidade / 100).clamp(0.0, 1.0);
  final g = temperatura ? kelvinGain(kelvin) : cor;
  double k(double v) => 1 + (v - 1) * d;
  return [
    k(g.r), 0, 0, 0, 0, //
    0, k(g.g), 0, 0, 0,
    0, 0, k(g.b), 0, 0,
    0, 0, 0, 1, 0,
  ];
}

/// Matriz do Gradient Map no modo Normal com duas paradas (sombra e
/// luz): a luminancia mapeada e linear, entao a matriz e exata para
/// esse caso; os outros modos ficam aproximados por ele.
List<double> gradientMapMatrix({
  required Rgb sombra,
  required Rgb luz,
  required double opacidade,
}) {
  final o = (opacidade / 100).clamp(0.0, 1.0);
  List<double> linha(int i, double s, double l) => [
    for (var j = 0; j < 3; j++)
      (j == i ? 1 - o : 0.0) + o * (l - s) * const [.299, .587, .114][j],
    0,
    o * s * 255,
  ];
  return [
    ...linha(0, sombra.r, luz.r),
    ...linha(1, sombra.g, luz.g),
    ...linha(2, sombra.b, luz.b),
    0, 0, 0, 1, 0,
  ];
}
