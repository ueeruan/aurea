import 'dart:ui' show Offset;

import 'package:flutter/foundation.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../domain/keyframe.dart';
import '../domain/layer.dart';
import '../domain/video_project.dart' show LayerProp;

/// UMA MARCA SELECIONADA: de que camada, de que propriedade, em que
/// instante (tempo LOCAL da camada, o mesmo das trilhas).
///
/// E um registro, e nao uma classe, porque a selecao e um CONJUNTO: dois
/// registros com os mesmos tres campos sao o mesmo elemento, sem `==`
/// escrito a mao para esquecer de atualizar.
typedef MarcaSelecionada = ({String layerId, LayerProp prop, Duration tempo});

/// A SELECAO DE KEYFRAMES — estado de tela, nunca vai para o arquivo.
///
/// Conjunto vazio = nada selecionado. Quem mexe usa [alternarMarca] (o
/// toque soma ou tira) e quem desenha pergunta a [marcaSelecionada]; as
/// duas usam a regua de "mesmo instante" das trilhas
/// ([kToleranciaDoKeyframe]), porque o tempo que vem do dedo quase nunca e
/// o microssegundo exato da marca.
final keyframesSelecionadosProvider = StateProvider<Set<MarcaSelecionada>>(
  (ref) => const <MarcaSelecionada>{},
);

bool _mesmaMarca(MarcaSelecionada a, MarcaSelecionada b) =>
    a.layerId == b.layerId &&
    a.prop == b.prop &&
    (a.tempo - b.tempo).abs() < kToleranciaDoKeyframe;

/// [marca] esta na [selecao]?
bool marcaSelecionada(Set<MarcaSelecionada> selecao, MarcaSelecionada marca) =>
    selecao.any((m) => _mesmaMarca(m, marca));

/// O toque: tira [marca] se ja estava, soma se nao estava. Devolve um
/// conjunto NOVO (o provider compara por identidade para avisar a tela).
Set<MarcaSelecionada> alternarMarca(
  Set<MarcaSelecionada> selecao,
  MarcaSelecionada marca,
) {
  if (marcaSelecionada(selecao, marca)) {
    return {
      for (final m in selecao)
        if (!_mesmaMarca(m, marca)) m,
    };
  }
  return {...selecao, marca};
}

/// TODAS AS MARCAS DE UM INSTANTE da camada, uma por propriedade que tem
/// marca ali — e o que o losango da linha do tempo (que e um INSTANTE, e
/// nao uma propriedade) seleciona de uma vez.
Set<MarcaSelecionada> marcasDoInstante(Layer camada, Duration local) => {
  for (final prop in LayerProp.values)
    if (temMarcaDaPropEm(camada, prop, local))
      (layerId: camada.id, prop: prop, tempo: local),
};

/// AS TRILHAS QUE UMA PROPRIEDADE GOVERNA, pelo nome do eixo.
///
/// O controlador trata cada `LayerProp` como um GRUPO — o losango de
/// Rotacao marca X, Y e Z juntos, o de Escala marca os dois eixos. Essa
/// lista estava reescrita em cada metodo (`toggleKeyframe`,
/// `setSegmentEase`, `setPropertyLoop`...), e foi assim que o loop de
/// rotacao passou a valer so para um eixo de tres. Copiar, colar, apagar
/// e mover por propriedade leem todos daqui.
({Map<String, AnimatedDouble> numeros, Map<String, AnimatedOffset> pontos})
trilhasDaProp(Layer l, LayerProp prop) => switch (prop) {
  LayerProp.position => (
    numeros: {'positionZ': l.positionZ},
    pontos: {'position': l.position},
  ),
  LayerProp.scale => (
    numeros: {'scaleX': l.scaleX, 'scaleY': l.scaleY},
    pontos: const {},
  ),
  LayerProp.rotation => (
    numeros: {
      'rotation': l.rotation,
      'rotationX': l.rotationX,
      'rotationY': l.rotationY,
    },
    pontos: const {},
  ),
  LayerProp.opacity => (numeros: {'opacity': l.opacity}, pontos: const {}),
  LayerProp.skew => (
    numeros: {'skewX': l.skewX, 'skewY': l.skewY},
    pontos: const {},
  ),
  LayerProp.pivot => (numeros: const {}, pontos: {'pivot': l.pivot}),
  LayerProp.parent => (numeros: const {}, pontos: const {}),
};

