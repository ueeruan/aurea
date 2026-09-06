import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/text_animator.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/particles_painter.dart';

/// Pinta o sistema numa tela gravada e devolve o numero de comandos —
/// so para garantir que cada combinacao desenha sem estourar.
int _pinta(ParticlesLayer l, Duration t) {
  final rec = ui.PictureRecorder();
  final canvas = Canvas(rec);
  ParticlesPainter(layer: l, time: t, rotXDeg: 20, rotYDeg: -35)
      .paint(canvas, const Size(420, 420));
  final pic = rec.endRecording();
  final n = pic.approximateBytesUsed;
  pic.dispose();
  return n;
}

ParticlesLayer _base() => ParticlesLayer(
      name: 'p',
      startTime: Duration.zero,
      duration: const Duration(seconds: 5),
      count: 120,
    );

void main() {
  group('particulas V1.1 (Particular)', () {
    test('toda forma, emissor e saida desenham sem erro', () {
      for (var shape = 0; shape < 6; shape++) {
        for (var em = 0; em < 4; em++) {
          for (var modo = 0; modo < 3; modo++) {
            final l = _base().copyParticles(
              shape: shape,
              emitter: em,
              emitMode: modo,
              trail: shape.isEven ? 0.5 : 0,
              turbulence: 80,
              drag: 0.6,
              windX: 120,
              spin: 90,
              sizeOverLife: shape % 4,
              opacityOverLife: em,
              colorEnd: const Color(0xFF35C4E7),
            );
            final bytes = _pinta(l, const Duration(milliseconds: 1300));
            expect(bytes, greaterThan(0), reason: 'forma $shape em $em/$modo');
          }
        }
      }
    });

    test('a simulacao e pura: mesmo tempo, mesmo desenho', () {
      final l = _base().copyParticles(turbulence: 200, trail: 0.4, drag: 1);
      final a = _pinta(l, const Duration(milliseconds: 2450));
      final b = _pinta(l, const Duration(milliseconds: 2450));
      expect(a, b);
    });

    test('shape vem de star quando nao salvo, e o store guarda tudo', () {
      final velho = _base();
      expect(velho.shape, 1, reason: 'star=true -> estrela');
      final semEstrela = ParticlesLayer(
          name: 'q',
          startTime: Duration.zero,
          duration: const Duration(seconds: 1),
          star: false);
      expect(semEstrela.shape, 0);

      final l = velho.copyParticles(
        emitter: 2,
        emitMode: 1,
        windX: 30,
        windY: -12,
        drag: 0.8,
        turbulence: 55,
        turbulenceScale: 210,
        turbulenceSpeed: 2.5,
        sizeOverLife: 3,
        sizeRandom: 0.2,
        opacityOverLife: 1,
        opacityRandom: 0.4,
        colorEnd: const Color(0xFF7C62FF),
        shape: 4,
        spin: 180,
        trail: 0.7,
        lifeRandom: 0.3,
        glow: 0.6,
      );
      final projeto = VideoProject(
        name: 'teste',
        createdAt: DateTime(2026, 1, 1),
        layers: [l],
      );
      final volta = projectFromJson(projectToJson(projeto));
      final r = volta.layers.single as ParticlesLayer;
      expect(r.emitter, 2);
      expect(r.emitMode, 1);
      expect(r.windX, 30);
      expect(r.windY, -12);
      expect(r.drag, 0.8);
      expect(r.turbulence, 55);
      expect(r.turbulenceScale, 210);
      expect(r.turbulenceSpeed, 2.5);
      expect(r.sizeOverLife, 3);
      expect(r.sizeRandom, 0.2);
      expect(r.opacityOverLife, 1);
      expect(r.opacityRandom, 0.4);
      expect(r.colorEnd, const Color(0xFF7C62FF));
      expect(r.shape, 4);
      expect(r.spin, 180);
      expect(r.trail, 0.7);
      expect(r.lifeRandom, 0.3);
      expect(r.glow, 0.6);

      // copyLayer (transform) nao perde os campos novos.
      final c = r.copyLayer(is3D: true);
      expect(c.shape, 4);
      expect(c.trail, 0.7);
      expect(c.colorEnd, const Color(0xFF7C62FF));
      // clearColorEnd limpa mesmo.
      expect(c.copyParticles(clearColorEnd: true).colorEnd, isNull);
    });
  });

  group('texto 3D e Liquid Glass', () {
    test('as propriedades 3D do animador existem e tem rotulo', () {
      for (final p in [
        TextAnimProp.rotationX,
        TextAnimProp.rotationY,
        TextAnimProp.positionZ,
      ]) {
        expect(textAnimPropLabel(p), isNotEmpty);
      }
      // Salvas por indice: as novas vem DEPOIS das antigas.
      expect(TextAnimProp.rotationX.index,
          greaterThan(TextAnimProp.brightness.index));
    });

    test('Liquid Glass tem spec com os parametros do vidro', () {
      final spec = effectSpecs[EffectType.liquidGlass]!;
      expect(spec.hasColor, isTrue);
      for (final k in [
        'blur',
        'refraction',
        'rim',
        'tint',
        'radius',
        'shadow',
        'padding'
      ]) {
        expect(spec.params.containsKey(k), isTrue, reason: k);
      }
    });
  });
}
