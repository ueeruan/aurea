import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/ds/ds.dart';
import '../../../core/l10n/app_language.dart';
import '../../../core/ui/tocavel.dart';
import '../../editor/domain/video_project.dart';
import '../../settings/application/settings_controller.dart';
import '../domain/project_presets.dart';

/// Abre a folha de projeto novo e devolve o projeto configurado (ou nulo
/// se a pessoa fechar). Quem chama poe na lista e abre o editor.
///
/// [nomeSugerido] e o nome que o projeto recebe se a pessoa nao escrever
/// nenhum ("Projeto 3"): criar nao pode exigir inventar um nome antes de
/// ver o que se esta criando.
///
/// UMA FOLHA SO, e ela sobe em 200 ms (a folha de criar da referencia):
/// nome, proporcao, resolucao, fps, fundo e CRIAR. Nada de telas antes do
/// editor — dois toques da Inicio ate a timeline.
Future<VideoProject?> showNewProjectSheet(
  BuildContext context, {
  String? presetAspectKey,
  String? nomeSugerido,
}) {
  return mostrarAureaFolha<VideoProject>(
    context,
    titulo: 'Novo projeto',
    grande: true,
    construtor: (_) => _FolhaDoNovoProjeto(
      presetAspectKey: presetAspectKey,
      nomeSugerido: nomeSugerido,
    ),
  );
}

/// O quadro que sai de uma proporcao e uma resolucao, pela MESMA regra
/// do projeto ([VideoProject.outputWidth]): a resolucao e o lado menor,
/// entao 1080p em 9:16 e 1080 × 1920, e nao 608 × 1080.
({int largura, int altura}) quadroDoFormato(double ratio, int resolucao) =>
    ratio >= 1
    ? (largura: (resolucao * ratio).round(), altura: resolucao)
    : (largura: resolucao, altura: (resolucao / ratio).round());

/// As proporcoes da folha, na ordem de quem cria para celular: vertical
/// primeiro. As quatro pedidas e, depois, as duas que ja existiam (4:3 e a
/// medida livre) — ninguem perde o que tinha.
const _ordemDasProporcoes = ['9:16', '16:9', '1:1', '4:5', '4:3'];

/// Os fps oferecidos. 25 entra aqui (PAL) sem mexer no catalogo do
/// dominio, que tambem alimenta os Ajustes.
const _fpsDaFolha = <int>[24, 25, 30, 60];

/// OS FUNDOS: cor de CONTEUDO do projeto (vai para o arquivo e para o
/// video), e nao cor de interface — por isso e valor fixo, e nao papel do
/// tema. Preto e o padrao de sempre; os dois chroma servem a recorte.
const fundosDoProjeto = <({String nome, Color cor})>[
  (nome: 'Preto', cor: Color(0xFF000000)),
  (nome: 'Branco', cor: Color(0xFFFFFFFF)),
  (nome: 'Cinza', cor: Color(0xFF7F7F7F)),
  (nome: 'Azul', cor: Color(0xFF0A1630)),
  (nome: 'Verde', cor: Color(0xFF00B140)),
  (nome: 'Azul chroma', cor: Color(0xFF0047BB)),
];

String _rotuloDaResolucao(int altura) => switch (altura) {
  2160 => '4K',
  _ => '${altura}p',
};

class _FolhaDoNovoProjeto extends ConsumerStatefulWidget {
  const _FolhaDoNovoProjeto({this.presetAspectKey, this.nomeSugerido});

  final String? presetAspectKey;
  final String? nomeSugerido;

  @override
  ConsumerState<_FolhaDoNovoProjeto> createState() =>
      _FolhaDoNovoProjetoState();
}

class _FolhaDoNovoProjetoState extends ConsumerState<_FolhaDoNovoProjeto> {
  final _nome = TextEditingController();
  late String _proporcao;
  late int _fps;
  late int _resolucao;
  Color _fundo = fundosDoProjeto.first.cor;

