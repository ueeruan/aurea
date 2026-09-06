import 'dart:async';
import 'dart:io';
import 'dart:isolate';
import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/foundation.dart';

import '../domain/panorama3d.dart';
import '../domain/scene3d.dart';

/// Cache de panorama importado. A imagem e reduzida, convertida em seis
/// faces e pre-filtrada em mipmaps uma vez; os quadros seguintes so amostram
/// o cubo pronto.
class PanoramaCache {
  PanoramaCache._();

  static final PanoramaCache instance = PanoramaCache._();

  final ValueNotifier<int> revision = ValueNotifier<int>(0);
  final Map<String, _PanoramaCube> _ready = {};
  final Set<String> _loading = {};
  final Set<String> _failed = {};
  final Map<String, Future<void>> _pending = {};
  int _generation = 0;

  EnvironmentSampler? samplerFor(Panorama3D panorama) {
    final path = panorama.sourcePath;
    if (path == null || path.isEmpty) return null;
    final key = _key(panorama);
    final cube = _ready[key];
    if (cube == null) {
      if (!_failed.contains(key)) {
        unawaited(_loadOnce(key, path, panorama));
      }
      return null;
    }
    return cube.sample;
  }

  /// Garante que o panorama esteja pronto antes de uma captura determinista.
  /// Falhas de arquivo/codec devolvem false; o chamador decide se pode usar
  /// fallback ou se deve interromper a exportacao.
  Future<bool> prepare(Panorama3D panorama) async {
    final path = panorama.sourcePath;
    if (path == null || path.isEmpty) return true;
    final key = _key(panorama);
    if (_ready.containsKey(key)) return true;
    if (_failed.contains(key)) return false;
    await _loadOnce(key, path, panorama);
    return _ready.containsKey(key);
  }

  String _key(Panorama3D panorama) =>
      '${panorama.sourcePath}|'
      '${panorama.coverageDegrees.toStringAsFixed(2)}|'
      '${panorama.mirrorTo360}|${panorama.seamSoftness.toStringAsFixed(3)}|'
      '${panorama.fillZenithNadir}';

  Future<void> _loadOnce(String key, String path, Panorama3D panorama) {
    final running = _pending[key];
    if (running != null) return running;
    final future = _load(key, path, panorama);
    _pending[key] = future;
    return future.whenComplete(() {
      _pending.remove(key);
    });
  }

  Future<void> _load(String key, String path, Panorama3D panorama) async {
    _loading.add(key);
    final generation = _generation;
    try {
      final bytes = await File(path).readAsBytes();
      final codec = await ui.instantiateImageCodec(
        bytes,
        targetWidth: 1024,
        targetHeight: 512,
      );
      final frame = await codec.getNextFrame();
      codec.dispose();
      final image = frame.image;
      final data = await image.toByteData(format: ui.ImageByteFormat.rawRgba);
      if (data == null) throw StateError('Panorama sem pixels');
      final pixels = _PanoramaPixels(
        image.width,
        image.height,
        data.buffer.asUint8List(),
      );
      image.dispose();
      final cube = await Isolate.run(() => _PanoramaCube.fromEquirectangular(pixels, panorama));
      if (generation != _generation) return;
      if (_ready.length >= 3) _ready.remove(_ready.keys.first);
      _ready[key] = cube;
      revision.value++;
    } catch (_) {
      _failed.add(key);
    } finally {
      _loading.remove(key);
    }
  }

  void clear() {
    _generation++;
    _ready.clear();
    _failed.clear();
    revision.value++;
  }
}

class _PanoramaPixels {
  _PanoramaPixels(this.width, this.height, this.rgba)
    : topAverage = _edgeAverage(width, height, rgba, top: true),
      bottomAverage = _edgeAverage(width, height, rgba, top: false);

  final int width;
  final int height;
  final Uint8List rgba;
  final EnvironmentSample topAverage;
  final EnvironmentSample bottomAverage;

