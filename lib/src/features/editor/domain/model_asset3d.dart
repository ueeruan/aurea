import 'dart:math' as math;
import 'dart:ui';

import 'package:vector_math/vector_math_64.dart' as vm;

import 'element3d.dart';
import 'scene3d.dart';

/// Portable, decoded model data. Geometry remains in the file's coordinate
/// system; a SINGLE bind-pose normalization is applied after skinning. Never
/// normalize individual parts or animation frames (that destroys a rig).
class ModelAsset3D {
  ModelAsset3D(this.data);
  final Map<String, dynamic> data;
  String get name => data['name'] as String? ?? 'Modelo';
  List<dynamic> get nodes => data['nodes'] as List;
  List<dynamic> get primitives => data['primitives'] as List;
  List<dynamic> get skins => data['skins'] as List? ?? const [];
  List<dynamic> get clips => data['clips'] as List? ?? const [];
  List<String> get warnings => (data['warnings'] as List? ?? []).cast<String>();
  List<String> get clipNames => [for (final c in clips) c['name'] as String];
  Set<int> get joints => {
    for (final s in skins) ...(s['joints'] as List).cast<int>(),
  };
  int get triangleCount => primitives.fold<int>(
    0,
    (sum, p) => sum + (p['indices'] as List).length ~/ 3,
  );

  // Bind bounds and one evaluated frame are cached per immutable asset. All
  // edits replace the motion object, so seeking backwards is deterministic.
  late final _bounds = _bindBounds();
  double get poseTranslationRange => 4 / _bounds.scale;
  late final int estimatedBytes = () {
    var bytes = 0;
    for (final p in primitives) {
      final count = (p['positions'] as List).length;
      bytes += count * 320 + (p['indices'] as List).length * 8;
      bytes += count * (p['targets'] as List? ?? []).length * 64;
    }
    for (final c in clips) {
      for (final channel in c['channels'] as List) {
        bytes += (channel['times'] as List).length * 64;
        for (final v in channel['values'] as List) {
          bytes += (v as List).length * 16;
        }
      }
    }
    for (final material in data['materials'] as List? ?? []) {
      bytes += (material['image'] as String? ?? '').length * 2;
    }
    return bytes;
  }();
  ModelMotion3D? _lastMotion;
  int? _lastTime;
  ModelFrame3D? _lastFrame;

  ModelFrame3D evaluate(Duration time, ModelMotion3D motion) {
    final staticPose =
        motion.keys.isEmpty && (motion.clip < 0 || motion.clip >= clips.length);
    if (identical(_lastMotion, motion) &&
        (_lastTime == time.inMicroseconds || staticPose)) {
      return _lastFrame!;
    }
    final frame = _evaluate(time, motion, normalize: true);
    _lastMotion = motion;
    _lastTime = time.inMicroseconds;
    return _lastFrame = frame;
  }

  ({vm.Vector3 center, double scale}) _bindBounds() {
    final f = _evaluate(
      Duration.zero,
      const ModelMotion3D(clip: -1),
      normalize: false,
    );
    final lo = vm.Vector3.all(double.infinity),
        hi = vm.Vector3.all(-double.infinity);
    for (final v in f.mesh.verts) {
      for (var i = 0; i < 3; i++) {
        lo[i] = math.min(lo[i], v[i]);
        hi[i] = math.max(hi[i], v[i]);
      }
    }
    if (f.mesh.verts.isEmpty) return (center: vm.Vector3.zero(), scale: 1);
    final size = hi - lo;
    return (
      center: (hi + lo) * .5,
      scale: 2 / math.max(1e-9, math.max(size.x, math.max(size.y, size.z))),
    );
  }

