import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/snack.dart';
import '../../application/editor_controller.dart';
import '../../application/media_preview_service.dart';
import '../../domain/layer.dart';
import 'am_colors.dart';
import 'am_widgets.dart';

/// PULSAR NA BATIDA.
///
/// Feito na mao, isso e um keyframe a cada meio segundo por tres minutos
/// — ninguem faz, e o video fica parado enquanto a musica anda. A conta
/// de achar a batida ja existe; o que faltava era virar keyframe.
Future<void> showBeatPulseSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) async {
  var forca = 0.12;
  String? fonteId;

  await showParamSheet(
    context,
    title: 'Pulsar na batida',
    heightFactor: 0.5,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        final project = ref.read(editorControllerProvider);
        final controller = ref.read(editorControllerProvider.notifier);
        final layer = project.layerById(layerId);
        if (layer == null) return const SizedBox.shrink();

        final fontes = [
          for (final l in project.layers)
            if (l is AudioLayer || (l is VideoLayer && l.volume > 0.001)) l,
        ];
        fonteId ??= fontes.isEmpty ? null : fontes.first.id;

        // A forma de onda pode nao estar pronta ainda.
        final caminho = switch (fontes
            .where((l) => l.id == fonteId)
            .firstOrNull) {
          AudioLayer a => a.sourcePath,
          VideoLayer v => v.sourcePath,
          _ => null,
        };
        if (caminho != null) {
          MediaPreviewService.instance.ensureWaveform(caminho);
        }
        final batidas = fonteId == null ? null : controller.beatsOf(fonteId!);

        return SafeArea(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(18, 14, 18, 18),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text(
                  'Pulsar na batida',
                  style: TextStyle(
                    fontSize: 17,
                    fontWeight: FontWeight.w700,
                    color: AmColors.text,
                  ),
                ),
                const SizedBox(height: 4),
                Text(
                  '"${layer.name}" cresce um tiquinho em cada ataque da '
                  'musica.',
                  style: const TextStyle(
                    fontSize: 11,
                    height: 1.35,
                    color: AmColors.muted,
                  ),
                ),
                const SizedBox(height: 12),

                if (fontes.isEmpty)
                  const Text(
                    'Nao ha faixa de som no projeto.',
                    style: TextStyle(fontSize: 12, color: AmColors.muted),
                  )
                else ...[
                  const Text(
                    'Ouvir de',
                    style: TextStyle(fontSize: 12, color: AmColors.muted),
                  ),
                  const SizedBox(height: 6),
                  Wrap(
                    spacing: 6,
                    runSpacing: 6,
                    children: [
                      for (final f in fontes)
                        GestureDetector(
                          onTap: () => setSheetState(() => fonteId = f.id),
                          child: Container(
                            padding: const EdgeInsets.symmetric(
                              horizontal: 11,
                              vertical: 6,
                            ),
                            decoration: BoxDecoration(
                              color: f.id == fonteId
                                  ? AmColors.accentDim
                                  : AmColors.chip,
                              borderRadius: BorderRadius.circular(8),
                            ),
                            child: Text(
                              f.name,
                              style: TextStyle(
                                fontSize: 11,
                                color: f.id == fonteId
                                    ? AmColors.accent
                                    : AmColors.muted,
                              ),
                            ),
                          ),
                        ),
                    ],
                  ),
                  const SizedBox(height: 12),

                  Row(
                    children: [
                      const SizedBox(
                        width: 62,
                        child: Text(
                          'Forca',
                          style: TextStyle(fontSize: 12, color: AmColors.muted),
                        ),
                      ),
                      Expanded(
                        child: AmTickRuler(
                          value: forca,
                          min: 0.02,
                          max: 0.6,
                          unitsPerPixel: ((0.6) - (0.02)) / 420,
                          height: 40,
                          onChanged: (v) => setSheetState(() => forca = v),
                        ),
                      ),
                      SizedBox(
                        width: 56,
                        child: Text(
                          '+${(forca * 100).toStringAsFixed(0)}%',
                          textAlign: TextAlign.right,
                          style: const TextStyle(
                            fontSize: 12,
                            color: AmColors.text,
                          ),
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 6),
                  Text(
                    batidas == null
                        ? 'Lendo o som...'
                        : '${batidas.length} batidas encontradas.',
                    style: const TextStyle(fontSize: 11, color: AmColors.muted),
                  ),
                  const SizedBox(height: 12),

                  _Botao('Aplicar', () {
                    final n = controller.applyBeatPulse(
                      layerId,
                      fonteId!,
                      amount: forca,
                    );
                    if (n == null) {
                      AureaSnack.show(
                        sheetContext,
                        'A forma de onda ainda nao ficou pronta',
                      );
                      return;
                    }
                    if (n == 0) {
                      AureaSnack.show(
                        sheetContext,
                        'Nenhuma batida cai dentro desta camada',
                      );
                      return;
                    }
                    closeParamSheet(sheetContext);
                    AureaSnack.show(
                      context,
                      '$n batidas viraram keyframe',
                      actionLabel: 'Desfazer',
                      onAction: controller.undo,
                    );
                  }),
                  _Botao('Tirar os keyframes de escala', () {
                    controller.clearScaleKeyframes(layerId);
                    closeParamSheet(sheetContext);
                    AureaSnack.show(
                      context,
                      'Escala voltou a ser fixa',
                      actionLabel: 'Desfazer',
                      onAction: controller.undo,
                    );
                  }),
                ],
              ],
            ),
          ),
        );
      },
    ),
  );
}

class _Botao extends StatelessWidget {
  const _Botao(this.rotulo, this.onTap);

  final String rotulo;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => GestureDetector(
    onTap: onTap,
    child: Container(
      margin: const EdgeInsets.symmetric(vertical: 3),
      padding: const EdgeInsets.symmetric(vertical: 10),
      alignment: Alignment.center,
      decoration: BoxDecoration(
        color: AmColors.panelHigh,
        borderRadius: BorderRadius.circular(9),
      ),
      child: Text(
        rotulo,
        style: const TextStyle(
          fontSize: 12,
          fontWeight: FontWeight.w600,
          color: AmColors.text,
        ),
      ),
    ),
  );
}
