import 'package:aurea/src/core/l10n/app_language.dart';
import 'dart:io';

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/snack.dart';
import '../../application/camera_track_service.dart';
import '../../application/editor_controller.dart';
import '../../application/tracking_service.dart';
import '../../domain/camera_solver3d.dart';
import '../../domain/cena_do_rastreio.dart';
import '../../domain/layer.dart';
import '../../domain/plano_do_rastreio.dart';
import '../estudio/folhas_do_estudio.dart' show escolherModeloProvider;
import '../../application/model_import_service.dart';
import 'am_colors.dart';
import 'am_widgets.dart';

/// A TELA DO RASTREIO DE CÂMERA 3D.
///
/// Depois que a conta fecha, o resultado é uma nuvem de números — e
/// número não é resposta para ninguém. O que a pessoa precisa é
/// RECONHECER a cena: ver os pontos grudados no vídeo, tocar nos que
/// estão no chão, e mandar pôr o texto ali. Esta tela existe para essa
/// travessia, e a ordem dela é a ordem do trabalho:
///
///   ver os pontos → escolher uma superfície → definir o chão →
///   criar a câmera → pôr coisas em cima
///
/// O QUE FICA ESCONDIDO. Matriz essencial, RANSAC, triangulação e
/// reprojeção não aparecem em lugar nenhum, e é de propósito: quem edita
/// vídeo quer que o texto fique parado na calçada, não quer saber por
/// quê. Quem quiser saber abre "Avançado" — e lá está tudo.
class Rastreio3DScreen extends ConsumerStatefulWidget {
  const Rastreio3DScreen({
    super.key,
    required this.layerId,
    required this.solucao,
  });

  final String layerId;
  final SolucaoCamera3D solucao;

  @override
  ConsumerState<Rastreio3DScreen> createState() => _Rastreio3DScreenState();
}

class _Rastreio3DScreenState extends ConsumerState<Rastreio3DScreen> {
  late SolucaoCamera3D _s = widget.solucao;
  List<File> _quadros = const [];
  int _indice = 0;

  final Set<int> _escolhidos = {};
  PlanoDoRastreio? _plano;

  /// O retângulo do laço, em coordenadas do palco.
  Rect? _laco;
  Offset? _inicioDoLaco;

  ModoDoSolve _modo = ModoDoSolve.equilibrado;
  bool _resolvendo = false;

  CameraTrackService get _t => CameraTrackService.instance;
  EditorController get _c => ref.read(editorControllerProvider.notifier);

  @override
  void initState() {
    super.initState();
    _carregarQuadros();
  }

  Future<void> _carregarQuadros() async {
    final camada = ref.read(editorControllerProvider).layerById(widget.layerId);
    if (camada is! VideoLayer) return;
    final arquivos = await TrackingService.instance.quadrosParaMostrar(
      camada.sourcePath,
      start: camada.sourceOffset,
      duration: camada.sourceSpan,
      chave: widget.layerId,
    );
    if (!mounted) return;
    setState(() => _quadros = arquivos);
  }

  // ------------------------------------------------------- a projeção

  /// O quadro do solve que corresponde à imagem que está na tela.
  int get _quadroDoSolve {
    if (_quadros.length < 2) return 0;
    final u = _indice / (_quadros.length - 1);
    return (u * (_s.quadros - 1)).round();
  }

  /// Onde cada ponto 3D cai na imagem, em pixels do quadro analisado.
  ///
  /// A pose mais próxima serve quando o quadro exato não foi resolvido:
  /// entre dois quadros a câmera quase não anda, e um ponto meio pixel
  /// fora é melhor do que um ponto que some.
  Map<int, Offset> _projetados() {
    final pose = _poseMaisPerto(_quadroDoSolve);
    if (pose == null) return const {};
    final f = _s.focalPx;
    final cx = _s.largura / 2, cy = _s.altura / 2;
    final out = <int, Offset>{};
    for (final e in _s.nuvem.entries) {
      final p = projetar(pose.rotacao, pose.translacao, e.value);
      if (p == null) continue;
      final x = cx + p[0] * f, y = cy + p[1] * f;
      if (x < -20 || y < -20 || x > _s.largura + 20 || y > _s.altura + 20) {
        continue;
      }
      out[e.key] = Offset(x, y);
    }
    return out;
  }

  PoseCamera? _poseMaisPerto(int quadro) {
    PoseCamera? melhor;
    var dist = 1 << 30;
    for (final p in _s.poses) {
      final d = (p.quadro - quadro).abs();
      if (d < dist) {
        dist = d;
        melhor = p;
      }
    }
    return melhor;
  }

  // --------------------------------------------------------- escolher

  void _recalcularPlano() {
    _plano = _escolhidos.length >= 3
        ? planoDosPontos(
            _s.nuvem,
            _escolhidos.toList(),
            ladoDeFora: _poseMaisPerto(_quadroDoSolve)?.posicao,
          )
        : null;
  }

