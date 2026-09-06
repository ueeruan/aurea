import 'dart:io';
import 'dart:isolate';
import 'dart:math' as math;
import 'dart:ui';

import 'package:thermion_flutter/thermion_flutter.dart' as f;
import 'package:vector_math/vector_math_64.dart' as vm;

import '../../domain/scene3d.dart';
import 'adaptive_quality.dart';
import 'renderer3d.dart';
import 'scene_glb.dart';
import 'filament_native.dart';

/// Native backend under validation. Production stays on the existing backend
/// until device, effect-composition and export parity tests pass.
const filamentPreviewEnabled = bool.fromEnvironment('AUREA_FILAMENT');

class FilamentRenderer implements Renderer3D {
  FilamentRenderer(this.quality, {this.viewerFactory});
  final Future<f.ThermionViewer> Function()? viewerFactory;
  final AdaptiveQuality quality;
  f.ThermionViewer? _viewer;
  f.ThermionViewer get viewer => _viewer!;
  final _nodes = <Object, _ResidentNode>{};
  int get residentAssetCount => _nodes.length;
  double nativeResolutionScale = 1;
  double? lastGpuMilliseconds;
  bool frameSubmitted = false;
  List<Light3D>? _lights;
  Color? _background;
  bool _backgroundInitialized = false;
  Object? _viewSettings;
  final _lightEntities = <String, int>{};
  bool _disposed = false;
  @override
  String get name => 'Filament';

  /// Unsupported features route the whole layer to the compatibility backend;
  /// never silently omit objects, animation or video-textured materials.
  static bool supports(Scene3D scene) =>
      !scene.nodes.any(
        (n) =>
            n.subdivisions != 0 ||
            n.material.textureLayerId != null ||
            n.material.faceImagePaths.isNotEmpty ||
            (n.modelAsset != null &&
                (n.modelMotion.keys.isNotEmpty ||
                    (n.modelMotion.clip >= 0 &&
                        n.modelMotion.clip < n.modelAsset!.clips.length))),
      ) &&
      !scene.panorama.showBackground &&
      !scene.planarFloorReflection;

  @override
  Future<void> initialize() async {
    f.ThermionFlutterPlugin.instance.setOptions(
      f.ThermionFlutterOptions(
        nativeOptions: f.NativeOptions(
          // iOS uses Metal. Android defaults to GLES until Vulkan texture sharing
          // is qualified on both Adreno and Mali; it is not a CPU fallback.
          backend: Platform.isIOS ? f.Backend.METAL : f.Backend.DEFAULT,
          androidTextureSource: f.AndroidTextureSource.surfaceProducer,
        ),
      ),
    );
    _viewer =
        await (viewerFactory?.call() ?? f.ThermionFlutterPlugin.createViewer());
    viewer.app.setAutomaticInstancingEnabled(true);
    await viewer.setRendering(false);
    await viewer.view.setBlendMode(f.BlendMode.transparent);
    await viewer.view.setFrustumCullingEnabled(true);
    await viewer.view.setPostProcessing(true);
    await configureFilamentDynamicResolution(viewer);
  }

  Object _geometryKey(SceneNode n) => (
    n.modelAsset,
    n.mesh,
    n.kind,
    n.material,
    n.useModelMaterials,
    n.modelMotion,
  );

