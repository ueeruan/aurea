import 'dart:math' as math;

import 'package:vector_math/vector_math_64.dart' as vm;

import 'model_asset3d.dart';
import 'modelo_do_texto3d.dart';
import 'text_anim.dart';
import 'text_animator.dart';
import 'texto3d.dart';

/// TEXTO 3D COM OS ANIMADORES DO TEXTO NORMAL.
///
/// Os presets de texto ("Aparecer", "Quicar por letra", "Onda"...) rodam
/// no rasterizador 2D, letra por letra. A malha extrudada era um bloco
/// so — nao tinha letra para animar. Aqui o texto vira UM NO POR LETRA
/// dentro do proprio modelo (o mesmo esqueleto que um .glb animado usa),
/// e a cobertura de cada unidade — calculada pelo MESMO motor de
/// seletores do texto 2D — vira a matriz local da letra a cada quadro.
///
/// O que traduz e o que fica de fora:
///   - posicao X/Y/Z, escala (e por eixo), giro 2D, giro X/Y (3D de
///     verdade agora), espacamento e inclinacao: iguais ao 2D;
///   - OPACIDADE vira ESCALA (metal nao fica translucido; a letra
///     cresce/some) — e o que faz "Maquina de escrever" existir em 3D;
///   - desfoque e cor (matiz/saturacao/brilho) nao existem em malha e
///     sao ignorados.
///
/// A SAIDA conta do fim da camada. A duracao chega pelo render
/// ([Scene3D.fimDaCamada]); quando nao chega (estudio aberto por fora),
/// vale a duracao gravada quando a animacao foi aplicada.

// ------------------------------------------------------ serializacao

/// O mesmo formato do projeto (project_store), para o bloco viajar
/// dentro do `data` do modelo sem conversao.
Map<String, dynamic> textAnimParaJson(TextAnim a) => {
  'id': a.id,
  'spec': a.specId,
  'slot': a.slot.index,
  'unit': a.unit.index,
  'start': a.start.inMicroseconds,
  'dur': a.duration.inMicroseconds,
  'stag': a.stagger.inMicroseconds,
  'order': a.order.index,
  'ease': a.ease.index,
  'seed': a.seed,
  'on': a.enabled,
  'amp': a.amplitude,
  'freq': a.frequency,
  'decay': a.decay,
  'params': a.params,
};

TextAnim? textAnimDeJson(Map<dynamic, dynamic> m) {
  final spec = m['spec'];
  if (spec is! String || textAnimSpecById(spec) == null) return null;
  T idx<T>(Object? v, List<T> valores, T padrao) {
    final i = v is num ? v.toInt() : -1;
    return i >= 0 && i < valores.length ? valores[i] : padrao;
  }

  return TextAnim(
    id: m['id'] as String?,
    specId: spec,
    slot: idx(m['slot'], TextAnimSlot.values, TextAnimSlot.entrada),
    unit: idx(m['unit'], TextAnimUnit.values, TextAnimUnit.character),
    start: Duration(microseconds: (m['start'] as num? ?? 0).toInt()),
    duration: Duration(microseconds: (m['dur'] as num? ?? 600000).toInt()),
    stagger: Duration(microseconds: (m['stag'] as num? ?? 55000).toInt()),
    order: idx(m['order'], TextAnimOrder.values, TextAnimOrder.forward),
    ease: idx(m['ease'], TextAnimEase.values, TextAnimEase.desacelerar),
    seed: (m['seed'] as num? ?? 1).toInt(),
    enabled: m['on'] as bool? ?? true,
    amplitude: (m['amp'] as num? ?? 1).toDouble(),
    frequency: (m['freq'] as num? ?? 1.8).toDouble(),
    decay: (m['decay'] as num? ?? 5).toDouble(),
    params: {
      for (final e in (m['params'] as Map? ?? const {}).entries)
        if (e.value is num) e.key as String: (e.value as num).toDouble(),
    },
  );
}

// ---------------------------------------------------------- o modelo

