import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import '../../../core/ui/am_colors.dart';
import 'package:aurea/src/core/l10n/app_language.dart';

// Change this revision when publishing a new set of release notes.
const releaseNoticeRevision = '2026-09-12-beta-78';
const releaseNoticeSeenKey = 'aurea.releaseNotice.seen';

const releaseHighlights = <(IconData, String, String)>[
  (
    CupertinoIcons.globe,
    'Idiomas e controles',
    'Nove idiomas nos controles principais, em Ajustes. Toque em Z para arrastar a profundidade. Segure a camada para movê-la na timeline.',
  ),
  (
    CupertinoIcons.play_rectangle,
    'Melhorar qualidade',
    'Nova alternativa de codificação para celulares que rejeitam a exportação. Correção de cor pode funcionar sem carregar a IA.',
  ),
  (
    CupertinoIcons.cube_box,
    'Cena 3D',
    'Interface nova para celular, dicas, troca fácil de câmera, reflexos do ambiente e otimização dos modelos.',
  ),
  (
    CupertinoIcons.move,
    'Animação',
    'Rotação, vínculos com nulos e seleção com profundidade Z corrigidos. AutoKey ligado por padrão.',
  ),
  (
    CupertinoIcons.timer,
    'Velocidade e câmera lenta',
    'Time Remap com curvas e Optical Flow estão em Efeitos para controlar o tempo e suavizar movimentos.',
  ),
  (
    CupertinoIcons.checkmark_seal,
    'Exportação',
    'Ajustes no Motion Tile, nas réguas de valores e na rolagem dos painéis. Desenho livre acessível na aba Desenhar.',
  ),
];

Future<void> showReleaseNotice(BuildContext context) => showDialog<void>(
  context: context,
  builder: (dialogContext) => AlertDialog(
    key: const Key('release-notice'),
    backgroundColor: AmColors.panel,
    surfaceTintColor: Colors.transparent,
    insetPadding: const EdgeInsets.symmetric(horizontal: 20, vertical: 24),
    scrollable: true,
    title: const AppText('O que mudou no Aurea',
      style: TextStyle(
        color: AmColors.text,
        fontSize: 21,
        fontWeight: FontWeight.w700,
      ),
    ),
    content: SizedBox(
      width: 380,
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          for (final (icon, title, body) in releaseHighlights)
            Padding(
              padding: const EdgeInsets.only(bottom: 14),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Icon(icon, color: AmColors.accent, size: 19),
                  const SizedBox(width: 10),
                  Expanded(
                    child: Text.rich(
                      TextSpan(
                        children: [
                          TextSpan(
                            text: '$title: ',
                            style: const TextStyle(fontWeight: FontWeight.w700),
                          ),
                          TextSpan(text: body),
                        ],
                      ),
                      style: const TextStyle(
                        color: AmColors.text,
                        fontSize: 13,
                        height: 1.4,
                      ),
                    ),
                  ),
                ],
              ),
            ),
        ],
      ),
    ),
    actions: [
      FilledButton(
        key: const Key('release-notice-dismiss'),
        style: FilledButton.styleFrom(
          backgroundColor: AmColors.action,
          foregroundColor: AmColors.onAction,
        ),
        onPressed: () => Navigator.of(dialogContext).pop(),
        child: const AppText('Vamos editar'),
      ),
    ],
  ),
);