  void _tocar(Offset noPalco, Size palco) {
    final proj = _projetados();
    final escala = palco.width / _s.largura;
    int? achado;
    var perto = 26.0;
    for (final e in proj.entries) {
      final d = (e.value * escala - noPalco).distance;
      if (d < perto) {
        perto = d;
        achado = e.key;
      }
    }
    if (achado == null) {
      // Tocar no vazio limpa a escolha. É o gesto que todo mundo tenta
      // primeiro quando escolheu errado.
      if (_escolhidos.isEmpty) return;
      setState(() {
        _escolhidos.clear();
        _plano = null;
      });
      return;
    }
    setState(() {
      if (!_escolhidos.remove(achado)) _escolhidos.add(achado!);
      _recalcularPlano();
    });
  }

  void _fecharLaco(Size palco) {
    final r = _laco;
    if (r == null) {
      setState(() => _inicioDoLaco = null);
      return;
    }
    final proj = _projetados();
    final escala = palco.width / _s.largura;
    setState(() {
      for (final e in proj.entries) {
        if (r.contains(e.value * escala)) _escolhidos.add(e.key);
      }
      _laco = null;
      _inicioDoLaco = null;
      _recalcularPlano();
    });
  }

  // ----------------------------------------------------------- ações

  Future<void> _guardar(SolucaoCamera3D nova) async {
    setState(() {
      _s = nova;
      _escolhidos.removeWhere((id) => !nova.nuvem.containsKey(id));
      _recalcularPlano();
    });
    await _t.guardar(widget.layerId, nova);
  }

  Future<void> _definirChao({bool automatico = false}) async {
    final ids = automatico ? maiorPlano(_s.nuvem)?.ids : _escolhidos.toList();
    if (ids == null || ids.length < 3) {
      AureaSnack.show(
        context,
        automatico
            ? 'Não achei uma superfície nítida na nuvem. Escolha uns '
                  'pontos no chão e toque em Definir chão.'
            : 'Escolha pelo menos três pontos que estejam no chão.',
        duration: const Duration(seconds: 5),
      );
      return;
    }
    await _guardar(definirChao(_s, ids));
    if (!mounted) return;
    AureaSnack.show(
      context,
      'Chão definido. O mundo 3D agora tem esse plano como y = 0.'
      '${_avisoDeCenaExistente()}',
      duration: const Duration(seconds: 4),
    );
  }

  Future<void> _apagarEscolhidos() async {
    if (_escolhidos.isEmpty) return;
    final quantos = _escolhidos.length;
    await _guardar(semPontos(_s, {..._escolhidos}));
    if (!mounted) return;
    setState(_escolhidos.clear);
    AureaSnack.show(context, '$quantos ponto(s) fora da conta.');
  }

  Future<void> _apagarOsRuins() async {
    final ruins = _s.pontosDaQualidade({QualidadeDoPonto.ruim});
    if (ruins.isEmpty) {
      AureaSnack.show(context, 'Nenhum ponto ruim para apagar.');
      return;
    }
    await _guardar(semPontos(_s, ruins.toSet()));
    if (!mounted) return;
    AureaSnack.show(context, '${ruins.length} ponto(s) ruim(ns) fora.');
  }

  Future<void> _resolverDeNovo() async {
    if (!_t.podeResolverDeNovo(widget.layerId)) {
      AureaSnack.show(
        context,
        'Para recalcular do zero é preciso rastrear de novo — o vídeo '
        'precisa ser lido outra vez.',
        duration: const Duration(seconds: 5),
      );
      return;
    }
    setState(() => _resolvendo = true);
    try {
      // O QUE FOI APAGADO CONTINUA APAGADO. Recalcular sem lembrar dos
      // pontos que a pessoa tirou traria todos de volta, e o trabalho
      // dela seria desfeito pelo botão que devia melhorar o resultado.
      final apagados = {
        for (final id in widget.solucao.nuvem.keys)
          if (!_s.nuvem.containsKey(id)) id,
      };
      final nova = await _t.resolverDeNovo(
        widget.layerId,
        pontosApagados: apagados,
        modo: _modo,
      );
      if (!mounted) return;
      if (nova == null) {
        AureaSnack.show(context, 'Não consegui recalcular agora.');
        return;
      }
      await _guardar(nova);
      if (!mounted) return;
      AureaSnack.show(
        context,
        'Recalculado: ${nova.erroPixels.toStringAsFixed(2)} px de erro.',
      );
    } on RastreioException catch (e) {
      if (mounted) {
        AureaSnack.show(
          context,
          e.mensagem,
          duration: const Duration(seconds: 7),
        );
      }
    } finally {
      if (mounted) setState(() => _resolvendo = false);
    }
  }

  /// Chao, origem e escala mexem no MUNDO inteiro. Objetos ja montados
  /// na cena ficam onde estavam (em coordenadas velhas) — o aviso existe
  /// para a pessoa preferir ajustar o mundo ANTES de montar.
  String _avisoDeCenaExistente() =>
      _cenaDoClipe(criarSePreciso: false) == null
      ? ''
      : ' Objetos já montados na cena não acompanham — confira a posição deles.';

  /// A CENA 3D onde as coisas entram. Cria se ainda não existe: a pessoa
  /// pediu para pôr um texto no chão, e não para administrar camadas.
  String? _cenaDoClipe({bool criarSePreciso = true}) {
    final projeto = ref.read(editorControllerProvider);
    for (final l in projeto.layers) {
      if (l is Scene3DLayer &&
          l.name.contains(widget.layerId.substring(0, 4))) {
        return l.id;
      }
    }
    for (final l in projeto.layers) {
      if (l is Scene3DLayer && l.name.startsWith('Rastreio 3D')) return l.id;
    }
    if (!criarSePreciso) return null;
    return _c.criarCenaDoRastreio(widget.layerId, _s);
  }

