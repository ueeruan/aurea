// SEGURAR E ARRASTAR O KEYFRAME (relato do testador, 14/09/2026): o
// instante inteiro anda junto — transformacao e efeito — com valor e
// easing, sem fundir com outra marca, e volta com um desfazer so.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

const _umSeg = Duration(seconds: 1);
const _umEMeio = Duration(milliseconds: 1500);
const _doisSeg = Duration(seconds: 2);

void main() {
  late ProviderContainer c;
  late EditorController e;

  setUp(() {
    c = ProviderContainer();
    e = c.read(editorControllerProvider.notifier);
    final fx = EffectInstance(
      type: EffectType.gaussianBlur,
    ).withKeyframeToggled(_umSeg);
    e.openProject(
      VideoProject(
        name: 'kf',
        createdAt: DateTime(2026, 9, 14),
        layers: [
          ShapeLayer(
            id: 'a',
            name: 'a',
            startTime: Duration.zero,
            duration: const Duration(seconds: 4),
            contents: [
              ShapePath(primitive: ShapePrimitive.rectangle, width: 20, height: 20),
              ShapeFill(color: const Color(0xFFFFFFFF)),
            ],
            opacity: AnimatedDouble(1)
                .withKeyframe(_umSeg, .2, Easing.easeIn)
                .withKeyframe(_doisSeg, 1),
            effects: [fx],
          ),
        ],
      ),
    );
  });
  tearDown(() => c.dispose());

  Layer camada() => c.read(editorControllerProvider).layerById('a')!;

  test('move o instante inteiro com valor e easing, num desfazer', () {
    expect(e.moverKeyframe('a', _umSeg, _umEMeio), isNull);
    final l = camada();
    final tempos = [for (final k in l.opacity.keyframes) k.time];
    expect(tempos, [_umEMeio, _doisSeg]);
    expect(l.opacity.keyframes.first.value, .2);
    expect(l.opacity.keyframes.first.ease, Easing.easeIn);
    expect(l.effectTimesUs, {_umEMeio.inMicroseconds}, reason: 'o efeito andou junto');
    e.undo();
    expect([for (final k in camada().opacity.keyframes) k.time], [_umSeg, _doisSeg]);
    expect(camada().effectTimesUs, {_umSeg.inMicroseconds});
  });

  test('nao funde com a marca que ja existe no destino', () {
    expect(e.moverKeyframe('a', _umSeg, _doisSeg), isNotNull);
    expect([for (final k in camada().opacity.keyframes) k.time], [_umSeg, _doisSeg]);
  });
}
