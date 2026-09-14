import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import '../../../core/ui/am_colors.dart';

import 'package:aurea/src/core/l10n/app_language.dart';

// Change this revision when publishing a new set of release notes.
const releaseNoticeRevision = '2026-09-14-beta-82';
const releaseNoticeSeenKey = 'aurea.releaseNotice.seen';

const releaseHighlights = <(IconData, String, String)>[
  (
    CupertinoIcons.viewfinder,
    'Tracker 3D novo',
    'Rastrear no clipe de vídeo: motor novo em C++ que resolve chão plano e tripé, calcula a lente e dá pose a todo quadro. A câmera acompanha velocidade e reverso.',
  ),
  (
    CupertinoIcons.textformat_alt,
    'Texto 3D de metal',
    'Adicionar > Objeto > Texto 3D: letras com chanfro em ouro, cromo ou aço escovado, num estúdio feito para metal. O texto segue um nulo na linha do tempo.',
  ),
  (
    CupertinoIcons.sparkles,
    'Efeitos com prévia de verdade',
    'A galeria mostra cada efeito animado numa foto. Novos: Looks com força, S_Sharpen, S_Flicker, S_MathOps, Hue/Saturation e Film Damage 2.',
  ),
  (
    CupertinoIcons.fullscreen,
    'Mídia em tela cheia',
    'Foto e vídeo importados cobrem a composição. Preencher e Ajustar no painel de Escala, e a mesclagem não encolhe mais a camada.',
  ),
  (
    CupertinoIcons.hand_draw,
    'Keyframes e limpeza',
    'Segure e arraste o keyframe na linha do tempo. Partículas de volta; saíram a aba Presets e o aviso de camada apagada.',
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
