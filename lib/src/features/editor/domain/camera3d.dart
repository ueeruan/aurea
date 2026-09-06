import 'dart:math' as math;
import 'dart:ui';

import 'package:uuid/uuid.dart';

import 'keyframe.dart';
import 'scene3d.dart';

/// CAMERA 3D (spec AUREA-camera-e-vistas): parametros fieis ao After
/// Effects, navegacao repensada para o toque.

/// Dois nos tem PONTO DE INTERESSE e sempre olha para ele; um no e
/// livre, dirigido por rotacao.
enum CameraKind { twoNode, oneNode }

/// Predefinicoes de lente (mm).
const lensPresets = <double>[15, 20, 24, 28, 35, 50, 80, 135, 200];

/// FORMATO DA IRIS — o que da a forma do bokeh.
enum IrisShape {
  fastRectangle,
  triangle,
  square,
  pentagon,
  hexagon,
  heptagon,
  octagon,
  nonagon,
  decagon,
}

int irisSides(IrisShape s) => switch (s) {
  IrisShape.fastRectangle => 4,
  IrisShape.triangle => 3,
  IrisShape.square => 4,
  IrisShape.pentagon => 5,
  IrisShape.hexagon => 6,
  IrisShape.heptagon => 7,
  IrisShape.octagon => 8,
  IrisShape.nonagon => 9,
  IrisShape.decagon => 10,
};

String irisLabel(IrisShape s) => switch (s) {
  IrisShape.fastRectangle => 'Retangulo rapido',
  IrisShape.triangle => 'Triangulo',
  IrisShape.square => 'Quadrado',
  IrisShape.pentagon => 'Pentagono',
  IrisShape.hexagon => 'Hexagono',
  IrisShape.heptagon => 'Heptagono',
  IrisShape.octagon => 'Octogono',
  IrisShape.nonagon => 'Eneagono',
  IrisShape.decagon => 'Decagono',
};

/// PROFUNDIDADE DE CAMPO. Os tres ultimos parametros sao o que separa
/// "desfoque" de "lente": sem GANHO e LIMIAR de realce, luz fora de
/// foco vira borrao cinza; com eles, vira a bola de bokeh que a gente
/// reconhece como fotografia.
class DepthOfField {
  DepthOfField({
    this.enabled = false,
    AnimatedDouble? focusDistance,
    AnimatedDouble? aperture,
    AnimatedDouble? blurLevel,
    this.lockToZoom = false,
    this.irisShape = IrisShape.hexagon,
    AnimatedDouble? irisRotation,
    AnimatedDouble? irisRoundness,
    AnimatedDouble? irisAspect,
    AnimatedDouble? diffractionFringe,
    AnimatedDouble? highlightGain,
    AnimatedDouble? highlightThreshold,
    AnimatedDouble? highlightSaturation,
  }) : focusDistance = focusDistance ?? AnimatedDouble(800),
       aperture = aperture ?? AnimatedDouble(25),
       blurLevel = blurLevel ?? AnimatedDouble(100),
       irisRotation = irisRotation ?? AnimatedDouble(0),
       irisRoundness = irisRoundness ?? AnimatedDouble(0),
       irisAspect = irisAspect ?? AnimatedDouble(1),
       diffractionFringe = diffractionFringe ?? AnimatedDouble(0),
       highlightGain = highlightGain ?? AnimatedDouble(0),
       highlightThreshold = highlightThreshold ?? AnimatedDouble(0.75),
       highlightSaturation = highlightSaturation ?? AnimatedDouble(1);

  final bool enabled;
  final AnimatedDouble focusDistance;
  final AnimatedDouble aperture;

  /// Multiplicador artistico, 0..200%.
  final AnimatedDouble blurLevel;
  final bool lockToZoom;

  final IrisShape irisShape;
  final AnimatedDouble irisRotation;

  /// -100 a 100: laminas retas ou curvas.
  final AnimatedDouble irisRoundness;

  /// Bokeh oval — o visual anamorfico.
  final AnimatedDouble irisAspect;
  final AnimatedDouble diffractionFringe;

  /// 0..100 — e ISTO que faz o ponto de luz virar bola de bokeh.
  final AnimatedDouble highlightGain;
  final AnimatedDouble highlightThreshold;
  final AnimatedDouble highlightSaturation;

