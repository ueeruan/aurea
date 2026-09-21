import 'dart:ui' show Offset;

import '../../../application/editor_controller.dart';
import '../../../application/keyframe_clipboard.dart' show trilhasDaProp;
import '../../../domain/camera3d.dart' show Camera3D;
import '../../../domain/effect.dart' show EffectInstance, EffectType;
import '../../../domain/keyframe.dart';
import '../../../domain/layer.dart';
import '../../../domain/scene3d.dart' show SceneNode;
import '../../../domain/texto3d.dart';

// ===========================================================================
// DE QUE TRILHA O EDITOR DE CURVA FALA
// ===========================================================================
//
// O editor de curva nao sabe o que e uma posicao, um parametro de efeito, o
// Time Remap ou a letra de um Texto 3D: ele sabe ler MARCAS, ler a CURVA do
// trecho que sai de uma marca e GRAVAR uma curva nova nele. Cada porta do app
// entrega isso por uma [TrilhaDaCurva] — e e so por isso que existe UM editor
// de curva no app inteiro, e nao um por tela.
//
// Tudo passa pela API do controlador que ja existia (`setSegmentEase`,
// `setEffectSegmentEase`, `setSceneNodePropEase`...): o dominio nao muda.

/// Le as marcas (tempos LOCAIS, em ordem, sem repetir) da trilha em [camada].
typedef LeitorDeMarcas =
    List<Duration> Function(EditorController c, Layer camada);

/// A curva do trecho que SAI da marca em [inicio].
typedef LeitorDeCurva =
    Easing Function(EditorController c, Layer camada, Duration inicio);

/// Grava [curva] no trecho que sai da marca em [inicio].
typedef GravadorDeCurva =
    void Function(
      EditorController c,
      Layer camada,
      Duration inicio,
      Easing curva,
    );

/// O valor da trilha no instante local [t], ja escrito para a tela (nulo =
/// a trilha nao tem um numero so para mostrar).
typedef LeitorDeValor =
    String? Function(EditorController c, Layer camada, Duration t);

/// Que horas sao DENTRO da camada, para esta trilha.
typedef RelogioDaTrilha = Duration Function(Layer camada, Duration global);

/// O relogio de quase tudo: o tempo local da camada (com o Posterize Time
/// que ela tiver).
Duration relogioDaCamada(Layer camada, Duration global) =>
    camada.localTime(global);

/// O relogio CRU: a trilha do Time Remap vive no tempo da barra, sem a
/// quantizacao do Posterize (e o mesmo `rawTime` do editor antigo).
Duration relogioCru(Layer camada, Duration global) =>
    global - camada.startTime;

/// UMA TRILHA QUE O EDITOR DE CURVA SABE EDITAR.
class TrilhaDaCurva {
  const TrilhaDaCurva({
    required this.rotuloDe,
    required this.marcasDe,
    required this.curvaDe,
    required this.gravar,
    this.gravarEmTodos,
    this.valorDe,
    this.relogio = relogioDaCamada,
    this.prop,
  });

  /// O nome da propriedade, em pt-BR (quem desenha traduz).
  final String Function(Layer camada) rotuloDe;
  final LeitorDeMarcas marcasDe;
  final LeitorDeCurva curvaDe;
  final GravadorDeCurva gravar;

  /// "Aplicar em todos os trechos" quando o controlador tem um metodo
  /// proprio. Nulo: o editor grava trecho a trecho, num passo de desfazer.
  final void Function(EditorController c, Layer camada, Easing curva)?
  gravarEmTodos;
  final LeitorDeValor? valorDe;
  final RelogioDaTrilha relogio;

  /// A propriedade de TRANSFORMACAO, quando e uma. So elas entram na
  /// selecao de keyframes (`keyframesSelecionadosProvider` guarda marcas de
  /// `LayerProp`), entao so elas oferecem "aplicar nos selecionados".
  final LayerProp? prop;

  /// O instante local desta trilha no instante [global] do projeto.
  Duration localEm(Layer camada, Duration global) => relogio(camada, global);

  // ------------------------------------------------------------ portas

