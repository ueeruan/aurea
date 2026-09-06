import 'dart:convert';
import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/caption.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/text_animator.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';

void main() {
  group('persistencia do projeto (JSON round-trip)', () {
    test('projeto completo sobrevive a ida e volta', () {
      final project = VideoProject(
        name: 'Teste',
        createdAt: DateTime(2026, 8, 30, 12),
        aspectRatio: 9 / 16,
        fps: 60,
        resolutionHeight: 1080,
        layers: [
          TextLayer(
            name: 'Titulo',
            startTime: const Duration(milliseconds: 500),
            duration: const Duration(seconds: 3),
            text: 'Ola mundo',
            color: const Color(0xFFB8FF3D),
            position: AnimatedOffset(const Offset(540, 960), [
              Keyframe(
                  time: Duration.zero,
                  value: const Offset(0, 0),
                  ease: Easing.easeInOut),
              Keyframe(
                  time: const Duration(seconds: 1),
                  value: const Offset(540, 960),
                  ease: const Easing(type: EasingType.bounce)),
            ]),
            animators: [
              TextAnimator(
                name: 'Entrada',
                properties: [
                  AnimatorProperty(
                      type: TextAnimProp.opacity,
                      value: AnimatedDouble(0)),
                ],
                selectors: [
                  RangeSelector(
                    shape: SelectorShape.smooth,
                    basedOn: SelectorBasedOn.words,
                    start: AnimatedDouble(0, [
                      Keyframe(time: Duration.zero, value: 0),
                      Keyframe(
                          time: const Duration(seconds: 1), value: 1),
                    ]),
                  ),
                  WigglySelector(mode: SelectorMode.intersect),
                ],
              ),
            ],
            effects: [
              EffectInstance(
                type: EffectType.lightGlow,
                color: const Color(0xFF7C62FF),
              ),
            ],
          ),
          ShapeLayer(
            name: 'Estrela',
            startTime: Duration.zero,
            duration: const Duration(seconds: 5),
            is3D: true,
            positionZ: AnimatedDouble(300),
            rotationX: AnimatedDouble(45),
            contents: [
              ShapePath(primitive: ShapePrimitive.star, points: 6),
              TrimOperator(end: AnimatedDouble(0.6)),
              RepeaterOperator(copies: 4, dx: 100),
              ShapeFill(color: const Color(0xFFFFB020)),
              ShapeStroke(dashLength: AnimatedDouble(10), gapLength: AnimatedDouble(6)),
            ],
          ),
          GroupLayer(
            name: 'Grupo',
            startTime: const Duration(seconds: 1),
            duration: const Duration(seconds: 2),
            children: [
              NullLayer(
                name: 'Nulo',
                startTime: Duration.zero,
                duration: const Duration(seconds: 2),
                is3D: true,
              ),
            ],
          ),
          CaptionLayer(
            name: 'Legendas',
            startTime: Duration.zero,
            duration: const Duration(seconds: 4),
            cues: [
              Cue(
                  start: Duration.zero,
                  end: const Duration(seconds: 2),
                  text: 'Primeira fala',
                  locked: true),
            ],
            style: const CaptionStyle(fontSize: 64, bold: false),
          ),
          ParticlesLayer(
            name: 'Particulas',
            startTime: Duration.zero,
            duration: const Duration(seconds: 5),
            count: 200,
            seed: 42,
            star: true,
            emitW: 800,
            emitH: 600,
            twinkle: true,
            color: const Color(0xFF35C4E7),
          ),
          VideoLayer(
            name: 'Video',
            startTime: Duration.zero,
            duration: const Duration(seconds: 8),
            sourcePath: '/tmp/v.mp4',
            sourceOffset: const Duration(milliseconds: 250),
            volume: 0.7,
            blendMode: BlendMode.screen,
          ),
        ],
        links: [
          PropertyLink(
            targetLayerId: 'a',
            targetProp: LayerProp.parent,
            sourceLayerId: 'b',
            offsetX: 10,
            offsetY: 20,
            baseRotation: 45,
            baseScale: 2,
            baseRotationX: 30,
            baseRotationY: -60,
            baseZ: 250,
          ),
        ],
      );

      // Ida e volta por STRING (como no disco).
      final restored = projectFromJson(
          jsonDecode(jsonEncode(projectToJson(project)))
              as Map<String, dynamic>);

      expect(restored.id, project.id);
      expect(restored.name, 'Teste');
      expect(restored.fps, 60);
      expect(restored.aspectRatio, closeTo(9 / 16, 1e-9));
      expect(restored.layers.length, 6);

      final text = restored.layers[0] as TextLayer;
      expect(text.text, 'Ola mundo');
      expect(text.position.keyframes.length, 2);
      expect(text.position.keyframes[1].ease.type, EasingType.bounce);
      expect(text.animators.single.selectors.length, 2);
      expect(text.animators.single.selectors[1], isA<WigglySelector>());
      expect(
          (text.animators.single.selectors[0] as RangeSelector)
              .start
              .keyframes
              .length,
          2);
      expect(text.effects.single.type, EffectType.lightGlow);

      final shape = restored.layers[1] as ShapeLayer;
      expect(shape.is3D, true);
      expect(shape.rotationX.base, 45);
      expect(shape.contents.length, 5);
      expect(shape.contents[2], isA<RepeaterOperator>());
      expect((shape.contents[4] as ShapeStroke).dashLength.base, 10);

      final group = restored.layers[2] as GroupLayer;
      expect(group.children.single, isA<NullLayer>());

      final captions = restored.layers[3] as CaptionLayer;
      expect(captions.cues.single.locked, true);
      expect(captions.style.fontSize, 64);

      final particles = restored.layers[4] as ParticlesLayer;
      expect(particles.count, 200);
      expect(particles.seed, 42);
      expect(particles.star, true);
      expect(particles.emitW, 800);
      expect(particles.emitH, 600);
      expect(particles.twinkle, true);

      final video = restored.layers[5] as VideoLayer;
      expect(video.sourceOffset, const Duration(milliseconds: 250));
      expect(video.blendMode, BlendMode.screen);

      final link = restored.links.single;
      expect(link.targetProp, LayerProp.parent);
      expect(link.baseRotation, 45);
      expect(link.baseScale, 2);
      expect(link.baseRotationX, 30);
      expect(link.baseRotationY, -60);
      expect(link.baseZ, 250);
    });

    test('valores avaliados batem apos o round-trip', () {
      final layer = ShapeLayer(
        name: 'S',
        startTime: Duration.zero,
        duration: const Duration(seconds: 4),
        rotation: AnimatedDouble(0, [
          Keyframe(time: Duration.zero, value: 0, ease: Easing.easeOut),
          Keyframe(time: const Duration(seconds: 2), value: 360),
        ]),
      );
      final restored = layerFromJson(
          jsonDecode(jsonEncode(layerToJson(layer)))
              as Map<String, dynamic>);
      for (final ms in [0, 333, 500, 1000, 1500, 1999, 2000]) {
        final t = Duration(milliseconds: ms);
        expect(restored.rotation.valueAt(t),
            closeTo(layer.rotation.valueAt(t), 1e-9),
            reason: 'em $ms ms');
      }
    });
  });
}