  ModelFrame3D _evaluate(
    Duration time,
    ModelMotion3D motion, {
    required bool normalize,
  }) {
    final trs = [
      for (final n in nodes)
        <String, List<double>>{
          'translation': modelDoubles(n['translation'] ?? [0, 0, 0]),
          'rotation': modelDoubles(n['rotation'] ?? [0, 0, 0, 1]),
          'scale': modelDoubles(n['scale'] ?? [1, 1, 1]),
          'weights': modelDoubles(n['weights'] ?? []),
        },
    ];
    if (motion.clip >= 0 && motion.clip < clips.length) {
      final clip = clips[motion.clip];
      final duration = (clip['duration'] as num).toDouble();
      var seconds = time.inMicroseconds / 1e6 * motion.speed + motion.offset;
      seconds = motion.loop && duration > 0
          ? seconds % duration
          : seconds.clamp(0.0, duration).toDouble();
      for (final channel in clip['channels'] as List) {
        final ni = channel['node'] as int;
        trs[ni][channel['path'] as String] = sampleModelChannel(
          channel,
          seconds,
        );
      }
    }
    final pose = motion.poseAt(time.inMicroseconds / 1e6);
    final local = <vm.Matrix4>[];
    for (var i = 0; i < nodes.length; i++) {
      final n = nodes[i], tr = trs[i];
      final m = n['matrix'] != null
          ? vm.Matrix4.fromList(modelDoubles(n['matrix']))
          : vm.Matrix4.compose(
              modelVector(tr['translation']!),
              modelQuaternion(tr['rotation']!),
              modelVector(tr['scale']!),
            );
      final delta = pose[i];
      if (delta != null) {
        m.multiply(
          vm.Matrix4.compose(
            modelVector(delta.translation),
            modelQuaternion(delta.rotation),
            vm.Vector3.all(delta.scale),
          ),
        );
      }
      local.add(m);
    }
    final world = List<vm.Matrix4?>.filled(nodes.length, null);
    vm.Matrix4 resolve(int i) {
      if (world[i] != null) return world[i]!;
      final parent = nodes[i]['parent'] as int?;
      final result = parent == null
          ? local[i]
          : (resolve(parent).clone()..multiply(local[i]));
      return world[i] = result;
    }

    for (var i = 0; i < nodes.length; i++) {
      resolve(i);
    }
    final skinMatrices = [
      for (final skin in skins)
        [
          for (var j = 0; j < (skin['joints'] as List).length; j++)
            world[skin['joints'][j] as int]!.clone()..multiply(
              vm.Matrix4.fromList(modelDoubles(skin['inverseBind'][j])),
            ),
        ],
    ];
    vm.Vector3 normalized(vm.Vector3 p) =>
        normalize ? (p - _bounds.center) * _bounds.scale : p;
    final vertices = <List<double>>[], faces = <List<int>>[];
    final uvs = <Offset?>[], normals = <Vec3?>[], materials = <Material3D>[];
    for (final p in primitives) {
      final ni = p['node'] as int;
      final positions = p['positions'] as List;
      final ns = p['normals'] as List?;
      final tex = p['uv'] as List?;
      final js = p['joints'] as List?, ws = p['weights'] as List?;
      final skin = nodes[ni]['skin'] as int?;
      final first = vertices.length;
      final morphs = p['targets'] as List? ?? const [];
      final morphWeights = trs[ni]['weights']!;
      for (var vi = 0; vi < positions.length; vi++) {
        final v = modelVector(positions[vi]);
        final normal = ns == null ? null : modelVector(ns[vi]);
        for (var k = 0; k < math.min(morphs.length, morphWeights.length); k++) {
          final w = morphWeights[k];
          if (w == 0) continue;
          if (morphs[k]['positions'] != null) {
            v.add(modelVector(morphs[k]['positions'][vi]) * w);
          }
          if (normal != null && morphs[k]['normals'] != null) {
            normal.add(modelVector(morphs[k]['normals'][vi]) * w);
          }
        }
        var transform = world[ni]!;
        if (skin != null && js != null && ws != null) {
          final mixed = vm.Matrix4.zero();
          var total = 0.0;
          for (var j = 0; j < (js[vi] as List).length; j++) {
            final w = (ws[vi][j] as num).toDouble();
            if (w <= 0) continue;
            final m = skinMatrices[skin][js[vi][j] as int];
            for (var k = 0; k < 16; k++) {
              mixed.storage[k] += m.storage[k] * w;
            }
            total += w;
          }
          if (total > 1e-9) {
            for (var k = 0; k < 16; k++) {
              mixed.storage[k] /= total;
            }
            transform = mixed;
          }
        }
        final out = normalized(transform.transformed3(v));
        vertices.add([out.x, out.y, out.z]);
        if (normal != null) {
          final nm = vm.Matrix3.zero();
          nm.copyNormalMatrix(transform);
          final result = nm.transformed(normal)..normalize();
          normals.add(Vec3(result.x, result.y, result.z));
        } else {
          normals.add(null);
        }
        uvs.add(
          tex == null
              ? null
              : Offset(
                  (tex[vi][0] as num).toDouble(),
                  (tex[vi][1] as num).toDouble(),
                ),
        );
      }
      final indices = p['indices'] as List;
      final material = _surface(p['material'] as int? ?? -1);
      // Mirrored node transforms reverse winding. Skinning already outputs
      // world coordinates; a mesh-node matrix must not be applied twice.
      final mirrored = skin == null && world[ni]!.determinant() < 0;
      for (var j = 0; j + 2 < indices.length; j += 3) {
        faces.add([
          first + (indices[j] as int),
          first + (indices[j + (mirrored ? 2 : 1)] as int),
          first + (indices[j + (mirrored ? 1 : 2)] as int),
        ]);
        materials.add(material);
      }
    }
    return ModelFrame3D(
      Element3DMesh(vertices, faces),
      uvs,
      normals,
      materials,
      {for (final j in joints) j: normalized(world[j]!.getTranslation())},
    );
  }

