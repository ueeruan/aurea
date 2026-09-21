import 'dart:io';

import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../media/application/midias_recentes.dart';
import '../../../../media/application/sons_recentes.dart';
import '../../../application/editor_controller.dart';
import '../../../application/playback_controller.dart';
import '../../../domain/element3d.dart';
import '../../../domain/shape_library.dart';
import '../../../../../core/ui/snack.dart';
import 'acoes_da_camada.dart' show agruparSelecao;
import 'legendar.dart' show showCaptionCreationSheet;
import 'galeria.dart';
import 'linha_de_som_recente.dart';
import '../shell/contrato.dart';
import 'adicionar_acoes.dart';

// ===========================================================================
// A FOLHA DE ADICIONAR (o "+")
// ===========================================================================
//
// 251 de altura, abas de 58 com as CINCO categorias — Mídia, Texto,
// Formas, 3D, Objetos — e o conteudo da aba embaixo. Sobe em 200 ms (folha
// grande), desacelerando. Um toque adiciona no cabecote, a camada nova vira
// a selecao e a folha fecha: a pessoa ve a camada nascer na timeline e a
// barra dela aparecer.
//
// Quem so escolhe (texto, nulo, forma) fecha a folha DEPOIS de criar; quem
// abre outra coisa (seletor de arquivos, Sketchfab, pergunta do Texto 3D)
// fecha ANTES, e continua com o contexto do editor.

/// AS CATEGORIAS DO "+", na ordem das abas. A chave da aba e
/// `adicionar-aba-<id>`.
enum CategoriaDeAdicionar {
  midia('midia', 'Mídia', CupertinoIcons.photo_on_rectangle),
  texto('texto', 'Texto', CupertinoIcons.textformat),
  formas('formas', 'Formas', CupertinoIcons.square_on_circle),
  tresD('3d', '3D', CupertinoIcons.cube),
  objetos('objetos', 'Objetos', CupertinoIcons.circle_grid_hex);

  const CategoriaDeAdicionar(this.id, this.rotulo, this.icone);

  final String id;
  final String rotulo;
  final IconData icone;
}

/// As sub-secoes de Midia (o trilho de 58 a esquerda).
enum _FonteDeMidia { recentes, galeria, audio }

/// ABRE A FOLHA DE ADICIONAR no cabecote atual.
///
/// [context] e [ref] sao os do EDITOR: o que a folha dispara continua
/// rodando depois de ela fechar.
Future<void> abrirAdicionar(
  BuildContext context,
  WidgetRef ref, {
  required PlaybackController playback,
  required void Function(PainelId id) abrirPainel,
  CategoriaDeAdicionar categoria = CategoriaDeAdicionar.midia,
}) {
  playback.pause();
  final editor = EditorParaAdicionar(
    context: context,
    ref: ref,
    playback: playback,
    abrirPainel: abrirPainel,
  );
  return mostrarAureaFolha<void>(
    context,
    grande: true,
    // Sem titulo a folha poe 10 de respiro em cima: o total fica nos 251.
    altura: AureaDims.folhaDeAdicionar - AureaDims.e10,
    construtor: (folha) =>
        FolhaDeAdicionar(editor: editor, categoriaInicial: categoria),
  );
}

/// O CONTEUDO DA FOLHA (publico para teste e para quem quiser embutir).
class FolhaDeAdicionar extends StatefulWidget {
  const FolhaDeAdicionar({
    super.key,
    required this.editor,
    this.categoriaInicial = CategoriaDeAdicionar.midia,
  });

  final EditorParaAdicionar editor;
  final CategoriaDeAdicionar categoriaInicial;

  @override
  State<FolhaDeAdicionar> createState() => _FolhaDeAdicionarState();
}

class _FolhaDeAdicionarState extends State<FolhaDeAdicionar> {
  late var _categoria = widget.categoriaInicial;
  _FonteDeMidia? _fonte;
  bool _ocupado = false;

  EditorParaAdicionar get _e => widget.editor;

  void _fechar() => Navigator.of(context).maybePop();

