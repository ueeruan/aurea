import 'package:flutter/cupertino.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../application/editor_controller.dart';
import '../../../application/keyframe_clipboard.dart';
import '../../../application/playback_controller.dart';
import '../../../domain/keyframe.dart';
import '../../../domain/layer.dart';
import '../../am/curve_panel.dart' show EasingClipboard;
import '../shell/contrato.dart' show EscopoDoEditor;
import 'grafico_da_curva.dart';
import 'navegacao_de_keyframes.dart';
import 'trilha_da_curva.dart';

// ===========================================================================
// O EDITOR DE CURVA (nivel 4 da referencia)
// ===========================================================================
//
// A curva ENTRE DOIS KEYFRAMES da propriedade, a do trecho em que o cabecote
// esta. Abre por cima da timeline (a altura do painel grande = timeline +
// transporte), com o palco a vista:
//
//   [Curva · Posicao ........ Valor Velocidade  ⋯  ✓]   38
//   [Linear  Ease  Ease in  Ease out  Ease in-out  Bezier  Hold]
//   [                grafico (pinca, dois dedos, toque duplo)          ]
//   [‹   Ease in-out · Trecho 1 de 2                     ›  Selecionados]
//   [    Alca 1 (0,42; 0) · Alca 2 (0,58; 1)                             ]
//
// O CABECOTE E DE QUEM O MOVE — do dedo, do relogio e das setas ‹ ›. O
// editor segue o cabecote para o trecho em que ele entra, e quando ele sai
// de todos os trechos (tocando, por exemplo) segura o ultimo, esmaecido, em
// vez de puxar o cabecote de volta (a briga com a reproducao que o editor
// antigo teve).

/// OS PRESETS DA FAIXA, na ordem da referencia: Linear, Ease, Ease In, Ease
/// Out, Ease In-Out, Bezier (personalizada) e Hold.
///
/// Da FONTE UNICA (`CatalogoDeCurvas.basicos`) — o que ela ja tem vem de la,
/// com o mesmo nome e a mesma curva. So o "Ease" (a curva `ease` padrao,
/// 0,25 0,1 0,25 1) nao esta na primeira linha do catalogo: ele mora na
/// familia Bezier como `Easing.appleStandard`, e e essa constante que entra
/// aqui — nenhum numero de curva novo.
List<PresetDeCurva> get presetsDoEditorDeCurva {
  PresetDeCurva doCatalogo(bool Function(PresetDeCurva) teste) =>
      CatalogoDeCurvas.basicos.firstWhere(teste);
  PresetDeCurva bezier(Easing e) =>
      doCatalogo((p) => !p.personalizada && p.ease.mesmoPresetQue(e));
  return [
    bezier(Easing.linear),
    const PresetDeCurva('Ease', Easing.appleStandard),
    bezier(Easing.easeIn),
    bezier(Easing.easeOut),
    bezier(Easing.easeInOut),
    doCatalogo((p) => p.personalizada),
    doCatalogo((p) => p.ease.type == EasingType.hold),
  ];
}

/// A chave de teste do chip de [p]: `curva-preset-<slug>`.
String chaveDoPreset(PresetDeCurva p) =>
    'curva-preset-${AureaPropertyRow.slugDoRotulo(p.nome)}';

/// ABRE O EDITOR DE CURVA da trilha [trilha] da camada [layerId], no trecho
/// do instante GLOBAL [tempo] (o losango tocado, ou o cabecote).
///
/// E a porta publica: a timeline (toque longo no losango) e o
/// `AureaKeyframeButton` (via `KeyframeState.onCurve`) chamam isto. O
/// relogio vem de [playback] ou, sem ele, da casca do editor
/// ([EscopoDoEditor]).
///
/// Folha NAO modal (sem veu) da altura do painel grande: cobre a timeline e
/// o transporte e deixa o palco inteiro a vista.
Future<void> abrirEditorDeCurva(
  BuildContext context,
  WidgetRef ref, {
  required String layerId,
  required TrilhaDaCurva trilha,
  required Duration tempo,
  PlaybackController? playback,
}) async {
  final relogio =
      playback ??
      context.getInheritedWidgetOfExactType<EscopoDoEditor>()?.playback;
  assert(relogio != null, 'abrirEditorDeCurva fora da casca e sem playback.');
  if (relogio == null) return;
  if (ref.read(editorControllerProvider).layerById(layerId) == null) return;
  await mostrarAureaFolha<void>(
    context,
    modal: false,
    // A folha poe 10 de respiro em cima: o total fica nos 326 do painel
    // grande.
    altura: AureaDims.painelGrande - AureaDims.e10,
    construtor: (_) => EditorDeCurva(
      layerId: layerId,
      trilha: trilha,
      tempoInicial: tempo,
      playback: relogio,
    ),
  );
}

