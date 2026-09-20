import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/audio_render_service.dart';
import '../../domain/audio_effect.dart';
import 'am_colors.dart';
import 'am_widgets.dart';
import 'package:aurea/src/core/l10n/app_language.dart';

class AudioEffectsPanel extends ConsumerStatefulWidget {
  const AudioEffectsPanel({super.key, required this.layerId});
  final String layerId;
  @override
  ConsumerState<AudioEffectsPanel> createState() => _AudioEffectsPanelState();
}

class _AudioEffectsPanelState extends ConsumerState<AudioEffectsPanel> {
  int? open;
  void update(List<AudioEffect> effects) {
    ref
        .read(editorControllerProvider.notifier)
        .updateAudioSpec(
          widget.layerId,
          (a) =>
              a.copyWith(processing: a.processing.copyWith(effects: effects)),
        );
  }

  @override
  Widget build(BuildContext context) {
    final project = ref.watch(editorControllerProvider);
    final layer = project.layerById(widget.layerId);
    if (layer == null) return const SizedBox.shrink();
    final effects =
        AudioRenderService.spec(layer)?.processing.effects ??
        const <AudioEffect>[];
    return ColoredBox(
      color: AmColors.panel,
      child: ListView(
        padding: const EdgeInsets.all(12),
        children: [
          ValueListenableBuilder(
            valueListenable: AudioRenderService.instance.revision,
            builder: (context, _, child) {
              final service = AudioRenderService.instance;
              final status =
                  service.error(layer) ??
                  (service.busy(layer)
                      ? 'Preparando som...'
                      : service.ready(layer) != null
                      ? 'Som atualizado'
                      : 'Efeitos de audio');
              return Padding(
                padding: const EdgeInsets.only(bottom: 8),
                child: AppText(
                  status,
                  style: const TextStyle(color: AmColors.muted),
                ),
              );
            },
          ),
          for (var i = 0; i < effects.length; i++)
            Card(
              color: AmColors.panelHigh,
              child: Column(
                children: [
                  ListTile(
                    dense: true,
                    title: AppText(
                      effects[i].spec.name,
                      style: const TextStyle(color: AmColors.text),
                    ),
                    onTap: () => setState(() => open = open == i ? null : i),
                    leading: IconButton(
                      tooltip: 'Ativar/desativar efeito de audio',
                      icon: Icon(
                        effects[i].enabled ? Icons.volume_up : Icons.volume_off,
                      ),
                      onPressed: () {
                        final next = [...effects];
                        next[i] = next[i].toggle();
                        update(next);
                      },
                    ),
                    trailing: Icon(
                      open == i ? Icons.expand_less : Icons.expand_more,
                    ),
                  ),
                  if (open == i) ...[
                    Row(
                      mainAxisAlignment: MainAxisAlignment.end,
                      children: [
                        IconButton(
                          tooltip: 'Mover efeito para cima',
                          onPressed: i == 0
                              ? null
                              : () {
                                  final next = [...effects];
                                  next.insert(i - 1, next.removeAt(i));
                                  update(next);
                                  setState(() => open = i - 1);
                                },
                          icon: const Icon(Icons.arrow_upward),
                        ),
                        IconButton(
                          tooltip: 'Mover efeito para baixo',
                          onPressed: i == effects.length - 1
                              ? null
                              : () {
                                  final next = [...effects];
                                  next.insert(i + 1, next.removeAt(i));
                                  update(next);
                                  setState(() => open = i + 1);
                                },
                          icon: const Icon(Icons.arrow_downward),
                        ),
                        IconButton(
                          tooltip: 'Resetar efeito de audio',
                          onPressed: () {
                            final next = [...effects];
                            next[i] = AudioEffect(next[i].type);
                            update(next);
                          },
                          icon: const Icon(Icons.restore),
                        ),
                        IconButton(
                          tooltip: 'Remover efeito de audio',
                          onPressed: () {
                            update([...effects]..removeAt(i));
                            setState(() => open = null);
                          },
                          icon: const Icon(Icons.delete_outline),
                        ),
                      ],
                    ),
                    for (final entry in effects[i].spec.params.entries)
                      Padding(
                        padding: const EdgeInsets.symmetric(
                          horizontal: 12,
                          vertical: 4,
                        ),
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            AppText(entry.value.label,
                              style: const TextStyle(color: AmColors.text),
                            ),
                            if (entry.value.options.isNotEmpty)
                              DropdownButton<double>(
                                isExpanded: true,
                                value: effects[i]
                                    .value(entry.key)
                                    .roundToDouble(),
                                dropdownColor: AmColors.panelHigh,
                                items: [
                                  for (
                                    var j = 0;
                                    j < entry.value.options.length;
                                    j++
                                  )
                                    DropdownMenuItem(
                                      value: j.toDouble(),
                                      child: AppText(
                                        entry.value.options[j],
                                        style: const TextStyle(
                                          color: AmColors.text,
                                        ),
                                      ),
                                    ),
                                ],
                                onChanged: (v) {
                                  if (v == null) return;
                                  final next = [...effects];
                                  next[i] = next[i].edit(entry.key, v);
                                  update(next);
                                },
                              )
                            else
                              Row(
                                children: [
                                  Expanded(
                                    child: AmTickRuler(
                                      value: effects[i].value(entry.key),
                                      min: entry.value.min,
                                      max: entry.value.max,
                                      unitsPerPixel:
                                          (entry.value.max - entry.value.min) /
                                          400,
                                      height: 58,
                                      onChanged: (v) {
                                        final next = [...effects];
                                        next[i] = next[i].edit(entry.key, v);
                                        update(next);
                                      },
                                    ),
                                  ),
                                  SizedBox(
                                    width: 62,
                                    child: AppText(
                                      effects[i]
                                          .value(entry.key)
                                          .toStringAsFixed(2),
                                      style: TextStyle(
                                        color: AmColors.accent,
                                      ),
                                    ),
                                  ),
                                ],
                              ),
                          ],
                        ),
                      ),
                  ],
                ],
              ),
            ),
          TextButton.icon(
            key: const ValueKey('adicionar-efeito-audio'),
            icon: const Icon(Icons.add),
            label: const AppText('Adicionar efeito de audio'),
            onPressed: () async {
              final type = await showModalBottomSheet<AudioEffectType>(
                context: context,
                backgroundColor: AmColors.panel,
                builder: (context) => SafeArea(
                  child: ListView(
                    children: [
                      for (final entry in audioEffectSpecs.entries)
                        ListTile(
                          title: AppText(
                            entry.value.name,
                            style: const TextStyle(color: AmColors.text),
                          ),
                          onTap: () => Navigator.pop(context, entry.key),
                        ),
                    ],
                  ),
                ),
              );
              if (type == null || !mounted) return;
              final current =
                  ref
                      .read(editorControllerProvider.notifier)
                      .audioSpecOf(widget.layerId)
                      ?.processing
                      .effects ??
                  const <AudioEffect>[];
              update([...current, AudioEffect(type)]);
              setState(() => open = current.length);
            },
          ),
        ],
      ),
    );
  }
}
