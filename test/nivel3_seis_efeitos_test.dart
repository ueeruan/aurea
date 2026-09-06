import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart' hide Easing;
import 'package:flutter/rendering.dart' show RenderRepaintBoundary;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/color_space.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';

/// NIVEL 3 — SEIS EFEITOS.
///
/// O teste que o documento pede em primeiro lugar e o de NEUTRALIDADE:
/// no valor neutro o efeito tem de devolver a entrada pixel a pixel.
/// "Efeito que nao e neutro no valor neutro e bug, nao calibracao."
void main() {
  /// Os seis do nivel, com o valor neutro de cada um.
  const seis = <EffectType, Map<String, double>>{
    EffectType.gaussianBlur: {'raio': 0},
    EffectType.lightGlow: {'intensity': 0},
    EffectType.levels: {
      'entradaMin': 0,
      'entradaMax': 1,
      'gama': 1,
      'saidaMin': 0,
      'saidaMax': 1,
    },
    EffectType.rgbSplit: {'deslocamento': 0},
    EffectType.tremor: {'amplitude': 0},
    EffectType.vignette: {'quantidade': 0},
  };

  /// Cartela de referencia: degrade escuro, degrade claro, borda dura,
  /// traco fino e cor saturada — o que denuncia banda, halo e franja.
  List<Layer> cartela() => [
        ShapeLayer(
          name: 'traco fino',
          startTime: Duration.zero,
          duration: const Duration(seconds: 2),
          contents: [
            ShapeParametric(
                kind: ParamShapeKind.rect,
                sizeX: AnimatedDouble(180),
                sizeY: AnimatedDouble(3)),
            ShapeFill(color: const Color(0xFFFFFFFF)),
          ],
          position: AnimatedOffset(const Offset(150, 60)),
        ),
        ShapeLayer(
          name: 'cor saturada',
          startTime: Duration.zero,
          duration: const Duration(seconds: 2),
          contents: [
            ShapeParametric(
                kind: ParamShapeKind.ellipse,
                sizeX: AnimatedDouble(120),
                sizeY: AnimatedDouble(120)),
            ShapeFill(color: const Color(0xFFFF2D00)),
          ],
          position: AnimatedOffset(const Offset(90, 150)),
        ),
        ShapeLayer(
          name: 'degrade',
          startTime: Duration.zero,
          duration: const Duration(seconds: 2),
          contents: [
            ShapeParametric(
                kind: ParamShapeKind.rect,
                sizeX: AnimatedDouble(300),
                sizeY: AnimatedDouble(300)),
            ShapeGradientFill(
                colorA: const Color(0xFF000000),
                colorB: const Color(0xFFDFE6F2),
                angleDeg: 90),
          ],
          position: AnimatedOffset(const Offset(150, 150)),
        ),
      ];

  Future<ByteData> render(WidgetTester tester, List<Layer> layers) async {
    final project = VideoProject(
      name: 'cartela',
      createdAt: DateTime(2026, 1, 1),
      aspectRatio: 1,
      resolutionHeight: 300,
      layers: layers,
    );
    final container = ProviderContainer();
    addTearDown(container.dispose);
    container.read(editorControllerProvider.notifier).openProject(project);
    final time = ValueNotifier<Duration>(const Duration(milliseconds: 400));
    final videos = VideoLayerManager();
    final key = GlobalKey();
    tester.view.physicalSize = const Size(300, 300);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);

    await tester.pumpWidget(UncontrolledProviderScope(
      container: container,
      child: MaterialApp(
        debugShowCheckedModeBanner: false,
        home: Material(
          color: Colors.black,
          child: Center(
            child: RepaintBoundary(
              key: key,
              child: SizedBox(
                width: 300,
                height: 300,
                child: ColoredBox(
                  color: Colors.black,
                  child: CompositionView(
                    time: time,
                    videos: videos,
                    selectedId: null,
                    exporting: true,
                  ),
                ),
              ),
            ),
          ),
        ),
      ),
    ));
    await tester.pump();
    await tester.pump();

    late ByteData data;
    await tester.runAsync(() async {
      final obj =
          key.currentContext!.findRenderObject() as RenderRepaintBoundary;
      final img = await obj.toImage(pixelRatio: 1);
      data = (await img.toByteData(format: ui.ImageByteFormat.rawRgba))!;
      img.dispose();
    });
    return data;
  }

  group('neutralidade — o teste que pega bug', () {
    for (final entry in seis.entries) {
      final tipo = entry.key;
      final nome = effectSpecs[tipo]!.name;
      testWidgets('$nome no valor neutro devolve a entrada pixel a pixel',
          (tester) async {
        final semEfeito = cartela();
        final referencia = await render(tester, semEfeito);

        // A MESMA cartela, com o efeito no valor neutro na camada de cima.
        final comEfeito = cartela();
        var fx = EffectInstance(type: tipo);
        for (final p in entry.value.entries) {
          fx = fx.withParamEdited(p.key, Duration.zero, p.value);
        }
        final alvo = comEfeito.last;
        comEfeito[comEfeito.length - 1] = alvo.copyLayer(effects: [fx]);
        final resultado = await render(tester, comEfeito);

        expect(resultado.lengthInBytes, referencia.lengthInBytes);
        var diferentes = 0;
        for (var i = 0; i < referencia.lengthInBytes; i++) {
          if (referencia.getUint8(i) != resultado.getUint8(i)) diferentes++;
        }
        expect(diferentes, 0,
            reason: '$nome mudou $diferentes bytes no valor neutro');
      });
    }
  });

  group('as tres profundidades (regra 2 da constituicao)', () {
    test('os seis tem tres presets e no maximo tres numeros no montar', () {
      for (final tipo in seis.keys) {
        final spec = effectSpecs[tipo]!;
        expect(spec.temProfundidades, isTrue, reason: spec.name);
        expect(spec.presets.length, 3, reason: spec.name);
        expect(spec.montar.length, lessThanOrEqualTo(3), reason: spec.name);
        expect(spec.montar, isNotEmpty, reason: spec.name);
        for (final k in spec.montar) {
          expect(spec.params.containsKey(k), isTrue,
              reason: '${spec.name}: montar cita "$k", que nao existe');
        }
        for (final p in spec.presets) {
          expect(p.nome, isNotEmpty, reason: spec.name);
          for (final k in p.valores.keys) {
            expect(spec.params.containsKey(k), isTrue,
                reason: '${spec.name}/${p.nome}: "$k" nao existe');
          }
        }
      }
    });

    test('descer e subir de profundidade nao mexe em numero nenhum', () {
      var fx = EffectInstance(type: EffectType.lightGlow);
      fx = fx.withParamEdited('raio', Duration.zero, 123);
      final antes = fx.paramAt('raio', Duration.zero);
      fx = fx.withDepth(EffectDepth.montar).withDepth(EffectDepth.avancado);
      expect(fx.depth, EffectDepth.avancado);
      expect(fx.paramAt('raio', Duration.zero), antes);
    });

    test('o preset crava os numeros dele e deixa o resto como estava', () {
      var fx = EffectInstance(type: EffectType.lightGlow);
      fx = fx.withParamEdited('mult_r', Duration.zero, 1.7);
      final neon =
          effectSpecs[EffectType.lightGlow]!.presets.firstWhere((p) => p.nome == 'Neon');
      fx = fx.withPreset(neon);
      expect(fx.paramAt('threshold', Duration.zero), neon.valores['threshold']);
      expect(fx.paramAt('mult_r', Duration.zero), 1.7,
          reason: 'o que o preset nao cita fica como estava');
      expect(fx.color, neon.cor);
      expect(fx.depth, EffectDepth.pronto);
    });

    test('a profundidade sobrevive ao salvar e abrir', () {
      final fx = EffectInstance(type: EffectType.vignette)
          .withDepth(EffectDepth.montar);
      final volta = effectFromJson(effectToJson(fx));
      expect(volta.depth, EffectDepth.montar);
    });
  });

  group('projeto antigo abre com o mesmo resultado', () {
    test('o raio do desfoque vira pixel sem mudar o desfoque', () {
      // Arquivo velho: sem 'v', com o parametro 'amount' de 0 a 1.
      final antigo = <String, dynamic>{
        'id': 'x',
        'kind': 'gaussian_blur',
        'type': EffectType.gaussianBlur.index,
        'color': 0xFFFF5566,
        'enabled': true,
        'params': {
          'amount': {'b': 0.5},
        },
      };
      final fx = effectFromJson(antigo);
      // 0.5 * 0.04 do menor lado = 2% -> 21.6 px em 1080.
      expect(fx.paramAt('raio', Duration.zero), closeTo(21.6, 1e-9));
      expect(fx.params.containsKey('amount'), isFalse);
    });

    test('o glow converte raio, limite e intensidade', () {
      final antigo = <String, dynamic>{
        'id': 'y',
        'kind': 'glow',
        'type': EffectType.lightGlow.index,
        'color': 0xFFFFFFFF,
        'enabled': true,
        'params': {
          'diffusion': {'b': 0.25},
          'threshold': {'b': 0.7},
          'intensity': {'b': 1.0},
        },
      };
      final fx = effectFromJson(antigo);
      expect(fx.paramAt('raio', Duration.zero), closeTo(14.85, 1e-9));
      expect(fx.paramAt('threshold', Duration.zero), closeTo(70, 1e-9));
      expect(fx.paramAt('intensity', Duration.zero), closeTo(100, 1e-9));
    });

    test('keyframe antigo tambem muda de unidade', () {
      // Monta o arquivo VELHO a partir do serializador de hoje: mesma
      // trilha, com o nome antigo da chave e sem a versao.
      final fx = EffectInstance(type: EffectType.gaussianBlur, params: {
        'raio': AnimatedDouble(0, [
          const Keyframe(time: Duration.zero, value: 0),
          const Keyframe(time: Duration(seconds: 1), value: 1),
        ]),
      });
      final json = effectToJson(fx);
      final params = Map<String, dynamic>.from(json['params'] as Map);
      params['amount'] = params.remove('raio');
      json['params'] = params;
      json.remove('v');

      final lido = effectFromJson(json);
      expect(lido.paramAt('raio', const Duration(seconds: 1)),
          closeTo(43.2, 1e-9));
      expect(lido.paramAt('raio', Duration.zero), 0);
    });

    test('arquivo novo nao e convertido duas vezes', () {
      final fx = EffectInstance(type: EffectType.gaussianBlur)
          .withParamEdited('raio', Duration.zero, 200);
      final volta = effectFromJson(effectToJson(fx));
      expect(volta.paramAt('raio', Duration.zero), 200);
    });
  });

  group('pre-requisito: espaco linear', () {
    test('sRGB e linear vao e voltam', () {
      for (final v in [0.0, 0.04, 0.2, 0.5, 0.8, 1.0]) {
        expect(linearToSrgb(srgbToLinear(v)), closeTo(v, 1e-6));
      }
    });

    test('somar luz em linear nao da o mesmo que somar em sRGB', () {
      // E POR ISTO que o glow precisa do espaco linear: somar duas luzes
      // de 50% da 100% de luz, nao 100% de codigo de cor.
      final emLinear = linearToSrgb(srgbToLinear(0.5) * 2);
      expect(emLinear, closeTo(0.686, 0.01));
      expect((0.5 * 2).clamp(0.0, 1.0), 1.0);
    });

    test('o raio em pixel de 1080p vale o mesmo em qualquer resolucao', () {
      // 100 px pensados em 1080 valem 100 px em 1080 e 200 em 2160.
      expect(pxAt1080(100, 1920, 1080), closeTo(100, 1e-9));
      expect(pxAt1080(100, 3840, 2160), closeTo(200, 1e-9));
    });
  });

  group('tarefa de aceite do nivel 3', () {
    test('a receita do documento monta o projeto que ela descreve', () {
      final imagem = ShapeLayer(
        name: 'imagem',
        startTime: Duration.zero,
        duration: const Duration(seconds: 4),
        contents: [
          ShapeParametric(
              kind: ParamShapeKind.rect,
              sizeX: AnimatedDouble(400),
              sizeY: AnimatedDouble(400)),
          ShapeFill(color: const Color(0xFFDDDDDD)),
        ],
      );
      final c = ProviderContainer();
      addTearDown(c.dispose);
      final ctl = c.read(editorControllerProvider.notifier);
      ctl.openProject(VideoProject(
        name: 'aceite',
        createdAt: DateTime(2026, 1, 1),
        layers: [imagem],
      ));
      final id = imagem.id;
      ShapeLayer camada() =>
          c.read(editorControllerProvider).layerById(id) as ShapeLayer;
      EffectInstance fx(EffectType t) =>
          camada().effects.firstWhere((e) => e.type == t);

      // 1. Glow pelo preset Neon.
      ctl.addEffect(id, EffectType.lightGlow);
      final neon = effectSpecs[EffectType.lightGlow]!
          .presets
          .firstWhere((p) => p.nome == 'Neon');
      ctl.applyEffectPronto(id, fx(EffectType.lightGlow).id, neon);
      expect(fx(EffectType.lightGlow).paramAt('threshold', Duration.zero), 55);

      // 2. Montar: baixar o limite.
      final glowId = fx(EffectType.lightGlow).id;
      ctl.setEffectDepth(id, glowId, EffectDepth.montar);
      ctl.editEffectParam(id, glowId, 'threshold', Duration.zero, 20);
      expect(fx(EffectType.lightGlow).paramAt('threshold', Duration.zero), 20);

      // 3. Avancado: trocar a cor. O que o montar fez continua la.
      ctl.setEffectDepth(id, glowId, EffectDepth.avancado);
      ctl.setEffectColor(id, glowId, const Color(0xFFB8FF3D));
      expect(fx(EffectType.lightGlow).color, const Color(0xFFB8FF3D));
      expect(fx(EffectType.lightGlow).paramAt('threshold', Duration.zero), 20,
          reason: 'subir de profundidade nao pode perder o ajuste');

      // 4. Raio de 0 a 200 com mola.
      ctl.editEffectParam(id, glowId, 'raio', Duration.zero, 0);
      ctl.toggleEffectKeyframe(id, glowId, Duration.zero);
      ctl.editEffectParam(id, glowId, 'raio', const Duration(seconds: 1), 200);
      ctl.setEffectSegmentEase(id, glowId, Duration.zero, Easing.overshoot);
      final raio = fx(EffectType.lightGlow).track('raio');
      expect(raio.keyframes.length, 2);
      expect(raio.keyframes.first.ease, Easing.overshoot);
      expect(raio.valueAt(const Duration(seconds: 1)), 200);

      // 5. RGB Split e Shake com keyframes em duas batidas.
      const batida1 = Duration(milliseconds: 500);
      const batida2 = Duration(milliseconds: 1200);
      ctl.addEffect(id, EffectType.rgbSplit);
      final splitId = fx(EffectType.rgbSplit).id;
      ctl.editEffectParam(id, splitId, 'deslocamento', batida1, 0);
      ctl.toggleEffectKeyframe(id, splitId, batida1);
      ctl.editEffectParam(id, splitId, 'deslocamento', batida2, 40);
      expect(fx(EffectType.rgbSplit).track('deslocamento').keyframes.length, 2);

      ctl.addEffect(id, EffectType.tremor);
      final shakeId = fx(EffectType.tremor).id;
      ctl.editEffectParam(id, shakeId, 'amplitude', batida1, 0);
      ctl.toggleEffectKeyframe(id, shakeId, batida1);
      ctl.editEffectParam(id, shakeId, 'amplitude', batida2, 6);
      expect(fx(EffectType.tremor).track('amplitude').keyframes.length, 2);

      // 6. Vinheta.
      ctl.addEffect(id, EffectType.vignette);
      expect(camada().effects.length, 4);

      // Os quatro empilhados sobrevivem ao salvar e abrir.
      final projeto = c.read(editorControllerProvider);
      final volta = projectFromJson(projectToJson(projeto));
      final camadaVolta = volta.layers.single;
      expect(camadaVolta.effects.length, 4);
      final glowVolta =
          camadaVolta.effects.firstWhere((e) => e.type == EffectType.lightGlow);
      expect(glowVolta.depth, EffectDepth.avancado);
      expect(glowVolta.paramAt('threshold', Duration.zero), 20);
      expect(glowVolta.track('raio').valueAt(const Duration(seconds: 1)), 200);
    });
  });
}