/// O EDITOR em si — publico para quem quiser embuti-lo num painel.
class EditorDeCurva extends ConsumerStatefulWidget {
  const EditorDeCurva({
    super.key,
    required this.layerId,
    required this.trilha,
    required this.tempoInicial,
    required this.playback,
    this.aoFechar,
  });

  final String layerId;
  final TrilhaDaCurva trilha;

  /// O instante GLOBAL que escolhe o trecho ao abrir.
  final Duration tempoInicial;
  final PlaybackController playback;

  /// O ✓. Nulo: fecha a rota (a folha).
  final VoidCallback? aoFechar;

  @override
  ConsumerState<EditorDeCurva> createState() => EditorDeCurvaState();
}

class EditorDeCurvaState extends ConsumerState<EditorDeCurva> {
  /// O comeco (tempo local) do trecho mostrado. Nulo = nenhum.
  Duration? _inicio;

  /// O cabecote esta fora do trecho mostrado (o grafico esmaece).
  bool _fora = false;

  final ValueNotifier<double?> _percorrido = ValueNotifier(null);

  bool _velocidade = false;
  bool _overshoot = false;

  /// Aplicar tambem nas marcas SELECIONADAS da timeline.
  bool _nosSelecionados = false;

  /// Uma alca esta sendo arrastada (entre `aoComecar` e `aoTerminar`).
  bool _arrastando = false;

  /// O controlador, guardado ao nascer: o grafico fecha o gesto de desfazer
  /// no proprio `dispose`, e ai este `ref` ja nao pode ser lido.
  late final EditorController _c = ref.read(editorControllerProvider.notifier);
  Layer? _camada() =>
      ref.read(editorControllerProvider).layerById(widget.layerId);

  /// Qual grafico esta a vista (para teste).
  ModoDoGrafico get modo =>
      _velocidade ? ModoDoGrafico.velocidade : ModoDoGrafico.valor;

  @override
  void initState() {
    super.initState();
    final camada = _camada();
    if (camada != null) {
      final marcas = widget.trilha.marcasDe(_c, camada);
      _inicio =
          trechoEm(marcas, widget.trilha.localEm(camada, widget.tempoInicial))
              ?.inicio ??
          trechoEm(
            marcas,
            widget.trilha.localEm(camada, widget.playback.time.value),
          )?.inicio;
      final c = _inicio;
      if (c != null) {
        _overshoot = _foraDeZeroUm(widget.trilha.curvaDe(_c, camada, c));
      }
    }
    // Ao abrir, quem escolhe o trecho e [tempoInicial] (o losango tocado):
    // o cabecote so passa a mandar quando ANDA.
    _sincronizar(reconstruir: false, seguirCabecote: false);
    widget.playback.time.addListener(_aoAndarOCabecote);
  }

  @override
  void dispose() {
    widget.playback.time.removeListener(_aoAndarOCabecote);
    _percorrido.dispose();
    super.dispose();
  }

  static bool _foraDeZeroUm(Easing e) =>
      e.type == EasingType.cubicBezier &&
      (e.y1 < 0 || e.y1 > 1 || e.y2 < 0 || e.y2 > 1);

