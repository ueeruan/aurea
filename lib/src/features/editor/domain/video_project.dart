import 'dart:math' as math;
import 'dart:ui';

import 'package:uuid/uuid.dart';
import 'package:vector_math/vector_math_64.dart' as vm;

import 'layer.dart';
import 'rotation_math.dart';
import 'layer_meta.dart';

/// Propriedade animavel de camada (alvo de keyframes, curvas e vinculos).
/// [parent] nao e uma propriedade animavel: e o vinculo de parenting
/// (objeto nulo / camada pai) que arrasta posicao+rotacao+escala juntas.
enum LayerProp { position, scale, rotation, opacity, skew, pivot, parent }

/// Vinculo de propriedade (spec AM2-formas-3d D4, o "pickwhip"): a
/// propriedade alvo passa a seguir a fonte, com escala e offset capturados
/// no instante do vinculo (nao ha interpretador de expressao).
class PropertyLink {
  PropertyLink({
    String? id,
    required this.targetLayerId,
    required this.targetProp,
    required this.sourceLayerId,
    this.scale = 1.0,
    this.offsetX = 0.0,
    this.offsetY = 0.0,
    this.baseRotation = 0.0,
    this.baseScale = 1.0,
    this.baseRotationX = 0.0,
    this.baseRotationY = 0.0,
    this.baseZ = 0.0,
    this.delay = Duration.zero,
  }) : id = id ?? const Uuid().v4();

  final String id;
  final String targetLayerId;
  final LayerProp targetProp;

  /// A fonte fornece a MESMA propriedade (posicao segue posicao, rotacao
  /// segue rotacao...).
  final String sourceLayerId;

  final double scale;

  /// Escalar (rotacao/opacidade/escala usam offsetX) ou vetor (posicao).
  /// Para [LayerProp.parent]: posicao do pai capturada no vinculo.
  final double offsetX;
  final double offsetY;

  /// So para [LayerProp.parent]: rotacao/escala do pai no instante do
  /// vinculo — o filho segue o DELTA (semantica AE: nada pula ao parear).
  final double baseRotation;
  final double baseScale;

  /// Rotacao 3D (X/Y) e profundidade do pai no instante do vinculo: o
  /// filho ORBITA o pai em 3D quando o nulo gira em X/Y.
  final double baseRotationX;
  final double baseRotationY;
  final double baseZ;

  /// Atraso temporal aplicado ao valor da fonte. Zero preserva o
  /// comportamento historico; valores positivos fazem o alvo seguir a
  /// mesma propriedade alguns milissegundos depois, sem efeito opaco.
  final Duration delay;
}

/// MARCADOR na linha do tempo.
///
/// Ouvir a locucao uma vez marcando "aqui entra o titulo", "aqui vira a
/// cena", e depois montar em cima das marcas, e mais rapido e mais
/// preciso que ficar procurando o mesmo instante toda vez que se volta
/// para ele.
class Marker {
  const Marker({
    required this.time,
    this.label = '',
    this.color = const Color(0xFFB8FF3D),
  });

  final Duration time;
  final String label;
  final Color color;

  Marker copyWith({Duration? time, String? label, Color? color}) => Marker(
    time: time ?? this.time,
    label: label ?? this.label,
    color: color ?? this.color,
  );
}

