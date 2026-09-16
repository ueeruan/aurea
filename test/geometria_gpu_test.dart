import 'dart:math' as math;
import 'dart:typed_data';

import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/estresse3d.dart';
import 'package:aurea/src/features/editor/domain/geometria_gpu.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:flutter_test/flutter_test.dart';

/// OS BUFFERS TIPADOS SAEM IGUAIS AOS DA VERSAO ANTIGA.
///
/// A versao antiga (copiada aqui como referencia) montava cada grupo em
/// listas de double boxed — 400 bytes por triangulo de heap transitorio,
/// o que abria um modelo grande com um pico de memoria que o iPhone 13
/// nao perdoa. A nova escreve direto em Float32List/Uint32List. O que se
/// prova: o MESMO algoritmo (normal de Newell, ordem corrigida, UV do
/// modelo ou planar) produz os MESMOS arrays — e cabe no tempo.

// --------------------------------------------------- a referencia antiga

class _GrupoRef {
  final positions = <double>[];
  final normals = <double>[];
  final uvs = <double>[];
  final indices = <int>[];
  final mapa = <int, int>{};

  int adicionar(List<double> p, List<double> n, List<double> uv) {
    positions.addAll([p[0], p[1], p[2]]);
    normals.addAll([n[0], n[1], n[2]]);
    uvs.addAll([uv[0], uv[1]]);
    return positions.length ~/ 3 - 1;
  }
}

class _CaixaRef {
  _CaixaRef(this.lo, this.hi);
  final List<double> lo;
  final List<double> hi;
  static _CaixaRef de(Element3DMesh m) {
    final lo = [double.infinity, double.infinity, double.infinity];
    final hi = [-double.infinity, -double.infinity, -double.infinity];
    for (final v in m.verts) {
      for (var i = 0; i < 3; i++) {
        if (v[i] < lo[i]) lo[i] = v[i];
        if (v[i] > hi[i]) hi[i] = v[i];
      }
    }
    return _CaixaRef(lo, hi);
  }

  double faixa(double v, int eixo) {
    final d = hi[eixo] - lo[eixo];
    return d <= 1e-9 ? 0.5 : (v - lo[eixo]) / d;
  }
}

List<double> _uvRef(
  List<Offset?>? uvs,
  int v,
  List<double> p,
  double nx,
  double ny,
  double nz,
  _CaixaRef caixa,
) {
  if (uvs != null && v < uvs.length && uvs[v] != null) {
    return [uvs[v]!.dx, uvs[v]!.dy];
  }
  final ax = nx.abs(), ay = ny.abs(), az = nz.abs();
  if (ax >= ay && ax >= az) return [caixa.faixa(p[2], 2), caixa.faixa(p[1], 1)];
  if (ay >= ax && ay >= az) return [caixa.faixa(p[0], 0), caixa.faixa(p[2], 2)];
  return [caixa.faixa(p[0], 0), caixa.faixa(p[1], 1)];
}

Map<Material3D, _GrupoRef> _referencia({
  required Element3DMesh malha,
  required List<Material3D> materiais,
  List<Vec3?>? normais,
  List<Offset?>? uvs,
}) {
  final grupos = <Material3D, _GrupoRef>{};
  final lisa =
      normais != null &&
      normais.length == malha.verts.length &&
      normais.every((n) => n != null);
  final caixa = _CaixaRef.de(malha);
  for (var f = 0; f < malha.faces.length; f++) {
    final face = malha.faces[f];
    if (face.length < 3) continue;
    final grupo = grupos[materiais[f]] ??= _GrupoRef();
    var nx = 0.0, ny = 0.0, nz = 0.0;
    for (var i = 0; i < face.length; i++) {
      final a = malha.verts[face[i]], b = malha.verts[face[(i + 1) % face.length]];
      nx += (a[1] - b[1]) * (a[2] + b[2]);
      ny += (a[2] - b[2]) * (a[0] + b[0]);
      nz += (a[0] - b[0]) * (a[1] + b[1]);
    }
    final len = math.sqrt(nx * nx + ny * ny + nz * nz);
    if (len < 1e-12) continue;
    nx /= len;
    ny /= len;
    nz /= len;
    for (var i = 1; i < face.length - 1; i++) {
      var ia = face[0], ib = face[i], ic = face[i + 1];
      final a = malha.verts[ia], b = malha.verts[ib], c = malha.verts[ic];
      final ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
      final vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
      final wx = uy * vz - uz * vy, wy = uz * vx - ux * vz, wz = ux * vy - uy * vx;
      if (wx * nx + wy * ny + wz * nz < 0) {
        final tmp = ib;
        ib = ic;
        ic = tmp;
      }
      for (final v in [ia, ib, ic]) {
        final p = malha.verts[v];
        if (lisa) {
          final idx = grupo.mapa[v] ??= grupo.adicionar(
            p,
            [normais[v]!.x, normais[v]!.y, normais[v]!.z],
            _uvRef(uvs, v, p, nx, ny, nz, caixa),
          );
          grupo.indices.add(idx);
        } else {
          grupo.indices.add(
            grupo.adicionar(p, [nx, ny, nz], _uvRef(uvs, v, p, nx, ny, nz, caixa)),
          );
        }
      }
    }
  }
  return grupos;
}

// ------------------------------------------------------------- os testes

