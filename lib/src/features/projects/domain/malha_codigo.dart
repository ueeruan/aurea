import 'dart:math' as math;
import 'dart:ui';

import '../../editor/domain/keyframe.dart';
import '../../editor/domain/model_asset3d.dart';
import '../../editor/domain/scene3d.dart';

/// MALHAS MONTADAS EM CODIGO para os modelos do app (Colina, Monolito).
///
/// Vertices compartilhados por material (uma primitiva por material),
/// normais por vertice quando o material e liso, faces planas com a
/// normal da face, e o no com a caixa envolvente calculada — as
/// coordenadas de mundo sobrevivem a normalizacao do ModelAsset3D.

/// Construtor de MODELO em codigo: vertices compartilhados por material
/// (uma primitiva por material), normais por vertice quando o material
/// e liso, e o no com a caixa envolvente calculada — as coordenadas de
/// mundo sobrevivem a normalizacao do ModelAsset3D.
class MalhaCodigo {
  MalhaCodigo(this.materiais);

  final List<Map<String, dynamic>> materiais;
  final Map<int, PrimitivaCodigo> _por = {};

  PrimitivaCodigo _p(int material) => _por[material] ??= PrimitivaCodigo();

  /// Vertice com normal (liso). Devolve o indice.
  int vertice(int material, Vec3 p, Vec3 n, {Offset? uv}) =>
      _p(material).add(p, n, uv);

  void tri(int material, int a, int b, int c) =>
      _p(material).indices.addAll([a, b, c]);

  /// Triangulo PLANO: tres vertices proprios com a normal da face. Com
  /// [virado], a face e invertida se estiver de costas para essa
  /// direcao — o motor descarta o que esta de costas, e um bisel
  /// emitido ao contrario simplesmente some.
  void triPlano(
    int material,
    Vec3 a,
    Vec3 b,
    Vec3 c, {
    Offset? ua,
    Offset? ub,
    Offset? uc,
    Vec3? virado,
  }) {
    var n = (b - a).cross(c - a).normalized;
    if (virado != null && n.dot(virado) < 0) {
      final tb = b;
      b = c;
      c = tb;
      final tu = ub;
      ub = uc;
      uc = tu;
      n = n * -1;
    }
    final p = _p(material);
    final ia = p.add(a, n, ua), ib = p.add(b, n, ub), ic = p.add(c, n, uc);
    p.indices.addAll([ia, ib, ic]);
  }

  void quadPlano(int material, Vec3 a, Vec3 b, Vec3 c, Vec3 d, {Vec3? virado}) {
    triPlano(material, a, b, c, virado: virado);
    triPlano(material, a, c, d, virado: virado);
  }

  int get triangulos =>
      _por.values.fold(0, (s, p) => s + p.indices.length ~/ 3);

  ModelAsset3D asset(String nome) => ModelAsset3D({
        'version': 1,
        'name': nome,
        'nodes': [
          {'name': nome},
        ],
        'primitives': [
          for (final e in _por.entries)
            {
              'node': 0,
              'positions': e.value.positions,
              'normals': e.value.normals,
              if (e.value.uvs.isNotEmpty) 'uv': e.value.uvs,
              'indices': e.value.indices,
              'material': e.key,
            },
        ],
        'materials': materiais,
        'skins': const [],
        'clips': const [],
      });

  /// A caixa envolvente, para o no ficar onde a malha foi desenhada.
  ({Vec3 centro, double meio}) caixa() {
    var lo = const Vec3(1e9, 1e9, 1e9), hi = const Vec3(-1e9, -1e9, -1e9);
    for (final p in _por.values) {
      for (final v in p.positions) {
        lo = Vec3(math.min(lo.x, v[0]), math.min(lo.y, v[1]), math.min(lo.z, v[2]));
        hi = Vec3(math.max(hi.x, v[0]), math.max(hi.y, v[1]), math.max(hi.z, v[2]));
      }
    }
    final d = hi - lo;
    return (
      centro: (lo + hi) * .5,
      meio: math.max(d.x, math.max(d.y, d.z)) / 2,
    );
  }

  SceneNode no(
    String id,
    String nome, {
    List<Vec3> instancias = const [],
    AnimatedDouble? rotX,
    AnimatedDouble? rotY,
    AnimatedDouble? rotZ,
    AnimatedDouble? y,
    Vec3? posicao,
  }) {
    final c = caixa();
    final base = posicao ?? c.centro;
    return SceneNode(
      id: id,
      name: nome,
      size: c.meio,
      x: AnimatedDouble(base.x),
      y: y ?? AnimatedDouble(base.y),
      z: AnimatedDouble(base.z),
      rotX: rotX,
      rotY: rotY,
      rotZ: rotZ,
      instances: instancias,
      modelAsset: asset(nome),
    );
  }
}

class PrimitivaCodigo {
  final positions = <List<double>>[];
  final normals = <List<double>>[];
  final uvs = <List<double>>[];
  final indices = <int>[];

  int add(Vec3 p, Vec3 n, Offset? uv) {
    positions.add([p.x, p.y, p.z]);
    normals.add([n.x, n.y, n.z]);
    if (uv != null) uvs.add([uv.dx, uv.dy]);
    return positions.length - 1;
  }
}

Map<String, dynamic> materialCodigo(
  String nome,
  int cor, {
  double rugosidade = .8,
  double metal = 0,
  double brilho = 0,
  bool semLuz = false,
  bool doisLados = false,
  String? imagem,
}) {
  final c = Color(cor);
  return {
    'name': nome,
    'color': [c.r, c.g, c.b, 1.0],
    'metallic': metal,
    'roughness': rugosidade,
    'emissive': brilho,
    if (semLuz) 'unlit': true,
    if (doisLados) 'doubleSided': true,
    'image': ?imagem,
  };
}