  /// DIAFRAGMA (f-stop) ligado a abertura pela distancia focal.
  double fStopFor(double focalLengthMm, Duration t) {
    final a = aperture.valueAt(t);
    return a <= 0 ? 22 : focalLengthMm / a;
  }

  /// Circulo de confusao (px) de um ponto a [distance] da camera.
  /// Zero na distancia de foco, cresce para os dois lados.
  double circleOfConfusion(double distance, Duration t) {
    final focus = focusDistance.valueAt(t);
    if (focus <= 0 || distance <= 0) return 0;
    final a = aperture.valueAt(t);
    final level = blurLevel.valueAt(t) / 100;
    return (a * (distance - focus).abs() / distance) * level;
  }

  DepthOfField copyWith({
    bool? enabled,
    AnimatedDouble? focusDistance,
    AnimatedDouble? aperture,
    AnimatedDouble? blurLevel,
    bool? lockToZoom,
    IrisShape? irisShape,
    AnimatedDouble? irisRotation,
    AnimatedDouble? irisRoundness,
    AnimatedDouble? irisAspect,
    AnimatedDouble? diffractionFringe,
    AnimatedDouble? highlightGain,
    AnimatedDouble? highlightThreshold,
    AnimatedDouble? highlightSaturation,
  }) => DepthOfField(
    enabled: enabled ?? this.enabled,
    focusDistance: focusDistance ?? this.focusDistance,
    aperture: aperture ?? this.aperture,
    blurLevel: blurLevel ?? this.blurLevel,
    lockToZoom: lockToZoom ?? this.lockToZoom,
    irisShape: irisShape ?? this.irisShape,
    irisRotation: irisRotation ?? this.irisRotation,
    irisRoundness: irisRoundness ?? this.irisRoundness,
    irisAspect: irisAspect ?? this.irisAspect,
    diffractionFringe: diffractionFringe ?? this.diffractionFringe,
    highlightGain: highlightGain ?? this.highlightGain,
    highlightThreshold: highlightThreshold ?? this.highlightThreshold,
    highlightSaturation: highlightSaturation ?? this.highlightSaturation,
  );
}

/// Caminho do bokeh: o formato da iris com arredondamento e proporcao.
/// Arredondamento 100 = circulo; 0 = poligono reto; negativo = laminas
/// concavas.
Path irisPath(
  IrisShape shape,
  double radius, {
  double roundness = 0,
  double rotationDeg = 0,
  double aspect = 1,
}) {
  final n = irisSides(shape);
  final r = roundness.clamp(-100.0, 100.0) / 100;
  final rot = rotationDeg * math.pi / 180 - math.pi / 2;
  final path = Path();

  if (r >= 0.999) {
    return path..addOval(
      Rect.fromCenter(
        center: Offset.zero,
        width: radius * 2 * aspect,
        height: radius * 2,
      ),
    );
  }

  final pts = <Offset>[
    for (var i = 0; i < n; i++)
      Offset(
        math.cos(rot + i * 2 * math.pi / n) * radius * aspect,
        math.sin(rot + i * 2 * math.pi / n) * radius,
      ),
  ];

  path.moveTo(pts[0].dx, pts[0].dy);
  for (var i = 0; i < n; i++) {
    final cur = pts[i];
    final next = pts[(i + 1) % n];
    if (r.abs() < 0.001) {
      path.lineTo(next.dx, next.dy);
    } else {
      // Puxa o meio da lamina para fora (curva) ou para dentro
      // (concava), que e o efeito de arredondamento do AE.
      final mid = Offset((cur.dx + next.dx) / 2, (cur.dy + next.dy) / 2);
      final outward = mid.distance < 1e-6
          ? Offset.zero
          : Offset(mid.dx / mid.distance, mid.dy / mid.distance);
      final bulge = radius * r * 0.55;
      final ctrl = mid + outward * bulge;
      path.quadraticBezierTo(ctrl.dx, ctrl.dy, next.dx, next.dy);
    }
  }
  return path..close();
}

/// GANHO DE REALCE: quanto um ponto de brilho [luminance] vira bokeh.
/// Abaixo do limiar, nada acontece — e o que evita borrar a cena
/// inteira e deixa so as luzes virarem bolas.
double highlightBoost(
  double luminance, {
  required double gain,
  required double threshold,
}) {
  if (gain <= 0) return 1;
  if (luminance <= threshold) return 1;
  final over = (luminance - threshold) / math.max(1e-6, 1 - threshold);
  return 1 + over * gain / 10;
}