void main() {
  void iguais(Map<Material3D, _GrupoRef> ref, Map<Material3D, GrupoGpu> novo) {
    expect(novo.keys.toSet(), ref.keys.toSet());
    for (final m in ref.keys) {
      final r = ref[m]!, g = novo[m]!;
      expect(g.positions, Float32List.fromList(r.positions), reason: 'posicoes');
      expect(g.normals, Float32List.fromList(r.normals), reason: 'normais');
      expect(g.texCoords, Float32List.fromList(r.uvs), reason: 'uvs');
      expect(g.indexList, Uint32List.fromList(r.indices), reason: 'indices');
      expect(g.vertices, r.positions.length ~/ 3);
      expect(g.triangulos, r.indices.length ~/ 3);
    }
  }

  test('cubo plano, um material: os mesmos arrays', () {
    final cubo = element3DMesh(Element3DKind.cube);
    const m = Material3D();
    final materiais = List<Material3D>.filled(cubo.faces.length, m);
    iguais(
      _referencia(malha: cubo, materiais: materiais),
      montarGruposGpu(malha: cubo, materiais: materiais),
    );
  });

  test('esfera lisa (normais e uvs do modelo), dois materiais: iguais', () {
    final esfera = esferaUV(24, 16);
    final normais = <Vec3?>[
      for (final v in esfera.verts) Vec3(v[0], v[1], v[2]),
    ];
    final uvs = <Offset?>[
      for (var i = 0; i < esfera.verts.length; i++)
        i % 5 == 0 ? null : Offset(i / esfera.verts.length, (i % 7) / 7),
    ];
    const a = Material3D(name: 'A');
    const b = Material3D(name: 'B', metallic: 1);
    final materiais = [
      for (var f = 0; f < esfera.faces.length; f++) f.isEven ? a : b,
    ];
    iguais(
      _referencia(malha: esfera, materiais: materiais, normais: normais, uvs: uvs),
      montarGruposGpu(malha: esfera, materiais: materiais, normais: normais, uvs: uvs),
    );
  });

  test('faces degeneradas e poligonos de quatro lados: iguais', () {
    final m = Element3DMesh(
      [
        [0, 0, 0],
        [1, 0, 0],
        [1, 1, 0],
        [0, 1, 0],
        [0, 0, 0],
      ],
      [
        [0, 1, 2, 3], // quadrado, em leque
        [0, 4, 1], // degenerada (dois vertices iguais): normal nula, some
        [3, 2, 1, 0], // o mesmo quadrado, ao contrario: ordem corrigida
      ],
    );
    final materiais = List<Material3D>.filled(m.faces.length, const Material3D());
    iguais(
      _referencia(malha: m, materiais: materiais),
      montarGruposGpu(malha: m, materiais: materiais),
    );
  });

  test('150 materiais numa malha lisa: cada grupo reserva so o que usa', () {
    // O modelo que derrubava o iPhone: muitos materiais. Cada grupo nascia
    // com espaco para min(65 536, faces do modelo) triangulos e um mapa do
    // tamanho de todos os vertices. Aqui o que se reserva tem de ser o que
    // o grupo usa, e os arrays continuam iguais aos da referencia.
    final esfera = esferaUV(160, 100);
    final normais = <Vec3?>[
      for (final v in esfera.verts) Vec3(v[0], v[1], v[2]),
    ];
    final paleta = [
      for (var i = 0; i < 150; i++) Material3D(name: 'm$i', roughness: i / 150),
    ];
    final materiais = [
      for (var f = 0; f < esfera.faces.length; f++) paleta[(f * 7) % 150],
    ];
    final grupos = montarGruposGpu(
      malha: esfera,
      materiais: materiais,
      normais: normais,
    );
    expect(grupos.length, 150);
    var reservado = 0;
    for (final g in grupos.values) {
      final (vertices, indices) = g.capacidade;
      expect(indices, g.indices, reason: 'indices exatos');
      expect(vertices, greaterThanOrEqualTo(g.vertices));
      expect(vertices, lessThanOrEqualTo(g.indices));
      reservado += vertices * 32 + indices * 4;
    }
    final usado = grupos.values.fold(0, (s, g) => s + g.vertices * 32 + g.indices * 4);
    // Folga so dos vertices lisos compartilhados (limite superior = um por
    // canto). A conta antiga reservava min(65536, faces) por grupo.
    final antigo = 150 * math.min(65536, esfera.faces.length) * 44;
    expect(reservado, lessThan(usado * 4));
    expect(reservado, lessThan(antigo ~/ 10));
    iguais(
      _referencia(malha: esfera, materiais: materiais, normais: normais),
      grupos,
    );
  });

  test('duzentos mil triangulos cabem no tempo', () {
    final grande = esferaUV(400, 250); // ~200 mil
    final materiais = List<Material3D>.filled(grande.faces.length, const Material3D());
    final relogio = Stopwatch()..start();
    final grupos = montarGruposGpu(malha: grande, materiais: materiais);
    relogio.stop();
    final tri = grupos.values.fold(0, (s, g) => s + g.triangulos);
    expect(tri, triangulosDe(grande));
    // ignore: avoid_print
    print('200 mil triangulos em buffers tipados: ${relogio.elapsedMilliseconds} ms');
    expect(relogio.elapsedMilliseconds, lessThan(4000));
  });
}
