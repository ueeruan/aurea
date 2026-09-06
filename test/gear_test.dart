import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/gear.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/text_animator.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter_test/flutter_test.dart';

VideoLayer _video({AnimatedDouble? opacity, AnimatedDouble? rotation}) =>
    VideoLayer(
      name: 'V',
      startTime: Duration.zero,
      duration: const Duration(seconds: 10),
      sourcePath: '/x.mp4',
      opacity: opacity,
      rotation: rotation,
    );

TextLayer _text([String name = 'T']) => TextLayer(
      name: name,
      startTime: Duration.zero,
      duration: const Duration(seconds: 10),
      text: 'oi',
    );

VideoProject _p(List<Layer> layers) =>
    VideoProject(name: 'p', createdAt: DateTime(2026), layers: layers);

void main() {
  group('classificador de marchas (PR-G1, tabela §10)', () {
    test('cenario 1: 1 video e nada mais -> M1', () {
      final d = classifyGear(_p([_video()]));
      expect(d.gear, PreviewGear.m1);
      expect(d.reason, contains('1 video'));
    });

    test('cenario 2: 1 video + 2 textos + 1 forma -> M2', () {
      final d = classifyGear(_p([
        _text('T1'),
        _text('T2'),
        ShapeLayer(
            name: 'F',
            startTime: Duration.zero,
            duration: const Duration(seconds: 10),
            contents: ShapePresets.paramRect()),
        _video(), // base = ultimo da lista
      ]));
      expect(d.gear, PreviewGear.m2);
      expect(d.reason, contains('3 grafismo'));
    });

    test('cenario 3: video com opacidade 80% -> M3', () {
      final d = classifyGear(_p([_video(opacity: AnimatedDouble(0.8))]));
      expect(d.gear, PreviewGear.m3);
    });

    test('cenario 4: blur forca M4 com o motivo nomeando a camada', () {
      final blurred = _text('Titulo').copyLayer(effects: [
        EffectInstance(type: EffectType.gaussianBlur),
      ]);
      final d = classifyGear(_p([blurred, _video()]));
      expect(d.gear, PreviewGear.m4);
      expect(d.reason, contains('efeito'));
      expect(d.reason, contains('Titulo'));
    });

    test('mais de 4 camadas pintaveis forca M4', () {
      final d = classifyGear(
          _p([_text(), _text(), _text(), _text(), _text()]));
      expect(d.gear, PreviewGear.m4);
      expect(d.reason, contains('>4'));
    });

    test('nulo e audio nao contam como camada pintavel', () {
      final d = classifyGear(_p([
        NullLayer(
            name: 'N',
            startTime: Duration.zero,
            duration: const Duration(seconds: 10)),
        AudioLayer(
            name: 'A',
            startTime: Duration.zero,
            duration: const Duration(seconds: 10),
            sourcePath: '/a.wav'),
        _video(),
      ]));
      expect(d.gear, PreviewGear.m1);
    });

    test('marcha nao depende do instante (estrutural, cacheavel)', () {
      final p = _p([_text(), _video()]);
      expect(classifyGear(p).gear, classifyGear(p).gear);
    });
  });

  group('portao de recomposicao', () {
    test('cena parada nao precisa de clock', () {
      expect(projectNeedsClockRebuild(_p([_text(), _video()])), isFalse);
    });

    test('keyframe, particulas e animadores de texto precisam', () {
      final animated = _text().copyLayer(
          opacity: AnimatedDouble(1)
              .withKeyframe(Duration.zero, 0)
              .withKeyframe(const Duration(seconds: 1), 1));
      expect(projectNeedsClockRebuild(_p([animated])), isTrue);
      expect(
          projectNeedsClockRebuild(_p([
            ParticlesLayer(
                name: 'P',
                startTime: Duration.zero,
                duration: const Duration(seconds: 5)),
          ])),
          isTrue);
      final withAnimator = TextLayer(
        name: 'T',
        startTime: Duration.zero,
        duration: const Duration(seconds: 5),
        text: 'oi',
        animators: [TextAnimator()],
      );
      expect(projectNeedsClockRebuild(_p([withAnimator])), isTrue);
    });

    test('assinatura muda quando camada entra/sai ou cue troca', () {
      final short = _text().copyLayer(
          startTime: Duration.zero,
          duration: const Duration(seconds: 2));
      final p = _p([short, _video()]);
      final sigInside = compositionSignature(p, const Duration(seconds: 1));
      final sigOutside =
          compositionSignature(p, const Duration(seconds: 5));
      expect(sigInside, isNot(sigOutside));
      // Mesmo instante -> mesma assinatura (reuso da arvore).
      expect(sigInside,
          compositionSignature(p, const Duration(seconds: 1)));
    });
  });
}
