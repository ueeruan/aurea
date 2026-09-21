import 'dart:async';
import 'dart:io';

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:url_launcher/url_launcher.dart';

import '../../../../core/l10n/app_language.dart';
import '../../../../core/ui/am_colors.dart';
import '../../../../core/ui/snack.dart';
import '../../../../core/ui/tocavel.dart';
import '../../application/sketchfab_service.dart';
import '../../domain/analise_do_modelo.dart';
import '../../domain/model_import3d.dart';
import '../widgets/importacao_3d.dart';

/// ABRE O ACERVO DO SKETCHFAB e devolve o id do no criado, ou `null` se o
/// dono saiu sem importar nada.
Future<String?> abrirTelaDoSketchfab(
  BuildContext context, {
  required Duration playhead,
  String? sceneId,
}) => Navigator.of(context, rootNavigator: true).push<String>(
  MaterialPageRoute<String>(
    builder: (_) => TelaDoSketchfab(playhead: playhead, sceneId: sceneId),
  ),
);

/// A TELA DO SKETCHFAB: buscar, ver a ficha, baixar e importar.
///
/// A BUSCA FUNCIONA SEM CONTA — e publica na Data API v3. O token so
/// aparece na hora de baixar, e ate la o botao diz "Conectar conta". Pedir
/// credencial antes de a pessoa ver o acervo e o jeito mais rapido de ela
/// desistir.
///
/// O CAMINHO DEPOIS DO DOWNLOAD E O MESMO DO SELETOR DO APARELHO:
/// [concluirImportacao3D] — aviso de modelo pesado, cinco mapas, conferencia
/// do motor e credito preso ao no. Nenhum renderizador novo entra aqui.
class TelaDoSketchfab extends ConsumerStatefulWidget {
  const TelaDoSketchfab({super.key, required this.playhead, this.sceneId});

  final Duration playhead;

  /// Com cena, o modelo entra como mais um objeto nela; sem cena, nasce
  /// uma camada nova.
  final String? sceneId;

  @override
  ConsumerState<TelaDoSketchfab> createState() => _TelaDoSketchfabState();
}

class _TelaDoSketchfabState extends ConsumerState<TelaDoSketchfab> {
  static const _espera = Duration(milliseconds: 400);

  final _campo = TextEditingController();
  final _rolagem = ScrollController();

  Timer? _debounce;
  String _termo = '';
  bool _soLeves = false;

  List<ModeloDoSketchfab> _itens = const [];
  String? _proxima;
  bool _carregando = false;
  bool _carregandoMais = false;
  String? _erro;

  /// Cresce a cada busca nova: uma resposta que chega atrasada, de um termo
  /// que o dono ja trocou, e descartada em vez de sobrescrever a lista.
  int _geracao = 0;

  String? _token;

  _ImportacaoEmCurso? _importacao;

  @override
  void initState() {
    super.initState();
    _rolagem.addListener(_aoRolar);
    unawaited(_carregarToken());
    unawaited(_buscar());
  }

  @override
  void dispose() {
    _debounce?.cancel();
    _rolagem.dispose();
    _campo.dispose();
    _importacao?.dispose();
    super.dispose();
  }

  SketchfabService get _servico => ref.read(sketchfabServiceProvider);

  Future<void> _carregarToken() async {
    try {
      final t = await ref.read(cofreDoTokenProvider).ler();
      if (mounted) setState(() => _token = t);
    } catch (_) {
      // Sem cofre (teste sem preferencias): segue sem conta conectada.
    }
  }

  // ----------------------------------------------------------------- busca

  void _aoDigitar(String texto) {
    _debounce?.cancel();
    // DIGITAR NAO E BUSCAR. Sem esta espera, "castelo" dispararia sete
    // pedidos e as respostas chegariam fora de ordem.
    _debounce = Timer(_espera, () {
      if (texto.trim() == _termo) return;
      _termo = texto.trim();
      unawaited(_buscar());
    });
  }

