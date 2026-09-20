import 'dart:math' as math;
import 'dart:typed_data';

import 'package:aurea_meshopt/aurea_meshopt.dart';

import 'packed_model_vectors.dart';

/// PREPARO DAS MALHAS DE UM MODELO RECEM-IMPORTADO, em C++ (meshoptimizer).
///
/// Roda UMA vez, no isolate da importacao — o mesmo que ja le e decodifica
/// o arquivo —, e o resultado vai para o projeto. Por primitiva:
///
///  1. SOLDA: vertices iguais em TODOS os atributos (posicao, normal, UV,
///     ossos, pesos, morphs) viram um. O FBX entrega um vertice por canto
///     de triangulo: uma malha de quads chegava com seis vezes os vertices;
///  2. CACHE DE VERTICES: reordena os triangulos para a GPU reaproveitar o
///     que acabou de transformar. Antes isso rodava na ponte da cena 3D,
///     num isolate aberto a cada malha — ~1,4 s sincrono no iPhone 13 — e
///     reenviava a geometria. So em material opaco: transparente depende
///     da ordem de desenho;
///  3. BUSCA DE VERTICES: renumera os vertices na ordem em que os
///     triangulos os usam, e larga os que nenhum triangulo usa;
///  4. NIVEIS DE DETALHE: dois indices simplificados (1/4 e 1/16 dos
///     triangulos) sobre os MESMOS vertices — rig e morph continuam
///     valendo. O pintor de CPU usa o nivel que cabe no teto dele, no
///     lugar do "uma face a cada N" que abria buracos na malha.
///
/// Nada disso muda o que o modelo e: as mesmas superficies, os mesmos
/// materiais, a mesma animacao. Qualquer falha nativa deixa a primitiva
/// como veio.
///
/// COM [alvoDeTriangulos] A MALHA PRINCIPAL TAMBEM DESCE ("Otimizar
/// automaticamente" do aviso de modelo pesado). E o UNICO caso em que a
/// importacao muda o que o modelo e, e so quando o dono pediu: entre a
/// SOLDA e o CACHE, o mesmo simplificador dos niveis de detalhe troca os
/// indices da primitiva. Os vertices nao se movem — so deixam de ser
/// usados, e a BUSCA os larga levando ossos, pesos e morphs junto —, entao
/// rig e animacao continuam valendo. O formato que o motor recebe e o
/// mesmo: posicoes, normais, UV e indices, so que menos.
void otimizarMalhasImportadas(
  Map<String, dynamic> data, {
  int? alvoDeTriangulos,
}) {
  final materiais = data['materials'] as List? ?? const [];
  final primitivas = data['primitives'] as List? ?? const [];
  final alvos = alvosPorPrimitiva([
    for (final p in primitivas)
      p is Map && p['indices'] is List ? (p['indices'] as List).length ~/ 3 : 0,
  ], alvoDeTriangulos);
  var aproximada = false;
  for (final (i, bruta) in primitivas.indexed) {
    final p = bruta as Map<String, dynamic>;
    try {
      final alvo = alvos[i];
      if (_otimizar(
        p,
        materiais,
        alvoDeIndices: alvo == null ? null : alvo * 3,
      )) {
        aproximada = true;
      }
    } catch (_) {
      // Primitiva estranha (atributo com contagem diferente, indice fora):
      // fica como veio. O importador ja validou o que precisava.
    }
  }
  if (aproximada) {
    const aviso =
        'Parte da malha só chegou ao alvo no modo aproximado da redução: '
        'confira as bordas e as costuras do modelo.';
    final avisos = data['warnings'];
    if (avisos is List && !avisos.contains(aviso)) avisos.add(aviso);
  }
}

