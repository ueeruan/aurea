import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

@Native<Int32 Function(Pointer<Uint32>, Uint32, Uint32)>(
  symbol: 'aurea_meshopt_cache',
)
external int _optimize(Pointer<Uint32> indices, int count, int vertices);

@Native<
  Uint32 Function(
    Pointer<Uint32>,
    Uint32,
    Uint32,
    Pointer<Pointer<Float>>,
    Pointer<Uint32>,
    Uint32,
    Pointer<Uint32>,
  )
>(symbol: 'aurea_meshopt_weld')
external int _weld(
  Pointer<Uint32> indices,
  int indexCount,
  int vertexCount,
  Pointer<Pointer<Float>> streams,
  Pointer<Uint32> components,
  int streamCount,
  Pointer<Uint32> remap,
);

@Native<Uint32 Function(Pointer<Uint32>, Uint32, Uint32, Pointer<Uint32>)>(
  symbol: 'aurea_meshopt_fetch',
)
external int _fetch(
  Pointer<Uint32> indices,
  int indexCount,
  int vertexCount,
  Pointer<Uint32> remap,
);

@Native<
  Uint32 Function(
    Pointer<Uint32>,
    Uint32,
    Pointer<Float>,
    Uint32,
    Pointer<Float>,
    Uint32,
    Float,
    Uint32,
    Uint32,
    Pointer<Uint32>,
    Pointer<Float>,
  )
>(symbol: 'aurea_meshopt_simplify')
external int _simplify(
  Pointer<Uint32> indices,
  int indexCount,
  Pointer<Float> positions,
  int vertexCount,
  Pointer<Float> normals,
  int targetIndexCount,
  double targetError,
  int options,
  int sloppy,
  Pointer<Uint32> destination,
  Pointer<Float> resultError,
);

@Native<
  Int32 Function(
    Uint32,
    Uint32,
    Pointer<Uint8>,
    Uint32,
    Uint32,
    Pointer<Uint8>,
    Uint32,
  )
>(symbol: 'aurea_meshopt_decode')
external int _decode(
  int mode,
  int filter,
  Pointer<Uint8> destination,
  int count,
  int stride,
  Pointer<Uint8> buffer,
  int bufferSize,
);

@Native<Uint32 Function(Uint32, Uint32, Uint32)>(
  symbol: 'aurea_meshopt_encode_bound',
)
external int _encodeBound(int kind, int count, int extra);

@Native<
  Uint32 Function(Pointer<Uint8>, Uint32, Pointer<Uint8>, Uint32, Uint32)
>(symbol: 'aurea_meshopt_encode_vertex')
external int _encodeVertex(
  Pointer<Uint8> out,
  int outSize,
  Pointer<Uint8> vertices,
  int count,
  int stride,
);

@Native<Uint32 Function(Pointer<Uint8>, Uint32, Pointer<Uint32>, Uint32)>(
  symbol: 'aurea_meshopt_encode_index',
)
external int _encodeIndex(
  Pointer<Uint8> out,
  int outSize,
  Pointer<Uint32> indices,
  int count,
);

/// Preserves vertex data, triangle membership, winding and degenerates.
/// Only suitable for opaque geometry: transparency may depend on draw order.
Uint32List optimizeVertexCache(Uint32List indices, int vertices) {
  if (indices.isEmpty || indices.length % 3 != 0 || vertices <= 0) {
    return indices;
  }
  final memory = calloc<Uint32>(indices.length);
  try {
    memory.asTypedList(indices.length).setAll(0, indices);
    if (_optimize(memory, indices.length, vertices) == 0) return indices;
    return Uint32List.fromList(memory.asTypedList(indices.length));
  } finally {
    calloc.free(memory);
  }
}

/// A vertex renumbering: [indices] rewritten, [remap] old -> new
/// (0xFFFFFFFF for vertices no triangle uses), [vertexCount] new count.
typedef MeshRemap = ({Uint32List indices, Uint32List remap, int vertexCount});

