import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart' hide Easing;

import '../../domain/apple_motion.dart';
import '../../domain/keyframe.dart';
import '../../domain/video_project.dart' show LayerProp;
import 'am_colors.dart';
import 'am_widgets.dart';

typedef ApplyCascade = void Function(
  Duration interval,
  CascadeOrder order,
  Easing? ease,
);

typedef ApplyCascadeLink = void Function(
  Duration interval,
  CascadeOrder order,
  Easing ease,
  LayerProp property,
);

enum _CascadeDepth { pronto, montar, avancado }

/// Superfície desacoplada da seleção e do controller. Quem abre fornece uma
/// única operação, permitindo que toda a cascata entre no undo como um passo.
Future<void> showAppleCascadeSheet(
  BuildContext context, {
  required int selectionCount,
  required ApplyCascade onApply,
  ApplyCascadeLink? onLinkProperty,
}) {
  var depth = _CascadeDepth.pronto;
  var intervalMs = 40.0;
  var order = CascadeOrder.start;
  var ease = Easing.interfaceSpring;
  var property = LayerProp.position;

  return showModalBottomSheet<void>(
    context: context,
    backgroundColor: AmColors.panel,
    isScrollControlled: true,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        void apply({bool ready = false}) {
          onApply(
            Duration(milliseconds: (ready ? 40 : intervalMs).round()),
            ready ? CascadeOrder.start : order,
            ready
                ? Easing.interfaceSpring
                : depth == _CascadeDepth.avancado
                ? ease
                : null,
          );
          Navigator.of(sheetContext).pop();
        }

        void linkProperty() {
          onLinkProperty?.call(
            Duration(milliseconds: intervalMs.round()),
            order,
            ease,
            property,
          );
          Navigator.of(sheetContext).pop();
        }

        Widget choice(String label, bool selected, VoidCallback onTap) =>
            GestureDetector(
              onTap: onTap,
              child: Container(
                padding: const EdgeInsets.symmetric(
                  horizontal: 10,
                  vertical: 7,
                ),
                decoration: BoxDecoration(
                  color: selected ? AmColors.accent : AmColors.chip,
                  borderRadius: BorderRadius.circular(8),
                ),
                child: Text(
                  label,
                  style: TextStyle(
                    fontSize: 11,
                    fontWeight: FontWeight.w600,
                    color: selected ? AmColors.bg : AmColors.text,
                  ),
                ),
              ),
            );

        return SafeArea(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(18, 14, 18, 18),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Text(
                  'Cascata · $selectionCount camadas',
                  style: const TextStyle(
                    fontSize: 17,
                    fontWeight: FontWeight.w700,
                    color: AmColors.text,
                  ),
                ),
                const SizedBox(height: 12),
                Row(
                  children: [
                    for (final value in _CascadeDepth.values)
                      Expanded(
                        child: Padding(
                          padding: const EdgeInsets.only(right: 6),
                          child: choice(
                            switch (value) {
                              _CascadeDepth.pronto => 'Pronto',
                              _CascadeDepth.montar => 'Montar',
                              _CascadeDepth.avancado => 'Avancado',
                            },
                            depth == value,
                            () => setSheetState(() => depth = value),
                          ),
                        ),
                      ),
                  ],
                ),
                const SizedBox(height: 14),
                if (depth == _CascadeDepth.pronto) ...[
                  const Text(
                    '40 ms entre cada camada, com Mola de interface.',
                    style: TextStyle(fontSize: 12, color: AmColors.muted),
                  ),
                  const SizedBox(height: 10),
                  CupertinoButton(
                    color: AmColors.accent,
                    onPressed: selectionCount < 2
                        ? null
                        : () => apply(ready: true),
                    child: const Text(
                      'Escalonar selecao',
                      style: TextStyle(
                        color: AmColors.bg,
                        fontWeight: FontWeight.w700,
                      ),
                    ),
                  ),
                ] else ...[
                  Row(
                    children: [
                      const SizedBox(
                        width: 82,
                        child: Text(
                          'Intervalo',
                          style: TextStyle(fontSize: 12, color: AmColors.muted),
                        ),
                      ),
                      Expanded(
                        child: AmTickRuler(
                          value: intervalMs,
                          min: 0,
                          max: 200,
                          unitsPerPixel: 0.5,
                          height: 40,
                          onChanged: (v) =>
                              setSheetState(() => intervalMs = v),
                        ),
                      ),
                      SizedBox(
                        width: 58,
                        child: Text(
                          '${intervalMs.round()} ms',
                          textAlign: TextAlign.end,
                          style: const TextStyle(
                            fontSize: 12,
                            color: AmColors.accent,
                          ),
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 10),
                  const Text(
                    'Ordem',
                    style: TextStyle(fontSize: 12, color: AmColors.muted),
                  ),
                  const SizedBox(height: 6),
                  Wrap(
                    spacing: 6,
                    runSpacing: 6,
                    children: [
                      for (final value in CascadeOrder.values)
                        choice(
                          switch (value) {
                            CascadeOrder.start => 'Inicio',
                            CascadeOrder.center => 'Centro',
                            CascadeOrder.end => 'Fim',
                            CascadeOrder.random => 'Aleatoria',
                          },
                          order == value,
                          () => setSheetState(() => order = value),
                        ),
                    ],
                  ),
                  if (depth == _CascadeDepth.avancado) ...[
                    const SizedBox(height: 14),
                    const Text(
                      'Vinculo de propriedade',
                      style: TextStyle(fontSize: 12, color: AmColors.muted),
                    ),
                    const SizedBox(height: 6),
                    Wrap(
                      spacing: 6,
                      runSpacing: 6,
                      children: [
                        for (final value in const [
                          LayerProp.position,
                          LayerProp.scale,
                          LayerProp.rotation,
                          LayerProp.opacity,
                        ])
                          choice(
                            switch (value) {
                              LayerProp.position => 'Posicao',
                              LayerProp.scale => 'Escala',
                              LayerProp.rotation => 'Rotacao',
                              LayerProp.opacity => 'Opacidade',
                              _ => value.name,
                            },
                            property == value,
                            () => setSheetState(() => property = value),
                          ),
                      ],
                    ),
                    const SizedBox(height: 14),
                    const Text(
                      'Curva compartilhada',
                      style: TextStyle(fontSize: 12, color: AmColors.muted),
                    ),
                    const SizedBox(height: 6),
                    Wrap(
                      spacing: 6,
                      runSpacing: 6,
                      children: [
                        for (final preset in const [
                          (label: 'Apple padrao', ease: Easing.appleStandard),
                          (label: 'Apple entrada', ease: Easing.appleEntrance),
                          (label: 'Apple saida', ease: Easing.appleExit),
                          (
                            label: 'Mola interface',
                            ease: Easing.interfaceSpring,
                          ),
                          (label: 'Mola suave', ease: Easing.softSpring),
                        ])
                          choice(
                            preset.label,
                            identical(ease, preset.ease),
                            () => setSheetState(() => ease = preset.ease),
                          ),
                      ],
                    ),
                    const SizedBox(height: 8),
                    const Text(
                      'Os keyframes continuam reais e podem ser editados por camada.',
                      style: TextStyle(fontSize: 10, color: AmColors.muted),
                    ),
                  ],
                  const SizedBox(height: 14),
                  CupertinoButton(
                    color: AmColors.accent,
                    onPressed: selectionCount < 2
                        ? null
                        : depth == _CascadeDepth.avancado &&
                              onLinkProperty != null
                        ? linkProperty
                        : apply,
                    child: Text(
                      depth == _CascadeDepth.avancado && onLinkProperty != null
                          ? 'Vincular com atraso incremental'
                          : 'Aplicar cascata',
                      style: const TextStyle(
                        color: AmColors.bg,
                        fontWeight: FontWeight.w700,
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