/// QUANTO CADA PRIMITIVA DESCE para o modelo inteiro chegar a [alvo].
///
/// Proporcional a fatia de cada uma no total, com uma excecao: primitiva
/// pequena (abaixo de [trianguloMinimoParaLod]) fica como esta — botoes,
/// olhos e parafusos somem inteiros quando o simplificador tira deles a
/// mesma fracao que tira do corpo. O que elas nao cedem sai das grandes.
/// `null` na posicao = nao reduzir aquela primitiva.
List<int?> alvosPorPrimitiva(List<int> triangulos, int? alvo) {
  final nada = List<int?>.filled(triangulos.length, null);
  if (alvo == null || alvo <= 0) return nada;
  final total = triangulos.fold<int>(0, (a, b) => a + b);
  if (total <= alvo) return nada;
  var pequenas = 0, grandes = 0;
  for (final t in triangulos) {
    if (t < trianguloMinimoParaLod) {
      pequenas += t;
    } else {
      grandes += t;
    }
  }
  if (grandes == 0) return nada;
  // O PISO DE 10%: um modelo feito de mil pecas pequenas e uma grande nao
  // pode zerar a grande para pagar a conta das outras.
  final verba = math.max(alvo - pequenas, grandes ~/ 10);
  return [
    for (final t in triangulos)
      if (t < trianguloMinimoParaLod)
        null
      else
        math.max(trianguloMinimoParaLod ~/ 2, (t * verba / grandes).round()),
  ];
}

/// Triangulos a partir dos quais vale gerar niveis de detalhe.
const trianguloMinimoParaLod = 1200;

List<double> _numeros(Object? v) => [
  for (final n in v as List) (n as num).toDouble(),
];

