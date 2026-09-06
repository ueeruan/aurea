import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/camera3d.dart';
import 'package:aurea/src/features/editor/domain/camera_cuts.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';

Duration _s(num v) => Duration(milliseconds: (v * 1000).round());

Camera3D _cam(String nome, double x, {double focal = 50}) => Camera3D(
      id: nome,
      name: nome,
      posX: AnimatedDouble(x),
      posZ: AnimatedDouble(800),
      focalLength: AnimatedDouble(focal),
    );

void main() {
  final a = _cam('a', 0);
  final b = _cam('b', 1000);
  final cams = [a, b];

  group('Qual tomada esta no ar', () {
    final tomadas = [
      const CameraShot(time: Duration(seconds: 2), cameraId: 'b'),
      const CameraShot(time: Duration.zero, cameraId: 'a'),
    ];

    test('ordena por tempo', () {
      expect(sortedShots(tomadas).first.time, Duration.zero);
    });

    test('a ultima que ja comecou', () {
      expect(shotAt(tomadas, _s(1))!.cameraId, 'a');
      expect(shotAt(tomadas, _s(3))!.cameraId, 'b');
    });

    test('antes da primeira, nenhuma', () {
      final so = [const CameraShot(time: Duration(seconds: 5), cameraId: 'b')];
      expect(shotAt(so, _s(1)), isNull);
    });
  });

  group('Resolver a camera', () {
    test('sem tomadas, vale a principal', () {
      expect(resolveCamera(cams, const [], _s(1), a).position.x, 0);
    });

    // Cena nao pode comecar apontando para lugar nenhum: antes da
    // primeira tomada, quem vale e a primeira.
    test('antes da primeira tomada, vale a primeira', () {
      final t = [const CameraShot(time: Duration(seconds: 5), cameraId: 'b')];
      expect(resolveCamera(cams, t, _s(1), a).position.x, 1000);
    });

    test('corte seco troca de uma vez', () {
      final t = [
        const CameraShot(time: Duration.zero, cameraId: 'a'),
        const CameraShot(time: Duration(seconds: 2), cameraId: 'b'),
      ];
      expect(resolveCamera(cams, t, _s(1.99), a).position.x, 0);
      expect(resolveCamera(cams, t, _s(2.01), a).position.x, 1000);
    });

    test('transicao passa pelo meio do caminho', () {
      final t = [
        const CameraShot(time: Duration.zero, cameraId: 'a'),
        CameraShot(
            time: _s(2), cameraId: 'b', transition: _s(1)),
      ];
      final meio = resolveCamera(cams, t, _s(2.5), a).position.x;
      expect(meio, greaterThan(100));
      expect(meio, lessThan(900));
      // No fim da transicao chegou.
      expect(resolveCamera(cams, t, _s(3), a).position.x, 1000);
    });

    test('a transicao anda sempre para frente', () {
      final t = [
        const CameraShot(time: Duration.zero, cameraId: 'a'),
        CameraShot(time: _s(2), cameraId: 'b', transition: _s(1)),
      ];
      var anterior = -1.0;
      for (var i = 0; i <= 10; i++) {
        final x = resolveCamera(cams, t, _s(2 + i / 10), a).position.x;
        expect(x, greaterThanOrEqualTo(anterior));
        anterior = x;
      }
    });

    // Apagar uma camera nao pode deixar a cena sem nenhuma.
    test('tomada apontando para camera que nao existe cai na principal', () {
      final t = [
        const CameraShot(time: Duration.zero, cameraId: 'fantasma'),
      ];
      expect(resolveCamera(const [], t, _s(1), a).position.x, 0);
    });
  });

  group('Misturar cameras', () {
    test('nas pontas, e cada uma delas', () {
      final ra = a.renderAt(Duration.zero);
      final rb = b.renderAt(Duration.zero);
      expect(lerpCamera(ra, rb, 0).position.x, 0);
      expect(lerpCamera(ra, rb, 1).position.x, 1000);
    });

    test('fora de 0..1 fica preso nas pontas', () {
      final ra = a.renderAt(Duration.zero);
      final rb = b.renderAt(Duration.zero);
      expect(lerpCamera(ra, rb, -2).position.x, 0);
      expect(lerpCamera(ra, rb, 5).position.x, 1000);
    });

    // A distancia focal e logaritmica: de 20 mm a 200 mm o meio
    // perceptual e ~63 mm. Linear daria 110, e a transicao pareceria
    // parada no comeco e disparada no fim.
    test('a lente e interpolada em log', () {
      final ra = _cam('x', 0, focal: 20).renderAt(Duration.zero);
      final rb = _cam('y', 0, focal: 200).renderAt(Duration.zero);
      final meio = lerpCamera(ra, rb, 0.5).focalLength;
      expect(meio, closeTo(63.2, 1));
      expect(meio, lessThan(110));
    });

    test('lente de comprimento igual nao muda', () {
      final ra = _cam('x', 0, focal: 35).renderAt(Duration.zero);
      final rb = _cam('y', 500, focal: 35).renderAt(Duration.zero);
      expect(lerpCamera(ra, rb, 0.5).focalLength, closeTo(35, 0.001));
    });

    test('nao devolve foco zero nem negativo', () {
      const ra = RenderCamera(focalLength: 0);
      const rb = RenderCamera(focalLength: 50);
      expect(lerpCamera(ra, rb, 0.5).focalLength, greaterThan(0));
    });
  });

  group('Copiar tomada', () {
    test('troca so o pedido', () {
      const t = CameraShot(time: Duration(seconds: 1), cameraId: 'a');
      expect(t.copyWith(cameraId: 'b').time, const Duration(seconds: 1));
      expect(t.copyWith(time: Duration.zero).cameraId, 'a');
    });

    test('zero e corte', () {
      expect(const CameraShot(time: Duration.zero, cameraId: 'a').isCut,
          isTrue);
      expect(
          CameraShot(time: Duration.zero, cameraId: 'a', transition: _s(1))
              .isCut,
          isFalse);
    });
  });
}