  Future<void> _criarCamera() async {
    final id = _c.criarCenaDoRastreio(widget.layerId, _s);
    if (!mounted) return;
    if (id == null) {
      AureaSnack.show(context, 'Não consegui criar a cena.');
      return;
    }
    Navigator.of(context).maybePop();
    AureaSnack.show(
      context,
      'Câmera 3D criada em cima do vídeo.',
      actionLabel: 'Desfazer',
      onAction: _c.undo,
      duration: const Duration(seconds: 5),
    );
  }

  Future<void> _adicionar(ObjetoNoPlano tipo) async {
    final plano = _plano;
    if (plano == null) return;
    final cena = _cenaDoClipe();
    if (cena == null) {
      AureaSnack.show(context, 'Não consegui criar a cena 3D.');
      return;
    }
    String? textura;
    if (tipo == ObjetoNoPlano.texto) {
      // O TEXTO É UMA CAMADA DE TEXTO usada como textura da placa. Assim
      // ele continua editável com as ferramentas de texto que já
      // existem — fonte, cor, animação — em vez de virar uma malha que
      // só esta tela sabe mexer.
      final antes = {
        for (final l in ref.read(editorControllerProvider).layers) l.id,
      };
      _c.addTextLayer(Duration.zero, text: 'Seu texto');
      for (final l in ref.read(editorControllerProvider).layers) {
        if (!antes.contains(l.id) && l is TextLayer) textura = l.id;
      }
    }
    _c.updateScene3D(
      cena,
      (c) => c.copyWith(
        nodes: [
          ...c.nodes,
          noNoPlano(plano, tipo, textureLayerId: textura),
        ],
      ),
    );
    if (!mounted) return;
    Navigator.of(context).maybePop();
    AureaSnack.show(
      context,
      '${tipo.emPalavras} na superfície (${plano.tipo.emPalavras.toLowerCase()}).',
      actionLabel: 'Desfazer',
      onAction: _c.undo,
      duration: const Duration(seconds: 5),
    );
  }

  /// UMA ANCORA (nulo da cena) no ponto escolhido — o lugar do mundo em
  /// que se pendura texto, modelo, o que vier. Nao guarda o id do ponto:
  /// guarda a POSICAO, entao apagar pontos depois nao a derruba.
  Future<void> _ancoraNoPonto() async {
    final id = _escolhidos.single;
    final cena = _cenaDoClipe();
    if (cena == null) {
      AureaSnack.show(context, 'Não consegui criar a cena 3D.');
      return;
    }
    _c.criarNoDoPonto(cena, _s, id);
    if (!mounted) return;
    AureaSnack.show(
      context,
      'Âncora criada no ponto. Pendure camadas nela no Estúdio 3D.',
      actionLabel: 'Desfazer',
      onAction: _c.undo,
      duration: const Duration(seconds: 5),
    );
  }

  Future<void> _definirOrigemAqui() async {
    final ponto = _s.nuvem[_escolhidos.single];
    if (ponto == null) return;
    await _guardar(definirOrigem(_s, ponto));
    if (!mounted) return;
    AureaSnack.show(
      context,
      'Origem definida: esse ponto agora é o (0, 0, 0).'
      '${_avisoDeCenaExistente()}',
      duration: const Duration(seconds: 5),
    );
  }

