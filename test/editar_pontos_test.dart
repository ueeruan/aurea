// EDITAR PONTOS (v1.1.1): alca de entrada ou saida com alcas iguais,
// mover o contorno inteiro e varios contornos na mesma forma.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/mask.dart';
import 'package:aurea/src/features/editor/domain/path_edit.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

class _Projetos extends ProjectsController {
  @override
  List<VideoProject> build() => const [];
}

BezierPath _quadrado({bool suave = false}) => BezierPath(
  vertices: [
    for (final p in const [Offset(-100, -100), Offset(100, -100), Offset(100, 100), Offset(-100, 100)])
      PathVertex(p: p, inT: const Offset(-20, 0), outT: const Offset(30, 0), corner: !suave),
  ],
);

void main() {
  test('alca de ponto suave: a oposta segue a direcao; com alcas iguais, o tamanho', () {
    final c = _quadrado(suave: true);
    final so = moveHandle(c, 0, Handle.saida, const Offset(-100, -60));
    expect(so.vertices[0].outT, const Offset(0, 40));
    expect(so.vertices[0].inT.dx, closeTo(0, 1e-9));
    expect(so.vertices[0].inT.dy, closeTo(-20, 1e-9), reason: 'tamanho antigo');
    final iguais = moveHandle(c, 0, Handle.saida, const Offset(-100, -60), alcasIguais: true);
    expect(iguais.vertices[0].inT, const Offset(0, -40));
    // Canto continua independente.
    final canto = moveHandle(_quadrado(), 0, Handle.entrada, const Offset(-150, -100), alcasIguais: true);
    expect(canto.vertices[0].outT, const Offset(30, 0));
  });

  test('mover o contorno inteiro leva todos os pontos e deixa as alcas', () {
    final movido = moverTodosOsPontos(_quadrado(), const Offset(10, -5));
    expect(movido.vertices.map((v) => v.p), [
      const Offset(-90, -105),
      const Offset(110, -105),
      const Offset(110, 95),
      const Offset(-90, 95),
    ]);
    expect(movido.vertices.first.outT, const Offset(30, 0));
  });

  test('contornos: a forma ganha um caminho novo antes da pintura', () {
    final c = ProviderContainer(
      overrides: [projectsControllerProvider.overrideWith(_Projetos.new)],
    );
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero, name: 'F', contents: [
      ShapeBezier(path: AnimatedPath(_quadrado())),
      ShapeFill(),
    ]);
    final id = c.read(editorControllerProvider).layers.first.id;
    expect(e.contornosDaForma(id), hasLength(1));
    final novo = e.adicionarContorno(id)!;
    expect(e.contornosDaForma(id), hasLength(2));
    final itens = (c.read(editorControllerProvider).layerById(id)! as ShapeLayer).contents;
    expect(itens.indexWhere((i) => i.id == novo), lessThan(itens.indexWhere((i) => i is ShapeFill)));
  });
}
