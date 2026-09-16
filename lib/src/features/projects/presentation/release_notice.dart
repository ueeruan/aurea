import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import '../../../core/ui/am_colors.dart';

import 'package:aurea/src/core/l10n/app_language.dart';

// Change this revision when publishing a new set of release notes.
const releaseNoticeRevision = '2026-09-16-beta-90';
const releaseNoticeSeenKey = 'aurea.releaseNotice.seen';

const releaseHighlights = <(IconData, String, String)>[
  (
    CupertinoIcons.play_circle,
    'Previa mais leve',
    'Texto animado, curvas de keyframe e efeitos sem keyframe deixaram de '
        'refazer trabalho a cada quadro, e o pontilhado pausa enquanto toca.',
  ),
  (
    CupertinoIcons.sparkles,
    'Glow e Luz e Diversos',
    'Chroma Key, S_Rays, Deep Glow, Brilho, S_SpotLight, S_Glint, '
        'S_GlintRainbow, S_GlowRings, S_EdgeRays, S_GlowAura e S_GlowDarks, '
        'conferidos contra o After Effects. Motion Tile voltou.',
  ),
  (
    CupertinoIcons.captions_bubble,
    'Legenda viral e batidas',
    'Novo estilo Viral nas legendas automaticas, e as batidas da musica '
        'viram marcadores na timeline.',
  ),
  (
    CupertinoIcons.checkmark_seal,
    'Correcoes do beta',
    'Legenda automatica nao fecha mais o app no iPhone, a musica nao '
        'balanca mais o relogio da previa e a selecao de varias camadas '
        'deixa as faixas a vista.',
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