/// Uma BOLA DE BOKEH pronta para desenhar: onde, de que tamanho, de que
/// cor. Sai como dado (e nao direto no canvas) para poder ser testada —
/// o teste da spec e "iris hexagonal com ganho alto produz hexagonos
/// brilhantes VISIVEIS, nao um borrao".
typedef BokehSprite = ({
  Offset center,
  double radius,
  Color color,
  double luminance,
});

/// Extrai as bolas de bokeh de um quadro ja renderizado, usando a
/// PROFUNDIDADE de cada triangulo — e por isso que profundidade de campo
/// de verdade depende do buffer de profundidade da Cena 3D.
List<BokehSprite> bokehSprites(
  SceneFrame frame,
  DepthOfField dof,
  Duration t, {
  double minRadius = 2.5,
  int maxSprites = 400,
}) {
  if (!dof.enabled) return const [];
  final gain = dof.highlightGain.valueAt(t);
  if (gain <= 0) return const [];
  final threshold = dof.highlightThreshold.valueAt(t).clamp(0.0, 0.999);
  final sat = dof.highlightSaturation.valueAt(t).clamp(0.0, 2.0);

  final out = <BokehSprite>[];
  for (final tri in [...frame.opaque, ...frame.transparent]) {
    // FACE COM IMAGEM nao vira bokeh: a cor dela e o branco que a
    // textura multiplica depois, e a bola sairia branca — e o pintor de
    // bokeh nao sabe de oclusao, entao um chao claro ESCONDIDO atras de
    // um morro virava uma fila de hexagonos atravessando o quadro.
    if (tri.texture != null) continue;
    final coc = dof.circleOfConfusion(tri.depth, t);
    if (coc < minRadius) continue;

    final r = tri.color.r, g = tri.color.g, b = tri.color.b;
    final lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    // Abaixo do limiar nada vira bokeh: e o que evita borrar a cena
    // inteira e deixa SO as luzes virarem bolas.
    if (lum <= threshold) continue;

    // UM PONTO DE LUZ vira bola; uma SUPERFICIE grande fora de foco so
    // ficaria borrada. Se o triangulo ja e maior que a bola que ele
    // geraria, ele nao e ponto de luz — e chao, parede, tela — e nao
    // entra. Sem isto, a borda distante de um terreno claro (a cor da
    // face com imagem e branca; a imagem entra depois) virava uma fila
    // de quatrocentos hexagonos atravessando o quadro.
    final area =
        ((tri.b.dx - tri.a.dx) * (tri.c.dy - tri.a.dy) -
                (tri.c.dx - tri.a.dx) * (tri.b.dy - tri.a.dy))
            .abs() /
        2;
    if (area > math.pi * coc * coc) continue;

    final boost = highlightBoost(lum, gain: gain, threshold: threshold);
    // Saturacao de realce: quanto de cor a bola preserva (0 = cinza).
    double chan(double c) => ((lum + (c - lum) * sat) * boost).clamp(0.0, 1.0);

    out.add((
      center: Offset(
        (tri.a.dx + tri.b.dx + tri.c.dx) / 3,
        (tri.a.dy + tri.b.dy + tri.c.dy) / 3,
      ),
      radius: coc,
      color: Color.from(
        alpha: tri.color.a,
        red: chan(r),
        green: chan(g),
        blue: chan(b),
      ),
      luminance: lum,
    ));
    if (out.length >= maxSprites) break;
  }
  return out;
}

/// A CAMERA.
class Camera3D {
  Camera3D({
    String? id,
    this.name = 'Camera',
    this.kind = CameraKind.twoNode,
    AnimatedDouble? posX,
    AnimatedDouble? posY,
    AnimatedDouble? posZ,
    AnimatedDouble? poiX,
    AnimatedDouble? poiY,
    AnimatedDouble? poiZ,
    AnimatedDouble? orientX,
    AnimatedDouble? orientY,
    AnimatedDouble? orientZ,
    AnimatedDouble? rotX,
    AnimatedDouble? rotY,
    AnimatedDouble? rotZ,
    AnimatedDouble? focalLength,
    this.filmWidth = 36,
    this.orthographic = false,
    DepthOfField? dof,
    this.autoOrient = AutoOrient.off,
  }) : id = id ?? const Uuid().v4(),
       posX = posX ?? AnimatedDouble(0),
       posY = posY ?? AnimatedDouble(0),
       posZ = posZ ?? AnimatedDouble(800),
       poiX = poiX ?? AnimatedDouble(0),
       poiY = poiY ?? AnimatedDouble(0),
       poiZ = poiZ ?? AnimatedDouble(0),
       orientX = orientX ?? AnimatedDouble(0),
       orientY = orientY ?? AnimatedDouble(0),
       orientZ = orientZ ?? AnimatedDouble(0),
       rotX = rotX ?? AnimatedDouble(0),
       rotY = rotY ?? AnimatedDouble(0),
       rotZ = rotZ ?? AnimatedDouble(0),
       focalLength = focalLength ?? AnimatedDouble(50),
       dof = dof ?? DepthOfField();