  EnvironmentSample sampleDirection(Vec3 direction, Panorama3D panorama) {
    final n = direction.normalized;
    var u = math.atan2(n.x, n.z) / (2 * math.pi) + 0.5;
    var v = 0.5 - math.asin(n.y.clamp(-1.0, 1.0)) / math.pi;
    u -= u.floorToDouble();
    v = v.clamp(0.0, 1.0).toDouble();

    if (panorama.mirrorTo360) {
      final repeats =
          360 / panorama.coverageDegrees.clamp(1.0, 360.0).toDouble();
      final phase = ((u * repeats) % 2).toDouble();
      u = phase <= 1 ? phase : 2 - phase;
    }

    final base = _pixel(u, v);
    var r = base.r, g = base.g, b = base.b;

    if (panorama.fillZenithNadir) {
      if (v < 0.13) {
        final k = 1 - v / 0.13;
        r += (topAverage.r - r) * k;
        g += (topAverage.g - g) * k;
        b += (topAverage.b - b) * k;
      } else if (v > 0.87) {
        final k = (v - 0.87) / 0.13;
        r += (bottomAverage.r - r) * k;
        g += (bottomAverage.g - g) * k;
        b += (bottomAverage.b - b) * k;
      }
    }

    final seam = panorama.seamSoftness.clamp(0.0, 0.45).toDouble();
    final edge = math.min(u, 1 - u);
    if (seam > 0 && edge < seam) {
      final other = _pixel(1 - u, v);
      final k = (1 - edge / seam) * 0.5;
      r += (other.r - r) * k;
      g += (other.g - g) * k;
      b += (other.b - b) * k;
    }
    return (r: r, g: g, b: b);
  }

  EnvironmentSample _pixel(double u, double v) {
    u -= u.floorToDouble();
    v = v.clamp(0.0, 1.0).toDouble();
    final x = u * width - .5, y = (v * height - .5).clamp(0.0, height - 1.0);
    final x0 = x.floor(), y0 = y.floor(), tx = x - x0, ty = y - y0;
    double channel(int c) {
      double at(int xx, int yy) => rgba[((yy.clamp(0, height-1)) * width + (xx % width)) * 4 + c] / 255;
      final a = at(x0, y0)*(1-tx) + at(x0+1, y0)*tx;
      final b = at(x0, y0+1)*(1-tx) + at(x0+1, y0+1)*tx;
      return a*(1-ty) + b*ty;
    }
    return (r: channel(0), g: channel(1), b: channel(2));
  }

  static EnvironmentSample _edgeAverage(
    int width,
    int height,
    Uint8List rgba, {
    required bool top,
  }) {
    final rows = math.max(1, (height * 0.04).round());
    final start = top ? 0 : height - rows;
    var r = 0.0, g = 0.0, b = 0.0, count = 0;
    for (var y = start; y < start + rows; y++) {
      for (var x = 0; x < width; x += 2) {
        final i = (y * width + x) * 4;
        r += rgba[i] / 255;
        g += rgba[i + 1] / 255;
        b += rgba[i + 2] / 255;
        count++;
      }
    }
    return (r: r / count, g: g / count, b: b / count);
  }
}

/// Cubo pre-filtrado. Cada nivel tem seis faces; a rugosidade escolhe e
/// interpola dois mips sem voltar a imagem equiretangular.
class _PanoramaCube {
  _PanoramaCube(this.levels, this.sizes);

  factory _PanoramaCube.fromEquirectangular(
    _PanoramaPixels source,
    Panorama3D panorama,
  ) {
    const side = 256;
    final base = [
      for (var face = 0; face < 6; face++) Float32List(side * side * 3),
    ];
    for (var face = 0; face < 6; face++) {
      final out = base[face];
      for (var y = 0; y < side; y++) {
        for (var x = 0; x < side; x++) {
          final u = (x + 0.5) / side * 2 - 1;
          final v = (y + 0.5) / side * 2 - 1;
          final color = source.sampleDirection(
            _faceDirection(face, u, v),
            panorama,
          );
          final index = (y * side + x) * 3;
          out[index] = color.r;
          out[index + 1] = color.g;
          out[index + 2] = color.b;
        }
      }
    }

    final levels = <List<Float32List>>[base];
    final sizes = <int>[side];
    var previousSize = side;
    while (previousSize > 1) {
      final size = math.max(1, previousSize ~/ 2);
      final next = [
        for (var face = 0; face < 6; face++) Float32List(size * size * 3),
      ];
      for (var face = 0; face < 6; face++) {
        for (var y = 0; y < size; y++) {
          for (var x = 0; x < size; x++) {
            final normal = _faceDirection(face, (x+.5)/size*2-1, (y+.5)/size*2-1);
            final up = normal.z.abs() < .999 ? const Vec3(0,0,1) : const Vec3(1,0,0);
            final tangent = up.cross(normal).normalized, bitangent = normal.cross(tangent);
            final rough = levels.length / 8.0, alpha = rough * rough;
            var r = 0.0, g = 0.0, b = 0.0, total = 0.0;
            // Deterministic Hammersley/GGX importance sampling, across cube
            // boundaries. Rough faces must not average only their own face.
            for (var k = 0; k < 32; k++) {
              var bits = k, inv = 0.0, fraction = .5;
              for (var bit = 0; bit < 5; bit++) { inv += (bits & 1)*fraction; bits >>= 1; fraction *= .5; }
              final phi = 2*math.pi*k/32;
              final cosTheta = math.sqrt((1-inv)/(1+(alpha*alpha-1)*inv));
              final sinTheta = math.sqrt(math.max(0, 1-cosTheta*cosTheta));
              final half = tangent*(math.cos(phi)*sinTheta) + bitangent*(math.sin(phi)*sinTheta) + normal*cosTheta;
              final light = half*(2*normal.dot(half)) - normal;
              final weight = math.max(0.0, normal.dot(light));
              if (weight == 0) continue;
              final sample = source.sampleDirection(light, panorama);
              r += sample.r*weight; g += sample.g*weight; b += sample.b*weight; total += weight;
            }
            final at = (y*size+x)*3;
            next[face][at] = r/total; next[face][at+1] = g/total; next[face][at+2] = b/total;
          }
        }
      }
      levels.add(next);
      sizes.add(size);
      previousSize = size;
    }
    return _PanoramaCube(levels, sizes);
  }

