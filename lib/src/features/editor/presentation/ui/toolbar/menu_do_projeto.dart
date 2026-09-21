import 'package:flutter/cupertino.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/snack.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../../../core/utils/time_format.dart';
import '../../../application/cronometro_de_edicao.dart';
import '../../../application/editor_controller.dart';
import '../../../application/playback_controller.dart';
import '../../../application/ui/editor_session.dart';
import '../../../application/ui/opcoes_de_visualizacao.dart';
import '../../../application/ui/pro_mode.dart';
import '../../../domain/layer.dart';
import '../../../domain/video_project.dart';
import '../paineis/batidas.dart' show showBeatsSheet;
import '../timeline/ima.dart' show alternarIma, magneticProvider;
import '../timeline/marcas_da_regua.dart' show menuDaMarca;

// ===========================================================================
// O MENU DO PROJETO E O MENU DAS MARCAS
// ===========================================================================
//
// O ⋮ do editor abre o que vale para o PROJETO inteiro ou para a linha do
// tempo, e nao para uma camada. Vieram de `shell/cromo_editor.dart`
// (`menuDaTimeline`) e `shell/layer_actions.dart` (`menuDasMarcas`) com as
// mesmas chamadas ao controlador e as mesmas chaves de teste; a casca e a
// do DS: folha que sobe, linhas no padrao do AureaMenu (item de 40, icone
// de 20, rotulo e detalhe), secoes com o titulo pequeno e apagado.
//
// São folhas, e nao o menu flutuante de 250: cada linha precisa do
// DETALHE (o tempo de hoje da miniatura, o que a marca faz), e o menu
// flutuante so tem rotulo.

/// UMA LINHA DE MENU EM FOLHA: barra de marca, icone, rotulo e detalhe
/// opcional — o [AureaMenu] com a segunda linha que a folha permite.
///
/// Marcado = barra de 4 no destaque a esquerda, texto no destaque e o
/// visto a direita (o mesmo "em vigor" do AureaMenu). Sem ripple: o
/// [Tocavel] so escurece debaixo do dedo.
class ItemDoMenuEmFolha extends StatelessWidget {
  const ItemDoMenuEmFolha({
    super.key,
    required this.chave,
    required this.icone,
    required this.rotulo,
    required this.onTap,
    this.detalhe,
    this.marcado = false,
    this.perigo = false,
  });

  /// A chave de teste da linha (as dos menus antigos, que os testes
  /// procuram).
  final String chave;
  final IconData icone;

  /// Rotulo e detalhe sao texto de UI: ja vem traduzidos quando levam
  /// numero ou tempo (moldados), e o `AppText` deixa passar o que nao
  /// esta no catalogo.
  final String rotulo;
  final String? detalhe;
  final bool marcado;
  final bool perigo;

  /// Nulo = indisponivel agora: a linha fica a vista, apagada — sumir com
  /// ela faria a pessoa procurar.
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final ativo = onTap != null;
    final cor = !ativo
        ? AureaCores.textoSecundario.withValues(alpha: .5)
        : perigo
        ? AureaCores.perigo
        : marcado
        ? AureaCores.destaque
        : AureaCores.texto;
    return Tocavel(
      key: ValueKey(chave),
      encolhe: 1,
      onTap: ativo
          ? () {
              HapticFeedback.selectionClick();
              onTap!();
            }
          : null,
      child: ConstrainedBox(
        constraints: const BoxConstraints(minHeight: AureaDims.itemDeMenu),
        child: Row(
          children: [
            Container(
              width: AureaDims.barraDeMarcaDoMenu,
              height: AureaDims.itemDeMenu - 16,
              color: AureaCores.destaque.withValues(alpha: marcado ? 1 : 0),
            ),
            const SizedBox(width: AureaDims.e10),
            Icon(icone, size: AureaDims.iconeMd, color: cor),
            const SizedBox(width: AureaDims.e10),
            Expanded(
              child: Padding(
                padding: const EdgeInsets.symmetric(vertical: AureaDims.e6),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    AppText(
                      rotulo,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: AureaEstilos.corpo.copyWith(color: cor),
                    ),
                    if (detalhe != null)
                      AppText(
                        detalhe!,
                        maxLines: 2,
                        overflow: TextOverflow.ellipsis,
                        style: AureaEstilos.propriedade.copyWith(fontSize: 11),
                      ),
                  ],
                ),
              ),
            ),
            if (marcado)
              Icon(
                CupertinoIcons.checkmark_alt,
                size: AureaDims.iconeSm,
                color: AureaCores.destaque,
              ),
            const SizedBox(width: AureaDims.e15),
          ],
        ),
      ),
    );
  }
}