  final String id;
  final String name;
  final CameraKind kind;

  final AnimatedDouble posX;
  final AnimatedDouble posY;
  final AnimatedDouble posZ;

  /// Ponto de interesse (so em dois nos).
  final AnimatedDouble poiX;
  final AnimatedDouble poiY;
  final AnimatedDouble poiZ;

  /// Orientacao: caminho CURTO. Rotacao separada: aditiva, aceita
  /// varias voltas. Coexistem porque sao necessidades opostas.
  final AnimatedDouble orientX;
  final AnimatedDouble orientY;
  final AnimatedDouble orientZ;
  final AnimatedDouble rotX;
  final AnimatedDouble rotY;
  final AnimatedDouble rotZ;

  final AnimatedDouble focalLength;
  final double filmWidth;
  final bool orthographic;
  final DepthOfField dof;
  final AutoOrient autoOrient;

  Vec3 positionAt(Duration t) =>
      Vec3(posX.valueAt(t), posY.valueAt(t), posZ.valueAt(t));

  Vec3 pointOfInterestAt(Duration t) =>
      Vec3(poiX.valueAt(t), poiY.valueAt(t), poiZ.valueAt(t));

  /// Angulo de visao e distancia focal sao a MESMA grandeza.
  double fovAt(Duration t) =>
      2 *
      math.atan(filmWidth / (2 * math.max(1e-6, focalLength.valueAt(t)))) *
      180 /
      math.pi;

  double zoomAt(Duration t, double compWidth) =>
      zoomFromFocal(focalLength.valueAt(t), compWidth, filmWidth: filmWidth);

  /// Direcao do olhar: em dois nos, aponta para o alvo; em um no, vem
  /// da orientacao + rotacoes.
  Vec3 forwardAt(Duration t) {
    if (kind == CameraKind.twoNode) {
      final d = pointOfInterestAt(t) - positionAt(t);
      return d.length < 1e-6 ? const Vec3(0, 0, -1) : d.normalized;
    }
    final rx = (orientX.valueAt(t) + rotX.valueAt(t)) * math.pi / 180;
    final ry = (orientY.valueAt(t) + rotY.valueAt(t)) * math.pi / 180;
    // Olhar padrao -Z, girado por X e Y.
    final cx = math.cos(rx), sx = math.sin(rx);
    final cy = math.cos(ry), sy = math.sin(ry);
    return Vec3(-sy * cx, sx, -cy * cx).normalized;
  }

  /// Camera de render para o instante [t].
  RenderCamera renderAt(Duration t) {
    final pos = positionAt(t);
    final target = kind == CameraKind.twoNode
        ? pointOfInterestAt(t)
        : pos + forwardAt(t) * 1000;
    final base = RenderCamera(
      position: pos,
      target: target,
      focalLength: focalLength.valueAt(t),
      filmWidth: filmWidth,
      orthographic: orthographic,
    );
    // Roll must rotate the camera's own up vector, including when looking
    // almost vertically down a shaft. World-Z rotation is not camera roll.
    final roll = (orientZ.valueAt(t) + rotZ.valueAt(t)) * math.pi / 180;
    if (roll.abs() < 1e-12) return base;
    final basis = cameraBasis(base);
    return RenderCamera(
      position: pos,
      target: target,
      up: basis.up * math.cos(roll) + basis.right * math.sin(roll),
      focalLength: base.focalLength,
      filmWidth: filmWidth,
      orthographic: orthographic,
    );
  }