/// Projeto = composicao: pilha de camadas + configuracoes de saida.
/// Ordem da lista: indice 0 e a camada MAIS ACIMA (painel de camadas).
class VideoProject {
  VideoProject({
    String? id,
    required this.name,
    required this.createdAt,
    this.aspectRatio = 16 / 9,
    this.fps = 30,
    this.resolutionHeight = 1080,
    List<Layer>? layers,
    List<PropertyLink>? links,
    Map<String, LayerMeta>? meta,
    this.palette = Palette.aurea,
    List<TextStyleDef>? textStyles,
    List<ExposedProperty>? exposed,
    this.guides = const GuidesSpec(),
    this.motionBlur = const MotionBlurSpec(),
    this.data,
    List<DataBinding>? bindings,
    List<Marker>? markers,
    List<Duration>? beats,
    this.bpm,
    this.lottieMode = false,
    this.backgroundColor = const Color(0xFF000000),
  }) : id = id ?? const Uuid().v4(),
       layers = List.unmodifiable(layers ?? const <Layer>[]),
       links = List.unmodifiable(links ?? const <PropertyLink>[]),
       meta = Map.unmodifiable(meta ?? const <String, LayerMeta>{}),
       textStyles = List.unmodifiable(textStyles ?? const <TextStyleDef>[]),
       exposed = List.unmodifiable(exposed ?? const <ExposedProperty>[]),
       bindings = List.unmodifiable(bindings ?? const <DataBinding>[]),
       markers = List.unmodifiable(
         [...(markers ?? const <Marker>[])]
           ..sort((a, b) => a.time.compareTo(b.time)),
       ),
       beats = List.unmodifiable([...(beats ?? const <Duration>[])]..sort());

  final String id;
  final String name;
  final DateTime createdAt;

  final double aspectRatio;
  final int fps;
  final int resolutionHeight;

  /// COR DE FUNDO da composicao (decisao Q9 do redesign). Preto por
  /// padrao, como sempre foi; o preview e a exportacao pintam esta cor
  /// atras das camadas.
  final Color backgroundColor;

  final List<Layer> layers;

  /// Vinculos de propriedade (pickwhip).
  final List<PropertyLink> links;

  // ---- camada de OFICIO (spec motion-graphics-pro) ----

  /// Rotulo, solo, timida, estilos, responsivo... por camada.
  final Map<String, LayerMeta> meta;

  /// Paleta do projeto (PR-X11).
  final Palette palette;

  /// Estilos de texto nomeados (PR-X12).
  final List<TextStyleDef> textStyles;

  /// Propriedades expostas do template (PR-X16).
  final List<ExposedProperty> exposed;

  /// Guias, grade, areas seguras e mascara de enquadramento (PR-X3).
  final GuidesSpec guides;

  /// Motion blur mestre da composicao (PR-X9).
  final MotionBlurSpec motionBlur;

  /// Fonte de dados e vinculos (PR-X21).
  final DataSource? data;
  final List<DataBinding> bindings;

  /// Marcas na linha do tempo, sempre em ordem de tempo.
  final List<Marker> markers;

  /// AS BATIDAS DA TRILHA, separadas dos marcadores de proposito.
  ///
  /// Marcador e decisao ("aqui vira a cena"); batida e medida ("aqui a
  /// musica bate"). Sao centenas contra algumas, e por isso aparecem
  /// como risquinhos finos, nao como bandeiras — misturar as duas
  /// coisas apagaria as poucas que a pessoa colocou a mao.
  final List<Duration> beats;

  /// O andamento estimado (ou corrigido a mao). Nulo = nunca analisado.
  final double? bpm;

  /// O marcador mais proximo de [t], dentro de [tolerance]. E o que faz
  /// o clipe grudar na marca ao ser arrastado.
  Marker? markerNear(Duration t, Duration tolerance) {
    Marker? melhor;
    var melhorD = tolerance.inMicroseconds;
    for (final m in markers) {
      final d = (m.time - t).inMicroseconds.abs();
      if (d <= melhorD) {
        melhorD = d;
        melhor = m;
      }
    }
    return melhor;
  }

  /// Modo "compativel com Lottie" (PR-X23): recursos nao suportados
  /// aparecem esmaecidos desde o comeco, em vez de surpreender no fim.
  final bool lottieMode;

  LayerMeta metaOf(String id) => meta[id] ?? LayerMeta.empty;

  /// Cor efetiva de uma camada: o vinculo com a paleta vence.
  Color? paletteColorFor(String layerId) {
    final ref = metaOf(layerId).colorRef;
    return ref == null ? null : palette[ref];
  }

  TextStyleDef? textStyleFor(String layerId) {
    final ref = metaOf(layerId).textStyleRef;
    if (ref == null) return null;
    for (final s in textStyles) {
      if (s.name == ref) return s;
    }
    return null;
  }

