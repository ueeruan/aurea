import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import '../../../core/ui/am_colors.dart';

import 'package:aurea/src/core/l10n/app_language.dart';

// Change this revision when publishing a new set of release notes.
const releaseNoticeRevision = '2026-09-14-beta-83';
const releaseNoticeSeenKey = 'aurea.releaseNotice.seen';

const releaseHighlights = <(IconData, String, String)>[
  (
    CupertinoIcons.speedometer,
    'Estúdio do Tempo',
    'O Time Remap novo: gráficos de valor e velocidade editados com o dedo, rampas, congelar, reverso, presets com miniatura e câmera lenta com IA.',
  ),
  (
    CupertinoIcons.viewfinder,
    'Cena 3D no vídeo',
    'Rastreie a câmera do clipe, toque numa superfície e pouse texto, âncoras e modelos 3D — com chão, origem e escala real (100 unidades = 1 m).',
  ),
  (
    CupertinoIcons.film,
    'Look de cinema',
    'Um toque cria a pilha de filme: grade de cor, grão, bloom, halation e vinheta numa camada de ajuste, tudo editável peça a peça.',
  ),
  (
    CupertinoIcons.bolt_fill,
    'AMV e social',
    'Impactos na batida, cortar nas batidas, whip, punch in, fundo desfocado, separar áudio e presets Animar — tudo keyframe de verdade.',
  ),
  (
    CupertinoIcons.circle_grid_hex,
    'Formas que viram outras',
    'Quadrado → círculo → pílula → card sem deformar canto, Auto Morph entre formas e cor de preenchimento animada.',
  ),
  (
    CupertinoIcons.hand_draw,
    'Mais fluido',
    'Todo botão responde ao toque no estilo iOS, alvos maiores, sem a borda no preview e mídia importada em tela cheia.',
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