  /// DEFINIR ESCALA: dois pontos + a distancia real entre eles. Depois
  /// disso, 100 unidades do mundo = 1 metro, e os numeros dos paineis
  /// passam a ter tamanho de verdade.
  Future<void> _definirEscalaReal() async {
    final ids = _escolhidos.toList();
    var unidade = UnidadeReal.m;
    final campo = TextEditingController(text: '1.0');
    final valor = await showCupertinoDialog<double>(
      context: context,
      builder: (dialogContext) => StatefulBuilder(
        builder: (dialogContext, muda) => CupertinoAlertDialog(
          title: const AppText('Distância real'),
          content: Column(
            children: [
              const SizedBox(height: 6),
              const AppText(
                'Quanto mede, no mundo real, a distância entre os dois '
                'pontos escolhidos?',
                style: TextStyle(fontSize: 12),
              ),
              const SizedBox(height: 10),
              CupertinoTextField(
                controller: campo,
                autofocus: true,
                keyboardType: const TextInputType.numberWithOptions(
                  decimal: true,
                ),
              ),
              const SizedBox(height: 8),
              Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  for (final u in UnidadeReal.values)
                    GestureDetector(
                      key: ValueKey('rastreio3d-unidade-${u.name}'),
                      onTap: () => muda(() => unidade = u),
                      child: Container(
                        margin: const EdgeInsets.symmetric(horizontal: 3),
                        padding: const EdgeInsets.symmetric(
                          horizontal: 8,
                          vertical: 5,
                        ),
                        decoration: BoxDecoration(
                          color: u == unidade
                              ? AmColors.accentDim
                              : AmColors.chip,
                          borderRadius: BorderRadius.circular(8),
                        ),
                        child: AppText(
                          u.emPalavras,
                          style: TextStyle(
                            fontSize: 11,
                            color: u == unidade
                                ? AmColors.accent
                                : AmColors.text,
                          ),
                        ),
                      ),
                    ),
                ],
              ),
            ],
          ),
          actions: [
            CupertinoDialogAction(
              onPressed: () => Navigator.of(dialogContext).pop(),
              child: const AppText('Cancelar'),
            ),
            CupertinoDialogAction(
              isDefaultAction: true,
              onPressed: () {
                final v = double.tryParse(campo.text.replaceAll(',', '.'));
                Navigator.of(
                  dialogContext,
                ).pop(v == null ? null : v * unidade.metros);
              },
              child: const AppText('Aplicar'),
            ),
          ],
        ),
      ),
    );
    if (valor == null || !mounted) return;
    final fator = fatorDeEscalaReal(_s, ids[0], ids[1], valor);
    if (fator == null) {
      AureaSnack.show(context, 'Esses dois pontos estão juntos demais.');
      return;
    }
    await _guardar(escalarMundo(_s, fator));
    if (!mounted) return;
    AureaSnack.show(
      context,
      'Escala definida: 100 unidades = 1 metro.${_avisoDeCenaExistente()}',
      duration: const Duration(seconds: 5),
    );
  }

  /// MODELO 3D NA SUPERFICIE: cria a ancora no plano, abre o importador
  /// de sempre e pendura o modelo na ancora — ele nasce deitado na
  /// superficie e preso ao lugar.
  Future<void> _importarModeloNoPlano(PlanoDoRastreio plano) async {
    final cena = _cenaDoClipe();
    if (cena == null) {
      AureaSnack.show(context, 'Não consegui criar a cena 3D.');
      return;
    }
    _c.updateScene3D(
      cena,
      (c) => c.copyWith(
        nodes: [
          ...c.nodes,
          noNoPlano(plano, ObjetoNoPlano.nulo, nome: 'Âncora · superfície'),
        ],
      ),
    );
    final projeto = ref.read(editorControllerProvider);
    final camada = projeto.layerById(cena);
    if (camada is! Scene3DLayer || camada.scene.nodes.isEmpty) return;
    final ancora = camada.scene.nodes.last.id;
    try {
      final caminhos = await ref.read(escolherModeloProvider)();
      if (!mounted || caminhos.isEmpty) return;
      final modelo = await readModel3DFiles(caminhos);
      if (!mounted) return;
      final noId = _c.addModel3D(cena, modelo);
      if (noId.isEmpty) return;
      _c.updateScene3D(
        cena,
        (c) => c.copyWith(
          nodes: [
            for (final n in c.nodes)
              if (n.id == noId) n.copyWith(parentId: ancora) else n,
          ],
        ),
      );
      if (!mounted) return;
      Navigator.of(context).maybePop();
      AureaSnack.show(
        context,
        '${modelo.name} preso à superfície.',
        actionLabel: 'Desfazer',
        onAction: _c.undo,
        duration: const Duration(seconds: 5),
      );
    } catch (e) {
      if (mounted) {
        AureaSnack.show(context, 'Não deu para importar o modelo: $e');
      }
    }
  }

  // ------------------------------------------------------------ tela

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AmColors.bg,
      body: SafeArea(
        child: Column(
          children: [
            _Cabecalho(
              onVoltar: () => Navigator.of(context).maybePop(),
              onAvancado: _abrirAvancado,
            ),
            Expanded(
              child: ListView(
                padding: const EdgeInsets.fromLTRB(14, 0, 14, 20),
                children: [
                  _palco(),
                  if (_quadros.length > 1) _regua(),
                  const SizedBox(height: 12),
                  _FichaDoSolve(solucao: _s),
                  const SizedBox(height: 12),
                  _acoes(),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _palco() {
    final proporcao = _s.altura <= 0 ? 16 / 9 : _s.largura / _s.altura;
    return LayoutBuilder(
      builder: (context, c) {
        final largura = c.maxWidth;
        final palco = Size(largura, largura / proporcao);
        return SizedBox(
          width: palco.width,
          height: palco.height,
          child: ClipRRect(
            borderRadius: BorderRadius.circular(12),
            child: GestureDetector(
              key: const ValueKey('rastreio3d-palco'),
              behavior: HitTestBehavior.opaque,
              onTapUp: (d) => _tocar(d.localPosition, palco),
              onPanStart: (d) => setState(() {
                _inicioDoLaco = d.localPosition;
                _laco = null;
              }),
              onPanUpdate: (d) {
                final i = _inicioDoLaco;
                if (i == null) return;
                setState(() => _laco = Rect.fromPoints(i, d.localPosition));
              },
              onPanEnd: (_) => _fecharLaco(palco),
              child: Stack(
                fit: StackFit.expand,
                children: [
                  if (_indice < _quadros.length)
                    Image.file(
                      _quadros[_indice],
                      fit: BoxFit.cover,
                      gaplessPlayback: true,
                      errorBuilder: (_, _, _) =>
                          ColoredBox(color: AmColors.panel),
                    )
                  else
                    ColoredBox(color: AmColors.panel),
                  CustomPaint(
                    painter: _PintorDosPontos(
                      pontos: _projetados(),
                      escala: palco.width / _s.largura,
                      escolhidos: _escolhidos,
                      qualidade: _s.qualidadeDoPonto,
                      laco: _laco,
                      plano: _plano,
                      pose: _poseMaisPerto(_quadroDoSolve),
                      focalPx: _s.focalPx,
                      centro: Offset(_s.largura / 2, _s.altura / 2),
                    ),
                  ),
                  if (_quadros.isEmpty)
                    Center(
                      child: AppText('Sem prévia do vídeo — os pontos continuam valendo.',
                        textAlign: TextAlign.center,
                        style: TextStyle(fontSize: 12, color: AmColors.muted),
                      ),
                    ),
                ],
              ),
            ),
          ),
        );
      },
    );
  }

  /// A RÉGUA DOS QUADROS — arrasta para andar no tempo.
  ///
  /// Superfície de arrasto, e não barrinha com bolinha: no editor
  /// inteiro é assim, e a bolinha de um slider num celular tem a
  /// largura de meio dedo — a pessoa acerta a barra e o valor pula para
  /// onde ela tocou, em vez de andar de onde estava.
  Widget _regua() => Padding(
    padding: const EdgeInsets.only(top: 8),
    child: Row(
      children: [
        AppText(
          '${_indice + 1}/${_quadros.length}',
          style: TextStyle(fontSize: 11, color: AmColors.muted),
        ),
        const SizedBox(width: 10),
        Expanded(
          child: AmTickRuler(
            key: const ValueKey('rastreio3d-quadro'),
            value: _indice.toDouble(),
            min: 0,
            max: (_quadros.length - 1).toDouble(),
            height: 40,
            // Um quadro a cada doze pixels de dedo: a mão anda o clipe
            // inteiro numa tela, e ainda dá para parar num quadro.
            unitsPerPixel: 1 / 12,
            onChanged: (v) {
              final i = v.round().clamp(0, _quadros.length - 1);
              if (i != _indice) setState(() => _indice = i);
            },
          ),
        ),
      ],
    ),
  );

  Widget _acoes() {
    final plano = _plano;
    if (_resolvendo) {
      return const _Faixa(
        chave: 'rastreio3d-resolvendo',
        texto: 'Reconstruindo o movimento da câmera...',
      );
    }
    if (plano != null && plano.ehSuperficie) {
      return Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _Faixa(
            chave: 'rastreio3d-plano',
            texto:
                '${plano.tipo.emPalavras} com ${plano.ids.length} pontos. '
                'Toque para pôr algo aqui.',
            destaque: true,
          ),
          const SizedBox(height: 10),
          for (final t in ObjetoNoPlano.values)
            Padding(
              padding: const EdgeInsets.only(bottom: 8),
              child: _Acao(
                chave: 'rastreio3d-add-${t.name}',
                icone: switch (t) {
                  ObjetoNoPlano.nulo => CupertinoIcons.plus_app,
                  ObjetoNoPlano.solido => CupertinoIcons.square_fill,
                  ObjetoNoPlano.forma => CupertinoIcons.cube,
                  ObjetoNoPlano.texto => CupertinoIcons.textformat,
                },
                titulo: t.emPalavras,
                detalhe: t.explicacao,
                onTap: () => _adicionar(t),
              ),
            ),
          _Acao(
            chave: 'rastreio3d-add-modelo',
            icone: CupertinoIcons.arrow_down_doc,
            titulo: 'Modelo 3D (importar)',
            detalhe:
                'Escolhe um arquivo de modelo e prende ele nesta '
                'superfície, por uma âncora.',
            onTap: () => _importarModeloNoPlano(plano),
          ),
          const SizedBox(height: 4),
          _Acao(
            chave: 'rastreio3d-definir-chao',
            icone: CupertinoIcons.square_grid_3x2,
            titulo: 'Definir como chão',
            detalhe: 'Essa superfície vira o y = 0 do mundo 3D.',
            onTap: _definirChao,
          ),
          const SizedBox(height: 8),
          _Acao(
            chave: 'rastreio3d-apagar-escolhidos',
            icone: CupertinoIcons.trash,
            titulo: 'Apagar ${_escolhidos.length} ponto(s)',
            detalhe: 'Tira da conta os pontos escolhidos.',
            onTap: _apagarEscolhidos,
          ),
        ],
      );
    }
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        if (_escolhidos.isNotEmpty)
          Padding(
            padding: const EdgeInsets.only(bottom: 10),
            child: _Faixa(
              chave: 'rastreio3d-poucos',
              texto: _escolhidos.length < 3
                  ? 'Escolhidos ${_escolhidos.length}. Com três ou mais eu '
                        'acho a superfície.'
                  : 'Esses pontos não formam uma superfície — estão '
                        'espalhados em profundidade.',
            ),
          ),
        if (_escolhidos.length == 1) ...[
          _Acao(
            chave: 'rastreio3d-ancora-ponto',
            icone: CupertinoIcons.pin,
            titulo: 'Âncora neste ponto',
            detalhe: 'Um nulo da cena preso a este lugar do mundo.',
            destaque: true,
            onTap: _ancoraNoPonto,
          ),
          const SizedBox(height: 8),
          _Acao(
            chave: 'rastreio3d-origem',
            icone: CupertinoIcons.smallcircle_circle,
            titulo: 'Definir origem aqui',
            detalhe: 'Este ponto vira o (0, 0, 0) do mundo 3D.',
            onTap: _definirOrigemAqui,
          ),
          const SizedBox(height: 8),
        ],
        if (_escolhidos.length == 2) ...[
          _Acao(
            chave: 'rastreio3d-escala',
            icone: CupertinoIcons.arrow_left_right,
            titulo: 'Definir distância real',
            detalhe:
                'Diga quanto mede a distância entre os dois pontos '
                'escolhidos, e o mundo ganha escala de verdade.',
            destaque: true,
            onTap: _definirEscalaReal,
          ),
          const SizedBox(height: 8),
        ],
        _Acao(
          chave: 'rastreio3d-criar-camera',
          icone: CupertinoIcons.videocam,
          titulo: 'Criar a câmera 3D',
          detalhe: 'Põe uma cena 3D em cima do vídeo, com a câmera rastreada.',
          destaque: true,
          onTap: _criarCamera,
        ),
        const SizedBox(height: 8),
        _Acao(
          chave: 'rastreio3d-chao-auto',
          icone: CupertinoIcons.square_grid_3x2,
          titulo: 'Achar o chão sozinho',
          detalhe: 'Procura a maior superfície plana da nuvem.',
          onTap: () => _definirChao(automatico: true),
        ),
        const SizedBox(height: 8),
        _Acao(
          chave: 'rastreio3d-apagar-ruins',
          icone: CupertinoIcons.wand_stars,
          titulo: 'Apagar os pontos ruins',
          detalhe:
              '${_s.pontosDaQualidade({QualidadeDoPonto.ruim}).length} '
              'ponto(s) com erro alto.',
          onTap: _apagarOsRuins,
        ),
      ],
    );
  }

  void _abrirAvancado() {
    showModalBottomSheet<void>(
      context: context,
      isScrollControlled: true,
      backgroundColor: Colors.transparent,
      builder: (_) => _FolhaAvancada(
        solucao: _s,
        modo: _modo,
        podeRecalcular: _t.podeResolverDeNovo(widget.layerId),
        onModo: (m) {
          setState(() => _modo = m);
          Navigator.of(context).maybePop();
        },
        onRecalcular: () {
          Navigator.of(context).maybePop();
          _resolverDeNovo();
        },
      ),
    );
  }
}

// ---------------------------------------------------------------- peças

class _Cabecalho extends StatelessWidget {
  const _Cabecalho({required this.onVoltar, required this.onAvancado});

  final VoidCallback onVoltar;
  final VoidCallback onAvancado;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(6, 4, 14, 6),
    child: Row(
      children: [
        // VOLTAR GRANDE E SÓ O SÍMBOLO — o mesmo tamanho de alvo do
        // resto do editor. O beta reclamou de um voltar que ninguém
        // acertava, e a régua é a mesma em toda tela.
        GestureDetector(
          key: const ValueKey('rastreio3d-voltar'),
          behavior: HitTestBehavior.opaque,
          onTap: onVoltar,
          child: const SizedBox(
            width: 52,
            height: 52,
            child: Icon(Icons.chevron_left, size: 30, color: Colors.white),
          ),
        ),
        Expanded(
          child: AppText('Rastreio de câmera 3D',
            style: TextStyle(
              fontSize: 15,
              fontWeight: FontWeight.w700,
              color: AmColors.text,
            ),
          ),
        ),
        GestureDetector(
          key: const ValueKey('rastreio3d-avancado'),
          behavior: HitTestBehavior.opaque,
          onTap: onAvancado,
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 14),
            child: AppText(
              'Avançado',
              style: TextStyle(fontSize: 13, color: AmColors.accent),
            ),
          ),
        ),
      ],
    ),
  );
}

