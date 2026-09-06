import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../domain/layout_ops.dart';
import 'am_colors.dart';
import '../../../../core/ui/snack.dart';
import 'am_widgets.dart';

/// ALINHAR E DISTRIBUIR (spec motion-graphics-pro, PR-X1). Motion
/// graphics e 60% posicionamento exato — no dedo nao fica exato.
Future<void> showAlignSheet(BuildContext context, WidgetRef ref,
    List<String> ids, Duration t) async {
  final controller = ref.read(editorControllerProvider.notifier);
  var to = AlignTo.composition;

  await showParamSheet(
    context,
    title: 'Alinhar',
    heightFactor: 0.42,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        Widget iconBtn(IconData icon, String tip, VoidCallback onTap,
            {bool enabled = true}) {
          return CupertinoButton(
            padding: const EdgeInsets.all(10),
            onPressed: enabled
                ? onTap
                : () => AureaSnack.show(context, tip,
                    duration: const Duration(milliseconds: 1400)),
            child: Opacity(
              opacity: enabled ? 1 : 0.32,
              child: Icon(icon, size: 22, color: AmColors.accent),
            ),
          );
        }

        final canDistribute = ids.length >= 3;

        return SafeArea(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(18, 12, 18, 14),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text('Alinhar — ${ids.length} camada(s)',
                    style: const TextStyle(
                        fontSize: 16,
                        fontWeight: FontWeight.w700,
                        color: AmColors.text)),
                const SizedBox(height: 8),
                // Referencia do alinhamento.
                Row(
                  children: [
                    const Text('Em relacao a',
                        style: TextStyle(
                            fontSize: 12, color: AmColors.muted)),
                    const SizedBox(width: 10),
                    for (final (label, value) in const [
                      ('Composicao', AlignTo.composition),
                      ('Selecao', AlignTo.selection),
                    ])
                      Padding(
                        padding: const EdgeInsets.only(right: 8),
                        child: GestureDetector(
                          onTap: () => setSheetState(() => to = value),
                          child: Container(
                            padding: const EdgeInsets.symmetric(
                                horizontal: 12, vertical: 7),
                            decoration: BoxDecoration(
                              color: to == value
                                  ? AmColors.accentDim
                                  : AmColors.chip,
                              borderRadius: BorderRadius.circular(9),
                            ),
                            child: Text(label,
                                style: const TextStyle(
                                    fontSize: 12,
                                    color: AmColors.accent)),
                          ),
                        ),
                      ),
                  ],
                ),
                const SizedBox(height: 6),
                Row(
                  children: [
                    iconBtn(CupertinoIcons.rectangle_grid_1x2, 'Esquerda',
                        () => controller.alignSelection(
                            ids, AlignEdge.left, t, to: to)),
                    iconBtn(CupertinoIcons.arrow_left_right, 'Centro H',
                        () => controller.alignSelection(
                            ids, AlignEdge.centerH, t, to: to)),
                    iconBtn(CupertinoIcons.rectangle_grid_1x2_fill,
                        'Direita',
                        () => controller.alignSelection(
                            ids, AlignEdge.right, t, to: to)),
                    const SizedBox(width: 8),
                    iconBtn(CupertinoIcons.arrow_up_to_line, 'Topo',
                        () => controller.alignSelection(
                            ids, AlignEdge.top, t, to: to)),
                    iconBtn(CupertinoIcons.arrow_up_arrow_down,
                        'Centro V',
                        () => controller.alignSelection(
                            ids, AlignEdge.centerV, t, to: to)),
                    iconBtn(CupertinoIcons.arrow_down_to_line, 'Base',
                        () => controller.alignSelection(
                            ids, AlignEdge.bottom, t, to: to)),
                  ],
                ),
                const Divider(color: AmColors.hairline, height: 20),
                const Text('Distribuir',
                    style: TextStyle(
                        fontSize: 13,
                        fontWeight: FontWeight.w700,
                        color: AmColors.text)),
                const Text(
                  'Por centro iguala os centros; por vao iguala os '
                  'espacos. Com tamanhos diferentes, dao resultados '
                  'distintos.',
                  style: TextStyle(fontSize: 11, color: AmColors.muted),
                ),
                const SizedBox(height: 6),
                Wrap(
                  spacing: 8,
                  runSpacing: 8,
                  children: [
                    for (final (label, axis, mode) in const [
                      ('↔ centro', DistributeAxis.horizontal,
                          DistributeMode.byCenter),
                      ('↔ vao igual', DistributeAxis.horizontal,
                          DistributeMode.byGap),
                      ('↕ centro', DistributeAxis.vertical,
                          DistributeMode.byCenter),
                      ('↕ vao igual', DistributeAxis.vertical,
                          DistributeMode.byGap),
                    ])
                      GestureDetector(
                        onTap: () {
                          if (!canDistribute) {
                            AureaSnack.show(context,
                                'Distribuir precisa de 3 ou mais camadas',
                                duration:
                                    const Duration(milliseconds: 1600));
                            return;
                          }
                          controller.distributeSelection(
                              ids, axis, mode, t);
                        },
                        child: Opacity(
                          opacity: canDistribute ? 1 : 0.35,
                          child: Container(
                            padding: const EdgeInsets.symmetric(
                                horizontal: 12, vertical: 8),
                            decoration: BoxDecoration(
                              color: AmColors.chip,
                              borderRadius: BorderRadius.circular(9),
                            ),
                            child: Text(label,
                                style: const TextStyle(
                                    fontSize: 12,
                                    color: AmColors.accent)),
                          ),
                        ),
                      ),
                  ],
                ),
                const SizedBox(height: 10),
                Row(
                  children: [
                    const Text('Espaco exato',
                        style: TextStyle(
                            fontSize: 12, color: AmColors.muted)),
                    const SizedBox(width: 10),
                    for (final gap in const [0.0, 16.0, 24.0, 48.0])
                      Padding(
                        padding: const EdgeInsets.only(right: 6),
                        child: GestureDetector(
                          onTap: () => controller.spaceSelection(
                              ids, DistributeAxis.horizontal, gap, t),
                          child: Container(
                            padding: const EdgeInsets.symmetric(
                                horizontal: 10, vertical: 6),
                            decoration: BoxDecoration(
                              color: AmColors.chip,
                              borderRadius: BorderRadius.circular(8),
                            ),
                            child: Text('${gap.round()}px',
                                style: const TextStyle(
                                    fontSize: 11,
                                    color: AmColors.accent)),
                          ),
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
