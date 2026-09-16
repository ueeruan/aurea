import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import '../../../core/ui/am_colors.dart';

import 'package:aurea/src/core/l10n/app_language.dart';

// Change this revision when publishing a new set of release notes.
const releaseNoticeRevision = '2026-09-16-beta-89';
const releaseNoticeSeenKey = 'aurea.releaseNotice.seen';

const releaseHighlights = <(IconData, String, String)>[
  (
    CupertinoIcons.textformat,
    'Texto grande nao quebra mais o app',
    'Texto muito ampliado no palco corrompia as letras do app inteiro: '
        'icones viravam quadrados e nomes sumiam ate fechar. Agora ele e '
        'desenhado sem estourar a memoria de letras da GPU.',
  ),
  (
    CupertinoIcons.cube,
    'Cena 3D mais estavel',
    'Selecionar a camada, mover o cursor ou dar play nao recriam mais o '
        'motor 3D, e modelo com muitos materiais nao pede gigas de memoria '
        'de uma vez.',
  ),
  (
    CupertinoIcons.arrow_down_doc,
    'Importar modelo falha sem derrubar',
    'Arquivo grande demais, textura acima de 8K ou GLB comprimido '
        'malformado viram uma mensagem dizendo o que fazer, e nao queda.',
  ),
  (
    CupertinoIcons.speedometer,
    'Bancada A-E',
    'Em Ajustes > Teste de estresse do motor 3D, o botao Bancada A-E mede '
        'o app no seu aparelho. Copie o relatorio e envie.',
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