/// WELDS vertices that are equal in every stream (one Float32List per
/// attribute, [components] values per vertex). Returns null on failure
/// (the caller keeps its mesh).
MeshRemap? weldVertices(
  Uint32List indices,
  int vertexCount,
  List<Float32List> streams,
  List<int> components,
) {
  if (indices.isEmpty ||
      indices.length % 3 != 0 ||
      vertexCount <= 0 ||
      streams.isEmpty ||
      streams.length != components.length) {
    return null;
  }
  for (var i = 0; i < streams.length; i++) {
    if (components[i] <= 0 ||
        streams[i].length != vertexCount * components[i]) {
      return null;
    }
  }
  final idx = calloc<Uint32>(indices.length);
  final remap = calloc<Uint32>(vertexCount);
  final ponteiros = calloc<Pointer<Float>>(streams.length);
  final comps = calloc<Uint32>(streams.length);
  final dados = <Pointer<Float>>[];
  try {
    idx.asTypedList(indices.length).setAll(0, indices);
    for (var i = 0; i < streams.length; i++) {
      final d = calloc<Float>(streams[i].length);
      d.asTypedList(streams[i].length).setAll(0, streams[i]);
      dados.add(d);
      ponteiros[i] = d;
      comps[i] = components[i];
    }
    final unique = _weld(
      idx,
      indices.length,
      vertexCount,
      ponteiros,
      comps,
      streams.length,
      remap,
    );
    if (unique == 0) return null;
    return (
      indices: Uint32List.fromList(idx.asTypedList(indices.length)),
      remap: Uint32List.fromList(remap.asTypedList(vertexCount)),
      vertexCount: unique,
    );
  } finally {
    for (final d in dados) {
      calloc.free(d);
    }
    calloc
      ..free(idx)
      ..free(remap)
      ..free(ponteiros)
      ..free(comps);
  }
}

/// Renumbers vertices in first-use order (run after the vertex cache
/// pass). Unused vertices are dropped. Returns null on failure.
MeshRemap? optimizeVertexFetch(Uint32List indices, int vertexCount) {
  if (indices.isEmpty || indices.length % 3 != 0 || vertexCount <= 0) {
    return null;
  }
  final idx = calloc<Uint32>(indices.length);
  final remap = calloc<Uint32>(vertexCount);
  try {
    idx.asTypedList(indices.length).setAll(0, indices);
    final unique = _fetch(idx, indices.length, vertexCount, remap);
    if (unique == 0) return null;
    return (
      indices: Uint32List.fromList(idx.asTypedList(indices.length)),
      remap: Uint32List.fromList(remap.asTypedList(vertexCount)),
      vertexCount: unique,
    );
  } finally {
    calloc
      ..free(idx)
      ..free(remap);
  }
}

/// SIMPLIFIES a triangle mesh to about [targetIndexCount] indices, never
/// moving vertices (the result indexes the same vertex buffer: skinning
/// and morphs keep working). [normals] (3 per vertex) protect shading.
/// [targetError] is relative to the mesh size (0.01 = 1%). [lockBorder]
/// keeps open borders in place. [sloppy] ignores topology to reach the
/// target at any cost. Returns null on failure.
({Uint32List indices, double error})? simplifyMesh(
  Uint32List indices,
  Float32List positions, {
  Float32List? normals,
  required int targetIndexCount,
  double targetError = .02,
  bool lockBorder = false,
  bool sloppy = false,
}) {
  final vertexCount = positions.length ~/ 3;
  if (indices.isEmpty ||
      indices.length % 3 != 0 ||
      vertexCount <= 0 ||
      positions.length != vertexCount * 3 ||
      (normals != null && normals.length != positions.length)) {
    return null;
  }
  final idx = calloc<Uint32>(indices.length);
  final pos = calloc<Float>(positions.length);
  final nor = normals == null ? nullptr : calloc<Float>(normals.length);
  final dst = calloc<Uint32>(indices.length);
  final erro = calloc<Float>();
  try {
    idx.asTypedList(indices.length).setAll(0, indices);
    pos.asTypedList(positions.length).setAll(0, positions);
    if (normals != null) nor.asTypedList(normals.length).setAll(0, normals);
    final n = _simplify(
      idx,
      indices.length,
      pos,
      vertexCount,
      nor,
      targetIndexCount.clamp(3, indices.length),
      targetError,
      lockBorder ? 1 : 0,
      sloppy ? 1 : 0,
      dst,
      erro,
    );
    if (n == 0 || n % 3 != 0) return null;
    return (
      indices: Uint32List.fromList(dst.asTypedList(n)),
      error: erro.value.toDouble(),
    );
  } finally {
    calloc
      ..free(idx)
      ..free(pos)
      ..free(dst)
      ..free(erro);
    if (normals != null) calloc.free(nor);
  }
}