  /// Posicao, escala, rotacao, opacidade, inclinacao e pivo da camada.
  ///
  /// A propriedade e um GRUPO de trilhas no controlador (escala = X e Y,
  /// rotacao = Z, X e Y): as marcas sao a uniao delas — as mesmas do
  /// losango — e a curva e gravada em todas de uma vez (`setSegmentEase`).
  factory TrilhaDaCurva.transformacao(LayerProp prop) => TrilhaDaCurva(
    prop: prop,
    rotuloDe: (_) => rotuloDaPropriedade(prop),
    marcasDe: (c, l) => _ordenadas(switch (prop) {
      LayerProp.position => l.positionTimesUs,
      LayerProp.scale => l.scaleTimesUs,
      LayerProp.rotation => l.rotationTimesUs,
      LayerProp.opacity => l.opacityTimesUs,
      LayerProp.skew => l.skewTimesUs,
      LayerProp.pivot => l.pivotTimesUs,
      LayerProp.parent => const <int>{},
    }),
    curvaDe: (c, l, t) {
      final trilhas = trilhasDaProp(l, prop);
      // O ponto primeiro: a posicao 2D e a trilha que a pessoa ve; a
      // profundidade so ganha marca junto dela.
      for (final p in trilhas.pontos.values) {
        if (p.hasKeyframeAt(t)) return p.easeAt(t);
      }
      for (final n in trilhas.numeros.values) {
        if (n.hasKeyframeAt(t)) return n.easeAt(t);
      }
      return Easing.linear;
    },
    gravar: (c, l, t, e) => c.setSegmentEase(l.id, prop, t, e),
    gravarEmTodos: (c, l, e) => c.applyEaseToAllSegments(l.id, prop, e),
    valorDe: (c, l, t) {
      final trilhas = trilhasDaProp(l, prop);
      for (final p in trilhas.pontos.values) {
        if (p.isAnimated) return _ponto(p.valueAt(t));
      }
      for (final n in trilhas.numeros.values) {
        if (n.isAnimated) return numeroDaCurva(n.valueAt(t));
      }
      return null;
    },
  );

  /// UM EFEITO DA CAMADA. O keyframe do efeito e universal (uma marca vale
  /// para todos os parametros) e a curva tambem: `setEffectSegmentEase`
  /// grava o trecho em todo parametro que tem marca ali. [parametro] so
  /// escolhe de onde ler marcas e valores.
  factory TrilhaDaCurva.efeito(
    String effectId, {
    String? parametro,
    String? rotulo,
  }) {
    AnimatedDouble? trilhaDe(Layer l) {
      final e = _efeito(l, effectId);
      if (e == null) return null;
      if (parametro != null) return e.params[parametro];
      for (final t in e.params.values) {
        if (t.isAnimated) return t;
      }
      return null;
    }

    return TrilhaDaCurva(
      rotuloDe: (l) => rotulo ?? _efeito(l, effectId)?.spec.name ?? 'Efeito',
      relogio: (l, g) => _efeito(l, effectId)?.type == EffectType.timeRemap
          ? relogioCru(l, g)
          : relogioDaCamada(l, g),
      marcasDe: (c, l) {
        final e = _efeito(l, effectId);
        if (e == null) return const [];
        if (parametro != null) {
          return _deTrilha(e.params[parametro]);
        }
        return _ordenadas({for (final t in e.keyframeTimes) t.inMicroseconds});
      },
      curvaDe: (c, l, t) {
        final e = _efeito(l, effectId);
        if (e == null) return Easing.linear;
        final escolhida = parametro == null ? null : e.params[parametro];
        if (escolhida != null && escolhida.hasKeyframeAt(t)) {
          return escolhida.easeAt(t);
        }
        for (final p in e.params.values) {
          if (p.hasKeyframeAt(t)) return p.easeAt(t);
        }
        return Easing.linear;
      },
      gravar: (c, l, t, e) => c.setEffectSegmentEase(l.id, effectId, t, e),
      gravarEmTodos: (c, l, e) =>
          c.applyEaseToAllEffectSegments(l.id, effectId, e),
      valorDe: (c, l, t) {
        final trilha = trilhaDe(l);
        return trilha == null ? null : numeroDaCurva(trilha.valueAt(t));
      },
    );
  }

  /// A TRILHA DO TIME REMAP (Speed/Time): o grafico de valor e o Time, o
  /// de velocidade e o Speed — a mesma trilha vista pela derivada.
  ///
  /// No clipe de video a trilha mora no efeito interno `timeRemap`
  /// (parametro `tempo`); na precomp (grupo), no campo do grupo — gravado
  /// por `updatePrecomp`.
  factory TrilhaDaCurva.timeRemap() => TrilhaDaCurva(
    rotuloDe: (_) => 'Time Remap',
    relogio: relogioCru,
    marcasDe: (c, l) => _deTrilha(_trilhaDoRemap(l)),
    curvaDe: (c, l, t) => _trilhaDoRemap(l)?.easeAt(t) ?? Easing.linear,
    gravar: (c, l, t, e) {
      if (l is GroupLayer) {
        final trilha = l.timeRemap;
        if (trilha != null) {
          c.updatePrecomp(l.id, timeRemap: trilha.withEase(t, e));
        }
        return;
      }
      final remap = _tempoDoRemap(l);
      if (remap != null) c.setEffectSegmentEase(l.id, remap.id, t, e);
    },
    gravarEmTodos: (c, l, e) {
      if (l is GroupLayer) {
        final trilha = l.timeRemap;
        if (trilha != null) {
          c.updatePrecomp(l.id, timeRemap: trilha.withEaseAll(e));
        }
        return;
      }
      final remap = _tempoDoRemap(l);
      if (remap != null) c.applyEaseToAllEffectSegments(l.id, remap.id, e);
    },
    valorDe: (c, l, t) {
      final trilha = _trilhaDoRemap(l);
      return trilha == null ? null : '${numeroDaCurva(trilha.valueAt(t))} s';
    },
  );

