// "ADD TB EFEITOS PRA CRIAR COLORING DE EDIT (PESQUISE)" (dono, 14/09/2026).
//
// Sete efeitos novos no shader (modos 37 a 43). Cada um tem a MESMA conta
// em Dart (domain/coloring.dart); aqui o shader e comparado com ela pixel
// a pixel, e cada efeito no valor inicial tem de devolver a imagem
// intacta — um coloring que tinge antes de alguem mexer nao serve.
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/coloring.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/effect_preset.dart';
import 'package:aurea/src/features/editor/domain/pixel_effect.dart';
import 'package:aurea/src/features/editor/presentation/widgets/pixel_effect_engine.dart';
import 'package:flutter_test/flutter_test.dart';

const _w = 64, _h = 32;

/// Rampa de cor opaca: vermelho cresce em x, verde em y, azul alterna.
Rgb _corDoFixture(int x, int y) => (
  r: (x * 255 ~/ (_w - 1)) / 255,
  g: (y * 255 ~/ (_h - 1)) / 255,
  b: (x % 8 < 4 ? 40 : 210) / 255,
);

Future<ui.Image> _fixture() async {
  final bytes = Uint8List(_w * _h * 4);
  for (var y = 0; y < _h; y++) {
    for (var x = 0; x < _w; x++) {
      final i = (y * _w + x) * 4;
      final c = _corDoFixture(x, y);
      bytes[i] = (c.r * 255).round();
      bytes[i + 1] = (c.g * 255).round();
      bytes[i + 2] = (c.b * 255).round();
      bytes[i + 3] = 255;
    }
  }
  final buffer = await ui.ImmutableBuffer.fromUint8List(bytes);
  final descriptor = ui.ImageDescriptor.raw(
    buffer,
    width: _w,
    height: _h,
    pixelFormat: ui.PixelFormat.rgba8888,
  );
  final codec = await descriptor.instantiateCodec();
  final image = (await codec.getNextFrame()).image;
  codec.dispose();
  descriptor.dispose();
  buffer.dispose();
  return image;
}

Future<Uint8List> _renderizar(ui.Image input, PixelEffectFrame frame) async {
  final recorder = ui.PictureRecorder();
  final canvas = ui.Canvas(recorder);
  final shader = PixelEffectEngine.createShader(
    frame,
    width: _w.toDouble(),
    height: _h.toDouble(),
    image: input,
  );
  canvas.drawRect(
    ui.Rect.fromLTWH(0, 0, _w.toDouble(), _h.toDouble()),
    ui.Paint()..shader = shader,
  );
  final picture = recorder.endRecording();
  final output = await picture.toImage(_w, _h);
  picture.dispose();
  shader.dispose();
  final data = await output.toByteData(
    format: ui.ImageByteFormat.rawStraightRgba,
  );
  output.dispose();
  return data!.buffer.asUint8List();
}

PixelEffectFrame _quadro(
  EffectType tipo,
  Map<String, double> valores, {
  List<double> cor = const [1, 1, 1, 1],
  List<double> extras = const [],
}) {
  final kernel = pixelKernels[tipo]!;
  final spec = effectSpecs[tipo]!;
  return PixelEffectFrame(
    kernel.mode,
    [for (final k in kernel.keys) valores[k] ?? spec.params[k]!.initial],
    color: cor,
    extraColors: extras,
  );
}

const _amostras = [
  (3, 2),
  (12, 9),
  (20, 30),
  (31, 16),
  (40, 5),
  (47, 24),
  (58, 12),
  (63, 31),
];