  @override
  Future<void> synchronize(
    Scene3D scene,
    RenderCamera camera,
    Duration time,
    Size size,
  ) async {
    if (_disposed || size.isEmpty) return;
    final groups = <Object, List<SceneNode>>{};
    for (final n in scene.nodes) {
      if (n.visible && !n.isNull) (groups[_geometryKey(n)] ??= []).add(n);
    }
    for (final id in _nodes.keys.toList()) {
      if (!groups.containsKey(id)) {
        await viewer.destroyAsset(_nodes.remove(id)!.asset);
      }
    }
    for (final group in groups.entries) {
      final key = group.key;
      final count = group.value.fold<int>(
        0,
        (sum, n) => sum + math.max(1, n.instances.length),
      );
      var resident = _nodes[key];
      if (resident == null || resident.instances.length != count) {
        // Release superseded GPU resources before admitting the replacement.
        if (resident != null) {
          _nodes.remove(key);
          await viewer.destroyAsset(resident.asset);
        }
        final source = group.value.first;
        final bytes = await Isolate.run(() => encodeNodeGlb(source));
        if (_disposed) return;
        final asset = await viewer.loadGltfFromBuffer(
          bytes,
          initialInstances: count,
          releaseSourceData: true,
          loadResourcesAsync: true,
        );
        resident = _ResidentNode(key, asset, await asset.getInstances());
        _nodes[key] = resident;
      }
      var offset = 0;
      for (final node in group.value) {
        final instanceCount = math.max(1, node.instances.length);
        final xf = resolveNodeTransform(scene, node, time);
        final transformKey = (
          node.id,
          offset,
          xf.position.x,
          xf.position.y,
          xf.position.z,
          xf.rotX,
          xf.rotY,
          xf.rotZ,
          xf.scale,
          node.size,
          node.instances,
        );
        if (resident.transformKeys[node.id] == transformKey) {
          offset += instanceCount;
          continue;
        }
        resident.transformKeys[node.id] = transformKey;
        final matrix =
            vm.Matrix4.translation(
                vm.Vector3(xf.position.x, xf.position.y, xf.position.z),
              )
              ..rotateZ(xf.rotZ * math.pi / 180)
              ..rotateY(xf.rotY * math.pi / 180)
              ..rotateX(xf.rotX * math.pi / 180);
        matrix.multiply(
          vm.Matrix4.diagonal3Values(xf.scale, xf.scale, xf.scale),
        );
        if (node.instances.isEmpty) {
          matrix.multiply(
            vm.Matrix4.diagonal3Values(node.size, node.size, node.size),
          );
          await resident.instances[offset].setTransform(matrix);
        } else {
          for (var i = 0; i < node.instances.length; i++) {
            final p = node.instances[i];
            final instance =
                matrix * vm.Matrix4.translation(vm.Vector3(p.x, p.y, p.z));
            instance.multiply(
              vm.Matrix4.diagonal3Values(node.size, node.size, node.size),
            );
            await resident.instances[offset + i].setTransform(instance);
          }
        }
        offset += instanceCount;
      }
      final liveIds = {for (final n in group.value) n.id};
      resident.transformKeys.removeWhere((id, _) => !liveIds.contains(id));
    }
    if (!identical(_lights, scene.lights)) {
      await viewer.destroyLights();
      _lightEntities.clear();
      _lights = scene.lights;
      for (final light in scene.lights) {
        if (light.kind == Light3DKind.ambient) continue;
        final color = light.color;
        _lightEntities[light.id] = await viewer.addDirectLight(
          f.DirectLight(
            type: switch (light.kind) {
              Light3DKind.point => f.LightType.POINT,
              Light3DKind.spot => f.LightType.SPOT,
              _ => f.LightType.DIRECTIONAL,
            },
            intensity: light.intensity.valueAt(time) * 100000,
            color: f.LinearColor(color.r, color.g, color.b),
            castShadows: light.castsShadow,
            direction: _vector(light.direction),
            position: _vector(light.position),
            falloffRadius: light.range,
            spotLightConeOuter: light.coneDegrees * math.pi / 180,
            spotLightConeInner: light.coneDegrees * math.pi / 360,
          ),
        );
      }
    }
    for (final light in scene.lights) {
      final entity = _lightEntities[light.id];
      if (entity != null) {
        viewer.app.lightManager.setIntensity(
          entity,
          light.intensity.valueAt(time) * 100000,
        );
      }
    }
    final nativeCamera = await viewer.view.getCamera();
    await nativeCamera.lookAt(
      _vector(camera.position),
      focus: _vector(camera.target),
      up: _vector(camera.up),
    );
    await nativeCamera.setProjectionFromHorizontalFieldOfView(
      camera.fovDegrees,
      camera.near,
      camera.far,
      size.aspectRatio,
    );
    final c = scene.background;
    // Thermion replaces its skybox on setBackgroundColor; never churn this GPU
    // resource during camera scrubbing when the background has not changed.
    if (!_backgroundInitialized || _background != c) {
      await viewer.setBackgroundColor(
        c?.r ?? 0,
        c?.g ?? 0,
        c?.b ?? 0,
        c?.a ?? 0,
      );
      _background = c;
      _backgroundInitialized = true;
    }
    final shadows = quality.shadows && !scene.draftMode;
    final msaa = quality.msaa && scene.msaa;
    final ao = quality.ambientOcclusion && !scene.draftMode;
    final settings = (
      shadows,
      msaa,
      ao,
      scene.fogDensity,
      scene.fogStart,
      scene.fogColor,
    );
    if (_viewSettings != settings) {
      await viewer.view.setShadowsEnabled(shadows);
      await viewer.view.setAntiAliasing(msaa, true, false);
      await viewer.view.setAmbientOcclusionOptions(
        f.AmbientOcclusionOptions(enabled: ao),
      );
      await viewer.view.setFogOptions(
        f.FogOptions(
          enabled: scene.fogDensity > 0,
          density: scene.fogDensity,
          distance: scene.fogStart,
          linearColor: vm.Vector3(
            scene.fogColor.r,
            scene.fogColor.g,
            scene.fogColor.b,
          ),
        ),
      );
      _viewSettings = settings;
    }
    frameSubmitted = await renderFilamentPreview(viewer);
    nativeResolutionScale = await readFilamentResolutionScale(viewer);
    quality.observeNativeScale(nativeResolutionScale);
    lastGpuMilliseconds = await readFilamentGpuMilliseconds(viewer);
  }

  static vm.Vector3 _vector(Vec3 v) => vm.Vector3(v.x, v.y, v.z);
  @override
  Future<void> suspend() async {
    if (_viewer != null) await viewer.setRendering(false);
  }

  @override
  Future<void> dispose() async {
    if (_disposed) return;
    _disposed = true;
    if (_viewer == null) return;
    await suspend();
    if (viewerFactory == null) {
      await f.ThermionFlutterPlugin.instance.destroyTextureForView(viewer.view);
    }
    await viewer.dispose();
    _nodes.clear();
    _lightEntities.clear();
    _viewer = null;
  }
}

class _ResidentNode {
  _ResidentNode(this.key, this.asset, this.instances);
  final Object key;
  final f.ThermionAsset asset;
  final List<f.ThermionAsset> instances;
  final transformKeys = <String, Object>{};
}