  /// TODO id de camada do projeto, inclusive os de dentro de grupos.
  late final Set<String> _todosOsIds = () {
    final out = <String>{};
    void varrer(Iterable<Layer> camadas) {
      for (final l in camadas) {
        out.add(l.id);
        if (l is GroupLayer) varrer(l.children);
      }
    }

    varrer(layers);
    return out;
  }();

  /// SOLO (PR-X26): havendo qualquer camada em solo, so as em solo
  /// renderizam.
  ///
  /// So conta o solo de camada que EXISTE. Uma marca de solo ORFA — a
  /// sobra de uma camada apagada — escondia todas as outras: a
  /// composicao ficava preta no preview E no arquivo exportado, sem
  /// botao para desligar, porque o botao morava justamente na camada
  /// que nao existe mais. Uma marca sem dono nao manda em ninguem.
  late final bool hasSolo = _todosOsIds.any((id) => metaOf(id).solo);

  bool rendersInPreview(String layerId) =>
      !metaOf(layerId).hidden && (!hasSolo || metaOf(layerId).solo);

  /// Olho fechado na timeline: fora do preview e da exportacao.
  bool isHidden(String layerId) => metaOf(layerId).hidden;

  factory VideoProject.empty(
    String name, {
    double aspectRatio = 16 / 9,
    int fps = 30,
    int resolutionHeight = 1080,
  }) {
    return VideoProject(
      name: name,
      createdAt: DateTime.now(),
      aspectRatio: aspectRatio,
      fps: fps,
      resolutionHeight: resolutionHeight,
    );
  }

  int get outputWidth => aspectRatio >= 1
      ? (resolutionHeight * aspectRatio).round()
      : resolutionHeight;

  int get outputHeight => aspectRatio >= 1
      ? resolutionHeight
      : (resolutionHeight / aspectRatio).round();

  late final Duration duration = _duration();

  Duration _duration() {
    var end = Duration.zero;
    for (final l in layers) {
      if (l.endTime > end) end = l.endTime;
    }
    return end < const Duration(seconds: 5) ? const Duration(seconds: 5) : end;
  }

  Duration get frameDuration => Duration(microseconds: 1000000 ~/ fps);

  late final Map<String, Layer> _layersById = {
    for (final l in layers.reversed) l.id: l,
  };

  Layer? layerById(String id) => _layersById[id];

  VideoLayer? get firstVideoLayer {
    for (final l in layers) {
      if (l is VideoLayer) return l;
    }
    return null;
  }

  late final Map<(String, LayerProp), PropertyLink> _linksByTarget = {
    for (final link in links.reversed)
      (link.targetLayerId, link.targetProp): link,
  };

  PropertyLink? linkFor(String layerId, LayerProp prop) =>
      _linksByTarget[(layerId, prop)];

  /// Uma copia com IDENTIDADE nova.
  ///
  /// Abrir o mesmo template duas vezes com o mesmo id sobrescreveria o
  /// trabalho da primeira vez — e a pessoa perderia o que fez sem
  /// entender por que.
  VideoProject comIdNovo() => VideoProject(
    name: name,
    createdAt: createdAt,
    aspectRatio: aspectRatio,
    fps: fps,
    resolutionHeight: resolutionHeight,
    layers: layers,
    links: links,
    meta: meta,
    palette: palette,
    textStyles: textStyles,
    exposed: exposed,
    guides: guides,
    motionBlur: motionBlur,
    data: data,
    bindings: bindings,
    markers: markers,
    lottieMode: lottieMode,
  );

