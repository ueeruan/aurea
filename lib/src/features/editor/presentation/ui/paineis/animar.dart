import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/layer.dart';
import '../../context/categories/presets_do_texto.dart' show PresetsDoTexto;
import '../shell/contrato.dart';
import 'comum.dart';

/// ANIMAR (texto) — a pilha de animadores do texto: ligar, desligar,
/// apagar e acrescentar um novo. Os presets prontos (com previa) abrem na
/// folha que ja existe; o ajuste fino de cada animador mora em Efeitos
/// (o Animador de Texto e um efeito da pilha desde 20/09).
class PainelAnimar extends ConsumerWidget {
  const PainelAnimar({super.key, required this.layerId});

  final String layerId;

  static const _titulo = 'Animar';

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, layerId);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final chave = 'painel-${PainelId.animar.name}';
    if (camada is! TextLayer) {
      return PainelDePortas(
        titulo: _titulo,
        chave: chave,
        aviso: 'Esta camada não é de texto.',
        portas: const [],
      );
    }
    final c = ref.read(editorControllerProvider.notifier);
    return AureaPanel(
      titulo: _titulo,
      chave: chave,
      aoFechar: escopo.fecharPainel,
      acoes: [
        Tocavel(
          key: const ValueKey('animar-adicionar'),
          onTap: () => c.addTextAnimator(layerId),
          child: SizedBox(
            width: AureaDims.toqueConfortavel,
            height: AureaDims.cabecalhoDoPainel,
            child: Icon(
              CupertinoIcons.plus,
              size: AureaDims.iconeMd,
              color: AureaCores.destaque,
            ),
          ),
        ),
      ],
      filhos: [
        LinhaDePorta(
          rotulo: 'Presets de animação',
          icone: CupertinoIcons.wand_stars,
          aoTocar: () => mostrarAureaFolha<void>(
            context,
            titulo: 'Presets de animação',
            altura: 320,
            grande: true,
            construtor: (_) => const PresetsDoTexto(),
          ),
        ),
        if (camada.animators.isEmpty)
          const AureaAvisoDoPainel(
            texto: 'Nenhum animador. Toque em + ou escolha um preset.',
          ),
        for (final a in camada.animators)
          AureaPropertyRow.personalizada(
            // O NOME do animador e dado do projeto (preset ou renomeado).
            rotulo: a.name,
            chave: 'animador-${a.id}',
            filho: Row(
              children: [
                AureaToggle(
                  valor: a.enabled,
                  aoMudar: (_) => c.toggleTextAnimator(layerId, a.id),
                ),
                const Spacer(),
                Tocavel(
                  key: ValueKey('animador-${a.id}-apagar'),
                  onTap: () => c.removeTextAnimator(layerId, a.id),
                  child: SizedBox(
                    width: AureaDims.toqueConfortavel,
                    height: AureaDims.toqueConfortavel,
                    child: Icon(
                      CupertinoIcons.trash,
                      size: AureaDims.iconeSm + 2,
                      color: AureaCores.textoSecundario,
                    ),
                  ),
                ),
              ],
            ),
          ),
        LinhaDePorta(
          rotulo: 'Ajustar animadores em Efeitos',
          icone: CupertinoIcons.sparkles,
          aoTocar: () => escopo.abrirPainel(PainelId.efeitos),
        ),
      ],
    );
  }
}
