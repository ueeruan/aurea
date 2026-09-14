import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import '../../../core/ui/am_colors.dart';
import 'package:aurea/src/core/l10n/app_language.dart';

// Change this revision when publishing a new set of release notes.
const releaseNoticeRevision = '2026-09-14-beta-79';
const releaseNoticeSeenKey = 'aurea.releaseNotice.seen';

const releaseHighlights = <(IconData, String, String)>[
  (
    CupertinoIcons.house,
    'Início e Comunidade novas',
    'Início com grade de projetos e atalhos. Comunidade no estilo feed de fotos, com a fila de criadores para ver os posts de cada pessoa.',
  ),
  (
    CupertinoIcons.wand_stars,
    'Melhorar qualidade com IA de verdade',
    'Real-ESRGAN rodando no aparelho (GPU), sem PNG por quadro, na taxa original do vídeo. Intensidade da IA e nitidez separadas.',
  ),
  (
    CupertinoIcons.timer,
    'Time Remap mais preciso',
    'O tempo do vídeo agora é calculado num núcleo em C++: curvas exatas, corte e divisão sem deslocar o tempo.',
  ),
  (
    CupertinoIcons.layers,
    'Camadas',
    'Profundidade Z sempre no painel de Posição, camada por camada. Copiar e colar efeitos entre camadas.',
  ),
  (
    CupertinoIcons.folder,
    'Projetos',
    'Apagar todos os projetos pelo menu de qualquer projeto. Arquivos .aurea abrem no iPhone. Nomes de projeto não mudam mais com o idioma.',
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
