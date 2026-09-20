import 'dart:io';

import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/snack.dart';
import '../../application/camera_track_service.dart';
import '../../application/editor_controller.dart';
import 'package:file_picker/file_picker.dart';

import '../../application/model_import_service.dart';
import '../../application/tracking_service.dart';
import '../../domain/camera_solver3d.dart';
import '../../domain/cena_do_rastreio.dart';
import '../../domain/layer.dart';
import '../../domain/plano_do_rastreio.dart';
import 'am_colors.dart';
import 'layer_menu.dart' show showReasonToast;

/// O ESTUDIO DO RASTREIO — a tela onde a solucao vira CENA.
///
/// A ordem de trabalho e a que evita refazer: ver os pontos sobre o
/// video, arrumar o MUNDO (chao, origem, escala real) e so entao pousar
/// coisas nele. Tudo aqui opera sobre a [SolucaoCamera3D] gravada; as
/// transformacoes de mundo sao de semelhanca e nao movem um pixel da
/// reprojecao — os pontos na tela sao a prova viva disso.
///
/// A selecao e POR TOQUE, e a legenda de qualidade e um seletor: tocar
/// em "Ruim" escolhe todos os pontos ruins de uma vez — apagar os ruins
/// e recalcular vira dois toques.
class EstudioDoRastreio extends ConsumerStatefulWidget {
  const EstudioDoRastreio({
    super.key,
    required this.layerId,
    required this.solucao,
  });

  final String layerId;
  final SolucaoCamera3D solucao;

  @override
  ConsumerState<EstudioDoRastreio> createState() => _EstudioDoRastreioState();
}

class _EstudioDoRastreioState extends ConsumerState<EstudioDoRastreio> {
  late SolucaoCamera3D _s = widget.solucao;
  List<File> _quadros = const [];
  int _indice = 0;
  final Set<int> _escolhidos = {};
  PlanoDoRastreio? _plano;
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

  // ------------------------------------------------------- projecao

  int get _quadroDoSolve {
    if (_quadros.length < 2) return 0;
    final u = _indice / (_quadros.length - 1);
    return (u * (_s.quadros - 1)).round();
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

  /// Onde cada ponto 3D cai na imagem, em pixels do quadro analisado.
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

  // ------------------------------------------------------- escolher

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
    setState(() {
      if (achado == null) {
        // Tocar no vazio limpa a escolha: o gesto que todo mundo tenta
        // primeiro quando escolheu errado.
        _escolhidos.clear();
      } else if (!_escolhidos.remove(achado)) {
        _escolhidos.add(achado);
      }
      _recalcularPlano();
    });
  }

  void _escolherQualidade(QualidadeDoPonto q) {
    final ids = _s.pontosDaQualidade({q});
    setState(() {
      final todosJa = ids.isNotEmpty && _escolhidos.containsAll(ids);
      if (todosJa) {
        _escolhidos.removeAll(ids);
      } else {
        _escolhidos.addAll(ids);
      }
      _recalcularPlano();
    });
  }

  // --------------------------------------------------------- acoes

  Future<void> _guardar(SolucaoCamera3D nova) async {
    setState(() {
      _s = nova;
      _escolhidos.removeWhere((id) => !nova.nuvem.containsKey(id));
      _recalcularPlano();
    });
    await _t.guardar(widget.layerId, nova);
  }

  /// Chao, origem e escala mexem no MUNDO inteiro. Objetos ja montados
  /// ficam onde estavam — o aviso existe para a pessoa preferir ajustar
  /// o mundo ANTES de montar.
  String _avisoDeCenaExistente() => _cenaDoClipe(criarSePreciso: false) == null
      ? ''
      : ' Objetos já montados na cena não acompanham — confira a posição '
            'deles.';

  /// A CENA 3D onde as coisas entram. Cria se ainda nao existe: a pessoa
  /// pediu para por um texto no chao, nao para administrar camadas.
  String? _cenaDoClipe({bool criarSePreciso = true}) {
    final projeto = ref.read(editorControllerProvider);
    for (final l in projeto.layers) {
      if (l is Scene3DLayer && l.name.contains(widget.layerId.substring(0, 4))) {
        return l.id;
      }
    }
    for (final l in projeto.layers) {
      if (l is Scene3DLayer && l.name.startsWith('Cena 3D')) return l.id;
    }
    if (!criarSePreciso) return null;
    return _c.criarCenaDoRastreio(widget.layerId, _s);
  }

