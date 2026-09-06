import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/snack.dart';
import '../../application/editor_controller.dart';
import '../../domain/audio_ops.dart';
import '../../domain/layer.dart';
import 'am_colors.dart';
import 'am_widgets.dart';

/// SOM da camada: fade, ganho, mudo, abaixar pela voz — e os dois
/// comandos que economizam mais tempo numa edicao falada, normalizar e
/// remover silencio.
Future<void> showAudioSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) async {
  await showParamSheet(
    context,
    title: 'Som',
    heightFactor: 0.52,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        final project = ref.read(editorControllerProvider);
        final controller = ref.read(editorControllerProvider.notifier);
        final layer = project.layerById(layerId);
        final spec = controller.audioSpecOf(layerId);
        if (layer == null || spec == null) {
          return const SizedBox.shrink();
        }

        final dur = layer.duration.inMilliseconds / 1000.0;
        final db = gainToDb(spec.gain);

        void edit(AudioSpec Function(AudioSpec) fn) {
          controller.updateAudioSpec(layerId, fn);
          setSheetState(() {});
        }

        // Candidatas a "voz que manda": qualquer outra faixa com som.
        final vozes = [
          for (final l in project.layers)
            if (l.id != layerId && (l is AudioLayer || l is VideoLayer)) l,
        ];

        return SafeArea(
          child: SingleChildScrollView(
            padding: EdgeInsets.fromLTRB(
              18,
              14,
              18,
              16 + MediaQuery.of(sheetContext).viewInsets.bottom,
            ),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    const Icon(
                      CupertinoIcons.speaker_2,
                      size: 18,
                      color: AmColors.accent,
                    ),
                    const SizedBox(width: 8),
                    const Text(
                      'Som',
                      style: TextStyle(
                        fontSize: 17,
                        fontWeight: FontWeight.w700,
                        color: AmColors.text,
                      ),
                    ),
                    const Spacer(),
                    Text(
                      spec.muted
                          ? 'mudo'
                          : '${db.isFinite ? db.toStringAsFixed(1) : '-∞'} dB',
                      style: const TextStyle(
                        fontSize: 13,
                        color: AmColors.accent,
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 10),

                _Toggle(
                  label: 'Mudo',
                  value: spec.muted,
                  onChanged: (v) => edit((a) => a.copyWith(muted: v)),
                ),
                _Slider(
                  label: 'Ganho',
                  value: spec.gain,
                  min: 0,
                  max: 4,
                  decimals: 2,
                  onChanged: (v) => edit((a) => a.copyWith(gain: v)),
                ),
                _Slider(
                  label: 'Fade de entrada',
                  value: spec.fadeIn.inMilliseconds / 1000.0,
                  min: 0,
                  max: dur.clamp(0.5, 10.0),
                  decimals: 2,
                  suffix: 's',
                  onChanged: (v) => edit(
                    (a) => a.copyWith(
                      fadeIn: Duration(milliseconds: (v * 1000).round()),
                    ),
                  ),
                ),
                _Slider(
                  label: 'Fade de saida',
                  value: spec.fadeOut.inMilliseconds / 1000.0,
                  min: 0,
                  max: dur.clamp(0.5, 10.0),
                  decimals: 2,
                  suffix: 's',
                  onChanged: (v) => edit(
                    (a) => a.copyWith(
                      fadeOut: Duration(milliseconds: (v * 1000).round()),
                    ),
                  ),
                ),
                const SizedBox(height: 6),
                const Text(
                  'O fade e de igual potencia: fade reto de volume soa '
                  'como um buraco no meio.',
                  style: TextStyle(
                    fontSize: 11,
                    height: 1.35,
                    color: AmColors.muted,
                  ),
                ),

                const SizedBox(height: 14),
                const Text(
                  'Abaixar pela voz',
                  style: TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.w700,
                    color: AmColors.text,
                  ),
                ),
                const SizedBox(height: 4),
                const Text(
                  'A trilha desce quando a voz entra e volta quando ela '
                  'para — sem desenhar envelope na mao.',
                  style: TextStyle(
                    fontSize: 11,
                    height: 1.35,
                    color: AmColors.muted,
                  ),
                ),
                const SizedBox(height: 8),
                if (vozes.isEmpty)
                  const Text(
                    'Nao ha outra faixa com som no projeto.',
                    style: TextStyle(fontSize: 11, color: AmColors.muted),
                  )
                else
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    children: [
                      _Chip(
                        label: 'Nenhuma',
                        selected: spec.duckAgainstId == null,
                        onTap: () => edit((a) => a.copyWith(clearDuck: true)),
                      ),
                      for (final v in vozes)
                        _Chip(
                          label: v.name,
                          selected: spec.duckAgainstId == v.id,
                          onTap: () =>
                              edit((a) => a.copyWith(duckAgainstId: v.id)),
                        ),
                    ],
                  ),
                if (spec.duckAgainstId != null)
                  _Slider(
                    label: 'Quanto desce',
                    value: spec.duckAmount,
                    min: 0,
                    max: 1,
                    decimals: 2,
                    onChanged: (v) => edit((a) => a.copyWith(duckAmount: v)),
                  ),

                const SizedBox(height: 16),
                Wrap(
                  spacing: 8,
                  runSpacing: 8,
                  children: [
                    _Action(
                      icon: CupertinoIcons.speedometer,
                      label: 'Normalizar',
                      onTap: () {
                        final g = controller.normalizeAudio(layerId);
                        setSheetState(() {});
                        if (!context.mounted) return;
                        AureaSnack.show(
                          context,
                          g == null
                              ? 'A forma de onda ainda esta sendo lida'
                              : 'Ganho ajustado para '
                                    '${gainToDb(g).toStringAsFixed(1)} dB',
                        );
                      },
                    ),
                    _Action(
                      icon: CupertinoIcons.scissors,
                      label: 'Remover silencio',
                      onTap: () {
                        final n = controller.removeSilence(layerId);
                        if (!context.mounted) return;
                        closeParamSheet(sheetContext);
                        AureaSnack.show(
                          context,
                          switch (n) {
                            null => 'A forma de onda ainda esta sendo lida',
                            <= 1 => 'Nao achei pausa longa o bastante',
                            _ => 'Ficaram $n pedacos, sem as pausas',
                          },
                          actionLabel: n != null && n > 1 ? 'Desfazer' : null,
                          onAction: controller.undo,
                        );
                      },
                    ),
                    _Action(
                      icon: CupertinoIcons.metronome,
                      label: 'Ver batidas',
                      onTap: () {
                        final b = controller.beatsOf(layerId);
                        if (!context.mounted) return;
                        AureaSnack.show(
                          context,
                          b == null
                              ? 'A forma de onda ainda esta sendo lida'
                              : b.isEmpty
                              ? 'Nao achei ataque nesta faixa'
                              : '${b.length} batidas — a primeira em '
                                    '${(b.first.inMilliseconds / 1000).toStringAsFixed(2)}s',
                        );
                      },
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

class _Slider extends StatelessWidget {
  const _Slider({
    required this.label,
    required this.value,
    required this.min,
    required this.max,
    required this.onChanged,
    this.decimals = 0,
    this.suffix = '',
  });

  final String label;
  final double value;
  final double min;
  final double max;
  final ValueChanged<double> onChanged;
  final int decimals;
  final String suffix;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 1),
      child: Row(
        children: [
          SizedBox(
            width: 110,
            child: Text(
              label,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(fontSize: 12, color: AmColors.muted),
            ),
          ),
          Expanded(
            child: AmTickRuler(
              value: value.clamp(min, max),
              min: min,
              max: max,
              unitsPerPixel: ((max) - (min)) / 420,
              height: 40,
              onChanged: onChanged,
            ),
          ),
          SizedBox(
            width: 52,
            child: Text(
              '${value.toStringAsFixed(decimals)}$suffix',
              textAlign: TextAlign.right,
              style: const TextStyle(fontSize: 12, color: AmColors.text),
            ),
          ),
        ],
      ),
    );
  }
}

class _Toggle extends StatelessWidget {
  const _Toggle({
    required this.label,
    required this.value,
    required this.onChanged,
  });

  final String label;
  final bool value;
  final ValueChanged<bool> onChanged;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.symmetric(vertical: 2),
    child: Row(
      children: [
        Expanded(
          child: Text(
            label,
            style: const TextStyle(fontSize: 13, color: AmColors.text),
          ),
        ),
        CupertinoSwitch(
          value: value,
          activeTrackColor: AmColors.accent,
          onChanged: onChanged,
        ),
      ],
    ),
  );
}

class _Chip extends StatelessWidget {
  const _Chip({
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
      padding: const EdgeInsets.symmetric(horizontal: 11, vertical: 6),
      decoration: BoxDecoration(
        color: selected ? AmColors.accentDim : AmColors.chip,
        borderRadius: BorderRadius.circular(8),
      ),
      child: Text(
        label,
        maxLines: 1,
        overflow: TextOverflow.ellipsis,
        style: TextStyle(
          fontSize: 11,
          color: selected ? AmColors.accent : AmColors.muted,
        ),
      ),
    ),
  );
}

class _Action extends StatelessWidget {
  const _Action({required this.icon, required this.label, required this.onTap});

  final IconData icon;
  final String label;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => GestureDetector(
    onTap: onTap,
    child: Container(
      padding: const EdgeInsets.symmetric(horizontal: 11, vertical: 8),
      decoration: BoxDecoration(
        color: AmColors.chip,
        borderRadius: BorderRadius.circular(9),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icon, size: 14, color: AmColors.accent),
          const SizedBox(width: 6),
          Text(
            label,
            style: const TextStyle(fontSize: 12, color: AmColors.accent),
          ),
        ],
      ),
    ),
  );
}