  /// Cria e fecha: a camada ja e a selecao quando a folha sai.
  void _criaEFecha(void Function() criar) {
    criar();
    _fechar();
  }

  /// Fecha e segue com o contexto do editor (outra tela, outra pergunta).
  void _fechaE(Future<void> Function() depois) {
    _fechar();
    depois();
  }

  /// Espera o trabalho com a folha aberta (o dedo ve "ocupado"), e fecha
  /// se entrou.
  Future<void> _espera(Future<bool> Function() trabalho) async {
    if (_ocupado) return;
    setState(() => _ocupado = true);
    var entrou = false;
    try {
      entrou = await trabalho();
    } finally {
      if (mounted) setState(() => _ocupado = false);
    }
    if (entrou && mounted) _fechar();
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      key: const ValueKey('folha-de-adicionar'),
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        SizedBox(
          height: AureaDims.abasDaFolhaDeAdicionar,
          child: Row(
            children: [
              for (final cat in CategoriaDeAdicionar.values)
                Expanded(
                  child: AureaToolbarButton(
                    key: ValueKey('adicionar-aba-${cat.id}'),
                    icone: cat.icone,
                    rotulo: cat.rotulo,
                    largura: double.infinity,
                    ativo: cat == _categoria,
                    aoTocar: () => setState(() => _categoria = cat),
                  ),
                ),
              SizedBox(
                width: AureaDims.toqueConfortavel,
                child: AureaToolbarButton(
                  key: const ValueKey('adicionar-fechar'),
                  icone: CupertinoIcons.xmark,
                  rotulo: 'Fechar',
                  largura: AureaDims.toqueConfortavel,
                  aoTocar: _fechar,
                ),
              ),
            ],
          ),
        ),
        Expanded(
          child: AnimatedSwitcher(
            duration: AureaMotion.rapido,
            switchInCurve: AureaMotion.entrada,
            switchOutCurve: AureaMotion.saida,
            child: KeyedSubtree(
              key: ValueKey('adicionar-conteudo-${_categoria.id}'),
              child: switch (_categoria) {
                CategoriaDeAdicionar.midia => _midia(),
                CategoriaDeAdicionar.texto => _texto(),
                CategoriaDeAdicionar.formas => _formas(),
                CategoriaDeAdicionar.tresD => _tresD(),
                CategoriaDeAdicionar.objetos => _objetos(),
              },
            ),
          ),
        ),
      ],
    );
  }

  // ---------------------------------------------------------------- blocos

  /// UMA GRADE DE BLOCOS (57 de altura, icone 32), quatro por fileira.
  Widget _blocos(List<(String, IconData, String, VoidCallback?)> blocos) {
    return LayoutBuilder(
      builder: (context, c) {
        const colunas = 4;
        final largura =
            (c.maxWidth -
                2 * AureaDims.margemDoPainel -
                AureaDims.vaoDoPainel * (colunas - 1)) /
            colunas;
        return SingleChildScrollView(
          padding: const EdgeInsets.fromLTRB(
            AureaDims.margemDoPainel,
            AureaDims.e10,
            AureaDims.margemDoPainel,
            AureaDims.e10,
          ),
          child: Wrap(
            spacing: AureaDims.vaoDoPainel,
            runSpacing: AureaDims.vaoDoPainel,
            children: [
              for (final (id, icone, rotulo, acao) in blocos)
                AureaToolbarButton(
                  key: ValueKey('adicionar-$id'),
                  icone: icone,
                  rotulo: rotulo,
                  bloco: true,
                  largura: largura,
                  aoTocar: _ocupado ? null : acao,
                ),
            ],
          ),
        );
      },
    );
  }

  // ----------------------------------------------------------------- midia

  Widget _midia() {
    final fonte =
        _fonte ??
        (_e.ref.read(midiasRecentesProvider).isNotEmpty
            ? _FonteDeMidia.recentes
            : _FonteDeMidia.galeria);
    const rotulos = {
      _FonteDeMidia.recentes: ('recentes', CupertinoIcons.clock, 'Recentes'),
      _FonteDeMidia.galeria: (
        'galeria',
        CupertinoIcons.photo_on_rectangle,
        'Galeria',
      ),
      _FonteDeMidia.audio: ('audio', CupertinoIcons.music_note_2, 'Áudio'),
    };
    return Row(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        // O TRILHO DE 58: de onde vem a midia. Recentes primeiro — quem
        // monta um projeto costuma reusar o que acabou de usar.
        SizedBox(
          width: AureaDims.abasDaFolhaDeAdicionar,
          child: Column(
            children: [
              for (final f in _FonteDeMidia.values)
                Expanded(
                  child: AureaToolbarButton(
                    key: ValueKey('adicionar-midia-${rotulos[f]!.$1}'),
                    icone: rotulos[f]!.$2,
                    rotulo: rotulos[f]!.$3,
                    largura: AureaDims.abasDaFolhaDeAdicionar,
                    ativo: f == fonte,
                    aoTocar: () => setState(() => _fonte = f),
                  ),
                ),
            ],
          ),
        ),
        Expanded(
          child: switch (fonte) {
            _FonteDeMidia.recentes => _recentes(),
            _FonteDeMidia.galeria => Padding(
              padding: const EdgeInsets.only(right: AureaDims.e6),
              child: GalleryPanel(
                onImport: (arquivo, video, duracao) async {
                  await adicionarMidia(
                    _e,
                    arquivo,
                    video: video,
                    duracao: duracao,
                  );
                  if (mounted) _fechar();
                },
                onImportLote: (midias, {required emSequencia}) async {
                  await adicionarLoteDeMidia(
                    _e,
                    midias,
                    emSequencia: emSequencia,
                  );
                  if (mounted) _fechar();
                },
              ),
            ),
            _FonteDeMidia.audio => _audio(),
          },
        ),
      ],
    );
  }

  /// OS RECENTES DA AUREA: o que ja entrou em algum projeto, sem seletor.
  Widget _recentes() {
    return Consumer(
      builder: (context, ref, _) {
        final lista = ref.watch(midiasRecentesProvider);
        if (lista.isEmpty) {
          return Center(
            child: Padding(
              padding: const EdgeInsets.all(AureaDims.e20),
              child: AppText(
                'As fotos e os vídeos que você usar ficam aqui.',
                textAlign: TextAlign.center,
                style: AureaEstilos.propriedade,
              ),
            ),
          );
        }
        return GridView.builder(
          key: const ValueKey('adicionar-grade-recentes'),
          padding: const EdgeInsets.fromLTRB(0, AureaDims.e6, AureaDims.e6, 0),
          gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(
            maxCrossAxisExtent: 96,
            mainAxisSpacing: AureaDims.e2,
            crossAxisSpacing: AureaDims.e2,
          ),
          itemCount: lista.length,
          itemBuilder: (context, i) {
            final m = lista[i];
            return _MiniaturaDeRecente(
              key: ValueKey('adicionar-recente-$i'),
              midia: m,
              aoTocar: _ocupado
                  ? null
                  : () => _espera(() => usarMidiaRecente(_e, m)),
            );
          },
        );
      },
    );
  }

  Widget _audio() {
    return Consumer(
      builder: (context, ref, _) {
        final sons = ref.watch(sonsRecentesProvider);
        return Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            SizedBox(
              height: AureaDims.blocoDePainel + 2 * AureaDims.e6,
              child: Padding(
                padding: const EdgeInsets.fromLTRB(
                  AureaDims.e6,
                  AureaDims.e6,
                  AureaDims.margemDoPainel,
                  AureaDims.e6,
                ),
                child: Row(
                  children: [
                    Expanded(
                      child: AureaToolbarButton(
                        key: const ValueKey('adicionar-audio-arquivo'),
                        icone: CupertinoIcons.music_note,
                        rotulo: _ocupado ? 'Preparando…' : 'Arquivo de áudio',
                        bloco: true,
                        largura: double.infinity,
                        aoTocar: _ocupado
                            ? null
                            : () => _espera(() => importarAudio(_e)),
                      ),
                    ),
                    const SizedBox(width: AureaDims.vaoDoPainel),
                    Expanded(
                      child: AureaToolbarButton(
                        key: const ValueKey('adicionar-audio-de-video'),
                        icone: CupertinoIcons.film,
                        rotulo: 'Som de um vídeo',
                        bloco: true,
                        largura: double.infinity,
                        aoTocar: _ocupado
                            ? null
                            : () => _espera(
                                () => importarAudio(_e, doVideo: true),
                              ),
                      ),
                    ),
                  ],
                ),
              ),
            ),
            // OS SONS RECENTES: a mesma trilha em outro projeto entra com um
            // toque, e o play deixa OUVIR antes de entrar.
            if (sons.isNotEmpty)
              Expanded(
                child: ListView(
                  padding: const EdgeInsets.only(right: AureaDims.e6),
                  children: [
                    for (final som in sons)
                      LinhaDeSomRecente(
                        key: ValueKey('som-recente-${som.caminho}'),
                        som: som,
                        onAdicionar: () =>
                            _espera(() => usarSomRecente(_e, som)),
                      ),
                  ],
                ),
              ),
          ],
        );
      },
    );
  }

  // ----------------------------------------------------------------- texto

  Widget _texto() => _blocos([
    (
      'texto',
      CupertinoIcons.textformat,
      'Texto',
      () {
        _criaEFecha(() => _e.controlador.addTextLayer(_e.agora));
      },
    ),
    (
      'legenda',
      CupertinoIcons.captions_bubble,
      'Legenda',
      () {
        // Transcricao automatica (nuvem ou aparelho) ou SRT colado.
        _fechaE(() => showCaptionCreationSheet(_e.context, _e.ref));
      },
    ),
    (
      'texto3d',
      CupertinoIcons.textformat_alt,
      'Texto 3D',
      () {
        _fechaE(() => criarTexto3D(_e));
      },
    ),
  ]);

  // ---------------------------------------------------------------- formas

  Widget _formas() {
    final ferramentas = <(String, IconData, String, VoidCallback)>[
      (
        'desenho-livre',
        CupertinoIcons.scribble,
        'Desenho livre',
        () {
          _criaEFecha(() => comecarDesenhoLivre(_e));
        },
      ),
      (
        'desenho-vetorial',
        CupertinoIcons.pencil_outline,
        'Vetorial',
        () {
          _fechaE(() async => comecarDesenhoVetorial(_e));
        },
      ),
      (
        'svg',
        CupertinoIcons.doc_text,
        'SVG',
        () {
          _fechaE(() => importarSvg(_e));
        },
      ),
    ];
    return CustomScrollView(
      slivers: [
        SliverPadding(
          padding: const EdgeInsets.fromLTRB(
            AureaDims.margemDoPainel,
            AureaDims.e10,
            AureaDims.margemDoPainel,
            AureaDims.vaoDoPainel,
          ),
          sliver: SliverGrid(
            gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(
              crossAxisCount: 3,
              mainAxisSpacing: AureaDims.vaoDoPainel,
              crossAxisSpacing: AureaDims.vaoDoPainel,
              mainAxisExtent: AureaDims.blocoDePainel,
            ),
            delegate: SliverChildListDelegate([
              for (final (id, icone, rotulo, acao) in ferramentas)
                AureaToolbarButton(
                  key: ValueKey('adicionar-$id'),
                  icone: icone,
                  rotulo: rotulo,
                  bloco: true,
                  largura: double.infinity,
                  aoTocar: acao,
                ),
            ]),
          ),
        ),
        SliverPadding(
          padding: const EdgeInsets.fromLTRB(
            AureaDims.margemDoPainel,
            0,
            AureaDims.margemDoPainel,
            AureaDims.e10,
          ),
          sliver: SliverGrid(
            gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(
              maxCrossAxisExtent: AureaDims.blocoDePainel,
              mainAxisSpacing: AureaDims.vaoDoPainel,
              crossAxisSpacing: AureaDims.vaoDoPainel,
            ),
            delegate: SliverChildBuilderDelegate(
              childCount: shapeLibrary.length,
              (context, i) => _LadrilhoDeForma(
                key: ValueKey('adicionar-forma-$i'),
                forma: shapeLibrary[i],
                aoTocar: () =>
                    _criaEFecha(() => adicionarForma(_e, shapeLibrary[i])),
              ),
            ),
          ),
        ),
      ],
    );
  }

  // -------------------------------------------------------------------- 3D

  Widget _tresD() => _blocos([
    // DO APARELHO: GLB, glTF, OBJ, FBX; o que e pesado de verdade pergunta
    // com a ficha dele e a opcao de otimizar (importacao_3d.dart).
    (
      '3d-aparelho',
      CupertinoIcons.device_phone_portrait,
      'Do aparelho',
      () {
        _fechaE(() => importarModelo3DDoAparelho(_e));
      },
    ),
    (
      '3d-sketchfab',
      CupertinoIcons.cloud_download,
      'Sketchfab',
      () {
        _fechaE(() => abrirSketchfab(_e));
      },
    ),
    (
      '3d-texto3d',
      CupertinoIcons.textformat_alt,
      'Texto 3D',
      () {
        _fechaE(() => criarTexto3D(_e));
      },
    ),
    (
      '3d-solido',
      CupertinoIcons.cube_fill,
      'Sólido 3D',
      () {
        _criaEFecha(
          () => _e.controlador.addElement3DLayer(_e.agora, Element3DKind.cube),
        );
      },
    ),
  ]);

  // --------------------------------------------------------------- objetos

  Widget _objetos() => _blocos([
    (
      'nulo',
      CupertinoIcons.smallcircle_circle,
      'Nulo',
      () {
        _criaEFecha(() => _e.controlador.addNullLayer(_e.agora));
      },
    ),
    (
      'camera',
      CupertinoIcons.videocam,
      'Câmera',
      () {
        _criaEFecha(() => _e.controlador.addCameraLayer(_e.agora));
      },
    ),
    (
      'ajuste',
      CupertinoIcons.slider_horizontal_3,
      'Ajuste',
      () {
        // Um efeito sobre tudo abaixo: a camada ja nasce com Efeitos aberto.
        _criaEFecha(() => _e.controlador.addAdjustmentLayer(_e.agora));
        _e.abrirPainel(PainelId.efeitos);
      },
    ),
    (
      'particulas',
      CupertinoIcons.sparkles,
      'Partículas',
      () {
        _criaEFecha(() => _e.controlador.addParticulasLayer(_e.agora));
      },
    ),
    (
      'grupo',
      CupertinoIcons.folder,
      'Grupo vazio',
      () {
        _criaEFecha(() => _e.controlador.addEmptyGroup(_e.agora));
      },
    ),
    (
      'agrupar',
      CupertinoIcons.folder_badge_plus,
      'Agrupar camadas',
      () {
        _fechaE(() => agruparPorEscolha(_e.context, _e.ref));
      },
    ),
  ]);
}