  VideoProject copyWith({
    String? name,
    double? aspectRatio,
    int? fps,
    int? resolutionHeight,
    List<Layer>? layers,
    List<PropertyLink>? links,
    Map<String, LayerMeta>? meta,
    Palette? palette,
    List<TextStyleDef>? textStyles,
    List<ExposedProperty>? exposed,
    GuidesSpec? guides,
    MotionBlurSpec? motionBlur,
    DataSource? data,
    List<DataBinding>? bindings,
    List<Marker>? markers,
    List<Duration>? beats,
    double? bpm,
    bool? lottieMode,
    Color? backgroundColor,
  }) {
    return VideoProject(
      id: id,
      name: name ?? this.name,
      createdAt: createdAt,
      aspectRatio: aspectRatio ?? this.aspectRatio,
      fps: fps ?? this.fps,
      resolutionHeight: resolutionHeight ?? this.resolutionHeight,
      layers: layers ?? this.layers,
      links: links ?? this.links,
      meta: meta ?? this.meta,
      palette: palette ?? this.palette,
      textStyles: textStyles ?? this.textStyles,
      exposed: exposed ?? this.exposed,
      guides: guides ?? this.guides,
      motionBlur: motionBlur ?? this.motionBlur,
      data: data ?? this.data,
      bindings: bindings ?? this.bindings,
      markers: markers ?? this.markers,
      beats: beats ?? this.beats,
      bpm: bpm ?? this.bpm,
      lottieMode: lottieMode ?? this.lottieMode,
      backgroundColor: backgroundColor ?? this.backgroundColor,
    );
  }
}

/// Transform efetivo de uma camada apos resolver a CADEIA de parenting.
class LayerTransform {
  const LayerTransform({
    required this.pos,
    required this.rot,
    required this.rotX,
    required this.rotY,
    required this.scale,
    required this.z,
  });

  final Offset pos;
  final double rot;
  final double rotX;
  final double rotY;
  final double scale;
  final double z;
}

/// Resolve o transform EFETIVO da camada no tempo global [t], seguindo a
/// cadeia de parenting recursivamente (objeto -> nulo 1 -> nulo 2 -> ...),
/// com guarda de ciclo. Cada elo aplica o DELTA do pai desde o instante do
/// vinculo, com o offset girado em 3D (X/Y/Z) e escalado.
/// A CAMERA ATIVA no instante [t]: a de cima da pilha cujo intervalo
/// contem o cabecote.
///
/// A regra e a mesma do After Effects e do AM — varias cameras podem
/// existir na linha do tempo, e quem manda e a primeira que esta no ar.
/// Sem camera no ar, `effectiveTransform` nao muda um pixel.
CameraLayer? cameraAtivaEm(VideoProject project, Duration t) {
  for (final l in project.layers) {
    if (l is CameraLayer && t >= l.startTime && t < l.endTime) return l;
  }
  return null;
}

LayerTransform effectiveTransform(
  VideoProject project,
  Layer layer,
  Duration t, [
  Set<String>? visited,
]) {
  // SO A CHAMADA DE FORA APLICA A CAMERA. As chamadas recursivas
  // resolvem o PAI em coordenadas de mundo: parentesco acontece no
  // mundo, e a camera olha o resultado. Aplicar nas duas pontas
  // transformaria a cena duas vezes.
  final raiz = visited == null;
  final local = layer.localTime(t);
  var pos = layer.position.valueAt(local);
  var rot = layer.rotation.valueAt(local);
  var rotX = layer.rotationX.valueAt(local);
  var rotY = layer.rotationY.valueAt(local);
  var scale = layer.scaleX.valueAt(local);
  var z = layer.positionZ.valueAt(local);

  final par = project.linkFor(layer.id, LayerProp.parent);
  if (par != null) {
    visited ??= <String>{};
    if (visited.add(layer.id)) {
      final pp = project.layerById(par.sourceLayerId);
      if (pp != null) {
        // RECURSIVO: o pai tambem pode ter pai (nulo linkado em nulo).
        final pe = effectiveTransform(project, pp, t - par.delay, visited);
        final ratio = par.baseScale.abs() < 1e-6
            ? 1.0
            : pe.scale / par.baseScale;
        final vx = (pos.dx - par.offsetX) * ratio;
        final vy = (pos.dy - par.offsetY) * ratio;
        final vz = (z - par.baseZ) * ratio;

        // Rotacoes em eixos diferentes nao comutam. O delta correto e
        // R(atual) * inversa(R(vinculo)), nao a diferenca dos angulos.
        final delta = rotationMatrix(pe.rotX, pe.rotY, pe.rot)
          ..multiply(
            rotationMatrix(
              par.baseRotationX,
              par.baseRotationY,
              par.baseRotation,
            )..transpose(),
          );
        final v = delta.transform3(vm.Vector3(vx, vy, vz));
        pos = pe.pos + Offset(v.x, v.y);
        z = pe.z + v.z;
        final orientation = delta..multiply(rotationMatrix(rotX, rotY, rot));
        final angles = rotationAngles(orientation);
        rot = nearestRotationTurn(angles.$3, rot + pe.rot - par.baseRotation);
        rotX = nearestRotationTurn(
          angles.$1,
          rotX + pe.rotX - par.baseRotationX,
        );
        rotY = nearestRotationTurn(
          angles.$2,
          rotY + pe.rotY - par.baseRotationY,
        );
        scale *= ratio;
      }
    }
  }
  // A CAMERA DA COMPOSICAO, aplicada ao contrario.
  //
  // So em camada com o 3D ligado: camada 2D nao ve camera, no AM como no
  // After Effects. E nunca na propria camera, que nao se olha.
  final cam = raiz && layer.is3D && layer is! CameraLayer
      ? cameraAtivaEm(project, t)
      : null;
  final mundo = LayerTransform(
    pos: pos,
    rot: rot,
    rotX: rotX,
    rotY: rotY,
    scale: scale,
    z: z,
  );
  return cam == null ? mundo : vistoPelaCamera(project, cam, t, mundo);
}

