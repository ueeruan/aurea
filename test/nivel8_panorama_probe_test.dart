import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/panorama3d.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';

void main() {
  group('panorama estatico', () {
    test('oferece exatamente os seis presets do nivel', () {
      expect(PanoramaPreset.values, hasLength(6));
      expect(
        PanoramaPreset.values.map(panoramaPresetLabel),
        containsAll(<String>[
          'Estudio',
          'Por do sol',
          'Noite',
          'Neon',
          'Branco',
          'Interior',
        ]),
      );
    });

    test('foto parcial e preparada uma vez e marcada aproximada', () {
      final panorama = preparePanorama(
        path: '/tmp/sala.jpg',
        coverageDegrees: 150,
        capturedWithPhone: true,
      );
      expect(panorama.source, PanoramaSource.camera);
      expect(panorama.approximate, isTrue);
      expect(panorama.mirrorTo360, isTrue);
      expect(panorama.fillZenithNadir, isTrue);
      expect(panorama.seamSoftness, greaterThan(0));
      expect(panorama.highlightBoost, greaterThan(0));
      expect(panorama.convertedAtImport, isTrue);
    });

    test('rugosidade percorre a cadeia de mip', () {
      expect(roughnessMip(0), 0);
      expect(roughnessMip(0.5), closeTo(3.5, 1e-9));
      expect(roughnessMip(1), 7);
    });
  });

  group('sonda de reflexo', () {
    test('atualiza uma face por quadro e zera custo quando estabiliza', () {
      final scheduler = ReflectionProbeScheduler();
      const probe = ReflectionProbe3D(enabled: true);
      for (var face = 0; face < 6; face++) {
        final pass = scheduler.nextFrame(probe);
        expect(pass, isNotNull);
        expect(pass!.face, face);
        expect(pass.resolution, 128);
        expect(pass.reflections, isFalse);
        expect(pass.shadows, isFalse);
        expect(pass.postEffects, isFalse);
        expect(pass.lodBias, 1);
      }
      expect(scheduler.nextFrame(probe), isNull);
      expect(scheduler.nextFrame(probe), isNull);

      scheduler.markDirty();
      expect(scheduler.nextFrame(probe, draftMode: true), isNull);
      expect(scheduler.nextFrame(probe)!.face, 0);
    });

    test('continuo nunca para e alta usa 512 px', () {
      final scheduler = ReflectionProbeScheduler(dirty: false);
      const probe = ReflectionProbe3D(
        enabled: true,
        quality: ProbeQuality.high,
        updateMode: ProbeUpdateMode.continuous,
      );
      final faces = [for (var i = 0; i < 8; i++) scheduler.nextFrame(probe)!];
      expect(faces.map((pass) => pass.face), [0, 1, 2, 3, 4, 5, 0, 1]);
      expect(faces.every((pass) => pass.resolution == 512), isTrue);
    });

    test('inclusao nunca vence a exclusao do proprio objeto', () {
      const probe = ReflectionProbe3D(
        enabled: true,
        includeNodeIds: {'heroi', 'vizinho'},
        excludeNodeIds: {'fora'},
      );
      expect(probe.includes('heroi', reflectiveNodeId: 'heroi'), isFalse);
      expect(probe.includes('vizinho', reflectiveNodeId: 'heroi'), isTrue);
      expect(probe.includes('fora', reflectiveNodeId: 'heroi'), isFalse);
      expect(probe.includes('nao-listado', reflectiveNodeId: 'heroi'), isFalse);
    });

    test('um unico objeto produz o mesmo resultado com a sonda ligada', () {
      final hero = SceneNode(
        id: 'heroi',
        material: materialFromPreset(MaterialPreset3D.polishedMetal),
      );
      final off = Scene3D(nodes: [hero], lights: const [], tonemap: false);
      final on = off.copyWith(
        reflectionProbe: const ReflectionProbe3D(enabled: true),
      );
      Color shade(Scene3D scene) => shadeFace(
        scene: scene,
        material: hero.material,
        normal: const Vec3(0, 0, -1),
        point: Vec3.zero,
        t: Duration.zero,
        viewDir: const Vec3(0, 0, -1),
        nodeId: hero.id,
      );
      expect(shade(on).toARGB32(), shade(off).toARGB32());
    });

    test('objeto vizinho colore o reflexo sem autorreflexao', () {
      final hero = SceneNode(
        id: 'heroi',
        material: materialFromPreset(MaterialPreset3D.polishedMetal),
      );
      final neighbor = SceneNode(
        id: 'vermelho',
        z: AnimatedDouble(-260),
        size: 130,
        material: const Material3D(baseColor: Color(0xFFFF1010)),
      );
      final base = Scene3D(
        nodes: [hero, neighbor],
        lights: const [],
        tonemap: false,
      );
      Color shade(Scene3D scene) => shadeFace(
        scene: scene,
        material: hero.material,
        normal: const Vec3(0, 0, -1),
        point: Vec3.zero,
        t: Duration.zero,
        viewDir: const Vec3(0, 0, -1),
        nodeId: hero.id,
      );
      final staticOnly = shade(base);
      final reflected = shade(
        base.copyWith(reflectionProbe: const ReflectionProbe3D(enabled: true)),
      );
      expect(
        reflected.r - reflected.b,
        greaterThan(staticOnly.r - staticOnly.b),
      );
    });
  });

  test('material oferece doze presets e degradacao corta efeitos caros', () {
    expect(MaterialPreset3D.values, hasLength(12));
    final scene = Scene3D(
      planarFloorReflection: true,
      reflectionProbe: const ReflectionProbe3D(
        enabled: true,
        quality: ProbeQuality.high,
        updateMode: ProbeUpdateMode.continuous,
      ),
    );
    final degraded = degradeScene(scene, 1);
    expect(degraded.planarFloorReflection, isFalse);
    expect(degraded.reflectionProbe.quality, ProbeQuality.low);
    expect(degraded.reflectionProbe.updateMode, ProbeUpdateMode.onMove);
  });
}
