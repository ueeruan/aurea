import 'dart:math';

import 'package:flutter/foundation.dart';

import 'dart:typed_data';

import 'package:aurea_meshopt/aurea_meshopt.dart';
import 'package:flutter_test/flutter_test.dart';

/// Grade plana side x side no plano XY, dois triangulos por celula.
({Float32List posicoes, Uint32List indices}) _grade(int side) {
  final posicoes = Float32List(side * side * 3);
  for (var y = 0; y < side; y++) {
    for (var x = 0; x < side; x++) {
      final i = (y * side + x) * 3;
      posicoes[i] = x / (side - 1);
      posicoes[i + 1] = y / (side - 1);
      // Um relevo suave: simplificar uma grade plana e trivial demais.
      posicoes[i + 2] = .05 * sin(x / 4) * cos(y / 5);
    }
  }
  final indices = <int>[];
  for (var y = 0; y < side - 1; y++) {
    for (var x = 0; x < side - 1; x++) {
      final a = y * side + x;
      indices.addAll([a, a + 1, a + side, a + 1, a + side + 1, a + side]);
    }
  }
  return (posicoes: posicoes, indices: Uint32List.fromList(indices));
}

double _area(Float32List p, Uint32List idx) {
  var total = 0.0;
  for (var i = 0; i < idx.length; i += 3) {
    final a = idx[i] * 3, b = idx[i + 1] * 3, c = idx[i + 2] * 3;
    final ux = p[b] - p[a], uy = p[b + 1] - p[a + 1], uz = p[b + 2] - p[a + 2];
    final vx = p[c] - p[a], vy = p[c + 1] - p[a + 1], vz = p[c + 2] - p[a + 2];
    final cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
    total += sqrt(cx * cx + cy * cy + cz * cz) / 2;
  }
  return total;
}

