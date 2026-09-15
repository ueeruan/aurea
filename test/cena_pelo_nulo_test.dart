// A CENA 3D DIRIGIDA PELO NULO DA LINHA DO TEMPO — a travessia de eixos.
//
// A composicao e Y-para-baixo com Z afastando; a cena e Y-para-cima com
// a camera olhando -Z. O nulo atravessa essa fronteira com a MESMA
// meia-volta dos nos (X igual; Y e Z de posicao invertem; rotX fica;
// rotY e rotZ invertem). O defeito de campo: subir o nulo descia a
// cena, e o giro orbitava para o lado errado — a camera recebia o nulo
// em coordenadas cruas da composicao.

import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart'
    show cameraDaCena;
import 'package:flutter_test/flutter_test.dart';

void main() {
  (VideoProject, Scene3DLayer, NullLayer) montar({
    Offset? pos,
    double rot = 0,
    double z = 0,
  }) {
    final nulo = NullLayer(
      name: 'Nulo',
      startTime: Duration.zero,
      duration: const Duration(seconds: 5),
      position: AnimatedOffset(pos ?? const Offset(960, 540)),
      rotation: AnimatedDouble(rot),
      positionZ: AnimatedDouble(z),
    );
    final cena = Scene3DLayer(
      name: 'Cena',
      startTime: Duration.zero,
      duration: const Duration(seconds: 5),
      cameraParentLayerId: nulo.id,
    );
    final p = VideoProject(
      name: 'p',
      createdAt: DateTime(2026),
      // 1920x1080: o centro e (960, 540).
      aspectRatio: 16 / 9,
      resolutionHeight: 1080,
      layers: [cena, nulo],
    );
    return (p, cena, nulo);
  }

  test('nulo parado no centro nao mexe na camera', () {
    final (p, cena, _) = montar();
    final cam = cameraDaCena(p, cena, Duration.zero, Duration.zero)!;
    final base = cena.cameraAt(Duration.zero);
    expect(cam.position.x, closeTo(base.position.x, 1e-6));
    expect(cam.position.y, closeTo(base.position.y, 1e-6));
    expect(cam.position.z, closeTo(base.position.z, 1e-6));
  });

  test('nulo para a direita leva a camera para +X da cena', () {
    final (p, cena, _) = montar(pos: const Offset(1060, 540));
    final cam = cameraDaCena(p, cena, Duration.zero, Duration.zero)!;
    final base = cena.cameraAt(Duration.zero);
    expect(cam.position.x, closeTo(base.position.x + 100, 1e-6));
  });

  test('nulo para CIMA leva a camera para +Y da cena (Y vira)', () {
    final (p, cena, _) = montar(pos: const Offset(960, 440));
    final cam = cameraDaCena(p, cena, Duration.zero, Duration.zero)!;
    final base = cena.cameraAt(Duration.zero);
    expect(
      cam.position.y,
      closeTo(base.position.y + 100, 1e-6),
      reason: 'subir na composicao e subir na cena',
    );
  });

  test('giro do nulo orbita a camera com o sinal certo', () {
    final (p, cena, _) = montar(rot: 90);
    final cam = cameraDaCena(p, cena, Duration.zero, Duration.zero)!;
    // rotZ da cena = -90: o "cima" da camera vai parar em +X.
    expect(cam.up.x, closeTo(1, 1e-6));
    expect(cam.up.y, closeTo(0, 1e-6));
  });

  test('nulo afastando em Z e um dolly-in (a camera entra na cena)', () {
    final (p, cena, _) = montar(z: 50);
    final cam = cameraDaCena(p, cena, Duration.zero, Duration.zero)!;
    final base = cena.cameraAt(Duration.zero);
    expect(
      cam.position.z,
      closeTo(base.position.z - 50, 1e-6),
      reason: 'o nulo e pai da CAMERA: empurra-lo para dentro da tela '
          'leva a camera para dentro da cena',
    );
  });
}