/// UM RECENTE: a miniatura (o proprio arquivo, na foto; o jpg guardado, no
/// video) com o selo de video.
class _MiniaturaDeRecente extends StatelessWidget {
  const _MiniaturaDeRecente({super.key, required this.midia, this.aoTocar});

  final MidiaRecente midia;
  final VoidCallback? aoTocar;

  @override
  Widget build(BuildContext context) {
    final imagem = midia.video ? midia.miniatura : midia.caminho;
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: aoTocar,
      child: ClipRRect(
        borderRadius: BorderRadius.circular(AureaDims.raioSm),
        child: Stack(
          fit: StackFit.expand,
          children: [
            ColoredBox(color: AureaCores.campo),
            if (imagem != null)
              Image.file(
                File(imagem),
                fit: BoxFit.cover,
                cacheWidth: 200,
                gaplessPlayback: true,
                errorBuilder: (_, _, _) => const SizedBox.shrink(),
              ),
            if (midia.video)
              Positioned(
                left: AureaDims.e4,
                bottom: AureaDims.e4,
                child: Icon(
                  CupertinoIcons.videocam_fill,
                  size: AureaDims.iconeSm,
                  color: AureaCores.texto,
                ),
              ),
          ],
        ),
      ),
    );
  }
}

/// UMA FORMA DA BIBLIOTECA desenhada do tamanho do ladrilho.
class _LadrilhoDeForma extends StatelessWidget {
  const _LadrilhoDeForma({
    super.key,
    required this.forma,
    required this.aoTocar,
  });