Future<void> _comparar(
  ui.Image entrada,
  PixelEffectFrame frame,
  Rgb Function(Rgb) referencia, {
  double tolerancia = 3.5,
  String nome = '',
}) async {
  final px = await _renderizar(entrada, frame);
  for (final (x, y) in _amostras) {
    final i = (y * _w + x) * 4;
    final esperado = referencia(_corDoFixture(x, y));
    final canais = [esperado.r, esperado.g, esperado.b];
    for (var c = 0; c < 3; c++) {
      expect(
        px[i + c].toDouble(),
        closeTo((canais[c].clamp(0.0, 1.0) * 255), tolerancia),
        reason: '$nome pixel ($x,$y) canal $c',
      );
    }
    expect(px[i + 3], 255, reason: '$nome alfa ($x,$y)');
  }
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUpAll(() async {
    await PixelEffectEngine.warmUp();
    expect(PixelEffectEngine.failure, isNull);
  });

  const novos = [
    EffectType.colorBalance,
    EffectType.selectiveColor,
    EffectType.channelMixer,
    EffectType.photoFilter,
    EffectType.gradientMap,
    EffectType.brightnessContrast,
    EffectType.colorTune,
  ];

  test('no valor inicial, nenhum dos sete mexe na imagem', () async {
    final entrada = await _fixture();
    for (final tipo in novos) {
      // O mapa de gradiente nasce com opacidade (e o coloring classico em
      // Soft Light); neutro para ele e opacidade zero.
      final valores = tipo == EffectType.gradientMap
          ? {'opacity': 0.0}
          : const <String, double>{};
      // O filtro de foto nasce com densidade 25 na cor laranja do
      // Warming Filter; neutro e densidade zero.
      final ajustados = tipo == EffectType.photoFilter
          ? {'density': 0.0}
          : valores;
      await _comparar(
        entrada,
        _quadro(tipo, ajustados),
        (c) => c,
        tolerancia: 1.5,
        nome: tipo.name,
      );
    }
    entrada.dispose();
  });

  test('Color Balance: shader = conta de referencia (com e sem preservar)', () async {
    final entrada = await _fixture();
    for (final preservar in [false, true]) {
      await _comparar(
        entrada,
        _quadro(EffectType.colorBalance, {
          'shadow_red': -12,
          'shadow_blue': 40,
          'midtone_green': 25,
          'midtone_red': -30,
          'highlight_red': 20,
          'highlight_blue': -15,
          'preserve_luminosity': preservar ? 1 : 0,
        }),
        (c) => colorBalance(
          c,
          shadowRed: -12,
          shadowBlue: 40,
          midtoneGreen: 25,
          midtoneRed: -30,
          highlightRed: 20,
          highlightBlue: -15,
          preserveLuminosity: preservar,
        ),
        nome: 'colorBalance preservar=$preservar',
      );
    }
    entrada.dispose();
  });

  test('Selective Color: as nove faixas batem com a referencia', () async {
    final entrada = await _fixture();
    for (var faixa = 0; faixa < 9; faixa++) {
      for (final relativo in [true, false]) {
        await _comparar(
          entrada,
          _quadro(EffectType.selectiveColor, {
            'colors': faixa.toDouble(),
            'cyan': 40,
            'magenta': -25,
            'yellow': 60,
            'black': 20,
            'method': relativo ? 0 : 1,
          }),
          (c) => selectiveColor(
            c,
            faixa: faixa,
            cyan: 40,
            magenta: -25,
            yellow: 60,
            black: 20,
            relative: relativo,
          ),
          nome: 'selectiveColor faixa=$faixa rel=$relativo',
        );
      }
    }
    entrada.dispose();
  });

  test('Channel Mixer troca canais e faz preto e branco', () async {
    final entrada = await _fixture();
    await _comparar(
      entrada,
      _quadro(EffectType.channelMixer, {
        'red_red': 0,
        'red_blue': 100,
        'blue_red': 100,
        'blue_blue': 0,
        'green_const': 10,
      }),
      (c) => (r: c.b, g: c.g + .1, b: c.r),
      nome: 'channelMixer troca',
    );
    await _comparar(
      entrada,
      _quadro(EffectType.channelMixer, {
        'monochrome': 1,
        'red_red': 50,
        'red_green': 40,
        'red_blue': 10,
      }),
      (c) {
        final y = c.r * .5 + c.g * .4 + c.b * .1;
        return (r: y, g: y, b: y);
      },
      nome: 'channelMixer mono',
    );
    entrada.dispose();
  });

  test('Photo Filter: cor e Kelvin batem com a referencia', () async {
    final entrada = await _fixture();
    const laranja = (r: 236 / 255, g: 138 / 255, b: 0.0);
    for (final preservar in [false, true]) {
      await _comparar(
        entrada,
        _quadro(
          EffectType.photoFilter,
          {'mode': 0, 'density': 60, 'preserve_luminosity': preservar ? 1 : 0},
          cor: [laranja.r, laranja.g, laranja.b, 1],
        ),
        (c) => photoFilter(
          c,
          temperatura: false,
          densidade: 60,
          kelvin: 6500,
          cor: laranja,
          preservarLuminosidade: preservar,
        ),
        nome: 'photoFilter cor preservar=$preservar',
      );
    }
    for (final k in [2500.0, 6500.0, 12000.0]) {
      await _comparar(
        entrada,
        _quadro(EffectType.photoFilter, {
          'mode': 1,
          'density': 100,
          'temperature': k,
        }),
        (c) => photoFilter(
          c,
          temperatura: true,
          densidade: 100,
          kelvin: k,
          cor: laranja,
        ),
        tolerancia: 4,
        nome: 'photoFilter $k K',
      );
    }
    entrada.dispose();
  });

  test('6500 K e neutro e Kelvin maior esfria', () {
    final neutro = kelvinGain(6500);
    expect(neutro.r, closeTo(1, 1e-9));
    expect(neutro.b, closeTo(1, 1e-9));
    final frio = kelvinGain(12000), quente = kelvinGain(3000);
    expect(frio.b, greaterThan(frio.r));
    expect(quente.r, greaterThan(quente.b));
  });

  test('Gradient Map: os sete modos batem com a referencia', () async {
    final entrada = await _fixture();
    const sombra = (r: 11 / 255, g: 42 / 255, b: 58 / 255);
    const meio = (r: 78 / 255, g: 171 / 255, b: 205 / 255);
    const luz = (r: 232 / 255, g: 244 / 255, b: 1.0);
    for (var modo = 0; modo < 7; modo++) {
      for (final meioTom in [true, false]) {
        await _comparar(
          entrada,
          _quadro(
            EffectType.gradientMap,
            {
              'blend_mode': modo.toDouble(),
              'opacity': 80,
              'midtones': meioTom ? 1 : 0,
              'balance': 40,
            },
            cor: [sombra.r, sombra.g, sombra.b, 1],
            extras: [meio.r, meio.g, meio.b, 1, luz.r, luz.g, luz.b, 1],
          ),
          (c) => gradientMap(
            c,
            sombra: sombra,
            meio: meio,
            luz: luz,
            modo: modo,
            opacidade: 80,
            meioTom: meioTom,
            pontoMedio: 40,
          ),
          nome: 'gradientMap modo=$modo meio=$meioTom',
        );
      }
    }
    entrada.dispose();
  });

  test('Brightness & Contrast e Color Tune batem com a referencia', () async {
    final entrada = await _fixture();
    for (final (b, ct) in [(30.0, 0.0), (-40.0, 50.0), (10.0, -60.0)]) {
      await _comparar(
        entrada,
        _quadro(EffectType.brightnessContrast, {
          'brightness': b,
          'contrast': ct,
        }),
        (c) => brightnessContrast(c, b, ct),
        nome: 'brightnessContrast $b $ct',
      );
    }
    await _comparar(
      entrada,
      _quadro(EffectType.colorTune, {
        'lift_hue': 190,
        'lift_saturation': 25,
        'lift_luminance': -0.03,
        'gamma_hue': 330,
        'gamma_saturation': 10,
        'gamma_luminance': 0.2,
        'gain_hue': 35,
        'gain_saturation': 20,
        'gain_luminance': 0.05,
        'offset_hue': 220,
        'offset_saturation': 30,
        'offset_luminance': -0.05,
      }),
      (c) => colorTune(
        c,
        lift: const RodaDeCor(matiz: 190, saturacao: 25, luminancia: -0.03),
        gamma: const RodaDeCor(matiz: 330, saturacao: 10, luminancia: 0.2),
        gain: const RodaDeCor(matiz: 35, saturacao: 20, luminancia: 0.05),
        offset: const RodaDeCor(matiz: 220, saturacao: 30, luminancia: -0.05),
      ),
      nome: 'colorTune',
    );
    entrada.dispose();
  });

  test('matrizes do caminho sem shader: as lineares sao exatas', () {
    // O filtro de cor limita a saida a 0..1, como a referencia.
    double l(double v) => v.clamp(0.0, 1.0);
    Rgb aplicar(List<double> m, Rgb c) => (
      r: l(m[0] * c.r + m[1] * c.g + m[2] * c.b + m[4] / 255),
      g: l(m[5] * c.r + m[6] * c.g + m[7] * c.b + m[9] / 255),
      b: l(m[10] * c.r + m[11] * c.g + m[12] * c.b + m[14] / 255),
    );
    const c = (r: .2, g: .55, b: .8);
    final bc = aplicar(brightnessContrastMatrix(-30, 40), c);
    final ref = brightnessContrast(c, -30, 40);
    expect(bc.r, closeTo(ref.r, 1e-9));
    expect(bc.g, closeTo(ref.g, 1e-9));
    expect(bc.b, closeTo(ref.b, 1e-9));

    final tune = aplicar(
      colorTuneMatrix(
        lift: const RodaDeCor(matiz: 10, saturacao: 40, luminancia: .1),
        gain: const RodaDeCor(matiz: 200, saturacao: 30, luminancia: -.2),
        offset: const RodaDeCor(matiz: 90, saturacao: 20, luminancia: .05),
      ),
      c,
    );
    final tuneRef = colorTune(
      c,
      lift: const RodaDeCor(matiz: 10, saturacao: 40, luminancia: .1),
      gain: const RodaDeCor(matiz: 200, saturacao: 30, luminancia: -.2),
      offset: const RodaDeCor(matiz: 90, saturacao: 20, luminancia: .05),
    );
    expect(tune.r, closeTo(tuneRef.r, 1e-9));
    expect(tune.g, closeTo(tuneRef.g, 1e-9));
    expect(tune.b, closeTo(tuneRef.b, 1e-9));
  });

  test('catalogo: busca por "coloring", cores de nascenca e pilhas prontas', () {
    final achados = searchEffects('coloring');
    for (final t in [
      EffectType.colorBalance,
      EffectType.selectiveColor,
      EffectType.channelMixer,
      EffectType.gradientMap,
      EffectType.colorTune,
    ]) {
      expect(achados, contains(t));
    }
    final mapa = EffectInstance(type: EffectType.gradientMap);
    expect(mapa.color, const ui.Color(0xFF0B2A3A));
    expect(mapa.extraColors, const [
      ui.Color(0xFF4EABCD),
      ui.Color(0xFFE8F4FF),
    ]);
    final pilhas = factoryPresets().where((p) => p.tags.contains('coloring'));
    expect(pilhas.length, greaterThanOrEqualTo(5));
    for (final p in pilhas) {
      expect(p.effects, isNotEmpty, reason: p.name);
    }
  });
}
