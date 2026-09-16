import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import '../../../core/ui/am_colors.dart';

import 'package:aurea/src/core/l10n/app_language.dart';

// Change this revision when publishing a new set of release notes.
const releaseNoticeRevision = '2026-09-16-beta-88';
const releaseNoticeSeenKey = 'aurea.releaseNotice.seen';

const releaseHighlights = <(IconData, String, String)>[
  (
    CupertinoIcons.color_filter,
    'Efeitos de cor com a conta do After',
    'Unsharp Mask, Levels, Brightness & Contrast, Hue/Saturation e '
        'Exposure. Efeitos de cor seguidos viram uma passada so na GPU, '
        'e a Exposure trabalha em luz linear, como no AE.',
  ),
  (
    CupertinoIcons.paintbrush,
    'Aba Estilizar: 12 efeitos',
    'CC Threshold, Threshold RGB, Vignette e Block Load; S_ScanLines, '
        'HalfTone, EdgeColorize, JpegDamage, PixelSort, AutoPaint, '
        'TVDamage e VHSDamage. Medidos contra renders do After Effects.',
  ),
  (
    CupertinoIcons.wand_stars,
    'Aba Distorcer: 8 efeitos',
    'CC Lens, Optics Compensation, Turbulent Displace, S_Shake, '
        'S_DissolveShake, Glitchify, Twitch e Cross Glitch.',
  ),
  (
    CupertinoIcons.slider_horizontal_3,
    'A ficha do efeito na planta do AM',
    'Cada efeito e um cartao com nome, ••• e lixeira; cada parametro tem '
        'nome, regua e caixa de valor que abre o teclado. Ligar, duplicar, '
        'ordem e resetar ficam no •••.',
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
