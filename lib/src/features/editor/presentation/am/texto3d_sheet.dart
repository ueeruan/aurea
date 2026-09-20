import 'package:file_picker/file_picker.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/l10n/app_language.dart';
import '../../application/editor_controller.dart';
import '../../application/font_service.dart';
import '../../application/motor3d_nativo.dart';
import '../../domain/layer.dart';
import '../../domain/modelo_do_texto3d.dart';
import '../../domain/video_project.dart';
import '../../domain/texto3d.dart';
import '../widgets/preview_stage.dart' show estado3DDoQuadro;
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
/// metal, a espessura, o chanfro e o espacamento, com a A PREVIA AO VIVO
/// em cima — a mesma cena que o palco desenha, pelo mesmo caminho
/// (`estado3DDoQuadro` + a ponte do motor), so que num quadro menor.
///
/// ============================ O QUE A PREVIA E ========================
///
/// A CENA DE VERDADE, e nao um desenho parecido. Um texto 3D e metal: um
/// desenho de mentira mostraria a geometria certa com o brilho errado, e a
/// decisao de ouro ou cromo seria tomada olhando a coisa errada. Sem motor
/// (§43) a previa diz que ele nao esta ali, e nao inventa um substituto.
Future<void> showTexto3DSheet(
  BuildContext context,
  WidgetRef ref, {
  required String sceneId,
  required String nodeId,
  Duration playhead = Duration.zero,
}) async {
  await showCupertinoModalPopup<void>(
    context: context,
    builder: (_) => _Texto3DSheet(
      sceneId: sceneId,
      nodeId: nodeId,
      playhead: playhead,
    ),
  );
}

class _Texto3DSheet extends ConsumerStatefulWidget {
  const _Texto3DSheet({
    required this.sceneId,
    required this.nodeId,
    required this.playhead,
  });

  final String sceneId;
  final String nodeId;
  final Duration playhead;

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
    _campo.dispose();
    super.dispose();
  }

  Texto3D get _atual => _params!;

  /// APLICA E ESPERA. Cada mudanca refaz a geometria (extrusao, chanfro e
  /// material) e sobe para o motor; o botao fica travado enquanto isso para
  /// um segundo toque nao enfileirar outra construcao pela metade.
  Future<void> _aplicar(Texto3D novo) async {
    setState(() {
      _params = novo;
      _montando = true;
    });
    await ref
        .read(editorControllerProvider.notifier)
        .editarTexto3D(widget.sceneId, widget.nodeId, novo, _estilo);
    if (mounted) setState(() => _montando = false);
  }

  Future<void> _trocarEstilo(EstiloDoTexto3D estilo) async {
    setState(() => _estilo = estilo);
    await _aplicar(_atual);
  }

  @override
  Widget build(BuildContext context) {
    // A CAMADA E RELIDA A CADA QUADRO: a folha nao guarda copia da cena —
    // quem manda e a timeline, e a previa desenha o que ela diz.
    final projeto = ref.watch(editorControllerProvider);
    final camada = projeto.layerById(widget.sceneId);

    return Container(
      decoration: const BoxDecoration(
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
            SizedBox(
              height: 190,
              child: camada is Scene3DLayer
                  ? _PreviaDoTexto3D(
                      layer: camada,
                      project: projeto,
                      playhead: widget.playhead,
                    )
                  : const SizedBox.shrink(),
            ),
            const SizedBox(height: 10),
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
          child: const AppText(
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
      _fileiraDeOpcoes<EstiloDoTexto3D>(
        'Metal',
        EstiloDoTexto3D.values,
        nomeDoEstiloDoTexto3D,
        _estilo,
        _trocarEstilo,
        chave: (e) => ValueKey('texto3d-estilo-${e.name}'),
      ),
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

  Widget _controle(
    String rotulo,
    double valor,
    double minimo,
    double maximo,
    ValueChanged<double> aoMudar, {
    String sufixo = '',
    ValueChanged<double>? aoArrastar,
  }) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 4),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Expanded(
                child: AppText(
                  rotulo,
                  style: const TextStyle(color: AmColors.muted, fontSize: 12),
                ),
              ),
              AppText(
                '${valor.toStringAsFixed(valor.abs() < 10 ? 1 : 0)}$sufixo',
                style: const TextStyle(color: AmColors.text, fontSize: 12),
              ),
            ],
          ),
          SliderTheme(
            data: SliderThemeData(
              trackHeight: 3,
              activeTrackColor: AmColors.accent,
              inactiveTrackColor: AmColors.chip,
              thumbColor: Colors.white,
              overlayShape: const RoundSliderOverlayShape(overlayRadius: 14),
              thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 7),
            ),
            child: Slider(
              value: valor.clamp(minimo, maximo),
              min: minimo,
              max: maximo,
              // O NUMERO ACOMPANHA O DEDO, E A GEOMETRIA NAO.
              //
              // Refazer a extrusao a cada quadro do arrasto travaria a
              // folha; nao fazer NADA no arrasto e pior ainda, e foi o que
              // estava aqui: o botao ficava parado, o rotulo nao mudava, e
              // o controle parecia morto ate o dedo soltar. O valor anda no
              // estado local e a letra so e refeita no fim.
              onChanged: (v) =>
                  aoArrastar != null ? aoArrastar(v) : setState(() {}),
              onChangeEnd: (v) => aoMudar(v),
            ),
          ),
        ],
      ),
    );
  }
}

/// A PREVIA AO VIVO — a cena de verdade, no motor de verdade.
class _PreviaDoTexto3D extends StatefulWidget {
  const _PreviaDoTexto3D({
    required this.layer,
    required this.project,
    required this.playhead,
  });

  final Scene3DLayer layer;
  final VideoProject project;

  /// O INSTANTE DO PALCO, e nao o comeco da camada.
  ///
  /// A previa mostrava sempre o primeiro quadro: um texto com posicao
  /// animada aparecia parado no lugar de onde ele saiu, e o dono ajustava a
  /// folha olhando uma cena que nao e a que esta no palco.
  final Duration playhead;

  @override
  State<_PreviaDoTexto3D> createState() => _PreviaDoTexto3DState();
}

class _PreviaDoTexto3DState extends State<_PreviaDoTexto3D> {
  @override
  void initState() {
    super.initState();
    Motor3DNativo.instance.revision.addListener(_acordar);
  }

  @override
  void dispose() {
    Motor3DNativo.instance.revision.removeListener(_acordar);
    super.dispose();
  }

  void _acordar() {
    if (mounted) setState(() {});
  }

  @override
  Widget build(BuildContext context) {
    final motor = Motor3DNativo.instance;
    if (!motor.ligado) {
      return const Center(
        child: AppText(
          'A prévia do 3D aparece no aparelho.',
          style: TextStyle(color: AmColors.muted, fontSize: 12),
        ),
      );
    }
    final estado = estado3DDoQuadro(
      project: widget.project,
      l: widget.layer,
      local: widget.layer.localTime(widget.playhead),
      global: widget.playhead,
      largura: 320,
      altura: 320,
    );
    motor.montar(
      cena: estado.cena,
      camera: estado.camera,
      local: widget.layer.localTime(widget.playhead),
      largura: 320,
      altura: 320,
      aspectoDaComposicao: 1,
    );
    final imagem = motor.quadro(estado.chave);
    if (imagem == null) return const SizedBox.shrink();
    return Container(
      decoration: BoxDecoration(
        color: Colors.black,
        borderRadius: BorderRadius.circular(12),
      ),
      clipBehavior: Clip.antiAlias,
      child: RawImage(image: imagem, fit: BoxFit.contain),
    );
  }
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