/// Modes of EXT_meshopt_compression.
abstract final class MeshoptMode {
  static const attributes = 0;
  static const triangles = 1;
  static const indices = 2;
}

/// Filters of EXT_meshopt_compression.
abstract final class MeshoptFilter {
  static const none = 0;
  static const octahedral = 1;
  static const quaternion = 2;
  static const exponential = 3;
  static const color = 4;
}

/// Whether meshoptimizer can decode [count] x [stride] in [mode] with
/// [filter]. These are the preconditions the library only ASSERTS: violated
/// in a release build they write past the output buffer, in a debug build
/// they abort the process. The native side checks the same rules again.
bool meshoptDecodeValido(int mode, int filter, int count, int stride) {
  if (count <= 0 || stride <= 0 || stride > 256) return false;
  if (count * stride > 1 << 30) return false;
  if (mode == MeshoptMode.attributes) {
    if (stride % 4 != 0) return false;
    return switch (filter) {
      MeshoptFilter.none || MeshoptFilter.exponential => true,
      MeshoptFilter.octahedral ||
      MeshoptFilter.color => stride == 4 || stride == 8,
      MeshoptFilter.quaternion => stride == 8,
      _ => false,
    };
  }
  if (mode == MeshoptMode.triangles) {
    return (stride == 2 || stride == 4) &&
        count % 3 == 0 &&
        filter == MeshoptFilter.none;
  }
  if (mode == MeshoptMode.indices) {
    return (stride == 2 || stride == 4) && filter == MeshoptFilter.none;
  }
  return false;
}

/// DECODES one compressed glTF buffer view into [count] x [stride] bytes.
/// Returns null when the data is malformed (the decoder is safe on
/// untrusted input).
Uint8List? decodeMeshopt({
  required int mode,
  int filter = MeshoptFilter.none,
  required int count,
  required int stride,
  required Uint8List source,
}) {
  if (source.isEmpty || !meshoptDecodeValido(mode, filter, count, stride)) {
    return null;
  }
  final total = count * stride;
  final dst = calloc<Uint8>(total);
  final src = calloc<Uint8>(source.length);
  try {
    src.asTypedList(source.length).setAll(0, source);
    final ok = _decode(mode, filter, dst, count, stride, src, source.length);
    if (ok == 0) return null;
    return Uint8List.fromList(dst.asTypedList(total));
  } finally {
    calloc
      ..free(dst)
      ..free(src);
  }
}

/// ENCODES vertex bytes ([stride] per vertex) the way EXT_meshopt_compression
/// stores them. For tests and tools.
Uint8List? encodeMeshoptVertices(Uint8List vertices, int stride) {
  final count = vertices.length ~/ stride;
  if (count <= 0 || vertices.length != count * stride) return null;
  final bound = _encodeBound(0, count, stride);
  final out = calloc<Uint8>(bound);
  final src = calloc<Uint8>(vertices.length);
  try {
    src.asTypedList(vertices.length).setAll(0, vertices);
    final n = _encodeVertex(out, bound, src, count, stride);
    return n == 0 ? null : Uint8List.fromList(out.asTypedList(n));
  } finally {
    calloc
      ..free(out)
      ..free(src);
  }
}

/// ENCODES a triangle index list (EXT_meshopt_compression TRIANGLES mode).
Uint8List? encodeMeshoptTriangles(Uint32List indices, int vertexCount) {
  if (indices.isEmpty || indices.length % 3 != 0) return null;
  final bound = _encodeBound(1, indices.length, vertexCount);
  final out = calloc<Uint8>(bound);
  final src = calloc<Uint32>(indices.length);
  try {
    src.asTypedList(indices.length).setAll(0, indices);
    final n = _encodeIndex(out, bound, src, indices.length);
    return n == 0 ? null : Uint8List.fromList(out.asTypedList(n));
  } finally {
    calloc
      ..free(out)
      ..free(src);
  }
}