  Future<void> _buscar() async {
    final geracao = ++_geracao;
    setState(() {
      _carregando = true;
      _erro = null;
    });
    try {
      final pagina = await _servico.buscar(
        termo: _termo,
        maxFaces: _soLeves ? 100000 : null,
        token: _token,
      );
      if (!mounted || geracao != _geracao) return;
      setState(() {
        _itens = pagina.itens;
        _proxima = pagina.proxima;
        _carregando = false;
      });
    } on SketchfabException catch (e) {
      if (!mounted || geracao != _geracao) return;
      setState(() {
        _carregando = false;
        _erro = e.mensagem;
      });
    }
  }

  void _aoRolar() {
    if (!_rolagem.hasClients || _proxima == null) return;
    final falta = _rolagem.position.maxScrollExtent - _rolagem.position.pixels;
    if (falta < 600) unawaited(_maisUmaPagina());
  }

  Future<void> _maisUmaPagina() async {
    final proxima = _proxima;
    if (proxima == null || _carregandoMais || _carregando) return;
    final geracao = _geracao;
    setState(() => _carregandoMais = true);
    try {
      final pagina = await _servico.buscar(proxima: proxima, token: _token);
      if (!mounted || geracao != _geracao) return;
      setState(() {
        _itens = [..._itens, ...pagina.itens];
        _proxima = pagina.proxima;
        _carregandoMais = false;
      });
    } on SketchfabException catch (e) {
      if (!mounted || geracao != _geracao) return;
      setState(() {
        _carregandoMais = false;
        // A pagina seguinte nao sai da lista que ja esta na tela: o que
        // falhou foi o "mais", e insistir e so rolar de novo.
        _proxima = null;
      });
      AureaSnack.show(context, e.mensagem);
    }
  }

  // ------------------------------------------------------------------ token

  Future<void> _conectarConta() async {
    final novo = await showCupertinoDialog<String?>(
      context: context,
      builder: (_) => DialogoDoTokenDoSketchfab(
        servico: _servico,
        cofre: ref.read(cofreDoTokenProvider),
        atual: _token,
      ),
    );
    if (!mounted) return;
    // O dialogo devolve '' quando o dono removeu o token.
    if (novo == null) return;
    setState(() => _token = novo.isEmpty ? null : novo);
  }

  // -------------------------------------------------------------- importar

  Future<void> _abrirDetalhe(ModeloDoSketchfab m) async {
    final acao = await showCupertinoModalPopup<String>(
      context: context,
      builder: (_) => _FolhaDoModelo(modelo: m, temToken: _token != null),
    );
    if (!mounted || acao == null) return;
    if (acao == 'creditos') {
      await Clipboard.setData(ClipboardData(text: m.credito.porExtenso));
      if (mounted) AureaSnack.show(context, 'Créditos copiados.');
      return;
    }
    if (acao != 'baixar') return;
    if (_token == null) {
      await _conectarConta();
      // CONECTOU AGORA: segue para o download sem obrigar o dono a achar o
      // mesmo cartao de novo — ele ja disse o que queria.
      if (!mounted || _token == null) return;
    }
    await _baixarEImportar(m);
  }

