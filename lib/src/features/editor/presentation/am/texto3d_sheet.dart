import 'dart:async';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/l10n/app_language.dart';
import '../../../../core/ui/snack.dart';
import '../../../../core/ui/tocavel.dart';
import '../../application/editor_controller.dart';
import '../../application/font_service.dart';
import '../../application/interacao.dart';
import '../../application/playback_controller.dart';
import '../../domain/element3d.dart';
import '../../domain/keyframe.dart';
import '../../domain/layer.dart';
import '../../domain/modelo_do_texto3d.dart';
import '../../domain/texto3d.dart';
import '../context/parameter_row.dart';
import 'am_colors.dart';
import 'color_picker_sheet.dart';

/// A FOLHA DO TEXTO 3D — refeita com os componentes da casa (20/09).
///
/// ===================== POR QUE ELA FOI REFEITA DO ZERO ==================
///
/// A folha anterior era funcional e ERRADA: rotulo solto em caixa alta,
/// fileira de fichas propria, secoes que nao abriam nem fechavam. Parecia
/// um plugin de desktop enfiado no celular — que foi exatamente o que o
/// dono disse ao ver. Nada aqui e componente novo: o cartao que abre e
/// fecha, a cabeca de grupo, o chip do rotulo, a fileira de escolha e a
/// linha de cor sao os MOLDES do painel de Efeitos
/// (`am/effects_panel.dart`), e toda linha de numero e a `ParameterRow` da
/// casa — com o MESMO losango de keyframe de qualquer outra propriedade
/// do aplicativo.
///
/// ============================ UM PREVIEW SO ============================
///
/// A folha OCUPA A METADE DE BAIXO e o palco fica a vista por cima: cada
/// toque aqui aparece na cena de verdade, no unico preview do aplicativo.
/// Ela ja teve uma previa propria (a mesma cena, pelo mesmo motor, num
/// quadro de 320x320) e isso custava caro de um jeito que nao se via: o
/// motor tem UM alvo, e alternar o tamanho dele entre a folha e o palco
/// recriava cor, profundidade, MSAA e staging DUAS vezes por toque. O
/// dono exige um preview oficial, e ele e o palco.
///
/// [playback] e o cabecote vivo — e dele que sai o instante em que o
/// losango crava. [playhead] continua na assinatura para quem chama sem
/// ter o controlador de reproducao a mao.
Future<void> showTexto3DSheet(
  BuildContext context,
  WidgetRef ref, {
  required String sceneId,
  required String nodeId,
  Duration playhead = Duration.zero,
  PlaybackController? playback,
}) async {
  await showCupertinoModalPopup<void>(
    context: context,
    // O ESCURECIMENTO E LEVE de proposito: o palco atras e o preview.
    barrierColor: const Color(0x33000000),
    builder: (_) => _FolhaDoTexto3D(
      sceneId: sceneId,
      nodeId: nodeId,
      playhead: playhead,
      playback: playback,
    ),
  );
}

/// Como a faixa de caracteres e escolhida.
enum _Selecao { todas, uma, intervalo }

class _FolhaDoTexto3D extends ConsumerStatefulWidget {
  const _FolhaDoTexto3D({
    required this.sceneId,
    required this.nodeId,
    required this.playhead,
    required this.playback,
  });

  final String sceneId;
  final String nodeId;
  final Duration playhead;
  final PlaybackController? playback;

  @override
  ConsumerState<_FolhaDoTexto3D> createState() => _FolhaDoTexto3DState();
}

class _FolhaDoTexto3DState extends ConsumerState<_FolhaDoTexto3D> {
  late final TextEditingController _campo;
  Texto3D? _params;
  EstiloDoTexto3D _estilo = EstiloDoTexto3D.ouro;
  List<String> _familias = const [];
  bool _montando = false;
  String? _aviso;
  Timer? _esperaDoControle;

  /// Os cartoes ABERTOS. Extrusao e Material nascem abertos porque sao o
  /// que se mexe em toda letra; Caracteres, Cena e Fonte ficam a um toque.
  final _abertos = <String>{'Extrusão', 'Material'};

  _Selecao _selecao = _Selecao.todas;
  int _umCaractere = 0;
  int _de = 0;
  int _ate = 0;

  @override
  void initState() {
    super.initState();
    final projeto = ref.read(editorControllerProvider);
    final camada = projeto.layerById(widget.sceneId);
    final no = camada is Scene3DLayer
        ? camada.scene.nodeById(widget.nodeId)
        : null;
    // SEM OS PARAMETROS GRAVADOS NAO HA O QUE EDITAR, e um texto vazio
    // seria pior do que dizer: cai no nome do no, que e o que o dono ve.
    _params =
        no?.texto3d ??
        Texto3D(texto: no?.name ?? 'TEXTO 3D', separarLetras: false);
    _estilo = no?.estiloTexto3d ?? EstiloDoTexto3D.ouro;
    _campo = TextEditingController(text: _params!.texto);
    _ate = (_letras.length - 1).clamp(0, 1 << 20);
    _carregarFontes();
  }

