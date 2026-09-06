import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/layer_meta.dart';
import 'package:aurea/src/features/editor/domain/template_pack.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';

TextLayer _texto(String nome) => TextLayer(
      name: nome,
      startTime: Duration.zero,
      duration: const Duration(seconds: 3),
      text: nome,
    );

VideoProject _proj({
  List<Layer>? layers,
  List<ExposedProperty>? exposed,
}) =>
    VideoProject(
      name: 'Vinheta',
      createdAt: DateTime(2026),
      layers: layers,
      exposed: exposed,
    );

void main() {
  group('Empacotar e abrir', () {
    test('a volta preserva nome, autor e camadas', () {
      final t = _proj(layers: [_texto('titulo')]);
      final pack = TemplatePack(
        name: 'Lower third',
        author: 'ruanzitwo',
        project: t,
      );
      final volta = TemplatePack.decode(pack.encode())!;
      expect(volta.name, 'Lower third');
      expect(volta.author, 'ruanzitwo');
      expect(volta.project.layers, hasLength(1));
      expect(volta.project.layers.first.name, 'titulo');
    });

    // Arquivo estragado nao pode derrubar o aplicativo.
    test('lixo devolve null em vez de estourar', () {
      expect(TemplatePack.decode('nao e json'), isNull);
      expect(TemplatePack.decode('{"aurea":"outra-coisa"}'), isNull);
      expect(TemplatePack.decode('{"aurea":"template"}'), isNull);
      expect(TemplatePack.decode('[]'), isNull);
    });

    test('agrupa os campos na ordem em que foram expostos', () {
      final l = _texto('titulo');
      final t = _proj(layers: [
        l
      ], exposed: [
        ExposedProperty(
            id: 'a', layerId: l.id, property: 'text', label: 'Titulo',
            group: 'Texto'),
        ExposedProperty(
            id: 'b', layerId: l.id, property: 'color', label: 'Cor',
            group: 'Cor'),
        ExposedProperty(
            id: 'c', layerId: l.id, property: 'size', label: 'Tamanho',
            group: 'Texto'),
      ]);
      final g = TemplatePack(name: 't', project: t).byGroup;
      expect(g.keys.toList(), ['Texto', 'Cor']);
      expect(g['Texto'], hasLength(2));
    });
  });

  group('Validar antes de mandar', () {
    test('projeto vazio nao vira template', () {
      final r = validateTemplate(_proj());
      expect(r.any((i) => i.blocking), isTrue);
    });

    test('sem campo exposto, ninguem muda nada', () {
      final r = validateTemplate(_proj(layers: [_texto('a')]));
      expect(
          r.any((i) => i.blocking && i.message.contains('exposta')), isTrue);
    });

    // O defeito classico: alguem apaga a camada e o campo continua no
    // formulario, sem fazer nada.
    test('campo apontando para camada apagada e bloqueio', () {
      final t = _proj(layers: [
        _texto('a')
      ], exposed: const [
        ExposedProperty(
            id: 'x', layerId: 'fantasma', property: 'text', label: 'T'),
      ]);
      final r = validateTemplate(t);
      expect(r.any((i) => i.blocking && i.message.contains('nao existe')),
          isTrue);
    });

    test('dois campos com o mesmo nome viram aviso', () {
      final l = _texto('a');
      final t = _proj(layers: [
        l
      ], exposed: [
        ExposedProperty(
            id: '1', layerId: l.id, property: 'text', label: 'Titulo'),
        ExposedProperty(
            id: '2', layerId: l.id, property: 'text2', label: 'Titulo'),
      ]);
      final r = validateTemplate(t);
      expect(r.any((i) => !i.blocking && i.message.contains('Titulo')),
          isTrue);
    });

    test('campo dentro de grupo tambem e achado', () {
      final dentro = _texto('interno');
      final grupo = GroupLayer(
        name: 'g',
        startTime: Duration.zero,
        duration: const Duration(seconds: 3),
        children: [dentro],
      );
      final t = _proj(layers: [
        grupo
      ], exposed: [
        ExposedProperty(
            id: '1', layerId: dentro.id, property: 'text', label: 'T'),
      ]);
      expect(validateTemplate(t).where((i) => i.blocking), isEmpty);
    });

    test('midia externa vira aviso, nao bloqueio', () {
      final v = VideoLayer(
        name: 'clipe',
        startTime: Duration.zero,
        duration: const Duration(seconds: 3),
        sourcePath: '/nao/existe.mp4',
      );
      final t = _proj(layers: [
        v
      ], exposed: [
        ExposedProperty(
            id: '1', layerId: v.id, property: 'opacity', label: 'Op'),
      ]);
      final r = validateTemplate(t);
      expect(r.any((i) => !i.blocking && i.message.contains('midia')),
          isTrue);
      expect(r.where((i) => i.blocking), isEmpty);
    });
  });
}