  final List<List<Float32List>> levels;
  final List<int> sizes;

  EnvironmentSample sample(Vec3 direction, double roughness) {
    final mip = roughnessMip(roughness, mipLevels: levels.length);
    final lo = mip.floor().clamp(0, levels.length - 1);
    final hi = mip.ceil().clamp(0, levels.length - 1);
    final a = _sampleLevel(lo, direction);
    if (lo == hi) return a;
    final b = _sampleLevel(hi, direction);
    final t = mip - lo;
    return (
      r: a.r + (b.r - a.r) * t,
      g: a.g + (b.g - a.g) * t,
      b: a.b + (b.b - a.b) * t,
    );
  }

  EnvironmentSample _sampleLevel(int level, Vec3 direction) {
    final mapped = _directionToFace(direction.normalized);
    final size = sizes[level];
    final x = (mapped.u+1)*.5*size-.5, y = (mapped.v+1)*.5*size-.5;
    final ix = x.floor(), iy = y.floor(), tx = x-ix, ty = y-iy;
    double channel(int c) {
      double at(int px, int py) {
        var face = mapped.face;
        if (px < 0 || px >= size || py < 0 || py >= size) {
          final remap = _directionToFace(_faceDirection(face, (px+.5)/size*2-1, (py+.5)/size*2-1));
          face = remap.face;
          px = ((remap.u+1)*.5*size).floor().clamp(0, size-1);
          py = ((remap.v+1)*.5*size).floor().clamp(0, size-1);
        }
        return levels[level][face][(py*size+px)*3+c];
      }
      return (at(ix,iy)*(1-tx)+at(ix+1,iy)*tx)*(1-ty) + (at(ix,iy+1)*(1-tx)+at(ix+1,iy+1)*tx)*ty;
    }
    return (r: channel(0), g: channel(1), b: channel(2));
  }

  static Vec3 _faceDirection(int face, double u, double v) => switch (face) {
    0 => Vec3(1, -v, -u).normalized,
    1 => Vec3(-1, -v, u).normalized,
    2 => Vec3(u, 1, v).normalized,
    3 => Vec3(u, -1, -v).normalized,
    4 => Vec3(u, -v, 1).normalized,
    _ => Vec3(-u, -v, -1).normalized,
  };

  static ({int face, double u, double v}) _directionToFace(Vec3 direction) {
    if (direction.length < 1e-9) return (face: 4, u: 0, v: 0);
    final ax = direction.x.abs();
    final ay = direction.y.abs();
    final az = direction.z.abs();
    if (ax >= ay && ax >= az) {
      if (direction.x >= 0) {
        return (face: 0, u: -direction.z / ax, v: -direction.y / ax);
      }
      return (face: 1, u: direction.z / ax, v: -direction.y / ax);
    }
    if (ay >= ax && ay >= az) {
      if (direction.y >= 0) {
        return (face: 2, u: direction.x / ay, v: direction.z / ay);
      }
      return (face: 3, u: direction.x / ay, v: -direction.z / ay);
    }
    if (direction.z >= 0) {
      return (face: 4, u: direction.x / az, v: -direction.y / az);
    }
    return (face: 5, u: -direction.x / az, v: -direction.y / az);
  }
}