  final ShapeLibraryEntry forma;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) {
    final itens = forma.build();
    return Semantics(
      label: forma.nome,
      button: true,
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: aoTocar,
        child: DecoratedBox(
          decoration: BoxDecoration(
            color: AureaCores.campo,
            borderRadius: BorderRadius.circular(AureaDims.raioLg),
          ),
          child: Padding(
            padding: const EdgeInsets.all(AureaDims.e10),
            child: CustomPaint(
              painter: _PintorDeForma(
                shapeLibraryPreviewPath(itens),
                traco: shapeLibraryIsStrokeOnly(itens),
                cor: AureaCores.texto,
              ),
              child: const SizedBox.expand(),
            ),
          ),
        ),
      ),
    );
  }
}

class _PintorDeForma extends CustomPainter {
  _PintorDeForma(this.caminho, {required this.traco, required this.cor});

  final Path caminho;
  final bool traco;
  final Color cor;

  @override
  void paint(Canvas canvas, Size size) {
    final b = caminho.getBounds();
    if (b.longestSide <= 0) return;
    final k = size.shortestSide / b.longestSide;
    canvas.save();
    canvas.translate(size.width / 2, size.height / 2);
    canvas.scale(k, k);
    canvas.translate(-b.center.dx, -b.center.dy);
    caminho.fillType = PathFillType.evenOdd;
    final tinta = Paint()..color = cor;
    if (traco) {
      tinta
        ..style = PaintingStyle.stroke
        ..strokeWidth = 2.5 / k
        ..strokeCap = StrokeCap.round;
    }
    canvas.drawPath(caminho, tinta);
    canvas.restore();
  }