  Future<void> _carregarFontes() async {
    await FontService.instance.loadAll();
    if (!mounted) return;
    setState(() => _familias = FontService.instance.families);
  }

  @override
  void dispose() {
    _esperaDoControle?.cancel();
    _campo.dispose();
    super.dispose();
  }

  EditorController get _controlador =>
      ref.read(editorControllerProvider.notifier);

  Texto3D get _atual => _params!;

  /// AS UNIDADES DE TEXTO (graphemes), na mesma contagem que o motor usa
  /// para saber a vez de cada letra — e a contagem em que a faixa e
  /// gravada. Espaco conta: mover "A B" pelo indice 2 tem de pegar o "B".
  List<String> get _letras =>
      _atual.texto.replaceAll('\r', '').characters.toList();

  Scene3DLayer? get _camada {
    final l = ref.read(editorControllerProvider).layerById(widget.sceneId);
    return l is Scene3DLayer ? l : null;
  }

  /// O INSTANTE LOCAL DA CAMADA: e nele que o losango crava. O keyframe de
  /// uma letra pertence a camada, nao a linha do tempo do projeto — mover a
  /// camada leva a animacao junto.
  Duration get _local {
    final global = widget.playback?.time.value ?? widget.playhead;
    return _camada?.localTime(global) ?? global;
  }

  /// APLICA E ESPERA. Cada mudanca destas refaz a geometria (extrusao,
  /// chanfro e material) e sobe para o motor; a folha fica travada enquanto
  /// isso para um segundo toque nao enfileirar outra construcao pela metade.
  Future<void> _aplicar(Texto3D novo) async {
    final anterior = _params;
    setState(() {
      _params = novo;
      _montando = true;
      _aviso = null;
    });
    final deuCerto = await _controlador.editarTexto3D(
      widget.sceneId,
      widget.nodeId,
      novo,
      _estilo,
    );
    if (!mounted) return;
    setState(() {
      _montando = false;
      if (!deuCerto) {
        // A EDICAO FALHOU: a folha VOLTA ao que estava e DIZ por que. Ficar
        // mostrando a fonte nova marcada com o texto na fonte velha e o
        // "troquei e nao mudou nada" do relato.
        _params = anterior;
        _aviso =
            _controlador.ultimoMotivoDoTexto3D ??
            'Nao foi possivel aplicar essa mudanca ao texto 3D.';
      }
    });
  }

  Future<void> _trocarEstilo(EstiloDoTexto3D estilo) async {
    setState(() => _estilo = estilo);
    // A PREDEFINICAO MANDA. Ouro que continuasse com a rugosidade e a cor
    // que o dono tinha ajustado a mao no cromo nao seria ouro — e o relato
    // seria "escolhi o metal e nao mudou".
    await _aplicar(_atual.copyWith(semAcabamentoProprio: true));
  }

  // ------------------------------------------------ valores do material

  /// O VALOR QUE O DONO VE quando ele ainda nao mexeu: o do metal
  /// escolhido. Sem isto, "Metal" e "Rugosidade" abririam em zero e a
  /// primeira coisa que a folha mostraria do ouro seria plastico.
  double get _metalBase =>
      _atual.metalico ??
      (materiaisDoTexto3D(_estilo).first['metallic'] as num).toDouble();

  double get _rugosidadeBase =>
      _atual.rugosidade ??
      (materiaisDoTexto3D(_estilo).first['roughness'] as num).toDouble();

  Color get _corBase {
    final propria = _atual.cor;
    if (propria != null) return Color(propria);
    final c = materiaisDoTexto3D(_estilo).first['color'] as List;
    int canal(int i) => ((c[i] as num).toDouble() * 255).round().clamp(0, 255);
    return Color.fromARGB(255, canal(0), canal(1), canal(2));
  }

  /// O REFLEXO, O AMBIENTE E A ILUMINACAO MORAM NA CENA (ela e quem tem o
  /// que refletir), entao sao lidos da camada a cada reconstrucao — nao ha
  /// copia local para desencontrar do palco.
  double get _reflexoAtual => _camada?.scene.envReflect ?? 0.9;
  double get _ambienteAtual => _camada?.scene.ambient ?? 0.28;
  EnvironmentKind get _iluminacaoAtual =>
      _camada?.scene.environment ?? EnvironmentKind.estudioMetal;

