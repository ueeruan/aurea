import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/snack.dart';
import '../../application/editor_controller.dart';
import '../../domain/cut.dart';
import 'am_colors.dart';
import 'am_widgets.dart';

Future<void> showFreezeSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  Duration globalTime,
) async {
  var seconds = 1.0;
  var placement = FreezePlacement.separateClip;
  await showParamSheet(
    context,
    title: 'Congelar quadro',
    heightFactor: 0.38,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) => SafeArea(
        child: Padding(
          padding: const EdgeInsets.fromLTRB(18, 14, 18, 22),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Text(
                'Duracao',
                style: TextStyle(fontSize: 12, color: AmColors.muted),
              ),
              Row(
                children: [
                  Expanded(
                    child: AmTickRuler(
                      value: seconds,
                      min: 0.1,
                      max: 10,
                      unitsPerPixel: 0.04,
                      height: 44,
                      onChanged: (value) =>
                          setSheetState(() => seconds = value),
                    ),
                  ),
                  SizedBox(
                    width: 62,
                    child: Text(
                      '${seconds.toStringAsFixed(1)} s',
                      textAlign: TextAlign.right,
                      style: const TextStyle(
                        fontSize: 12,
                        color: AmColors.text,
                      ),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 12),
              CupertinoSlidingSegmentedControl<FreezePlacement>(
                groupValue: placement,
                thumbColor: AmColors.accentDim,
                children: const {
                  FreezePlacement.separateClip: Padding(
                    padding: EdgeInsets.symmetric(horizontal: 8, vertical: 7),
                    child: Text('Clipe separado'),
                  ),
                  FreezePlacement.insideClip: Padding(
                    padding: EdgeInsets.symmetric(horizontal: 8, vertical: 7),
                    child: Text('Dentro do clipe'),
                  ),
                },
                onValueChanged: (value) {
                  if (value != null) setSheetState(() => placement = value);
                },
              ),
              const Spacer(),
              SizedBox(
                width: double.infinity,
                child: CupertinoButton.filled(
                  onPressed: () {
                    final ok = ref
                        .read(editorControllerProvider.notifier)
                        .freezeFrame(
                          layerId,
                          globalTime,
                          duration: Duration(
                            milliseconds: (seconds * 1000).round(),
                          ),
                          placement: placement,
                        );
                    if (!ok) {
                      AureaSnack.show(
                        sheetContext,
                        'Leve o cabecote para dentro de um clipe de video',
                      );
                      return;
                    }
                    closeParamSheet(sheetContext);
                  },
                  child: const Text('Congelar aqui'),
                ),
              ),
            ],
          ),
        ),
      ),
    ),
  );
}