/// A LENTE DA COMPOSICAO: a mesma focal dos solidos (World3DPainter) e da
/// camera parada (`CameraLayer.lenteNeutra`).
const double focalDaComposicao = 1200;

/// Mais perto que isso a camada ja passou da camera e nao aparece, como no
/// After Effects. Da 12x de tamanho no limite.
const double zPertoDaCamera = -1100;

/// PROFUNDIDADE DE VERDADE para camada plana: recuar em Z encolhe E puxa a
/// camada para o centro da composicao (o ponto de fuga); avancar faz o
/// contrario. Antes so a escala mudava — a camada ficava parada no lugar,
/// o Z parecia um zoom, e uma foto e um solido 3D no mesmo X/Y/Z apareciam
/// em lugares diferentes. Nulo quando a camada passou da camera.
({Offset pos, double escala})? projetarProfundidade(
  VideoProject project,
  Offset pos,
  double z,
) {
  if (!z.isFinite || z <= zPertoDaCamera) return null;
  final k = focalDaComposicao / (focalDaComposicao + z);
  final centro = Offset(project.outputWidth / 2, project.outputHeight / 2);
  return (pos: centro + (pos - centro) * k, escala: k);
}

/// A CAMERA DA COMPOSICAO aplicada a um transform ja resolvido no mundo
/// (pai e vinculos incluidos). O palco usa direto para camada 3D sem pai:
/// antes so o `effectiveTransform` de camada COM pai via a camera, entao
/// mover a camera nao mexia na camada solta — e a moldura de selecao, que
/// via a camera, ficava num lugar e a camada em outro.
LayerTransform vistoPelaCamera(
  VideoProject project,
  CameraLayer cam,
  Duration t,
  LayerTransform mundo,
) {
  var pos = mundo.pos;
  var rot = mundo.rot;
  var rotX = mundo.rotX;
  var rotY = mundo.rotY;
  var scale = mundo.scale;
  var z = mundo.z;
  {
    final cl = cam.localTime(t);
    final cp = cam.position.valueAt(cl);
    final cz = cam.positionZ.valueAt(cl);
    final centro = Offset(project.outputWidth / 2, project.outputHeight / 2);
    var vx = pos.dx - cp.dx;
    var vy = pos.dy - cp.dy;
    var vz = z - cz;

    // O INVERSO DE Rz*Ry*Rx e Rx(-a)*Ry(-b)*Rz(-c): angulos trocados de
    // sinal E ordem invertida. Fazer so o primeiro daria uma cena que
    // gira certo num eixo e errado nos outros dois.
    final crz = -cam.rotation.valueAt(cl) * math.pi / 180;
    final cry = -cam.rotationY.valueAt(cl) * math.pi / 180;
    final crx = -cam.rotationX.valueAt(cl) * math.pi / 180;
    final cosz = math.cos(crz), sinz = math.sin(crz);
    final x0 = vx * cosz - vy * sinz;
    final y0 = vx * sinz + vy * cosz;
    vx = x0;
    vy = y0;
    final cosy = math.cos(cry), siny = math.sin(cry);
    final x1 = vx * cosy + vz * siny;
    final z1 = -vx * siny + vz * cosy;
    vx = x1;
    vz = z1;
    final cosx = math.cos(crx), sinx = math.sin(crx);
    final y2 = vy * cosx - vz * sinx;
    final z2 = vy * sinx + vz * cosx;
    vy = y2;
    vz = z2;

    // A LENTE E UM ZOOM DE PINHOLE: aproximar a lente aumenta o que se
    // ve E afasta do centro na mesma proporcao. 1200 e a lente neutra
    // porque e a focal com que o motor inteiro ja projetava.
    final f = cam.zoom.valueAt(cl).clamp(60.0, 12000.0);
    final k = f / CameraLayer.lenteNeutra;
    pos = centro + Offset(vx, vy) * k;
    z = vz;
    scale *= k;
    // A ORIENTACAO vista pela camera e R(camera)^-1 * R(camada) — a
    // MESMA conta do parenting logo acima. Subtrair angulo por angulo
    // (como era) so bate quando a camera gira num eixo unico; com dois
    // eixos, rotacoes nao comutam e cada camada saia com um erro
    // proprio — era o "rotacoes 3D invertidas com varias camadas".
    // O sinal do angulo continua perto da conta antiga (nearest turn)
    // para keyframe existente nao pular de volta.
    final camX = cam.rotationX.valueAt(cl);
    final camY = cam.rotationY.valueAt(cl);
    final camZ = cam.rotation.valueAt(cl);
    if (camX != 0 || camY != 0 || camZ != 0) {
      final orientacao = rotationMatrix(camX, camY, camZ)..transpose();
      orientacao.multiply(rotationMatrix(rotX, rotY, rot));
      final ang = rotationAngles(orientacao);
      rot = nearestRotationTurn(ang.$3, rot - camZ);
      rotX = nearestRotationTurn(ang.$1, rotX - camX);
      rotY = nearestRotationTurn(ang.$2, rotY - camY);
    }
  }

  return LayerTransform(
    pos: pos,
    rot: rot,
    rotX: rotX,
    rotY: rotY,
    scale: scale,
    z: z,
  );
}