  @override
  bool shouldRepaint(_PintorDeForma old) =>
      old.caminho != caminho || old.traco != traco || old.cor != cor;
}

/// GRUPO SEM SELECAO: escolher as camadas numa lista. Veio do `EditorScreen`
/// antigo (`_agruparPorEscolha`), agora com as pecas do design system.
Future<void> agruparPorEscolha(BuildContext context, WidgetRef ref) async {
  final layers = ref.read(editorControllerProvider).layers;
  if (layers.length < 2) {
    showReasonToast(context, 'Um grupo precisa de duas ou mais camadas');
    return;
  }
  final escolhidas = <String>{};
  final ok = await mostrarAureaFolha<bool>(
    context,
    titulo: 'Agrupar quais camadas?',
    construtor: (folha) => StatefulBuilder(
      builder: (folha, setFolha) => Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Flexible(
            child: ListView(
              shrinkWrap: true,
              padding: const EdgeInsets.symmetric(
                horizontal: AureaDims.margemDoPainel,
              ),
              children: [
                for (final l in layers)
                  AureaLayerRow(
                    key: ValueKey('agrupar-${l.id}'),
                    nome: l.name,
                    icone: escolhidas.contains(l.id)
                        ? CupertinoIcons.checkmark_circle_fill
                        : CupertinoIcons.circle,
                    selecionada: escolhidas.contains(l.id),
                    aoTocar: () => setFolha(() {
                      if (!escolhidas.remove(l.id)) escolhidas.add(l.id);
                    }),
                  ),
              ],
            ),
          ),
          Padding(
            padding: const EdgeInsets.all(AureaDims.e15),
            child: CupertinoButton.filled(
              key: const ValueKey('agrupar-confirmar'),
              onPressed: escolhidas.length >= 2
                  ? () => Navigator.of(folha).pop(true)
                  : null,
              child: AppText(
                'Agrupar',
                style: TextStyle(color: AureaCores.sobreAcao),
              ),
            ),
          ),
        ],
      ),
    ),
  );
  if (ok == true && escolhidas.length >= 2) {
    final c = ref.read(editorControllerProvider.notifier);
    c.runAsOneUndo(() => agruparSelecao(ref, escolhidas));
  }
}