void main() {
  test(
    'native optimizer preserves every triangle and reduces vertex cache misses',
    () {
      const side = 80;
      final triangles = <List<int>>[];
      for (var y = 0; y < side - 1; y++) {
        for (var x = 0; x < side - 1; x++) {
          final a = y * side + x;
          triangles.add([a, a + 1, a + side]);
          triangles.add([a + 1, a + side + 1, a + side]);
        }
      }
      triangles.shuffle(Random(42));
      final input = Uint32List.fromList(triangles.expand((t) => t).toList());
      final output = optimizeVertexCache(input, side * side);
      List<String> faces(Uint32List values) => [
        for (var i = 0; i < values.length; i += 3)
          '${values[i]},${values[i + 1]},${values[i + 2]}',
      ]..sort();
      int misses(Uint32List values) {
        final cache = <int>[];
        var misses = 0;
        for (final v in values) {
          if (!cache.remove(v)) misses++;
          cache.insert(0, v);
          if (cache.length > 16) cache.removeLast();
        }
        return misses;
      }

      expect(faces(output), faces(input));
      expect(misses(output), lessThan(misses(input) * .6));
      // Useful measurement, not a frame-rate claim.
      debugPrint('Vertex misses: ${misses(input)} -> ${misses(output)}');
      expect(optimizeVertexCache(Uint32List.fromList([0, 1, 90]), 3), [
        0,
        1,
        90,
      ]);
    },
  );

  test('solda: um vertice por canto (FBX) volta a um por ponto', () {
    const side = 24;
    final g = _grade(side);
    // Desfaz o compartilhamento: cada canto de triangulo ganha vertice
    // proprio, como o importador de FBX entrega.
    final soltos = Float32List(g.indices.length * 3);
    final sequencia = Uint32List(g.indices.length);
    for (var i = 0; i < g.indices.length; i++) {
      final v = g.indices[i] * 3;
      soltos
        ..[i * 3] = g.posicoes[v]
        ..[i * 3 + 1] = g.posicoes[v + 1]
        ..[i * 3 + 2] = g.posicoes[v + 2];
      sequencia[i] = i;
    }
    final r = weldVertices(sequencia, g.indices.length, [soltos], [3])!;
    expect(r.vertexCount, side * side);
    // Os triangulos continuam os mesmos pontos, na mesma ordem.
    for (var i = 0; i < g.indices.length; i++) {
      expect(r.remap[i], r.indices[i]);
    }
    // Vertices com a mesma posicao mas UV diferente NAO se juntam.
    final uv = Float32List(g.indices.length * 2);
    for (var i = 0; i < g.indices.length; i++) {
      uv[i * 2] = (i % 2).toDouble();
    }
    final comUv = weldVertices(sequencia, g.indices.length, [soltos, uv], [3, 2])!;
    expect(comUv.vertexCount, greaterThan(side * side));
    // Entrada invalida nao explode: devolve null.
    expect(weldVertices(Uint32List.fromList([0, 1, 9]), 3, [soltos], [3]), isNull);
  });

  test('busca de vertices: renumera na ordem de uso e larga os sobrando', () {
    final idx = Uint32List.fromList([7, 3, 5, 5, 3, 9]);
    final r = optimizeVertexFetch(idx, 12)!;
    expect(r.vertexCount, 4);
    expect(r.indices, [0, 1, 2, 2, 1, 3]);
    expect(r.remap[7], 0);
    expect(r.remap[0], 0xFFFFFFFF, reason: 'vertice sem triangulo sai');
  });

  test('simplificador: chega perto do alvo sem mover vertice nem abrir buraco', () {
    const side = 60;
    final g = _grade(side);
    final alvo = g.indices.length ~/ 4;
    final r = simplifyMesh(
      g.indices,
      g.posicoes,
      targetIndexCount: alvo,
      targetError: .05,
    )!;
    expect(r.indices.length, lessThanOrEqualTo((alvo * 1.15).round()));
    expect(r.indices.length, greaterThan(0));
    expect(r.indices.every((i) => i < side * side), isTrue);
    // Area quase igual: simplificar nao fura a malha (o "uma face a cada
    // N" do rascunho antigo perdia 3/4 da area).
    final antes = _area(g.posicoes, g.indices);
    final depois = _area(g.posicoes, r.indices);
    expect(depois, closeTo(antes, antes * .05));
    debugPrint(
      'Simplify: ${g.indices.length ~/ 3} -> ${r.indices.length ~/ 3} '
      'triangles, error ${r.error.toStringAsFixed(4)}',
    );
    // O modo desleixado chega a qualquer alvo.
    final seco = simplifyMesh(
      g.indices,
      g.posicoes,
      targetIndexCount: 300,
      targetError: 1,
      sloppy: true,
    )!;
    expect(seco.indices.length, lessThanOrEqualTo(600));
  });

  test('codec do EXT_meshopt_compression vai e volta byte a byte', () {
    const side = 30;
    final g = _grade(side);
    final bytes = Uint8List.view(g.posicoes.buffer);
    // O codec de vertices pede passo multiplo de 4.
    final comprimido = encodeMeshoptVertices(bytes, 12)!;
    expect(comprimido.length, lessThan(bytes.length));
    final volta = decodeMeshopt(
      mode: MeshoptMode.attributes,
      count: side * side,
      stride: 12,
      source: comprimido,
    )!;
    expect(volta, bytes);

    final tris = encodeMeshoptTriangles(g.indices, side * side)!;
    final indicesVolta = decodeMeshopt(
      mode: MeshoptMode.triangles,
      count: g.indices.length,
      stride: 4,
      source: tris,
    )!;
    // O codec de triangulos pode girar a ordem dos cantos de cada
    // triangulo, mas preserva o triangulo e o enrolamento.
    final lidos = Uint32List.view(indicesVolta.buffer);
    String rotulo(List<int> t) {
      final m = [t, [t[1], t[2], t[0]], [t[2], t[0], t[1]]]
        ..sort((a, b) => a[0].compareTo(b[0]));
      return m.first.join(',');
    }

    final esperado = <String>{
      for (var i = 0; i < g.indices.length; i += 3)
        rotulo([g.indices[i], g.indices[i + 1], g.indices[i + 2]]),
    };
    final obtido = <String>{
      for (var i = 0; i < lidos.length; i += 3)
        rotulo([lidos[i], lidos[i + 1], lidos[i + 2]]),
    };
    expect(obtido, esperado);
    // Lixo nao derruba: devolve null.
    expect(
      decodeMeshopt(
        mode: MeshoptMode.attributes,
        count: 10,
        stride: 12,
        source: Uint8List.fromList(List.filled(40, 7)),
      ),
      isNull,
    );
  });
}