  /// CONVERSAO ENTRE TIPOS preservando o enquadramento (§1.1): trocar
  /// de tipo NUNCA pode fazer a cena pular.
  Camera3D convertedTo(CameraKind target, Duration t) {
    if (target == kind) return this;
    final pos = positionAt(t);
    final fwd = forwardAt(t);

    if (target == CameraKind.twoNode) {
      // Um no -> dois nos: o alvo vira um ponto na direcao do olhar, na
      // distancia que ja estava em foco.
      final dist = dof.focusDistance.valueAt(t);
      final poi = pos + fwd * (dist <= 0 ? 800 : dist);
      return copyWith(
        kind: CameraKind.twoNode,
        poiX: AnimatedDouble(poi.x),
        poiY: AnimatedDouble(poi.y),
        poiZ: AnimatedDouble(poi.z),
      );
    }

    // Dois nos -> um no: a direcao do alvo vira orientacao, e as
    // rotacoes separadas zeram para nao somar duas vezes.
    final angles = anglesFromForward(fwd);
    return copyWith(
      kind: CameraKind.oneNode,
      orientX: AnimatedDouble(angles.pitchDeg),
      orientY: AnimatedDouble(angles.yawDeg),
      orientZ: AnimatedDouble(0),
      rotX: AnimatedDouble(0),
      rotY: AnimatedDouble(0),
      rotZ: AnimatedDouble(0),
    );
  }

  Camera3D copyWith({
    String? name,
    CameraKind? kind,
    AnimatedDouble? posX,
    AnimatedDouble? posY,
    AnimatedDouble? posZ,
    AnimatedDouble? poiX,
    AnimatedDouble? poiY,
    AnimatedDouble? poiZ,
    AnimatedDouble? orientX,
    AnimatedDouble? orientY,
    AnimatedDouble? orientZ,
    AnimatedDouble? rotX,
    AnimatedDouble? rotY,
    AnimatedDouble? rotZ,
    AnimatedDouble? focalLength,
    double? filmWidth,
    bool? orthographic,
    DepthOfField? dof,
    AutoOrient? autoOrient,
  }) => Camera3D(
    id: id,
    name: name ?? this.name,
    kind: kind ?? this.kind,
    posX: posX ?? this.posX,
    posY: posY ?? this.posY,
    posZ: posZ ?? this.posZ,
    poiX: poiX ?? this.poiX,
    poiY: poiY ?? this.poiY,
    poiZ: poiZ ?? this.poiZ,
    orientX: orientX ?? this.orientX,
    orientY: orientY ?? this.orientY,
    orientZ: orientZ ?? this.orientZ,
    rotX: rotX ?? this.rotX,
    rotY: rotY ?? this.rotY,
    rotZ: rotZ ?? this.rotZ,
    focalLength: focalLength ?? this.focalLength,
    filmWidth: filmWidth ?? this.filmWidth,
    orthographic: orthographic ?? this.orthographic,
    dof: dof ?? this.dof,
    autoOrient: autoOrient ?? this.autoOrient,
  );
}

enum AutoOrient { off, alongPath, towardsPoi }

/// Angulos (pitch/yaw) que produzem a direcao [f] — a volta exata de
/// [Camera3D.forwardAt] no modo de um no.
({double pitchDeg, double yawDeg}) anglesFromForward(Vec3 f) {
  final n = f.normalized;
  final pitch = math.asin(n.y.clamp(-1.0, 1.0));
  final yaw = math.atan2(-n.x, -n.z);
  return (pitchDeg: pitch * 180 / math.pi, yawDeg: yaw * 180 / math.pi);
}

// ------------------------------------------------------------ vistas

enum SceneView {
  camera,
  front,
  back,
  left,
  right,
  top,
  bottom,
  custom1,
  custom2,
}

String sceneViewLabel(SceneView v) => switch (v) {
  SceneView.camera => 'Camera',
  SceneView.front => 'Frente',
  SceneView.back => 'Tras',
  SceneView.left => 'Esquerda',
  SceneView.right => 'Direita',
  SceneView.top => 'Topo',
  SceneView.bottom => 'Base',
  SceneView.custom1 => 'Livre 1',
  SceneView.custom2 => 'Livre 2',
};