/// A FICHA DO SOLVE — o que dá para confiar, em números.
class _FichaDoSolve extends StatelessWidget {
  const _FichaDoSolve({required this.solucao});
  final SolucaoCamera3D solucao;

  @override
  Widget build(BuildContext context) {
    final e = solucao.estrelas;
    return Container(
      key: const ValueKey('rastreio3d-ficha'),
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: AmColors.panel,
        borderRadius: BorderRadius.circular(12),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              AppText('Qualidade do rastreio',
                style: TextStyle(
                  fontSize: 13,
                  fontWeight: FontWeight.w700,
                  color: AmColors.text,
                ),
              ),
              const Spacer(),
              for (var i = 0; i < 5; i++)
                Icon(
                  i < e ? CupertinoIcons.star_fill : CupertinoIcons.star,
                  size: 13,
                  color: i < e ? AmColors.accent : AmColors.muted,
                ),
            ],
          ),
          const SizedBox(height: 10),
          Wrap(
            spacing: 18,
            runSpacing: 8,
            children: [
              _Numero(
                'Erro de reprojeção',
                '${solucao.erroPixels.toStringAsFixed(2)} px',
              ),
              _Numero('Pontos 3D', '${solucao.nuvem.length}'),
              _Numero('Pontos seguidos', '${solucao.pontosSeguidos}'),
              _Numero('Bons', '${solucao.pontosBons}'),
              _Numero('Quadros', '${solucao.poses.length}'),
              _Numero(
                'Lente',
                '${(36 * solucao.focalPx / solucao.largura).round()} mm',
              ),
            ],
          ),
        ],
      ),
    );
  }
}