  /// BAIXAR E IMPORTAR, em etapas que a tela mostra.
  ///
  /// O LINK DE DOWNLOAD SO E PEDIDO AGORA: ele vence em ~300 s, e um link
  /// pedido quando o detalhe abriu ja estaria morto quando o dono decidisse.
  Future<void> _baixarEImportar(ModeloDoSketchfab m) async {
    final token = _token;
    if (token == null || _importacao != null) return;
    final curso = _ImportacaoEmCurso(m.nome);
    setState(() => _importacao = curso);
    File? baixado;
    try {
      final link = await _servico.linkDeDownload(m.uid, token: token);
      if (curso.cancelamento.cancelado) return;
      final pasta = await _servico.pastaDeDownload();
      baixado = await _servico.baixar(
        link,
        '${pasta.path}${Platform.pathSeparator}${m.uid}${link.extensao}',
        cancelamento: curso.cancelamento,
        onProgresso: (recebidos, total) {
          curso.progresso.value = total > 0 ? recebidos / total : -1;
        },
      );
      if (!mounted || curso.cancelamento.cancelado) return;
      final nodeId = await concluirImportacao3D(
        context,
        ref,
        [baixado.path],
        playhead: widget.playhead,
        sceneId: widget.sceneId,
        credito: m.credito,
        etapa: curso.etapa,
      );
      if (!mounted || nodeId == null) return;
      Navigator.of(context).pop(nodeId);
      return;
    } on DownloadCancelado {
      // Cancelar nao e erro: nada a avisar.
    } on SketchfabException catch (e) {
      if (mounted) AureaSnack.show(context, e.mensagem);
    } on ModelImportException catch (e) {
      if (mounted) AureaSnack.show(context, e.message);
    } catch (_) {
      if (mounted) {
        AureaSnack.show(
          context,
          'Não consegui importar esse modelo do Sketchfab.',
        );
      }
    } finally {
      // O PACOTE BAIXADO NAO FICA. O modelo ja foi copiado para dentro do
      // projeto (geometria e texturas), e um zip de dezenas de MB por
      // importacao encheria o aparelho em silencio.
      try {
        if (baixado != null && baixado.existsSync()) baixado.deleteSync();
      } catch (_) {
        // Arquivo preso pelo sistema: o cache se recolhe sozinho depois.
      }
      curso.dispose();
      if (mounted) setState(() => _importacao = null);
    }
  }

  // -------------------------------------------------------------------- UI

