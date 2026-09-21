import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart' show MaterialPageRoute;
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/pedir_nome.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../../help/presentation/quick_guide_screen.dart';
import '../../../../projects/domain/project_presets.dart';
import '../../../application/editor_controller.dart';
import '../../../application/freehand_session.dart' show onionSkinProvider;
import '../../../application/preview_stats.dart' show debugOverlayProvider;
import '../../../application/ui/pro_mode.dart';
import '../../../domain/layer_meta.dart' show parseCsv;
import '../paineis/pecas_centrais.dart' show respiroDoPainel;

// ===========================================================================
// ⚙ PROJETO — as configuracoes da composicao, no design system novo
// ===========================================================================
//
// Composicao (proporcao, resolucao, quadros, fundo), previa (casca de
// cebola), guias, motion blur da composicao, paleta, propriedades expostas
// do template, dados (CSV), ajuda e o diagnostico na tela.
//
// A folha le o projeto por conta propria (um `Consumer` dentro dela) em vez
// de usar o `ref` de quem a abriu: quem abre e a barra do projeto, que sai
// da arvore quando a selecao muda — e um `ref` de widget morto lanca na
// primeira leitura. O `ref` do parametro fica so pela assinatura publica.

/// A fracao da tela que a folha pode ocupar. Sobra uma faixa do editor em
/// cima: e o que diz "isto e uma folha" e o que fecha com um toque fora.
const _fracaoDaFolha = .82;

/// Altura da [AureaChip] (fixa no componente): a linha de escolha alinha o
/// rotulo pelo meio da primeira fileira de pilulas.
const _alturaDaPilula = 28.0;

/// Abre o ⚙ Projeto.
Future<void> showProjectSettingsSheet(BuildContext context, WidgetRef ref) {
  return mostrarAureaFolha<void>(
    context,
    titulo: 'Projeto',
    grande: true,
    construtor: (folha) => ConstrainedBox(
      constraints: BoxConstraints(
        maxHeight: MediaQuery.sizeOf(folha).height * _fracaoDaFolha,
      ),
      child: const _AjustesDoProjeto(),
    ),
  );
}

class _AjustesDoProjeto extends ConsumerStatefulWidget {
  const _AjustesDoProjeto();

  @override
  ConsumerState<_AjustesDoProjeto> createState() => _AjustesDoProjetoState();
}

class _AjustesDoProjetoState extends ConsumerState<_AjustesDoProjeto> {
  /// O que deu errado na ultima leitura de CSV. Fica DENTRO da folha: um
  /// aviso flutuante nasceria no editor, atras dela.
  String? _avisoDosDados;

  EditorController get _c => ref.read(editorControllerProvider.notifier);

  /// UMA ESCOLHA DELIBERADA = UM PASSO DE DESFAZER. Fora de um grupo o
  /// controlador junta edicoes a menos de 450 ms: trocar a proporcao logo
  /// depois de outro ajuste faria um desfazer levar os dois.
  void _passo(VoidCallback acao) => _c.runAsOneUndo(acao);

  /// O SELETOR DE COR COMO UM GESTO: a cor muda viva enquanto o dedo
  /// escolhe, e tudo vira UM passo de desfazer. Sem o grupo, cada pausa de
  /// meio segundo no quadro de cor virava um passo.
  Future<void> _escolherCor({
    required Color inicial,
    required ValueChanged<Color> aplicar,
    bool comAlfa = true,
  }) async {
    final c = _c;
    c.beginGesture();
    try {
      final nova = await showColorPicker(
        context,
        initial: inicial,
        withAlpha: comAlfa,
        onChanged: aplicar,
      );
      if (nova != null) aplicar(nova);
    } finally {
      c.endGesture();
    }
  }

  Future<void> _renomear(String atual) async {
    final nome = await pedirNome(
      context,
      titulo: 'Nome do projeto',
      atual: atual,
    );
    if (nome == null || !mounted) return;
    _passo(() => _c.renameProject(nome));
  }

  Future<void> _adicionarCorNaPaleta(int quantas) async {
    final nome = await pedirNome(
      context,
      titulo: 'Nome da cor',
      atual: moldar(context, 'Cor {0}', [quantas + 1]),
    );
    if (nome == null || !mounted) return;
    final cor = await showColorPicker(context, initial: AureaCores.acao);
    if (cor == null || !mounted) return;
    _passo(() => _c.setPaletteColor(nome, cor));
  }