/// Devolve `true` quando a reducao pedida so chegou perto do alvo no modo
/// aproximado (o chamador avisa o dono).
bool _otimizar(
  Map<String, dynamic> p,
  List materiais, {
  int? alvoDeIndices,
}) {
  final posicoes = p['positions'] as List;
  final n = posicoes.length;
  final indicesBrutos = p['indices'] as List;
  if (n < 3 || indicesBrutos.length < 3 || indicesBrutos.length % 3 != 0) {
    return false;
  }
  var indices = Uint32List.fromList([
    for (final i in indicesBrutos) i as int,
  ]);
  if (indices.any((i) => i >= n)) return false;

  // ------------------------------------------------------ atributos
  // Cada atributo por vertice vira um fluxo de floats para a solda, e
  // sabe se reescrever com a tabela de renumeracao.
  final atributos = <_Atributo>[
    _Atributo.vetores(p, 'positions', 3, n),
    if (p['normals'] != null) _Atributo.vetores(p, 'normals', 3, n),
    if (p['uv'] != null) _Atributo.vetores(p, 'uv', 2, n),
    if (p['joints'] != null) _Atributo.inteiros(p, 'joints', n),
    if (p['weights'] != null) _Atributo.vetores(p, 'weights', null, n),
    for (final (i, alvo) in (p['targets'] as List? ?? const []).indexed)
      for (final chave in const ['positions', 'normals'])
        if ((alvo as Map)[chave] != null)
          _Atributo.vetores(alvo.cast<String, dynamic>(), chave, 3, n,
              rotulo: 'targets[$i].$chave'),
  ];
  if (atributos.any((a) => a.fluxo.length != n * a.componentes)) return false;

  // 1. SOLDA
  var total = n;
  final soldada = weldVertices(
    indices,
    total,
    [for (final a in atributos) a.fluxo],
    [for (final a in atributos) a.componentes],
  );
  if (soldada != null && soldada.vertexCount < total) {
    for (final a in atributos) {
      a.renumerar(soldada.remap, soldada.vertexCount);
    }
    indices = soldada.indices;
    total = soldada.vertexCount;
  } else if (soldada != null) {
    indices = soldada.indices;
  }

  // 1b. REDUCAO DA MALHA PRINCIPAL (so quando o dono pediu).
  //
  // DEPOIS DA SOLDA, obrigatoriamente: o simplificador anda pela
  // topologia, e o vertice-por-canto do FBX e uma sopa de triangulos
  // soltos que ele nao consegue juntar. ANTES do cache e da busca: os
  // dois passam a trabalhar na malha que vai ficar, e a busca larga os
  // vertices que sobraram sem dono.
  var aproximada = false;
  if (alvoDeIndices != null && alvoDeIndices < indices.length) {
    final pos = atributos.first.fluxo;
    final normais = atributos.length > 1 && atributos[1].chave == 'normals'
        ? atributos[1].fluxo
        : null;
    ({Uint32List indices, double error})? melhor;
    // O ERRO SOBE AOS POUCOS: 2% do tamanho do modelo resolve a malha
    // escaneada (densa e lisa); a malha de pecas e costuras precisa de
    // mais folga para chegar ao alvo. Para no primeiro que chega perto.
    for (final erro in const [.02, .05, .12]) {
      final r = simplifyMesh(
        indices,
        pos,
        normals: normais,
        targetIndexCount: alvoDeIndices,
        targetError: erro,
      );
      if (r == null || r.indices.isEmpty) break;
      if (melhor == null || r.indices.length < melhor.indices.length) {
        melhor = r;
      }
      if (r.indices.length <= alvoDeIndices * 1.25) break;
    }
    // ULTIMO RECURSO, o modo que ignora a topologia: so quando o resultado
    // ainda passa do DOBRO do alvo. O dono pediu a reducao porque o modelo
    // nao cabe; devolver a malha quase cheia seria nao atender. O chamador
    // avisa que houve aproximacao.
    if (melhor == null || melhor.indices.length > alvoDeIndices * 2) {
      final r = simplifyMesh(
        indices,
        pos,
        targetIndexCount: alvoDeIndices,
        targetError: .1,
        sloppy: true,
      );
      if (r != null &&
          r.indices.isNotEmpty &&
          (melhor == null || r.indices.length < melhor.indices.length)) {
        melhor = r;
        aproximada = true;
      }
    }
    if (melhor != null &&
        melhor.indices.length >= 3 &&
        melhor.indices.length < indices.length &&
        !melhor.indices.any((i) => i >= total)) {
      indices = melhor.indices;
    } else {
      aproximada = false;
    }
  }

  // 2. CACHE (so opaco)
  final mi = p['material'] as int? ?? -1;
  final transparente =
      mi >= 0 &&
      mi < materiais.length &&
      (materiais[mi] as Map)['alpha'] == 'BLEND';
  if (!transparente && indices.length >= 48) {
    indices = optimizeVertexCache(indices, total);
  }

  // 3. BUSCA
  final busca = optimizeVertexFetch(indices, total);
  if (busca != null) {
    for (final a in atributos) {
      a.renumerar(busca.remap, busca.vertexCount);
    }
    indices = busca.indices;
    total = busca.vertexCount;
  }

  for (final a in atributos) {
    a.gravar();
  }
  p['indices'] = List<int>.from(indices);

  // 4. NIVEIS DE DETALHE
  p.remove('lods');
  if (indices.length ~/ 3 >= trianguloMinimoParaLod) {
    final pos = atributos.first.fluxo;
    final normais = atributos.length > 1 && atributos[1].chave == 'normals'
        ? atributos[1].fluxo
        : null;
    final lods = <List<int>>[];
    var base = indices;
    for (final divisor in const [4, 16]) {
      final alvo = math.max(300 * 3, indices.length ~/ divisor);
      if (alvo >= base.length) break;
      var r = simplifyMesh(
        base,
        pos,
        normals: normais,
        targetIndexCount: alvo,
        targetError: divisor == 4 ? .02 : .06,
      );
      // Malha de pecas soltas (ou com costuras demais) nao desce: o modo
      // desleixado chega ao alvo — e so rascunho do pintor de CPU.
      if (r == null || r.indices.length > alvo * 1.6) {
        r = simplifyMesh(
          base,
          pos,
          targetIndexCount: alvo,
          targetError: .1,
          sloppy: true,
        );
      }
      if (r == null || r.indices.isEmpty || r.indices.length >= base.length) {
        break;
      }
      lods.add(List<int>.from(r.indices));
      base = r.indices;
    }
    if (lods.isNotEmpty) p['lods'] = lods;
  }
  return aproximada;
}