  @override
  Widget build(BuildContext context) {
    final curso = _importacao;
    return Scaffold(
      backgroundColor: AmColors.bg,
      appBar: AppBar(
        backgroundColor: AmColors.topBar,
        elevation: 0,
        title: const AppText(
          'Sketchfab',
          style: TextStyle(color: AmColors.text, fontSize: 17),
        ),
        iconTheme: const IconThemeData(color: AmColors.text),
        actions: [
          Padding(
            padding: const EdgeInsets.only(right: 8),
            child: Tocavel(
              key: const ValueKey('sketchfab-conta'),
              onTap: _conectarConta,
              child: Center(
                child: Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 10),
                  child: AppText(
                    _token == null ? 'Conectar conta' : 'Conta',
                    style: TextStyle(color: AmColors.action, fontSize: 14),
                  ),
                ),
              ),
            ),
          ),
        ],
      ),
      body: SafeArea(
        top: false,
        child: Stack(
          children: [
            Column(
              children: [
                _barraDeBusca(),
                Expanded(child: _corpo()),
                _rodape(),
              ],
            ),
            if (curso != null) _PainelDeEtapas(curso: curso),
          ],
        ),
      ),
    );
  }

  Widget _barraDeBusca() => Padding(
    padding: const EdgeInsets.fromLTRB(12, 8, 12, 4),
    child: Row(
      children: [
        Expanded(
          child: CupertinoSearchTextField(
            key: const ValueKey('sketchfab-busca'),
            controller: _campo,
            placeholder: translate(context, 'Buscar modelos 3D'),
            style: const TextStyle(color: AmColors.text, fontSize: 15),
            backgroundColor: AmColors.chip,
            onChanged: _aoDigitar,
            onSubmitted: (t) {
              _debounce?.cancel();
              _termo = t.trim();
              unawaited(_buscar());
            },
          ),
        ),
        const SizedBox(width: 8),
        Tocavel(
          key: const ValueKey('sketchfab-leves'),
          onTap: () {
            setState(() => _soLeves = !_soLeves);
            unawaited(_buscar());
          },
          child: Container(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            decoration: BoxDecoration(
              color: _soLeves ? AmColors.action : AmColors.chip,
              borderRadius: BorderRadius.circular(9),
            ),
            child: AppText(
              'Leves',
              style: TextStyle(
                color: _soLeves ? AmColors.onAction : AmColors.text,
                fontSize: 13,
                fontWeight: FontWeight.w600,
              ),
            ),
          ),
        ),
      ],
    ),
  );

  Widget _corpo() {
    final erro = _erro;
    if (erro != null) {
      return _Recado(
        texto: erro,
        acao: 'Tentar de novo',
        aoTocar: () => unawaited(_buscar()),
      );
    }
    if (_carregando && _itens.isEmpty) {
      return const Center(
        key: ValueKey('sketchfab-carregando'),
        child: CupertinoActivityIndicator(),
      );
    }
    if (_itens.isEmpty) {
      return const _Recado(texto: 'Nenhum modelo encontrado.');
    }
    return GridView.builder(
      key: const ValueKey('sketchfab-grade'),
      controller: _rolagem,
      padding: const EdgeInsets.fromLTRB(12, 8, 12, 12),
      gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(
        crossAxisCount: 2,
        mainAxisSpacing: 14,
        crossAxisSpacing: 12,
        childAspectRatio: 0.82,
      ),
      itemCount: _itens.length + (_carregandoMais ? 1 : 0),
      itemBuilder: (_, i) {
        if (i >= _itens.length) {
          return const Center(child: CupertinoActivityIndicator());
        }
        final m = _itens[i];
        return _CartaoDoModelo(modelo: m, aoTocar: () => _abrirDetalhe(m));
      },
    );
  }

  /// A EXIGENCIA DA MARCA: as diretrizes do Sketchfab pedem que o app diga
  /// claramente de onde vem o acervo.
  Widget _rodape() => Padding(
    padding: const EdgeInsets.fromLTRB(16, 0, 16, 10),
    child: AppText(
      'Modelos 3D fornecidos por Sketchfab',
      style: TextStyle(color: AmColors.muted, fontSize: 11),
    ),
  );
}

// ==========================================================================
// O CARTAO DA GRADE
// ==========================================================================

class _CartaoDoModelo extends StatelessWidget {
  const _CartaoDoModelo({required this.modelo, required this.aoTocar});

  final ModeloDoSketchfab modelo;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) {
    final pacote = modelo.pacote;
    return Tocavel(
      key: ValueKey('sketchfab-cartao-${modelo.uid}'),
      onTap: aoTocar,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Expanded(
            child: ClipRRect(
              borderRadius: BorderRadius.circular(10),
              child: _Miniatura(url: modelo.miniatura(512)),
            ),
          ),
          const SizedBox(height: 6),
          Text(
            modelo.nome,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: const TextStyle(
              color: AmColors.text,
              fontSize: 13,
              fontWeight: FontWeight.w600,
            ),
          ),
          Text(
            modelo.autor,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: TextStyle(color: AmColors.muted, fontSize: 11),
          ),
          const SizedBox(height: 3),
          Row(
            children: [
              Flexible(child: _Selo(texto: modelo.licenca)),
              const SizedBox(width: 6),
              AppTextMoldado(
                '{0} tri',
                [contagemLegivel(pacote?.faces ?? modelo.faces)],
                style: TextStyle(color: AmColors.muted, fontSize: 10),
              ),
              if (pacote != null && pacote.bytes > 0) ...[
                Text(
                  '  ·  ${memoriaLegivel(pacote.bytes)}',
                  style: TextStyle(color: AmColors.muted, fontSize: 10),
                ),
              ],
            ],
          ),
        ],
      ),
    );
  }
}

/// O SELO DA LICENCA. Sem caixa com borda: um rotulo curto na cor certa
/// basta, e em CC0 ele nem precisa chamar atencao.
class _Selo extends StatelessWidget {
  const _Selo({required this.texto});

