import 'package:flutter/cupertino.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/snack.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../application/editor_controller.dart';
import '../../../application/info_da_midia.dart';
import '../../../domain/layer.dart';
import '../../../domain/layout_ops.dart';

// ===========================================================================
// ACOES DE MIDIA E DE ESTILO DO MENU DA CAMADA
// ===========================================================================
//
// A LOGICA veio intacta do menu antigo (`shell/menu_da_camada.dart`):
// colar estilo escolhendo o que vai, a ficha da midia e extrair o audio
// com o aviso. O controlador ja faz cada uma num desfazer so
// (`colarEstilo` e `extrairAudioDaCamada` rodam em `runAsOneUndo` por
// dentro); aqui so mora a conversa com a pessoa, no design system.

/// COLAR ESTILO: escolher o que vai. So aparece o que faz sentido entre
/// as duas camadas; tudo nasce marcado.
Future<void> mostrarColarEstilo(
  BuildContext context,
  WidgetRef ref,
  String destinoId,
) async {
  final c = ref.read(editorControllerProvider.notifier);
  final possiveis = c.categoriasColaveis(destinoId);
  if (possiveis.isEmpty) {
    AureaSnack.show(
      context,
      translate(context, 'Copie o estilo de outra camada primeiro'),
    );
    return;
  }
  final escolhidas = {...possiveis};
  final n = await mostrarAureaFolha<int>(
    context,
    titulo: 'Colar estilo',
    construtor: (folha) => StatefulBuilder(
      builder: (folha, setFolha) => Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          // A LISTA ROLA, os botoes nao: numa tela baixa com as oito
          // categorias, "Colar" nao pode sumir para baixo da borda.
          Flexible(
            child: ListView(
              key: const ValueKey('colar-estilo-folha'),
              shrinkWrap: true,
              padding: const EdgeInsets.symmetric(
                horizontal: AureaDims.margemDoPainel,
              ),
              children: [
                for (final cat in CategoriaDeEstilo.values)
                  if (possiveis.contains(cat))
                    _LinhaDeMarcar(
                      chave: 'colar-estilo-${cat.name}',
                      icone: _iconeDaCategoria(cat),
                      rotulo: rotuloDaCategoriaDeEstilo(cat),
                      marcada: escolhidas.contains(cat),
                      aoTocar: () => setFolha(() {
                        if (!escolhidas.remove(cat)) escolhidas.add(cat);
                      }),
                    ),
              ],
            ),
          ),
          Padding(
            padding: const EdgeInsets.fromLTRB(
              AureaDims.margemDoPainel,
              AureaDims.e10,
              AureaDims.margemDoPainel,
              AureaDims.topoDoPainel,
            ),
            child: Row(
              children: [
                Expanded(
                  child: CupertinoButton(
                    key: const ValueKey('colar-estilo-cancelar'),
                    color: AureaCores.campo,
                    borderRadius: BorderRadius.circular(AureaDims.raioXl),
                    onPressed: () => Navigator.of(folha).pop(),
                    child: AppText(
                      'Cancelar',
                      style: AureaEstilos.corpo.copyWith(fontSize: 15),
                    ),
                  ),
                ),
                const SizedBox(width: AureaDims.e10),
                Expanded(
                  child: CupertinoButton(
                    key: const ValueKey('colar-estilo-colar'),
                    color: AureaCores.acao,
                    disabledColor: AureaCores.campoAlto,
                    borderRadius: BorderRadius.circular(AureaDims.raioXl),
                    // O COLAR ACONTECE AQUI, com a folha aberta, e a folha
                    // devolve quantas categorias entraram — o aviso so sai
                    // quando algo mudou de fato.
                    onPressed: escolhidas.isEmpty
                        ? null
                        : () => Navigator.of(
                            folha,
                          ).pop(c.colarEstilo(destinoId, escolhidas)),
                    child: AppText(
                      'Colar',
                      style: AureaEstilos.corpo.copyWith(
                        fontSize: 15,
                        fontWeight: FontWeight.w700,
                        color: AureaCores.sobreAcao,
                      ),
                    ),
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    ),
  );
  if (n != null && n > 0 && context.mounted) {
    AureaSnack.show(context, translate(context, 'Estilo colado'));
  }
}

IconData _iconeDaCategoria(CategoriaDeEstilo cat) => switch (cat) {
  CategoriaDeEstilo.corEPreenchimento => CupertinoIcons.drop_fill,
  CategoriaDeEstilo.bordaESombra => CupertinoIcons.square_on_square,
  CategoriaDeEstilo.mesclagemEOpacidade => CupertinoIcons.circle_lefthalf_fill,
  CategoriaDeEstilo.moverETransformar => CupertinoIcons.move,
  CategoriaDeEstilo.estiloDeTexto => CupertinoIcons.textformat,
  CategoriaDeEstilo.volume => CupertinoIcons.speaker_2,
  CategoriaDeEstilo.efeitos => CupertinoIcons.sparkles,
  CategoriaDeEstilo.velocidade => CupertinoIcons.speedometer,
};

/// UMA ESCOLHA QUE LIGA E DESLIGA numa lista: a linha INTEIRA e o alvo
/// (nao so o visto), como no menu antigo. Item de lista de 37 + respiro,
/// o visto no destaque a direita; desmarcada, a linha so apaga o icone.
class _LinhaDeMarcar extends StatelessWidget {
  const _LinhaDeMarcar({
    required this.chave,
    required this.icone,
    required this.rotulo,
    required this.marcada,
    required this.aoTocar,
  });

  final String chave;
  final IconData icone;
  final String rotulo;
  final bool marcada;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => Tocavel(
    key: ValueKey(chave),
    encolhe: 1,
    onTap: () {
      HapticFeedback.selectionClick();
      aoTocar();
    },
    child: SizedBox(
      height: AureaDims.itemDeLista + AureaDims.e6,
      child: Row(
        children: [
          Icon(
            icone,
            size: AureaDims.iconeSm + 2,
            color: marcada ? AureaCores.destaque : AureaCores.textoSecundario,
          ),
          const SizedBox(width: AureaDims.e10),
          Expanded(
            child: AppText(
              rotulo,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: AureaEstilos.corpo,
            ),
          ),
          Icon(
            CupertinoIcons.checkmark_alt,
            size: AureaDims.iconeSm + 2,
            color: AureaCores.destaque.withValues(alpha: marcada ? 1 : 0),
          ),
        ],
      ),
    ),
  );
}

/// A FICHA DA MIDIA: nome, dimensoes, quadros, duracao, formato, tamanho
/// e amostragem.
///
/// [nome] e o nome da CAMADA; a ficha mostra o do ARQUIVO (quem lê a
/// ficha quer saber o que esta no disco), exatamente como a antiga.
Future<void> mostrarInfoDaMidia(
  BuildContext context,
  String caminho,
  String nome,
) async {
  final info = await lerInfoDaMidia(caminho);
  if (!context.mounted) return;
  await mostrarAureaFolha<void>(
    context,
    titulo: 'Informações da mídia',
    construtor: (folha) => SingleChildScrollView(
      padding: const EdgeInsets.fromLTRB(
        AureaDims.margemDoPainel,
        AureaDims.e4,
        AureaDims.margemDoPainel,
        AureaDims.topoDoPainel,
      ),
      child: Column(
        key: const ValueKey('info-da-midia'),
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          for (final (rotulo, valor) in info.linhas)
            _LinhaDaFicha(rotulo: rotulo, valor: valor),
        ],
      ),
    ),
  );
}

/// UMA LINHA DA FICHA: o rotulo (texto de UI) a esquerda, o valor
/// (conteudo: nome de arquivo, numero) a direita. O rotulo nao cabe nos
/// 75 da linha de propriedade ("Quadros por segundo"), por isso a linha
/// e propria e divide a largura.
class _LinhaDaFicha extends StatelessWidget {
  const _LinhaDaFicha({required this.rotulo, required this.valor});

  final String rotulo;
  final String valor;

  @override
  Widget build(BuildContext context) => ConstrainedBox(
    constraints: const BoxConstraints(minHeight: AureaDims.itemDeLista),
    child: Padding(
      padding: const EdgeInsets.symmetric(vertical: AureaDims.e6),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Expanded(
            flex: 2,
            child: AppText(rotulo, style: AureaEstilos.propriedade),
          ),
          const SizedBox(width: AureaDims.e10),
          Expanded(
            flex: 3,
            child: Text(
              valor,
              textAlign: TextAlign.right,
              maxLines: 2,
              overflow: TextOverflow.ellipsis,
              style: AureaEstilos.valor,
            ),
          ),
        ],
      ),
    ),
  );
}

/// EXTRAIR O AUDIO com a conversa inteira: espera, cria a camada, avisa.
/// A camada de audio e o video mudo sao UM desfazer (o controlador junta);
/// o "Desfazer" do aviso leva os dois.
Future<void> extrairAudioComAviso(
  BuildContext context,
  WidgetRef ref,
  VideoLayer video,
) async {
  AureaSnack.show(context, translate(context, 'Extraindo o áudio…'));
  final caminho = await extrairAudioDoArquivo(video.sourcePath);
  if (!context.mounted) return;
  if (caminho == null) {
    AureaSnack.show(
      context,
      translate(context, 'Este vídeo não tem áudio para extrair'),
    );
    return;
  }
  final c = ref.read(editorControllerProvider.notifier);
  final id = c.extrairAudioDaCamada(video.id, caminho);
  if (id == null) return;
  AureaSnack.show(
    context,
    translate(context, 'Áudio extraído para uma camada própria'),
    actionLabel: translate(context, 'Desfazer'),
    onAction: c.undo,
  );
}