/// Camera ortografica das vistas fixas. Elas sao o que torna posicao em
/// Z compreensivel — sem elas, "esta atras ou e so menor?" nao tem
/// resposta.
RenderCamera orthoViewCamera(
  SceneView view, {
  double distance = 1500,
  double scale = 0.5,
  Vec3 center = Vec3.zero,
}) {
  final (pos, up) = switch (view) {
    SceneView.front => (Vec3(0, 0, distance), Vec3(0, 1, 0)),
    SceneView.back => (Vec3(0, 0, -distance), Vec3(0, 1, 0)),
    SceneView.right => (Vec3(distance, 0, 0), Vec3(0, 1, 0)),
    SceneView.left => (Vec3(-distance, 0, 0), Vec3(0, 1, 0)),
    SceneView.top => (Vec3(0, distance, 0), Vec3(0, 0, -1)),
    SceneView.bottom => (Vec3(0, -distance, 0), Vec3(0, 0, 1)),
    _ => (Vec3(distance * 0.7, distance * 0.5, distance * 0.7), Vec3(0, 1, 0)),
  };
  return RenderCamera(
    position: pos + center,
    target: center,
    up: up,
    orthographic: true,
    orthoScale: scale,
  );
}

// -------------------------------------------------------- navegacao

/// O CONFLITO a resolver (§2.1): um dedo arrastando pode significar
/// mover a camada ou girar a camera. Escolher errado torna o 3D
/// insuportavel.
enum TouchIntent { moveLayer, orbitCamera, selectLayer }

TouchIntent resolveTouch({
  required bool onSelectedLayer,
  required bool onOtherLayer,
  required bool navigationMode,
}) {
  if (navigationMode) return TouchIntent.orbitCamera;
  if (onSelectedLayer) return TouchIntent.moveLayer;
  if (onOtherLayer) return TouchIntent.selectLayer;
  return TouchIntent.orbitCamera;
}

/// ORBITA: gira a camera em volta de um PIVO fixado no inicio do gesto
/// (trocar o pivo no meio e o que mais atrapalha).
Camera3D orbitCamera(
  Camera3D cam,
  Vec3 pivot,
  double deltaYawDeg,
  double deltaPitchDeg,
  Duration t,
) {
  final pos = cam.positionAt(t);
  final rel = pos - pivot;
  final radius = rel.length;
  if (radius < 1e-6) return cam;

  var yaw = math.atan2(rel.x, rel.z);
  var pitch = math.asin((rel.y / radius).clamp(-1.0, 1.0));
  yaw += deltaYawDeg * math.pi / 180;
  pitch = (pitch + deltaPitchDeg * math.pi / 180).clamp(
    -math.pi / 2 + 0.01,
    math.pi / 2 - 0.01,
  );

  final np = Vec3(
    pivot.x + radius * math.cos(pitch) * math.sin(yaw),
    pivot.y + radius * math.sin(pitch),
    pivot.z + radius * math.cos(pitch) * math.cos(yaw),
  );

  final moved = cam.copyWith(
    posX: cam.posX.withBase(np.x),
    posY: cam.posY.withBase(np.y),
    posZ: cam.posZ.withBase(np.z),
  );
  if (cam.kind == CameraKind.twoNode) {
    // Dois nos ja olha para o alvo; se o pivo e outro ponto, mantem o
    // alvo onde estava.
    return moved;
  }
  final angles = anglesFromForward(pivot - np);
  return moved.copyWith(
    orientX: AnimatedDouble(angles.pitchDeg),
    orientY: AnimatedDouble(angles.yawDeg),
  );
}

/// PINCA move a camera no Z — e NAO mexe na distancia focal. Aproximar
/// a camera muda a PERSPECTIVA; mudar o zoom muda a LENTE. Sao coisas
/// diferentes e quem faz 3D precisa das duas separadas.
Camera3D dollyCamera(Camera3D cam, double factor, Duration t) {
  final pos = cam.positionAt(t);
  final target = cam.kind == CameraKind.twoNode
      ? cam.pointOfInterestAt(t)
      : pos + cam.forwardAt(t) * 1000;
  final rel = pos - target;
  final scaled = rel * (1 / factor.clamp(0.05, 20.0));
  final np = target + scaled;
  return cam.copyWith(
    posX: cam.posX.withBase(np.x),
    posY: cam.posY.withBase(np.y),
    posZ: cam.posZ.withBase(np.z),
  );
}