  final String texto;

  /// Licenca que proibe uso comercial ou obras derivadas: o app informa,
  /// nao policia — a decisao de usar assim e de quem edita.
  bool get _restrita {
    final l = texto.toLowerCase();
    return l.contains('noncommercial') ||
        l.contains('noderiv') ||
        l.contains('editorial');
  }

  @override
  Widget build(BuildContext context) {
    if (texto.isEmpty) return const SizedBox.shrink();
    return Text(
      texto,
      maxLines: 1,
      overflow: TextOverflow.ellipsis,
      style: TextStyle(
        color: _restrita ? AmColors.pink : AmColors.muted,
        fontSize: 10,
        fontWeight: FontWeight.w600,
      ),
    );
  }
}

class _Miniatura extends StatelessWidget {
  const _Miniatura({this.url});

  final String? url;

  @override
  Widget build(BuildContext context) {
    final vazio = Container(
      color: AmColors.panelHigh,
      alignment: Alignment.center,
      child: Icon(CupertinoIcons.cube_box, size: 26, color: AmColors.muted),
    );
    final endereco = url;
    if (endereco == null) return vazio;
    return Image.network(
      endereco,
      fit: BoxFit.cover,
      width: double.infinity,
      height: double.infinity,
      // Sem miniatura a grade nao pode quebrar: a caixa vazia ocupa o
      // mesmo espaco e o cartao continua tocavel.
      errorBuilder: (_, _, _) => vazio,
      loadingBuilder: (_, filho, progresso) =>
          progresso == null ? filho : Container(color: AmColors.panelHigh),
    );
  }
}

// ==========================================================================
// O DETALHE
// ==========================================================================

class _FolhaDoModelo extends StatelessWidget {
  const _FolhaDoModelo({required this.modelo, required this.temToken});

  final ModeloDoSketchfab modelo;
  final bool temToken;

  Future<void> _abrir(String? url) async {
    if (url == null) return;
    try {
      await launchUrl(Uri.parse(url), mode: LaunchMode.externalApplication);
    } catch (_) {
      // Sem navegador: o link simplesmente nao abre.
    }
  }

