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

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/coloring.dart' show Rgb;
import 'package:aurea/src/features/editor/domain/efeitos_do_after.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/fx.dart' show integratedPhase;
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/pixel_effect.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/pixel_effect_engine.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart' show RenderRepaintBoundary;
import 'package:flutter_riverpod/flutter_riverpod.dart';
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
}) => PixelEffectFrame.of(_efeito(tipo, valores), tempo);

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

EffectInstance _efeito(EffectType tipo, Map<String, double> valores) =>
    EffectInstance(
      type: tipo,
      params: {for (final e in valores.entries) e.key: AnimatedDouble(e.value)},
    );

/// A composicao de verdade (a mesma da previa e da exportacao) com um
/// quadrado 40x40 no meio, devolvendo os pixels RGBA.
Future<Uint8List> _pixelsDaComposicao(
  WidgetTester tester,
  List<EffectInstance> efeitos,
  Duration tempo, {
  Color cor = const Color(0xFF305070),
}) async {
  final projeto = VideoProject(
    name: 'efeitos-do-after',
    createdAt: DateTime(2026, 9, 14),
    aspectRatio: 1,
    resolutionHeight: 128,
    backgroundColor: const Color(0xFF000000),
    layers: [
      ShapeLayer(
        id: 'alvo',
        name: 'Alvo',
        startTime: Duration.zero,
        duration: const Duration(seconds: 10),
        position: AnimatedOffset(const Offset(64, 64)),
        contents: [
          ShapePath(primitive: ShapePrimitive.rectangle, width: 40, height: 40),
          ShapeFill(color: cor),
        ],
        effects: efeitos,
      ),
    ],
  );
  final container = ProviderContainer();
  addTearDown(container.dispose);
  container.read(editorControllerProvider.notifier).openProject(projeto);
  final relogio = ValueNotifier(tempo);
  addTearDown(relogio.dispose);
  final videos = VideoLayerManager();
  addTearDown(videos.dispose);
  final chave = GlobalKey();
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: MaterialApp(
        home: Center(
          child: RepaintBoundary(
            key: chave,
            child: SizedBox(
              width: 128,
              height: 128,
              child: CompositionView(
                time: relogio,
                videos: videos,
                selectedId: null,
                exporting: true,
              ),
            ),
          ),
        ),
      ),
    ),
  );
  await tester.pump();
  final limite =
      chave.currentContext!.findRenderObject() as RenderRepaintBoundary;
  final imagem = limite.toImageSync(pixelRatio: 1);
  final dados = await tester.runAsync(
    () => imagem.toByteData(format: ui.ImageByteFormat.rawStraightRgba),
  );
  imagem.dispose();
  return dados!.buffer.asUint8List();
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
      achou('S_Flicker', EffectType.sFlicker);
      achou('sapphire flicker', EffectType.sFlicker);
      achou('cintilacao', EffectType.sFlicker);
      achou('S_MathOps', EffectType.mathOps);
      achou('math ops', EffectType.mathOps);
      achou('operacoes', EffectType.mathOps);
    });
  });

  group('catalogo', () {
    test('categorias, atalho Edits e contrato do shader', () {
      expect(effectSpecs[EffectType.hueSaturation]!.category, 'Color');
      expect(efeitosDeEdit, contains(EffectType.hueSaturation));
      expect(pixelKernels[EffectType.hueSaturation]!.mode, 45);

      // S_Flicker nao passa pelo shader: e uma matriz de cor por quadro.
      expect(effectSpecs[EffectType.sFlicker]!.category, 'Time');
      expect(efeitosDeEdit, contains(EffectType.sFlicker));
      expect(pixelKernels.containsKey(EffectType.sFlicker), isFalse);
      // O Flicker antigo continua de pe, com a mesma ficha.
      expect(effectSpecs[EffectType.flicker]!.id, 'flicker');
      expect(pixelKernels[EffectType.flicker]!.mode, 31);

      expect(effectSpecs[EffectType.mathOps]!.category, 'Color');
      expect(efeitosDeEdit, contains(EffectType.mathOps));
      expect(pixelKernels[EffectType.mathOps]!.mode, 46);
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

  group('S_Flicker', () {
    /// O ganho de uma instancia num instante, lendo a ficha como a
    /// composicao le.
    Rgb ganhoEm(EffectInstance e, Duration t) => ganhoDoSFlicker(
      faseAleatoria: integratedPhase(e.track('rand_freq'), t),
      faseDaOnda: integratedPhase(e.track('wave_freq'), t),
      amplitude: e.paramAt('amplitude', t),
      brilhoAleatorio: e.paramAt('rand_luma_amp', t),
      corAleatoria: e.paramAt('rand_color_amp', t),
      amplitudeDaOnda: e.paramAt('wave_amp', t),
      faseR: e.paramAt('wave_red_phase', t),
      faseG: e.paramAt('wave_green_phase', t),
      faseB: e.paramAt('wave_blue_phase', t),
      forcaR: e.paramAt('red_amp', t),
      forcaG: e.paramAt('green_amp', t),
      forcaB: e.paramAt('blue_amp', t),
      brilho: e.paramAt('brightness', t),
      semente: e.paramAt('seed', t).round(),
    );

    Duration quadro(int f) =>
        Duration(microseconds: (f * 1000000 / 30).round());

    test('amplitude zero: o ganho e o Brilho e a matriz e identidade', () {
      for (final t in [Duration.zero, quadro(7), quadro(45)]) {
        final g = ganhoEm(
          _efeito(EffectType.sFlicker, const {
            'amplitude': 0,
            'rand_color_amp': 1,
            'wave_amp': 1,
          }),
          t,
        );
        expect([g.r, g.g, g.b], [1.0, 1.0, 1.0]);
        final forte = ganhoEm(
          _efeito(EffectType.sFlicker, const {
            'amplitude': 0,
            'brightness': 1.5,
          }),
          t,
        );
        expect([forte.r, forte.g, forte.b], [1.5, 1.5, 1.5]);
      }
      expect(matrizDeGanho((r: 1, g: 1, b: 1)), const [
        1.0, 0.0, 0.0, 0.0, 0.0, //
        0.0, 1.0, 0.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 0.0, 1.0, 0.0,
      ]);
    });

    test('deterministico: o mesmo (tempo, semente) da o mesmo ganho', () {
      final e = _efeito(EffectType.sFlicker, const {'seed': 11});
      final ida = [for (var f = 0; f < 60; f++) ganhoEm(e, quadro(f))];
      // Voltar no tempo (scrub) e pular quadros nao muda nada: nada
      // acumula estado entre um quadro e outro.
      final volta = [for (var f = 59; f >= 0; f--) ganhoEm(e, quadro(f))]
          .reversed
          .toList();
      expect(volta, ida);
      expect(ganhoEm(e, quadro(37)), ida[37]);
    });

    test('varia no tempo e muda com a semente', () {
      final padrao = _efeito(EffectType.sFlicker, const {});
      final outraSemente = _efeito(EffectType.sFlicker, const {'seed': 7});
      final ganhos = [
        for (var f = 0; f < 60; f++) ganhoEm(padrao, quadro(f)).r,
      ];
      expect(ganhos.toSet().length, greaterThan(30));
      final maior = ganhos.reduce((a, b) => a > b ? a : b);
      final menor = ganhos.reduce((a, b) => a < b ? a : b);
      expect(maior - menor, greaterThan(.1));
      // Amplitude .2 com o ruido em -1..1: nunca sai de 0,8..1,2.
      expect(maior, lessThanOrEqualTo(1.2 + 1e-9));
      expect(menor, greaterThanOrEqualTo(.8 - 1e-9));
      var diferentes = 0;
      for (var f = 0; f < 60; f++) {
        if ((ganhoEm(outraSemente, quadro(f)).r - ganhos[f]).abs() > 1e-6) {
          diferentes++;
        }
      }
      expect(diferentes, greaterThan(50));
    });

    test('cor aleatoria separa os canais; sem ela os tres andam juntos', () {
      final junto = _efeito(EffectType.sFlicker, const {'amplitude': 1});
      final separado = _efeito(EffectType.sFlicker, const {
        'amplitude': 1,
        'rand_luma_amp': 0,
        'rand_color_amp': 1,
      });
      var separou = false;
      for (var f = 0; f < 30; f++) {
        final g = ganhoEm(junto, quadro(f));
        expect(g.g, g.r, reason: 'quadro $f');
        expect(g.b, g.r, reason: 'quadro $f');
        final s = ganhoEm(separado, quadro(f));
        if ((s.r - s.g).abs() > .01 || (s.g - s.b).abs() > .01) separou = true;
      }
      expect(separou, isTrue);
    });

    test('onda: fase e forca por canal', () {
      // Um quarto de ciclo: seno 1 no vermelho; fase de 180 no verde da -1;
      // o azul com forca zero nao pisca.
      final g = ganhoDoSFlicker(
        faseAleatoria: 0,
        faseDaOnda: .25,
        amplitude: .5,
        brilhoAleatorio: 0,
        amplitudeDaOnda: 1,
        faseG: 180,
        forcaB: 0,
      );
      _mesmaCor(g, (r: 1.5, g: .5, b: 1.0));
      // A frequencia da ficha chega integrada: 1 Hz em 250 ms.
      final e = _efeito(EffectType.sFlicker, const {
        'amplitude': .5,
        'rand_luma_amp': 0,
        'wave_amp': 1,
        'wave_freq': 1,
        'wave_green_phase': 180,
        'blue_amp': 0,
      });
      _mesmaCor(ganhoEm(e, const Duration(milliseconds: 250)), g);
      // O ganho nunca e negativo, nem com o pisca no maximo.
      final fundo = ganhoDoSFlicker(
        faseAleatoria: 0,
        faseDaOnda: .75,
        amplitude: 2,
        brilhoAleatorio: 0,
        amplitudeDaOnda: 2,
        forcaR: 2,
      );
      expect(fundo.r, 0);
    });

    testWidgets('na composicao a camada recebe o ganho exato da conta', (
      tester,
    ) async {
      tester.view.physicalSize = const Size(256, 256);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      // 0x30, 0x50, 0x70: com ganho ate 2 o azul ainda nao estoura.
      const base = [0x30, 0x50, 0x70];
      final pisca = _efeito(EffectType.sFlicker, const {
        'amplitude': 1,
        'rand_freq': 7,
        'seed': 3,
      });
      // Um instante que pisca de verdade (o ruido pode passar perto de 0).
      var t = Duration.zero;
      for (var k = 1; k <= 40; k++) {
        final candidato = Duration(milliseconds: 100 * k);
        if ((ganhoEm(pisca, candidato).r - 1).abs() > .2) {
          t = candidato;
          break;
        }
      }
      expect(t, isNot(Duration.zero), reason: 'nenhum instante piscou');
      final g = ganhoEm(pisca, t);

      final parado = await _pixelsDaComposicao(tester, [
        _efeito(EffectType.sFlicker, const {'amplitude': 0}),
      ], t);
      final aceso = await _pixelsDaComposicao(tester, [pisca], t);
      final i = (64 * 128 + 64) * 4;
      expect(parado.sublist(i, i + 3), base);
      final ganhos = [g.r, g.g, g.b];
      for (var c = 0; c < 3; c++) {
        expect(
          aceso[i + c].toDouble(),
          closeTo((base[c] * ganhos[c]).clamp(0, 255), 2),
          reason: 'canal $c com ganho ${ganhos[c]}',
        );
      }
    });
  });

  group('S_MathOps', () {
    test('nos valores iniciais a imagem sai intacta', () async {
      final entrada = await _imagem(_rampa);
      await _comparar(
        entrada,
        _rampa,
        _quadro(EffectType.mathOps, const {}),
        (c) => c,
        tolerancia: 1.5,
        nome: 'mathOps neutro',
      );
      entrada.dispose();
    });

    test('as nove operacoes, com luzes, sombras e saturacao, batem com a '
        'referencia', () async {
      final entrada = await _imagem(_rampa);
      for (var op = 0; op < 9; op++) {
        await _comparar(
          entrada,
          _rampa,
          _quadro(EffectType.mathOps, {
            'operation': op.toDouble(),
            'source_b': 1,
            'a_lights': 1.2,
            'a_darks': -.05,
            'b_lights': .6,
            'b_darks': .1,
            'b_saturation': 1.4,
            'dest_darks': .02,
            'dest_saturation': .8,
          }),
          (c) => mathOps(
            c,
            operacao: op,
            fonteB: 1,
            luzesA: 1.2,
            sombrasA: -.05,
            luzesB: .6,
            sombrasB: .1,
            saturacaoB: 1.4,
            sombrasDestino: .02,
            saturacaoDestino: .8,
          ),
          nome: 'mathOps operacao $op',
        );
      }
      entrada.dispose();
    });

    test(
      'o preset do edit de referencia: Somar sem B, sombras de A -0,03',
      () async {
        final preset = effectSpecs[EffectType.mathOps]!.presets.first;
        expect(preset.valores, const {
          'operation': 0,
          'source_b': 0,
          'a_darks': -.03,
          'mask_blur': 12,
        });
        final entrada = await _imagem(_rampa);
        await _comparar(
          entrada,
          _rampa,
          _quadro(EffectType.mathOps, preset.valores),
          (c) => mathOps(c, sombrasA: -.03),
          nome: 'mathOps edit de referencia',
        );
        // Sombras negativas escurecem mais os escuros: o preto desce, o
        // branco fica.
        _mesmaCor(mathOps((r: 1, g: 1, b: 1), sombrasA: -.03), (
          r: 1,
          g: 1,
          b: 1,
        ));
        expect(
          mathOps((r: .1, g: .1, b: .1), sombrasA: -.03).r,
          closeTo(.073, 1e-9),
        );
        entrada.dispose();
      },
    );

    test(
      'mascara de luma limita o resultado; inverter so vale com ela',
      () async {
        final entrada = await _imagem(_rampa);
        // Multiplicar pelo preto da fonte Nenhuma apaga tudo onde a mascara
        // deixa passar: e o jeito mais visivel de ver a mascara.
        for (final inverter in [false, true]) {
          await _comparar(
            entrada,
            _rampa,
            _quadro(EffectType.mathOps, {
              'operation': 2,
              'mask': 1,
              'invert_mask': inverter ? 1 : 0,
            }),
            (c) => mathOps(
              c,
              operacao: 2,
              mascaraDeLuma: true,
              inverterMascara: inverter,
            ),
            nome: 'mathOps mascara inverter=$inverter',
          );
        }
        await _comparar(
          entrada,
          _rampa,
          _quadro(EffectType.mathOps, const {'operation': 2, 'invert_mask': 1}),
          (c) => mathOps(c, operacao: 2),
          nome: 'mathOps inverter sem mascara',
        );
        entrada.dispose();
      },
    );

    test('os dois desfoques conservam uma imagem lisa e estao vivos', () async {
      const lisa = (r: 90 / 255, g: 140 / 255, b: 200 / 255);
      Rgb fonteLisa(int x, int y) => lisa;
      final plana = await _imagem(fonteLisa);
      // B = a camada desfocada; numa imagem lisa ela e a propria camada, e
      // a diferenca e zero.
      await _comparar(
        plana,
        fonteLisa,
        _quadro(EffectType.mathOps, const {
          'operation': 8,
          'source_b': 2,
          'b_blur': 40,
        }),
        (c) => (r: 0.0, g: 0.0, b: 0.0),
        tolerancia: 1.5,
        nome: 'mathOps B desfocada lisa',
      );
      await _comparar(
        plana,
        fonteLisa,
        _quadro(EffectType.mathOps, const {
          'operation': 2,
          'mask': 1,
          'mask_blur': 60,
        }),
        (c) => mathOps(c, operacao: 2, mascaraDeLuma: true),
        nome: 'mathOps mascara desfocada lisa',
      );
      plana.dispose();

      // Na rampa (o azul alterna a cada 4 px) o desfoque muda a imagem.
      final entrada = await _imagem(_rampa);
      final nitida = await _renderizar(
        entrada,
        _quadro(EffectType.mathOps, const {
          'operation': 8,
          'source_b': 2,
          'b_blur': 0,
        }),
      );
      final borrada = await _renderizar(
        entrada,
        _quadro(EffectType.mathOps, const {
          'operation': 8,
          'source_b': 2,
          'b_blur': 40,
        }),
      );
      var acesos = 0;
      for (var i = 0; i < nitida.length; i += 4) {
        expect(nitida[i + 2], lessThanOrEqualTo(1), reason: 'sem desfoque');
        if (borrada[i + 2] > 20) acesos++;
      }
      expect(acesos, greaterThan(_w * _h ~/ 4));
      entrada.dispose();
    });
  });
}
