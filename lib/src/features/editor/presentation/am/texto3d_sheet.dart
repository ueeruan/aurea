import 'dart:async';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/l10n/app_language.dart';
import '../../application/editor_controller.dart';
import '../../application/font_service.dart';
import '../../domain/layer.dart';
import '../../domain/modelo_do_texto3d.dart';
import '../../domain/texto3d.dart';
import '../context/parameter_row.dart';
import 'am_colors.dart';

/// A FOLHA DO TEXTO 3D — onde a letra e feita e refeita.
///
/// ============================ POR QUE UMA FOLHA =======================
///
/// Antes disto, criar um texto 3D eram tres perguntas em fila (o texto, o
/// metal, a fonte) e NADA depois: para trocar uma palavra ou engrossar a
/// letra, o dono jogava a camada fora e refazia — perdendo a posicao, o
/// giro, os keyframes e a camera que ja tinha ajustado.
///
/// Aqui o texto e um objeto que se edita: o campo do texto, a fonte, o
/// metal, a espessura, o chanfro e o espacamento.
///
/// ============================ UM PREVIEW SO ============================
///
/// A folha OCUPA A METADE DE BAIXO e o palco fica a vista por cima: cada
/// toque aqui aparece na cena de verdade, no unico preview do aplicativo.
/// Ela ja teve uma previa propria (a mesma cena, pelo mesmo motor, num
/// quadro de 320x320) e isso custava caro de um jeito que nao se via: o
/// motor tem UM alvo, e alternar o tamanho dele entre a folha e o palco
/// recriava cor, profundidade, MSAA e staging DUAS vezes por toque — pico
/// de memoria, dois desenhos e dois readbacks para mostrar a mesma coisa
/// duas vezes. O dono exige um preview oficial, e ele e o palco.
///
/// [playhead] fica na assinatura por compatibilidade com quem chama; o
/// instante que se ve e o do palco, que ja e o do cabecote.
Future<void> showTexto3DSheet(
  BuildContext context,
  WidgetRef ref, {
  required String sceneId,
  required String nodeId,
  Duration playhead = Duration.zero,
}) async {
  await showCupertinoModalPopup<void>(
    context: context,
    // O ESCURECIMENTO E LEVE de proposito: o palco atras e o preview.
    barrierColor: const Color(0x33000000),
    builder: (_) => _Texto3DSheet(sceneId: sceneId, nodeId: nodeId),
  );
}

class _Texto3DSheet extends ConsumerStatefulWidget {
  const _Texto3DSheet({required this.sceneId, required this.nodeId});

  final String sceneId;
  final String nodeId;

  @override
  ConsumerState<_Texto3DSheet> createState() => _Texto3DSheetState();
}

class _Texto3DSheetState extends ConsumerState<_Texto3DSheet> {
  late final TextEditingController _campo;
  Texto3D? _params;
  EstiloDoTexto3D _estilo = EstiloDoTexto3D.ouro;
  List<String> _familias = const [];
  bool _montando = false;