  /// REFLEXO E AMBIENTE vao direto para a cena, SEM espera.
  ///
  /// Os outros controles esperam 140 ms porque refazem a geometria; estes
  /// nao mexem em um triangulo, entao esperar so faria o controle parecer
  /// morto com o dedo na tela.
  void _mudarReflexo(double v) {
    Interacao.marcar();
    _controlador.ajustarReflexoDoTexto3D(widget.sceneId, v);
    if (mounted) setState(() {});
  }

  void _mudarAmbiente(double v) {
    Interacao.marcar();
    _controlador.ajustarAmbienteDoTexto3D(widget.sceneId, v);
    if (mounted) setState(() {});
  }

  // ---------------------------------------------- ajuste por caractere

  /// A FAIXA ESCOLHIDA agora, como ela e gravada: fim negativo quer dizer
  /// "ate a ultima letra", que e como "Todas as letras" se escreve.
  (int, int) get _faixa => switch (_selecao) {
    _Selecao.todas => (0, -1),
    _Selecao.uma => (_umCaractere, _umCaractere),
    _Selecao.intervalo => (
      _de <= _ate ? _de : _ate,
      _de <= _ate ? _ate : _de,
    ),
  };

  /// O ajuste GRAVADO para a faixa escolhida, ou um vazio para editar. Um
  /// ajuste so vira projeto quando algum numero sai do repouso.
  AjusteDeCaracteres get _ajuste {
    final (i, f) = _faixa;
    return _atual.ajusteDaFaixa(i, f) ??
        AjusteDeCaracteres(inicio: i, fim: f);
  }

  void _gravarAjuste(AjusteDeCaracteres novo) {
    final (i, f) = _faixa;
    final resto = [
      for (final a in _atual.ajustes)
        if (a.inicio != i || a.fim != f) a,
    ];
    final lista = novo.inerte ? resto : [...resto, novo];
    setState(() => _params = _atual.copyWith(ajustes: lista));
    // NAO PASSA PELO EXTRUSOR: mover uma letra e matriz, nao malha.
    _controlador.ajustarCaracteresDoTexto3D(
      widget.sceneId,
      widget.nodeId,
      lista,
    );
  }

  void _mudarMedida(MedidaDoCaractere m, double v) {
    Interacao.marcar();
    final aj = _ajuste;
    final trilha = aj.trilha(m);
    final agora = _local;
    if (!trilha.aceitaEdicaoEm(agora)) {
      // A REGRA DA CASA (docs/keyframe-explicito.md): editar um valor
      // nunca cria keyframe sozinho. Quem crava e o losango.
      AureaSnack.show(
        context,
        'Esta opção tem keyframes: toque no losango para marcar este instante',
      );
      return;
    }
    _gravarAjuste(aj.com(m, trilha.edited(agora, v)));
  }

  void _alternarKeyframe(MedidaDoCaractere m) {
    final aj = _ajuste;
    final trilha = aj.trilha(m);
    final agora = _local;
    _gravarAjuste(
      aj.com(
        m,
        trilha.hasKeyframeAt(agora)
            ? trilha.withoutKeyframe(agora)
            : trilha.comMarcaInserida(agora),
      ),
    );
  }

  // --------------------------------------------------------- a fonte

  bool _importandoFonte = false;

  /// TRAZ UMA FONTE .ttf/.otf E JA A APLICA AO TEXTO.
  ///
  /// A fileira de fontes so aparece quando ha mais de uma familia, e o
  /// aplicativo vem com UMA: sem este botao nao havia por onde trocar a
  /// fonte de um texto 3D — a opcao existia e ficava escondida.
  Future<void> _importarFonte() async {
    if (_importandoFonte) return;
    setState(() => _importandoFonte = true);
    try {
      final r = await FilePicker.platform.pickFiles(
        type: FileType.custom,
        allowedExtensions: const ['ttf', 'otf'],
        allowMultiple: true,
      );
      if (r == null) return;
      final result = await FontService.instance.importMany(
        r.files.map((f) => f.path).whereType<String>(),
      );
      if (!mounted) return;
      setState(() => _familias = FontService.instance.families);
      if (result.imported.isNotEmpty) {
        await _aplicar(_atual.copyWith(familia: result.imported.first));
      }
    } catch (_) {
      // Fonte ilegivel: o texto fica com a fonte que tinha.
    } finally {
      if (mounted) setState(() => _importandoFonte = false);
    }
  }

  // ----------------------------------------------------------- a folha

