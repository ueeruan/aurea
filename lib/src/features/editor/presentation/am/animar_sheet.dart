import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/snack.dart';
import '../../../../core/ui/tocavel.dart';
import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/layer.dart';
import '../../domain/presets_de_movimento.dart';
import '../../domain/shape.dart';
import 'am_colors.dart';
import 'am_widgets.dart';

/// A FOLHA ANIMAR — os presets de motion design a um toque.
///
/// Tudo aqui crava KEYFRAMES REAIS nas trilhas de sempre, a partir do
/// cabecote: nada procedural escondido, nada que nao se edite depois no
/// painel e no editor de curvas. Um preset e um comeco, nao uma jaula.
Future<void> showAnimarSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  PlaybackController playback,
) async {
  await showParamSheet(
    context,
    title: 'Animar',
    heightFactor: 0.62,
    builder: (sheetContext) =>
        _Animar(layerId: layerId, playback: playback),
  );
}

class _Animar extends ConsumerWidget {
  const _Animar({required this.layerId, required this.playback});

  final String layerId;
  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef refDaFolha) {
    final controller = refDaFolha.read(editorControllerProvider.notifier);
    final projeto = refDaFolha.watch(editorControllerProvider);
    final layer = projeto.layerById(layerId);
    if (layer == null) return const SizedBox.shrink();
    final agora = playback.time.value;

    void feito(String texto) {
      AureaSnack.show(
        context,
        texto,
        actionLabel: 'Desfazer',
        onAction: controller.undo,
      );
    }

    final forma = layer is ShapeLayer ? layer : null;
    final retangulo = forma?.contents
        .whereType<ShapeParametric>()
        .any((i) => i.kind == ParamShapeKind.rect);
    final fill = forma?.contents.whereType<ShapeFill>().firstOrNull;

    return SafeArea(
      child: SingleChildScrollView(
        padding: const EdgeInsets.fromLTRB(18, 12, 18, 20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const AppText(
              'Keyframes de verdade, a partir do cabeçote. Tudo continua '
              'editável no painel e nas curvas.',
              style: TextStyle(
                fontSize: 11.5,
                height: 1.35,
                color: AmColors.muted,
              ),
            ),
            const SizedBox(height: 12),
            for (final p in PresetDeMovimento.values)
              _Acao(
                chave: 'animar-${p.name}',
                titulo: p.emPalavras,
                detalhe: p.explicacao,
                onTap: () {
                  controller.aplicarPresetDeMovimento(layerId, agora, p);
                  feito('${p.emPalavras}: keyframes cravados.');
                },
              ),
            if (forma != null && (retangulo ?? false)) ...[
              const SizedBox(height: 14),
              const AppText(
                'Morph da forma',
                style: TextStyle(
                  fontSize: 13,
                  fontWeight: FontWeight.w700,
                  color: AmColors.text,
                ),
              ),
              const SizedBox(height: 3),
              const AppText(
                'Tamanho e arredondamento animam com overshoot — o canto '
                'nunca deforma, porque é a conta da forma, não a escala.',
                style: TextStyle(
                  fontSize: 11,
                  height: 1.35,
                  color: AmColors.muted,
                ),
              ),
              const SizedBox(height: 8),
              for (final f in FormaRapida.values)
                _Acao(
                  chave: 'animar-forma-${f.name}',
                  titulo: f.emPalavras,
                  detalhe: 'Meio segundo, editável keyframe a keyframe.',
                  onTap: () {
                    controller.morphRapidoDeForma(layerId, agora, f);
                    feito('${f.emPalavras}: keyframes cravados.');
                  },
                ),
            ],
            if (forma != null) ...[
              const SizedBox(height: 14),
              const AppText(
                'Auto Morph',
                style: TextStyle(
                  fontSize: 13,
                  fontWeight: FontWeight.w700,
                  color: AmColors.text,
                ),
              ),
              const SizedBox(height: 3),
              const AppText(
                'A forma atual vira outra: os caminhos são reamostrados e '
                'casados sozinhos, e o progresso anima por keyframe.',
                style: TextStyle(
                  fontSize: 11,
                  height: 1.35,
                  color: AmColors.muted,
                ),
              ),
              const SizedBox(height: 8),
              Wrap(
                spacing: 6,
                runSpacing: 6,
                children: [
                  for (final alvo in const [
                    ShapePrimitive.ellipse,
                    ShapePrimitive.star,
                    ShapePrimitive.heart,
                    ShapePrimitive.flower,
                    ShapePrimitive.arrow,
                    ShapePrimitive.sparkle,
                  ])
                    Tocavel(
                      key: ValueKey('animar-automorph-${alvo.name}'),
                      onTap: () {
                        controller.autoMorphPara(layerId, alvo, agora);
                        feito('Auto Morph: progresso 0 → 1 cravado.');
                      },
                      child: Container(
                        padding: const EdgeInsets.symmetric(
                          horizontal: 12,
                          vertical: 10,
                        ),
                        decoration: BoxDecoration(
                          color: AmColors.chip,
                          borderRadius: BorderRadius.circular(10),
                        ),
                        child: AppText(
                          switch (alvo) {
                            ShapePrimitive.ellipse => 'Círculo',
                            ShapePrimitive.star => 'Estrela',
                            ShapePrimitive.heart => 'Coração',
                            ShapePrimitive.flower => 'Flor',
                            ShapePrimitive.arrow => 'Seta',
                            ShapePrimitive.sparkle => 'Brilho',
                            _ => alvo.name,
                          },
                          style: const TextStyle(
                            fontSize: 12,
                            color: AmColors.text,
                          ),
                        ),
                      ),
                    ),
                ],
              ),
              if (fill != null) ...[
                const SizedBox(height: 12),
                _Acao(
                  chave: 'animar-cor-keyframe',
                  titulo: fill.corAnimada &&
                          fill.corR!.hasKeyframeAt(
                            layer.localTime(agora),
                          )
                      ? 'Tirar o keyframe de cor daqui'
                      : 'Keyframe da cor do preenchimento aqui',
                  detalhe:
                      'Crava a cor atual neste instante; mude a cor em '
                      'outro tempo e o preenchimento faz o morph sozinho.',
                  onTap: () {
                    controller.toggleShapeFillColorKeyframe(
                      layerId,
                      fill.id,
                      agora,
                    );
                    feito('Cor do preenchimento: losango alternado.');
                  },
                ),
              ],
            ],
          ],
        ),
      ),
    );
  }
}

class _Acao extends StatelessWidget {
  const _Acao({
    required this.chave,
    required this.titulo,
    required this.detalhe,
    required this.onTap,
  });

  final String chave;
  final String titulo;
  final String detalhe;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(bottom: 8),
    child: Tocavel(
      key: ValueKey(chave),
      onTap: onTap,
      child: Container(
        width: double.infinity,
        padding: const EdgeInsets.symmetric(horizontal: 13, vertical: 11),
        decoration: BoxDecoration(
          color: AmColors.chip,
          borderRadius: BorderRadius.circular(12),
        ),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            AppText(
              titulo,
              style: const TextStyle(
                fontSize: 13,
                fontWeight: FontWeight.w700,
                color: AmColors.text,
              ),
            ),
            const SizedBox(height: 2),
            AppText(
              detalhe,
              style: const TextStyle(fontSize: 11, color: AmColors.muted),
            ),
          ],
        ),
      ),
    ),
  );
}