/// A camada com as trilhas de [prop] trocadas. As funcoes recebem o nome
/// do eixo (o mesmo de [trilhasDaProp]) e a trilha; devolver a propria
/// trilha e "nao mexi".
Layer comTrilhasDaProp(
  Layer l,
  LayerProp prop, {
  required AnimatedDouble Function(String eixo, AnimatedDouble trilha) numero,
  required AnimatedOffset Function(String eixo, AnimatedOffset trilha) ponto,
}) => switch (prop) {
  LayerProp.position => l.copyLayer(
    position: ponto('position', l.position),
    positionZ: numero('positionZ', l.positionZ),
  ),
  LayerProp.scale => l.copyLayer(
    scaleX: numero('scaleX', l.scaleX),
    scaleY: numero('scaleY', l.scaleY),
  ),
  LayerProp.rotation => l.copyLayer(
    rotation: numero('rotation', l.rotation),
    rotationX: numero('rotationX', l.rotationX),
    rotationY: numero('rotationY', l.rotationY),
  ),
  LayerProp.opacity => l.copyLayer(opacity: numero('opacity', l.opacity)),
  LayerProp.skew => l.copyLayer(
    skewX: numero('skewX', l.skewX),
    skewY: numero('skewY', l.skewY),
  ),
  LayerProp.pivot => l.copyLayer(pivot: ponto('pivot', l.pivot)),
  LayerProp.parent => l,
};

/// Alguma trilha de [prop] tem marca em [local]?
bool temMarcaDaPropEm(Layer l, LayerProp prop, Duration local) {
  final t = trilhasDaProp(l, prop);
  return t.numeros.values.any((a) => a.hasKeyframeAt(local)) ||
      t.pontos.values.any((a) => a.hasKeyframeAt(local));
}

/// O QUE FOI COPIADO: por propriedade e por eixo, as marcas com o tempo
/// contado DESDE A PRIMEIRA marca copiada (de todas as trilhas), o valor e
/// a curva — alcas inclusive. E a promessa de
/// `docs/keyframe-explicito.md`: "copiar preserva valor, relacao de tempo,
/// interpolacao e alcas".
@immutable
class KeyframesCopiados {
  const KeyframesCopiados({required this.numeros, required this.pontos});

  final Map<LayerProp, Map<String, List<Keyframe<double>>>> numeros;
  final Map<LayerProp, Map<String, List<Keyframe<Offset>>>> pontos;

  Iterable<Duration> get _tempos sync* {
    for (final eixos in numeros.values) {
      for (final marcas in eixos.values) {
        for (final k in marcas) {
          yield k.time;
        }
      }
    }
    for (final eixos in pontos.values) {
      for (final marcas in eixos.values) {
        for (final k in marcas) {
          yield k.time;
        }
      }
    }
  }

  /// Quantas marcas ha (cada eixo conta a sua).
  int get quantidade => _tempos.length;

  bool get vazio => _tempos.isEmpty;

  /// Da primeira a ultima marca copiada.
  Duration get duracao =>
      _tempos.fold(Duration.zero, (maior, t) => t > maior ? t : maior);

  /// As propriedades que tem alguma marca copiada.
  Set<LayerProp> get props => {
    for (final e in numeros.entries)
      if (e.value.values.any((m) => m.isNotEmpty)) e.key,
    for (final e in pontos.entries)
      if (e.value.values.any((m) => m.isNotEmpty)) e.key,
  };