/// O TITULO DE UMA SECAO do menu em folha: pequeno, apagado, em caixa
/// alta, alinhado com os icones das linhas.
class SecaoDoMenuEmFolha extends StatelessWidget {
  const SecaoDoMenuEmFolha(this.titulo, {super.key});

  final String titulo;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(
      AureaDims.barraDeMarcaDoMenu + AureaDims.e10,
      AureaDims.e15,
      AureaDims.e15,
      AureaDims.e4,
    ),
    // Traduz ANTES da caixa alta: o catalogo tem a frase como foi escrita.
    child: Text(
      translate(context, titulo).toUpperCase(),
      maxLines: 1,
      style: AureaEstilos.secao,
    ),
  );
}

// ------------------------------------------------------------------------
// O menu das marcas

/// O QUE AS MARCAS DESTRAVAM: ir para a proxima, cortar em todas,
/// distribuir as camadas nelas, limpar — e, no Pro, a Entrada e a Saida.
Future<void> menuDasMarcas(
  BuildContext context,
  WidgetRef ref,
  PlaybackController playback,
) async {
  final controller = ref.read(editorControllerProvider.notifier);
  final project = ref.read(editorControllerProvider);
  final quantas = project.markers.length;
  final bpm = project.bpm;

  await mostrarAureaFolha<void>(
    context,
    // O numero entra no molde depois de traduzido: "3 marcas" inteiro
    // nunca casaria com o catalogo.
    titulo: moldar(context, quantas == 1 ? '{0} marca' : '{0} marcas', [
      quantas,
    ]),
    acoes: [
      if (bpm != null)
        Center(
          child: Text(
            '${bpm.toStringAsFixed(0)} bpm',
            style: AureaEstilos.propriedade,
          ),
        ),
    ],
    construtor: (folha) {
      void fechar() => Navigator.of(folha).pop();
      final session = ref.read(editorSessionProvider);
      return ListView(
        shrinkWrap: true,
        padding: const EdgeInsets.only(bottom: AureaDims.e6),
        children: [
          ItemDoMenuEmFolha(
            chave: 'marcas-marcar-aqui',
            icone: CupertinoIcons.bookmark,
            rotulo: 'Marcar aqui',
            onTap: () {
              controller.toggleMarker(playback.timeForInput());
              fechar();
            },
          ),
          // A MARCA DEBAIXO DO CABECOTE: a bandeirinha no cabecote fica sob
          // o toque dele na regua, entao renomear e pintar essa marca mora
          // aqui tambem (o toque longo na marca faz o mesmo nas outras).
          if (project.markerNear(
                playback.time.value,
                const Duration(milliseconds: 120),
              )
              case final aqui?)
            ItemDoMenuEmFolha(
              chave: 'marcas-editar-aqui',
              icone: CupertinoIcons.pencil,
              rotulo: 'Renomear, pintar ou apagar a marca daqui',
              onTap: () {
                fechar();
                menuDaMarca(context, ref, aqui);
              },
            ),
          ItemDoMenuEmFolha(
            chave: 'marcas-proxima',
            icone: CupertinoIcons.chevron_right_2,
            rotulo: 'Ir para a próxima marca',
            onTap: quantas == 0
                ? null
                : () {
                    final t = playback.time.value;
                    final proximo =
                        controller.markerAfter(t) ??
                        (project.markers.isEmpty
                            ? null
                            : project.markers.first.time);
                    if (proximo != null) playback.seek(proximo);
                    fechar();
                  },
          ),
          ItemDoMenuEmFolha(
            chave: 'marcas-cortar',
            icone: CupertinoIcons.scissors,
            rotulo: 'Cortar em todas as marcas',
            onTap: quantas == 0
                ? null
                : () {
                    // `cutAtMarkers` ja e um passo de desfazer so.
                    final n = controller.cutAtMarkers();
                    fechar();
                    AureaSnack.show(
                      context,
                      moldar(context, n == 1 ? '{0} corte' : '{0} cortes', [
                        n,
                      ]),
                    );
                  },
          ),
          ItemDoMenuEmFolha(
            chave: 'marcas-distribuir',
            icone: CupertinoIcons.square_grid_2x2,
            rotulo: 'Distribuir as camadas nas marcas',
            detalhe: 'Uma camada por marca, na ordem em que estão',
            onTap: quantas < 2
                ? null
                : () {
                    final n = controller.distributeAtMarkers();
                    fechar();
                    AureaSnack.show(
                      context,
                      moldar(context, '{0} camadas distribuídas', [n]),
                    );
                  },
          ),
          // ------------------------------------- Entrada e Saida (Pro)
          //
          // AS DUAS PONTAS DA EDICAO DE 3 PONTOS. A regua pinta `I` e `O`
          // e as acoes da camada oferecem Levantar e Extrair, mas marcar os
          // pontos so se faz aqui: o dono mandou tirar da regua tudo o que
          // a atravancava (ha teste guardando isso), e este e o menu do
          // assunto.
          if (ref.read(proModeProvider))
            for (final (chave, rotulo, dica, tempo, marcar) in [
              (
                'timeline-entrada',
                'Marcar Entrada (I)',
                'O começo do trecho que Levantar e Extrair usam',
                session.inPoint,
                () => ref
                    .read(editorSessionProvider.notifier)
                    .setInPoint(playback.time.value),
              ),
              (
                'timeline-saida',
                'Marcar Saída (O)',
                'O fim do trecho que Levantar e Extrair usam',
                session.outPoint,
                () => ref
                    .read(editorSessionProvider.notifier)
                    .setOutPoint(playback.time.value),
              ),
            ])
              ItemDoMenuEmFolha(
                chave: chave,
                icone: tempo == null
                    ? CupertinoIcons.arrow_right_to_line
                    : CupertinoIcons.checkmark_circle_fill,
                rotulo: rotulo,
                marcado: tempo != null,
                detalhe: tempo == null
                    ? dica
                    : moldar(folha, 'já marcado em {0}', [
                        formatTimecode(tempo, 30),
                      ]),
                onTap: () {
                  marcar();
                  fechar();
                },
              ),
          // ------------------------------------------------ batidas
          //
          // A GRADE DO RITMO mora aqui porque e daqui que se navega e se
          // corta por marca — e batida e a marca que a musica poe.
          ItemDoMenuEmFolha(
            chave: 'marcas-batidas-detectar',
            icone: CupertinoIcons.music_note_2,
            rotulo: 'Batidas da música…',
            detalhe: 'Detecta o ritmo e risca a régua',
            onTap: () {
              fechar();
              String? comSom;
              for (final l in project.layers) {
                if (l is AudioLayer || (l is VideoLayer && l.volume > 0.001)) {
                  comSom = l.id;
                  break;
                }
              }
              if (comSom == null) {
                AureaSnack.show(
                  context,
                  translate(context, 'Adicione uma música primeiro.'),
                );
                return;
              }
              showBeatsSheet(context, ref, comSom);
            },
          ),
          ItemDoMenuEmFolha(
            chave: 'marcas-batidas-virar',
            icone: CupertinoIcons.flag,
            rotulo: 'Batidas viram marcas',
            detalhe: 'Cada batida vira uma marca de verdade na régua',
            onTap: project.beats.isEmpty
                ? null
                : () {
                    final n = controller.batidasViramMarcadores();
                    fechar();
                    AureaSnack.show(
                      context,
                      n == 0
                          ? translate(context, 'As batidas já têm marcas.')
                          : moldar(context, '{0} marcas no ritmo', [n]),
                    );
                  },
          ),
          ItemDoMenuEmFolha(
            chave: 'marcas-limpar',
            icone: CupertinoIcons.delete,
            rotulo: 'Limpar as marcas',
            perigo: true,
            onTap: quantas == 0
                ? null
                : () {
                    controller.clearMarkers();
                    fechar();
                  },
          ),
        ],
      );
    },
  );
}