/// Deslocar (pan) com dois dedos: move camera E alvo juntos.
Camera3D panCamera(Camera3D cam, Offset delta, Duration t) {
  final rc = cam.renderAt(t);
  final basis = cameraBasis(rc);
  final shift = basis.right * (-delta.dx) + basis.up * delta.dy;
  final pos = cam.positionAt(t) + shift;
  var out = cam.copyWith(
    posX: cam.posX.withBase(pos.x),
    posY: cam.posY.withBase(pos.y),
    posZ: cam.posZ.withBase(pos.z),
  );
  if (cam.kind == CameraKind.twoNode) {
    final poi = cam.pointOfInterestAt(t) + shift;
    out = out.copyWith(
      poiX: cam.poiX.withBase(poi.x),
      poiY: cam.poiY.withBase(poi.y),
      poiZ: cam.poiZ.withBase(poi.z),
    );
  }
  return out;
}

// --------------------------------------------------------- comandos

/// ENQUADRAR: move a camera para caber [bounds] com margem.
Camera3D frameBounds(
  Camera3D cam,
  Bounds3D bounds,
  Duration t, {
  double margin = 1.35,
}) {
  if (bounds.radius <= 0) return cam;
  final fov = cam.fovAt(t) * math.pi / 180;
  final dist = bounds.radius * margin / math.tan(fov / 2);
  final dir = cam.kind == CameraKind.twoNode
      ? (cam.positionAt(t) - bounds.center).normalized
      : (cam.forwardAt(t) * -1);
  final np =
      bounds.center + (dir.length < 1e-6 ? const Vec3(0, 0, 1) : dir) * dist;
  var out = cam.copyWith(
    posX: cam.posX.withBase(np.x),
    posY: cam.posY.withBase(np.y),
    posZ: cam.posZ.withBase(np.z),
  );
  if (cam.kind == CameraKind.twoNode) {
    out = out.copyWith(
      poiX: cam.poiX.withBase(bounds.center.x),
      poiY: cam.poiY.withBase(bounds.center.y),
      poiZ: cam.poiZ.withBase(bounds.center.z),
    );
  }
  return out.copyWith(
    dof: out.dof.copyWith(focusDistance: AnimatedDouble(dist)),
  );
}

/// ALINHAR CAMERA A VISTA — o comando mais usado de todos: navega-se
/// livre ate achar o enquadramento, e so entao a camera assume ele.
Camera3D alignToView(Camera3D cam, RenderCamera view) {
  var out = cam.copyWith(
    posX: cam.posX.withBase(view.position.x),
    posY: cam.posY.withBase(view.position.y),
    posZ: cam.posZ.withBase(view.position.z),
  );
  if (cam.kind == CameraKind.twoNode) {
    return out.copyWith(
      poiX: cam.poiX.withBase(view.target.x),
      poiY: cam.poiY.withBase(view.target.y),
      poiZ: cam.poiZ.withBase(view.target.z),
    );
  }
  final angles = anglesFromForward(view.target - view.position);
  return out.copyWith(
    orientX: AnimatedDouble(angles.pitchDeg),
    orientY: AnimatedDouble(angles.yawDeg),
    rotX: AnimatedDouble(0),
    rotY: AnimatedDouble(0),
    rotZ: AnimatedDouble(0),
  );
}

/// Volume que envolve a cena inteira (para "enquadrar tudo").
Bounds3D sceneBounds(Scene3D scene, Duration t) {
  if (scene.nodes.isEmpty) return Bounds3D.empty;
  var minX = double.infinity, minY = double.infinity, minZ = double.infinity;
  var maxX = -double.infinity, maxY = -double.infinity, maxZ = -double.infinity;
  for (final n in scene.nodes) {
    if (!n.visible || n.isNull) continue;
    final transform = resolveNodeTransform(scene, n, t);
    final r = n.size * transform.scale.abs() * 1.8;
    final offsets = n.instances.isEmpty ? const [Vec3.zero] : n.instances;
    for (final offset in offsets) {
      final p = composeTransforms(
        transform,
        NodeTransform(position: offset),
      ).position;
      minX = math.min(minX, p.x - r);
      minY = math.min(minY, p.y - r);
      minZ = math.min(minZ, p.z - r);
      maxX = math.max(maxX, p.x + r);
      maxY = math.max(maxY, p.y + r);
      maxZ = math.max(maxZ, p.z + r);
    }
  }
  if (!minX.isFinite) return Bounds3D.empty;
  final center = Vec3((minX + maxX) / 2, (minY + maxY) / 2, (minZ + maxZ) / 2);
  final radius = Vec3(maxX - center.x, maxY - center.y, maxZ - center.z).length;
  return Bounds3D(center, radius);
}