/// O TEXTO 3D COM UM NO POR LETRA. Mesma geometria do modelo de bloco
/// unico; a diferenca e o esqueleto: no raiz + um no por letra (a letra
/// na origem do glifo, o no com a translacao do layout). Letra repetida
/// compartilha os buffers da malha — "ELEMENT" tem tres "E" e uma malha.
ModelAsset3D modeloDoTexto3DPorLetra(
  DisposicaoDoTexto3D disposicao,
  Texto3D t,
  double unidadesPorEm,
  String nome,
  EstiloDoTexto3D estilo,
) {
  final nodes = <Map<String, dynamic>>[
    {'name': nome},
  ];
  final primitivas = <Map<String, dynamic>>[];
  final unidades = <int>[];
  final centros = <List<double>>[];
  // Buffers convertidos uma vez por malha de letra (identidade): a letra
  // repetida reaproveita as MESMAS listas.
  final convertidas = <PrimitivaDoTexto3D, Map<String, dynamic>>{};
  for (final letra in disposicao.letras) {
    final ni = nodes.length;
    nodes.add({
      'name': 'letra${letra.indice}',
      'parent': 0,
      'translation': [letra.x, letra.y, 0.0],
    });
    final m = malhaDaLetra(letra.glifo, t, unidadesPorEm);
    unidades.add(letra.unidade);
    centros.add([m.centroX, m.centroY]);
    for (final parte in ParteDoTexto3D.values) {
      final p = m.partes[parte];
      if (p == null || p.triangulos == 0) continue;
      final buffers = convertidas[p] ??= () {
        final n = p.vertices;
        return <String, dynamic>{
          'positions': [
            for (var i = 0; i < n; i++)
              [
                p.posicoes[3 * i],
                p.posicoes[3 * i + 1],
                p.posicoes[3 * i + 2],
              ],
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
        };
      }();
      primitivas.add({
        'node': ni,
        ...buffers,
        'material': parte.index,
      });
    }
  }
  return ModelAsset3D({
    'version': 1,
    'name': nome,
    'nodes': nodes,
    'primitives': primitivas,
    'materials': materiaisDoTexto3D(estilo),
    'skins': const [],
    'clips': const [],
    'texto': {
      't': t.texto.replaceAll('\r', ''),
      'u': unidades,
      'c': centros,
      'tam': t.tamanho,
      ...blocoDaPoseDoTexto3D(
        t.rotLetraX,
        t.rotLetraY,
        t.rotLetraZ,
        t.ajustes,
      ),
      'anims': <Map<String, dynamic>>[],
      'fim': 0,
    },
  });
}

/// O PEDACO "POSE" DO BLOCO DE TEXTO: o giro por letra e os ajustes por
/// faixa de caracteres.
///
/// O QUARTO NUMERO DE `rot` E UMA BANDEIRA, e nao um angulo.
/// `ModelAsset3D.temAnimacaoDeTexto` decide se vale a pena pedir as
/// matrizes por letra olhando so para `rot` (algum numero diferente de
/// zero) e para `anims` (lista nao vazia). Um texto que tem SO ajuste por
/// caractere nao tem nem um nem outro, e a pose ficaria gravada sem nunca
/// ser aplicada — a letra escolhida nao sairia do lugar. O quarto numero e
/// a quantidade de ajustes: liga a bandeira sem inventar um giro, e quem
/// le a pose continua lendo apenas os tres primeiros.
Map<String, Object> blocoDaPoseDoTexto3D(
  double rotX,
  double rotY,
  double rotZ,
  List<AjusteDeCaracteres> ajustes,
) {
  final vivos = [
    for (final a in ajustes)
      if (!a.inerte) a,
  ];
  final temRot = rotX != 0 || rotY != 0 || rotZ != 0;
  return {
    if (temRot || vivos.isNotEmpty)
      'rot': [rotX, rotY, rotZ, vivos.length.toDouble()],
    if (vivos.isNotEmpty) 'ajustes': [for (final a in vivos) a.toJson()],
  };
}

/// UM MODELO NOVO com as mesmas malhas e estes ajustes por caractere.
///
/// Mexer numa letra NAO refaz geometria: o unico lugar que muda e o bloco
/// `texto` do modelo, e por isso o controle acompanha o dedo em vez de
/// esperar o extrusor ler a fonte e remontar cada glifo.
ModelAsset3D texto3DComAjustes(
  ModelAsset3D asset,
  List<AjusteDeCaracteres> ajustes,
) {
  final texto = asset.data['texto'];
  if (texto is! Map) return asset;
  final base = texto.cast<String, dynamic>();
  final rot = base['rot'] as List?;
  double angulo(int i) =>
      rot != null && rot.length > i && rot[i] is num
      ? (rot[i] as num).toDouble()
      : 0.0;
  final limpo = {...base}
    ..remove('rot')
    ..remove('ajustes');
  return ModelAsset3D({
    ...asset.data,
    'texto': {
      ...limpo,
      ...blocoDaPoseDoTexto3D(angulo(0), angulo(1), angulo(2), ajustes),
    },
  });
}

/// As animacoes gravadas no modelo (vazio = texto parado).
List<TextAnim> animsDoTexto3D(ModelAsset3D asset) {
  final texto = asset.data['texto'];
  if (texto is! Map) return const [];
  return [
    for (final a in texto['anims'] as List? ?? const [])
      if (a is Map) ?textAnimDeJson(a),
  ];
}

/// Um modelo NOVO com as mesmas malhas e estas animacoes. [fimDaCamada]
/// fica gravado como ancora da SAIDA para quando o render nao souber a
/// duracao da camada.
ModelAsset3D texto3DComAnims(
  ModelAsset3D asset,
  List<TextAnim> anims, {
  required Duration fimDaCamada,
}) {
  final texto = asset.data['texto'];
  if (texto is! Map) return asset;
  return ModelAsset3D({
    ...asset.data,
    'texto': {
      ...texto.cast<String, dynamic>(),
      'anims': [for (final a in anims) textAnimParaJson(a)],
      'fim': fimDaCamada.inMicroseconds,
    },
  });
}

// ----------------------------------------------------- a avaliacao

final _unitsGuardadas = Expando<TextUnits>('unidades do texto 3D');
final _compilados = Expando<(int, List<TextAnimator>)>('animadores compilados');

/// Espelho da pilha do pintor 2D (`animated_text.dart`): os mesmos
/// campos, a mesma ordem de aplicacao.
class _EstadoDaUnidade {
  double dx = 0, dy = 0, dz = 0, rotacao = 0, tracking = 0;
  double inclinacao = 0, rotX = 0, rotY = 0;
  double escalaP = 100, opacidadeP = 100, escalaXP = 100, escalaYP = 100;

  /// O AJUSTE POR CARACTERE, ja em UNIDADES DA CENA (+X direita, +Y cima,
  /// +Z para quem olha). Os campos acima vem do motor 2D e passam pela
  /// conversao de eixo; estes nao — o painel fala a lingua da cena, que e
  /// a que o dono ve quando empurra uma letra no Z.
  double ex = 0, ey = 0, ez = 0;

  void aplicar(AnimatorProperty p, Duration t, double c) {
    switch (p.type) {
      case TextAnimProp.positionX:
        dx = p.apply(dx, t, c);
      case TextAnimProp.positionY:
        dy = p.apply(dy, t, c);
      case TextAnimProp.positionZ:
        dz = p.apply(dz, t, c);
      case TextAnimProp.rotation:
        rotacao = p.apply(rotacao, t, c);
      case TextAnimProp.rotationX:
        rotX = p.apply(rotX, t, c);
      case TextAnimProp.rotationY:
        rotY = p.apply(rotY, t, c);
      case TextAnimProp.tracking:
        tracking = p.apply(tracking, t, c);
      case TextAnimProp.scale:
        escalaP = p.apply(escalaP, t, c);
      case TextAnimProp.scaleX:
        escalaXP = p.apply(escalaXP, t, c);
      case TextAnimProp.scaleY:
        escalaYP = p.apply(escalaYP, t, c);
      case TextAnimProp.opacity:
        opacidadeP = p.apply(opacidadeP, t, c);
      case TextAnimProp.skew:
        inclinacao = p.apply(inclinacao, t, c);
      // Sem desfoque nem cor em malha extrudada.
      case TextAnimProp.blur:
      case TextAnimProp.hue:
      case TextAnimProp.saturation:
      case TextAnimProp.brightness:
        break;
    }
  }
}

/// A MATRIZ LOCAL DE CADA LETRA no instante [t] (indice = no do modelo;
/// nulo = no sem override). Nulo quando o modelo nao tem texto animado.
///
/// Conversao 2D -> cena: a composicao tem Y para baixo e a cena Y para
/// cima, entao dy, giro 2D e giro X trocam de sinal (conjugacao pela
/// reflexao) e o giro Y fica. "Longe" (dz positivo no 2D) e -z na cena,
/// que olha de +z para a frente do texto.
List<vm.Matrix4?>? matrizesDoTextoAnimado(
  Map<String, dynamic> data,
  Duration t,
  Duration? fimDaCamada,
) {
  final texto = data['texto'];
  if (texto is! Map) return null;
  final brutas = texto['anims'] as List? ?? const [];
  // A ROTACAO POR LETRA VALE MESMO SEM ANIMADOR NENHUM: ela e a pose de
  // repouso de cada letra, e os animadores somam por cima dela.
  final rot = texto['rot'] as List?;
  final rotX = rot != null && rot.isNotEmpty ? (rot[0] as num).toDouble() : 0.0;
  final rotY = rot != null && rot.length > 1 ? (rot[1] as num).toDouble() : 0.0;
  final rotZ = rot != null && rot.length > 2 ? (rot[2] as num).toDouble() : 0.0;
  final temRot = rotX != 0 || rotY != 0 || rotZ != 0;
  // OS AJUSTES POR FAIXA DE CARACTERES valem sozinhos, como o giro: sao
  // pose de repouso das letras escolhidas, e os animadores somam por cima.
  final ajustes = ajustesDeJson(texto['ajustes']);
  final temAjuste = ajustes.isNotEmpty;
  if (brutas.isEmpty && !temRot && !temAjuste) return null;
  final s = texto['t'];
  final u = texto['u'] as List?;
  final c = texto['c'] as List?;
  final nodes = data['nodes'] as List? ?? const [];
  if (s is! String || u == null || c == null || u.isEmpty) return null;

  final anims = <TextAnim>[
    for (final a in brutas)
      if (a is Map) ?textAnimDeJson(a),
  ];
  if (anims.isEmpty && !temRot && !temAjuste) return null;

  final units = _unitsGuardadas[data] ??= TextUnits.of(s);
  final fim =
      fimDaCamada ??
      Duration(microseconds: ((texto['fim'] as num?) ?? 0).toInt());

  var compilado = _compilados[data];
  if (compilado == null || compilado.$1 != fim.inMicroseconds) {
    compilado = (
      fim.inMicroseconds,
      compileTextAnims(
        anims,
        layerDuration: fim,
        contar: (unit) => switch (unit) {
          TextAnimUnit.character || TextAnimUnit.all => units.charCount,
          TextAnimUnit.charactersNoSpaces => units.charNoSpaceCount,
          TextAnimUnit.word => units.wordCount,
          TextAnimUnit.line => units.lineCount,
        },
      ),
    );
    _compilados[data] = compilado;
  }
  final animators = [
    for (final a in compilado.$2)
      if (a.enabled && a.properties.isNotEmpty) a,
  ];
  // SEM ANIMADOR ATIVO AINDA PODE HAVER O GIRO POR LETRA, que e pose de
  // repouso: sair aqui o jogava fora, e o controle ficava morto.
  if (animators.isEmpty && !temRot && !temAjuste) return null;

  // Distancias dos presets sao pixels sobre o texto 2D padrao (120px de
  // corpo); aqui viram unidades da cena na proporcao do tamanho do em.
  final tam = ((texto['tam'] as num?) ?? 100).toDouble();

  // A pilha de cada unidade, com o espacamento acumulado como no 2D:
  // a unidade i desloca pela soma do tracking das anteriores.
  final estados = List<_EstadoDaUnidade?>.filled(units.length, null);
  final desvioDoTracking = List<double>.filled(units.length, 0);
  var acumulado = 0.0;
  for (var ci = 0; ci < units.length; ci++) {
    desvioDoTracking[ci] = acumulado;
    final e = _EstadoDaUnidade()
      ..rotX = rotX
      ..rotY = rotY
      ..rotacao = rotZ;
    for (final a in animators) {
      final cobertura = units.coverageFor(
        a.selectors,
        ci,
        t,
        allowOvershoot: a.allowOvershoot,
      );
      for (final p in a.properties) {
        e.aplicar(p, t, cobertura);
      }
    }
    // O AJUSTE DA FAIXA, POR CIMA DE TUDO. Cada faixa que pega esta letra
    // soma a sua parte: mover "BCD" e mover o C tambem, e o resultado e a
    // soma — como duas camadas de ajuste, nao uma vencendo a outra.
    for (final aj in ajustes) {
      if (!aj.pega(ci)) continue;
      e.ex += aj.valorEm(MedidaDoCaractere.x, t);
      e.ey += aj.valorEm(MedidaDoCaractere.y, t);
      e.ez += aj.valorEm(MedidaDoCaractere.z, t);
      e.rotX += aj.valorEm(MedidaDoCaractere.girX, t);
      e.rotY += aj.valorEm(MedidaDoCaractere.girY, t);
      e.rotacao += aj.valorEm(MedidaDoCaractere.girZ, t);
      e.escalaP *= aj.valorEm(MedidaDoCaractere.escala, t);
      // ESPACAMENTO abre a faixa letra a letra (acumula dentro dela, e so
      // dentro dela: as letras de fora nao andam junto); OFFSET desliza a
      // faixa inteira pela linha do texto. As duas medem em FRACAO DO
      // CORPO, entao valem o mesmo em qualquer tamanho de texto.
      final passo = (ci - (aj.inicio < 0 ? 0 : aj.inicio)).toDouble();
      e.ex +=
          (aj.valorEm(MedidaDoCaractere.espacamento, t) * passo +
              aj.valorEm(MedidaDoCaractere.offset, t)) *
          tam;
    }
    acumulado += e.tracking;
    estados[ci] = e;
  }

  final fator = tam / 120.0;
  const d2r = math.pi / 180;
  final out = List<vm.Matrix4?>.filled(nodes.length, null);
  for (var i = 0; i < u.length && i + 1 < nodes.length; i++) {
    final ci = (u[i] as num).toInt();
    if (ci < 0 || ci >= units.length) continue;
    final e = estados[ci]!;
    final base = nodes[i + 1];
    final tr = base is Map ? base['translation'] as List? : null;
    final px = tr == null ? 0.0 : (tr[0] as num).toDouble();
    final py = tr == null ? 0.0 : (tr[1] as num).toDouble();
    final centro = i < c.length ? c[i] as List? : null;
    final cx = centro == null ? 0.0 : (centro[0] as num).toDouble();
    final cy = centro == null || centro.length < 2
        ? 0.0
        : (centro[1] as num).toDouble();

    final op = (e.opacidadeP / 100).clamp(0.0, 1.0);
    var sx = math.max(0.0, (e.escalaP / 100) * (e.escalaXP / 100)) * op;
    var sy = math.max(0.0, (e.escalaP / 100) * (e.escalaYP / 100)) * op;
    var sz = math.max(0.0, e.escalaP / 100) * op;
    if (op <= 0.001) {
      sx = 0;
      sy = 0;
      sz = 0;
    }

    final m = vm.Matrix4.identity()
      ..setTranslationRaw(
        px + cx + (e.dx + desvioDoTracking[ci]) * fator + e.ex,
        py + cy - e.dy * fator + e.ey,
        -e.dz * fator + e.ez,
      );
    if (e.rotacao != 0) m.rotateZ(-e.rotacao * d2r);
    if (e.rotX != 0) m.rotateX(-e.rotX * d2r);
    if (e.rotY != 0) m.rotateY(e.rotY * d2r);
    if (e.inclinacao != 0) {
      m.multiply(
        vm.Matrix4.identity()..setEntry(0, 1, math.tan(e.inclinacao * d2r)),
      );
    }
    if (sx != 1 || sy != 1 || sz != 1) {
      m.multiply(vm.Matrix4.diagonal3Values(sx, sy, sz));
    }
    m.translateByDouble(-cx, -cy, 0, 1);
    out[i + 1] = m;
  }
  return out;
}