  Future<void> _criarCena() async {
    final id = _c.criarCenaDoRastreio(widget.layerId, _s);
    if (!mounted) return;
    if (id == null) {
      AureaSnack.show(context, 'Não consegui criar a cena.');
      return;
    }
    Navigator.of(context).maybePop();
    AureaSnack.show(
      context,
      'Cena 3D criada em cima do vídeo.',
      actionLabel: 'Desfazer',
      onAction: _c.undo,
      duration: const Duration(seconds: 5),
    );
  }

  Future<void> _definirChao({required bool automatico}) async {
    final ids = automatico ? maiorPlano(_s.nuvem)?.ids : _escolhidos.toList();
    if (ids == null || ids.length < 3) {
      showReasonToast(
        context,
        automatico
            ? 'Não achei uma superfície dominante na nuvem.'
            : 'Escolha pelo menos três pontos do chão.',
      );
      return;
    }
    await _guardar(definirChao(_s, ids));
    if (!mounted) return;
    AureaSnack.show(
      context,
      'Chão definido: esses pontos agora são o y = 0.'
      '${_avisoDeCenaExistente()}',
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
  /// disso, 100 unidades do mundo = 1 metro.
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
                      key: ValueKey('estudio-rastreio-unidade-${u.name}'),
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

  /// UMA ANCORA (nulo da cena) no ponto escolhido. Guarda a POSICAO, nao
  /// o id do ponto — apagar pontos depois nao a derruba.
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

  Future<void> _porNaSuperficie() async {
    final plano = _plano;
    if (plano == null) return;
    final tipo = await showCupertinoModalPopup<ObjetoNoPlano>(
      context: context,
      builder: (popupContext) => CupertinoActionSheet(
        title: AppText(
          'Pôr na superfície (${plano.tipo.emPalavras.toLowerCase()})',
        ),
        actions: [
          for (final t in ObjetoNoPlano.values)
            CupertinoActionSheetAction(
              key: ValueKey('estudio-rastreio-por-${t.name}'),
              onPressed: () => Navigator.of(popupContext).pop(t),
              child: AppText(t.emPalavras),
            ),
        ],
        cancelButton: CupertinoActionSheetAction(
          onPressed: () => Navigator.of(popupContext).pop(),
          child: const AppText('Cancelar'),
        ),
      ),
    );
    if (tipo == null || !mounted) return;
    final cena = _cenaDoClipe();
    if (cena == null) {
      AureaSnack.show(context, 'Não consegui criar a cena 3D.');
      return;
    }
    String? textura;
    if (tipo == ObjetoNoPlano.texto) {
      // O TEXTO E UMA CAMADA DE TEXTO usada como textura da placa: ele
      // continua editavel com as ferramentas de texto de sempre.
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
        nodes: [...c.nodes, noNoPlano(plano, tipo, textureLayerId: textura)],
      ),
    );
    if (!mounted) return;
    AureaSnack.show(
      context,
      '${tipo.emPalavras} na superfície.',
      actionLabel: 'Desfazer',
      onAction: _c.undo,
      duration: const Duration(seconds: 5),
    );
  }

  /// MODELO 3D NA SUPERFICIE: ancora no plano + importador de sempre, e
  /// o modelo nasce preso ao lugar.
  Future<void> _modeloNaSuperficie() async {
    final plano = _plano;
    if (plano == null) return;
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

  Future<void> _apagarEscolhidos() async {
    await _guardar(semPontos(_s, {..._escolhidos}));
  }

  Future<void> _apagarRuins() async {
    final ruins = _s.pontosDaQualidade({
      QualidadeDoPonto.ruim,
      QualidadeDoPonto.fraco,
    });
    if (ruins.isEmpty) {
      showReasonToast(context, 'Não há pontos ruins para apagar.');
      return;
    }
    await _guardar(semPontos(_s, ruins.toSet()));
  }

  /// RESOLVE DE NOVO com os rastros guardados — quase imediato. Sem
  /// rastros (o app reabriu), manda rastrear pela porta.
  Future<void> _resolverDeNovo() async {
    if (!_t.podeResolverDeNovo(widget.layerId)) {
      showReasonToast(
        context,
        'Os rastros dessa análise não estão mais na memória. '
        'Rastreie de novo pela folha Cena 3D.',
      );
      return;
    }
    setState(() => _resolvendo = true);
    try {
      final apagados = <int>{};
      final nova = await _t.resolverDeNovo(
        widget.layerId,
        pontosApagados: apagados,
        tipoDeTomada: _s.tipoDeTomada,
      );
      if (nova != null) await _guardar(nova);
    } on RastreioException catch (e) {
      if (mounted) AureaSnack.show(context, e.mensagem);
    } finally {
      if (mounted) setState(() => _resolvendo = false);
    }
  }

  // ---------------------------------------------------------- build

  @override
  Widget build(BuildContext context) {
    final proj = _projetados();
    final selecionados = _escolhidos.length;
    final tempo = _s.analiseMs == null
        ? null
        : (_s.analiseMs! / 1000).toStringAsFixed(_s.analiseMs! >= 10000 ? 0 : 1);
    return Scaffold(
      backgroundColor: AmColors.bg,
      body: SafeArea(
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // ------------------------------------------------ cabecalho
            Padding(
              padding: const EdgeInsets.fromLTRB(16, 8, 8, 4),
              child: Row(
                children: [
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        const AppText(
                          'Estúdio do rastreio',
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis,
                          style: TextStyle(
                            fontSize: 17,
                            fontWeight: FontWeight.w700,
                            color: AmColors.text,
                          ),
                        ),
                        AppText(
                          '${'★' * _s.estrelas}${'☆' * (5 - _s.estrelas)} · '
                          '${_s.erroPixels.toStringAsFixed(1)} px · '
                          '${_s.nuvem.length} pontos'
                          '${_s.motor == null ? ' · análise antiga' : ''}'
                          '${tempo == null ? '' : ' · $tempo s'}',
                          key: const ValueKey('estudio-rastreio-ficha'),
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis,
                          style: TextStyle(
                            fontSize: 11,
                            color: _s.motor == null
                                ? AmColors.pink
                                : AmColors.muted,
                          ),
                        ),
                      ],
                    ),
                  ),
                  GestureDetector(
                    key: const ValueKey('estudio-rastreio-fechar'),
                    onTap: () => Navigator.of(context).maybePop(),
                    child: const Padding(
                      padding: EdgeInsets.all(10),
                      child: Icon(
                        CupertinoIcons.xmark,
                        size: 20,
                        color: AmColors.text,
                      ),
                    ),
                  ),
                ],
              ),
            ),

            // ---------------------------------------------------- palco
            Expanded(
              child: Center(
                child: AspectRatio(
                  aspectRatio: _s.largura / _s.altura,
                  child: LayoutBuilder(
                    builder: (context, box) {
                      final palco = Size(box.maxWidth, box.maxHeight);
                      return GestureDetector(
                        key: const ValueKey('estudio-rastreio-palco'),
                        behavior: HitTestBehavior.opaque,
                        onTapUp: (d) => _tocar(d.localPosition, palco),
                        child: ClipRRect(
                          borderRadius: BorderRadius.circular(10),
                          child: Stack(
                            fit: StackFit.expand,
                            children: [
                              if (_indice < _quadros.length)
                                Image.file(
                                  _quadros[_indice],
                                  fit: BoxFit.cover,
                                  gaplessPlayback: true,
                                )
                              else
                                const ColoredBox(color: Color(0xFF101012)),
                              CustomPaint(
                                painter: _PontosPainter(
                                  solucao: _s,
                                  projetados: proj,
                                  escolhidos: _escolhidos,
                                  escala: palco.width / _s.largura,
                                ),
                              ),
                            ],
                          ),
                        ),
                      );
                    },
                  ),
                ),
              ),
            ),

            // ------------------------------------------------- scrubber
            if (_quadros.length > 1)
              Padding(
                padding: const EdgeInsets.symmetric(horizontal: 12),
                child: SizedBox(
                  height: 34,
                  child: CupertinoSlider(
                    key: const ValueKey('estudio-rastreio-scrub'),
                    value: _indice.toDouble(),
                    max: (_quadros.length - 1).toDouble(),
                    onChanged: (v) => setState(() => _indice = v.round()),
                  ),
                ),
              ),

            // ------------------------------------- legenda que seleciona
            Padding(
              padding: const EdgeInsets.fromLTRB(16, 2, 16, 8),
              child: SingleChildScrollView(
                scrollDirection: Axis.horizontal,
                child: Row(
                  children: [
                    for (final q in QualidadeDoPonto.values) ...[
                      _ChipDeQualidade(
                        qualidade: q,
                        quantos: _s.pontosDaQualidade({q}).length,
                        onTap: () => _escolherQualidade(q),
                      ),
                      const SizedBox(width: 6),
                    ],
                    if (selecionados > 0)
                      AppTextMoldado(
                        '{0} na mão', [selecionados],
                        key: const ValueKey('estudio-rastreio-selecao'),
                        style: TextStyle(
                          fontSize: 11,
                          color: AmColors.accent,
                          fontWeight: FontWeight.w600,
                        ),
                      ),
                  ],
                ),
              ),
            ),

            // --------------------------------------------------- acoes
            Padding(
              padding: const EdgeInsets.fromLTRB(16, 0, 16, 12),
              child: SingleChildScrollView(
                scrollDirection: Axis.horizontal,
                child: Row(
                  children: [
                    _Acao(
                      chave: 'estudio-rastreio-criar-cena',
                      icone: CupertinoIcons.videocam,
                      rotulo: 'Criar a cena',
                      destaque: true,
                      onTap: _criarCena,
                    ),
                    _Acao(
                      chave: 'estudio-rastreio-chao-auto',
                      icone: CupertinoIcons.square_grid_3x2,
                      rotulo: 'Chão automático',
                      onTap: () => _definirChao(automatico: true),
                    ),
                    _Acao(
                      chave: 'estudio-rastreio-chao',
                      icone: CupertinoIcons.squares_below_rectangle,
                      rotulo: 'Chão dos escolhidos',
                      onTap: selecionados >= 3
                          ? () => _definirChao(automatico: false)
                          : () => showReasonToast(
                                context,
                                'Escolha pelo menos três pontos do chão.',
                              ),
                    ),
                    _Acao(
                      chave: 'estudio-rastreio-origem',
                      icone: CupertinoIcons.smallcircle_circle,
                      rotulo: 'Origem aqui',
                      onTap: selecionados == 1
                          ? _definirOrigemAqui
                          : () => showReasonToast(
                                context,
                                'Escolha exatamente um ponto para a origem.',
                              ),
                    ),
                    _Acao(
                      chave: 'estudio-rastreio-escala',
                      icone: CupertinoIcons.arrow_left_right,
                      rotulo: 'Escala real',
                      onTap: selecionados == 2
                          ? _definirEscalaReal
                          : () => showReasonToast(
                                context,
                                'Escolha dois pontos com distância conhecida.',
                              ),
                    ),
                    _Acao(
                      chave: 'estudio-rastreio-ancora',
                      icone: CupertinoIcons.pin,
                      rotulo: 'Âncora no ponto',
                      onTap: selecionados == 1
                          ? _ancoraNoPonto
                          : () => showReasonToast(
                                context,
                                'Escolha exatamente um ponto para a âncora.',
                              ),
                    ),
                    _Acao(
                      chave: 'estudio-rastreio-por',
                      icone: CupertinoIcons.cube,
                      rotulo: 'Pôr na superfície',
                      onTap: _plano != null
                          ? _porNaSuperficie
                          : () => showReasonToast(
                                context,
                                'Escolha três ou mais pontos de uma '
                                'superfície primeiro.',
                              ),
                    ),
                    _Acao(
                      chave: 'estudio-rastreio-modelo',
                      icone: CupertinoIcons.cube_box,
                      rotulo: 'Modelo 3D aqui',
                      onTap: _plano != null
                          ? _modeloNaSuperficie
                          : () => showReasonToast(
                                context,
                                'Escolha três ou mais pontos de uma '
                                'superfície primeiro.',
                              ),
                    ),
                    _Acao(
                      chave: 'estudio-rastreio-apagar',
                      icone: CupertinoIcons.delete,
                      rotulo: 'Apagar escolhidos',
                      onTap: selecionados > 0
                          ? _apagarEscolhidos
                          : () => showReasonToast(
                                context,
                                'Toque nos pontos que quer apagar.',
                              ),
                    ),
                    _Acao(
                      chave: 'estudio-rastreio-apagar-ruins',
                      icone: CupertinoIcons.wand_stars,
                      rotulo: 'Apagar os ruins',
                      onTap: _apagarRuins,
                    ),
                    _Acao(
                      chave: 'estudio-rastreio-resolver',
                      icone: CupertinoIcons.arrow_2_circlepath,
                      rotulo: _resolvendo ? 'Resolvendo...' : 'Resolver de novo',
                      onTap: _resolvendo ? () {} : _resolverDeNovo,
                    ),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// A cor de cada qualidade — a MESMA no chip e no ponto, senao a legenda
/// nao legenda nada.
Color corDaQualidade(QualidadeDoPonto q) => switch (q) {
  QualidadeDoPonto.excelente => const Color(0xFF34C759),
  QualidadeDoPonto.bom => const Color(0xFF8BD34A),
  QualidadeDoPonto.fraco => const Color(0xFFFFB340),
  QualidadeDoPonto.ruim => const Color(0xFFFF5A5F),
};

class _ChipDeQualidade extends StatelessWidget {
  const _ChipDeQualidade({
    required this.qualidade,
    required this.quantos,
    required this.onTap,
  });

  final QualidadeDoPonto qualidade;
  final int quantos;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => GestureDetector(
    key: ValueKey('estudio-rastreio-qualidade-${qualidade.name}'),
    onTap: onTap,
    child: Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 7),
      decoration: BoxDecoration(
        color: AmColors.chip,
        borderRadius: BorderRadius.circular(9),
      ),
      child: Row(
        children: [
          Container(
            width: 8,
            height: 8,
            decoration: BoxDecoration(
              color: corDaQualidade(qualidade),
              shape: BoxShape.circle,
            ),
          ),
          const SizedBox(width: 5),
          AppText(
            '${qualidade.emPalavras} · $quantos',
            style: const TextStyle(fontSize: 11, color: AmColors.text),
          ),
        ],
      ),
    ),
  );
}

class _Acao extends StatelessWidget {
  const _Acao({
    required this.chave,
    required this.icone,
    required this.rotulo,
    required this.onTap,
    this.destaque = false,
  });

  final String chave;
  final IconData icone;
  final String rotulo;
  final VoidCallback onTap;
  final bool destaque;

  @override
  Widget build(BuildContext context) => GestureDetector(
    key: ValueKey(chave),
    onTap: onTap,
    child: Container(
      margin: const EdgeInsets.only(right: 8),
      padding: const EdgeInsets.symmetric(horizontal: 12),
      constraints: const BoxConstraints(minHeight: 44),
      decoration: BoxDecoration(
        color: destaque ? AmColors.accentDim : AmColors.chip,
        borderRadius: BorderRadius.circular(12),
      ),
      child: Row(
        children: [
          Icon(
            icone,
            size: 15,
            color: destaque ? AmColors.accent : AmColors.text,
          ),
          const SizedBox(width: 6),
          AppText(
            rotulo,
            style: TextStyle(
              fontSize: 12.5,
              fontWeight: FontWeight.w600,
              color: destaque ? AmColors.accent : AmColors.text,
            ),
          ),
        ],
      ),
    ),
  );
}

/// Os pontos sobre o quadro: cor pela qualidade, anel nos escolhidos.
class _PontosPainter extends CustomPainter {
  _PontosPainter({
    required this.solucao,
    required this.projetados,
    required this.escolhidos,
    required this.escala,
  });

  final SolucaoCamera3D solucao;
  final Map<int, Offset> projetados;
  final Set<int> escolhidos;
  final double escala;

  @override
  void paint(Canvas canvas, Size size) {
    final ponto = Paint()..style = PaintingStyle.fill;
    final anel = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2
      ..color = const Color(0xFFFFFFFF);
    for (final e in projetados.entries) {
      final p = e.value * escala;
      ponto.color = corDaQualidade(solucao.qualidadeDoPonto(e.key));
      canvas.drawCircle(p, 3.2, ponto);
      if (escolhidos.contains(e.key)) canvas.drawCircle(p, 6.5, anel);
    }
  }

  @override
  bool shouldRepaint(_PontosPainter old) =>
      old.projetados != projetados ||
      old.escolhidos.length != escolhidos.length ||
      old.escala != escala ||
      old.solucao != solucao;
}

/// O SELETOR DE ARQUIVO DE MODELO, COMO PORTA TROCAVEL.
///
/// Provider, e nao chamada direta: em teste o seletor do sistema nao
/// existe, e sem uma porta de troca o caminho inteiro de importacao
/// ficaria sem cobertura. Ele morava na folha do estudio 3D, que saiu
/// junto com o motor; o unico consumidor e esta tela, entao veio junto.
final escolherModeloProvider = Provider<Future<List<String>> Function()>(
  (ref) => _escolherModeloDoSistema,
);

Future<List<String>> _escolherModeloDoSistema() async {
  final r = await FilePicker.platform.pickFiles(
    type: FileType.custom,
    allowedExtensions: const [
      'glb', 'gltf', 'obj', 'fbx', 'bin', 'mtl', 'png', 'jpg', 'jpeg',
    ],
    allowMultiple: true,
  );
  return [
    for (final f in r?.files ?? const <PlatformFile>[])
      if (f.path != null) f.path!,
  ];
}
