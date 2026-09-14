import 'model_asset3d.dart';
import 'texto3d.dart';

/// OS METAIS DO TEXTO 3D (o "Element 3D" que o dono pediu). Frente,
/// chanfro e lateral tem material proprio: o chanfro mais polido e claro
/// pega o brilho da quina, a lateral mais escura da o volume — e a
/// diferenca entre letra de metal e letra de plastico.
enum EstiloDoTexto3D { ouro, cromo, acoEscovado, brancoFosco }

String nomeDoEstiloDoTexto3D(EstiloDoTexto3D e) => switch (e) {
  EstiloDoTexto3D.ouro => 'Ouro',
  EstiloDoTexto3D.cromo => 'Cromo',
  EstiloDoTexto3D.acoEscovado => 'Aço escovado',
  EstiloDoTexto3D.brancoFosco => 'Branco fosco',
};

Map<String, dynamic> _metal(String nome, int cor, double rug, double metal) {
  final r = ((cor >> 16) & 0xFF) / 255, g = ((cor >> 8) & 0xFF) / 255;
  final b = (cor & 0xFF) / 255;
  return {
    'name': nome,
    'color': [r, g, b, 1.0],
    'metallic': metal,
    'roughness': rug,
    'emissive': 0.0,
  };
}

/// Frente, chanfro e lateral, nesta ordem (o indice e o de ParteDoTexto3D).
List<Map<String, dynamic>> materiaisDoTexto3D(EstiloDoTexto3D e) =>
    switch (e) {
      EstiloDoTexto3D.ouro => [
        _metal('Ouro', 0xFFE39D, .16, 1),
        _metal('Ouro polido', 0xFFF1C4, .07, 1),
        _metal('Ouro escuro', 0xC9A34E, .28, 1),
      ],
      EstiloDoTexto3D.cromo => [
        _metal('Cromo', 0xD7D9DA, .05, 1),
        _metal('Cromo polido', 0xF2F4F5, .02, 1),
        _metal('Cromo escuro', 0x9EA3A8, .12, 1),
      ],
      EstiloDoTexto3D.acoEscovado => [
        _metal('Aço escovado', 0xC5C7C8, .34, 1),
        _metal('Aço polido', 0xE3E5E6, .12, 1),
        _metal('Aço escuro', 0x8D9196, .4, 1),
      ],
      EstiloDoTexto3D.brancoFosco => [
        _metal('Branco', 0xF2F2F2, .6, 0),
        _metal('Branco brilhante', 0xFFFFFF, .3, 0),
        _metal('Cinza', 0xC9CCD1, .65, 0),
      ],
    };

/// A MALHA DO TEXTO VIRA UM MODELO em memoria, pelo mesmo caminho dos
/// modelos importados: material por parte, normais proprias (o pintor de
/// CPU nao vira a normal de modelo) e o rascunho sem chanfro como nivel de
/// detalhe.
ModelAsset3D modeloDoTexto3D(
  MalhaDoTexto3D malha,
  String nome,
  EstiloDoTexto3D estilo,
) {
  final primitivas = <Map<String, dynamic>>[];
  for (final parte in ParteDoTexto3D.values) {
    final p = malha.partes[parte];
    if (p == null || p.triangulos == 0) continue;
    final n = p.vertices;
    primitivas.add({
      'node': 0,
      'positions': [
        for (var i = 0; i < n; i++)
          [p.posicoes[3 * i], p.posicoes[3 * i + 1], p.posicoes[3 * i + 2]],
      ],
      'normals': [
        for (var i = 0; i < n; i++)
          [p.normais[3 * i], p.normais[3 * i + 1], p.normais[3 * i + 2]],
      ],
      'uv': [
        for (var i = 0; i < n; i++) [p.uvs[2 * i], p.uvs[2 * i + 1]],
      ],
      'indices': p.indices.toList(),
      'lods': [p.indicesDoRascunho.toList()],
      'material': parte.index,
    });
  }
  return ModelAsset3D({
    'version': 1,
    'name': nome,
    'nodes': [
      {'name': nome},
    ],
    'primitives': primitivas,
    'materials': materiaisDoTexto3D(estilo),
    'skins': const [],
    'clips': const [],
  });
}