  void _aoAndarOCabecote() {
    if (!mounted) return;
    // O relogio pode bater DENTRO do quadro (o ticker roda antes do
    // build): o setState vai para depois dele.
    if (SchedulerBinding.instance.schedulerPhase ==
        SchedulerPhase.persistentCallbacks) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) _sincronizar();
      });
    } else {
      _sincronizar();
    }
  }

  /// SEGUE O CABECOTE: troca de trecho quando ele entra noutro, e mede
  /// quanto do trecho mostrado ele ja andou. Reconstroi so quando o trecho
  /// ou o "fora" mudam — o ponto que corre e do pintor.
  void _sincronizar({bool reconstruir = true, bool seguirCabecote = true}) {
    final camada = _camada();
    if (camada == null) return;
    final marcas = widget.trilha.marcasDe(_c, camada);
    final agora = widget.trilha.localEm(camada, widget.playback.time.value);
    final sob = trechoEm(marcas, agora);
    var inicio = _inicio;
    // Com a alca na mao o trecho NAO troca: o arrasto gravaria a curva no
    // trecho vizinho no meio do gesto (o relogio pode estar tocando).
    if (sob != null && seguirCabecote && !_arrastando) inicio = sob.inicio;
    final mostrado = inicio == null ? null : _trechoQueComeca(marcas, inicio);
    if (mostrado == null) inicio = null;
    final fora = mostrado == null || sob?.inicio != mostrado.inicio;
    double? percorrido;
    if (!fora) {
      final vao = (mostrado.fim - mostrado.inicio).inMicroseconds;
      if (vao > 0) {
        percorrido = ((agora - mostrado.inicio).inMicroseconds / vao).clamp(
          0.0,
          1.0,
        );
      }
    }
    _percorrido.value = percorrido;
    if (inicio != _inicio || fora != _fora) {
      if (reconstruir) {
        setState(() {
          _inicio = inicio;
          _fora = fora;
        });
      } else {
        _inicio = inicio;
        _fora = fora;
      }
    }
  }

  /// O trecho que comeca na marca [inicio] (nulo se ela nao existe mais ou
  /// e a ultima).
  static ({int indice, Duration inicio, Duration fim})? _trechoQueComeca(
    List<Duration> marcas,
    Duration inicio,
  ) {
    for (var i = 0; i < marcas.length - 1; i++) {
      if ((marcas[i] - inicio).abs() < kToleranciaDoKeyframe) {
        return (indice: i, inicio: marcas[i], fim: marcas[i + 1]);
      }
    }
    return null;
  }

  // ---------------------------------------------------------- gravar

  /// A selecao que conta para esta trilha: so as de transformacao.
  Set<MarcaSelecionada> _selecaoUtil(Set<MarcaSelecionada> selecao) =>
      widget.trilha.prop == null ? const {} : selecao;

  /// GRAVA [e] no trecho mostrado e, com "Selecionados" ligado, no trecho
  /// que sai de cada marca selecionada. Quem chama decide o passo de
  /// desfazer (um gesto ou [EditorController.runAsOneUndo]).
  void _gravar(Easing e) {
    final inicio = _inicio;
    final camada = _camada();
    if (inicio == null || camada == null) return;
    final finita =
        e.x1.isFinite && e.y1.isFinite && e.x2.isFinite && e.y2.isFinite;
    if (!finita) return;
    widget.trilha.gravar(_c, camada, inicio, e);
    if (_nosSelecionados) {
      for (final m in _selecaoUtil(ref.read(keyframesSelecionadosProvider))) {
        _c.setSegmentEase(m.layerId, m.prop, m.tempo, e);
      }
    }
  }

  /// UM ARRASTO, UM PASSO DE DESFAZER: o gesto abre no primeiro movimento
  /// da alca e fecha ao soltar, por mais passos que haja no meio.
  void _comecarArrasto() {
    _arrastando = true;
    _c.beginGesture();
  }

  void _terminarArrasto() {
    _arrastando = false;
    _c.endGesture();
  }

  /// Um toque (preset, colar, inverter): UM passo de desfazer, sempre —
  /// nunca engolido pela janela de 450 ms do controlador.
  void _aplicar(Easing e) => _c.runAsOneUndo(() => _gravar(e));

  void _aplicarEmTodos(Easing e) {
    final camada = _camada();
    if (camada == null) return;
    final emTodos = widget.trilha.gravarEmTodos;
    _c.runAsOneUndo(() {
      if (emTodos != null) {
        emTodos(_c, camada, e);
        return;
      }
      // Trecho a trecho, relendo a camada a cada um: quem grava pode
      // montar a camada nova a partir da que recebe.
      for (final m in widget.trilha.marcasDe(_c, camada)) {
        final atual = _camada();
        if (atual == null) return;
        widget.trilha.gravar(_c, atual, m, e);
      }
    });
  }

  void _ir(int direcao) {
    final camada = _camada();
    if (camada == null) return;
    irParaMarcaVizinha(
      playback: widget.playback,
      camada: camada,
      marcasLocais: widget.trilha.marcasDe(_c, camada),
      direcao: direcao,
      relogio: widget.trilha.relogio,
    );
  }

  void _fechar() {
    final fechar = widget.aoFechar;
    if (fechar != null) {
      fechar();
    } else {
      Navigator.of(context).maybePop();
    }
  }

  // ---------------------------------------------------------- menu

  Future<void> _abrirMenu(
    BuildContext ancora,
    Easing curva,
    Layer camada,
  ) async {
    final prop = widget.trilha.prop;
    final loop = prop == null ? null : _loopDa(camada, prop);
    final escolha = await mostrarAureaMenu<String>(
      ancora,
      titulo: 'Curva',
      itens: [
        AureaMenuItem(
          valor: 'copiar',
          rotulo: 'Copiar curva',
          icone: CupertinoIcons.doc_on_doc,
          chave: 'curva-copiar',
        ),
        AureaMenuItem(
          valor: 'colar',
          rotulo: 'Colar curva',
          icone: CupertinoIcons.doc_on_clipboard,
          habilitado: EasingClipboard.valor != null,
          chave: 'curva-colar',
        ),
        AureaMenuItem(
          valor: 'todos',
          rotulo: 'Aplicar em todos os trechos',
          icone: CupertinoIcons.square_stack_3d_up,
          chave: 'curva-todos',
        ),
        AureaMenuItem(
          valor: 'inverter',
          rotulo: 'Inverter curva',
          icone: CupertinoIcons.arrow_right_arrow_left,
          habilitado: curva.invertida != null,
          chave: 'curva-inverter',
        ),
        AureaMenuItem(
          valor: 'overshoot',
          rotulo: 'Overshoot',
          marcado: _overshoot,
          chave: 'curva-overshoot',
        ),
        AureaMenuItem(
          valor: 'mais',
          rotulo: 'Mais curvas…',
          icone: CupertinoIcons.waveform_path,
          chave: 'curva-mais',
        ),
        if (loop != null) ...[
          AureaMenuItem(
            valor: 'loop-none',
            rotulo: 'Loop: nenhum',
            marcado: loop == LoopMode.none,
            chave: 'curva-loop-nenhum',
          ),
          AureaMenuItem(
            valor: 'loop-cycle',
            rotulo: 'Loop: repetir',
            marcado: loop == LoopMode.cycle,
            chave: 'curva-loop-repetir',
          ),
          AureaMenuItem(
            valor: 'loop-pingPong',
            rotulo: 'Loop: vai e volta',
            marcado: loop == LoopMode.pingPong,
            chave: 'curva-loop-vai-e-volta',
          ),
        ],
      ],
    );
    if (!mounted || escolha == null) return;
    switch (escolha) {
      case 'copiar':
        EasingClipboard.valor = curva;
      case 'colar':
        final colada = EasingClipboard.valor;
        if (colada != null) _aplicar(colada);
      case 'todos':
        _aplicarEmTodos(curva);
      case 'inverter':
        final invertida = curva.invertida;
        if (invertida != null) _aplicar(invertida);
      case 'overshoot':
        setState(() => _overshoot = !_overshoot);
      case 'mais':
        if (!ancora.mounted) return;
        await _abrirMaisCurvas(ancora, curva);
      case 'loop-none':
        if (prop != null) _c.setPropertyLoop(widget.layerId, prop, LoopSpec.none);
      case 'loop-cycle':
        if (prop != null) {
          _c.setPropertyLoop(
            widget.layerId,
            prop,
            const LoopSpec(mode: LoopMode.cycle),
          );
        }
      case 'loop-pingPong':
        if (prop != null) {
          _c.setPropertyLoop(
            widget.layerId,
            prop,
            const LoopSpec(mode: LoopMode.pingPong),
          );
        }
    }
  }

  /// As FAMILIAS do catalogo (quique, elastico, degraus, mola...) — tudo
  /// o que nao esta na faixa, sem repetir nada dela.
  Future<void> _abrirMaisCurvas(BuildContext ancora, Easing curva) async {
    final presets = CatalogoDeCurvas.alemDosBasicos;
    final i = await mostrarAureaMenu<int>(
      ancora,
      titulo: 'Mais curvas',
      itens: [
        for (var k = 0; k < presets.length; k++)
          AureaMenuItem(
            valor: k,
            rotulo: presets[k].nome,
            marcado: curva.mesmoPresetQue(presets[k].ease),
            chave: 'curva-mais-${AureaPropertyRow.slugDoRotulo(presets[k].nome)}',
          ),
      ],
    );
    if (!mounted || i == null) return;
    _aplicar(presets[i].ease);
  }

  static LoopMode _loopDa(Layer camada, LayerProp prop) {
    final t = trilhasDaProp(camada, prop);
    for (final n in t.numeros.values) {
      if (n.loop.active) return n.loop.mode;
    }
    for (final p in t.pontos.values) {
      if (p.loop.active) return p.loop.mode;
    }
    return LoopMode.none;
  }

  // ---------------------------------------------------------- tela

  @override
  Widget build(BuildContext context) {
    final camada = ref.watch(
      editorControllerProvider.select((p) => p.layerById(widget.layerId)),
    );
    final selecao = _selecaoUtil(ref.watch(keyframesSelecionadosProvider));
    if (camada == null) {
      return AureaPanel(
        titulo: 'Curva',
        chave: 'curva',
        aoFechar: _fechar,
        filhos: const [
          AureaAvisoDoPainel(texto: 'Esta camada não existe mais.'),
        ],
      );
    }
    final trilha = widget.trilha;
    final marcas = trilha.marcasDe(_c, camada);
    // O trecho pode ter sumido (marca apagada, desfeita): cai no do
    // cabecote, ou em nenhum.
    var trecho = _inicio == null ? null : _trechoQueComeca(marcas, _inicio!);
    if (trecho == null) {
      trecho = trechoEm(
        marcas,
        trilha.localEm(camada, widget.playback.time.value),
      );
      _inicio = trecho?.inicio;
    }
    final curva = trecho == null
        ? null
        : trilha.curvaDe(_c, camada, trecho.inicio);
    final titulo = moldar(context, 'Curva · {0}', [
      translate(context, trilha.rotuloDe(camada)),
    ]);

    return AureaPanel(
      titulo: titulo,
      chave: 'curva',
      aoFechar: _fechar,
      acoes: [
        AureaChip(
          key: const ValueKey('curva-modo-valor'),
          rotulo: 'Valor',
          ativo: !_velocidade,
          aoTocar: () => setState(() => _velocidade = false),
        ),
        const SizedBox(width: AureaDims.e4),
        AureaChip(
          key: const ValueKey('curva-modo-velocidade'),
          rotulo: 'Velocidade',
          ativo: _velocidade,
          aoTocar: () => setState(() => _velocidade = true),
        ),
        Builder(
          builder: (ancora) => Tocavel(
            key: const ValueKey('curva-menu'),
            onTap: curva == null
                ? null
                : () => _abrirMenu(ancora, curva, camada),
            child: SizedBox(
              width: AureaDims.toqueConfortavel,
              height: AureaDims.cabecalhoDoPainel,
              child: Icon(
                CupertinoIcons.ellipsis,
                size: AureaDims.iconeMd,
                color: curva == null
                    ? AureaCores.textoSecundario.withValues(alpha: .4)
                    : AureaCores.texto,
              ),
            ),
          ),
        ),
      ],
      corpo: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _faixaDePresets(curva),
          Expanded(
            child: Padding(
              padding: const EdgeInsets.symmetric(
                horizontal: AureaDims.e6,
              ),
              child: curva == null || trecho == null
                  ? _aviso(marcas.length)
                  : ClipRRect(
                      borderRadius: BorderRadius.circular(AureaDims.raioMd),
                      child: ColoredBox(
                        color: AureaCores.cromo,
                        child: AnimatedOpacity(
                          opacity: _fora ? .45 : 1,
                          duration: AureaMotion.rapido,
                          child: GraficoDaCurva(
                            key: const ValueKey('curva-grafico'),
                            curva: curva,
                            modo: modo,
                            overshoot: _overshoot,
                            percorrido: _percorrido,
                            aoComecar: _comecarArrasto,
                            aoTerminar: _terminarArrasto,
                            aoMudar: _gravar,
                            valorDoInicio: trilha.valorDe?.call(
                              _c,
                              camada,
                              trecho.inicio,
                            ),
                            valorDoFim: trilha.valorDe?.call(
                              _c,
                              camada,
                              trecho.fim,
                            ),
                            tempoDoInicio: _segundos(trecho.inicio),
                            tempoDoFim: _segundos(trecho.fim),
                          ),
                        ),
                      ),
                    ),
            ),
          ),
          _rodape(context, curva, trecho, marcas.length, selecao.length),
        ],
      ),
    );
  }

  static String _segundos(Duration d) =>
      '${numeroDaCurva(d.inMicroseconds / 1e6)} s';

  Widget _faixaDePresets(Easing? curva) {
    final presets = presetsDoEditorDeCurva;
    // Sete fichas: uma fila montada inteira (e nao uma lista preguicosa),
    // que rola na horizontal quando a tela e estreita — nenhuma ficha deixa
    // de existir so por estar fora da vista.
    return SizedBox(
      height: 28 + AureaDims.e8,
      child: SingleChildScrollView(
        scrollDirection: Axis.horizontal,
        padding: const EdgeInsets.symmetric(
          horizontal: AureaDims.e10,
          vertical: AureaDims.e4,
        ),
        child: Row(
          children: [
            for (var i = 0; i < presets.length; i++) ...[
              if (i > 0) const SizedBox(width: AureaDims.e6),
              AureaChip(
                key: ValueKey(chaveDoPreset(presets[i])),
                rotulo: presets[i].nome,
                ativo:
                    curva != null && CatalogoDeCurvas.aceso(presets[i], curva),
                aoTocar: curva == null
                    ? null
                    : () => _aplicar(
                        CatalogoDeCurvas.aoEscolher(presets[i], curva),
                      ),
              ),
            ],
          ],
        ),
      ),
    );
  }

  Widget _aviso(int quantasMarcas) => Center(
    key: const ValueKey('curva-aviso'),
    child: Padding(
      padding: const EdgeInsets.symmetric(horizontal: AureaDims.margemDoPainel),
      child: AppText(
        quantasMarcas < 2
            ? 'Crie dois keyframes nesta propriedade para editar a curva.'
            : 'Leve o cabeçote para entre dois keyframes para ver a curva.',
        textAlign: TextAlign.center,
        style: AureaEstilos.propriedade,
      ),
    ),
  );

  Widget _seta(String chave, IconData icone, VoidCallback? acao) => Tocavel(
    key: ValueKey(chave),
    onTap: acao,
    child: SizedBox(
      width: AureaDims.toqueConfortavel,
      height: AureaDims.toqueConfortavel,
      child: Icon(
        icone,
        size: AureaDims.iconeSm,
        color: acao == null
            ? AureaCores.textoSecundario.withValues(alpha: .3)
            : AureaCores.keyframe,
      ),
    ),
  );

  Widget _rodape(
    BuildContext context,
    Easing? curva,
    ({int indice, Duration inicio, Duration fim})? trecho,
    int quantasMarcas,
    int selecionadas,
  ) {
    final camada = _camada();
    final marcas = camada == null
        ? const <Duration>[]
        : widget.trilha.marcasDe(_c, camada);
    final agora = camada == null
        ? Duration.zero
        : widget.trilha.localEm(camada, widget.playback.time.value);
    bool temVizinha(int d) =>
        marcaVizinha(marcasLocais: marcas, agoraLocal: agora, direcao: d) !=
        null;
    final linha1 = curva == null || trecho == null
        ? translate(context, 'Sem trecho')
        : moldar(context, '{0} · Trecho {1} de {2}', [
            translate(context, _nomeDaCurva(curva)),
            trecho.indice + 1,
            quantasMarcas - 1,
          ]);
    final linha2 = curva == null ? '' : _leitura(context, curva);
    return SizedBox(
      height: AureaDims.toqueConfortavel + AureaDims.e6,
      child: Row(
        children: [
          const SizedBox(width: AureaDims.e6),
          _seta(
            'curva-anterior',
            CupertinoIcons.chevron_left,
            temVizinha(-1) ? () => _ir(-1) : null,
          ),
          Expanded(
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Text(
                  linha1,
                  key: const ValueKey('curva-trecho'),
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: AureaEstilos.corpo.copyWith(
                    fontSize: AureaDims.textoDePropriedade,
                    fontWeight: FontWeight.w600,
                  ),
                ),
                if (linha2.isNotEmpty)
                  Text(
                    linha2,
                    key: const ValueKey('curva-leitura'),
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: AureaEstilos.rotulo.copyWith(
                      fontFeatures: const [FontFeature.tabularFigures()],
                    ),
                  ),
              ],
            ),
          ),
          _seta(
            'curva-proximo',
            CupertinoIcons.chevron_right,
            temVizinha(1) ? () => _ir(1) : null,
          ),
          if (selecionadas > 0) ...[
            AureaChip(
              key: const ValueKey('curva-selecionados'),
              rotulo: moldar(context, 'Selecionados ({0})', [selecionadas]),
              traduzir: false,
              ativo: _nosSelecionados,
              aoTocar: () =>
                  setState(() => _nosSelecionados = !_nosSelecionados),
            ),
            const SizedBox(width: AureaDims.e6),
          ],
          const SizedBox(width: AureaDims.e6),
        ],
      ),
    );
  }

  /// O nome do trecho: o chip da faixa quando e um deles, o preset do
  /// catalogo quando e outro, "(personalizada)" quando as alcas ja sairam
  /// de todos.
  static String _nomeDaCurva(Easing e) {
    for (final p in presetsDoEditorDeCurva) {
      if (!p.personalizada && e.mesmoPresetQue(p.ease)) return p.nome;
    }
    final preset = CatalogoDeCurvas.presetDe(e);
    if (preset != null) return preset.nome;
    return e.type == EasingType.cubicBezier
        ? 'Bézier (personalizada)'
        : e.label;
  }

  /// A LEITURA NUMERICA das alcas (ou dos numeros da familia).
  String _leitura(BuildContext context, Easing e) {
    final n = numeroDaCurva;
    if (_velocidade) {
      if (e.type != EasingType.cubicBezier) {
        return translate(context, 'A velocidade se edita na curva Bézier.');
      }
      return moldar(context, 'Saída {0}× · {1}%   Chegada {2}× · {3}%', [
        n(velocidadeDeSaida(e)),
        (e.x1 * 100).round(),
        n(velocidadeDeChegada(e)),
        ((1 - e.x2) * 100).round(),
      ]);
    }
    return switch (e.type) {
      EasingType.cubicBezier => moldar(
        context,
        'Alça 1 ({0}; {1})   Alça 2 ({2}; {3})',
        [n(e.x1), n(e.y1), n(e.x2), n(e.y2)],
      ),
      EasingType.hold => translate(
        context,
        'Segura o valor até o próximo keyframe.',
      ),
      EasingType.bounce ||
      EasingType.bounceIn ||
      EasingType.elastic ||
      EasingType.elasticIn => moldar(context, 'Repetições {0} · Força {1}', [
        e.count,
        n(e.intensity),
      ]),
      EasingType.cyclic => moldar(context, 'Repetições {0} · Suavidade {1}', [
        e.count,
        n(e.smooth),
      ]),
      EasingType.steps ||
      EasingType.stepsRandom ||
      EasingType.elasticSteps => moldar(context, 'Degraus {0}', [e.count]),
      _ => translate(context, e.label),
    };
  }
}
