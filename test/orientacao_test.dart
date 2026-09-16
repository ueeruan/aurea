// A ORIENTACAO (paridade com o After Effects, 16/09).
//
// La a camada tem ORIENTACAO e ROTACAO, e as duas nao sao a mesma
// coisa: a rotacao ACUMULA voltas — e o que se anima quando algo gira
// — e a orientacao e POSE, onde a camada esta virada. Tinhamos so a
// rotacao, entao deixar uma camada de lado e girar em cima disso
// obrigava a misturar as duas coisas no mesmo keyframe.
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter_test/flutter_test.dart';

ShapeLayer _forma({
  AnimatedDouble? orientX,
  AnimatedDouble? orientY,
  AnimatedDouble? orientZ,
}) => ShapeLayer(
  id: 'f',
  name: 'Forma',
  startTime: Duration.zero,
  duration: const Duration(seconds: 3),
  is3D: true,
  orientX: orientX,
  orientY: orientY,
  orientZ: orientZ,
  contents: [ShapePath(primitive: ShapePrimitive.rectangle)],
);

void main() {
  test('nasce em zero: nada muda no que ja existe', () {
    final l = _forma();
    expect(l.orientX.valueAt(Duration.zero), 0);
    expect(l.orientY.valueAt(Duration.zero), 0);
    expect(l.orientZ.valueAt(Duration.zero), 0);
  });

  test('e separada da rotacao: uma nao mexe na outra', () {
    final l = _forma(orientY: AnimatedDouble(45));
    expect(l.orientY.valueAt(Duration.zero), 45);
    expect(
      l.rotationY.valueAt(Duration.zero),
      0,
      reason: 'orientar nao pode escrever na rotacao',
    );
    final girada = l.copyLayer(rotationY: AnimatedDouble(90));
    expect(girada.orientY.valueAt(Duration.zero), 45);
    expect(girada.rotationY.valueAt(Duration.zero), 90);
  });

  test('copyLayer leva os tres eixos', () {
    final l = _forma(
      orientX: AnimatedDouble(10),
      orientY: AnimatedDouble(20),
      orientZ: AnimatedDouble(30),
    );
    final c = l.copyLayer(name: 'outra');
    expect(c.orientX.valueAt(Duration.zero), 10);
    expect(c.orientY.valueAt(Duration.zero), 20);
    expect(c.orientZ.valueAt(Duration.zero), 30);
  });

  test('e animavel, como qualquer propriedade', () {
    final l = _forma(
      orientZ: AnimatedDouble(0)
          .withKeyframe(Duration.zero, 0)
          .withKeyframe(const Duration(seconds: 2), 80),
    );
    expect(l.orientZ.valueAt(const Duration(seconds: 1)), closeTo(40, 1));
  });

  test('vai e volta do arquivo', () {
    final p = VideoProject(
      name: 'p',
      createdAt: DateTime(2026, 9, 16),
      layers: [
        _forma(
          orientX: AnimatedDouble(15),
          orientY: AnimatedDouble(-25),
          orientZ: AnimatedDouble(60),
        ),
      ],
    );
    final v = projectFromJson(projectToJson(p)).layers.single;
    expect(v.orientX.valueAt(Duration.zero), 15);
    expect(v.orientY.valueAt(Duration.zero), -25);
    expect(v.orientZ.valueAt(Duration.zero), 60);
  });

  test('projeto SEM orientacao nao ganha as chaves e abre em zero', () {
    final p = VideoProject(
      name: 'p',
      createdAt: DateTime(2026, 9, 16),
      layers: [_forma()],
    );
    final json = projectToJson(p);
    final camada = (json['layers'] as List).first as Map<String, dynamic>;
    for (final k in ['oriX', 'oriY', 'oriZ']) {
      expect(camada.containsKey(k), isFalse, reason: k);
    }
    expect(
      projectFromJson(json).layers.single.orientY.valueAt(Duration.zero),
      0,
    );
  });
}
