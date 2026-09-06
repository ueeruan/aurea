import 'dart:math' as math;

import 'camera3d.dart';
import 'keyframe.dart';
import 'scene3d.dart';

/// VARIAS CAMERAS, COM CORTE.
///
/// Uma camera so obriga a animar a mesma camera de um enquadramento ao
/// outro — e ai todo corte vira um voo. Cinema nao voa entre planos:
/// corta. Com uma lista de tomadas, cada instante tem UMA camera ativa,
/// e a troca e instantanea (corte) ou suave (transicao), como se pede.
///
/// A tomada guarda o INSTANTE em que entra, nao a duracao: mover uma
/// tomada nao empurra as outras, e a lista continua legivel quando se
/// olha a linha do tempo.
class CameraShot {
  const CameraShot({
    required this.time,
    required this.cameraId,
    this.transition = Duration.zero,
  });

  /// Quando esta camera entra.
  final Duration time;

  final String cameraId;

  /// Zero = corte seco. Maior que zero = a camera anterior derrete
  /// nesta ao longo desse tempo.
  final Duration transition;

  bool get isCut => transition <= Duration.zero;

  CameraShot copyWith({
    Duration? time,
    String? cameraId,
    Duration? transition,
  }) =>
      CameraShot(
        time: time ?? this.time,
        cameraId: cameraId ?? this.cameraId,
        transition: transition ?? this.transition,
      );
}

double _lerp(double a, double b, double t) => a + (b - a) * t;

Vec3 _lerpVec(Vec3 a, Vec3 b, double t) => Vec3(
      _lerp(a.x, b.x, t),
      _lerp(a.y, b.y, t),
      _lerp(a.z, b.z, t),
    );

/// Mistura duas cameras de render.
///
/// A distancia focal e interpolada em LOG: de 20 mm para 200 mm o meio
/// perceptual e ~63 mm, nao 110. Interpolar linear faz a transicao
/// parecer que fica parada e depois dispara no fim.
RenderCamera lerpCamera(RenderCamera a, RenderCamera b, double t) {
  final f = t.clamp(0.0, 1.0);
  final fa = math.max(1e-3, a.focalLength);
  final fb = math.max(1e-3, b.focalLength);
  return RenderCamera(
    position: _lerpVec(a.position, b.position, f),
    target: _lerpVec(a.target, b.target, f),
    up: _lerpVec(a.up, b.up, f),
    focalLength: math.exp(_lerp(math.log(fa), math.log(fb), f)),
    filmWidth: _lerp(a.filmWidth, b.filmWidth, f),
    orthographic: f < 0.5 ? a.orthographic : b.orthographic,
    orthoScale: _lerp(a.orthoScale, b.orthoScale, f),
    near: _lerp(a.near, b.near, f),
    far: _lerp(a.far, b.far, f),
  );
}

/// Tomadas em ordem de tempo.
List<CameraShot> sortedShots(List<CameraShot> shots) =>
    [...shots]..sort((a, b) => a.time.compareTo(b.time));

/// Qual tomada esta no ar em [t] (a ultima que ja comecou), ou null se
/// nenhuma comecou ainda.
CameraShot? shotAt(List<CameraShot> shots, Duration t) {
  CameraShot? atual;
  for (final s in sortedShots(shots)) {
    if (s.time <= t) atual = s;
  }
  return atual;
}

Camera3D? _byId(List<Camera3D> cameras, String id) {
  for (final c in cameras) {
    if (c.id == id) return c;
  }
  return null;
}

/// A camera de render em [t], resolvendo tomadas e transicoes.
///
/// Sem tomadas, ou com tomada que aponta para camera apagada, vale
/// [fallback]: a lista de tomadas nunca pode deixar a cena sem camera.
RenderCamera resolveCamera(
  List<Camera3D> cameras,
  List<CameraShot> shots,
  Duration t,
  Camera3D fallback,
) {
  final ordenadas = sortedShots(shots);
  if (ordenadas.isEmpty) return fallback.renderAt(t);

  var indice = -1;
  for (var i = 0; i < ordenadas.length; i++) {
    if (ordenadas[i].time <= t) indice = i;
  }
  if (indice < 0) {
    // Antes da primeira tomada, quem vale e a primeira: cena nao comeca
    // apontando para lugar nenhum.
    final c = _byId(cameras, ordenadas.first.cameraId) ?? fallback;
    return c.renderAt(t);
  }

  final atual = ordenadas[indice];
  final cam = _byId(cameras, atual.cameraId) ?? fallback;
  final destino = cam.renderAt(t);

  if (atual.isCut || indice == 0) return destino;

  final decorrido = t - atual.time;
  if (decorrido >= atual.transition) return destino;

  final anterior = ordenadas[indice - 1];
  final camAnterior = _byId(cameras, anterior.cameraId) ?? fallback;
  final f = atual.transition.inMicroseconds <= 0
      ? 1.0
      : decorrido.inMicroseconds / atual.transition.inMicroseconds;
  // Suaviza as pontas: uma transicao linear COMECA e PARA de repente, e
  // o olho enxerga os dois solavancos.
  final suave = Easing.easeInOut.transform(f.clamp(0.0, 1.0));
  return lerpCamera(camAnterior.renderAt(t), destino, suave);
}
