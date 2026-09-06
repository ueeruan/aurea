import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/snack.dart';
import '../../application/editor_controller.dart';
import '../../domain/audio_ops.dart';
import 'am_colors.dart';
import 'am_widgets.dart';

/// DETECTAR BATIDAS numa trilha.
///
/// O que sai daqui nao sao os ataques crus: e a GRADE regular que nasce
/// do andamento. Ataque treme alguns milissegundos, e corte encaixado em
/// ataque herda o tremor — soa fora do tempo mesmo caindo "onde a musica
/// bateu". Por isso o BPM aparece e da para corrigir: quem edita musica
/// muitas vezes sabe o andamento, e digitar acerta mais rapido do que
/// reanalisar.
Future<void> showBeatsSheet(
    BuildContext context, WidgetRef ref, String layerId) async {
  var band = BeatBand.grave;
  var sensibilidade = 50.0;
  var denominador = 4;
  var rodando = false;

  await showParamSheet(
    context,
    title: 'Batidas',
    heightFactor: 0.5,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        final project = ref.watch(editorControllerProvider);
        final controller = ref.read(editorControllerProvider.notifier);

        Future<void> analisar() async {
          setSheetState(() => rodando = true);
          final n = await controller.detectBeatsInto(
            layerId,
            band: band,
            sensitivity: sensibilidade,
            denominador: denominador,
          );
          if (!sheetContext.mounted) return;
          setSheetState(() => rodando = false);
          AureaSnack.show(
            sheetContext,
            n == null
                ? 'Nao achei ritmo nessa faixa'
                : '$n marcas de batida',
          );
        }

        return SafeArea(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(18, 12, 18, 16),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text(
                  'FAIXA DE FREQUENCIA',
                  style: TextStyle(
                      fontSize: 10.5,
                      letterSpacing: 0.6,
                      color: AmColors.muted),
                ),
                const SizedBox(height: 6),
                Row(
                  children: [
                    for (final b in BeatBand.values)
                      Expanded(
                        child: _Chip(
                          label: switch (b) {
                            BeatBand.grave => 'Grave',
                            BeatBand.medio => 'Medio',
                            BeatBand.agudo => 'Agudo',
                            BeatBand.tudo => 'Tudo',
                          },
                          selecionado: b == band,
                          onTap: () => setSheetState(() => band = b),
                        ),
                      ),
                  ],
                ),
                const SizedBox(height: 4),
                const Text(
                  'Bumbo e chimbal atacam em instantes diferentes. Cortar '
                  'no grave e cortar no pulso; no agudo, na levada.',
                  style: TextStyle(
                      fontSize: 11, height: 1.35, color: AmColors.muted),
                ),
                const SizedBox(height: 14),

                _Linha(
                  label: 'Sensibilidade',
                  value: sensibilidade,
                  min: 0,
                  max: 100,
                  onChanged: (v) => setSheetState(() => sensibilidade = v),
                ),

                const SizedBox(height: 10),
                const Text(
                  'SUBDIVISAO',
                  style: TextStyle(
                      fontSize: 10.5,
                      letterSpacing: 0.6,
                      color: AmColors.muted),
                ),
                const SizedBox(height: 6),
                Row(
                  children: [
                    for (final d in const [1, 2, 4, 8])
                      Expanded(
                        child: _Chip(
                          label: '1/$d',
                          selecionado: d == denominador,
                          onTap: () {
                            setSheetState(() => denominador = d);
                            final bpm = project.bpm;
                            // Ja analisado: trocar a densidade nao precisa
                            // reler o arquivo, so refazer a grade.
                            if (bpm != null) {
                              controller.setBpm(bpm, denominador: d);
                            }
                          },
                        ),
                      ),
                  ],
                ),
                const SizedBox(height: 4),
                const Text(
                  'Em compasso 4/4: 1/4 poe uma marca em cada tempo, 1/8 '
                  'duas, 1/1 uma por compasso.',
                  style: TextStyle(
                      fontSize: 11, height: 1.35, color: AmColors.muted),
                ),

                const SizedBox(height: 14),
                Row(
                  children: [
                    const SizedBox(
                      width: 110,
                      child: Text('Andamento',
                          style: TextStyle(
                              fontSize: 12, color: AmColors.muted)),
                    ),
                    Expanded(
                      child: Text(
                        project.bpm == null
                            ? 'ainda nao analisado'
                            : '${project.bpm!.toStringAsFixed(1)} bpm '
                                '· ${project.beats.length} marcas',
                        style: const TextStyle(
                            fontSize: 12.5, color: AmColors.text),
                      ),
                    ),
                    if (project.bpm != null) ...[
                      _MiniBotao(
                        label: '−',
                        onTap: () => controller.setBpm(project.bpm! - 1,
                            denominador: denominador),
                      ),
                      _MiniBotao(
                        label: '+',
                        onTap: () => controller.setBpm(project.bpm! + 1,
                            denominador: denominador),
                      ),
                    ],
                  ],
                ),

                const SizedBox(height: 16),
                GestureDetector(
                  onTap: rodando ? null : analisar,
                  child: Container(
                    height: 46,
                    alignment: Alignment.center,
                    decoration: BoxDecoration(
                      color: AmColors.accentDim,
                      borderRadius: BorderRadius.circular(11),
                    ),
                    child: Text(
                      rodando
                          ? 'Ouvindo a faixa...'
                          : (project.beats.isEmpty
                              ? 'Detectar batidas'
                              : 'Detectar de novo'),
                      style: const TextStyle(
                          fontSize: 13.5,
                          fontWeight: FontWeight.w700,
                          color: AmColors.accent),
                    ),
                  ),
                ),
                if (project.beats.isNotEmpty) ...[
                  const SizedBox(height: 8),
                  Row(
                    children: [
                      Expanded(
                        child: _Secundario(
                          label: 'Cortar nas batidas',
                          onTap: () {
                            final n =
                                controller.cutAtMarkers(usarBatidas: true);
                            AureaSnack.show(sheetContext, '$n cortes');
                          },
                        ),
                      ),
                      const SizedBox(width: 8),
                      Expanded(
                        child: _Secundario(
                          label: 'Limpar',
                          onTap: controller.clearBeats,
                        ),
                      ),
                    ],
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

class _Chip extends StatelessWidget {
  const _Chip({
    required this.label,
    required this.selecionado,
    required this.onTap,
  });

  final String label;
  final bool selecionado;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: onTap,
        child: Container(
          height: 38,
          margin: const EdgeInsets.symmetric(horizontal: 3),
          alignment: Alignment.center,
          decoration: BoxDecoration(
            color: selecionado ? AmColors.accentDim : AmColors.chip,
            borderRadius: BorderRadius.circular(9),
          ),
          child: Text(
            label,
            style: TextStyle(
              fontSize: 12.5,
              fontWeight: selecionado ? FontWeight.w700 : FontWeight.w500,
              color: selecionado ? AmColors.accent : AmColors.text,
            ),
          ),
        ),
      );
}

class _MiniBotao extends StatelessWidget {
  const _MiniBotao({required this.label, required this.onTap});

  final String label;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: onTap,
        child: Container(
          width: 38,
          height: 34,
          margin: const EdgeInsets.only(left: 6),
          alignment: Alignment.center,
          decoration: BoxDecoration(
            color: AmColors.chip,
            borderRadius: BorderRadius.circular(8),
          ),
          child: Text(label,
              style: const TextStyle(fontSize: 16, color: AmColors.text)),
        ),
      );
}

class _Secundario extends StatelessWidget {
  const _Secundario({required this.label, required this.onTap});

  final String label;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: onTap,
        child: Container(
          height: 40,
          alignment: Alignment.center,
          decoration: BoxDecoration(
            color: AmColors.chip,
            borderRadius: BorderRadius.circular(10),
          ),
          child: Text(label,
              style: const TextStyle(fontSize: 12.5, color: AmColors.text)),
        ),
      );
}

class _Linha extends StatelessWidget {
  const _Linha({
    required this.label,
    required this.value,
    required this.min,
    required this.max,
    required this.onChanged,
  });

  final String label;
  final double value;
  final double min;
  final double max;
  final ValueChanged<double> onChanged;

  @override
  Widget build(BuildContext context) => Row(
        children: [
          SizedBox(
            width: 110,
            child: Text(label,
                style:
                    const TextStyle(fontSize: 12, color: AmColors.muted)),
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
            width: 42,
            child: Text(value.toStringAsFixed(0),
                textAlign: TextAlign.right,
                style:
                    const TextStyle(fontSize: 12, color: AmColors.text)),
          ),
        ],
      );
}