  @override
  Widget build(BuildContext context) {
    final relogio = widget.playback?.time;
    // A FOLHA NASCE NUMA ROTA CUPERTINO, sem `Material` acima: os `Text`
    // sairiam com o sublinhado amarelo do "texto sem estilo". O `Material`
    // transparente da o `DefaultTextStyle` e o chao dos toques sem pintar
    // nada por cima do painel.
    return Material(
      type: MaterialType.transparency,
      child: Container(
        key: const ValueKey('texto3d-folha'),
        // A METADE DE BAIXO, como a folha da Cena 3D: o palco fica a vista.
        constraints: BoxConstraints(
          maxHeight: MediaQuery.sizeOf(context).height * 0.52,
        ),
        decoration: BoxDecoration(
          color: AmColors.panel,
          borderRadius: const BorderRadius.vertical(top: Radius.circular(18)),
        ),
        padding: const EdgeInsets.fromLTRB(10, 8, 10, 12),
        child: SafeArea(
          top: false,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              _cabecalho(),
              const SizedBox(height: 6),
              Flexible(
                // ROLAGEM SEM PREGUICA: o teste (e o dedo) tem de achar o
                // cartao de baixo sem ele precisar entrar na tela antes.
                child: SingleChildScrollView(
                  child: relogio == null
                      ? _corpo()
                      : ValueListenableBuilder<Duration>(
                          valueListenable: relogio,
                          builder: (_, _, _) => _corpo(),
                        ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  /// A MESMA BARRA DE CIMA DO PAINEL DE EFEITOS: nome a esquerda, acoes a
  /// direita, 48 de altura.
  Widget _cabecalho() => SizedBox(
    height: 48,
    child: Row(
      children: [
        const Expanded(
          child: AppText(
            'Texto 3D',
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: TextStyle(
              fontSize: 17,
              fontWeight: FontWeight.w600,
              color: AmColors.text,
            ),
          ),
        ),
        if (_montando)
          const Padding(
            padding: EdgeInsets.only(right: 10),
            child: CupertinoActivityIndicator(radius: 7),
          ),
        CupertinoButton(
          padding: EdgeInsets.zero,
          onPressed: () => Navigator.of(context).pop(),
          child: AppText(
            'Pronto',
            style: TextStyle(color: AmColors.accent, fontSize: 15),
          ),
        ),
      ],
    ),
  );

  Widget _corpo() => Column(
    crossAxisAlignment: CrossAxisAlignment.stretch,
    children: [
      _campoDeTexto(),
      if (_aviso != null)
        Padding(
          key: const ValueKey('texto3d-aviso'),
          padding: const EdgeInsets.fromLTRB(4, 8, 4, 0),
          child: Text(
            _aviso!,
            style: const TextStyle(fontSize: 12.5, color: Color(0xFFFFB454)),
          ),
        ),
      const SizedBox(height: 10),
      // A ORDEM E A DA PRIORIDADE DO DONO: primeiro o que faz a letra ter
      // volume, depois superficie, depois as letras soltas, a cena e o
      // ajuste fino.
      _cartao('Extrusão', _linhasDaExtrusao),
      _cartao('Material', _linhasDoMaterial),
      _cartao('Caracteres', _linhasDosCaracteres),
      _cartao('Cena', _linhasDaCena),
      _cartao('Fonte e ajuste fino', _linhasDaFonte),
    ],
  );

  Widget _campoDeTexto() => Container(
    decoration: BoxDecoration(
      color: AmColors.campo,
      borderRadius: BorderRadius.circular(10),
    ),
    padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 2),
    child: TextField(
      key: const ValueKey('texto3d-campo'),
      controller: _campo,
      style: const TextStyle(color: AmColors.text, fontSize: 15),
      cursorColor: AmColors.accent,
      textCapitalization: TextCapitalization.characters,
      decoration: InputDecoration(
        border: InputBorder.none,
        hintText: translate(context, 'Escreva o texto'),
        hintStyle: const TextStyle(color: AmColors.muted, fontSize: 15),
      ),
      // A CADA TECLA NAO: refazer a geometria de dez letras custa caro, e
      // o campo perderia o foco a cada reconstrucao. Edita no fim.
      onSubmitted: (v) {
        final limpo = v.trim();
        if (limpo.isEmpty) return;
        _aplicar(_atual.copyWith(texto: limpo));
      },
    ),
  );

  // ---------------------------------------------------------- cartoes

  Widget _cartao(String titulo, List<Widget> Function() linhas) => _Cartao(
    key: ValueKey('texto3d-cartao-${_slug(titulo)}'),
    titulo: titulo,
    aberto: _abertos.contains(titulo),
    aoAlternar: () => setState(() {
      if (!_abertos.remove(titulo)) _abertos.add(titulo);
    }),
    linhas: linhas,
  );

  List<Widget> _linhasDaExtrusao() => [
    // PROFUNDIDADE e o nome do que isto faz — "espessura" e a medida
    // interna da extrusao, e ninguem procura por ela.
    _controle(
      'Profundidade',
      _atual.espessura,
      4,
      120,
      (v) => _aplicar(_atual.copyWith(espessura: v)),
      aoArrastar: (v) => setState(() => _params = _atual.copyWith(espessura: v)),
    ),
    _LinhaDeEscolha(
      rotulo: 'Chanfro',
      opcoes: [for (final c in TipoDeChanfro.values) nomeDoChanfro(c)],
      chaves: [
        for (final c in TipoDeChanfro.values) ValueKey('texto3d-chanfro-${c.name}'),
      ],
      valor: TipoDeChanfro.values.indexOf(_atual.chanfro),
      aoMudar: (i) => _aplicar(_atual.copyWith(chanfro: TipoDeChanfro.values[i])),
    ),
    if (_atual.chanfro != TipoDeChanfro.nenhum)
      _controle(
        'Largura do chanfro',
        _atual.larguraDoChanfro,
        0.5,
        12,
        (v) => _aplicar(_atual.copyWith(larguraDoChanfro: v)),
        aoArrastar: (v) =>
            setState(() => _params = _atual.copyWith(larguraDoChanfro: v)),
      ),
  ];

  List<Widget> _linhasDoMaterial() => [
    // AS FICHAS DE PREDEFINICAO. Escolher um metal LIMPA o acabamento a
    // mao: ouro que continuasse com a rugosidade do cromo anterior seria
    // "escolhi ouro e nao ficou ouro".
    _LinhaDeEscolha(
      rotulo: 'Predefinição',
      opcoes: [for (final e in EstiloDoTexto3D.values) nomeDoEstiloDoTexto3D(e)],
      chaves: [
        for (final e in EstiloDoTexto3D.values)
          ValueKey('texto3d-estilo-${e.name}'),
      ],
      valor: EstiloDoTexto3D.values.indexOf(_estilo),
      aoMudar: (i) => _trocarEstilo(EstiloDoTexto3D.values[i]),
    ),
    ParameterColorRow(
      key: const ValueKey('texto3d-cor'),
      label: 'Cor base',
      color: _corBase,
      onTap: () async {
        final escolhida = await showColorPicker(
          context,
          initial: _corBase,
          withAlpha: false,
        );
        if (escolhida == null) return;
        await _aplicar(_atual.copyWith(cor: escolhida.toARGB32()));
      },
    ),
    _controle(
      'Metal',
      _metalBase,
      0,
      1,
      (v) => _aplicar(_atual.copyWith(metalico: v)),
      aoArrastar: (v) => setState(() => _params = _atual.copyWith(metalico: v)),
    ),
    _controle(
      'Rugosidade',
      _rugosidadeBase,
      0,
      1,
      (v) => _aplicar(_atual.copyWith(rugosidade: v)),
      aoArrastar: (v) => setState(() => _params = _atual.copyWith(rugosidade: v)),
    ),
    _controle(
      'Brilho próprio',
      _atual.emissivo,
      0,
      2,
      (v) => _aplicar(_atual.copyWith(emissivo: v)),
      aoArrastar: (v) => setState(() => _params = _atual.copyWith(emissivo: v)),
    ),
  ];

  /// REFLEXO / ILUMINACAO / AMBIENTE: quanto do estudio ao redor a letra
  /// devolve, qual estudio e quanta luz solta ha no ar. Vao para a CENA,
  /// nao para o material, e por isso nao passam pelo `_aplicar` (que refaz
  /// a malha) — aqui nao ha malha a refazer.
  List<Widget> _linhasDaCena() => [
    _controle('Reflexo', _reflexoAtual, 0, 1, _mudarReflexo,
        aoArrastar: _mudarReflexo),
    _LinhaDeEscolha(
      rotulo: 'Iluminação',
      opcoes: [for (final k in EnvironmentKind.values) environmentLabel(k)],
      chaves: [
        for (final k in EnvironmentKind.values)
          ValueKey('texto3d-iluminacao-${k.name}'),
      ],
      valor: EnvironmentKind.values.indexOf(_iluminacaoAtual),
      aoMudar: (i) {
        _controlador.trocarIluminacaoDoTexto3D(
          widget.sceneId,
          EnvironmentKind.values[i],
        );
        setState(() {});
      },
    ),
    _controle('Ambiente', _ambienteAtual, 0, 1, _mudarAmbiente,
        aoArrastar: _mudarAmbiente),
  ];

  List<Widget> _linhasDaFonte() => [
    Padding(
      padding: const EdgeInsets.fromLTRB(2, 2, 2, 6),
      child: Align(
        alignment: Alignment.centerLeft,
        child: CupertinoButton(
          key: const ValueKey('texto3d-importar-fonte'),
          padding: EdgeInsets.zero,
          minimumSize: const Size(0, 32),
          onPressed: _importandoFonte ? null : _importarFonte,
          child: AppText(
            _importandoFonte
                ? 'Importando fonte...'
                : 'Importar fonte (.ttf / .otf)',
            style: const TextStyle(fontSize: 13),
          ),
        ),
      ),
    ),
    if (_familias.length > 1)
      _LinhaDeEscolha(
        rotulo: 'Fonte',
        opcoes: _familias,
        chaves: [for (final f in _familias) ValueKey('texto3d-fonte-$f')],
        valor: _familias.indexOf(_atual.familia),
        aoMudar: (i) => _aplicar(_atual.copyWith(familia: _familias[i])),
      ),
    _controle(
      'Espaçamento',
      _atual.espacamento,
      -0.2,
      0.6,
      (v) => _aplicar(_atual.copyWith(espacamento: v)),
      aoArrastar: (v) =>
          setState(() => _params = _atual.copyWith(espacamento: v)),
    ),
    _LinhaDeEscolha(
      rotulo: 'Qualidade',
      opcoes: [for (final q in QualidadeDoTexto3D.values) nomeDaQualidade(q)],
      chaves: [
        for (final q in QualidadeDoTexto3D.values)
          ValueKey('texto3d-qualidade-${q.name}'),
      ],
      valor: QualidadeDoTexto3D.values.indexOf(_atual.qualidade),
      aoMudar: (i) =>
          _aplicar(_atual.copyWith(qualidade: QualidadeDoTexto3D.values[i])),
    ),
  ];

  // ------------------------------------------------------- caracteres

  /// MOVER LETRAS SOLTAS: escolher a faixa e mexer so nela.
  ///
  /// "Todas as letras" e a faixa aberta (0..fim), e nao um modo a parte: o
  /// que muda de um caso para o outro sao dois numeros, e as mesmas nove
  /// linhas servem os tres.
  List<Widget> _linhasDosCaracteres() {
    final letras = _letras;
    final ultima = (letras.length - 1).clamp(0, 1 << 20).toDouble();
    return [
      _LinhaDeEscolha(
        rotulo: 'Seleção',
        opcoes: const ['Todas as letras', 'Um caractere', 'Intervalo'],
        chaves: const [
          ValueKey('texto3d-selecao-todas'),
          ValueKey('texto3d-selecao-uma'),
          ValueKey('texto3d-selecao-intervalo'),
        ],
        valor: _Selecao.values.indexOf(_selecao),
        aoMudar: (i) => setState(() => _selecao = _Selecao.values[i]),
      ),
      if (_selecao != _Selecao.todas) _fileiraDeLetras(letras),
      if (_selecao == _Selecao.intervalo) ...[
        _controle(
          'De',
          _de.toDouble().clamp(0, ultima),
          0,
          ultima,
          (v) => setState(() => _de = v.round()),
          casas: 0,
          direto: true,
        ),
        _controle(
          'Até',
          _ate.toDouble().clamp(0, ultima),
          0,
          ultima,
          (v) => setState(() => _ate = v.round()),
          casas: 0,
          direto: true,
        ),
      ],
      for (final m in MedidaDoCaractere.values) _linhaDaMedida(m),
    ];
  }

  /// AS LETRAS COMO FICHAS: tocar no "C" escolhe o "C". Num celular,
  /// procurar o indice 2 de "ABCDE" num numero e trabalho; a letra esta ali.
  Widget _fileiraDeLetras(List<String> letras) {
    final (i0, f0) = _faixa;
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Row(
        children: [
          const _ChipDoRotulo('Letras'),
          const SizedBox(width: 8),
          Expanded(
            child: Wrap(
              spacing: 6,
              runSpacing: 6,
              children: [
                for (var i = 0; i < letras.length; i++)
                  Builder(
                    builder: (_) {
                      final dentro = i >= i0 && (f0 < 0 || i <= f0);
                      return Tocavel(
                        key: ValueKey('texto3d-letra-$i'),
                        onTap: () => setState(() {
                          if (_selecao == _Selecao.uma) {
                            _umCaractere = i;
                          } else if (i < _de) {
                            _de = i;
                          } else {
                            _ate = i;
                          }
                        }),
                        child: Container(
                          width: 30,
                          height: 30,
                          alignment: Alignment.center,
                          decoration: BoxDecoration(
                            color: dentro ? AmColors.accentDim : AmColors.campo,
                            borderRadius: BorderRadius.circular(8),
                          ),
                          child: Text(
                            letras[i].trim().isEmpty ? '␣' : letras[i],
                            style: TextStyle(
                              fontSize: 13,
                              color: dentro ? AmColors.accent : AmColors.text,
                            ),
                          ),
                        ),
                      );
                    },
                  ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _linhaDaMedida(MedidaDoCaractere m) {
    final aj = _ajuste;
    final trilha = aj.trilha(m);
    final agora = _local;
    final (minimo, maximo, casas, unidade) = _faixaDaMedida(m);
    return ParameterRow(
      key: ValueKey('texto3d-ajuste-${m.name}'),
      label: rotuloDaMedida(m),
      value: trilha.valueAt(agora),
      min: minimo,
      max: maximo,
      unit: unidade,
      decimals: casas,
      unitsPerPixel: (maximo - minimo).abs() / 260,
      // O MESMO LOSANGO DE TODA PROPRIEDADE DO APLICATIVO. Nao ha logica
      // de keyframe propria aqui: a trilha e uma `AnimatedDouble` e o
      // losango e o da `ParameterRow`.
      keyframe: KeyframeState(
        animated: trilha.isAnimated,
        here: trilha.hasKeyframeAt(agora),
        onToggle: () => _alternarKeyframe(m),
      ),
      onReset: () => _gravarAjuste(
        aj.com(m, AnimatedDouble(padraoDaMedida(m))),
      ),
      onChanged: (v) => _mudarMedida(m, v.clamp(minimo, maximo).toDouble()),
    );
  }

  /// Faixa, casas e unidade de cada medida. Posicao anda em unidades da
  /// cena (o texto nasce com 100 de corpo), giro em graus, escala em
  /// multiplos, e espacamento e offset em FRACAO DO CORPO — assim valem o
  /// mesmo num texto grande e num pequeno.
  static (double, double, int, String) _faixaDaMedida(MedidaDoCaractere m) =>
      switch (m) {
        MedidaDoCaractere.x ||
        MedidaDoCaractere.y ||
        MedidaDoCaractere.z => (-400, 400, 1, ''),
        MedidaDoCaractere.girX ||
        MedidaDoCaractere.girY ||
        MedidaDoCaractere.girZ => (-180, 180, 1, '°'),
        MedidaDoCaractere.escala => (0, 4, 2, '×'),
        MedidaDoCaractere.espacamento => (-1, 2, 2, ''),
        MedidaDoCaractere.offset => (-4, 4, 2, ''),
      };

  // ------------------------------------------------------- a linha da casa

  /// UMA LINHA DE PARAMETRO DA CASA — a mesma `ParameterRow` das fichas do
  /// editor (nome, regua de arrastar e caixa de valor que abre o teclado),
  /// e nao um slider avulso.
  ///
  /// O VALOR APARECE NA HORA E A MALHA ESPERA O DEDO. Refazer extrusao e
  /// chanfro a cada pixel de arrasto travaria a folha; o numero muda a cada
  /// passo e a geometria e refeita 140 ms depois do ultimo. [direto] pula a
  /// espera para o que nao toca na malha.
  Widget _controle(
    String rotulo,
    double valor,
    double minimo,
    double maximo,
    ValueChanged<double> aoMudar, {
    String sufixo = '',
    int? casas,
    bool direto = false,
    ValueChanged<double>? aoArrastar,
  }) {
    final faixa = (maximo - minimo).abs();
    return ParameterRow(
      key: ValueKey('texto3d-controle-$rotulo'),
      label: rotulo,
      value: valor,
      min: minimo,
      max: maximo,
      unit: sufixo,
      decimals: casas ?? (faixa <= 2 ? 2 : (faixa <= 30 ? 1 : 0)),
      unitsPerPixel: faixa <= 0 ? 1 : faixa / 260,
      onChanged: (v) {
        // O SINAL DE "HA UM GESTO EM CURSO": o palco troca qualidade por
        // resposta enquanto o dedo anda.
        Interacao.marcar();
        final limitado = v.clamp(minimo, maximo).toDouble();
        if (direto) {
          aoMudar(limitado);
          return;
        }
        aoArrastar?.call(limitado);
        _esperaDoControle?.cancel();
        _esperaDoControle = Timer(const Duration(milliseconds: 140), () {
          if (mounted) aoMudar(limitado);
        });
      },
    );
  }

  /// O NOME DO CARTAO VIRA CHAVE: "Extrusão" -> "extrusao".
  ///
  /// O acento SAI ANTES do corte: sem isto "Extrusão" virava "extrus-o" —
  /// uma chave que ninguem adivinha e que muda sozinha se o rotulo ganhar
  /// ou perder um acento.
  static String _slug(String s) {
    const acentos = {
      'á': 'a', 'à': 'a', 'â': 'a', 'ã': 'a', 'ä': 'a',
      'é': 'e', 'ê': 'e', 'è': 'e',
      'í': 'i', 'ì': 'i', 'î': 'i',
      'ó': 'o', 'ô': 'o', 'õ': 'o', 'ò': 'o',
      'ú': 'u', 'ü': 'u', 'ù': 'u',
      'ç': 'c', 'ñ': 'n',
    };
    var texto = s.toLowerCase();
    acentos.forEach((de, para) => texto = texto.replaceAll(de, para));
    return texto
        .replaceAll(RegExp('[^a-z0-9]+'), '-')
        .replaceAll(RegExp(r'^-|-$'), '');
  }
}

// ====================================================== os componentes
//
// Os tres moldes do painel de Efeitos (`am/effects_panel.dart`), onde eles
// sao privados: o cartao que abre e fecha, o chip do rotulo e a fileira de
// escolha. Copiados na medida — mesma altura, mesmo raio, mesma cor, mesmo
// peso de fonte — para a folha e o painel parecerem a mesma peca, que e o
// que o dono pediu.

/// O CARTAO DE UM GRUPO: ▼ recolhe, ▶ abre. Recolhido nem constroi o corpo.
class _Cartao extends StatelessWidget {
  const _Cartao({
    super.key,
    required this.titulo,
    required this.aberto,
    required this.aoAlternar,
    required this.linhas,
  });

  final String titulo;
  final bool aberto;
  final VoidCallback aoAlternar;
  final List<Widget> Function() linhas;

  @override
  Widget build(BuildContext context) => Container(
    margin: const EdgeInsets.only(bottom: 10),
    padding: const EdgeInsets.fromLTRB(10, 2, 6, 8),
    decoration: BoxDecoration(
      color: AmColors.panelHigh,
      borderRadius: BorderRadius.circular(14),
    ),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        SizedBox(
          height: 48,
          child: Semantics(
            button: true,
            expanded: aberto,
            label: titulo,
            child: Tocavel(
              encolhe: 1,
              onTap: aoAlternar,
              child: Row(
                children: [
                  Icon(
                    aberto
                        ? CupertinoIcons.arrowtriangle_down_fill
                        : CupertinoIcons.arrowtriangle_right_fill,
                    size: 13,
                    color: AmColors.text,
                  ),
                  const SizedBox(width: 12),
                  Flexible(
                    child: AppText(
                      titulo,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(
                        fontSize: 17,
                        fontWeight: FontWeight.w600,
                        color: AmColors.text,
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
        if (aberto)
          Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: linhas(),
          ),
      ],
    ),
  );
}

/// O CHIP DA ESQUERDA das linhas que nao tem regua: mesma largura e mesmo
/// tipo do chip da linha de parametro, para a coluna dos nomes nao pular.
class _ChipDoRotulo extends StatelessWidget {
  const _ChipDoRotulo(this.rotulo);

  final String rotulo;

  @override
  Widget build(BuildContext context) => Container(
    width: 94,
    height: 32,
    alignment: Alignment.center,
    padding: const EdgeInsets.symmetric(horizontal: 6),
    child: AppText(
      rotulo,
      maxLines: 2,
      textAlign: TextAlign.center,
      overflow: TextOverflow.ellipsis,
      style: const TextStyle(
        fontSize: 12,
        height: 1.1,
        fontWeight: FontWeight.w600,
        color: AmColors.muted,
      ),
    ),
  );
}

/// [nome]  (A) (B) (C) — as fichas de escolha do painel de Efeitos.
class _LinhaDeEscolha extends StatelessWidget {
  const _LinhaDeEscolha({
    required this.rotulo,
    required this.opcoes,
    required this.valor,
    required this.aoMudar,
    this.chaves,
  });

  final String rotulo;
  final List<String> opcoes;
  final List<Key>? chaves;
  final int valor;
  final ValueChanged<int> aoMudar;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.symmetric(vertical: 6),
    child: Row(
      crossAxisAlignment: CrossAxisAlignment.center,
      children: [
        _ChipDoRotulo(rotulo),
        const SizedBox(width: 8),
        Expanded(
          child: Wrap(
            spacing: 6,
            runSpacing: 6,
            children: [
              for (var i = 0; i < opcoes.length; i++)
                Tocavel(
                  key: chaves != null && i < chaves!.length ? chaves![i] : null,
                  onTap: () => aoMudar(i),
                  child: Container(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 11,
                      vertical: 6,
                    ),
                    decoration: BoxDecoration(
                      color: valor == i ? AmColors.accentDim : AmColors.campo,
                      borderRadius: BorderRadius.circular(10),
                    ),
                    child: AppText(
                      opcoes[i],
                      style: TextStyle(
                        fontSize: 12,
                        color: valor == i ? AmColors.accent : AmColors.text,
                      ),
                    ),
                  ),
                ),
            ],
          ),
        ),
      ],
    ),
  );
}

String nomeDoChanfro(TipoDeChanfro c) => switch (c) {
  TipoDeChanfro.nenhum => 'Sem chanfro',
  TipoDeChanfro.angular => 'Angular',
  TipoDeChanfro.redondo => 'Redondo',
};

String nomeDaQualidade(QualidadeDoTexto3D q) => switch (q) {
  QualidadeDoTexto3D.baixa => 'Baixa',
  QualidadeDoTexto3D.media => 'Média',
  QualidadeDoTexto3D.alta => 'Alta',
};
