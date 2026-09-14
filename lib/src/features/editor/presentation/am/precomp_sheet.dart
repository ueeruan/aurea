import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/layer.dart';
import 'am_colors.dart';
import 'am_widgets.dart';

/// PRECOMP: o grupo ganha tempo proprio.
///
/// Ate aqui, agrupar era so organizacao — os filhos liam o mesmo relogio
/// da composicao. Com a duracao interna e o remapeamento, o grupo passa
/// a ser uma composicao dentro da composicao: da para congelar, inverter,
/// e fazer rampa de velocidade sem tocar em nenhum filho.
Future<void> showPrecompSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  PlaybackController playback,
) async {
  await showParamSheet(
    context,
    title: 'Precomp',
    heightFactor: 0.56,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        final project = ref.read(editorControllerProvider);
        final controller = ref.read(editorControllerProvider.notifier);
        final layer = project.layerById(layerId);
        if (layer is! GroupLayer) return const SizedBox.shrink();

        final remapAtivo = layer.timeRemap != null;
        final segInterna = layer.innerDuration.inMicroseconds / 1000000.0;
        final segBarra = layer.duration.inMicroseconds / 1000000.0;

        void redesenha() => setSheetState(() {});

        return SafeArea(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(18, 14, 18, 18),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const AppText('Precomp',
                  style: TextStyle(
                    fontSize: 17,
                    fontWeight: FontWeight.w700,
                    color: AmColors.text,
                  ),
                ),
                const SizedBox(height: 12),

                _Ruler(
                  label: 'Duracao interna',
                  value: segInterna,
                  min: 0.1,
                  max: 600,
                  decimals: 2,
                  suffix: ' s',
                  onChanged: (v) {
                    controller.updatePrecomp(
                      layerId,
                      sourceDuration: Duration(
                        microseconds: (v * 1000000).round(),
                      ),
                    );
                    redesenha();
                  },
                ),
                if (layer.sourceDuration != null)
                  _Botao(
                    'Igualar a barra (${segBarra.toStringAsFixed(2)} s)',
                    () {
                      controller.updatePrecomp(
                        layerId,
                        clearSourceDuration: true,
                      );
                      redesenha();
                    },
                  ),

                // O TEMPO REMAPEADO saiu do app. Projeto antigo que ja o
                // tinha ligado ve so o interruptor para desligar.
                if (remapAtivo) ...[
                  const SizedBox(height: 10),
                  _Toggle(
                    label: 'Remapear tempo',
                    value: true,
                    onChanged: (v) {
                      if (!v) {
                        controller.updatePrecomp(layerId, clearRemap: true);
                      }
                      redesenha();
                    },
                  ),
                ],

                const SizedBox(height: 10),
                _Toggle(
                  label: 'Colapsar transformacoes',
                  value: layer.collapse,
                  onChanged: (v) {
                    controller.updatePrecomp(layerId, collapse: v);
                    redesenha();
                  },
                ),
                const AppText('Sem quadro proprio, os filhos compoem direto com o pai '
                  '— e a forma vetorial nao pixela ao ampliar.',
                  style: TextStyle(
                    fontSize: 11,
                    height: 1.35,
                    color: AmColors.muted,
                  ),
                ),
                const SizedBox(height: 8),
                _Toggle(
                  label: 'Recortar no quadro',
                  value: layer.clipToComp,
                  onChanged: (v) {
                    controller.updatePrecomp(layerId, clipToComp: v);
                    redesenha();
                  },
                ),
              ],
            ),
          ),
        );
      },
    ),
  );
}

class _Botao extends StatelessWidget {
  const _Botao(this.label, this.onTap);

  final String label;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => GestureDetector(
    onTap: onTap,
    child: Container(
      margin: const EdgeInsets.symmetric(vertical: 4),
      padding: const EdgeInsets.symmetric(vertical: 9),
      alignment: Alignment.center,
      decoration: BoxDecoration(
        color: AmColors.chip,
        borderRadius: BorderRadius.circular(9),
      ),
      child: AppText(
        label,
        style: const TextStyle(fontSize: 12, color: AmColors.text),
      ),
    ),
  );
}

class _Ruler extends StatelessWidget {
  const _Ruler({
    required this.label,
    required this.value,
    required this.min,
    required this.max,
    required this.onChanged,
    this.suffix = '',
    this.decimals = 0,
  });

  final String label;
  final double value;
  final double min;
  final double max;
  final ValueChanged<double> onChanged;
  final String suffix;
  final int decimals;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.symmetric(vertical: 2),
    child: Row(
      children: [
        SizedBox(
          width: 104,
          child: AppText(
            label,
            style: const TextStyle(fontSize: 12, color: AmColors.muted),
          ),
        ),
        Expanded(
          child: AmTickRuler(
            value: value,
            min: min,
            max: max,
            unitsPerPixel: (max - min) / 400,
            height: 40,
            onChanged: onChanged,
          ),
        ),
        SizedBox(
          width: 62,
          child: AppText(
            '${value.toStringAsFixed(decimals)}$suffix',
            textAlign: TextAlign.right,
            style: const TextStyle(fontSize: 12, color: AmColors.text),
          ),
        ),
      ],
    ),
  );
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
          child: AppText(
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
