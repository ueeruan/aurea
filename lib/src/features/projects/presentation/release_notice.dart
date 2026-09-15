import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import '../../../core/ui/am_colors.dart';

import 'package:aurea/src/core/l10n/app_language.dart';

// Change this revision when publishing a new set of release notes.
const releaseNoticeRevision = '2026-09-15-beta-84';
const releaseNoticeSeenKey = 'aurea.releaseNotice.seen';

const releaseHighlights = <(IconData, String, String)>[
  (
    CupertinoIcons.viewfinder,
    'Rastreio 3D: motor 2.0',
    'O rastreio de câmera foi refeito do zero: motor nativo novo com trava anti-fantasma, tripé detectado sozinho, distorção de lente medida e toda análise assinada (motor, quadros, tempo). Estúdio novo: chão, origem, escala real, âncoras e objetos na superfície.',
  ),
  (
    CupertinoIcons.rectangle_split_3x1,
    'Decupar sozinho',
    'Toque em Decupar no clipe: o detector de mudança de cena corta o vídeo inteiro nos pontos certos — ou só marca na régua para revisar. E a emenda de cortes não trava mais o play: o pedaço seguinte já entra rodando.',
  ),
  (
    CupertinoIcons.videocam_fill,
    'Câmera 3D e batidas',
    'Câmera da composição ao lado do Nulo, com lente animável (dolly-zoom!). Batidas da música com porta própria no menu de marcas, compasso destacado na régua e batidas viram marcas de verdade.',
  ),
  (
    CupertinoIcons.sparkles,
    'Efeitos novos e correções',
    'Light Sweep, Saber, Lens Blur e 8-Bit. Exportar não falha mais com clipe sem áudio, a galeria mostra as mídias recentes primeiro, projetos abrem muito mais rápido e o Texto 3D aceita suas fontes importadas.',
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