  /// MEDIDA LIVRE: quem sabe o quadro exato digita os dois numeros, e a
  /// proporcao sai deles.
  bool _livre = false;
  int _larguraLivre = 1080;
  int _alturaLivre = 1350;

  @override
  void initState() {
    super.initState();
    // Os padroes vem dos Ajustes, como sempre vieram.
    final ajustes = ref.read(settingsControllerProvider);
    _proporcao = widget.presetAspectKey ?? ajustes.defaultAspectKey;
    _fps = ajustes.defaultFps;
    _resolucao = ajustes.defaultResolution;
  }

  @override
  void dispose() {
    _nome.dispose();
    super.dispose();
  }

  ({int largura, int altura}) get _quadro => _livre
      ? (largura: _larguraLivre, altura: _alturaLivre)
      : quadroDoFormato(
          ProjectPresets.aspectByKey(_proporcao).ratio,
          _resolucao,
        );

  void _criar() {
    final nome = _nome.text.trim();
    final projeto = VideoProject.empty(
      nome.isEmpty ? (widget.nomeSugerido ?? 'Projeto sem titulo') : nome,
      aspectRatio: _livre
          ? _larguraLivre / _alturaLivre
          : ProjectPresets.aspectByKey(_proporcao).ratio,
      fps: _fps,
      resolutionHeight: _livre
          ? (_larguraLivre < _alturaLivre ? _larguraLivre : _alturaLivre)
          : _resolucao,
    );
    Navigator.of(context).pop(
      _fundo == projeto.backgroundColor
          ? projeto
          : projeto.copyWith(backgroundColor: _fundo),
    );
  }

