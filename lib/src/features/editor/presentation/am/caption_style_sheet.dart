import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../domain/caption_highlight.dart';
import '../../domain/layer.dart';
import 'am_colors.dart';
import 'am_widgets.dart';
import 'color_picker_sheet.dart';

/// AS TRES PROFUNDIDADES DO ESTILO DESTAQUE.
///
/// Pronto: cinco presets, um toque. Montar: as quatro coisas que se muda
/// de verdade. Avancado: a ficha inteira. Subir nunca perde o que foi
/// feito embaixo — e a mesma regra 2 dos efeitos.
enum _Prof { pronto, montar, avancado }

Future<void> showCaptionStyleSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) async {
  var prof = _Prof.pronto;

  await showParamSheet(
    context,
    title: 'Estilo da legenda',
    heightFactor: 0.62,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        final projeto = ref.watch(editorControllerProvider);
        final camada = projeto.layerById(layerId);
        if (camada is! CaptionLayer) return const SizedBox.shrink();
        final h = camada.highlight;
        final controller = ref.read(editorControllerProvider.notifier);

        void edita(CaptionHighlightStyle Function(CaptionHighlightStyle) fn) {
          controller.updateCaptionHighlight(layerId, fn);
          setSheetState(() {});
        }

        // SEM TEMPO POR PALAVRA nao ha o que destacar: a legenda foi
        // gerada em frases. Falhar com dignidade e dizer isso, nao
        // mostrar um controle que nao faz nada.
        final temPalavras = camada.cues.length > 1 &&
            camada.cues.every((c) => c.end - c.start < const Duration(seconds: 2));

        return SafeArea(
          child: ListView(
            padding: EdgeInsets.fromLTRB(
              16,
              12,
              16,
              14 + MediaQuery.of(sheetContext).viewInsets.bottom,
            ),
            children: [
              if (!temPalavras)
                const Padding(
                  padding: EdgeInsets.only(bottom: 12),
                  child: Text(
                    'Esta legenda foi gerada em frases. O destaque precisa '
                    'do tempo por palavra — gere de novo no modo Palavra '
                    'por palavra.',
                    style: TextStyle(
                      fontSize: 12,
                      height: 1.35,
                      color: AmColors.muted,
                    ),
                  ),
                ),

              // ------------------------------------------------ PRONTO
              const _Rotulo('PRONTO'),
              Wrap(
                spacing: 6,
                runSpacing: 6,
                children: [
                  _Chip(
                    rotulo: 'Comum',
                    aceso: !h.ativo,
                    onTap: () => edita((_) => const CaptionHighlightStyle()),
                  ),
                  for (final (nome, preset) in HighlightPresets.todos)
                    _Chip(
                      rotulo: nome,
                      aceso: h.ativo &&
                          h.layout == preset.layout &&
                          h.corDestaque == preset.corDestaque,
                      onTap: () => edita((_) => preset),
                    ),
                ],
              ),
              const SizedBox(height: 8),
              _CaminhoRow(
                prof: prof,
                onProf: (p) => setSheetState(() => prof = p),
              ),

              // ------------------------------------------------ MONTAR
              if (prof != _Prof.pronto && h.ativo) ...[
                const SizedBox(height: 14),
                const _Rotulo('COR DO DESTAQUE'),
                Row(
                  children: [
                    _Amostra(
                      cor: h.corDestaque,
                      onTap: () async {
                        final nova = await showColorPicker(
                          context,
                          initial: h.corDestaque,
                        );
                        if (nova != null) edita((s) => s.copyWith(corDestaque: nova));
                      },
                    ),
                    const SizedBox(width: 10),
                    Expanded(
                      child: Text(
                        'A palavra dita entra nesta cor; as vizinhas ficam '
                        'na cor do contexto.',
                        style: const TextStyle(
                          fontSize: 11,
                          height: 1.3,
                          color: AmColors.muted,
                        ),
                      ),
                    ),
                  ],
                ),

                const SizedBox(height: 14),
                const _Rotulo('TAMANHO DA PALAVRA'),
                AmTickRuler(
                  value: h.destaque * 100,
                  min: 100,
                  max: 320,
                  unitsPerPixel: 0.55,
                  height: 42,
                  onChanged: (v) => edita((s) => s.copyWith(destaque: v / 100)),
                ),
                Text(
                  h.destaque <= 1.001
                      ? '100% — igual ao contexto, vira legenda comum'
                      : '${(h.destaque * 100).round()}% do tamanho do contexto',
                  style: const TextStyle(fontSize: 11, color: AmColors.muted),
                ),

                const SizedBox(height: 14),
                const _Rotulo('LAYOUT'),
                Wrap(
                  spacing: 6,
                  runSpacing: 6,
                  children: [
                    for (final l in HighlightLayout.values)
                      _Chip(
                        rotulo: l.rotulo,
                        aceso: h.layout == l,
                        onTap: () => edita((s) => s.copyWith(layout: l)),
                      ),
                  ],
                ),

                const SizedBox(height: 14),
                // ATRAS DA PESSOA: a mascara de segmentacao ainda nao
                // existe no aplicativo. O interruptor fica visivel e
                // esmaecido COM O MOTIVO — nada inerte em silencio.
                Opacity(
                  opacity: 0.4,
                  child: Row(
                    children: [
                      const Icon(CupertinoIcons.person_crop_rectangle,
                          size: 16, color: AmColors.muted),
                      const SizedBox(width: 8),
                      const Expanded(
                        child: Text(
                          'Atras da pessoa',
                          style: TextStyle(fontSize: 13, color: AmColors.text),
                        ),
                      ),
                      const Text(
                        'sem mascara de segmentacao',
                        style: TextStyle(fontSize: 10, color: AmColors.muted),
                      ),
                    ],
                  ),
                ),
              ],

              // ---------------------------------------------- AVANCADO
              if (prof == _Prof.avancado && h.ativo) ...[
                const SizedBox(height: 16),
                const _Rotulo('CAIXA'),
                Row(
                  children: [
                    _Chip(
                      rotulo: 'MAIUSCULAS',
                      aceso: h.maiusculas,
                      onTap: () => edita((s) => s.copyWith(maiusculas: true)),
                    ),
                    const SizedBox(width: 6),
                    _Chip(
                      rotulo: 'minusculas',
                      aceso: !h.maiusculas,
                      onTap: () => edita((s) => s.copyWith(maiusculas: false)),
                    ),
                  ],
                ),

                const SizedBox(height: 14),
                const _Rotulo('TRACKING'),
                AmTickRuler(
                  value: h.tracking,
                  min: -6,
                  max: 8,
                  unitsPerPixel: 0.04,
                  height: 40,
                  onChanged: (v) => edita((s) => s.copyWith(tracking: v)),
                ),

                const SizedBox(height: 12),
                const _Rotulo('ENTRELINHA'),
                AmTickRuler(
                  value: h.entrelinha * 100,
                  min: 70,
                  max: 180,
                  unitsPerPixel: 0.4,
                  height: 40,
                  onChanged: (v) => edita((s) => s.copyWith(entrelinha: v / 100)),
                ),

                const SizedBox(height: 12),
                const _Rotulo('DURACAO DO INFLAR'),
                AmTickRuler(
                  value: h.duracaoInflar.inMilliseconds.toDouble(),
                  min: 60,
                  max: 600,
                  unitsPerPixel: 1.6,
                  height: 40,
                  onChanged: (v) => edita((s) => s.copyWith(
                      duracaoInflar: Duration(milliseconds: v.round()))),
                ),

                const SizedBox(height: 12),
                const _Rotulo('PALAVRAS DE CONTEXTO, DE CADA LADO'),
                Row(
                  children: [
                    for (var n = 0; n <= kMaxContextoPorLado; n++) ...[
                      _Chip(
                        rotulo: '$n',
                        aceso: h.contextoPorLado == n,
                        onTap: () => edita((s) => s.copyWith(contextoPorLado: n)),
                      ),
                      const SizedBox(width: 6),
                    ],
                  ],
                ),
                const SizedBox(height: 6),
                const Text(
                  'A fonte do destaque e a do contexto saem da lista de '
                  'fontes importadas (secao Fonte). Quem tiver a licenca '
                  'da original usa a original.',
                  style: TextStyle(
                    fontSize: 11,
                    height: 1.35,
                    color: AmColors.muted,
                  ),
                ),
              ],
            ],
          ),
        );
      },
    ),
  );
}

