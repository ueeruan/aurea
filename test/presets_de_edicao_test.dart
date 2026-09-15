// OS PRESETS DO BUNDLE 4nas.ftbl, convertidos para o motor.
//
// A regra de ouro do arquivo de receitas vale aqui: TODA chave e todo
// valor sao conferidos contra o catalogo — receita com chave errada ou
// numero fora da faixa derruba a suite, nao o aparelho.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/effect_preset_store.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/domain/cut_ops.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/presets_de_edicao.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/context/effects/effect_gallery.dart';
import 'package:aurea/src/features/editor/presentation/context/effects/effect_thumbnail.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

VideoLayer _video() => VideoLayer(
  name: 'v',
  startTime: Duration.zero,
  duration: const Duration(seconds: 6),
  sourcePath: 'x.mp4',
  sourceDuration: const Duration(seconds: 30),
);

void main() {
  setUpAll(() {
    EffectThumbnailCache.semDisco = true;
    EffectPresetStore.semArquivo = true;
  });

  group('o catalogo de presets', () {
    test('sao 16, com id unico, nome e o que fazem', () {
      expect(presetsDeEdicao, hasLength(16));
      expect(
        presetsDeEdicao.map((p) => p.id).toSet(),
        hasLength(presetsDeEdicao.length),
      );
      for (final p in presetsDeEdicao) {
        expect(p.nome.trim(), isNotEmpty, reason: p.id);
        expect(p.detalhe.trim(), isNotEmpty, reason: p.id);
        expect(p.receita, isNotEmpty, reason: p.id);
        if (p.acao == AcaoDoPreset.cameraLenta) {
          expect(p.rampa, isNotNull, reason: p.id);
        }
      }
    });

    test('toda chave e todo valor batem com o catalogo de efeitos', () {
      for (final p in presetsDeEdicao) {
        for (final r in p.receita) {
          final spec = effectSpecs[r.tipo];
          expect(spec, isNotNull, reason: '${p.id}: ${r.tipo}');
          for (final e in r.valores.entries) {
            final param = spec!.params[e.key];
            expect(
              param,
              isNotNull,
              reason: '${p.id}: ${spec.id} nao tem "${e.key}"',
            );
            expect(
              e.value,
              inInclusiveRange(param!.min, param.max),
              reason: '${p.id}: ${spec.id}.${e.key} = ${e.value}',
            );
          }
          if (r.coresExtras != null) {
            expect(
              r.coresExtras!.length,
              spec!.extraColors,
              reason: '${p.id}: ${spec.id} cores extras',
            );
          }
        }
      }
    });

    test('montar gera instancias novas (ids proprios) com os valores', () {
      final p = presetDeEdicaoPorId('4nas-cold-cc')!;
      final a = p.montar();
      final b = p.montar();
      expect(a.map((e) => e.id).toSet().intersection(
            b.map((e) => e.id).toSet(),
          ),
          isEmpty);
      final sat = a.singleWhere((e) => e.type == EffectType.hueSaturation);
      // O +25 extraido do proprio .ffx do bundle.
      expect(sat.params['master_saturation']!.valueAt(Duration.zero), 25);
    });
  });

  group('aplicar', () {
    (ProviderContainer, EditorController, VideoLayer) montar() {
      final c = ProviderContainer();
      final e = c.read(editorControllerProvider.notifier);
      final v = _video();
      e.openProject(
        VideoProject(name: 'p', createdAt: DateTime(2026), layers: [v]),
      );
      return (c, e, v);
    }

    test('cada preset de efeitos entra inteiro, e o desfazer limpa', () {
      for (final p in presetsDeEdicao) {
        if (p.acao != AcaoDoPreset.efeitos) continue;
        final (c, e, v) = montar();
        final n = e.aplicarPresetDeEdicao(v.id, p);
        expect(n, p.receita.length, reason: p.id);
        final depois = c.read(editorControllerProvider).layerById(v.id)!;
        expect(depois.effects, hasLength(p.receita.length), reason: p.id);
        e.undo();
        expect(
          c.read(editorControllerProvider).layerById(v.id)!.effects,
          isEmpty,
          reason: p.id,
        );
        c.dispose();
      }
    });

    test('Twixtor: rampa + motion blur em video, recusa em texto', () {
      final (c, e, v) = montar();
      addTearDown(c.dispose);
      final p = presetDeEdicaoPorId('4nas-twixtor')!;
      final n = e.aplicarPresetDeEdicao(v.id, p);
      expect(n, 1);
      final depois =
          c.read(editorControllerProvider).layerById(v.id)! as VideoLayer;
      expect(hasTimeRemap(depois), isTrue);
      expect(
        depois.effects.any((x) => x.type == EffectType.forceMotionBlur),
        isTrue,
      );
      // Um desfazer so remove rampa E efeito.
      e.undo();
      final zerado =
          c.read(editorControllerProvider).layerById(v.id)! as VideoLayer;
      expect(hasTimeRemap(zerado), isFalse);
      expect(
        zerado.effects.any((x) => x.type == EffectType.forceMotionBlur),
        isFalse,
      );

      final texto = TextLayer(
        name: 't',
        text: 'oi',
        startTime: Duration.zero,
        duration: const Duration(seconds: 3),
      );
      e.openProject(
        VideoProject(name: 'p2', createdAt: DateTime(2026), layers: [texto]),
      );
      expect(e.aplicarPresetDeEdicao(texto.id, p), 0);
      expect(
        c.read(editorControllerProvider).layerById(texto.id)!.effects,
        isEmpty,
      );
    });
  });

  test('Impact Flow: rampa flow + speed blur + zoom lento + flow otico', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    final v = _video();
    e.openProject(
      VideoProject(name: 'p', createdAt: DateTime(2026), layers: [v]),
    );
    final p = presetDeEdicaoPorId('impact-flow')!;
    expect(e.aplicarPresetDeEdicao(v.id, p), 2);
    final depois =
        c.read(editorControllerProvider).layerById(v.id)! as VideoLayer;
    // A rampa das duas pontas rapidas + o blur por velocidade.
    expect(hasTimeRemap(depois), isTrue);
    expect(depois.speedBlur, isTrue);
    // O zoom lento em keyframes de verdade: comeca no 100% e fecha 8%
    // acima no fim do clipe.
    expect(depois.scaleX.isAnimated, isTrue);
    expect(
      depois.scaleX.valueAt(depois.duration),
      closeTo(depois.scaleX.valueAt(Duration.zero) * 1.08, 1e-6),
    );
    expect(
      depois.effects.map((x) => x.type),
      containsAll([EffectType.opticalFlow, EffectType.tremor]),
    );
    // Aplicar de novo NAO duplica o optical flow (regra do addEffect);
    // a escala ja animada e respeitada.
    expect(e.aplicarPresetDeEdicao(v.id, p), 1);
    final deNovo =
        c.read(editorControllerProvider).layerById(v.id)! as VideoLayer;
    expect(
      deNovo.effects.where((x) => x.type == EffectType.opticalFlow),
      hasLength(1),
    );
    expect(
      deNovo.effects.where((x) => x.type == EffectType.tremor),
      hasLength(2),
    );
    expect(deNovo.scaleX.keyframes, hasLength(2));
    // Em texto, recusa limpa.
    final t = TextLayer(
      name: 't',
      text: 'oi',
      startTime: Duration.zero,
      duration: const Duration(seconds: 3),
    );
    e.openProject(
      VideoProject(name: 'p2', createdAt: DateTime(2026), layers: [t]),
    );
    expect(e.aplicarPresetDeEdicao(t.id, p), 0);
  });

  testWidgets('a aba Presets da galeria lista os 16 e um toque aplica', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1170, 2532);
    tester.view.devicePixelRatio = 3;
    addTearDown(tester.view.reset);

    final container = ProviderContainer();
    addTearDown(container.dispose);
    final v = _video();
    container.read(editorControllerProvider.notifier).openProject(
      VideoProject(name: 'g', createdAt: DateTime(2026), layers: [v]),
    );
    final playback = PlaybackController(
      vsync: tester,
      durationOf: () => const Duration(seconds: 6),
    );
    addTearDown(playback.dispose);

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: MaterialApp(
          home: Consumer(
            builder: (context, ref, _) => Scaffold(
              body: Center(
                child: TextButton(
                  key: const ValueKey('abrir-galeria'),
                  onPressed: () =>
                      showEffectGallery(context, ref, v.id, playback),
                  child: const Text('abrir'),
                ),
              ),
            ),
          ),
        ),
      ),
    );
    await tester.tap(find.byKey(const ValueKey('abrir-galeria')));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400));

    await tester.tap(find.byKey(const ValueKey('galeria-presets')));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 100));

    expect(
      find.byKey(const ValueKey('galeria-lista-presets')),
      findsOneWidget,
    );
    expect(find.byKey(const ValueKey('preset-4nas-main-cc-2024')),
        findsOneWidget);
    // O primeiro cartao (Impact Flow) esta a vista: um toque aplica a
    // pilha inteira — rampa, blur por velocidade, zoom e efeitos.
    await tester.tap(find.byKey(const ValueKey('preset-impact-flow')));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400));

    final depois =
        container.read(editorControllerProvider).layerById(v.id)!
            as VideoLayer;
    expect(
      depois.effects.map((e) => e.type),
      containsAll([EffectType.opticalFlow, EffectType.tremor]),
    );
    expect(depois.speedBlur, isTrue);
    // O snack de 5 s precisa morrer antes do fim do teste.
    await tester.pump(const Duration(seconds: 6));
  });
}