// ------------------------------------------------------------------------
// O menu do projeto (o ⋮ do editor)

/// O ⋮ DO EDITOR: tudo o que vale para o projeto inteiro ou para a linha
/// do tempo, e nao para uma camada — selecao, reproducao, modo de previa,
/// aparar, miniatura, marcas de introducao e final, marcas e batidas,
/// cronometro de edicao, o ima da timeline, agrupar e guia.
Future<void> mostrarMenuDoProjeto(
  BuildContext context,
  WidgetRef ref,
  PlaybackController playback, {
  required VoidCallback onAgrupar,
  required VoidCallback onGuia,
  VoidCallback? onDefinirMiniatura,
}) async {
  playback.pause();
  await mostrarAureaFolha<void>(
    context,
    construtor: (folha) => _MenuDoProjeto(
      playback: playback,
      contextoDoEditor: context,
      refDoEditor: ref,
      onAgrupar: onAgrupar,
      onGuia: onGuia,
      onDefinirMiniatura: onDefinirMiniatura,
    ),
  );
}

/// OS IDS DAS CAMADAS, como valor comparavel: o `select` compara com
/// `==`, e uma `List` nova a cada leitura acordaria o menu por nada.
@immutable
class _IdsDasCamadas {
  const _IdsDasCamadas(this.ids);

