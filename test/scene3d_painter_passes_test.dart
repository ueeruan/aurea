import 'dart:ui' as ui;

import 'package:flutter/painting.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/application/texture_cache.dart';
import 'package:aurea/src/features/editor/domain/camera3d.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/presentation/widgets/scene3d_painter.dart';

/// QUANTOS PASSES DE GPU UM QUADRO 3D PODE CUSTAR.
///
/// O iPhone 13 reiniciava na cena 3D. Nao era falta de memoria do app:
/// era o pintor abrindo uma camada (saveLayer) ou um desfoque POR
/// TRIANGULO — neblina em face com imagem, reflexo do chao, sombra de
/// contato. Cada um e um alvo de render e um passe de GPU; com alguns
/// milhares de faces o driver da GPU desiste, a tela apaga e o sistema
/// cai junto. Estes testes contam o que o pintor pede ao canvas: o numero
/// de camadas tem de ser CONSTANTE, nunca proporcional as faces.
class _CanvasQueConta implements Canvas {
  int camadas = 0;
  int caminhos = 0;
  int lotes = 0;

  @override
  void saveLayer(Rect? bounds, Paint paint) => camadas++;

  @override
  void drawPath(Path path, Paint paint) => caminhos++;

  @override
  void drawVertices(ui.Vertices vertices, BlendMode blendMode, Paint paint) =>
      lotes++;

  @override
  dynamic noSuchMethod(Invocation invocation) => null;
}

ui.Image _imagem() {
  final rec = ui.PictureRecorder();
  Canvas(rec).drawRect(
    const Rect.fromLTWH(0, 0, 4, 4),
    Paint()..color = const Color(0xFFC08040),
  );
  return rec.endRecording().toImageSync(4, 4);
}

const _cam = RenderCamera(position: Vec3(0, 160, 900), target: Vec3.zero);

_CanvasQueConta _pinta(Scene3D cena) {
  final canvas = _CanvasQueConta();
  Scene3DPainter(
    scene: cena,
    camera: Camera3D(),
    view: SceneView.camera,
    time: Duration.zero,
    overrideCamera: _cam,
  ).paint(canvas, const Size(1080, 1920));
  return canvas;
}

void main() {
  const textura = 'teste://cubo';
  setUpAll(() => TextureCache.instance.put(textura, _imagem()));

  SceneNode cubo({int n = 0, bool comImagem = true}) => SceneNode(
        id: 'cubo$n',
        name: 'Cubo $n',
        kind: Element3DKind.cube,
        size: 140,
        material: Material3D(
          baseColor: const Color(0xFF7C62FF),
          imagePath: comImagem ? textura : null,
        ),
      );

  test('neblina em faces com imagem nao abre camada por triangulo', () {
    final c = _pinta(
      Scene3D(
        nodes: [cubo()],
        lights: [Light3D()],
        fogDensity: 0.002,
        background: const Color(0xFF101010),
      ),
    );
    // Um cubo de frente mostra ate tres faces = seis triangulos com
    // imagem; a versao antiga abria seis camadas so aqui.
    expect(c.camadas, 0, reason: 'a neblina tem de ser desenho, nao camada');
    expect(c.lotes, greaterThanOrEqualTo(2),
        reason: 'imagem e neblina, cada um um lote');
  });

  test('reflexo do chao e sombra de contato custam UMA camada cada', () {
    final c = _pinta(
      Scene3D(
        nodes: [for (var i = 0; i < 6; i++) cubo(n: i, comImagem: false)],
        lights: [Light3D(castsShadow: true)],
        planarFloorReflection: true,
        planarFloorRoughness: 0.5,
        background: const Color(0xFF101010),
      ),
    );
    expect(c.camadas, lessThanOrEqualTo(2),
        reason: 'reflexo (1) + sombras (1), nunca por objeto ou por face');
  });

  test('a conta nao cresce com o numero de faces', () {
    int camadasCom(int cubos) => _pinta(
          Scene3D(
            nodes: [for (var i = 0; i < cubos; i++) cubo(n: i)],
            lights: [Light3D(castsShadow: true)],
            fogDensity: 0.002,
            planarFloorReflection: true,
            planarFloorRoughness: 0.5,
            background: const Color(0xFF101010),
          ),
        ).camadas;
    expect(camadasCom(12), camadasCom(1));
  });
}