  @override
  Widget build(BuildContext context) {
    final ficha = modelo.ficha;
    final pacote = modelo.pacote;
    final linha = TextStyle(color: AmColors.muted, fontSize: 12, height: 1.5);
    return CupertinoActionSheet(
      title: Text(
        modelo.nome,
        style: const TextStyle(color: AmColors.text, fontSize: 15),
      ),
      message: Column(
        key: const ValueKey('sketchfab-detalhe'),
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          if (modelo.miniatura(720) != null)
            ClipRRect(
              borderRadius: BorderRadius.circular(10),
              child: AspectRatio(
                aspectRatio: 16 / 9,
                child: _Miniatura(url: modelo.miniatura(720)),
              ),
            ),
          const SizedBox(height: 10),
          AppTextMoldado('por {0}', [modelo.autor], style: linha),
          if (modelo.licenca.isNotEmpty) _Selo(texto: modelo.licenca),
          const SizedBox(height: 6),
          AppTextMoldado('Triângulos: {0} · Vértices: {1}', [
            contagemLegivel(ficha.triangulos),
            contagemLegivel(ficha.vertices),
          ], style: linha),
          if (pacote != null)
            AppTextMoldado('Texturas: {0} até {1} px · Arquivo: {2}', [
              pacote.texturas,
              pacote.maiorTextura,
              memoriaLegivel(pacote.bytes),
            ], style: linha),
          if (modelo.animacoes > 0)
            AppTextMoldado('Animações: {0}', [modelo.animacoes], style: linha),
          const SizedBox(height: 6),
          // A ATRIBUICAO, DO JEITO QUE VAI FICAR PRESA AO MODELO. As
          // licencas Creative Commons pedem titulo, autor, origem e licenca
          // onde quer que a obra apareca — mostrar a linha aqui e o que
          // deixa o dono ver, ANTES de baixar, o que ele vai ter de creditar.
          const AppText(
            'Créditos',
            style: TextStyle(
              color: AmColors.text,
              fontSize: 12,
              fontWeight: FontWeight.w600,
            ),
          ),
          Text(
            modelo.credito.porExtenso,
            key: const ValueKey('sketchfab-creditos'),
            style: linha,
          ),
          if (ficha.pesado())
            AppText(
              'Modelo pesado: o app vai oferecer reduzir na importação.',
              style: linha.copyWith(color: AmColors.pink),
            ),
        ],
      ),
      actions: [
        CupertinoActionSheetAction(
          key: const ValueKey('sketchfab-baixar'),
          isDefaultAction: true,
          onPressed: () => Navigator.of(context).pop('baixar'),
          child: AppText(temToken ? 'Baixar e importar' : 'Conectar conta'),
        ),
        CupertinoActionSheetAction(
          key: const ValueKey('sketchfab-copiar-creditos'),
          onPressed: () => Navigator.of(context).pop('creditos'),
          child: const AppText('Copiar créditos'),
        ),
        if (modelo.viewerUrl != null)
          CupertinoActionSheetAction(
            onPressed: () => _abrir(modelo.viewerUrl),
            child: const AppText('Ver no Sketchfab'),
          ),
        if (modelo.autorUrl != null)
          CupertinoActionSheetAction(
            onPressed: () => _abrir(modelo.autorUrl),
            child: const AppText('Ver o autor'),
          ),
      ],
      cancelButton: CupertinoActionSheetAction(
        key: const ValueKey('sketchfab-detalhe-fechar'),
        onPressed: () => Navigator.of(context).pop(),
        child: const AppText('Cancelar'),
      ),
    );
  }
}

// ==========================================================================
// AS ETAPAS
// ==========================================================================

/// O QUE ESTA ACONTECENDO AGORA, e o botao de desistir.
///
/// Vive na tela (e nao numa rota nova) de proposito: o aviso de modelo
/// pesado abre POR CIMA deste painel, e empilhar dialogo sobre dialogo em
/// navegadores diferentes e de onde vem tela presa.
class _ImportacaoEmCurso {
  _ImportacaoEmCurso(this.nome);

  final String nome;
  final etapa = ValueNotifier(EtapaDaImportacao3D.baixando);

  /// 0..1 enquanto baixa; -1 = tamanho desconhecido.
  final progresso = ValueNotifier<double>(0);
  final cancelamento = Cancelamento();

  /// Ha DOIS caminhos ate aqui — o `finally` da importacao e o `dispose` da
  /// tela — e quando a tela fecha sozinha (importou, e a rota saiu) os dois
  /// passam. Soltar um `ValueNotifier` duas vezes derruba por asercao.
  bool _solto = false;

  void dispose() {
    if (_solto) return;
    _solto = true;
    etapa.dispose();
    progresso.dispose();
  }
}

class _PainelDeEtapas extends StatelessWidget {
  const _PainelDeEtapas({required this.curso});

  final _ImportacaoEmCurso curso;

