import 'package:flutter/foundation.dart';

import '../../../application/keyframe_clipboard.dart';
import '../../../domain/effect.dart' show efeitosInternos;
import '../../../domain/keyframe.dart' show kToleranciaDoKeyframe;
import '../../../domain/layer.dart';
import '../../../domain/video_project.dart' show LayerProp;
import '../shell/contrato.dart' show PropriedadeAtiva;

// ===========================================================================
// OS KEYFRAMES QUE A TIMELINE DESENHA
// ===========================================================================
//
// A camada e imutavel (editou, e outra instancia): toda conta daqui e
// guardada por INSTANCIA num `Expando`, e solta junto com a camada. O
// getter `keyframeTimes` junta nove conjuntos e aloca um `Duration` por
// marca — pedi-lo a cada pintura seria refazer a conta sessenta vezes por
// segundo durante o play.

/// A CHAVE DE UMA TRILHA animada (a linha da camada expandida): uma
/// propriedade da transformacao ou um efeito.
@immutable
class ChaveDaTrilha {
  const ChaveDaTrilha.transformacao(LayerProp this.prop) : efeitoId = null;
  const ChaveDaTrilha.efeito(String this.efeitoId) : prop = null;

  final LayerProp? prop;
  final String? efeitoId;

  /// Para chaves de teste e de widget.
  String get nome => prop?.name ?? 'efeito-$efeitoId';

  @override
  bool operator ==(Object other) =>
      other is ChaveDaTrilha &&
      other.prop == prop &&
      other.efeitoId == efeitoId;

  @override
  int get hashCode => Object.hash(prop, efeitoId);
}

/// UMA TRILHA ANIMADA: o que a linha expandida desenha.
@immutable
class TrilhaAnimada {
  const TrilhaAnimada({
    required this.chave,
    required this.rotulo,
    required this.temposUs,
  });

  final ChaveDaTrilha chave;

  /// Texto de UI (vai por `AppText`: os nomes das propriedades e dos
  /// efeitos do catalogo sao do app, nao do usuario).
  final String rotulo;

  /// Instantes LOCAIS (tempo da camada), em µs, ordenados.
  final List<int> temposUs;
}

/// As propriedades da transformacao que ganham linha, na ordem do painel.
const propriedadesDaTimeline = <LayerProp>[
  LayerProp.position,
  LayerProp.scale,
  LayerProp.rotation,
  LayerProp.opacity,
  LayerProp.skew,
  LayerProp.pivot,
];

String rotuloDaPropriedade(LayerProp p) => switch (p) {
  LayerProp.position => 'Posição',
  LayerProp.scale => 'Escala',
  LayerProp.rotation => 'Rotação',
  LayerProp.opacity => 'Opacidade',
  LayerProp.skew => 'Inclinação',
  LayerProp.pivot => 'Pivô',
  LayerProp.parent => 'Vínculo',
};

Set<int> _temposDaProp(Layer l, LayerProp p) => switch (p) {
  LayerProp.position => l.positionTimesUs,
  LayerProp.scale => l.scaleTimesUs,
  LayerProp.rotation => l.rotationTimesUs,
  LayerProp.opacity => l.opacityTimesUs,
  LayerProp.skew => l.skewTimesUs,
  LayerProp.pivot => l.pivotTimesUs,
  LayerProp.parent => const <int>{},
};

List<int> _ordenados(Iterable<int> us) =>
    List<int>.unmodifiable(us.toSet().toList()..sort());

final Expando<List<int>> _instantes = Expando('instantes-da-camada');

/// TODOS OS INSTANTES COM MARCA da camada (locais, µs, ordenados): os
/// losangos da linha principal. Inclui efeitos, mascaras e modulos.
List<int> instantesDaCamada(Layer l) => _instantes[l] ??= _ordenados([
  for (final t in l.keyframeTimes) t.inMicroseconds,
]);

final Expando<List<TrilhaAnimada>> _trilhas = Expando('trilhas-animadas');