class _Numero extends StatelessWidget {
  const _Numero(this.rotulo, this.valor);
  final String rotulo;
  final String valor;

  @override
  Widget build(BuildContext context) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    mainAxisSize: MainAxisSize.min,
    children: [
      AppText(rotulo, style: TextStyle(fontSize: 10.5, color: AmColors.muted)),
      AppText(
        valor,
        style: TextStyle(
          fontSize: 14,
          fontWeight: FontWeight.w700,
          color: AmColors.text,
        ),
      ),
    ],
  );
}

class _Acao extends StatelessWidget {
  const _Acao({
    required this.chave,
    required this.icone,
    required this.titulo,
    required this.detalhe,
    required this.onTap,
    this.destaque = false,
  });

  final String chave;
  final IconData icone;
  final String titulo;
  final String detalhe;
  final VoidCallback onTap;
  final bool destaque;

  @override
  Widget build(BuildContext context) => GestureDetector(
    key: ValueKey(chave),
    behavior: HitTestBehavior.opaque,
    onTap: onTap,
    child: Container(
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
      decoration: BoxDecoration(
        color: destaque
            ? AmColors.accent.withValues(alpha: .16)
            : AmColors.panel,
        borderRadius: BorderRadius.circular(12),
      ),
      child: Row(
        children: [
          Icon(
            icone,
            size: 19,
            color: destaque ? AmColors.accent : AmColors.muted,
          ),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                AppText(titulo,
                  style: TextStyle(
                    fontSize: 13.5,
                    fontWeight: FontWeight.w700,
                    color: destaque ? AmColors.accent : AmColors.text,
                  ),
                ),
                const SizedBox(height: 2),
                AppText(
                  detalhe,
                  style: TextStyle(fontSize: 11.5, color: AmColors.muted),
                ),
              ],
            ),
          ),
        ],
      ),
    ),
  );
}