  /// CARREGAR UM CSV: trocar a fonte e reaplicar os vinculos sao duas
  /// mutacoes, e um desfazer tem de levar as duas — senao o texto das
  /// camadas fica com os valores de um arquivo que ja nao esta no projeto.
  Future<void> _carregarCsv() async {
    try {
      final r = await FilePicker.platform.pickFiles(
        type: FileType.custom,
        allowedExtensions: const ['csv', 'txt'],
      );
      final arquivo = r?.files.single;
      final caminho = arquivo?.path;
      if (arquivo == null || caminho == null) return;
      final conteudo = await File(caminho).readAsString();
      final dados = parseCsv(conteudo, name: arquivo.name);
      if (!mounted) return;
      final c = _c;
      c.runAsOneUndo(() {
        c.setDataSource(dados);
        c.applyDataBindings();
      });
      setState(() => _avisoDosDados = null);
    } catch (_) {
      // O antigo engolia o erro e a linha simplesmente nao mudava: a
      // pessoa nao sabia se o arquivo tinha entrado.
      if (mounted) {
        setState(
          () => _avisoDosDados = translate(
            context,
            'Não foi possível ler este arquivo.',
          ),
        );
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final p = ref.watch(editorControllerProvider);
    final pro = ref.watch(proModeProvider);
    final cebola = ref.watch(onionSkinProvider);
    final diagnostico = ref.watch(debugOverlayProvider);
    final c = _c;

    final proporcaoAtual = ProjectPresets.aspects
        .where((a) => (a.ratio - p.aspectRatio).abs() < 0.01)
        .map((a) => a.key)
        .firstOrNull;
    final segundos = (p.duration.inMilliseconds / 1000).toStringAsFixed(1);

    return ListView(
      shrinkWrap: true,
      padding: respiroDoPainel,
      children: [
        LinhaDeAjuste(
          key: const ValueKey('projeto-nome'),
          icone: CupertinoIcons.pencil,
          titulo: p.name,
          tituloDoUsuario: true,
          subtitulo: 'Toque para renomear',
          seta: true,
          aoTocar: () => _renomear(p.name),
        ),
        AureaSection(
          titulo: 'Composição',
          chave: 'projeto-composicao',
          recolhivel: false,
          filhos: [
            _LinhaDeEscolha<String>(
              chave: 'projeto-proporcao',
              rotulo: 'Proporção',
              opcoes: [for (final a in ProjectPresets.aspects) a.key],
              rotuloDe: (k) => k,
              atual: proporcaoAtual,
              aoEscolher: (k) => _passo(
                () => c.setComposition(
                  aspectRatio: ProjectPresets.aspectByKey(k).ratio,
                ),
              ),
            ),
            _LinhaDeEscolha<int>(
              chave: 'projeto-resolucao',
              rotulo: 'Resolução',
              opcoes: ProjectPresets.resolutions,
              rotuloDe: ProjectPresets.resolutionLabel,
              atual: p.resolutionHeight,
              aoEscolher: (r) =>
                  _passo(() => c.setComposition(resolutionHeight: r)),
            ),
            _LinhaDeEscolha<int>(
              chave: 'projeto-fps',
              rotulo: 'Quadros',
              opcoes: ProjectPresets.fpsOptions,
              rotuloDe: (f) => '$f',
              atual: p.fps,
              aoEscolher: (f) => _passo(() => c.setComposition(fps: f)),
            ),
            AureaPropertyRow.cor(
              key: const ValueKey('projeto-fundo'),
              chave: 'projeto-fundo',
              rotulo: 'Fundo',
              cor: p.backgroundColor,
              // O fundo e a cor do VIDEO, sem transparencia: um alfa aqui
              // viraria preto no arquivo e enganaria a previa.
              aoTocar: () => _escolherCor(
                inicial: p.backgroundColor,
                comAlfa: false,
                aplicar: c.setBackgroundColor,
              ),
            ),
            // O QUADRO QUE SAI: e numero, nao rotulo (Text).
            Padding(
              key: const ValueKey('projeto-quadro'),
              padding: const EdgeInsets.only(bottom: AureaDims.e6),
              child: Text(
                '${p.outputWidth} × ${p.outputHeight} · $segundos s',
                style: AureaEstilos.propriedade,
              ),
            ),
          ],
        ),
        AureaSection(
          titulo: 'Prévia',
          chave: 'projeto-previa',
          recolhivel: false,
          filhos: [
            // Casca de cebola: quantos quadros vizinhos aparecem em
            // transparencia. Tres escolhas visiveis em vez do toque que
            // girava 0 → 1 → 2 sem mostrar o que vinha depois.
            _LinhaDeEscolha<int>(
              key: const ValueKey('projeto-cebola'),
              chave: 'projeto-cebola',
              rotulo: 'Casca de cebola',
              opcoes: const [0, 1, 2],
              rotuloDe: (n) => switch (n) {
                0 => 'Desligada',
                1 => '1 quadro',
                // Frases inteiras, e nao '$n quadros': o rotulo da pilula
                // vai ao catalogo como esta escrito.
                _ => '2 quadros',
              },
              chaveDe: (n) => '$n',
              atual: cebola,
              aoEscolher: (n) =>
                  ref.read(onionSkinProvider.notifier).state = n,
            ),
          ],
        ),
        if (pro) ...[
          AureaSection(
            titulo: 'Guias',
            chave: 'projeto-guias',
            recolhivel: false,
            filhos: [
              LinhaDeAjuste(
                key: const ValueKey('projeto-areas-seguras'),
                icone: CupertinoIcons.rectangle_dock,
                titulo: 'Áreas seguras',
                subtitulo: 'Margens de título e ação na prévia',
                ligado: p.guides.showSafeAreas,
                aoTocar: () => _passo(
                  () => c.setGuides(
                    p.guides.copyWith(showSafeAreas: !p.guides.showSafeAreas),
                  ),
                ),
              ),
              _LinhaDeEscolha<int>(
                chave: 'projeto-colunas',
                rotulo: 'Colunas',
                opcoes: const [0, 2, 3, 4, 6, 12],
                rotuloDe: (n) => n == 0 ? 'Sem' : '$n',
                atual: p.guides.columns,
                aoEscolher: (n) =>
                    _passo(() => c.setGuides(p.guides.copyWith(columns: n))),
              ),
              LinhaDeAjuste(
                key: const ValueKey('projeto-guia-vertical'),
                icone: CupertinoIcons.line_horizontal_3,
                titulo: 'Adicionar guia vertical',
                subtitulo: moldar(context, '{0} vertical, {1} horizontal', [
                  p.guides.vertical.length,
                  p.guides.horizontal.length,
                ]),
                subtituloPronto: true,
                aoTocar: () => _passo(() => c.addGuide(x: p.outputWidth / 2)),
              ),
              LinhaDeAjuste(
                key: const ValueKey('projeto-guia-horizontal'),
                icone: CupertinoIcons.line_horizontal_3,
                titulo: 'Adicionar guia horizontal',
                aoTocar: () =>
                    _passo(() => c.addGuide(y: p.outputHeight / 2)),
              ),
              if (p.guides.vertical.isNotEmpty ||
                  p.guides.horizontal.isNotEmpty)
                LinhaDeAjuste(
                  key: const ValueKey('projeto-guias-limpar'),
                  icone: CupertinoIcons.clear,
                  titulo: 'Limpar guias',
                  perigo: true,
                  aoTocar: () => _passo(
                    () => c.setGuides(
                      p.guides.copyWith(
                        vertical: const [],
                        horizontal: const [],
                      ),
                    ),
                  ),
                ),
            ],
          ),
          AureaSection(
            titulo: 'Motion blur da composição',
            chave: 'projeto-motion-blur',
            recolhivel: false,
            filhos: [
              LinhaDeAjuste(
                key: const ValueKey('projeto-motion-blur'),
                icone: CupertinoIcons.speedometer,
                titulo: 'Motion blur',
                subtitulo: p.motionBlur.enabled
                    ? moldar(context, 'Obturador {0}° · {1} amostras', [
                        p.motionBlur.shutterAngle.round(),
                        p.motionBlur.samples,
                      ])
                    : translate(
                        context,
                        'Desligado (as camadas com motion blur só borram '
                        'com isto ligado)',
                      ),
                subtituloPronto: true,
                ligado: p.motionBlur.enabled,
                aoTocar: () => _passo(
                  () => c.setMotionBlur(
                    p.motionBlur.copyWith(enabled: !p.motionBlur.enabled),
                  ),
                ),
              ),
              if (p.motionBlur.enabled) ...[
                _LinhaDeEscolha<int>(
                  chave: 'projeto-obturador',
                  rotulo: 'Obturador',
                  opcoes: const [90, 180, 270, 360],
                  rotuloDe: (a) => '$a°',
                  atual: p.motionBlur.shutterAngle.round(),
                  aoEscolher: (a) => _passo(
                    () => c.setMotionBlur(
                      p.motionBlur.copyWith(shutterAngle: a.toDouble()),
                    ),
                  ),
                ),
                _LinhaDeEscolha<int>(
                  chave: 'projeto-amostras',
                  rotulo: 'Amostras',
                  opcoes: const [8, 16, 32],
                  rotuloDe: (n) => '$n',
                  atual: p.motionBlur.samples,
                  aoEscolher: (n) => _passo(
                    () => c.setMotionBlur(p.motionBlur.copyWith(samples: n)),
                  ),
                ),
              ],
            ],
          ),
          AureaSection(
            titulo: 'Paleta do projeto',
            chave: 'projeto-paleta',
            recolhivel: false,
            filhos: [
              for (final e in p.palette.entries.entries)
                LinhaDeAjuste(
                  key: ValueKey('projeto-paleta-${e.key}'),
                  titulo: e.key,
                  tituloDoUsuario: true,
                  subtitulo: 'Toque para trocar a cor · segure para tirar',
                  fim: _Amostra(cor: e.value),
                  aoTocar: () => _escolherCor(
                    inicial: e.value,
                    aplicar: (cor) => c.setPaletteColor(e.key, cor),
                  ),
                  // O antigo prometia "segure para tirar" e nao tirava.
                  aoSegurar: () => _passo(() => c.removePaletteColor(e.key)),
                ),
              LinhaDeAjuste(
                key: const ValueKey('projeto-paleta-adicionar'),
                icone: CupertinoIcons.add_circled,
                titulo: 'Adicionar cor à paleta',
                aoTocar: () =>
                    _adicionarCorNaPaleta(p.palette.entries.length),
              ),
            ],
          ),
          AureaSection(
            titulo: 'Propriedades expostas (template)',
            chave: 'projeto-expostas',
            recolhivel: false,
            filhos: [
              if (p.exposed.isEmpty)
                const LinhaDeAjuste(
                  icone: CupertinoIcons.slider_horizontal_3,
                  titulo: 'Nenhuma propriedade exposta',
                  subtitulo:
                      'Exponha um parâmetro para quem usar este projeto '
                      'como template.',
                ),
              for (final ex in p.exposed)
                LinhaDeAjuste(
                  key: ValueKey('projeto-exposta-${ex.id}'),
                  icone: CupertinoIcons.slider_horizontal_3,
                  // Nome e grupo sao do autor do template: conteudo.
                  titulo: ex.label,
                  tituloDoUsuario: true,
                  subtitulo: '${ex.group} · ${ex.property}',
                  subtituloPronto: true,
                  fim: Icon(
                    CupertinoIcons.minus_circle,
                    size: AureaDims.iconeSm + 2,
                    color: AureaCores.perigo,
                  ),
                  aoTocar: () => _passo(() => c.unexposeProperty(ex.id)),
                ),
            ],
          ),
          AureaSection(
            titulo: 'Dados (CSV)',
            chave: 'projeto-dados',
            recolhivel: false,
            filhos: [
              LinhaDeAjuste(
                key: const ValueKey('projeto-dados'),
                icone: CupertinoIcons.table,
                titulo: p.data?.name ?? 'Carregar CSV',
                tituloDoUsuario: p.data != null,
                subtitulo: p.data == null
                    ? translate(
                        context,
                        'Colunas viram fontes para textos (vincular no '
                        'painel do texto)',
                      )
                    : moldar(
                        context,
                        '{0} colunas · {1} linhas · toque para trocar',
                        [p.data!.columns.length, p.data!.rows.length],
                      ),
                subtituloPronto: true,
                seta: true,
                aoTocar: _carregarCsv,
              ),
              if (p.data != null)
                LinhaDeAjuste(
                  key: const ValueKey('projeto-dados-remover'),
                  icone: CupertinoIcons.clear,
                  titulo: 'Remover dados',
                  perigo: true,
                  aoTocar: () => _passo(() => c.setDataSource(null)),
                ),
              if (_avisoDosDados case final aviso?)
                Padding(
                  key: const ValueKey('projeto-dados-aviso'),
                  padding: const EdgeInsets.only(bottom: AureaDims.e6),
                  child: Text(
                    aviso,
                    style: AureaEstilos.propriedade.copyWith(
                      color: AureaCores.perigo,
                    ),
                  ),
                ),
            ],
          ),
        ],
        AureaSection(
          titulo: 'Ajuda',
          chave: 'projeto-ajuda',
          recolhivel: false,
          filhos: [
            LinhaDeAjuste(
              key: const ValueKey('projeto-ajuda'),
              icone: CupertinoIcons.question_circle,
              titulo: 'Como usar o editor',
              subtitulo: 'Guia rápido, com busca',
              seta: true,
              aoTocar: () => Navigator.of(context).push(
                MaterialPageRoute<void>(
                  builder: (_) => const QuickGuideScreen(initialQuery: ''),
                ),
              ),
            ),
            if (pro)
              LinhaDeAjuste(
                key: const ValueKey('projeto-diagnostico'),
                icone: CupertinoIcons.waveform_path_ecg,
                titulo: 'Diagnóstico na tela',
                subtitulo:
                    'Marcha, composições por segundo, memória e o motor 3D.',
                ligado: diagnostico,
                aoTocar: () => ref.read(debugOverlayProvider.notifier).state =
                    !diagnostico,
              ),
          ],
        ),
      ],
    );
  }
}

/// UMA LINHA DE AJUSTE: icone, titulo, subtitulo opcional e, no fim, um
/// interruptor ([ligado]), uma seta ([seta]) ou um enfeite ([fim]).
///
/// A LINHA INTEIRA e o alvo do toque, inclusive quando tem interruptor: o
/// interruptor ocupa 51 px no canto e o dedo acerta o texto. (Por isso nao
/// e a `linhaDeLigar` dos paineis, em que so o interruptor responde.)
///
/// Texto de UI passa pelo catalogo; [tituloDoUsuario] e [subtituloPronto]
/// marcam o que e conteudo (nome do projeto, cor da paleta) ou ja veio
/// montado e traduzido por `moldar`.
class LinhaDeAjuste extends StatelessWidget {
  const LinhaDeAjuste({
    super.key,
    required this.titulo,
    this.icone,
    this.subtitulo,
    this.ligado,
    this.perigo = false,
    this.seta = false,
    this.fim,
    this.aoTocar,
    this.aoSegurar,
    this.tituloDoUsuario = false,
    this.subtituloPronto = false,
  });

  final String titulo;
  final IconData? icone;
  final String? subtitulo;

  /// Com valor, a linha vira um interruptor e o toque o alterna.
  final bool? ligado;
  final bool perigo;
  final bool seta;
  final Widget? fim;
  final VoidCallback? aoTocar;
  final VoidCallback? aoSegurar;
  final bool tituloDoUsuario;
  final bool subtituloPronto;

  @override
  Widget build(BuildContext context) {
    final inerte = aoTocar == null && aoSegurar == null;
    final corDoTitulo = perigo
        ? AureaCores.perigo
        : inerte
        ? AureaCores.textoSecundario
        : AureaCores.texto;
    final corDoIcone = perigo
        ? AureaCores.perigo
        : inerte
        ? AureaCores.textoSecundario
        : AureaCores.destaque;
    final estiloDoTitulo = AureaEstilos.corpo.copyWith(color: corDoTitulo);
    final estiloDoSubtitulo = AureaEstilos.propriedade.copyWith(
      fontSize: 11,
      height: 1.3,
    );
    final sub = subtitulo;
    return Tocavel(
      onTap: aoTocar,
      onLongPress: aoSegurar,
      encolhe: 1,
      child: ConstrainedBox(
        constraints: const BoxConstraints(
          minHeight: AureaDims.itemDeLista + AureaDims.e6,
        ),
        child: Padding(
          padding: const EdgeInsets.symmetric(vertical: AureaDims.e6),
          child: Row(
            children: [
              if (icone != null) ...[
                Icon(icone, size: AureaDims.iconeSm + 2, color: corDoIcone),
                const SizedBox(width: AureaDims.e10),
              ],
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    tituloDoUsuario
                        ? Text(
                            titulo,
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                            style: estiloDoTitulo,
                          )
                        : AppText(
                            titulo,
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                            style: estiloDoTitulo,
                          ),
                    if (sub != null)
                      subtituloPronto
                          ? Text(
                              sub,
                              maxLines: 2,
                              overflow: TextOverflow.ellipsis,
                              style: estiloDoSubtitulo,
                            )
                          : AppText(
                              sub,
                              maxLines: 2,
                              overflow: TextOverflow.ellipsis,
                              style: estiloDoSubtitulo,
                            ),
                  ],
                ),
              ),
              if (fim != null) ...[
                const SizedBox(width: AureaDims.e8),
                fim!,
              ],
              if (ligado != null) ...[
                const SizedBox(width: AureaDims.e8),
                AureaToggle(
                  valor: ligado!,
                  habilitado: aoTocar != null,
                  aoMudar: (_) => aoTocar?.call(),
                ),
              ] else if (seta) ...[
                const SizedBox(width: AureaDims.e8),
                Icon(
                  CupertinoIcons.chevron_right,
                  size: 13,
                  color: AureaCores.textoSecundario,
                ),
              ],
            ],
          ),
        ),
      ),
    );
  }
}