class _CaminhoRow extends StatelessWidget {
  const _CaminhoRow({required this.prof, required this.onProf});

  final _Prof prof;
  final ValueChanged<_Prof> onProf;

  @override
  Widget build(BuildContext context) => Row(
        children: [
          if (prof == _Prof.pronto)
            _Chip(rotulo: 'Ajustar', onTap: () => onProf(_Prof.montar))
          else ...[
            _Chip(rotulo: 'Pronto', onTap: () => onProf(_Prof.pronto)),
            const SizedBox(width: 6),
            if (prof == _Prof.montar)
              _Chip(rotulo: 'Avancado', onTap: () => onProf(_Prof.avancado)),
          ],
        ],
      );
}

class _Rotulo extends StatelessWidget {
  const _Rotulo(this.texto);

  final String texto;

  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.only(bottom: 6),
        child: Text(
          texto,
          style: const TextStyle(
            fontSize: 10,
            fontWeight: FontWeight.w700,
            letterSpacing: 0.8,
            color: AmColors.muted,
          ),
        ),
      );
}

class _Chip extends StatelessWidget {
  const _Chip({required this.rotulo, required this.onTap, this.aceso = false});

  final String rotulo;
  final VoidCallback onTap;
  final bool aceso;

  @override
  Widget build(BuildContext context) => GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: onTap,
        child: Container(
          height: 32,
          padding: const EdgeInsets.symmetric(horizontal: 12),
          alignment: Alignment.center,
          decoration: BoxDecoration(
            color: aceso ? AmColors.accentDim : AmColors.chip,
            borderRadius: BorderRadius.circular(9),
          ),
          child: Text(
            rotulo,
            style: TextStyle(
              fontSize: 12,
              fontWeight: FontWeight.w600,
              color: aceso ? AmColors.accent : AmColors.text,
            ),
          ),
        ),
      );
}

class _Amostra extends StatelessWidget {
  const _Amostra({required this.cor, required this.onTap});

  final Color cor;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => GestureDetector(
        onTap: onTap,
        child: Container(
          width: 44,
          height: 32,
          decoration: BoxDecoration(
            color: cor,
            borderRadius: BorderRadius.circular(9),
          ),
        ),
      );
}