class _Atributo {
  _Atributo._(this.dono, this.chave, this.componentes, this.fluxo, this.inteiro);

  /// Vetores de floats (posicao, normal, UV, pesos, morph). [componentes]
  /// null = o maior tamanho encontrado (pesos de 4 ou 8 influencias).
  factory _Atributo.vetores(
    Map<String, dynamic> dono,
    String chave,
    int? componentes,
    int n, {
    String? rotulo,
  }) {
    final lista = dono[chave] as List;
    final k = componentes ??
        lista.fold<int>(1, (m, v) => math.max(m, (v as List).length));
    final fluxo = Float32List(n * k);
    if (lista is PackedModelVectors && lista.components == k) {
      for (var i = 0; i < math.min(n * k, lista.data.length); i++) {
        fluxo[i] = lista.data[i];
      }
    } else {
      for (var i = 0; i < math.min(n, lista.length); i++) {
        final v = _numeros(lista[i]);
        for (var c = 0; c < math.min(k, v.length); c++) {
          fluxo[i * k + c] = v[c];
        }
      }
    }
    return _Atributo._(dono, chave, k, fluxo, false);
  }

  /// Indices de ossos: guardados como inteiros, soldados como floats.
  factory _Atributo.inteiros(Map<String, dynamic> dono, String chave, int n) {
    final lista = dono[chave] as List;
    final k = lista.fold<int>(1, (m, v) => math.max(m, (v as List).length));
    final fluxo = Float32List(n * k);
    for (var i = 0; i < math.min(n, lista.length); i++) {
      final v = lista[i] as List;
      for (var c = 0; c < math.min(k, v.length); c++) {
        fluxo[i * k + c] = (v[c] as num).toDouble();
      }
    }
    return _Atributo._(dono, chave, k, fluxo, true);
  }

  final Map<String, dynamic> dono;
  final String chave;
  final int componentes;
  Float32List fluxo;
  final bool inteiro;
  bool _mudou = false;

  void renumerar(Uint32List tabela, int novoTotal) {
    final k = componentes;
    final novo = Float32List(novoTotal * k);
    for (var i = 0; i < tabela.length; i++) {
      final j = tabela[i];
      if (j == 0xFFFFFFFF || j >= novoTotal) continue;
      for (var c = 0; c < k; c++) {
        novo[j * k + c] = fluxo[i * k + c];
      }
    }
    fluxo = novo;
    _mudou = true;
  }

  /// Devolve o atributo ao mapa do modelo, no formato que o resto do app
  /// le: vetores empacotados para floats, listas de inteiros para ossos.
  void gravar() {
    if (!_mudou) return;
    final n = fluxo.length ~/ componentes;
    if (inteiro) {
      final original = dono[chave] as List;
      final tamanho = original.isEmpty ? componentes : (original.first as List).length;
      dono[chave] = [
        for (var i = 0; i < n; i++)
          [
            for (var c = 0; c < tamanho; c++)
              fluxo[i * componentes + c].round(),
          ],
      ];
      return;
    }
    if (chave == 'weights') {
      final original = dono[chave] as List;
      final tamanho = original.isEmpty ? componentes : (original.first as List).length;
      dono[chave] = [
        for (var i = 0; i < n; i++)
          [
            for (var c = 0; c < tamanho; c++)
              fluxo[i * componentes + c].toDouble(),
          ],
      ];
      return;
    }
    final vetores = PackedModelVectors(n, componentes);
    for (var i = 0; i < fluxo.length; i++) {
      vetores.data[i] = fluxo[i];
    }
    dono[chave] = vetores;
  }
}
