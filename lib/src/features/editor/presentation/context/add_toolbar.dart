import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';

import '../../../../core/theme/tokens.dart';
import '../am/layer_look.dart';

/// O que cada tile do E1 pede.
enum AddTarget {
  midia,
  audio,
  texto,
  forma,
  efeito,
  icone,
  grupo,
  objeto,
  legendas,
  marcas,
  batidas,
  autoEdit,
  ajuda,
}

/// E1 — NADA SELECIONADO: a barra de ADICIONAR (Blurrr/CapCut).
///
/// Icones grandes com rotulo, rolaveis. Cada um abre o seletor certo.
/// Embaixo, a linha do projeto: legendas, marcas, batidas, AutoEdit.
class AddToolbar extends StatelessWidget {
  const AddToolbar({
    super.key,
    required this.onTarget,
    required this.pro,
    this.empty = false,
  });

  final ValueChanged<AddTarget> onTarget;
  final bool pro;

  /// Sem camada nenhuma: estado vazio com a chamada.
  final bool empty;

  @override
  Widget build(BuildContext context) {
    final principais = <(AddTarget, IconData, String, Color)>[
      (
        AddTarget.midia,
        CupertinoIcons.photo_on_rectangle,
        'Mídia',
        layerKindColor(LayerKind.video),
      ),
      (
        AddTarget.audio,
        CupertinoIcons.music_note,
        'Áudio',
        layerKindColor(LayerKind.audio),
      ),
      (
        AddTarget.texto,
        CupertinoIcons.textformat,
        'Texto',
        layerKindColor(LayerKind.text),
      ),
      (
        AddTarget.forma,
        CupertinoIcons.square_on_circle,
        'Forma',
        layerKindColor(LayerKind.shape),
      ),
      (
        AddTarget.efeito,
        CupertinoIcons.wand_stars,
        'Efeito',
        layerKindColor(LayerKind.adjustment),
      ),
      (
        AddTarget.icone,
        CupertinoIcons.smiley,
        'Ícone',
        layerKindColor(LayerKind.shape),
      ),
      (
        AddTarget.grupo,
        CupertinoIcons.folder,
        'Grupo',
        layerKindColor(LayerKind.group),
      ),
      if (pro)
        (
          AddTarget.objeto,
          CupertinoIcons.cube,
          'Objeto',
          layerKindColor(LayerKind.element3d),
        ),
    ];
    final projeto = <(AddTarget, IconData, String)>[
      (AddTarget.legendas, CupertinoIcons.captions_bubble, 'Legendas'),
      (AddTarget.marcas, CupertinoIcons.bookmark, 'Marcas'),
      (AddTarget.batidas, CupertinoIcons.metronome, 'Batidas'),
      (AddTarget.ajuda, CupertinoIcons.question_circle, 'Ajuda'),
    ];
    return LayoutBuilder(
      builder: (context, c) {
        final compacto = c.maxHeight < 170;
        // Lista, nao Column: em altura pequena rola em vez de estourar.
        return ListView(
          padding: EdgeInsets.zero,
          children: [
            if (empty && !compacto)
              EstadoVazio(onMidia: () => onTarget(AddTarget.midia)),
            SizedBox(
              height: compacto ? 64 : 84,
              child: ListView.separated(
                scrollDirection: Axis.horizontal,
                padding: const EdgeInsets.symmetric(horizontal: 12),
                itemCount: principais.length,
                separatorBuilder: (_, _) => const SizedBox(width: 10),
                itemBuilder: (_, i) {
                  final (alvo, icone, rotulo, cor) = principais[i];
                  return _Tile(
                    key: ValueKey('adicionar-${alvo.name}'),
                    icone: icone,
                    rotulo: rotulo,
                    cor: cor,
                    compacto: compacto,
                    onTap: () => onTarget(alvo),
                  );
                },
              ),
            ),
            if (!compacto)
              SizedBox(
                height: 40,
                child: ListView.separated(
                  scrollDirection: Axis.horizontal,
                  padding: const EdgeInsets.symmetric(horizontal: 12),
                  itemCount: projeto.length,
                  separatorBuilder: (_, _) => const SizedBox(width: 8),
                  itemBuilder: (_, i) {
                    final (alvo, icone, rotulo) = projeto[i];
                    return _Chip(
                      key: ValueKey('projeto-${alvo.name}'),
                      icone: icone,
                      rotulo: rotulo,
                      onTap: () => onTarget(alvo),
                    );
                  },
                ),
              ),
          ],
        );
      },
    );
  }
}

class _Tile extends StatelessWidget {
  const _Tile({
    super.key,
    required this.icone,
    required this.rotulo,
    required this.cor,
    required this.onTap,
    required this.compacto,
  });

  final IconData icone;
  final String rotulo;
  final Color cor;
  final VoidCallback onTap;
  final bool compacto;

  @override
  Widget build(BuildContext context) {
    final t = AureaTokens.of(context);
    final lado = compacto ? 48.0 : 56.0;
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      child: SizedBox(
        width: 64,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Container(
              width: lado,
              height: lado,
              decoration: BoxDecoration(
                color: t.chip,
                borderRadius: BorderRadius.circular(16),
              ),
              child: Icon(
                icone,
                size: compacto ? 22 : 26,
                color: Color.lerp(cor, t.text, .45),
              ),
            ),
            const SizedBox(height: 4),
            AppText(
              rotulo,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(fontSize: 11.5, color: t.text),
            ),
          ],
        ),
      ),
    );
  }
}

class _Chip extends StatelessWidget {
  const _Chip({
    super.key,
    required this.icone,
    required this.rotulo,
    required this.onTap,
  });

  final IconData icone;
  final String rotulo;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final t = AureaTokens.of(context);
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 12),
        alignment: Alignment.center,
        decoration: BoxDecoration(
          color: t.chip,
          borderRadius: BorderRadius.circular(AureaTokens.radiusChip),
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(icone, size: 15, color: t.muted),
            const SizedBox(width: 6),
            AppText(rotulo, style: TextStyle(fontSize: 12.5, color: t.text)),
          ],
        ),
      ),
    );
  }
}

/// O ESTADO VAZIO (criterio 13 do prompt): sem camada nenhuma, o painel
/// nao fica em branco — diz o que fazer e tem o botao que faz.
///
/// Ele mora fora da barra de adicionar de proposito: a barra so aparece
/// quando se toca no "+", e o estado vazio precisa aparecer ANTES disso.
class EstadoVazio extends StatelessWidget {
  const EstadoVazio({super.key, required this.onMidia});

  final VoidCallback onMidia;

  @override
  Widget build(BuildContext context) {
    final t = AureaTokens.of(context);
    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 10, 16, 10),
      child: Row(
        children: [
          Expanded(
            child: AppText('Comece adicionando uma mídia, um texto ou uma forma.',
              key: const ValueKey('estado-vazio'),
              style: TextStyle(fontSize: 12.5, color: t.muted),
            ),
          ),
          const SizedBox(width: 8),
          GestureDetector(
            key: const ValueKey('estado-vazio-cta'),
            behavior: HitTestBehavior.opaque,
            onTap: onMidia,
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
              decoration: BoxDecoration(
                color: t.accent,
                borderRadius: BorderRadius.circular(10),
              ),
              child: AppText('+ Adicione uma mídia',
                style: TextStyle(
                  fontSize: 12.5,
                  fontWeight: FontWeight.w700,
                  color: t.onAccent,
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}
