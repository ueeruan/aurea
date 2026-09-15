import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import '../../../core/ui/am_colors.dart';

import 'package:aurea/src/core/l10n/app_language.dart';

// Change this revision when publishing a new set of release notes.
const releaseNoticeRevision = '2026-09-15-beta-85';
const releaseNoticeSeenKey = 'aurea.releaseNotice.seen';

const releaseHighlights = <(IconData, String, String)>[
  (
    CupertinoIcons.wand_rays,
    'Aba PRESETS na galeria',
    '16 presets de um toque: o bundle 4nas.ftbl inteiro convertido pro motor (Main CC, Cold, Aura, Gorgeous, shakes, zooms, Twixtor…) e o novo Impact Flow — entra em disparada, freia em câmera lenta com optical flow e sai acelerando, pronto pros clipes decupados.',
  ),
  (
    CupertinoIcons.slider_horizontal_3,
    'Editor numa língua só',
    'Som, Partículas, Grade, Máscara, Elemento 3D, Formas, Precomp, Legenda e Estilos falam as linhas novas: a linha inteira arrasta, o número digita valor exato e o losango de keyframe mora na própria linha. Na Início, o cartão "Continuar editando" e a barra com blur ao rolar.',
  ),
  (
    CupertinoIcons.textformat_alt,
    'Texto 3D animado e iPhone 3D',
    'Os presets de animação do texto normal agora valem letra a letra no Texto 3D (entrada, ênfase e saída). E o novo iPhone 3D entra com corpo, tela e lentes editáveis — a tela aceita sua imagem.',
  ),
  (
    CupertinoIcons.checkmark_seal,
    'Consertos dos seus relatos',
    'Keyframes não escapam mais do lugar ao aparar vídeo/áudio; mídia importada com nome-hash vira "Vídeo 1"; rotações 3D coerentes entre camadas sob a câmera; e botões que "não respondiam" (um enfeite roubava o toque) corrigidos na raiz.',
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