  @override
  void initState() {
    super.initState();
    final projeto = ref.read(editorControllerProvider);
    final camada = projeto.layerById(widget.sceneId);
    final no = camada is Scene3DLayer
        ? camada.scene.nodeById(widget.nodeId)
        : null;
    // SEM OS PARAMETROS GRAVADOS NAO HA O QUE EDITAR, e um texto vazio seria
    // pior do que dizer: cai no nome do no, que e o que o dono ve.
    _params =
        no?.texto3d ??
        Texto3D(texto: no?.name ?? 'TEXTO 3D', separarLetras: false);
    _estilo = no?.estiloTexto3d ?? EstiloDoTexto3D.ouro;
    _campo = TextEditingController(text: _params!.texto);
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

  Texto3D get _atual => _params!;

  /// APLICA E ESPERA. Cada mudanca refaz a geometria (extrusao, chanfro e
  /// material) e sobe para o motor; o botao fica travado enquanto isso para
  /// um segundo toque nao enfileirar outra construcao pela metade.
  String? _aviso;

  Future<void> _aplicar(Texto3D novo) async {
    final anterior = _params;
    setState(() {
      _params = novo;
      _montando = true;
      _aviso = null;
    });
    final controlador = ref.read(editorControllerProvider.notifier);
    final deuCerto = await controlador.editarTexto3D(
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
        _aviso = controlador.ultimoMotivoDoTexto3D ??
            'Nao foi possivel aplicar essa mudanca ao texto 3D.';
      }
    });
  }

  Future<void> _trocarEstilo(EstiloDoTexto3D estilo) async {
    setState(() => _estilo = estilo);
    await _aplicar(_atual);
  }

  @override
  Widget build(BuildContext context) {
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
          borderRadius: BorderRadius.vertical(top: Radius.circular(18)),
        ),
        padding: const EdgeInsets.fromLTRB(14, 10, 14, 18),
        child: SafeArea(
          top: false,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              _cabecalho(),
              const SizedBox(height: 8),
              Flexible(
                child: SingleChildScrollView(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      _campoDeTexto(),
                      const SizedBox(height: 10),
                      ..._linhas(),
                    ],
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _cabecalho() {
    return Row(
      children: [
        const Expanded(
          child: AppText(
            'Texto 3D',
            style: TextStyle(
              color: AmColors.text,
              fontSize: 15,
              fontWeight: FontWeight.w600,
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
    );
  }

  Widget _campoDeTexto() {
    return Container(
      decoration: BoxDecoration(
        color: AmColors.chip,
        borderRadius: BorderRadius.circular(10),
      ),
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 2),
      child: TextField(
        key: const ValueKey('texto3d-campo'),
        controller: _campo,
        style: const TextStyle(color: AmColors.text, fontSize: 15),
        cursorColor: AmColors.accent,
        textCapitalization: TextCapitalization.characters,
        decoration: const InputDecoration(
          border: InputBorder.none,
          hintText: 'Escreva o texto',
          hintStyle: TextStyle(color: AmColors.muted, fontSize: 15),
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
  }

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

  List<Widget> _linhas() {
    return [
      if (_aviso != null)
        Padding(
          key: const ValueKey('texto3d-aviso'),
          padding: const EdgeInsets.fromLTRB(16, 6, 16, 6),
          child: Text(
            _aviso!,
            style: const TextStyle(fontSize: 12.5, color: Color(0xFFFFB454)),
          ),
        ),
      Padding(
        padding: const EdgeInsets.fromLTRB(16, 4, 16, 4),
        child: Align(
          alignment: Alignment.centerLeft,
          child: CupertinoButton(
            key: const ValueKey('texto3d-importar-fonte'),
            padding: EdgeInsets.zero,
            minimumSize: const Size(0, 32),
            onPressed: _importandoFonte ? null : _importarFonte,
            child: AppText(
              _importandoFonte ? 'Importando fonte...' : 'Importar fonte (.ttf / .otf)',
              style: const TextStyle(fontSize: 13),
            ),
          ),
        ),
      ),
      if (_familias.length > 1)
        _fileiraDeOpcoes<String>(
          'Fonte',
          _familias,
          (f) => f,
          _atual.familia,
          (f) => _aplicar(_atual.copyWith(familia: f)),
          chave: (f) => ValueKey('texto3d-fonte-$f'),
        ),
      _secao('Material'),
      _fileiraDeOpcoes<EstiloDoTexto3D>(
        'Metal',
        EstiloDoTexto3D.values,
        nomeDoEstiloDoTexto3D,
        _estilo,
        _trocarEstilo,
        chave: (e) => ValueKey('texto3d-estilo-${e.name}'),
      ),
      _secao('Forma'),
      _controle(
        'Espessura',
        _atual.espessura,
        4,
        120,
        (v) => _aplicar(_atual.copyWith(espessura: v)),
        sufixo: '',
        aoArrastar: (v) =>
            setState(() => _params = _atual.copyWith(espessura: v)),
      ),
      _fileiraDeOpcoes<TipoDeChanfro>(
        'Chanfro',
        TipoDeChanfro.values,
        _nomeDoChanfro,
        _atual.chanfro,
        (c) => _aplicar(_atual.copyWith(chanfro: c)),
        chave: (c) => ValueKey('texto3d-chanfro-${c.name}'),
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
      _controle(
        'Espaçamento',
        _atual.espacamento,
        -0.2,
        0.6,
        (v) => _aplicar(_atual.copyWith(espacamento: v)),
        aoArrastar: (v) =>
            setState(() => _params = _atual.copyWith(espacamento: v)),
      ),
      // GIRO POR LETRA, como o "Per-character 3D" do After Effects: cada
      // letra gira em torno do proprio centro. O giro do texto INTEIRO e o
      // da camada (Transformar > Rotacao), e nao este.
      _secao('Letras'),
      _controle(
        'Girar letras X',
        _atual.rotLetraX,
        -180,
        180,
        (v) => _aplicar(_atual.copyWith(rotLetraX: v)),
        sufixo: '°',
        aoArrastar: (v) =>
            setState(() => _params = _atual.copyWith(rotLetraX: v)),
      ),
      _controle(
        'Girar letras Y',
        _atual.rotLetraY,
        -180,
        180,
        (v) => _aplicar(_atual.copyWith(rotLetraY: v)),
        sufixo: '°',
        aoArrastar: (v) =>
            setState(() => _params = _atual.copyWith(rotLetraY: v)),
      ),
      _controle(
        'Girar letras Z',
        _atual.rotLetraZ,
        -180,
        180,
        (v) => _aplicar(_atual.copyWith(rotLetraZ: v)),
        sufixo: '°',
        aoArrastar: (v) =>
            setState(() => _params = _atual.copyWith(rotLetraZ: v)),
      ),
      _secao('Qualidade'),
      _fileiraDeOpcoes<QualidadeDoTexto3D>(
        'Qualidade',
        QualidadeDoTexto3D.values,
        _nomeDaQualidade,
        _atual.qualidade,
        (q) => _aplicar(_atual.copyWith(qualidade: q)),
        chave: (q) => ValueKey('texto3d-qualidade-${q.name}'),
      ),
    ];
  }

  Widget _fileiraDeOpcoes<T>(
    String rotulo,
    List<T> valores,
    String Function(T) nome,
    T? escolhido,
    ValueChanged<T> aoEscolher, {
    required Key Function(T) chave,
  }) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 8),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          AppText(
            rotulo,
            style: const TextStyle(color: AmColors.muted, fontSize: 12),
          ),
          const SizedBox(height: 6),
          SingleChildScrollView(
            scrollDirection: Axis.horizontal,
            child: Row(
              children: [
                for (final v in valores)
                  Padding(
                    padding: const EdgeInsets.only(right: 6),
                    child: GestureDetector(
                      key: chave(v),
                      onTap: () => aoEscolher(v),
                      child: Container(
                        padding: const EdgeInsets.symmetric(
                          horizontal: 12,
                          vertical: 7,
                        ),
                        decoration: BoxDecoration(
                          color: v == escolhido ? AmColors.accent : AmColors.chip,
                          borderRadius: BorderRadius.circular(9),
                        ),
                        child: AppText(
                          nome(v),
                          style: TextStyle(
                            color: v == escolhido
                                ? Colors.white
                                : AmColors.text,
                            fontSize: 13,
                          ),
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

  Timer? _esperaDoControle;

  /// UMA LINHA DE PARAMETRO DA CASA — a mesma `ParameterRow` das fichas do
  /// editor (nome, valor e regua de arrastar), e nao um slider avulso.
  ///
  /// A folha tinha componentes proprios (rotulo solto + slider do Material),
  /// e por isso nao parecia com o resto do Aurea. Com a linha da casa ela
  /// herda o toque, a tipografia e a regua que o dono ja conhece.
  ///
  /// O VALOR APARECE NA HORA E A MALHA ESPERA O DEDO. Refazer extrusao e
  /// chanfro a cada pixel de arrasto travaria a folha; o numero muda a cada
  /// passo e a geometria e refeita 140 ms depois do ultimo.
  Widget _controle(
    String rotulo,
    double valor,
    double minimo,
    double maximo,
    ValueChanged<double> aoMudar, {
    String sufixo = '',
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
      decimals: faixa <= 2 ? 2 : (faixa <= 30 ? 1 : 0),
      unitsPerPixel: faixa / 260,
      onChanged: (v) {
        final limitado = v.clamp(minimo, maximo).toDouble();
        aoArrastar?.call(limitado);
        _esperaDoControle?.cancel();
        _esperaDoControle = Timer(
          const Duration(milliseconds: 140),
          () {
            if (mounted) aoMudar(limitado);
          },
        );
      },
    );
  }

  /// O TITULO DE UM GRUPO DE LINHAS, no tom das fichas do editor.
  Widget _secao(String titulo) => Padding(
    padding: const EdgeInsets.fromLTRB(2, 14, 2, 6),
    child: AppText(
      titulo.toUpperCase(),
      style: const TextStyle(
        color: AmColors.muted,
        fontSize: 11,
        fontWeight: FontWeight.w600,
        letterSpacing: 0.8,
      ),
    ),
  );
}

String _nomeDoChanfro(TipoDeChanfro c) => switch (c) {
  TipoDeChanfro.nenhum => 'Sem chanfro',
  TipoDeChanfro.angular => 'Angular',
  TipoDeChanfro.redondo => 'Redondo',
};

String _nomeDaQualidade(QualidadeDoTexto3D q) => switch (q) {
  QualidadeDoTexto3D.baixa => 'Baixa',
  QualidadeDoTexto3D.media => 'Média',
  QualidadeDoTexto3D.alta => 'Alta',
};