  /// UMA PROPRIEDADE DE UM OBJETO DA CENA 3D.
  factory TrilhaDaCurva.noDaCena(String nodeId, PropDoNo prop) {
    SceneNode? no(Layer l) =>
        l is Scene3DLayer ? l.scene.nodeById(nodeId) : null;
    return TrilhaDaCurva(
      rotuloDe: (_) => propDoNoLabel(prop),
      marcasDe: (c, l) {
        final n = no(l);
        return n == null ? const [] : _deTrilha(c.sceneNodeTrack(n, prop));
      },
      curvaDe: (c, l, t) {
        final n = no(l);
        return n == null ? Easing.linear : c.sceneNodeTrack(n, prop).easeAt(t);
      },
      gravar: (c, l, t, e) =>
          c.setSceneNodePropEase(l.id, nodeId, prop, t, e),
      valorDe: (c, l, t) {
        final n = no(l);
        return n == null
            ? null
            : numeroDaCurva(c.sceneNodeTrack(n, prop).valueAt(t));
      },
    );
  }

  /// UMA PROPRIEDADE DE UMA CAMERA DA CENA 3D (a principal ou uma extra).
  factory TrilhaDaCurva.cameraDaCena(String cameraId, PropDaCamera prop) {
    Camera3D? camera(Layer l) {
      if (l is! Scene3DLayer) return null;
      if (l.camera.id == cameraId) return l.camera;
      for (final c in l.extraCameras) {
        if (c.id == cameraId) return c;
      }
      return null;
    }

    return TrilhaDaCurva(
      rotuloDe: (_) => propDaCameraLabel(prop),
      marcasDe: (c, l) {
        final cam = camera(l);
        return cam == null
            ? const []
            : _deTrilha(c.sceneCameraTrack(cam, prop));
      },
      curvaDe: (c, l, t) {
        final cam = camera(l);
        return cam == null
            ? Easing.linear
            : c.sceneCameraTrack(cam, prop).easeAt(t);
      },
      gravar: (c, l, t, e) =>
          c.setSceneCameraPropEase(l.id, cameraId, prop, t, e),
      valorDe: (c, l, t) {
        final cam = camera(l);
        return cam == null
            ? null
            : numeroDaCurva(c.sceneCameraTrack(cam, prop).valueAt(t));
      },
    );
  }

  /// UMA MEDIDA DE UM AJUSTE POR CARACTERE DO TEXTO 3D, identificado pela
  /// faixa ([inicio]..[fim]) — a mesma chave do painel que o criou.
  ///
  /// ATENCAO: o arquivo do projeto ainda nao guarda a curva destas trilhas
  /// (`AjusteDeCaracteres._trilhaParaJson` grava so tempo e valor). A curva
  /// vale na sessao e no desfazer; ao reabrir o projeto, volta a linear.
  factory TrilhaDaCurva.caractereDoTexto3D({
    required String nodeId,
    required int inicio,
    required int fim,
    required MedidaDoCaractere medida,
  }) {
    AjusteDeCaracteres? ajuste(Layer l) {
      if (l is! Scene3DLayer) return null;
      return l.scene.nodeById(nodeId)?.texto3d?.ajusteDaFaixa(inicio, fim);
    }

    return TrilhaDaCurva(
      rotuloDe: (_) => rotuloDaMedida(medida),
      marcasDe: (c, l) => _deTrilha(ajuste(l)?.trilhas[medida]),
      curvaDe: (c, l, t) =>
          ajuste(l)?.trilhas[medida]?.easeAt(t) ?? Easing.linear,
      gravar: (c, l, t, e) {
        if (l is! Scene3DLayer) return;
        final ajustes = l.scene.nodeById(nodeId)?.texto3d?.ajustes;
        if (ajustes == null) return;
        c.ajustarCaracteresDoTexto3D(l.id, nodeId, [
          // NO LUGAR: a ordem dos ajustes e a ordem em que se sobrepoem.
          for (final a in ajustes)
            a.inicio == inicio && a.fim == fim
                ? a.com(medida, a.trilha(medida).withEase(t, e))
                : a,
        ]);
      },
      valorDe: (c, l, t) {
        final trilha = ajuste(l)?.trilhas[medida];
        return trilha == null ? null : numeroDaCurva(trilha.valueAt(t));
      },
    );
  }

