import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../domain/cut.dart';
import '../../domain/effect.dart';
import '../../domain/keyframe.dart';
import 'am_colors.dart';
import 'am_widgets.dart';

Future<void> showTransitionSheet(
  BuildContext context,
  WidgetRef ref,
  String outgoingId,
) async {
  await showParamSheet(
    context,
    title: 'Transicao',
    heightFactor: 0.72,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        final controller = ref.read(editorControllerProvider.notifier);
        final transition = controller.transitionAfter(outgoingId);

        Future<void> apply(
          ClipTransitionType type, {
          EffectType? effectType,
        }) async {
          final current = controller.transitionAfter(outgoingId);
          final duration =
              current?.duration ?? const Duration(milliseconds: 300);
          final alignment = current?.alignment ?? TransitionAlignment.center;
          final ok = controller.applyTransition(
            outgoingId,
            type,
            duration: duration,
            alignment: alignment,
            curve: current?.curve ?? Easing.easeInOut,
            effectType: effectType,
          );
          if (ok) {
            setSheetState(() {});
            return;
          }
          if (!sheetContext.mounted) return;
          final fallback = await showCupertinoDialog<TransitionEdgeFallback>(
            context: sheetContext,
            builder: (dialogContext) => CupertinoAlertDialog(
              title: const Text('Falta sobra de midia'),
              content: const Text(
                'Esta transicao precisa de quadros alem do corte. '
                'Encurte a transicao ou congele as pontas.',
              ),
              actions: [
                CupertinoDialogAction(
                  onPressed: () => Navigator.pop(
                    dialogContext,
                    TransitionEdgeFallback.shorten,
                  ),
                  child: const Text('Encurtar'),
                ),
                CupertinoDialogAction(
                  onPressed: () => Navigator.pop(
                    dialogContext,
                    TransitionEdgeFallback.freeze,
                  ),
                  child: const Text('Congelar pontas'),
                ),
                CupertinoDialogAction(
                  isDestructiveAction: true,
                  onPressed: () => Navigator.pop(dialogContext),
                  child: const Text('Cancelar'),
                ),
              ],
            ),
          );
          if (fallback == null) return;
          controller.applyTransition(
            outgoingId,
            type,
            duration: duration,
            alignment: alignment,
            curve: current?.curve ?? Easing.easeInOut,
            fallback: fallback,
            effectType: effectType,
          );
          setSheetState(() {});
        }

        Future<void> chooseEffect() async {
          final effect = await showCupertinoModalPopup<EffectType>(
            context: sheetContext,
            builder: (popupContext) => Material(
              color: AmColors.panel,
              child: SafeArea(
                top: false,
                child: SizedBox(
                  height: MediaQuery.of(popupContext).size.height * 0.62,
                  child: ListView(
                    children: [
                      const Padding(
                        padding: EdgeInsets.fromLTRB(18, 16, 18, 8),
                        child: Text(
                          'Efeito da transicao',
                          style: TextStyle(
                            fontSize: 17,
                            fontWeight: FontWeight.w700,
                            color: AmColors.text,
                          ),
                        ),
                      ),
                      for (final type in effectSpecs.keys)
                        if (type != EffectType.timeRemap &&
                            type != EffectType.echo &&
                            type != EffectType.forceMotionBlur)
                          ListTile(
                            dense: true,
                            title: Text(
                              effectSpecs[type]!.name,
                              style: const TextStyle(color: AmColors.text),
                            ),
                            subtitle: Text(
                              effectSpecs[type]!.category,
                              style: const TextStyle(color: AmColors.muted),
                            ),
                            onTap: () => Navigator.pop(popupContext, type),
                          ),
                    ],
                  ),
                ),
              ),
            ),
          );
          if (effect != null) {
            await apply(ClipTransitionType.effect, effectType: effect);
          }
        }

        return SafeArea(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(18, 14, 18, 24),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text(
                  'Pronto',
                  style: TextStyle(
                    fontSize: 12,
                    fontWeight: FontWeight.w700,
                    color: AmColors.muted,
                  ),
                ),
                const SizedBox(height: 8),
                Wrap(
                  spacing: 6,
                  runSpacing: 6,
                  children: [
                    for (final type in ClipTransitionType.values)
                      if (type != ClipTransitionType.effect)
                        _ChoiceChip(
                          label: type.label,
                          selected: transition?.type == type,
                          onTap: () => apply(type),
                        ),
                    _ChoiceChip(
                      label: transition?.type == ClipTransitionType.effect
                          ? transition!.effect?.spec.name ?? 'Com efeito'
                          : 'Com efeito',
                      selected: transition?.type == ClipTransitionType.effect,
                      onTap: chooseEffect,
                    ),
                  ],
                ),
                if (transition != null) ...[
                  const SizedBox(height: 18),
                  Row(
                    children: [
                      const SizedBox(
                        width: 72,
                        child: Text(
                          'Duracao',
                          style: TextStyle(fontSize: 12, color: AmColors.muted),
                        ),
                      ),
                      Expanded(
                        child: AmTickRuler(
                          value: transition.duration.inMilliseconds / 1000,
                          min: 0,
                          max: 2.5,
                          unitsPerPixel: 0.01,
                          height: 42,
                          onChanged: (v) {
                            controller.setTransitionDuration(
                              outgoingId,
                              Duration(milliseconds: (v * 1000).round()),
                            );
                            setSheetState(() {});
                          },
                        ),
                      ),
                      SizedBox(
                        width: 58,
                        child: Text(
                          '${transition.duration.inMilliseconds} ms',
                          textAlign: TextAlign.right,
                          style: const TextStyle(
                            fontSize: 11,
                            color: AmColors.text,
                          ),
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 12),
                  const Text(
                    'Alinhamento',
                    style: TextStyle(fontSize: 11, color: AmColors.muted),
                  ),
                  const SizedBox(height: 6),
                  Wrap(
                    spacing: 6,
                    children: [
                      for (final alignment in TransitionAlignment.values)
                        _ChoiceChip(
                          label: alignment.label,
                          selected: transition.alignment == alignment,
                          onTap: () {
                            controller.setTransitionAlignment(
                              outgoingId,
                              alignment,
                            );
                            setSheetState(() {});
                          },
                        ),
                    ],
                  ),
                  const SizedBox(height: 12),
                  const Text(
                    'Curva',
                    style: TextStyle(fontSize: 11, color: AmColors.muted),
                  ),
                  const SizedBox(height: 6),
                  Wrap(
                    spacing: 6,
                    runSpacing: 6,
                    children: [
                      for (final item in const <(String, Easing)>[
                        ('Linear', Easing.linear),
                        ('Suave', Easing.easeInOut),
                        ('Entrada', Easing.easeIn),
                        ('Saida', Easing.easeOut),
                        ('Overshoot', Easing.overshoot),
                      ])
                        _ChoiceChip(
                          label: item.$1,
                          selected: transition.curve == item.$2,
                          onTap: () {
                            controller.setTransitionCurve(outgoingId, item.$2);
                            setSheetState(() {});
                          },
                        ),
                    ],
                  ),
                  if (transition.type == ClipTransitionType.dissolve) ...[
                    const SizedBox(height: 12),
                    Row(
                      children: [
                        const Expanded(
                          child: Text(
                            'Cruzar audio junto',
                            style: TextStyle(
                              fontSize: 13,
                              color: AmColors.text,
                            ),
                          ),
                        ),
                        CupertinoSwitch(
                          value: transition.crossfadeAudio,
                          activeTrackColor: AmColors.accent,
                          onChanged: (value) {
                            controller.setTransitionAudioCrossfade(
                              outgoingId,
                              value,
                            );
                            setSheetState(() {});
                          },
                        ),
                      ],
                    ),
                  ],
                  if (transition.type == ClipTransitionType.effect &&
                      transition.effect != null) ...[
                    const SizedBox(height: 16),
                    Text(
                      'Avancado · ${transition.effect!.spec.name}',
                      style: const TextStyle(
                        fontSize: 12,
                        fontWeight: FontWeight.w700,
                        color: AmColors.accent,
                      ),
                    ),
                    const SizedBox(height: 8),
                    for (final entry in transition.effect!.spec.params.entries)
                      _EffectParamRow(
                        name: entry.value.label,
                        value: transition.effect!.track(entry.key).base,
                        min: entry.value.min,
                        max: entry.value.max,
                        onChanged: (value) {
                          controller.setTransitionEffectParam(
                            outgoingId,
                            entry.key,
                            value,
                          );
                          setSheetState(() {});
                        },
                      ),
                  ],
                  const SizedBox(height: 14),
                  CupertinoButton(
                    padding: EdgeInsets.zero,
                    onPressed: () {
                      controller.removeTransition(outgoingId);
                      setSheetState(() {});
                    },
                    child: const Text(
                      'Remover transicao',
                      style: TextStyle(color: CupertinoColors.systemRed),
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

class _ChoiceChip extends StatelessWidget {
  const _ChoiceChip({
    required this.label,
    required this.selected,
    required this.onTap,
  });

  final String label;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => GestureDetector(
    onTap: onTap,
    child: Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: selected ? AmColors.accentDim : AmColors.chip,
        borderRadius: BorderRadius.circular(9),
      ),
      child: Text(
        label,
        style: TextStyle(
          fontSize: 12,
          color: selected ? AmColors.accent : AmColors.text,
        ),
      ),
    ),
  );
}

class _EffectParamRow extends StatelessWidget {
  const _EffectParamRow({
    required this.name,
    required this.value,
    required this.min,
    required this.max,
    required this.onChanged,
  });

  final String name;
  final double value;
  final double min;
  final double max;
  final ValueChanged<double> onChanged;

  @override
  Widget build(BuildContext context) => Row(
    children: [
      SizedBox(
        width: 108,
        child: Text(
          name,
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
          style: const TextStyle(fontSize: 11, color: AmColors.muted),
        ),
      ),
      Expanded(
        // Trackpad, nao slider: alvo grande, marcas dando precisao e o
        // mesmo controle de todo numero do aplicativo.
        child: AmTickRuler(
          value: value.clamp(min, max),
          min: min,
          max: max,
          unitsPerPixel: (max - min) / 420,
          height: 40,
          onChanged: onChanged,
        ),
      ),
      SizedBox(
        width: 48,
        child: Text(
          value.toStringAsFixed(2),
          textAlign: TextAlign.right,
          style: const TextStyle(fontSize: 10, color: AmColors.text),
        ),
      ),
    ],
  );
}
