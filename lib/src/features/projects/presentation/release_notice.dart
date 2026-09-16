import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import '../../../core/ui/am_colors.dart';

import 'package:aurea/src/core/l10n/app_language.dart';

// Change this revision when publishing a new set of release notes.
const releaseNoticeRevision = '2026-09-16-beta-87';
const releaseNoticeSeenKey = 'aurea.releaseNotice.seen';

const releaseHighlights = <(IconData, String, String)>[
  (
    CupertinoIcons.rectangle_grid_2x2,
    'O editor desafogou',
    'A barra que flutuava por cima da linha do tempo foi embora: tudo '
        'o que ela fazia ja tinha porta a um toque, e ela so tapava os '
        'clipes. Duplicar subiu para a barra da camada. E as fichas do '
        'painel agora sao TRES por fileira — as sete cabem sem rolar.',
  ),
  (
    CupertinoIcons.waveform,
    'O tremor finalmente treme',
    'Ele nunca esteve com a conta errada: os EIXOS nasciam em 0,2 e '
        '0,1, entao "Amplitude 1" entregava 12 px — invisivel. O eixo '
        'nasce cheio agora e a Amplitude vale o que diz: o padrao saiu '
        'de 6 px para 32 px de balanco em 1080p, medido.',
  ),
  (
    CupertinoIcons.sparkles,
    'As particulas enxergam a lente',
    'A nuvem projetava com uma lente propria e nao sabia que existia '
        'camera: numa grande-angular a cena abria e as particulas '
        'ficavam paradas, como adesivo num vidro. Agora a profundidade '
        'da nuvem abre e fecha junto com o resto da cena.',
  ),
  (
    CupertinoIcons.globe,
    'Consertos que voces apontaram',
    'A aba de som quebrava assim que havia um som recente; o numero da '
        'ordem no lote da galeria ficava ATRAS da miniatura e nunca '
        'aparecia; e o painel de adicionar falava duas linguas — quatro '
        'rotulos nunca tinham sido traduzidos.',
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