  factory _IdsDasCamadas.de(VideoProject p) =>
      _IdsDasCamadas([for (final l in p.layers) l.id]);

  final List<String> ids;

  @override
  bool operator ==(Object other) {
    if (other is! _IdsDasCamadas || other.ids.length != ids.length) {
      return false;
    }
    for (var i = 0; i < ids.length; i++) {
      if (other.ids[i] != ids[i]) return false;
    }
    return true;
  }

  @override
  int get hashCode => Object.hashAll(ids);
}

class _MenuDoProjeto extends ConsumerWidget {
  const _MenuDoProjeto({
    required this.playback,
    required this.contextoDoEditor,
    required this.refDoEditor,
    required this.onAgrupar,
    required this.onGuia,
    required this.onDefinirMiniatura,
  });

  final PlaybackController playback;

  /// O contexto e o `ref` do EDITOR: as folhas e os avisos que este menu
  /// abre nascem deles, e nao da folha que se fecha — o `ref` de um
  /// widget que saiu da arvore recusa leitura.
  final BuildContext contextoDoEditor;
  final WidgetRef refDoEditor;
  final VoidCallback onAgrupar;
  final VoidCallback onGuia;
  final VoidCallback? onDefinirMiniatura;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    // SO OS CAMPOS QUE O MENU MOSTRA.
    //
    // Este menu e uma folha modal por cima do editor: observando o
    // projeto inteiro ele se refazia junto com qualquer mutacao — e
    // enquanto ele esta aberto o que muda o projeto sao as proprias
    // acoes dele. A lista de ids vai como `_IdsDasCamadas`, que compara
    // por valor.
    final projeto = ref.watch(
      editorControllerProvider.select(
        (p) => (
          id: p.id,
          thumbTime: p.thumbTime,
          introFim: p.introFim,
          finalInicio: p.finalInicio,
          marcadores: p.markers.length,
          ids: _IdsDasCamadas.de(p),
        ),
      ),
    );
    final controller = ref.read(editorControllerProvider.notifier);
    final opcoes = ref.watch(opcoesDeVisualizacaoProvider);
    final cronometro = ref.watch(cronometroDeEdicaoProvider(projeto.id));
    final ima = ref.watch(magneticProvider);
    final agora = playback.time.value;
    final expandido = ref.watch(
      editorSessionProvider.select((s) => s.previewExpanded),
    );
    final editor = refDoEditor;

    void fechar() => Navigator.of(context).pop();
    void fecharE(VoidCallback acao) {
      fechar();
      acao();
    }

    Widget item(
      String chave,
      IconData icone,
      String rotulo, {
      String? detalhe,
      bool marcado = false,
      required VoidCallback? onTap,
    }) => ItemDoMenuEmFolha(
      chave: chave,
      icone: icone,
      rotulo: rotulo,
      detalhe: detalhe,
      marcado: marcado,
      onTap: onTap,
    );

    // Tempo e contagem entram no molde depois de traduzido.
    String hoje(Duration t) => moldar(context, 'Hoje: {0}', [tempoDaInfobar(t)]);

    final estado = cronometro.estado;
    final todas = projeto.ids.ids;

