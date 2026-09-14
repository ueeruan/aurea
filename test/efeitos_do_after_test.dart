// "ADICIONE ESSES EFEITOS" (dono, 14/09/2026): a camada de ajuste de um
// edit no After Effects com Magic Bullet Looks, S_Sharpen, S_Flicker,
// S_MathOps, S_FilmDamage 2, Hue/Saturation e Brightness & Contrast.
//
// Cada efeito novo tem de: devolver a imagem intacta nos valores neutros
// (pixel a pixel, pelo shader), bater com a conta de referencia em Dart
// (domain/efeitos_do_after.dart) e ser achado pela busca como a pessoa
// digita — com ou sem acento.
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/coloring.dart' show Rgb;
import 'package:aurea/src/features/editor/domain/efeitos_do_after.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/pixel_effect.dart';
import 'package:aurea/src/features/editor/presentation/widgets/pixel_effect_engine.dart';
import 'package:flutter_test/flutter_test.dart';

const _w = 64, _h = 32;

/// Rampa de cor opaca: vermelho cresce em x, verde em y, azul alterna.
Rgb _rampa(int x, int y) => (
  r: (x * 255 ~/ (_w - 1)) / 255,
  g: (y * 255 ~/ (_h - 1)) / 255,
  b: (x % 8 < 4 ? 40 : 210) / 255,
);