class _Faixa extends StatelessWidget {
  const _Faixa({
    required this.chave,
    required this.texto,
    this.destaque = false,
  });

  final String chave;
  final String texto;
  final bool destaque;

  @override
  Widget build(BuildContext context) => Container(
    key: ValueKey(chave),
    padding: const EdgeInsets.all(11),
    decoration: BoxDecoration(
      color: destaque ? AmColors.accent.withValues(alpha: .16) : AmColors.panel,
      borderRadius: BorderRadius.circular(10),
    ),
    child: AppText(texto,
      style: TextStyle(
        fontSize: 12,
        height: 1.35,
        color: destaque ? AmColors.accent : AmColors.muted,
      ),
    ),
  );
}

/// OS PONTOS EM CIMA DO VÍDEO.
///
/// A cor diz a qualidade e o tamanho diz a escolha. Não é enfeite: é
/// como a pessoa descobre, sem ler número nenhum, que a metade esquerda
/// do quadro rastreou bem e a direita não.
class _PintorDosPontos extends CustomPainter {
  _PintorDosPontos({
    required this.pontos,
    required this.escala,
    required this.escolhidos,
    required this.qualidade,
    required this.laco,
    required this.plano,
    required this.pose,
    required this.focalPx,
    required this.centro,
  });

  final Map<int, Offset> pontos;
  final double escala;
  final Set<int> escolhidos;
  final QualidadeDoPonto Function(int) qualidade;
  final Rect? laco;
  final PlanoDoRastreio? plano;
  final PoseCamera? pose;
  final double focalPx;
  final Offset centro;

  static const _cores = {
    QualidadeDoPonto.excelente: Color(0xFF6FE3B0),
    QualidadeDoPonto.bom: Color(0xFF8FD3FF),
    QualidadeDoPonto.fraco: Color(0xFFFFC978),
    QualidadeDoPonto.ruim: Color(0xFFFF7A7A),
  };

  /// Um ponto do mundo na tela, ou null quando fica atrás da câmera.
  Offset? _naTela(List<double> x) {
    final p = pose;
    if (p == null) return null;
    final v = projetar(p.rotacao, p.translacao, x);
    if (v == null) return null;
    return Offset(centro.dx + v[0] * focalPx, centro.dy + v[1] * focalPx) *
        escala;
  }