  final Map<int, Material3D> _surfaces = {};

  Material3D _surface(int index) =>
      _surfaces.putIfAbsent(index, () => _readSurface(index));

  Material3D _readSurface(int index) {
    final list = data['materials'] as List? ?? [];
    if (index < 0 || index >= list.length) return const Material3D();
    final m = list[index];
    final c = modelDoubles(m['color'] ?? [1, 1, 1, 1]);
    final alpha = m['alpha'] as String? ?? 'OPAQUE';
    return Material3D(
      name: m['name'] as String? ?? 'Material',
      baseColor: Color.from(
        alpha: alpha == 'OPAQUE' ? 1 : c[3],
        red: c[0],
        green: c[1],
        blue: c[2],
      ),
      metallic: (m['metallic'] as num? ?? 0).toDouble(),
      roughness: (m['roughness'] as num? ?? .6).toDouble(),
      emissive: (m['emissive'] as num? ?? 0).toDouble(),
      reflectivity: (m['metallic'] as num? ?? 0).toDouble(),
      opacity: alpha == 'OPAQUE' ? 1 : c[3],
      kind: m['unlit'] == true
          ? MaterialKind.unlit
          : alpha == 'BLEND'
          ? MaterialKind.transparent
          : alpha == 'MASK'
          ? MaterialKind.cutout
          : MaterialKind.pbr,
      alphaCutoff: (m['cutoff'] as num? ?? .5).toDouble(),
      doubleSided: m['doubleSided'] == true,
      imagePath: m['image'] as String?,
      textureWrapX: switch (m['wrapS'] ?? 10497) {
        33071 => TileMode.clamp,
        33648 => TileMode.mirror,
        _ => TileMode.repeated,
      },
      textureWrapY: switch (m['wrapT'] ?? 10497) {
        33071 => TileMode.clamp,
        33648 => TileMode.mirror,
        _ => TileMode.repeated,
      },
    );
  }
}

class ModelFrame3D {
  const ModelFrame3D(
    this.mesh,
    this.uvs,
    this.normals,
    this.materials,
    this.joints,
  );
  final Element3DMesh mesh;
  final List<Offset?> uvs;
  final List<Vec3?> normals;
  final List<Material3D> materials;
  final Map<int, vm.Vector3> joints;
}

class ModelPose3D {
  const ModelPose3D({
    this.translation = const [0, 0, 0],
    this.rotation = const [0, 0, 0, 1],
    this.scale = 1,
  });
  final List<double> translation, rotation;
  final double scale;
  Map<String, dynamic> toJson() => {
    't': translation,
    'r': rotation,
    's': scale,
  };
  factory ModelPose3D.fromJson(dynamic v) => ModelPose3D(
    translation: modelDoubles(v['t']),
    rotation: modelDoubles(v['r']),
    scale: (v['s'] as num).toDouble(),
  );
  static ModelPose3D lerp(ModelPose3D a, ModelPose3D b, double t) =>
      ModelPose3D(
        translation: [
          for (var i = 0; i < 3; i++)
            a.translation[i] + (b.translation[i] - a.translation[i]) * t,
        ],
        rotation: modelSlerp(a.rotation, b.rotation, t),
        scale: a.scale + (b.scale - a.scale) * t,
      );
}

class ModelPoseKey3D {
  const ModelPoseKey3D(this.seconds, this.pose);
  final double seconds;
  final Map<int, ModelPose3D> pose;
  Map<String, dynamic> toJson() => {
    't': seconds,
    'pose': {for (final e in pose.entries) '${e.key}': e.value.toJson()},
  };
}

class ModelMotion3D {
  const ModelMotion3D({
    this.clip = 0,
    this.speed = 1,
    this.offset = 0,
    this.loop = true,
    this.keys = const [],
  });
  final int clip;
  final double speed, offset;
  final bool loop;
  final List<ModelPoseKey3D> keys;
  ModelMotion3D copyWith({
    int? clip,
    double? speed,
    double? offset,
    bool? loop,
    List<ModelPoseKey3D>? keys,
  }) => ModelMotion3D(
    clip: clip ?? this.clip,
    speed: speed ?? this.speed,
    offset: offset ?? this.offset,
    loop: loop ?? this.loop,
    keys: keys ?? this.keys,
  );
  ModelMotion3D withPose(double seconds, Map<int, ModelPose3D> pose) =>
      copyWith(
        keys: [
          ...keys.where((k) => (k.seconds - seconds).abs() > 1e-6),
          ModelPoseKey3D(seconds, Map.unmodifiable(pose)),
        ]..sort((a, b) => a.seconds.compareTo(b.seconds)),
      );
  Map<int, ModelPose3D> poseAt(double seconds) {
    if (keys.isEmpty) return const {};
    if (seconds <= keys.first.seconds) return keys.first.pose;
    if (seconds >= keys.last.seconds) return keys.last.pose;
    var i = 1;
    while (keys[i].seconds < seconds) {
      i++;
    }
    final a = keys[i - 1], b = keys[i];
    final t = (seconds - a.seconds) / (b.seconds - a.seconds);
    return {
      for (final id in {...a.pose.keys, ...b.pose.keys})
        id: ModelPose3D.lerp(
          a.pose[id] ?? const ModelPose3D(),
          b.pose[id] ?? const ModelPose3D(),
          t,
        ),
    };
  }