// ------------------------------------------------------------- rigs

/// RIGS EM UM TOQUE (§5): cada um gera NULLS E KEYFRAMES REAIS,
/// editaveis depois — nenhum e caixa-preta.
enum CameraRig { orbit, tripod, dolly, handheld, dollyZoom }

String cameraRigLabel(CameraRig r) => switch (r) {
  CameraRig.orbit => 'Orbita',
  CameraRig.tripod => 'Tripe',
  CameraRig.dolly => 'Dolly',
  CameraRig.handheld => 'Camera na mao',
  CameraRig.dollyZoom => 'Dolly zoom',
};

/// Aplica um rig, devolvendo a camera com keyframes REAIS.
Camera3D applyCameraRig(
  Camera3D cam,
  CameraRig rig, {
  required Duration duration,
  Vec3 target = Vec3.zero,
  double radius = 800,
  double intensity = 1,
}) {
  final end = duration.inMicroseconds <= 0
      ? const Duration(seconds: 4)
      : duration;

  switch (rig) {
    case CameraRig.orbit:
      // Uma volta completa: keyframes de posicao em 8 passos.
      var px = AnimatedDouble(cam.posX.base);
      var pz = AnimatedDouble(cam.posZ.base);
      const steps = 8;
      for (var i = 0; i <= steps; i++) {
        final f = i / steps;
        final a = f * 2 * math.pi;
        final t = end * f;
        px = px.withKeyframe(t, target.x + math.sin(a) * radius);
        pz = pz.withKeyframe(t, target.z + math.cos(a) * radius);
      }
      return cam.copyWith(
        kind: CameraKind.twoNode,
        posX: px,
        posZ: pz,
        poiX: AnimatedDouble(target.x),
        poiY: AnimatedDouble(target.y),
        poiZ: AnimatedDouble(target.z),
      );

    case CameraRig.tripod:
      // Camera fixa, so rotacao — panoramica sem paralaxe.
      return cam.copyWith(
        kind: CameraKind.oneNode,
        rotY: AnimatedDouble(0)
            .withKeyframe(Duration.zero, -12 * intensity)
            .withKeyframe(end, 12 * intensity),
      );

    case CameraRig.dolly:
      final start = cam.positionAt(Duration.zero);
      return cam.copyWith(
        posZ: AnimatedDouble(start.z)
            .withKeyframe(Duration.zero, start.z)
            .withKeyframe(end, start.z - radius * 0.6 * intensity),
      );

    case CameraRig.handheld:
      // Tremor suave e deterministico em X/Y.
      var px = AnimatedDouble(cam.posX.base);
      var py = AnimatedDouble(cam.posY.base);
      const steps = 16;
      for (var i = 0; i <= steps; i++) {
        final t = end * (i / steps);
        final a = i * 1.7;
        px = px.withKeyframe(t, cam.posX.base + math.sin(a) * 6 * intensity);
        py = py.withKeyframe(
          t,
          cam.posY.base + math.cos(a * 1.3) * 4 * intensity,
        );
      }
      return cam.copyWith(posX: px, posY: py);

    case CameraRig.dollyZoom:
      // O efeito vertigo: posicao e distancia focal em OPOSICAO, de
      // modo que o alvo mantem o tamanho e o fundo "respira".
      final start = cam.positionAt(Duration.zero);
      final d0 = (start - target).length;
      final f0 = cam.focalLength.base;
      final d1 = d0 * 0.5;
      final f1 = f0 * d1 / d0;
      final dir = (start - target).normalized;
      final endPos = target + dir * d1;
      return cam.copyWith(
        posX: AnimatedDouble(start.x)
            .withKeyframe(Duration.zero, start.x)
            .withKeyframe(end, endPos.x),
        posY: AnimatedDouble(start.y)
            .withKeyframe(Duration.zero, start.y)
            .withKeyframe(end, endPos.y),
        posZ: AnimatedDouble(start.z)
            .withKeyframe(Duration.zero, start.z)
            .withKeyframe(end, endPos.z),
        focalLength: AnimatedDouble(f0)
            .withKeyframe(Duration.zero, f0)
            .withKeyframe(end, f1),
      );
  }
}
