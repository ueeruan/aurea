import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/keyframe.dart';
import '../../domain/layer_meta.dart';
import 'am_colors.dart';
import 'am_widgets.dart';

enum _ShadowDepth { pronto, montar, avancado }

/// ORGANIZACAO (PR-X26): rotulo colorido, solo, timida, bloqueio.
Future<void> showOrganizeSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) async {
  final controller = ref.read(editorControllerProvider.notifier);

  await showParamSheet(
    context,
    heightFactor: 0.4,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        final project = ref.read(editorControllerProvider);
        final meta = project.metaOf(layerId);
        final layer = project.layerById(layerId);
        if (layer == null) return const SizedBox.shrink();

        Widget toggle(
          String label,
          bool value,
          VoidCallback onTap,
          String hint,
        ) {
          return Padding(
            padding: const EdgeInsets.only(bottom: 4),
            child: Row(
              children: [
                SizedBox(
                  width: 92,
                  child: Text(
                    label,
                    style: const TextStyle(fontSize: 13, color: AmColors.muted),
                  ),
                ),
                Transform.scale(
                  scale: 0.72,
                  child: CupertinoSwitch(
                    value: value,
                    activeTrackColor: AmColors.accent,
                    onChanged: (_) {
                      onTap();
                      setSheetState(() {});
                    },
                  ),
                ),
                Expanded(
                  child: Text(
                    hint,
                    style: const TextStyle(fontSize: 10, color: AmColors.muted),
                  ),
                ),
              ],
            ),
          );
        }

        return SafeArea(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(18, 12, 18, 14),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  'Organizar — ${layer.name}',
                  style: const TextStyle(
                    fontSize: 16,
                    fontWeight: FontWeight.w700,
                    color: AmColors.text,
                  ),
                ),
                const SizedBox(height: 10),
                const Text(
                  'Rotulo',
                  style: TextStyle(fontSize: 12, color: AmColors.muted),
                ),
                const SizedBox(height: 6),
                Row(
                  children: [
                    for (final l in LayerLabel.palette)
                      GestureDetector(
                        onTap: () {
                          controller.setLayerLabel(layerId, l);
                          setSheetState(() {});
                        },
                        child: Container(
                          width: 30,
                          height: 30,
                          margin: const EdgeInsets.only(right: 8),
                          decoration: BoxDecoration(
                            color: l.color,
                            shape: BoxShape.circle,
                            border: meta.label?.color == l.color
                                ? Border.all(color: Colors.white, width: 2.5)
                                : null,
                          ),
                        ),
                      ),
                    GestureDetector(
                      onTap: () {
                        controller.setLayerLabel(layerId, null);
                        setSheetState(() {});
                      },
                      child: const Icon(
                        CupertinoIcons.clear_circled,
                        size: 22,
                        color: AmColors.muted,
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 12),
                toggle(
                  'Solo',
                  meta.solo,
                  () => controller.toggleSolo(layerId),
                  'so as camadas em solo aparecem',
                ),
                toggle(
                  'Timida',
                  meta.shy,
                  () => controller.toggleShy(layerId),
                  'some da timeline, continua no render',
                ),
                toggle(
                  'Bloquear',
                  meta.locked,
                  () => controller.toggleLocked(layerId),
                  'nao aceita edicao no palco',
                ),
                toggle(
                  'Motion blur',
                  meta.motionBlur,
                  () => controller.toggleLayerMotionBlur(layerId),
                  'borrao de movimento nesta camada',
                ),
              ],
            ),
          ),
        );
      },
    ),
  );
}

