import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import '../../../core/ui/am_colors.dart';

import 'package:aurea/src/core/l10n/app_language.dart';

// Change this revision when publishing a new set of release notes.
const releaseNoticeRevision = '2026-09-16-beta-86';
const releaseNoticeSeenKey = 'aurea.releaseNotice.seen';

const releaseHighlights = <(IconData, String, String)>[
  (
    CupertinoIcons.pencil_outline,
    'Desenho a mao livre e animadores',
    'Caneta, pincel macio, balde e borracha desenham direto no palco, e o '
        'traco vira camada de verdade. E qualquer propriedade numerica '
        'ganha ANIMADOR automatico (onda, vaivem, rampa, sorteio, pulso) '
        'pelo menu do nome do parametro: movimento sem cravar keyframe.',
  ),
  (
    CupertinoIcons.wand_stars,
    'Vinte efeitos novos e galeria que sugere',
    'Dissolver, pena, aparecer e sumir, cortina e cortina radial, quatro '
        'repeticoes (linha, grade, circulo, espalhar), seis geradores '
        '(nuvens, xadrez, listras, pontos, estrelas, raios), meio-tom, '
        'contorno, brilho por dentro e bordas asperas. A galeria abre com '
        'Sugeridos e Recentes, e segurar um efeito mostra a ficha com '
        'ajustes prontos.',
  ),
  (
    CupertinoIcons.square_grid_2x2,
    'Projetos e midia em lote',
    'A lista de projetos busca, ordena e trabalha em lote (duplicar ou '
        'excluir varios). Criar aceita 4:3, QHD 1440p e MEDIDA LIVRE. Na '
        'galeria da certo marcar varias midias e mandar juntas ou em '
        'sequencia, com a duracao de cada imagem na barra.',
  ),
  (
    CupertinoIcons.arrow_up_doc,
    'O pacote .aurea e uma exportacao que avisa',
    'O projeto agora viaja COM as midias num arquivo .aurea, e a Aurea '
        'tambem abre .amproj e cenas em zip. A exportacao lembra seus '
        'ajustes e mostra quanto tempo falta. Mais: sons recentes com anel '
        'de escuta, fontes com busca e estrela, duracao padrao de camada '
        'nova e a tela de boas-vindas na primeira abertura.',
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