  /// QUALQUER OUTRA TRILHA NUMERICA (parametro de forma, modulo grade...):
  /// quem chama diz como acha-la e como gravar — o mesmo contrato do
  /// `showTrackCurveSheet` do editor antigo.
  factory TrilhaDaCurva.deUmaTrilha({
    required String rotulo,
    required AnimatedDouble? Function(Layer camada) trilhaDe,
    required void Function(
      EditorController c,
      String layerId,
      Duration inicio,
      Easing curva,
    )
    gravar,
    void Function(EditorController c, String layerId, Easing curva)?
    gravarEmTodos,
    RelogioDaTrilha relogio = relogioDaCamada,
  }) => TrilhaDaCurva(
    rotuloDe: (_) => rotulo,
    relogio: relogio,
    marcasDe: (c, l) => _deTrilha(trilhaDe(l)),
    curvaDe: (c, l, t) => trilhaDe(l)?.easeAt(t) ?? Easing.linear,
    gravar: (c, l, t, e) => gravar(c, l.id, t, e),
    gravarEmTodos: gravarEmTodos == null
        ? null
        : (c, l, e) => gravarEmTodos(c, l.id, e),
    valorDe: (c, l, t) {
      final trilha = trilhaDe(l);
      return trilha == null ? null : numeroDaCurva(trilha.valueAt(t));
    },
  );
}

/// O nome de uma propriedade de transformacao (pt-BR).
String rotuloDaPropriedade(LayerProp prop) => switch (prop) {
  LayerProp.position => 'Posição',
  LayerProp.scale => 'Escala',
  LayerProp.rotation => 'Rotação',
  LayerProp.opacity => 'Opacidade',
  LayerProp.skew => 'Inclinação',
  LayerProp.pivot => 'Pivô',
  LayerProp.parent => 'Vínculo',
};

/// O TRECHO de [marcas] em que o instante local [agora] cai.
///
/// N marcas = N-1 trechos; o trecho `i` vai de `marcas[i]` ate
/// `marcas[i + 1]`. Em cima de uma marca, o trecho e o que SAI dela — e o
/// que o toque longo no losango quer dizer —, e em cima da ULTIMA, o que
/// chega nela (a ultima nao tem trecho de saida). Fora das marcas, nulo:
/// a regra da referencia e "a curva so aparece com o cabecote entre dois
/// keyframes".
///
/// A folga e a do "mesmo instante" das trilhas ([kToleranciaDoKeyframe]):
/// o `seek` para numa grade de quadros e pode cair um pouco antes da marca.
({int indice, Duration inicio, Duration fim})? trechoEm(
  List<Duration> marcas,
  Duration agora,
) {
  if (marcas.length < 2) return null;
  const folga = kToleranciaDoKeyframe;
  final ultima = marcas.length - 1;
  if ((agora - marcas[ultima]).abs() < folga) {
    return (indice: ultima - 1, inicio: marcas[ultima - 1], fim: marcas[ultima]);
  }
  for (var i = 0; i < ultima; i++) {
    if (agora >= marcas[i] - folga && agora < marcas[i + 1] - folga) {
      return (indice: i, inicio: marcas[i], fim: marcas[i + 1]);
    }
  }
  return null;
}

/// Um numero para a leitura do grafico: ate duas casas, virgula decimal,
/// sem zeros sobrando ("1,5", e nao "1,50"). Nunca "NaN" na tela.
String numeroDaCurva(double v) {
  if (!v.isFinite) return '—';
  var s = v.toStringAsFixed(2);
  if (s.contains('.')) {
    s = s.replaceFirst(RegExp(r'0+$'), '').replaceFirst(RegExp(r'\.$'), '');
  }
  if (s == '-0') s = '0';
  return s.replaceAll('.', ',');
}

String _ponto(Offset p) => '${numeroDaCurva(p.dx)}; ${numeroDaCurva(p.dy)}';

List<Duration> _ordenadas(Set<int> us) {
  final lista = us.toList()..sort();
  return [for (final u in lista) Duration(microseconds: u)];
}

List<Duration> _deTrilha(AnimatedDouble? t) => t == null
    ? const []
    : _ordenadas({for (final k in t.keyframes) k.time.inMicroseconds});

EffectInstance? _efeito(Layer l, String effectId) {
  for (final e in l.effects) {
    if (e.id == effectId) return e;
  }
  return null;
}

AnimatedDouble? _trilhaDoRemap(Layer l) =>
    l is GroupLayer ? l.timeRemap : _tempoDoRemap(l)?.params['tempo'];

EffectInstance? _tempoDoRemap(Layer l) {
  for (final e in l.effects) {
    if (e.type == EffectType.timeRemap) return e;
  }
  return null;
}