/// ESTILOS DE CAMADA (PR-X10): aplicam depois do transform e acompanham
/// a forma da camada.
Future<void> showLayerStylesSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  PlaybackController playback,
) async {
  final controller = ref.read(editorControllerProvider.notifier);
  var shadowDepth = _ShadowDepth.pronto;

  await showParamSheet(
    context,
    heightFactor: 0.5,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        final s = ref.read(editorControllerProvider).metaOf(layerId).styles;

        Widget row(
          String label,
          bool on,
          VoidCallback toggle,
          List<Widget> controls,
        ) {
          return Padding(
            padding: const EdgeInsets.only(bottom: 8),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Transform.scale(
                      scale: 0.72,
                      child: CupertinoSwitch(
                        value: on,
                        activeTrackColor: AmColors.accent,
                        onChanged: (_) {
                          toggle();
                          setSheetState(() {});
                        },
                      ),
                    ),
                    Text(
                      label,
                      style: const TextStyle(
                        fontSize: 13,
                        color: AmColors.text,
                      ),
                    ),
                  ],
                ),
                if (on) ...controls,
              ],
            ),
          );
        }

        Widget slider(
          String label,
          double value,
          double min,
          double max,
          ValueChanged<double> onChanged,
        ) {
          return Padding(
            padding: const EdgeInsets.only(left: 12, bottom: 4),
            child: Row(
              children: [
                SizedBox(
                  width: 74,
                  child: Text(
                    label,
                    style: const TextStyle(fontSize: 11, color: AmColors.muted),
                  ),
                ),
                Expanded(
                  child: AmTickRuler(
                    value: value,
                    min: min,
                    max: max,
                    unitsPerPixel: (max - min) / 380,
                    height: 36,
                    onChanged: (v) {
                      onChanged(v);
                      setSheetState(() {});
                    },
                  ),
                ),
                SizedBox(
                  width: 44,
                  child: Text(
                    amNumber(value, 0),
                    textAlign: TextAlign.center,
                    style: const TextStyle(
                      fontSize: 12,
                      color: AmColors.accent,
                    ),
                  ),
                ),
              ],
            ),
          );
        }

        final ds = s.dropShadow;
        final inner = s.innerShadow;
        final og = s.outerGlow;
        final st = s.stroke;
        final co = s.colorOverlay;

        return SafeArea(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(18, 12, 18, 14),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text(
                  'Estilos de camada',
                  style: TextStyle(
                    fontSize: 16,
                    fontWeight: FontWeight.w700,
                    color: AmColors.text,
                  ),
                ),
                const SizedBox(height: 8),
                Row(
                  children: [
                    for (final depth in _ShadowDepth.values)
                      Expanded(
                        child: GestureDetector(
                          onTap: () => setSheetState(() => shadowDepth = depth),
                          child: Container(
                            height: 34,
                            alignment: Alignment.center,
                            margin: const EdgeInsets.only(right: 6),
                            decoration: BoxDecoration(
                              color: shadowDepth == depth
                                  ? AmColors.accent
                                  : AmColors.bg,
                              borderRadius: BorderRadius.circular(8),
                            ),
                            child: Text(
                              switch (depth) {
                                _ShadowDepth.pronto => 'Pronto',
                                _ShadowDepth.montar => 'Montar',
                                _ShadowDepth.avancado => 'Avancado',
                              },
                              style: TextStyle(
                                fontSize: 11,
                                fontWeight: FontWeight.w700,
                                color: shadowDepth == depth
                                    ? AmColors.bg
                                    : AmColors.muted,
                              ),
                            ),
                          ),
                        ),
                      ),
                  ],
                ),
                const SizedBox(height: 10),
                if (shadowDepth == _ShadowDepth.pronto)
                  SizedBox(
                    width: double.infinity,
                    child: CupertinoButton(
                      color: AmColors.panelHigh,
                      onPressed: () {
                        controller.updateLayerStyles(
                          layerId,
                          (v) => v.copyWith(
                            dropShadow: ShadowStyle(
                              opacity: AnimatedDouble(0.12),
                              angleDeg: AnimatedDouble(270),
                              distance: AnimatedDouble(18),
                              size: AnimatedDouble(48),
                            ),
                          ),
                        );
                        setSheetState(() {});
                      },
                      child: const Text(
                        'Sombra suave',
                        style: TextStyle(color: AmColors.text),
                      ),
                    ),
                  )
                else ...[
                  row(
                    'Sombra projetada',
                    ds != null,
                    () => controller.updateLayerStyles(
                      layerId,
                      (v) => ds == null
                          ? v.copyWith(dropShadow: ShadowStyle())
                          : v.copyWith(clearDropShadow: true),
                    ),
                    [
                      if (ds != null) ...[
                        slider(
                          'Distancia',
                          ds.distance.base,
                          0,
                          120,
                          (v) => controller.updateLayerStyles(
                            layerId,
                            (x) => x.copyWith(
                              dropShadow: ds.copyWith(
                                distance: AnimatedDouble(v),
                              ),
                            ),
                          ),
                        ),
                        slider(
                          'Tamanho',
                          ds.size.base,
                          0,
                          120,
                          (v) => controller.updateLayerStyles(
                            layerId,
                            (x) => x.copyWith(
                              dropShadow: ds.copyWith(size: AnimatedDouble(v)),
                            ),
                          ),
                        ),
                        slider(
                          'Opacidade',
                          ds.opacity.base * 100,
                          0,
                          100,
                          (v) => controller.updateLayerStyles(
                            layerId,
                            (x) => x.copyWith(
                              dropShadow: ds.copyWith(
                                opacity: AnimatedDouble(v / 100),
                              ),
                            ),
                          ),
                        ),
                        if (shadowDepth == _ShadowDepth.avancado) ...[
                          slider(
                            'Angulo',
                            ds.angleDeg.base,
                            0,
                            360,
                            (v) => controller.updateLayerStyles(
                              layerId,
                              (x) => x.copyWith(
                                dropShadow: ds.copyWith(
                                  angleDeg: AnimatedDouble(v),
                                ),
                              ),
                            ),
                          ),
                          slider(
                            'Espalhar',
                            ds.spread.base,
                            0,
                            100,
                            (v) => controller.updateLayerStyles(
                              layerId,
                              (x) => x.copyWith(
                                dropShadow: ds.copyWith(
                                  spread: AnimatedDouble(v),
                                ),
                              ),
                            ),
                          ),
                          Padding(
                            padding: const EdgeInsets.only(left: 12, bottom: 8),
                            child: Row(
                              children: [
                                const SizedBox(
                                  width: 74,
                                  child: Text(
                                    'Cor',
                                    style: TextStyle(
                                      fontSize: 11,
                                      color: AmColors.muted,
                                    ),
                                  ),
                                ),
                                for (final c in const [
                                  Color(0xFF000000),
                                  Color(0xFF0A1E3C),
                                  Color(0xFF392A68),
                                  Color(0xFFFFFFFF),
                                ])
                                  GestureDetector(
                                    onTap: () {
                                      controller.updateLayerStyles(
                                        layerId,
                                        (x) => x.copyWith(
                                          dropShadow: ds.copyWith(color: c),
                                        ),
                                      );
                                      setSheetState(() {});
                                    },
                                    child: Container(
                                      width: 26,
                                      height: 26,
                                      margin: const EdgeInsets.only(right: 8),
                                      decoration: BoxDecoration(
                                        color: c,
                                        shape: BoxShape.circle,
                                        border: Border.all(
                                          color: ds.color == c
                                              ? AmColors.accent
                                              : AmColors.hairline,
                                          width: ds.color == c ? 2 : 1,
                                        ),
                                      ),
                                    ),
                                  ),
                              ],
                            ),
                          ),
                        ],
                      ],
                    ],
                  ),
                  if (shadowDepth == _ShadowDepth.avancado)
                    row(
                      'Sombra interna',
                      inner != null,
                      () => controller.updateLayerStyles(
                        layerId,
                        (v) => inner == null
                            ? v.copyWith(
                                innerShadow: ShadowStyle(
                                  opacity: AnimatedDouble(0.16),
                                  angleDeg: AnimatedDouble(270),
                                  distance: AnimatedDouble(4),
                                  size: AnimatedDouble(12),
                                ),
                              )
                            : v.copyWith(clearInnerShadow: true),
                      ),
                      const [],
                    ),
                ],
                const SizedBox(height: 10),
                row(
                  'Brilho externo',
                  og != null,
                  () => controller.updateLayerStyles(
                    layerId,
                    (v) => og == null
                        ? v.copyWith(outerGlow: GlowStyle())
                        : v.copyWith(clearOuterGlow: true),
                  ),
                  [
                    if (og != null)
                      slider(
                        'Tamanho',
                        og.size.base,
                        0,
                        120,
                        (v) => controller.updateLayerStyles(
                          layerId,
                          (x) => x.copyWith(
                            outerGlow: og.copyWith(size: AnimatedDouble(v)),
                          ),
                        ),
                      ),
                  ],
                ),
                row(
                  'Contorno',
                  st != null,
                  () => controller.updateLayerStyles(
                    layerId,
                    (v) => st == null
                        ? v.copyWith(stroke: StrokeStyle())
                        : v.copyWith(clearStroke: true),
                  ),
                  [
                    if (st != null)
                      slider(
                        'Espessura',
                        st.width.base,
                        0,
                        40,
                        (v) => controller.updateLayerStyles(
                          layerId,
                          (x) => x.copyWith(
                            stroke: st.copyWith(width: AnimatedDouble(v)),
                          ),
                        ),
                      ),
                  ],
                ),
                row(
                  'Sobreposicao de cor',
                  co != null,
                  () => controller.updateLayerStyles(
                    layerId,
                    (v) => co == null
                        ? v.copyWith(colorOverlay: OverlayStyle())
                        : v.copyWith(clearColorOverlay: true),
                  ),
                  [
                    if (co != null)
                      Padding(
                        padding: const EdgeInsets.only(left: 12),
                        child: Row(
                          children: [
                            for (final c in const [
                              Color(0xFFB8FF3D),
                              Color(0xFF7C62FF),
                              Color(0xFFFF3B52),
                              Color(0xFFFFFFFF),
                              Color(0xFF000000),
                            ])
                              GestureDetector(
                                onTap: () {
                                  controller.updateLayerStyles(
                                    layerId,
                                    (x) => x.copyWith(
                                      colorOverlay: co.copyWith(color: c),
                                    ),
                                  );
                                  setSheetState(() {});
                                },
                                child: Container(
                                  width: 26,
                                  height: 26,
                                  margin: const EdgeInsets.only(right: 8),
                                  decoration: BoxDecoration(
                                    color: c,
                                    shape: BoxShape.circle,
                                    border: co.color == c
                                        ? Border.all(
                                            color: Colors.white,
                                            width: 2,
                                          )
                                        : Border.all(color: AmColors.hairline),
                                  ),
                                ),
                              ),
                          ],
                        ),
                      ),
                  ],
                ),
              ],
            ),
          ),
        );
      },
    ),
  );
}