  @override
  Widget build(BuildContext context) => Positioned.fill(
    child: ColoredBox(
      color: const Color(0xCC000000),
      child: Center(
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 300),
          child: Column(
            key: const ValueKey('sketchfab-etapas'),
            mainAxisSize: MainAxisSize.min,
            children: [
              Text(
                curso.nome,
                maxLines: 2,
                textAlign: TextAlign.center,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(color: AmColors.text, fontSize: 14),
              ),
              const SizedBox(height: 10),
              ValueListenableBuilder<EtapaDaImportacao3D>(
                valueListenable: curso.etapa,
                builder: (_, etapa, _) => ValueListenableBuilder<double>(
                  valueListenable: curso.progresso,
                  builder: (_, p, _) {
                    final baixando = etapa == EtapaDaImportacao3D.baixando;
                    final pct = (p * 100).clamp(0, 100).round();
                    return Column(
                      children: [
                        AppText(
                          etapa.rotulo,
                          style: TextStyle(
                            color: AmColors.muted,
                            fontSize: 13,
                          ),
                        ),
                        const SizedBox(height: 8),
                        SizedBox(
                          width: 220,
                          child: LinearProgressIndicator(
                            value: baixando && p >= 0 ? p : null,
                            minHeight: 3,
                            backgroundColor: AmColors.chip,
                            color: AmColors.action,
                          ),
                        ),
                        if (baixando && p >= 0) ...[
                          const SizedBox(height: 6),
                          AppTextMoldado('{0}%', [
                            pct,
                          ], style: TextStyle(color: AmColors.muted, fontSize: 11)),
                        ],
                      ],
                    );
                  },
                ),
              ),
              const SizedBox(height: 14),
              Tocavel(
                key: const ValueKey('sketchfab-cancelar'),
                onTap: curso.cancelamento.cancelar,
                child: Padding(
                  padding: const EdgeInsets.symmetric(
                    horizontal: 16,
                    vertical: 8,
                  ),
                  child: AppText(
                    'Cancelar',
                    style: TextStyle(color: AmColors.action, fontSize: 14),
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    ),
  );
}

// ==========================================================================
// O TOKEN
// ==========================================================================

/// CONECTAR A CONTA COLANDO O TOKEN DA DATA API.
///
/// E o caminho PROVISORIO: as diretrizes do Sketchfab pedem login por
/// OAuth 2.0, que exige o dono registrar o app (client_id, redirect URI) e
/// o app ganhar deep link ou webview — nenhum dos dois existe hoje.
///
/// Devolve o token novo, `''` quando o dono removeu, e `null` quando saiu
/// sem mexer.
class DialogoDoTokenDoSketchfab extends StatefulWidget {
  const DialogoDoTokenDoSketchfab({
    super.key,
    required this.servico,
    required this.cofre,
    this.atual,
  });

  final SketchfabService servico;
  final CofreDoToken cofre;
  final String? atual;

  @override
  State<DialogoDoTokenDoSketchfab> createState() =>
      _DialogoDoTokenDoSketchfabState();
}

class _DialogoDoTokenDoSketchfabState extends State<DialogoDoTokenDoSketchfab> {
  late final _campo = TextEditingController(text: widget.atual ?? '');
  String _recado = '';
  bool _testando = false;

  @override
  void dispose() {
    _campo.dispose();
    super.dispose();
  }

  Future<void> _colar() async {
    final dados = await Clipboard.getData(Clipboard.kTextPlain);
    final texto = dados?.text?.trim();
    if (texto == null || texto.isEmpty || !mounted) return;
    setState(() {
      _campo.text = texto;
      _recado = '';
    });
  }

  /// TESTAR = perguntar a propria API de quem e este token. Nao ha jeito
  /// melhor: um token so e valido se o servidor disser que e.
  Future<void> _testar() async {
    final token = _campo.text.trim();
    if (token.isEmpty) {
      setState(() => _recado = 'Cole o token antes de testar.');
      return;
    }
    setState(() {
      _testando = true;
      _recado = '';
    });
    try {
      final conta = await widget.servico.eu(token);
      if (!mounted) return;
      await widget.cofre.gravar(token);
      if (!mounted) return;
      Navigator.of(context).pop(token);
      AureaSnack.show(
        context,
        moldar(context, 'Conta do Sketchfab conectada: {0}', [conta.nome]),
      );
    } on SketchfabException catch (e) {
      if (!mounted) return;
      setState(() {
        _testando = false;
        _recado = e.mensagem;
      });
    }
  }

  Future<void> _remover() async {
    await widget.cofre.apagar();
    if (mounted) Navigator.of(context).pop('');
  }

  @override
  Widget build(BuildContext context) => CupertinoAlertDialog(
    title: const AppText('Conta do Sketchfab'),
    content: Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        const SizedBox(height: 6),
        const AppText(
          'A busca funciona sem conta. Baixar exige o seu token de API do '
          'Sketchfab.',
          style: TextStyle(fontSize: 12, height: 1.35),
        ),
        const SizedBox(height: 10),
        CupertinoTextField(
          key: const ValueKey('sketchfab-token-campo'),
          controller: _campo,
          // O TOKEN NAO APARECE NA TELA: e credencial, e a tela pode estar
          // sendo gravada ou espelhada.
          obscureText: true,
          autocorrect: false,
          enableSuggestions: false,
          placeholder: translate(context, 'Token de API'),
          style: const TextStyle(fontSize: 13),
        ),
        const SizedBox(height: 8),
        Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            CupertinoButton(
              key: const ValueKey('sketchfab-token-colar'),
              padding: const EdgeInsets.symmetric(horizontal: 10),
              minimumSize: Size.zero,
              onPressed: _colar,
              child: const AppText('Colar', style: TextStyle(fontSize: 13)),
            ),
            CupertinoButton(
              key: const ValueKey('sketchfab-token-ajuda'),
              padding: const EdgeInsets.symmetric(horizontal: 10),
              minimumSize: Size.zero,
              onPressed: () async {
                try {
                  await launchUrl(
                    Uri.parse(SketchfabService.paginaDoToken),
                    mode: LaunchMode.externalApplication,
                  );
                } catch (_) {
                  // Sem navegador: o endereco fica escrito no recado.
                  if (mounted) {
                    setState(
                      () => _recado = SketchfabService.paginaDoToken,
                    );
                  }
                }
              },
              child: const AppText(
                'Onde acho?',
                style: TextStyle(fontSize: 13),
              ),
            ),
          ],
        ),
        if (_recado.isNotEmpty)
          Padding(
            padding: const EdgeInsets.only(top: 4),
            child: AppText(
              _recado,
              textAlign: TextAlign.center,
              style: const TextStyle(fontSize: 12),
            ),
          ),
      ],
    ),
    actions: [
      CupertinoDialogAction(
        key: const ValueKey('sketchfab-token-testar'),
        isDefaultAction: true,
        onPressed: _testando ? null : _testar,
        child: AppText(_testando ? 'Testando…' : 'Testar e salvar'),
      ),
      if (widget.atual != null)
        CupertinoDialogAction(
          key: const ValueKey('sketchfab-token-remover'),
          isDestructiveAction: true,
          onPressed: _remover,
          child: const AppText('Remover'),
        ),
      CupertinoDialogAction(
        onPressed: () => Navigator.of(context).pop(),
        child: const AppText('Fechar'),
      ),
    ],
  );
}

// ==========================================================================

class _Recado extends StatelessWidget {
  const _Recado({required this.texto, this.acao, this.aoTocar});

  final String texto;
  final String? acao;
  final VoidCallback? aoTocar;

  @override
  Widget build(BuildContext context) => Center(
    child: Padding(
      padding: const EdgeInsets.symmetric(horizontal: 32),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          AppText(
            texto,
            textAlign: TextAlign.center,
            style: TextStyle(color: AmColors.muted, fontSize: 13),
          ),
          if (acao != null) ...[
            const SizedBox(height: 10),
            Tocavel(
              key: const ValueKey('sketchfab-tentar'),
              onTap: aoTocar,
              child: Padding(
                padding: const EdgeInsets.all(8),
                child: AppText(
                  acao!,
                  style: TextStyle(color: AmColors.action, fontSize: 14),
                ),
              ),
            ),
          ],
        ],
      ),
    ),
  );
}