  @override
  void paint(Canvas canvas, Size size) {
    final alvo = plano;
    if (alvo != null && alvo.ehSuperficie) {
      // O ALVO é desenhado no espaço do plano e projetado, e não como um
      // quadrado na tela: um retângulo plano na tela não conta em que
      // ângulo a superfície está, e é justamente isso que a pessoa
      // precisa ver antes de pôr o texto ali.
      final t = alvo.tamanho;
      final quinas = <Offset>[];
      for (final (a, b) in const [(-1, -1), (1, -1), (1, 1), (-1, 1)]) {
        final p = _naTela([
          alvo.origem[0] + alvo.eixoX[0] * a * t + alvo.eixoZ[0] * b * t,
          alvo.origem[1] + alvo.eixoX[1] * a * t + alvo.eixoZ[1] * b * t,
          alvo.origem[2] + alvo.eixoX[2] * a * t + alvo.eixoZ[2] * b * t,
        ]);
        if (p != null) quinas.add(p);
      }
      if (quinas.length == 4) {
        final caminho = Path()..addPolygon(quinas, true);
        canvas
          ..drawPath(
            caminho,
            Paint()..color = const Color(0xFF7C62FF).withValues(alpha: .22),
          )
          ..drawPath(
            caminho,
            Paint()
              ..style = PaintingStyle.stroke
              ..strokeWidth = 2
              ..color = const Color(0xFFB6A6FF),
          );
      }
      final centroNaTela = _naTela(alvo.origem);
      if (centroNaTela != null) {
        canvas.drawCircle(
          centroNaTela,
          5,
          Paint()..color = const Color(0xFFB6A6FF),
        );
      }
    }

    for (final e in pontos.entries) {
      final p = e.value * escala;
      final escolhido = escolhidos.contains(e.key);
      final cor = _cores[qualidade(e.key)] ?? const Color(0xFF8FD3FF);
      if (escolhido) {
        canvas
          ..drawCircle(
            p,
            7,
            Paint()..color = Colors.white.withValues(alpha: .9),
          )
          ..drawCircle(p, 4, Paint()..color = cor);
      } else {
        // O X é o desenho do After Effects, e é melhor do que um ponto
        // cheio: sobre um vídeo claro, um ponto some; um X continua
        // legível porque tem borda em duas direções.
        final t = Paint()
          ..color = cor
          ..strokeWidth = 1.6
          ..strokeCap = StrokeCap.round;
        canvas
          ..drawLine(
            p + const Offset(-3.5, -3.5),
            p + const Offset(3.5, 3.5),
            t,
          )
          ..drawLine(
            p + const Offset(3.5, -3.5),
            p + const Offset(-3.5, 3.5),
            t,
          );
      }
    }

    final r = laco;
    if (r != null) {
      canvas
        ..drawRect(r, Paint()..color = Colors.white.withValues(alpha: .10))
        ..drawRect(
          r,
          Paint()
            ..style = PaintingStyle.stroke
            ..strokeWidth = 1.2
            ..color = Colors.white.withValues(alpha: .7),
        );
    }
  }

  @override
  bool shouldRepaint(_PintorDosPontos old) =>
      old.pontos != pontos ||
      old.escolhidos.length != escolhidos.length ||
      old.laco != laco ||
      old.plano != plano;
}

/// O MODO AVANÇADO: o que estava escondido, para quem quer mexer.
class _FolhaAvancada extends StatelessWidget {
  const _FolhaAvancada({
    required this.solucao,
    required this.modo,
    required this.podeRecalcular,
    required this.onModo,
    required this.onRecalcular,
  });

  final SolucaoCamera3D solucao;
  final ModoDoSolve modo;
  final bool podeRecalcular;
  final ValueChanged<ModoDoSolve> onModo;
  final VoidCallback onRecalcular;

  @override
  Widget build(BuildContext context) => Container(
    decoration: BoxDecoration(
      color: AmColors.bg,
      borderRadius: const BorderRadius.vertical(top: Radius.circular(18)),
    ),
    padding: const EdgeInsets.fromLTRB(18, 16, 18, 20),
    child: SafeArea(
      top: false,
      child: SingleChildScrollView(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            AppText(
              'Avançado',
              style: TextStyle(
                fontSize: 16,
                fontWeight: FontWeight.w700,
                color: AmColors.text,
              ),
            ),
            const SizedBox(height: 3),
            AppText('O que a análise fez, e o que dá para mudar antes de '
              'refazê-la.',
              style: TextStyle(fontSize: 12, color: AmColors.muted),
            ),
            const SizedBox(height: 14),
            for (final m in ModoDoSolve.values)
              Padding(
                padding: const EdgeInsets.only(bottom: 8),
                child: _Acao(
                  chave: 'rastreio3d-modo-${m.name}',
                  icone: m == modo
                      ? CupertinoIcons.checkmark_circle_fill
                      : CupertinoIcons.circle,
                  titulo: m.emPalavras,
                  detalhe:
                      '${m.explicacao} '
                      '${m.fps} quadros/s, ${m.pontos} pontos.',
                  destaque: m == modo,
                  onTap: () => onModo(m),
                ),
              ),
            const SizedBox(height: 8),
            _Faixa(
              chave: 'rastreio3d-detalhes',
              texto:
                  'Tomada: ${solucao.tipoDeTomada.emPalavras}. '
                  'Lente resolvida: '
                  '${(36 * solucao.focalPx / solucao.largura).toStringAsFixed(1)} mm '
                  '(${solucao.focalPx.round()} px em '
                  '${solucao.largura}×${solucao.altura}). '
                  'Erro médio ${solucao.erroPixels.toStringAsFixed(2)} px em '
                  '${solucao.poses.length} quadros.',
            ),
            const SizedBox(height: 10),
            _Acao(
              chave: 'rastreio3d-recalcular',
              icone: CupertinoIcons.arrow_2_circlepath,
              titulo: 'Resolver de novo',
              detalhe: podeRecalcular
                  ? 'Refaz a conta com o que já foi lido do vídeo.'
                  : 'Só depois de rastrear nesta sessão — o vídeo precisa '
                        'estar lido.',
              destaque: podeRecalcular,
              onTap: onRecalcular,
            ),
          ],
        ),
      ),
    ),
  );
}