/// LOOP DE KEYFRAMES (PR-X6): dois keyframes e um Ciclo ja sao uma
/// animacao infinita, sem encher a timeline.
Future<void> showLoopSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) async {
  final controller = ref.read(editorControllerProvider.notifier);
  var prop = LayerProp.position;

  await showParamSheet(
    context,
    heightFactor: 0.42,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        final layer = ref.read(editorControllerProvider).layerById(layerId);
        if (layer == null) return const SizedBox.shrink();

        LoopSpec current() => switch (prop) {
          LayerProp.position => layer.position.loop,
          LayerProp.scale => layer.scaleX.loop,
          LayerProp.rotation => layer.rotation.loop,
          LayerProp.opacity => layer.opacity.loop,
          LayerProp.skew => layer.skewX.loop,
          LayerProp.pivot => layer.pivot.loop,
          LayerProp.parent => LoopSpec.none,
        };

        int keyframeCount() => switch (prop) {
          LayerProp.position => layer.position.keyframes.length,
          LayerProp.scale => layer.scaleX.keyframes.length,
          LayerProp.rotation => layer.rotation.keyframes.length,
          LayerProp.opacity => layer.opacity.keyframes.length,
          LayerProp.skew => layer.skewX.keyframes.length,
          LayerProp.pivot => layer.pivot.keyframes.length,
          LayerProp.parent => 0,
        };

        final spec = current();
        final n = keyframeCount();

        return SafeArea(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(18, 12, 18, 14),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text(
                  'Loop de keyframes',
                  style: TextStyle(
                    fontSize: 16,
                    fontWeight: FontWeight.w700,
                    color: AmColors.text,
                  ),
                ),
                const SizedBox(height: 8),
                Wrap(
                  spacing: 8,
                  runSpacing: 8,
                  children: [
                    for (final (label, value) in const [
                      ('Posicao', LayerProp.position),
                      ('Escala', LayerProp.scale),
                      ('Rotacao', LayerProp.rotation),
                      ('Opacidade', LayerProp.opacity),
                    ])
                      GestureDetector(
                        onTap: () => setSheetState(() => prop = value),
                        child: Container(
                          padding: const EdgeInsets.symmetric(
                            horizontal: 12,
                            vertical: 7,
                          ),
                          decoration: BoxDecoration(
                            color: prop == value
                                ? AmColors.accentDim
                                : AmColors.chip,
                            borderRadius: BorderRadius.circular(9),
                          ),
                          child: Text(
                            label,
                            style: const TextStyle(
                              fontSize: 12,
                              color: AmColors.accent,
                            ),
                          ),
                        ),
                      ),
                  ],
                ),
                const SizedBox(height: 10),
                if (n < 2)
                  const Text(
                    'Esta propriedade precisa de 2 ou mais keyframes '
                    'para ter loop.',
                    style: TextStyle(fontSize: 12, color: AmColors.muted),
                  )
                else ...[
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    children: [
                      for (final (label, mode) in const [
                        ('Sem loop', LoopMode.none),
                        ('Ciclo', LoopMode.cycle),
                        ('Vai-e-volta', LoopMode.pingPong),
                        ('Deslocado', LoopMode.offset),
                        ('Continuar', LoopMode.continueValue),
                      ])
                        GestureDetector(
                          onTap: () {
                            controller.setPropertyLoop(
                              layerId,
                              prop,
                              spec.copyWith(mode: mode),
                            );
                            setSheetState(() {});
                          },
                          child: Container(
                            padding: const EdgeInsets.symmetric(
                              horizontal: 12,
                              vertical: 8,
                            ),
                            decoration: BoxDecoration(
                              color: spec.mode == mode
                                  ? AmColors.accentDim
                                  : AmColors.chip,
                              borderRadius: BorderRadius.circular(9),
                            ),
                            child: Text(
                              label,
                              style: const TextStyle(
                                fontSize: 12,
                                color: AmColors.accent,
                              ),
                            ),
                          ),
                        ),
                    ],
                  ),
                  const SizedBox(height: 8),
                  const Text(
                    'Ciclo repete do inicio · Vai-e-volta alterna a '
                    'direcao · Deslocado soma o percurso a cada volta '
                    '(esteira) · Continuar mantem a velocidade final.',
                    style: TextStyle(fontSize: 11, color: AmColors.muted),
                  ),
                  const SizedBox(height: 10),
                  GestureDetector(
                    onTap: () {
                      controller.reversePropertyInTime(layerId, prop);
                      setSheetState(() {});
                    },
                    child: Container(
                      padding: const EdgeInsets.symmetric(
                        horizontal: 14,
                        vertical: 9,
                      ),
                      decoration: BoxDecoration(
                        color: AmColors.chip,
                        borderRadius: BorderRadius.circular(10),
                      ),
                      child: const Text(
                        'Inverter no tempo',
                        style: TextStyle(fontSize: 12, color: AmColors.accent),
                      ),
                    ),
                  ),
                ],
              ],
            ),
          ),
        );
      },
    ),
  );
}