/// UMA ESCOLHA CURTA: rotulo na coluna de 75 da linha de propriedade e as
/// pilulas numa fileira que QUEBRA linha.
///
/// Quebra, e nao rola de lado como a `FileiraDePilulas`: as resolucoes
/// ("Full HD 1080p") nao cabem numa linha de 375, e uma pilula escondida
/// fora da vista e uma opcao que a pessoa nao sabe que existe.
///
/// Chaves: `<chave>-<chaveDe(opcao)>` em cada pilula (padrao: o rotulo).
class _LinhaDeEscolha<T> extends StatelessWidget {
  const _LinhaDeEscolha({
    super.key,
    required this.chave,
    required this.rotulo,
    required this.opcoes,
    required this.rotuloDe,
    required this.atual,
    required this.aoEscolher,
    this.chaveDe,
  });

  final String chave;
  final String rotulo;
  final List<T> opcoes;
  final String Function(T) rotuloDe;
  final String Function(T)? chaveDe;
  final T? atual;
  final ValueChanged<T> aoEscolher;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.symmetric(
      vertical: (AureaDims.linhaDePropriedade - _alturaDaPilula) / 2,
    ),
    child: Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        SizedBox(
          width: AureaDims.rotuloDaPropriedade,
          height: _alturaDaPilula,
          child: Align(
            alignment: Alignment.centerLeft,
            child: AppText(
              rotulo,
              maxLines: 2,
              overflow: TextOverflow.ellipsis,
              style: AureaEstilos.propriedade,
            ),
          ),
        ),
        Expanded(
          child: Wrap(
            spacing: AureaDims.e6,
            runSpacing: AureaDims.e6,
            children: [
              for (final o in opcoes)
                AureaChip(
                  key: ValueKey('$chave-${(chaveDe ?? rotuloDe)(o)}'),
                  rotulo: rotuloDe(o),
                  ativo: o == atual,
                  // Tocar no que ja esta escolhido nao pode gerar um passo
                  // de desfazer vazio.
                  aoTocar: o == atual ? null : () => aoEscolher(o),
                ),
            ],
          ),
        ),
      ],
    ),
  );
}

/// A amostra de uma cor de CONTEUDO (fundo, paleta): a propria cor, no
/// raio e no tamanho da amostra da linha de cor do DS.
class _Amostra extends StatelessWidget {
  const _Amostra({required this.cor});

  final Color cor;

  @override
  Widget build(BuildContext context) => Container(
    width: 34,
    height: 22,
    decoration: BoxDecoration(
      color: cor,
      borderRadius: BorderRadius.circular(AureaDims.raioMd),
    ),
  );
}
