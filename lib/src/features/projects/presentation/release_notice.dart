import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import '../../../core/ui/am_colors.dart';

import 'package:aurea/src/core/l10n/app_language.dart';

// Change this revision when publishing a new set of release notes.
const releaseNoticeRevision = '2026-09-18-beta-91';
const releaseNoticeSeenKey = 'aurea.releaseNotice.seen';

const releaseHighlights = <(IconData, String, String)>[
  (
    CupertinoIcons.sparkles,
    'O brilho parou de travar',
    'Deep Glow, Brilho, S_GlowAura e os outros da aba Glow e Luz pediam '
        'centenas de leituras de textura por pixel a cada quadro. Agora a '
        'previa paga uma fracao disso e a exportacao continua no maximo — '
        'o resultado salvo nao mudou.',
  ),
  (
    CupertinoIcons.textformat,
    'Texto arabe ligado',
    'Com animacao por letra, cada letra arabe saia solta. A forma de cada '
        'letra vem dos vizinhos, e agora o desenho mantem a ligacao.',
  ),
  (
    CupertinoIcons.lock_fill,
    'Camada bloqueada de verdade',
    'O cadeado ganhou botao no menu da camada e uma faixa com '
        'Desbloquear. Bloqueada nao anda, nao apara, nao edita e nao some '
        'por engano.',
  ),
  (
    CupertinoIcons.checkmark_seal,
    'Editor de pontos e transicoes',
    'Editar pontos voltou a desenhar e editar o mesmo caminho, e o '
        'sistema de transicoes saiu do app — projetos antigos continuam '
        'abrindo.',
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
