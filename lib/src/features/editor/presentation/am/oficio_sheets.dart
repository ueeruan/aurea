import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/tocavel.dart';
import '../../application/editor_controller.dart';
import '../../domain/keyframe.dart';
import '../../domain/layer_meta.dart';
import 'am_colors.dart';
import 'am_widgets.dart';

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
                  child: AppText(
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
                  child: AppText(hint,
                    style: const TextStyle(fontSize: 10, color: AmColors.muted),
                  ),
                ),
              ],
            ),
          );
        }

        return SafeArea(
          child: SingleChildScrollView(
            padding: EdgeInsets.fromLTRB(
              18,
              12,
              18,
              64 + MediaQuery.viewInsetsOf(sheetContext).bottom,
            ),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                AppText(
                  'Organizar — ${layer.name}',
                  style: const TextStyle(
                    fontSize: 16,
                    fontWeight: FontWeight.w700,
                    color: AmColors.text,
                  ),
                ),
                const SizedBox(height: 10),
                const AppText('Rotulo',
                  style: TextStyle(fontSize: 12, color: AmColors.muted),
                ),
                const SizedBox(height: 6),
                Row(
                  children: [
                    for (final l in LayerLabel.palette)
                      Tocavel(
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
                    Tocavel(
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
                  'nao anda, nao apara, nao edita nem apaga',
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
            padding: EdgeInsets.fromLTRB(
              18,
              12,
              18,
              64 + MediaQuery.viewInsetsOf(sheetContext).bottom,
            ),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const AppText('Loop de keyframes',
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
                      Tocavel(
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
                          child: AppText(
                            label,
                            style: TextStyle(
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
                  const AppText('Esta propriedade precisa de 2 ou mais keyframes '
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
                        Tocavel(
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
                            child: AppText(
                              label,
                              style: TextStyle(
                                fontSize: 12,
                                color: AmColors.accent,
                              ),
                            ),
                          ),
                        ),
                    ],
                  ),
                  const SizedBox(height: 8),
                  const AppText('Ciclo repete do inicio · Vai-e-volta alterna a '
                    'direcao · Deslocado soma o percurso a cada volta '
                    '(esteira) · Continuar mantem a velocidade final.',
                    style: TextStyle(fontSize: 11, color: AmColors.muted),
                  ),
                  const SizedBox(height: 10),
                  Tocavel(
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
                      child: AppText('Inverter no tempo',
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
