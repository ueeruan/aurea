// AS OPERACOES DO LOTE (v1.1.1): varias camadas, um toque, um desfazer.
//
// Aparar/dividir no cabecote quando ele passa por dentro; estender e
// mover ate ele quando esta de fora; alinhar e distribuir no tempo; e o
// agrupar que ja nasce mascarando (ou recortando) as de baixo.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/operacoes_do_lote.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/painting.dart' show BlendMode;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

class _Projetos extends ProjectsController {
  @override
  List<VideoProject> build() => const [];
}

const _s = Duration(seconds: 1);

(ProviderContainer, EditorController, List<String>) _tres() {
  final c = ProviderContainer(
    overrides: [projectsControllerProvider.overrideWith(_Projetos.new)],
  );
  addTearDown(c.dispose);
  final e = c.read(editorControllerProvider.notifier);
  e.addShapeLayer(Duration.zero, name: 'A'); // 0..3
  e.addShapeLayer(_s * 2, name: 'B'); // 2..5
  e.addShapeLayer(_s * 6, name: 'C'); // 6..9
  final ids = [for (final l in c.read(editorControllerProvider).layers) l.id];
  return (c, e, ids);
}

Layer _de(ProviderContainer c, String nome) => c
    .read(editorControllerProvider)
    .layers
    .firstWhere((l) => l.name.startsWith(nome));

void main() {
  test('com o cabecote dentro, divide so quem ele atravessa (um desfazer)', () {
    final (c, e, ids) = _tres();
    final antes = c.read(editorControllerProvider).layers.length;
    final p = c.read(editorControllerProvider);
    expect(cabecoteDentroDoLote(p, ids, const Duration(milliseconds: 2500)), isTrue);
    dividirLote(e, p, ids, const Duration(milliseconds: 2500));
    // A e B passam por 2,5 s; C nao.
    expect(c.read(editorControllerProvider).layers.length, antes + 2);
    e.undo();
    expect(c.read(editorControllerProvider).layers.length, antes);
  });

  test('aparar comeco e fim no cabecote', () {
    final (c, e, ids) = _tres();
    aparaInicioDoLote(e, c.read(editorControllerProvider), ids, const Duration(milliseconds: 2500));
    expect(_de(c, 'A').startTime, const Duration(milliseconds: 2500));
    expect(_de(c, 'B').startTime, const Duration(milliseconds: 2500));
    expect(_de(c, 'C').startTime, _s * 6);
    e.undo();
    aparaFimDoLote(e, c.read(editorControllerProvider), ids, const Duration(milliseconds: 2500));
    expect(_de(c, 'A').endTime, const Duration(milliseconds: 2500));
    expect(_de(c, 'B').endTime, const Duration(milliseconds: 2500));
    expect(_de(c, 'C').endTime, _s * 9);
  });

  test('cabecote de fora: estender e mover ate ele, cada uma na sua direcao', () {
    final (c, e, _) = _tres();
    final a = _de(c, 'A').id;
    final cc = _de(c, 'C').id;
    final p = c.read(editorControllerProvider);
    // 5,5 s: depois do fim de A (3 s) e antes do comeco de C (6 s).
    const t = Duration(milliseconds: 5500);
    expect(cabecoteDentroDoLote(p, [a, cc], t), isFalse);
    estenderLoteAteOCabecote(e, p, [a, cc], t);
    expect(_de(c, 'A').endTime, t);
    expect(_de(c, 'C').startTime, t);
    expect(_de(c, 'C').endTime, _s * 9);
    e.undo();
    moverLoteAteOCabecote(e, c.read(editorControllerProvider), [a, cc], t);
    expect(_de(c, 'A').endTime, t, reason: 'a de antes encosta pelo fim');
    expect(_de(c, 'A').duration, _s * 3);
    expect(_de(c, 'C').startTime, t, reason: 'a de depois encosta pelo comeco');
  });

  test('alinhar comecos, alinhar fins e distribuir uma depois da outra', () {
    final (c, e, ids) = _tres();
    alinharIniciosNoTempo(e, c.read(editorControllerProvider), ids);
    for (final n in ['A', 'B', 'C']) {
      expect(_de(c, n).startTime, Duration.zero);
    }
    e.undo();
    alinharFinsNoTempo(e, c.read(editorControllerProvider), ids);
    for (final n in ['A', 'B', 'C']) {
      expect(_de(c, n).endTime, _s * 9);
    }
    e.undo();
    distribuirNoTempo(e, c.read(editorControllerProvider), ids);
    expect(_de(c, 'A').startTime, Duration.zero);
    expect(_de(c, 'B').startTime, _s * 3);
    expect(_de(c, 'C').startTime, _s * 6);
    expect(_de(c, 'B').endTime, _s * 6);
  });

  test('agrupar e mascarar: a de cima vira mascara dentro do grupo novo', () {
    final (c, e, ids) = _tres();
    final topo = c.read(editorControllerProvider).layers.first;
    agruparComForma(e, c.read(editorControllerProvider), ids, recortar: false);
    final layers = c.read(editorControllerProvider).layers;
    expect(layers, hasLength(1));
    final grupo = layers.single as GroupLayer;
    expect(grupo.children.first.id, topo.id);
    expect(grupo.children.first.blendMode, BlendMode.dstIn);
    e.undo();
    expect(c.read(editorControllerProvider).layers, hasLength(3));
    expect(c.read(editorControllerProvider).layers.first.blendMode, BlendMode.srcOver);
    agruparComForma(e, c.read(editorControllerProvider), ids, recortar: true);
    final g2 = c.read(editorControllerProvider).layers.single as GroupLayer;
    expect(g2.children.first.blendMode, BlendMode.dstOut);
  });
}
