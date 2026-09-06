import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../domain/layer.dart';
import '../../domain/text_path.dart';
import 'am_colors.dart';
import 'am_widgets.dart';

/// TEXTO EM CAMINHO: selo circular, arco, ou acompanhando uma forma
/// desenhada no proprio projeto.
Future<void> showTextPathSheet(
    BuildContext context, WidgetRef ref, String layerId) async {
  await showParamSheet(
    context,
    title: 'Texto em caminho',
    heightFactor: 0.5,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        final project = ref.read(editorControllerProvider);
        final controller = ref.read(editorControllerProvider.notifier);
        final layer = project.layerById(layerId);
        if (layer is! TextLayer) return const SizedBox.shrink();
        final spec = layer.textPath;

        void edit(TextPathSpec Function(TextPathSpec) fn) {
          controller.updateTextPath(layerId, fn);
          setSheetState(() {});
        }

        final formas =
            project.layers.whereType<ShapeLayer>().toList();

        return SafeArea(
          child: SingleChildScrollView(
            padding: EdgeInsets.fromLTRB(18, 14, 18,
                16 + MediaQuery.of(sheetContext).viewInsets.bottom),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text('Texto em caminho',
                    style: TextStyle(
                        fontSize: 17,
                        fontWeight: FontWeight.w700,
                        color: AmColors.text)),
                const SizedBox(height: 4),
                const Text(
                  'Selo circular, arco, ou seguindo uma forma que voce '
                  'desenhou — com os operadores e tudo.',
                  style: TextStyle(
                      fontSize: 11, height: 1.35, color: AmColors.muted),
                ),
                const SizedBox(height: 12),

                _Chips(
                  label: 'Caminho',
                  options: [
                    for (final k in TextPathKind.values)
                      textPathKindLabel(k)
                  ],
                  index: spec.kind.index,
                  onChanged: (i) => edit((s) =>
                      s.copyWith(kind: TextPathKind.values[i])),
                ),

                if (spec.kind == TextPathKind.layer) ...[
                  const SizedBox(height: 6),
                  if (formas.isEmpty)
                    const Text(
                      'Nao ha camada de forma no projeto para seguir.',
                      style: TextStyle(
                          fontSize: 11, color: AmColors.muted),
                    )
                  else
                    Wrap(
                      spacing: 6,
                      runSpacing: 6,
                      children: [
                        for (final f in formas)
                          GestureDetector(
                            onTap: () => edit(
                                (s) => s.copyWith(shapeLayerId: f.id)),
                            child: Container(
                              padding: const EdgeInsets.symmetric(
                                  horizontal: 10, vertical: 6),
                              decoration: BoxDecoration(
                                color: spec.shapeLayerId == f.id
                                    ? AmColors.accentDim
                                    : AmColors.chip,
                                borderRadius: BorderRadius.circular(8),
                              ),
                              child: Text(f.name,
                                  style: TextStyle(
                                      fontSize: 11,
                                      color: spec.shapeLayerId == f.id
                                          ? AmColors.accent
                                          : AmColors.muted)),
                            ),
                          ),
                      ],
                    ),
                ],

                if (spec.kind == TextPathKind.circle ||
                    spec.kind == TextPathKind.arc) ...[
                  _Ruler(
                    label: 'Raio',
                    value: spec.radius,
                    min: 20,
                    max: 800,
                    onChanged: (v) => edit((s) => s.copyWith(radius: v)),
                  ),
                  _Ruler(
                    label: 'Comeco',
                    value: spec.startDeg,
                    min: -180,
                    max: 180,
                    suffix: '°',
                    onChanged: (v) =>
                        edit((s) => s.copyWith(startDeg: v)),
                  ),
                ],
                if (spec.kind == TextPathKind.arc)
                  _Ruler(
                    label: 'Abertura',
                    value: spec.sweepDeg,
                    min: 10,
                    max: 360,
                    suffix: '°',
                    onChanged: (v) =>
                        edit((s) => s.copyWith(sweepDeg: v)),
                  ),

                if (spec.active) ...[
                  _Ruler(
                    label: 'Deslizar',
                    value: spec.offset,
                    min: -1000,
                    max: 1000,
                    onChanged: (v) => edit((s) => s.copyWith(offset: v)),
                  ),
                  _Ruler(
                    label: 'Espaco',
                    value: spec.spacing,
                    min: -20,
                    max: 60,
                    onChanged: (v) => edit((s) => s.copyWith(spacing: v)),
                  ),
                  _Chips(
                    label: 'Alinhar',
                    options: const ['Acima', 'Sobre', 'Abaixo'],
                    index: spec.align.index,
                    onChanged: (i) => edit((s) =>
                        s.copyWith(align: TextPathAlign.values[i])),
                  ),
                  _Toggle(
                    label: 'Girar com a curva',
                    value: spec.perpendicular,
                    onChanged: (v) =>
                        edit((s) => s.copyWith(perpendicular: v)),
                  ),
                  _Toggle(
                    label: 'Inverter o sentido',
                    value: spec.reverse,
                    onChanged: (v) => edit((s) => s.copyWith(reverse: v)),
                  ),
                  const SizedBox(height: 6),
                  const Text(
                    'Animar "Deslizar" faz o texto correr pelo caminho — '
                    'e assim que um selo gira.',
                    style: TextStyle(
                        fontSize: 11, height: 1.35, color: AmColors.muted),
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

class _Ruler extends StatelessWidget {
  const _Ruler({
    required this.label,
    required this.value,
    required this.min,
    required this.max,
    required this.onChanged,
    this.suffix = '',
  });

  final String label;
  final double value;
  final double min;
  final double max;
  final ValueChanged<double> onChanged;
  final String suffix;

  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.symmetric(vertical: 2),
        child: Row(
          children: [
            SizedBox(
              width: 84,
              child: Text(label,
                  style: const TextStyle(
                      fontSize: 12, color: AmColors.muted)),
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
              width: 56,
              child: Text('${value.round()}$suffix',
                  textAlign: TextAlign.right,
                  style: const TextStyle(
                      fontSize: 12, color: AmColors.text)),
            ),
          ],
        ),
      );
}

class _Chips extends StatelessWidget {
  const _Chips({
    required this.label,
    required this.options,
    required this.index,
    required this.onChanged,
  });

  final String label;
  final List<String> options;
  final int index;
  final ValueChanged<int> onChanged;

  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.symmetric(vertical: 4),
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            SizedBox(
                width: 84,
                child: Text(label,
                    style: const TextStyle(
                        fontSize: 12, color: AmColors.muted))),
            Expanded(
              child: Wrap(
                spacing: 6,
                runSpacing: 6,
                children: [
                  for (var i = 0; i < options.length; i++)
                    GestureDetector(
                      onTap: () => onChanged(i),
                      child: Container(
                        padding: const EdgeInsets.symmetric(
                            horizontal: 10, vertical: 6),
                        decoration: BoxDecoration(
                          color: i == index
                              ? AmColors.accentDim
                              : AmColors.chip,
                          borderRadius: BorderRadius.circular(8),
                        ),
                        child: Text(options[i],
                            style: TextStyle(
                                fontSize: 11,
                                color: i == index
                                    ? AmColors.accent
                                    : AmColors.muted)),
                      ),
                    ),
                ],
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
              child: Text(label,
                  style: const TextStyle(
                      fontSize: 13, color: AmColors.text)),
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