  @override
  Widget build(BuildContext context) {
    final teclado = MediaQuery.viewInsetsOf(context).bottom;
    final quadro = _quadro;
    return SingleChildScrollView(
      padding: EdgeInsets.fromLTRB(
        AureaDims.margemDoPainel,
        0,
        AureaDims.margemDoPainel,
        AureaDims.e15 + teclado,
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          const _Titulo('Nome'),
          CupertinoTextField(
            key: const ValueKey('projeto-nome'),
            controller: _nome,
            placeholder: widget.nomeSugerido ?? 'Projeto sem titulo',
            textCapitalization: TextCapitalization.sentences,
            style: AureaEstilos.corpo.copyWith(fontSize: 15),
            placeholderStyle: AureaEstilos.corpo.copyWith(
              fontSize: 15,
              color: AureaCores.textoSecundario,
            ),
            cursorColor: AureaCores.destaque,
            padding: const EdgeInsets.symmetric(
              horizontal: AureaDims.e10 + 2,
              vertical: AureaDims.e10 + 2,
            ),
            decoration: BoxDecoration(
              color: AureaCores.campo,
              borderRadius: BorderRadius.circular(AureaDims.raioXl),
            ),
            onSubmitted: (_) => _criar(),
          ),
          const _Titulo('Proporção'),
          _Fichas(
            children: [
              for (final chave in _ordemDasProporcoes)
                _Ficha(
                  chave: 'formato-$chave',
                  rotulo: chave,
                  ativa: !_livre && _proporcao == chave,
                  aoTocar: () => setState(() {
                    _livre = false;
                    _proporcao = chave;
                  }),
                ),
              _Ficha(
                chave: 'formato-livre',
                rotulo: 'Livre',
                traduzir: true,
                ativa: _livre,
                aoTocar: () => setState(() => _livre = true),
              ),
            ],
          ),
          if (_livre) ...[
            const SizedBox(height: AureaDims.e6),
            Row(
              children: [
                Expanded(
                  child: _MedidaLivre(
                    rotulo: 'Largura',
                    campo: AureaValueField(
                      key: const ValueKey('livre-largura'),
                      valor: _larguraLivre.toDouble(),
                      casas: 0,
                      min: 64,
                      max: 7680,
                      largura: 72,
                      titulo: 'Largura',
                      aoMudar: (v) =>
                          setState(() => _larguraLivre = v.round()),
                    ),
                  ),
                ),
                const SizedBox(width: AureaDims.e10),
                Expanded(
                  child: _MedidaLivre(
                    rotulo: 'Altura',
                    campo: AureaValueField(
                      key: const ValueKey('livre-altura'),
                      valor: _alturaLivre.toDouble(),
                      casas: 0,
                      min: 64,
                      max: 7680,
                      largura: 72,
                      titulo: 'Altura',
                      aoMudar: (v) =>
                          setState(() => _alturaLivre = v.round()),
                    ),
                  ),
                ),
              ],
            ),
          ] else ...[
            // Na medida livre os numeros JA SAO a resolucao.
            const _Titulo('Resolução'),
            _Fichas(
              children: [
                for (final r in ProjectPresets.resolutions)
                  _Ficha(
                    chave: 'resolucao-$r',
                    rotulo: _rotuloDaResolucao(r),
                    ativa: _resolucao == r,
                    aoTocar: () => setState(() => _resolucao = r),
                  ),
              ],
            ),
          ],
          const _Titulo('Quadros por segundo'),
          _Fichas(
            children: [
              for (final f in _fpsDaFolha)
                _Ficha(
                  chave: 'fps-$f',
                  rotulo: '$f',
                  ativa: _fps == f,
                  aoTocar: () => setState(() => _fps = f),
                ),
            ],
          ),
          const _Titulo('Fundo'),
          Wrap(
            spacing: AureaDims.e6,
            runSpacing: AureaDims.e6,
            children: [
              for (var i = 0; i < fundosDoProjeto.length; i++)
                _Amostra(
                  key: ValueKey('fundo-$i'),
                  nome: fundosDoProjeto[i].nome,
                  cor: fundosDoProjeto[i].cor,
                  ativa: _fundo == fundosDoProjeto[i].cor,
                  aoTocar: () =>
                      setState(() => _fundo = fundosDoProjeto[i].cor),
                ),
            ],
          ),
          const SizedBox(height: AureaDims.e20),
          // A FICHA VIVA: o quadro na proporcao real e os numeros que vao
          // para o arquivo, mudando com cada escolha.
          Row(
            children: [
              _MolduraDoQuadro(
                largura: quadro.largura,
                altura: quadro.altura,
                fundo: _fundo,
              ),
              const SizedBox(width: AureaDims.e10),
              Expanded(
                child: Text(
                  '${quadro.largura} × ${quadro.altura} · $_fps fps',
                  key: const ValueKey('projeto-ficha'),
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: AureaEstilos.valor.copyWith(
                    color: AureaCores.textoSecundario,
                  ),
                ),
              ),
            ],
          ),
          const SizedBox(height: AureaDims.e10),
          Tocavel(
            key: const ValueKey('criar-projeto'),
            haptico: true,
            onTap: _criar,
            child: Container(
              height: 48,
              alignment: Alignment.center,
              decoration: BoxDecoration(
                color: AureaCores.acao,
                borderRadius: BorderRadius.circular(AureaDims.raioPilula),
              ),
              child: AppText(
                'Criar projeto',
                maxLines: 1,
                style: TextStyle(
                  fontSize: 15,
                  fontWeight: FontWeight.w600,
                  color: AureaCores.sobreAcao,
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

/// Titulo de secao da folha: pequeno e apagado, para nao competir com a
/// escolha.
class _Titulo extends StatelessWidget {
  const _Titulo(this.texto);

  final String texto;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.only(
      top: AureaDims.e15,
      bottom: AureaDims.e6,
    ),
    child: AppText(texto, style: AureaEstilos.secao),
  );
}

/// Um dos dois numeros da medida livre: o rotulo encolhe, a caixa nao.
class _MedidaLivre extends StatelessWidget {
  const _MedidaLivre({required this.rotulo, required this.campo});

  final String rotulo;
  final Widget campo;

  @override
  Widget build(BuildContext context) => Row(
    children: [
      Flexible(
        child: AppText(
          rotulo,
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
          style: AureaEstilos.propriedade,
        ),
      ),
      const SizedBox(width: AureaDims.e6),
      campo,
    ],
  );
}

class _Fichas extends StatelessWidget {
  const _Fichas({required this.children});

  final List<Widget> children;

  @override
  Widget build(BuildContext context) =>
      Wrap(spacing: AureaDims.e6, children: children);
}

/// Uma [AureaChip] com alvo de toque de 40: a pilula tem 28 de altura, e
/// o respiro em volta tambem responde — o dedo nao precisa acertar o
/// desenho.
class _Ficha extends StatelessWidget {
  const _Ficha({
    required this.chave,
    required this.rotulo,
    required this.ativa,
    required this.aoTocar,
    this.traduzir = false,
  });

  final String chave;
  final String rotulo;
  final bool ativa;
  final VoidCallback aoTocar;
  final bool traduzir;

  @override
  Widget build(BuildContext context) => GestureDetector(
    key: ValueKey(chave),
    behavior: HitTestBehavior.opaque,
    onTap: aoTocar,
    child: Padding(
      padding: const EdgeInsets.symmetric(vertical: AureaDims.e6),
      child: AureaChip(
        rotulo: rotulo,
        ativo: ativa,
        traduzir: traduzir,
        aoTocar: aoTocar,
      ),
    ),
  );
}

/// Uma cor de fundo: o disco da cor sobre um circulo de campo (o preto
/// precisa de um tom atras para aparecer na folha escura). A escolhida
/// ganha o fundo do destaque e a marca.
class _Amostra extends StatelessWidget {
  const _Amostra({
    super.key,
    required this.nome,
    required this.cor,
    required this.ativa,
    required this.aoTocar,
  });

  final String nome;
  final Color cor;
  final bool ativa;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) {
    final claro = cor.computeLuminance() > .5;
    return Semantics(
      button: true,
      selected: ativa,
      label: translate(context, nome),
      child: Tocavel(
        onTap: aoTocar,
        child: Container(
          // 40 e nao 44: as seis cores cabem numa linha ja em 320 de largura.
          width: AureaDims.toqueMinimo,
          height: AureaDims.toqueMinimo,
          alignment: Alignment.center,
          decoration: BoxDecoration(
            color: ativa ? AureaCores.destaqueApagado : AureaCores.campo,
            shape: BoxShape.circle,
          ),
          child: Container(
            width: 26,
            height: 26,
            decoration: BoxDecoration(color: cor, shape: BoxShape.circle),
            child: ativa
                ? Icon(
                    CupertinoIcons.checkmark_alt,
                    size: AureaDims.iconeSm,
                    color: claro
                        ? AureaCores.palco
                        : AureaCores.texto,
                  )
                : null,
          ),
        ),
      ),
    );
  }
}

/// O QUADRO DO PROJETO em miniatura, na proporcao real e na cor de fundo
/// escolhida — ver a forma e mais rapido que ler "4:5".
class _MolduraDoQuadro extends StatelessWidget {
  const _MolduraDoQuadro({
    required this.largura,
    required this.altura,
    required this.fundo,
  });

  final int largura;
  final int altura;
  final Color fundo;

  @override
  Widget build(BuildContext context) {
    const lado = 28.0;
    final r = (largura / altura).clamp(.2, 5.0);
    return Container(
      width: lado,
      height: lado,
      alignment: Alignment.center,
      child: Container(
        width: r >= 1 ? lado : lado * r,
        height: r >= 1 ? lado / r : lado,
        decoration: BoxDecoration(
          // Fundo muito escuro some na folha escura: o campo alto por baixo
          // desenha o contorno sem borda.
          color: fundo.computeLuminance() < .02 ? AureaCores.campoAlto : fundo,
          borderRadius: BorderRadius.circular(AureaDims.raioSm),
        ),
      ),
    );
  }
}
