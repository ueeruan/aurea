import 'package:aurea/src/core/l10n/app_language.dart';
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
              title: const AppText('Falta sobra de midia'),
              content: const AppText('Esta transicao precisa de quadros alem do corte. '
                'Encurte a transicao ou congele as pontas.',
              ),
              actions: [
                CupertinoDialogAction(
                  onPressed: () => Navigator.pop(
                    dialogContext,
                    TransitionEdgeFallback.shorten,
                  ),
                  child: const AppText('Encurtar'),
                ),
                CupertinoDialogAction(
                  onPressed: () => Navigator.pop(
                    dialogContext,
                    TransitionEdgeFallback.freeze,
                  ),
                  child: const AppText('Congelar pontas'),
                ),
                CupertinoDialogAction(
                  isDestructiveAction: true,
                  onPressed: () => Navigator.pop(dialogContext),
                  child: const AppText('Cancelar'),
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
                        child: AppText('Efeito da transicao',
                          style: TextStyle(
                            fontSize: 17,
                            fontWeight: FontWeight.w700,
                            color: AmColors.text,
                          ),
                        ),
                      ),
                      for (final type in effectSpecs.keys)
                        if (type != EffectType.echo &&
                            type != EffectType.forceMotionBlur)
                          ListTile(
                            dense: true,
                            title: AppText(
                              effectSpecs[type]!.name,
                              style: const TextStyle(color: AmColors.text),
                            ),
                            subtitle: AppText(
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
                const AppText(
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
                const SizedBox(height: 16),
                const AppText('Distorcao e revelacao',
                  style: TextStyle(color: AmColors.muted, fontSize: 12),
                ),
                const SizedBox(height: 8),
                Wrap(
                  spacing: 6,
                  runSpacing: 6,
                  children: [
                    // A LISTA E DERIVADA DO CATALOGO, e nao escrita a mao.
                    //
                    // Ela era uma lista fixa de oito tipos (twirl, fisheye,
                    // kaleidoscope, waveWarp, venetianBlinds, blockDissolve,
                    // offset, invert) e lia `effectSpecs[type]!.name` para
                    // montar o rotulo. Quando o catalogo foi cortado, os oito
                    // sairam — e o `!` derrubava a folha inteira ao ABRIR:
                    // transicao nenhuma podia ser aplicada.
                    //
                    // Aqui nao ha como errar: se o tipo tem ficha, aparece.
                    for (final tipo in _tiposDeTransicao)
                      _ChoiceChip(
                        label: effectSpecs[tipo]!.name,
                        selected:
                            transition?.type == ClipTransitionType.effect &&
                            transition?.effect?.type == tipo,
                        onTap: () =>
                            apply(ClipTransitionType.effect, effectType: tipo),
                      ),
                  ],
                ),
                if (transition != null) ...[
                  const SizedBox(height: 18),
                  Row(
                    children: [
                      const SizedBox(
                        width: 72,
                        child: AppText(
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
                        child: AppText(
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
                  const AppText('Alinhamento',
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
                  const AppText('Curva',
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
                          child: AppText('Cruzar audio junto',
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
                    AppText(
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
                    child: const AppText('Remover transicao',
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

/// Os efeitos oferecidos como TRANSICAO entre dois clipes.
///
/// Sao os que deformam a imagem inteira — a lista e filtrada contra o
/// catalogo na hora de montar os chips, entao tipo que sair do catalogo
/// amanha simplesmente deixa de aparecer, em vez de derrubar a folha.
const _candidatosDeTransicao = <EffectType>[
  EffectType.turbulentDisplace,
  EffectType.ccLens,
  EffectType.opticsCompensation,
  EffectType.glitchify,
  EffectType.crossGlitch,
  EffectType.twitch,
  EffectType.dissolveShake,
  EffectType.motionTile,
  EffectType.tremor,
  EffectType.pixelSort,
];

/// So os que existem de fato no catalogo de agora.
List<EffectType> get _tiposDeTransicao =>
    [for (final t in _candidatosDeTransicao) if (effectSpecs.containsKey(t)) t];

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
      child: AppText(
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
        child: AppText(
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
        child: AppText(
          value.toStringAsFixed(2),
          textAlign: TextAlign.right,
          style: const TextStyle(fontSize: 10, color: AmColors.text),
        ),
      ),
    ],
  );
}