Future<ui.Image> _imagem(Rgb Function(int x, int y) cor) async {
  final bytes = Uint8List(_w * _h * 4);
  for (var y = 0; y < _h; y++) {
    for (var x = 0; x < _w; x++) {
      final i = (y * _w + x) * 4;
      final c = cor(x, y);
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

Future<Uint8List> _renderizar(ui.Image entrada, PixelEffectFrame frame) async {
  final recorder = ui.PictureRecorder();
  final shader = PixelEffectEngine.createShader(
    frame,
    width: _w.toDouble(),
    height: _h.toDouble(),
    image: entrada,
  );
  ui.Canvas(recorder).drawRect(
    ui.Rect.fromLTWH(0, 0, _w.toDouble(), _h.toDouble()),
    ui.Paint()..shader = shader,
  );
  final picture = recorder.endRecording();
  final saida = await picture.toImage(_w, _h);
  picture.dispose();
  shader.dispose();
  final dados = await saida.toByteData(
    format: ui.ImageByteFormat.rawStraightRgba,
  );
  saida.dispose();
  return dados!.buffer.asUint8List();
}

/// O quadro pelo MESMO caminho da previa e da exportacao: a instancia do
/// efeito com os valores pedidos (o resto no inicial da ficha) avaliada
/// por [PixelEffectFrame.of], que limita a faixa e monta os slots.
PixelEffectFrame _quadro(
  EffectType tipo,
  Map<String, double> valores, {
  Duration tempo = Duration.zero,
}) => PixelEffectFrame.of(
  EffectInstance(
    type: tipo,
    params: {for (final e in valores.entries) e.key: AnimatedDouble(e.value)},
  ),
  tempo,
);

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
  Rgb Function(int x, int y) fonte,
  PixelEffectFrame frame,
  Rgb Function(Rgb) referencia, {
  double tolerancia = 3.5,
  String nome = '',
}) async {
  final px = await _renderizar(entrada, frame);
  for (final (x, y) in _amostras) {
    final i = (y * _w + x) * 4;
    final esperado = referencia(fonte(x, y));
    final canais = [esperado.r, esperado.g, esperado.b];
    for (var c = 0; c < 3; c++) {
      expect(
        px[i + c].toDouble(),
        closeTo(canais[c].clamp(0.0, 1.0) * 255, tolerancia),
        reason: '$nome pixel ($x,$y) canal $c',
      );
    }
    expect(px[i + 3], 255, reason: '$nome alfa ($x,$y)');
  }
}

/// Aplica uma matriz 4x5 de cor como o `ColorFilter.matrix` faz.
Rgb _aplicarMatriz(List<double> m, Rgb c) {
  double l(double v) => v.clamp(0.0, 1.0);
  return (
    r: l(m[0] * c.r + m[1] * c.g + m[2] * c.b + m[4] / 255),
    g: l(m[5] * c.r + m[6] * c.g + m[7] * c.b + m[9] / 255),
    b: l(m[10] * c.r + m[11] * c.g + m[12] * c.b + m[14] / 255),
  );
}

void _mesmaCor(Rgb a, Rgb b, {double tol = 1e-9, String nome = ''}) {
  expect(a.r, closeTo(b.r, tol), reason: '$nome r');
  expect(a.g, closeTo(b.g, tol), reason: '$nome g');
  expect(a.b, closeTo(b.b, tol), reason: '$nome b');
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUpAll(() async {
    await PixelEffectEngine.warmUp();
    expect(PixelEffectEngine.failure, isNull);
    expect(PixelEffectEngine.ready, isTrue);
  });

  group('busca', () {
    test('ignora acento e caixa dos dois lados', () {
      expect(normalizarBusca('Saturação'), 'saturacao');
      expect(normalizarBusca('PARTÍCULAS'), 'particulas');
      // "partículas" nao achava o sinonimo "particulas".
      expect(searchEffects('partículas'), contains(EffectType.ccScatterize));
      expect(searchEffects('PARTICULAS'), contains(EffectType.ccScatterize));
      expect(searchEffects('oscilacao'), contains(EffectType.oscillate));
      expect(searchEffects('Oscilação'), contains(EffectType.oscillate));
      expect(searchEffects('zzzz'), isEmpty);
    });

    test('Brightness & Contrast pelo nome, em portugues e por "brilho"', () {
      for (final termo in [
        'Brightness & Contrast',
        'brightness & contrast',
        'brilho e contraste',
        'Brilho e Contraste',
        'brilho',
      ]) {
        expect(
          searchEffects(termo),
          contains(EffectType.brightnessContrast),
          reason: termo,
        );
      }
    });

    test('os efeitos novos aparecem pelo nome do After e em portugues', () {
      void achou(String termo, EffectType alvo) => expect(
        searchEffects(termo),
        contains(alvo),
        reason: 'buscar "$termo" nao achou ${alvo.name}',
      );

      achou('Hue/Saturation', EffectType.hueSaturation);
      achou('hue saturation', EffectType.hueSaturation);
      achou('saturação', EffectType.hueSaturation);
      achou('SATURACAO', EffectType.hueSaturation);
      achou('colorir', EffectType.hueSaturation);
    });
  });

  group('catalogo', () {
    test('categorias, atalho Edits e contrato do shader', () {
      expect(effectSpecs[EffectType.hueSaturation]!.category, 'Color');
      expect(efeitosDeEdit, contains(EffectType.hueSaturation));
      expect(pixelKernels[EffectType.hueSaturation]!.mode, 45);
    });
  });

  group('Hue/Saturation', () {
    test('nos valores iniciais a imagem sai intacta', () async {
      final entrada = await _imagem(_rampa);
      await _comparar(
        entrada,
        _rampa,
        _quadro(EffectType.hueSaturation, const {}),
        (c) => c,
        tolerancia: 1.5,
        nome: 'hueSaturation neutro',
      );
      entrada.dispose();
    });

    test('shader = conta de referencia', () async {
      final entrada = await _imagem(_rampa);
      const casos = <Map<String, double>>[
        {'master_hue': 90},
        {'master_hue': -45, 'master_saturation': 40, 'master_lightness': -20},
        {'master_saturation': -100},
        {'master_saturation': 100},
        {'master_saturation': 60, 'master_lightness': 35},
        {
          'colorize': 1,
          'colorize_hue': 35,
          'colorize_saturation': 30,
          'master_lightness': 10,
        },
      ];
      for (final caso in casos) {
        await _comparar(
          entrada,
          _rampa,
          _quadro(EffectType.hueSaturation, caso),
          (c) => hueSaturation(
            c,
            matiz: caso['master_hue'] ?? 0,
            saturacao: caso['master_saturation'] ?? 0,
            luminosidade: caso['master_lightness'] ?? 0,
            colorir: (caso['colorize'] ?? 0) >= .5,
            matizColorir: caso['colorize_hue'] ?? 0,
            saturacaoColorir: caso['colorize_saturation'] ?? 25,
          ),
          nome: 'hueSaturation $caso',
        );
      }
      entrada.dispose();
    });

    test('o cinza continua cinza e -100 vira a luminosidade do HSL', () {
      const cinza = (r: .4, g: .4, b: .4);
      _mesmaCor(hueSaturation(cinza, saturacao: 100, matiz: 70), cinza);
      // (maximo + minimo) / 2 = .45, como no Photoshop.
      _mesmaCor(hueSaturation((r: .8, g: .3, b: .1), saturacao: -100), (
        r: .45,
        g: .45,
        b: .45,
      ));
      // Saturar ate o fim nunca passa de 0..1.
      final cheio = hueSaturation((r: .7, g: .5, b: .45), saturacao: 100);
      for (final v in [cheio.r, cheio.g, cheio.b]) {
        expect(v, inInclusiveRange(-1e-9, 1 + 1e-9));
      }
    });

    test('matriz sem shader: identidade no neutro, exata onde e linear', () {
      final neutra = hueSaturationMatrix();
      const identidade = [
        1.0, 0.0, 0.0, 0.0, 0.0, //
        0.0, 1.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 1.0, 0.0,
      ];
      for (var i = 0; i < 20; i++) {
        expect(neutra[i], closeTo(identidade[i], 1e-9), reason: 'celula $i');
      }
      const c = (r: .2, g: .55, b: .9);
      for (final claro in [40.0, -30.0]) {
        _mesmaCor(
          _aplicarMatriz(hueSaturationMatrix(luminosidade: claro), c),
          hueSaturation(c, luminosidade: claro),
          nome: 'luminosidade $claro',
        );
      }
      // Colorir: exato no preto e no meio-tom (cinza de luma 0,5).
      for (final base in [(r: 0.0, g: 0.0, b: 0.0), (r: .5, g: .5, b: .5)]) {
        _mesmaCor(
          _aplicarMatriz(
            hueSaturationMatrix(
              colorir: true,
              matizColorir: 200,
              saturacaoColorir: 60,
            ),
            base,
          ),
          hueSaturation(
            base,
            colorir: true,
            matizColorir: 200,
            saturacaoColorir: 60,
          ),
          nome: 'colorir $base',
        );
      }
    });
  });
}