    return ConstrainedBox(
      constraints: BoxConstraints(
        maxHeight: MediaQuery.sizeOf(context).height * .78,
      ),
      child: ListView(
        key: const ValueKey('timeline-menu'),
        shrinkWrap: true,
        padding: const EdgeInsets.only(bottom: AureaDims.e10),
        children: [
          const SecaoDoMenuEmFolha('Seleção'),
          item(
            'timeline-menu-selecionar-todas',
            CupertinoIcons.checkmark_square,
            'Selecionar todas as camadas',
            onTap: todas.length < 2
                ? null
                : () => fecharE(() {
                    editor.read(selectedLayerProvider.notifier).state = null;
                    editor.read(multiSelectProvider.notifier).state = todas
                        .toSet();
                  }),
          ),
          item(
            'timeline-menu-limpar-selecao',
            CupertinoIcons.square,
            'Limpar seleção',
            onTap: () => fecharE(() {
              editor.read(multiSelectProvider.notifier).state = const {};
              editor.read(selectedLayerProvider.notifier).state = null;
            }),
          ),
          const SecaoDoMenuEmFolha('Reprodução e prévia'),
          item(
            'timeline-menu-loop',
            CupertinoIcons.repeat,
            'Reprodução em loop',
            marcado: playback.loop.value,
            onTap: () =>
                fecharE(() => playback.loop.value = !playback.loop.value),
          ),
          item(
            'timeline-menu-tela-cheia',
            CupertinoIcons.fullscreen,
            expandido ? 'Sair da tela cheia' : 'Tela cheia',
            onTap: () => fecharE(
              () => editor
                  .read(editorSessionProvider.notifier)
                  .togglePreviewExpanded(),
            ),
          ),
          for (final modo in ModoDePrevia.values)
            item(
              'timeline-menu-modo-${modo.name}',
              switch (modo) {
                ModoDePrevia.resultadoFinal => CupertinoIcons.sparkles,
                ModoDePrevia.semEfeitos => CupertinoIcons.wand_rays_inverse,
                ModoDePrevia.meioTransparente =>
                  CupertinoIcons.circle_lefthalf_fill,
              },
              moldar(context, 'Prévia: {0}', [
                translate(context, rotuloDoModoDePrevia(modo)),
              ]),
              marcado: opcoes.modo == modo,
              onTap: () => fecharE(
                () => editor
                    .read(opcoesDeVisualizacaoProvider.notifier)
                    .definirModo(modo),
              ),
            ),
          const SecaoDoMenuEmFolha('Projeto'),
          item(
            'timeline-menu-aparar-projeto',
            CupertinoIcons.scissors_alt,
            'Aparar o projeto no cabeçote',
            detalhe: moldar(context, 'Corta tudo o que passa de {0}', [
              tempoDaInfobar(agora),
            ]),
            onTap: agora <= Duration.zero
                ? null
                : () => fecharE(() {
                    // Um passo de desfazer: o controlador ja agrupa o
                    // corte e as camadas que saem.
                    controller.aparaProjetoNoCabecote(agora);
                    if (!contextoDoEditor.mounted) return;
                    AureaSnack.show(
                      contextoDoEditor,
                      translate(contextoDoEditor, 'Projeto aparado no cabeçote'),
                      actionLabel: translate(contextoDoEditor, 'Desfazer'),
                      onAction: controller.undo,
                    );
                  }),
          ),
          item(
            'timeline-menu-miniatura',
            CupertinoIcons.photo,
            'Usar este quadro como miniatura',
            detalhe: projeto.thumbTime == null
                ? null
                : hoje(projeto.thumbTime!),
            onTap: onDefinirMiniatura == null
                ? null
                : () => fecharE(onDefinirMiniatura!),
          ),
          if (projeto.thumbTime != null)
            item(
              'timeline-menu-limpar-miniatura',
              CupertinoIcons.photo_on_rectangle,
              'Voltar à miniatura automática',
              onTap: () =>
                  fecharE(() => controller.definirQuadroDaMiniatura(null)),
            ),
          item(
            'timeline-menu-intro',
            CupertinoIcons.arrow_right_to_line,
            'Marcar aqui o fim da introdução',
            detalhe: projeto.introFim == null
                ? 'Esticado noutro projeto, a introdução toca intacta'
                : hoje(projeto.introFim!),
            onTap: () => fecharE(() => controller.marcarFimDaIntroducao(agora)),
          ),
          if (projeto.introFim != null)
            item(
              'timeline-menu-intro-tirar',
              CupertinoIcons.xmark,
              'Tirar a marca da introdução',
              onTap: () =>
                  fecharE(() => controller.marcarFimDaIntroducao(null)),
            ),
          item(
            'timeline-menu-final',
            CupertinoIcons.arrow_left_to_line,
            'Marcar aqui o começo do final',
            detalhe: projeto.finalInicio == null
                ? 'Esticado noutro projeto, o final toca intacto'
                : hoje(projeto.finalInicio!),
            onTap: () => fecharE(() => controller.marcarInicioDoFinal(agora)),
          ),
          if (projeto.finalInicio != null)
            item(
              'timeline-menu-final-tirar',
              CupertinoIcons.xmark,
              'Tirar a marca do final',
              onTap: () => fecharE(() => controller.marcarInicioDoFinal(null)),
            ),
          const SecaoDoMenuEmFolha('Marcas e ritmo'),
          item(
            'timeline-menu-marcador',
            CupertinoIcons.bookmark,
            'Marcar este instante',
            onTap: () => fecharE(
              () => controller.toggleMarker(playback.timeForInput()),
            ),
          ),
          item(
            'timeline-menu-marcas',
            CupertinoIcons.bookmark_solid,
            'Marcas na timeline',
            detalhe: projeto.marcadores == 0 ? null : '${projeto.marcadores}',
            onTap: () => fecharE(
              () => menuDasMarcas(contextoDoEditor, editor, playback),
            ),
          ),
          item(
            'timeline-menu-batidas',
            CupertinoIcons.music_note_2,
            'Batidas da música',
            onTap: () => fecharE(() {
              final som = editor
                  .read(editorControllerProvider)
                  .layers
                  .where((l) => l is AudioLayer || l is VideoLayer)
                  .firstOrNull;
              if (som == null) {
                AureaSnack.show(
                  contextoDoEditor,
                  translate(
                    contextoDoEditor,
                    'Adicione um áudio ou um vídeo primeiro',
                  ),
                );
                return;
              }
              showBeatsSheet(contextoDoEditor, editor, som.id);
            }),
          ),
          const SecaoDoMenuEmFolha('Cronômetro de edição'),
          if (estado == EstadoDoCronometro.parado)
            item(
              'timeline-menu-cronometro-iniciar',
              CupertinoIcons.timer,
              'Iniciar o cronômetro',
              detalhe: 'Conta o tempo que você passa editando este projeto',
              onTap: () => fecharE(cronometro.iniciar),
            ),
          if (estado == EstadoDoCronometro.rodando)
            item(
              'timeline-menu-cronometro-pausar',
              CupertinoIcons.pause_circle,
              'Pausar o cronômetro',
              detalhe: textoDoCronometro(cronometro.total),
              onTap: () => fecharE(cronometro.pausar),
            ),
          if (estado == EstadoDoCronometro.pausado)
            item(
              'timeline-menu-cronometro-retomar',
              CupertinoIcons.play_circle,
              'Retomar o cronômetro',
              detalhe: textoDoCronometro(cronometro.total),
              onTap: () => fecharE(cronometro.iniciar),
            ),
          if (estado != EstadoDoCronometro.parado)
            item(
              'timeline-menu-cronometro-apagar',
              CupertinoIcons.trash,
              'Apagar o cronômetro',
              onTap: () => fecharE(cronometro.apagar),
            ),
          const SecaoDoMenuEmFolha('Mais'),
          // O IMA DA TIMELINE: a regua antiga tinha o interruptor e a
          // timeline nova nao tem porta para ele. Ligado, apagar fecha o
          // buraco; desligado, cada clipe fica onde esta.
          item(
            'timeline-menu-ima',
            CupertinoIcons.arrow_right_arrow_left_square,
            'Ímã da timeline',
            detalhe: ima
                ? 'Ligado: apagar fecha o buraco'
                : 'Desligado: apagar deixa o buraco',
            marcado: ima,
            onTap: () => fecharE(() => alternarIma(editor)),
          ),
          item(
            'timeline-menu-agrupar',
            CupertinoIcons.rectangle_stack,
            'Agrupar camadas…',
            onTap: () => fecharE(onAgrupar),
          ),
          item(
            'timeline-menu-guia',
            CupertinoIcons.book,
            'Guia rápido',
            onTap: () => fecharE(onGuia),
          ),
        ],
      ),
    );
  }
}