/// Ordena a lista JA em ordem de pintura (fundo primeiro) aplicando a
/// regra 3D (D2): trechos contiguos de camadas 3D sao ordenados por
/// profundidade (Z maior = mais longe = pintado antes); camadas 2D mantem
/// a ordem de empilhamento e funcionam como barreira.
List<Layer> depthSortPaintOrder(
  List<Layer> paintOrder,
  Duration t, {
  VideoProject? project,
}) {
  final out = <Layer>[];
  final run = <Layer>[];

  // Com o projeto, a profundidade e a EFETIVA (pai e camera incluidos):
  // uma camada presa a um nulo que foi para tras tem de ir para tras
  // tambem na pintura, e nao ficar na frente pelo Z proprio.
  double zDe(Layer l) => project != null
      ? effectiveTransform(project, l, t).z
      : l.positionZ.valueAt(l.localTime(t));

  void flush() {
    if (run.isEmpty) return;
    // Desempate ESTAVEL por indice na pilha (triagem 3D §5 item 14):
    // profundidades empatadas nao podem piscar entre frames.
    final decorated = [
      for (var i = 0; i < run.length; i++) (run[i], zDe(run[i]), i),
    ];
    decorated.sort((a, b) {
      final c = b.$2.compareTo(a.$2);
      return c != 0 ? c : a.$3.compareTo(b.$3);
    });
    out.addAll([for (final d in decorated) d.$1]);
    run.clear();
  }

  for (final layer in paintOrder) {
    if (layer.is3D) {
      run.add(layer);
    } else {
      flush();
      out.add(layer);
    }
  }
  flush();
  return out;
}
