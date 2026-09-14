import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import '../../../core/ui/am_colors.dart';

import 'package:aurea/src/core/l10n/app_language.dart';

// Change this revision when publishing a new set of release notes.
const releaseNoticeRevision = '2026-09-14-beta-80';
const releaseNoticeSeenKey = 'aurea.releaseNotice.seen';

const releaseHighlights = <(IconData, String, String)>[
  (
    CupertinoIcons.wand_stars,
    'Aprimorar com IA no clipe',
    'No painel do clipe de vídeo: tipo Vídeo real ou Animação, redução de ruído, intensidade e antes e depois no quadro. O vídeo exportado sai com o efeito. Por enquanto só no Android.',
  ),
  (
    CupertinoIcons.speedometer,
    'Câmera lenta com IA',
    'No Android, clipe lento com interpolação Movimento ganha quadros novos do RIFE na exportação. Se o vídeo já tem quadros suficientes, como em 60 ou 120 fps, usa os próprios quadros.',
  ),
  (
    CupertinoIcons.sparkles,
    'Efeitos de edit',
    'Coloring com pilhas CC prontas, one frame edits, Twitch, Shake mais forte, Time Slice e Posterize Time. Atalho Edits na galeria de efeitos.',
  ),
  (
    CupertinoIcons.layers,
    'Grupos como no Alight Motion',
    'Modo Selecionar para agrupar com toques. A caixa do grupo envolve o conteúdo, o grupo gira no próprio centro e cortar ou dividir não reinicia o que está dentro.',
  ),
  (
    CupertinoIcons.film,
    'Prévia e timeline',
    'Vídeo cortado não trava mais na prévia. A onda de áudio aparece bem visível no clipe de vídeo, e a tela cheia ocupa a tela toda.',
  ),
  (
    CupertinoIcons.cube,
    'Modelos 3D',
    'A importação é preparada em C++, abre GLB comprimido com meshopt e corrige a rotação de alguns modelos FBX.',
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
    title: const AppText(
      'O que mudou no Aurea',
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
