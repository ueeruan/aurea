import 'package:flutter/cupertino.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/ui/snack.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/layer.dart';
import '../shell/contrato.dart';
import 'comum.dart';
import 'comum_de_objetos.dart';

/// GRUPO — entrar (o grupo abre como a composicao), desagrupar, e o TEMPO
/// PROPRIO do grupo (precomp): duracao interna, colapsar e recortar.
///
/// Com o tempo proprio o grupo deixa de ser so organizacao e vira uma
/// composicao dentro da composicao. O remapeamento de tempo saiu do app:
/// projeto antigo que o tinha ligado ve so o interruptor para desligar.
class PainelGrupo extends ConsumerWidget {
  const PainelGrupo({super.key, required this.layerId});

  final String layerId;

  static const _titulo = 'Grupo';

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, layerId);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final chave = 'painel-${PainelId.grupo.name}';
    if (camada is! GroupLayer) {
      return PainelDeTipoErrado(
        titulo: _titulo,
        chave: chave,
        aviso: 'Esta camada não é um grupo.',
      );
    }
    final c = ref.read(editorControllerProvider.notifier);
    final segundosInternos = camada.innerDuration.inMicroseconds / 1e6;
    final segundosDaBarra = camada.duration.inMicroseconds / 1e6;
    return AureaPanel(
      titulo: _titulo,
      chave: chave,
      aoFechar: escopo.fecharPainel,
      filhos: [
        LinhaDeAcao(
          key: const ValueKey('grupo-entrar'),
          rotulo: 'Entrar no grupo',
          icone: CupertinoIcons.arrow_down_right_square,
          aoTocar: () {
            HapticFeedback.lightImpact();
            escopo.playback.pause();
            escopo.fecharPainel();
            c.enterGroup(layerId);
          },
        ),
        LinhaDeAcao(
          key: const ValueKey('grupo-desagrupar'),
          rotulo: 'Desagrupar',
          icone: CupertinoIcons.square_split_2x2,
          aoTocar: () {
            HapticFeedback.lightImpact();
            escopo.playback.pause();
            escopo.fecharPainel();
            final avisos = c.ungroupLayer(layerId);
            if (avisos.isNotEmpty && context.mounted) {
              AureaSnack.show(context, avisos.join('\n'));
            }
          },
        ),
        linhaNumerica(
          ref,
          rotulo: 'Duração interna',
          chave: 'grupo-duracao',
          valor: segundosInternos,
          min: .1,
          max: 600,
          casas: 2,
          unidade: 's',
          aoMudar: (v) => c.updatePrecomp(
            layerId,
            sourceDuration: Duration(microseconds: (v * 1e6).round()),
          ),
        ),
        if (camada.sourceDuration != null)
          LinhaDeAcao(
            key: const ValueKey('grupo-igualar'),
            rotulo: 'Igualar à barra (${segundosDaBarra.toStringAsFixed(2)} s)',
            icone: CupertinoIcons.arrow_left_right,
            aoTocar: () => c.updatePrecomp(layerId, clearSourceDuration: true),
          ),
        if (camada.timeRemap != null)
          linhaDeLigar(
            rotulo: 'Remapear tempo',
            chave: 'grupo-remapear',
            valor: true,
            aoMudar: (v) {
              if (!v) c.updatePrecomp(layerId, clearRemap: true);
            },
          ),
        // SEM QUADRO PROPRIO os filhos compoem direto com o pai — e a forma
        // vetorial nao pixela ao ampliar.
        linhaDeLigar(
          rotulo: 'Colapsar',
          chave: 'grupo-colapsar',
          valor: camada.collapse,
          aoMudar: (v) => c.updatePrecomp(layerId, collapse: v),
        ),
        linhaDeLigar(
          rotulo: 'Recortar no quadro',
          chave: 'grupo-recortar',
          valor: camada.clipToComp,
          aoMudar: (v) => c.updatePrecomp(layerId, clipToComp: v),
        ),
      ],
    );
  }
}