/// AS TRILHAS ANIMADAS da camada: uma por propriedade da transformacao com
/// marca e uma por efeito com marca (o keyframe do efeito vale para todos
/// os parametros dele — e a mesma unidade do losango do painel).
List<TrilhaAnimada> trilhasAnimadas(Layer l) =>
    _trilhas[l] ??= List.unmodifiable([
      for (final p in propriedadesDaTimeline)
        if (_temposDaProp(l, p).isNotEmpty)
          TrilhaAnimada(
            chave: ChaveDaTrilha.transformacao(p),
            rotulo: rotuloDaPropriedade(p),
            temposUs: _ordenados(_temposDaProp(l, p)),
          ),
      for (final e in l.effects)
        if (!efeitosInternos.contains(e.type) && e.keyframeTimes.isNotEmpty)
          TrilhaAnimada(
            chave: ChaveDaTrilha.efeito(e.id),
            rotulo: e.specOuNulo?.name ?? e.type.name,
            temposUs: _ordenados([
              for (final t in e.keyframeTimes) t.inMicroseconds,
            ]),
          ),
    ]);

/// A trilha de [chave] na camada, ou nula (a propriedade deixou de ter
/// marca).
TrilhaAnimada? trilhaDe(Layer l, ChaveDaTrilha chave) {
  for (final t in trilhasAnimadas(l)) {
    if (t.chave == chave) return t;
  }
  return null;
}

/// OS INSTANTES DA PROPRIEDADE ATIVA na camada (locais, µs) — os losangos
/// que acendem. Nulo = nenhuma propriedade em foco: tudo aceso.
Set<int>? instantesAcesos(Layer l, PropriedadeAtiva? ativa) {
  if (ativa == null) return null;
  final prop = ativa.prop;
  if (prop != null) return _temposDaProp(l, prop);
  final efeito = ativa.efeitoId;
  if (efeito != null) {
    for (final e in l.effects) {
      if (e.id == efeito) {
        return {for (final t in e.keyframeTimes) t.inMicroseconds};
      }
    }
    return const <int>{};
  }
  final mascara = ativa.mascaraId;
  if (mascara != null) {
    for (final m in l.masks) {
      if (m.id != mascara) continue;
      return {
        for (final k in m.path.keyframes) k.time.inMicroseconds,
        for (final k in m.feather.keyframes) k.time.inMicroseconds,
        for (final k in m.featherY?.keyframes ?? const [])
          k.time.inMicroseconds,
        for (final k in m.opacity.keyframes) k.time.inMicroseconds,
        for (final k in m.expansion.keyframes) k.time.inMicroseconds,
      };
    }
    return const <int>{};
  }
  return null;
}

/// A trilha [chave] e a da propriedade ativa?
bool trilhaEstaAtiva(ChaveDaTrilha chave, PropriedadeAtiva? ativa) {
  if (ativa == null) return false;
  if (chave.prop != null) return ativa.prop == chave.prop;
  return ativa.efeitoId != null && ativa.efeitoId == chave.efeitoId;
}

/// A propriedade ativa que a trilha [chave] representa.
PropriedadeAtiva ativaDaTrilha(ChaveDaTrilha chave) => chave.prop != null
    ? PropriedadeAtiva.transformacao(chave.prop!)
    : PropriedadeAtiva.efeito(chave.efeitoId!);

/// [us] esta em [conjunto], pela regua de "mesmo instante" das trilhas?
bool temInstante(Set<int> conjunto, int us) {
  if (conjunto.contains(us)) return true;
  final tol = kToleranciaDoKeyframe.inMicroseconds;
  for (final t in conjunto) {
    if ((t - us).abs() < tol) return true;
  }
  return false;
}

/// OS INSTANTES SELECIONADOS da camada [layerId] (locais, µs): qualquer
/// propriedade. E o que a linha principal pinta como escolhido.
Set<int> instantesSelecionados(Set<MarcaSelecionada> selecao, String layerId) =>
    {
      for (final m in selecao)
        if (m.layerId == layerId) m.tempo.inMicroseconds,
    };

/// Os instantes selecionados de UMA propriedade da camada.
Set<int> instantesSelecionadosDaProp(
  Set<MarcaSelecionada> selecao,
  String layerId,
  LayerProp prop,
) => {
  for (final m in selecao)
    if (m.layerId == layerId && m.prop == prop) m.tempo.inMicroseconds,
};

/// O INSTANTE DO QUADRO [quadro], sempre para cima — a mesma conta do
/// `PlaybackController`: a marca cai onde o cabecote consegue parar.
int instanteDoQuadroUs(int quadro, int fps) => (quadro * 1000000 / fps).ceil();

/// O quadro EXATO (fracionario) de [us].
double quadroExato(num us, int fps) => us * fps / 1e6;