  /// COPIA de [camada] as marcas de [marcas] (as de outra camada ficam de
  /// fora). Nulo quando nenhuma delas existe de verdade.
  static KeyframesCopiados? de(Layer camada, Iterable<MarcaSelecionada> marcas) {
    final daCamada = [
      for (final m in marcas)
        if (m.layerId == camada.id) m,
    ];
    bool pedida(LayerProp prop, Duration t) => daCamada.any(
      (m) => m.prop == prop && (m.tempo - t).abs() < kToleranciaDoKeyframe,
    );

    final numeros = <LayerProp, Map<String, List<Keyframe<double>>>>{};
    final pontos = <LayerProp, Map<String, List<Keyframe<Offset>>>>{};
    Duration? primeira;
    void ver(Duration t) {
      if (primeira == null || t < primeira!) primeira = t;
    }

    for (final prop in {for (final m in daCamada) m.prop}) {
      final trilhas = trilhasDaProp(camada, prop);
      for (final e in trilhas.numeros.entries) {
        final achadas = [
          for (final k in e.value.keyframes)
            if (pedida(prop, k.time)) k,
        ];
        if (achadas.isEmpty) continue;
        for (final k in achadas) {
          ver(k.time);
        }
        (numeros[prop] ??= {})[e.key] = achadas;
      }
      for (final e in trilhas.pontos.entries) {
        final achadas = [
          for (final k in e.value.keyframes)
            if (pedida(prop, k.time)) k,
        ];
        if (achadas.isEmpty) continue;
        for (final k in achadas) {
          ver(k.time);
        }
        (pontos[prop] ??= {})[e.key] = achadas;
      }
    }
    final origem = primeira;
    if (origem == null) return null;
    return KeyframesCopiados(
      numeros: {
        for (final p in numeros.entries)
          p.key: {
            for (final e in p.value.entries)
              e.key: [
                for (final k in e.value) k.copyWith(time: k.time - origem),
              ],
          },
      },
      pontos: {
        for (final p in pontos.entries)
          p.key: {
            for (final e in p.value.entries)
              e.key: [
                for (final k in e.value) k.copyWith(time: k.time - origem),
              ],
          },
      },
    );
  }

  /// COLA em [camada] com a primeira marca em [emLocal]. Marca que cairia
  /// fora da camada (antes do zero ou depois do fim) NAO entra: a regra e
  /// a mesma de arrastar (`porQueNaoMoveKeyframe`), e uma marca fora da
  /// camada nao aparece na linha do tempo para ser desfeita a mao.
  /// Onde ja ha marca, a colada substitui — e o que "colar aqui" quer dizer.
  ({Layer camada, int coladas, int foraDaCamada}) colarEm(
    Layer camada,
    Duration emLocal,
  ) {
    var coladas = 0, fora = 0;
    bool cabe(Duration t) {
      final ok = t >= Duration.zero && t <= camada.duration;
      if (ok) {
        coladas++;
      } else {
        fora++;
      }
      return ok;
    }

    var nova = camada;
    for (final prop in props) {
      nova = comTrilhasDaProp(
        nova,
        prop,
        numero: (eixo, trilha) {
          var t = trilha;
          for (final k in numeros[prop]?[eixo] ?? const <Keyframe<double>>[]) {
            final quando = emLocal + k.time;
            if (cabe(quando)) t = t.withKeyframe(quando, k.value, k.ease);
          }
          return t;
        },
        ponto: (eixo, trilha) {
          var t = trilha;
          for (final k in pontos[prop]?[eixo] ?? const <Keyframe<Offset>>[]) {
            final quando = emLocal + k.time;
            if (cabe(quando)) t = t.withKeyframe(quando, k.value, k.ease);
          }
          return t;
        },
      );
    }
    return (camada: nova, coladas: coladas, foraDaCamada: fora);
  }
}

/// AREA DE TRANSFERENCIA DE KEYFRAMES. Uma so, global, em memoria, no
/// molde da `EasingClipboard` (a da curva): copia-se aqui, vai-se a outro
/// instante — ou a outra camada — e cola-se.
class KeyframeClipboard {
  KeyframeClipboard._();

  static KeyframesCopiados? valor;

  static bool get temAlgo => !(valor?.vazio ?? true);

  static void limpar() => valor = null;
}