  Map<String, dynamic> toJson() => {
    'clip': clip,
    'speed': speed,
    'offset': offset,
    'loop': loop,
    'keys': [for (final k in keys) k.toJson()],
  };
  factory ModelMotion3D.fromJson(dynamic m) {
    if (m == null) return const ModelMotion3D();
    return ModelMotion3D(
      clip: m['clip'] as int? ?? 0,
      speed: (m['speed'] as num? ?? 1).toDouble(),
      offset: (m['offset'] as num? ?? 0).toDouble(),
      loop: m['loop'] as bool? ?? true,
      keys: [
        for (final k in m['keys'] as List? ?? [])
          ModelPoseKey3D((k['t'] as num).toDouble(), {
            for (final e in (k['pose'] as Map).entries)
              int.parse(e.key as String): ModelPose3D.fromJson(e.value),
          }),
      ]..sort((a, b) => a.seconds.compareTo(b.seconds)),
    );
  }
}

List<double> modelDoubles(dynamic v) =>
    (v as List).map((x) => (x as num).toDouble()).toList();
vm.Vector3 modelVector(dynamic v) => vm.Vector3(
  (v[0] as num).toDouble(),
  (v[1] as num).toDouble(),
  (v[2] as num).toDouble(),
);
vm.Quaternion modelQuaternion(dynamic v) => vm.Quaternion(
  (v[0] as num).toDouble(),
  (v[1] as num).toDouble(),
  (v[2] as num).toDouble(),
  (v[3] as num).toDouble(),
)..normalize();

List<double> modelSlerp(List<double> a, List<double> b, double t) {
  var dot = 0.0;
  for (var i = 0; i < 4; i++) {
    dot += a[i] * b[i];
  }
  final sign = dot < 0 ? -1.0 : 1.0;
  dot = dot.abs().clamp(0.0, 1.0);
  final theta = math.acos(dot);
  final x = dot > .9995 ? 1 - t : math.sin((1 - t) * theta) / math.sin(theta);
  final y = dot > .9995 ? t : math.sin(t * theta) / math.sin(theta);
  final result = [for (var i = 0; i < 4; i++) a[i] * x + b[i] * y * sign];
  final length = math.sqrt(result.fold<double>(0, (sum, v) => sum + v * v));
  return length < 1e-12 ? [0, 0, 0, 1] : [for (final v in result) v / length];
}

/// glTF interpolation: time-scaled Hermite tangents, quaternion shortest-arc
/// SLERP for LINEAR and normalization AFTER CUBICSPLINE evaluation.
List<double> sampleModelChannel(dynamic c, double time) {
  final times = modelDoubles(c['times']), values = c['values'] as List;
  final cubic = c['interpolation'] == 'CUBICSPLINE';
  List<double> value(int i) => modelDoubles(values[cubic ? i * 3 + 1 : i]);
  if (time <= times.first) return value(0);
  if (time >= times.last) return value(times.length - 1);
  var lo = 0, hi = times.length - 1;
  while (hi - lo > 1) {
    final mid = (lo + hi) ~/ 2;
    if (times[mid] <= time) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  final a = value(lo), b = value(hi), dt = times[hi] - times[lo];
  final t = (time - times[lo]) / dt;
  if (c['interpolation'] == 'STEP') return a;
  if (!cubic && c['path'] == 'rotation') return modelSlerp(a, b, t);
  final result = [
    for (var j = 0; j < a.length; j++)
      cubic
          ? (2 * t * t * t - 3 * t * t + 1) * a[j] +
                (t * t * t - 2 * t * t + t) *
                    dt *
                    (values[lo * 3 + 2][j] as num) +
                (-2 * t * t * t + 3 * t * t) * b[j] +
                (t * t * t - t * t) * dt * (values[hi * 3][j] as num)
          : a[j] + (b[j] - a[j]) * t,
  ];
  if (c['path'] == 'rotation') {
    final norm = math.sqrt(result.fold<double>(0, (s, v) => s + v * v));
    return norm < 1e-12 ? [0, 0, 0, 1] : [for (final v in result) v / norm];
  }
  return result;
}
