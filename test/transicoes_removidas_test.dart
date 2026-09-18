// AS TRANSICOES DE CLIPE FORAM REMOVIDAS DO PRODUTO (17/09).
//
// O que este teste protege e o unico ponto que nao pode ceder numa remocao:
// um projeto salvo com transicao tem de continuar ABRINDO. A transicao se
// perde; a camada, o projeto e o resto da linha do tempo, nunca.
//
// A armadilha e conhecida e ja custou caro neste app uma vez (ver
// `catalogo_vazio_test.dart`): o carregador le camada a camada dentro de um
// `try`, e o `catch` de camada engole a excecao. Um campo que estoura
// dentro de `layerFromJson` nao perde o campo — perde a CAMADA INTEIRA, em
// silencio, junto com audio, keyframes e vinculos de matte.
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter_test/flutter_test.dart';

/// O `transitionIn` que um aparelho com a versao antiga gravava, com todos
/// os campos que ele gravava — inclusive o efeito da transicao.
Map<String, dynamic> _transicaoAntiga(String deQuem) => {
  'out': deQuem,
  'type': 'dissolve',
  'dur': 500000,
  'align': 'center',
  'curve': {'t': 3, 'x1': 0.42, 'y1': 0.0, 'x2': 0.58, 'y2': 1.0},
  'audio': false,
  'freeze': true,
  'ripple': ['outra-camada'],
  'amount': {'b': 0.0, 'k': <dynamic>[]},
};

VideoProject _projeto() => VideoProject(
  name: 'antigo',
  createdAt: DateTime(2026, 9, 1),
  layers: [
    ShapeLayer(
      id: 'a',
      name: 'Forma A',
      startTime: Duration.zero,
      duration: const Duration(seconds: 3),
      position: AnimatedOffset(const Offset(10, 20)),
      contents: [ShapePath(primitive: ShapePrimitive.rectangle)],
    ),
    ShapeLayer(
      id: 'b',
      name: 'Forma B',
      startTime: const Duration(seconds: 3),
      duration: const Duration(seconds: 3),
      position: AnimatedOffset(const Offset(40, 60)),
      contents: [ShapePath(primitive: ShapePrimitive.ellipse)],
    ),
  ],
);

void main() {
  test('projeto salvo COM transicao ainda abre — sem a transicao', () {
    final json = projectToJson(_projeto());
    final camadas = (json['layers'] as List).cast<Map<String, dynamic>>();
    camadas[1]['transitionIn'] = _transicaoAntiga('a');

    final volta = projectFromJson(json);

    expect(volta.name, 'antigo');
    expect(volta.layers, hasLength(2), reason: 'nenhuma camada pode se perder');
    expect(volta.layers.map((l) => l.name), ['Forma A', 'Forma B']);
    expect(volta.layers.last.startTime, const Duration(seconds: 3));
    expect(
      volta.layers.last.duration,
      const Duration(seconds: 3),
      reason: 'a duracao do clipe nao muda',
    );
  });

  test('transicao com efeito dentro tambem nao derruba a camada', () {
    final json = projectToJson(_projeto());
    final camadas = (json['layers'] as List).cast<Map<String, dynamic>>();
    camadas[1]['transitionIn'] = {
      ..._transicaoAntiga('a'),
      'type': 'effect',
      // Um efeito que saiu do catalogo: o caminho mais hostil que existe,
      // porque era justamente ele que estourava dentro do carregador.
      'effect': {'kind': 'glow', 'params': <String, dynamic>{}},
    };

    final volta = projectFromJson(json);
    expect(volta.layers, hasLength(2));
    expect(volta.layers.last.name, 'Forma B');
  });

  test('transicao ilegivel nao derruba nada', () {
    final json = projectToJson(_projeto());
    final camadas = (json['layers'] as List).cast<Map<String, dynamic>>();
    camadas[1]['transitionIn'] = 'lixo';

    final volta = projectFromJson(json);
    expect(volta.layers, hasLength(2));
  });

  test('salvar de novo nao reescreve a transicao', () {
    final json = projectToJson(_projeto());
    final camadas = (json['layers'] as List).cast<Map<String, dynamic>>();
    camadas[1]['transitionIn'] = _transicaoAntiga('a');

    final segunda = projectToJson(projectFromJson(json));
    for (final c in (segunda['layers'] as List).cast<Map<String, dynamic>>()) {
      expect(
        c.containsKey('transitionIn'),
        isFalse,
        reason: 'o campo nao pode voltar para o arquivo',
      );
    }
  });
}
