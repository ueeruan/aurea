import 'dart:math' as math;

import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/curva_de_tempo.dart';
import '../../domain/cut.dart';
import '../../domain/cut_ops.dart';
import '../../domain/keyframe.dart';
import '../../domain/layer.dart';
import '../../domain/time_core.dart';
import '../../domain/video_project.dart';
import 'am_colors.dart';
import 'area_de_arrasto.dart';

/// Modelo de Keyframe local para edição da Curva de Time Remapping
class RemapPoint {
  RemapPoint({
    required this.compositionTime,
    required this.sourceTime,
    this.preservedEase,
    this.interpolation =
        0, // 0=Linear, 1=Hold, 2=Bezier, 3=EaseIn, 4=EaseOut, 5=EaseInOut
    this.inHandle = const Offset(-0.2, 0.0),
    this.outHandle = const Offset(0.2, 0.0),
  });

  Easing? preservedEase;
  double compositionTime; // segundos
  double sourceTime; // segundos
  int interpolation;
  Offset inHandle;
  Offset outHandle;

  RemapPoint copyWith({
    double? compositionTime,
    double? sourceTime,
    int? interpolation,
    Offset? inHandle,
    Offset? outHandle,
  }) => RemapPoint(
    compositionTime: compositionTime ?? this.compositionTime,
    sourceTime: sourceTime ?? this.sourceTime,
    interpolation: interpolation ?? this.interpolation,
    preservedEase: preservedEase,
    inHandle: inHandle ?? this.inHandle,
    outHandle: outHandle ?? this.outHandle,
  );
}

List<RemapPoint> remapPointsFromTrack(AnimatedDouble track) {
  final points = [
    for (final k in track.keyframes)
      RemapPoint(
        compositionTime: k.time.inMicroseconds / 1e6,
        sourceTime: k.value,
        interpolation: k.ease.type == EasingType.hold
            ? 1
            : (k.ease.isLinear ? 0 : 2),
        preservedEase: k.ease,
      ),
  ];
  if (points.isEmpty) {
    return [RemapPoint(compositionTime: 0, sourceTime: track.base)];
  }
  for (var i = 0; i + 1 < points.length; i++) {
    final a = points[i], b = points[i + 1], e = track.keyframes[i].ease;
    final dt = b.compositionTime - a.compositionTime;
    final dy = b.sourceTime - a.sourceTime;
    a.outHandle = Offset(e.x1 * dt, e.y1 * dy);
    b.inHandle = Offset((e.x2 - 1) * dt, (e.y2 - 1) * dy);
  }
  return points;
}

AnimatedDouble remapTrackFromPoints(List<RemapPoint> points) {
  if (points.isEmpty) return AnimatedDouble(0);
  var track = AnimatedDouble(points.first.sourceTime);
  for (var i = 0; i < points.length; i++) {
    final p = points[i];
    Easing ease = p.preservedEase ?? Easing.linear;
    if (p.preservedEase == null && i + 1 < points.length) {
      final n = points[i + 1];
      final dt = n.compositionTime - p.compositionTime;
      final dy = n.sourceTime - p.sourceTime;
      ease = switch (p.interpolation) {
        1 => const Easing(type: EasingType.hold),
        3 => Easing.easeIn,
        4 => Easing.easeOut,
        5 => Easing.easeInOut,
        2 when dt > 0 && dy.abs() > 1e-9 => Easing(
          x1: (p.outHandle.dx / dt).clamp(0, 1),
          y1: p.outHandle.dy / dy,
          x2: (1 + n.inHandle.dx / dt).clamp(0, 1),
          y2: 1 + n.inHandle.dy / dy,
        ),
        _ => Easing.linear,
      };
    }
    track = track.withKeyframe(
      Duration(microseconds: (p.compositionTime * 1e6).round()),
      p.sourceTime,
      ease,
    );
  }
  return track;
}

// ---------------------------------------------------------------------------
// O EDITOR NOVO
// ---------------------------------------------------------------------------

/// Raio do toque que pega um ponto: a ponta do dedo, e nao o desenho.
const _raioDoToqueNoPonto = 36.0;

/// Distancia vertical ate a linha que ainda conta como "tocou na linha".
const _distanciaDaLinha = 28.0;

/// Quanto o dedo anda antes de o eixo do arrasto ser decidido.
const _travaDoEixo = 10.0;

/// Alcance do ima, em pixels de tela.
const _pixelsDoIma = 10.0;

const _alturaDoCabecalho = 52.0;
const _alturaDoInspetor = 104.0;
const _alturaDosProntos = 56.0;

double _segundos(Duration d) => d.inMicroseconds / 1000000.0;

/// O GRAFICO DE TEMPO QUE SE MEXE COM O DEDO (estilo After Effects).
///
/// Os testadores pediram "vem o grafico reto e ai pode ajustar", e o dono
/// resumiu o editor antigo: MUITO complicado de mexer num celular. Os
/// motivos estavam no codigo: a folha modal roubava o arrasto vertical
/// (so o quase horizontal chegava ao grafico), as alcas invisiveis de um
/// ponto linear entortavam a curva no segundo toque, o eixo vertical se
/// recalculava a cada evento e o valor fugia do dedo, o inspetor entrava
/// e o grafico encolhia, e cada evento virava um passo de desfazer.
///
/// Agora: o grafico vive numa [AreaDeArrasto] (ganha da folha e da
/// rolagem), os eixos ficam CONGELADOS enquanto o dedo esta no vidro,
/// os pontos sao grandes e sem alcas (a curva sai do modo do ponto, ver
/// `domain/curva_de_tempo.dart`), o inspetor tem altura fixa e um arrasto
/// e um passo de desfazer.
///
/// Abrir nao cria remap: so a primeira edicao cria, como sempre foi.
class TimeRemapCurveEditor extends ConsumerStatefulWidget {
  const TimeRemapCurveEditor({super.key, required this.layerId, this.playback});

  final String layerId;
  final PlaybackController? playback;

  @override
  ConsumerState<TimeRemapCurveEditor> createState() =>
      _TimeRemapCurveEditorState();
}

enum _Eixo { vertical, horizontal, livre }

class _TimeRemapCurveEditorState extends ConsumerState<TimeRemapCurveEditor> {
  List<PontoDeTempo> _pontos = const [];
  AnimatedDouble _curva = AnimatedDouble(0);
  int? _selecionado;
  bool _temCamada = false;
  Duration _duracao = const Duration(seconds: 1);
  Duration _inicioDaCamada = Duration.zero;
  Duration _sourceOffset = Duration.zero;
  Duration? _sourceDuration;
  int _fps = 30;
  double _vMin = 0;
  double _vMax = 1;
  double _yMin = 0;
  double _yMax = 1;

  /// O que o editor viu por ultimo na camada. Diferente disto, sem gesto e
  /// sem gravacao nossa em curso, a mudanca veio de fora (desfazer).
  Object? _assinatura;
  bool _gravando = false;
  String? _aviso;

  // O gesto em andamento.
  _Geometria? _geometria;
  int? _tocado;
  PontoDeTempo? _pontoNoInicio;
  Offset _inicioDoToque = Offset.zero;
  _Eixo? _eixo;
  bool _arrastando = false;
  bool _esfregando = false;
  ImaDoValor? _ima;
  EditorController? _controladorDoGesto;

  @override
  void initState() {
    super.initState();
    _carregar(ref.read(editorControllerProvider));
    widget.playback?.time.addListener(_aoAndarOCabecote);
  }

  @override
  void didUpdateWidget(covariant TimeRemapCurveEditor oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.playback != widget.playback) {
      oldWidget.playback?.time.removeListener(_aoAndarOCabecote);
      widget.playback?.time.addListener(_aoAndarOCabecote);
    }
    if (oldWidget.layerId != widget.layerId) {
      _selecionado = null;
      _carregar(ref.read(editorControllerProvider));
    }
  }

  @override
  void dispose() {
    widget.playback?.time.removeListener(_aoAndarOCabecote);
    // UM GESTO ABERTO NAO PODE FICAR ABERTO: a folha pode fechar com o
    // dedo ainda no vidro.
    if (_arrastando) _controladorDoGesto?.endGesture();
    super.dispose();
  }

  void _aoAndarOCabecote() {
    if (mounted) setState(() {});
  }

  static Object _assinaturaDe(VideoLayer l) => (
    timeRemapTrackOf(l),
    l.reverse,
    l.duration,
    l.startTime,
    l.sourceOffset,
    l.sourceDuration,
    l.speed,
  );

  /// Le a camada e monta os pontos DO QUE TOCA.
  ///
  /// Sem remap, a reta da velocidade do clipe. Com Reverso, os valores
  /// espelhados pelo mesmo span do nucleo — o grafico mostra o quadro que
  /// aparece, e a primeira edicao assa isso na curva.
  void _carregar(VideoProject projeto) {
    _fps = projeto.fps < 1 ? 30 : projeto.fps;
    final layer = projeto.layerById(widget.layerId);
    if (layer is! VideoLayer) {
      _temCamada = false;
      _assinatura = null;
      _pontos = const [];
      _curva = AnimatedDouble(0);
      _selecionado = null;
      return;
    }
    _temCamada = true;
    _duracao = layer.duration > Duration.zero
        ? layer.duration
        : const Duration(seconds: 1);
    _inicioDaCamada = layer.startTime;
    _sourceOffset = layer.sourceOffset;
    _sourceDuration = layer.sourceDuration;
    final trilha = timeRemapTrackOf(layer);
    var pontos = trilha == null
        ? curvaReta(_duracao, velocidade: layer.speed)
        : ajustarPontasAoClipe(pontosDaCurva(trilha), _duracao);
    if (pontos.length < 2) {
      pontos = curvaReta(_duracao, velocidade: layer.speed);
    }
    if (layer.reverse) {
      pontos = espelharValores(pontos, _segundos(videoSourceSpan(layer)));
    }
    _pontos = pontos;
    _curva = curvaDosPontos(pontos);
    _assinatura = _assinaturaDe(layer);
    if (_selecionado != null && _selecionado! >= _pontos.length) {
      _selecionado = null;
    }
    _recalcularEixos();
  }

  /// EIXOS: so fora do gesto. Com o dedo no vidro a escala nao muda, senao
  /// o valor sob o dedo foge (o topo do grafico subia junto com o ponto).
  void _recalcularEixos() {
    final limites = limitesDaFonte(
      sourceOffset: _sourceOffset,
      sourceDuration: _sourceDuration,
      duracao: _duracao,
      pontos: _pontos,
    );
    _vMin = limites.vMin;
    _vMax = limites.vMax;
    var baixo = 0.0, alto = 0.0;
    for (final p in _pontos) {
      baixo = math.min(baixo, p.v);
      alto = math.max(alto, p.v);
    }
    final (lo, hi) = coreRange(_curva);
    baixo = math.min(baixo, lo);
    alto = math.max(alto, hi);
    // A fonte que sobra abre espaco para cima, mas com teto: um clipe de
    // 3 s cortado de um video de 10 min viraria uma linha deitada no chao.
    final escala = math.max(_segundos(_duracao), alto);
    final fonte = _sourceDuration == null
        ? escala
        : _segundos(_sourceDuration! - _sourceOffset);
    alto = math.max(alto, math.min(fonte, 2 * escala));
    if (alto - baixo < 0.1) alto = baixo + 0.1;
    final folga = (alto - baixo) * 0.08;
    _yMin = baixo - folga;
    _yMax = alto + folga;
  }

  Duration get _folga => Duration(microseconds: (1000000 / _fps).ceil());

  /// O quadro da composicao mais perto (o mesmo instante que o cabecote
  /// usa para cada quadro).
  Duration _noQuadro(Duration t) {
    final quadro = (t.inMicroseconds * _fps / 1000000).round();
    return Duration(microseconds: (quadro * 1000000 / _fps).ceil());
  }

  Duration get _cabecote {
    final playback = widget.playback;
    if (playback == null) return Duration.zero;
    final t = playback.time.value - _inicioDaCamada;
    if (t < Duration.zero) return Duration.zero;
    return t > _duracao ? _duracao : t;
  }

  void _buscar(Duration local) {
    final playback = widget.playback;
    if (playback == null) return;
    var t = local;
    if (t < Duration.zero) t = Duration.zero;
    if (t > _duracao) t = _duracao;
    playback.seek(_inicioDaCamada + t);
  }

  /// GRAVA NO PROJETO.
  ///
  /// Dentro de um arrasto, o passo de desfazer ja foi aberto pelo
  /// `beginGesture`. Fora dele, cada acao e o proprio passo
  /// (`runAsOneUndo`): sem isso, tocar num pronto logo depois de um
  /// arrasto caia na janela de 450 ms e um desfazer levava os dois.
  void _gravar(
    List<PontoDeTempo> novos, {
    int? selecionar,
    bool limparSelecao = false,
  }) {
    final controller = ref.read(editorControllerProvider.notifier);
    final curva = curvaDosPontos(novos);
    final id = widget.layerId;
    void escrever() {
      final atual = ref.read(editorControllerProvider).layerById(id);
      if (atual is VideoLayer && atual.reverse) {
        controller.assarReversoNoTimeRemap(id);
      }
      controller.setClipTimeRemap(id, curva);
    }

    _gravando = true;
    try {
      if (_arrastando) {
        escrever();
      } else {
        controller.runAsOneUndo(escrever);
      }
    } finally {
      _gravando = false;
    }
    final layer = ref.read(editorControllerProvider).layerById(id);
    setState(() {
      _pontos = novos;
      _curva = curva;
      _aviso = null;
      if (layer is VideoLayer) _assinatura = _assinaturaDe(layer);
      if (limparSelecao) _selecionado = null;
      if (selecionar != null) _selecionado = selecionar;
      if (!_arrastando) _recalcularEixos();
    });
  }

  int? _pontoMaisPerto(Offset local) {
    final g = _geometria;
    if (g == null) return null;
    int? melhor;
    var distancia = _raioDoToqueNoPonto;
    for (var i = 0; i < _pontos.length; i++) {
      final d = (g.ponto(_pontos[i]) - local).distance;
      if (d <= distancia) {
        melhor = i;
        distancia = d;
      }
    }
    return melhor;
  }

  // ----------------------------------------------------------------- gestos

  /// TOQUE: ponto perto seleciona; perto da linha cria um ponto ali (acao
  /// explicita, ver `docs/keyframe-explicito.md`); no vazio, deseleciona.
  void _aoTocar(Offset local) {
    final g = _geometria;
    if (g == null || !_temCamada) return;
    final perto = _pontoMaisPerto(local);
    if (perto != null) {
      setState(() {
        _selecionado = perto;
        _aviso = null;
      });
      return;
    }
    var t = _noQuadro(g.tempo(local.dx));
    if (t < Duration.zero) t = Duration.zero;
    if (t > _duracao) t = _duracao;
    final naFaixa =
        local.dx >= _Geometria.esquerda - 16 &&
        local.dx <= g.tamanho.width - _Geometria.direita + 16;
    final yDaLinha = g.y(coreValue(_curva, t, linear: true));
    if (naFaixa && (local.dy - yDaLinha).abs() <= _distanciaDaLinha) {
      final r = inserirNaCurva(_pontos, t, fps: _fps);
      if (r.criou) {
        HapticFeedback.lightImpact();
        _gravar(r.pontos, selecionar: r.indice);
      } else {
        setState(() => _selecionado = r.indice);
      }
      return;
    }
    setState(() {
      _selecionado = null;
      _aviso = null;
    });
  }

  void _aoComecar(Offset local) {
    _inicioDoToque = local;
    _tocado = _temCamada ? _pontoMaisPerto(local) : null;
    _pontoNoInicio = _tocado == null ? null : _pontos[_tocado!];
    _eixo = null;
    _arrastando = false;
    _esfregando = false;
    _ima = null;
  }

  void _aoMover(Offset local, Offset delta) {
    final g = _geometria;
    if (g == null || !_temCamada) return;
    final desloc = local - _inicioDoToque;
    final i = _tocado;
    if (i == null) {
      // Arrastar fora dos pontos passeia o cabecote: e o jeito de levar
      // o "+ Keyframe" e o "Congelar aqui" para o lugar certo.
      if (!_esfregando && desloc.distance < _travaDoEixo) return;
      _esfregando = true;
      _buscar(g.tempo(local.dx));
      return;
    }
    if (!_arrastando) {
      if (desloc.distance < _travaDoEixo) return;
      // TRAVA DE EIXO: um arrasto para cima nao pode empurrar o ponto de
      // lado por tremer o dedo. As pontas so andam na vertical.
      final ponta = i == 0 || i == _pontos.length - 1;
      final dx = desloc.dx.abs(), dy = desloc.dy.abs();
      _eixo = ponta || dy > 1.4 * dx
          ? _Eixo.vertical
          : (dx > 1.4 * dy ? _Eixo.horizontal : _Eixo.livre);
      final playback = widget.playback;
      if (playback != null && playback.playing.value) playback.pause();
      final controller = ref.read(editorControllerProvider.notifier);
      controller.beginGesture();
      _controladorDoGesto = controller;
      _arrastando = true;
      _selecionado = i;
    }
    final inicio = _pontoNoInicio!;
    var t = inicio.t;
    var v = inicio.v;
    if (_eixo != _Eixo.vertical) {
      t = _noQuadro(
        inicio.t +
            Duration(
              microseconds: (desloc.dx * g.segundosPorPixelX * 1000000).round(),
            ),
      );
    }
    if (_eixo != _Eixo.horizontal) {
      v = inicio.v - desloc.dy * g.valorPorPixelY;
    }
    var novos = moverPonto(
      _pontos,
      i,
      t: t,
      v: v,
      duracao: _duracao,
      vMin: _vMin,
      vMax: _vMax,
      folga: _folga,
    );
    ImaDoValor? ima;
    if (_eixo != _Eixo.horizontal) {
      final encaixe = imaDoValor(
        novos,
        i,
        t: novos[i].t,
        v: novos[i].v,
        tolerancia: _pixelsDoIma * g.valorPorPixelY,
      );
      ima = encaixe.ima;
      if (ima != null) {
        novos = moverPonto(
          novos,
          i,
          v: encaixe.valor,
          duracao: _duracao,
          vMin: _vMin,
          vMax: _vMax,
          folga: _folga,
        );
      }
    }
    if (ima != null && ima != _ima) HapticFeedback.selectionClick();
    _ima = ima;
    _gravar(novos);
    // A previa acima mostra o quadro que esta sendo remapeado.
    _buscar(novos[i].t);
  }

  void _aoSoltar() {
    if (_arrastando) {
      _controladorDoGesto?.endGesture();
      _controladorDoGesto = null;
      _arrastando = false;
      final projeto = ref.read(editorControllerProvider);
      final layer = projeto.layerById(widget.layerId);
      setState(() {
        if (layer is! VideoLayer || _assinaturaDe(layer) != _assinatura) {
          _carregar(projeto);
        } else {
          _recalcularEixos();
        }
      });
    }
    _tocado = null;
    _pontoNoInicio = null;
    _eixo = null;
    _esfregando = false;
    _ima = null;
  }

  // ----------------------------------------------------------------- acoes

  void _definirModo(ModoDoPonto modo) {
    final i = _selecionado;
    if (i == null || i >= _pontos.length) return;
    _gravar([
      for (var k = 0; k < _pontos.length; k++)
        k == i
            ? _pontos[k].copyWith(modo: modo, semEaseLivre: true)
            : _pontos[k],
    ]);
  }

  void _removerSelecionado() {
    final i = _selecionado;
    if (i == null || i <= 0 || i >= _pontos.length - 1) return;
    _gravar([..._pontos]..removeAt(i), limparSelecao: true);
  }

  /// "+ Keyframe": um ponto no cabecote, com o valor que a curva ja tem
  /// ali — nada muda ate a pessoa arrastar.
  void _maisKeyframe() {
    if (!_temCamada) return;
    final r = inserirNaCurva(_pontos, _noQuadro(_cabecote), fps: _fps);
    if (r.criou) {
      _gravar(r.pontos, selecionar: r.indice);
    } else {
      setState(() => _selecionado = r.indice);
    }
  }

  void _aplicar(List<PontoDeTempo> novos) {
    if (!_temCamada) return;
    _gravar(novos, limparSelecao: true);
  }

  /// CONGELAR AQUI: um segundo parado no cabecote, e o resto da curva
  /// inteiro depois dele (o clipe cresce um segundo). E o congelar
  /// "dentro do clipe" do controlador, o mesmo da folha de congelar.
  void _congelarAqui() {
    const aviso = 'Leve o cabeçote para dentro do clipe para congelar.';
    const margem = Duration(milliseconds: 50);
    final playback = widget.playback;
    if (!_temCamada || playback == null) {
      setState(() => _aviso = aviso);
      return;
    }
    final global = playback.time.value;
    final local = global - _inicioDaCamada;
    if (local < margem || _duracao - local < margem) {
      setState(() => _aviso = aviso);
      return;
    }
    final controller = ref.read(editorControllerProvider.notifier);
    var ok = false;
    _gravando = true;
    try {
      controller.runAsOneUndo(() {
        ok = controller.freezeFrame(
          widget.layerId,
          global,
          placement: FreezePlacement.insideClip,
        );
      });
    } finally {
      _gravando = false;
    }
    setState(() {
      _carregar(ref.read(editorControllerProvider));
      if (!ok) {
        _aviso = aviso;
        return;
      }
      _aviso = null;
      final alvo = _pontos.indexWhere(
        (p) => (p.t - local).abs() < const Duration(milliseconds: 9),
      );
      _selecionado = alvo < 0 ? null : alvo;
    });
  }

  // ----------------------------------------------------------------- tela

  @override
  Widget build(BuildContext context) {
    ref.watch(editorControllerProvider);
    ref.listen<VideoProject>(editorControllerProvider, (_, proximo) {
      if (_gravando || _arrastando) return;
      final layer = proximo.layerById(widget.layerId);
      final assinatura = layer is VideoLayer ? _assinaturaDe(layer) : null;
      if (assinatura == _assinatura) return;
      setState(() => _carregar(proximo));
    });
    final i = _selecionado;
    final selecionado = i != null && i < _pontos.length ? _pontos[i] : null;
    return ColoredBox(
      color: AmColors.panel,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _cabecalho(context),
          Expanded(child: _grafico(context)),
          SizedBox(
            key: const ValueKey('curva-inspetor'),
            height: _alturaDoInspetor,
            child: _inspetor(selecionado),
          ),
          SizedBox(height: _alturaDosProntos, child: _prontos()),
        ],
      ),
    );
  }

  Widget _cabecalho(BuildContext context) {
    var velocidade = _temCamada
        ? coreSlope(_curva, _cabecote, linear: true)
        : 1.0;
    if (!velocidade.isFinite) velocidade = 0;
    return SizedBox(
      height: _alturaDoCabecalho,
      child: Padding(
        padding: const EdgeInsets.only(left: 16, right: 4),
        child: Row(
          children: [
            const Expanded(
              child: AppText(
                'Curva de tempo',
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  color: AmColors.text,
                  fontSize: 16,
                  fontWeight: FontWeight.w700,
                ),
              ),
            ),
            Text(
              '${translate(context, 'Velocidade')}: '
              '${(velocidade * 100).round()}%',
              key: const ValueKey('curva-velocidade'),
              maxLines: 1,
              style: TextStyle(
                color: velocidade < 0 ? AmColors.pink : AmColors.accent,
                fontSize: 13,
                fontWeight: FontWeight.w600,
              ),
            ),
            const SizedBox(width: 4),
            Semantics(
              button: true,
              label: translate(context, 'Fechar'),
              child: GestureDetector(
                key: const ValueKey('curva-fechar'),
                behavior: HitTestBehavior.opaque,
                onTap: () => Navigator.maybePop(context),
                child: const SizedBox(
                  width: 44,
                  height: 44,
                  child: Icon(
                    CupertinoIcons.xmark,
                    size: 20,
                    color: AmColors.muted,
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _grafico(BuildContext context) {
    final textos = (
      video: translate(context, 'Vídeo'),
      clipe: translate(context, 'Clipe'),
      inicio: translate(context, 'Início do vídeo'),
      fim: translate(context, 'Fim do vídeo'),
    );
    return LayoutBuilder(
      builder: (context, constraints) {
        final tamanho = Size(
          constraints.maxWidth.isFinite ? constraints.maxWidth : 360,
          constraints.maxHeight.isFinite ? constraints.maxHeight : 240,
        );
        final g = _Geometria(tamanho, _duracao, _yMin, _yMax);
        _geometria = g;
        return AreaDeArrasto(
          key: const ValueKey('curva-grafico'),
          onStart: _aoComecar,
          onUpdate: _aoMover,
          onEnd: _aoSoltar,
          onTap: _aoTocar,
          child: SizedBox.fromSize(
            size: tamanho,
            child: Stack(
              children: [
                Positioned.fill(
                  child: CustomPaint(
                    painter: _PintorDaCurva(
                      geometria: g,
                      pontos: _pontos,
                      curva: _curva,
                      selecionado: _selecionado,
                      cabecote: widget.playback == null ? null : _cabecote,
                      inicioDoVideo: _temCamada
                          ? -_segundos(_sourceOffset)
                          : null,
                      fimDoVideo: _temCamada && _sourceDuration != null
                          ? _segundos(_sourceDuration! - _sourceOffset)
                          : null,
                      textos: textos,
                    ),
                  ),
                ),
                // Marcas invisiveis na posicao de cada ponto: o teste de
                // gesto acha o ponto por elas, sem copiar a conta do eixo.
                for (var k = 0; k < _pontos.length; k++)
                  Positioned(
                    left: g.ponto(_pontos[k]).dx - 11,
                    top: g.ponto(_pontos[k]).dy - 11,
                    width: 22,
                    height: 22,
                    child: SizedBox(key: ValueKey('curva-ponto-$k')),
                  ),
              ],
            ),
          ),
        );
      },
    );
  }

  /// ALTURA FIXA, com ou sem selecao: nada pula quando um ponto e tocado.
  Widget _inspetor(PontoDeTempo? p) {
    final i = _selecionado;
    if (p == null || i == null) {
      final aviso = _aviso;
      return Padding(
        padding: const EdgeInsets.fromLTRB(16, 10, 16, 8),
        child: Align(
          alignment: Alignment.topLeft,
          child: AppText(
            aviso ??
                'Toque na linha para criar um ponto. Arraste o ponto para '
                    'cima ou para baixo para mudar o tempo do vídeo.',
            maxLines: 3,
            overflow: TextOverflow.ellipsis,
            style: TextStyle(
              fontSize: 13,
              height: 1.35,
              color: aviso == null ? AmColors.muted : AmColors.pink,
            ),
          ),
        ),
      );
    }
    final ponta = i == 0 || i == _pontos.length - 1;
    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 8, 12, 8),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SizedBox(
            height: 40,
            child: Row(
              children: [
                Expanded(
                  child: _Leitura(
                    rotulo: 'Tempo do clipe',
                    valor: _textoDeSegundos(_segundos(p.t)),
                  ),
                ),
                Expanded(
                  child: _Leitura(
                    rotulo: 'Tempo do vídeo',
                    valor: _textoDeSegundos(p.v),
                  ),
                ),
                Expanded(
                  child: _Leitura(
                    rotulo: 'Antes',
                    valor: _textoDePorcentagem(
                      velocidadeDoTrecho(_pontos, i - 1),
                    ),
                  ),
                ),
                Expanded(
                  child: _Leitura(
                    rotulo: 'Depois',
                    valor: _textoDePorcentagem(velocidadeDoTrecho(_pontos, i)),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 8),
          SizedBox(
            height: 40,
            child: Row(
              children: [
                Expanded(
                  child: _Chip(
                    key: const ValueKey('curva-modo-linear'),
                    rotulo: 'Linear',
                    expandir: true,
                    selecionado: p.modo == ModoDoPonto.linear,
                    onTap: () => _definirModo(ModoDoPonto.linear),
                  ),
                ),
                const SizedBox(width: 6),
                Expanded(
                  child: _Chip(
                    key: const ValueKey('curva-modo-suave'),
                    rotulo: 'Suave',
                    expandir: true,
                    selecionado: p.modo == ModoDoPonto.suave,
                    onTap: () => _definirModo(ModoDoPonto.suave),
                  ),
                ),
                const SizedBox(width: 6),
                Expanded(
                  child: _Chip(
                    key: const ValueKey('curva-modo-segurar'),
                    rotulo: 'Segurar',
                    expandir: true,
                    // Segurar vale para o trecho que SAI do ponto: o ultimo
                    // nao tem trecho depois dele.
                    habilitado: i < _pontos.length - 1,
                    selecionado: p.modo == ModoDoPonto.segurar,
                    onTap: () => _definirModo(ModoDoPonto.segurar),
                  ),
                ),
                const SizedBox(width: 8),
                _BotaoDeRemover(
                  key: const ValueKey('curva-remover'),
                  habilitado: !ponta,
                  onTap: _removerSelecionado,
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _prontos() {
    return SingleChildScrollView(
      scrollDirection: Axis.horizontal,
      padding: const EdgeInsets.fromLTRB(12, 8, 12, 8),
      child: Row(
        children: [
          _Chip(
            key: const ValueKey('curva-mais-keyframe'),
            rotulo: '+ Keyframe',
            destaque: true,
            onTap: _maisKeyframe,
          ),
          const SizedBox(width: 8),
          _Chip(
            key: const ValueKey('curva-pronto-reto'),
            rotulo: 'Reto (1x)',
            onTap: () => _aplicar(curvaReta(_duracao, vMax: _vMax)),
          ),
          const SizedBox(width: 8),
          _Chip(
            key: const ValueKey('curva-pronto-camera-lenta'),
            rotulo: 'Câmera lenta no meio',
            onTap: () =>
                _aplicar(curvaCameraLentaNoMeio(_duracao, vMax: _vMax)),
          ),
          const SizedBox(width: 8),
          _Chip(
            key: const ValueKey('curva-pronto-acelerando'),
            rotulo: 'Acelerando',
            onTap: () => _aplicar(curvaAcelerando(_duracao, vMax: _vMax)),
          ),
          const SizedBox(width: 8),
          _Chip(
            key: const ValueKey('curva-pronto-desacelerando'),
            rotulo: 'Desacelerando',
            onTap: () => _aplicar(curvaDesacelerando(_duracao, vMax: _vMax)),
          ),
          const SizedBox(width: 8),
          _Chip(
            key: const ValueKey('curva-pronto-congelar'),
            rotulo: 'Congelar aqui',
            icone: CupertinoIcons.snow,
            onTap: _congelarAqui,
          ),
          const SizedBox(width: 8),
          _Chip(
            key: const ValueKey('curva-pronto-inverter'),
            rotulo: 'Inverter',
            icone: CupertinoIcons.arrow_right_arrow_left,
            onTap: () => _aplicar(inverterCurva(_pontos)),
          ),
        ],
      ),
    );
  }
}

String _textoDeSegundos(double s) => '${s.toStringAsFixed(2)} s';

String _textoDePorcentagem(double? v) =>
    v == null || !v.isFinite ? '—' : '${(v * 100).round()}%';

/// Rotulo pequeno e numero grande, numa linha cada.
class _Leitura extends StatelessWidget {
  const _Leitura({required this.rotulo, required this.valor});

  final String rotulo;
  final String valor;

  @override
  Widget build(BuildContext context) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    mainAxisAlignment: MainAxisAlignment.center,
    children: [
      AppText(
        rotulo,
        maxLines: 1,
        overflow: TextOverflow.ellipsis,
        style: const TextStyle(fontSize: 11, color: AmColors.muted),
      ),
      Text(
        valor,
        maxLines: 1,
        overflow: TextOverflow.ellipsis,
        style: const TextStyle(
          fontSize: 14,
          fontWeight: FontWeight.w600,
          color: AmColors.text,
        ),
      ),
    ],
  );
}

/// Chip de 40 px, sem ripple e sem borda (direcao visual do app).
class _Chip extends StatelessWidget {
  const _Chip({
    super.key,
    required this.rotulo,
    required this.onTap,
    this.icone,
    this.selecionado = false,
    this.habilitado = true,
    this.destaque = false,
    this.expandir = false,
  });

  final String rotulo;
  final VoidCallback onTap;
  final IconData? icone;
  final bool selecionado;
  final bool habilitado;
  final bool destaque;
  final bool expandir;

  @override
  Widget build(BuildContext context) {
    final cor = !habilitado
        ? AmColors.muted.withValues(alpha: 0.5)
        : selecionado
        ? AmColors.accent
        : destaque
        ? AmColors.onAction
        : AmColors.text;
    final fundo = !habilitado
        ? AmColors.chip.withValues(alpha: 0.5)
        : selecionado
        ? AmColors.accentDim
        : destaque
        ? AmColors.action
        : AmColors.chip;
    final texto = AppText(
      rotulo,
      maxLines: 1,
      overflow: TextOverflow.ellipsis,
      style: TextStyle(fontSize: 13, fontWeight: FontWeight.w600, color: cor),
    );
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: habilitado ? onTap : null,
      child: Container(
        height: 40,
        alignment: Alignment.center,
        padding: const EdgeInsets.symmetric(horizontal: 14),
        decoration: BoxDecoration(
          color: fundo,
          borderRadius: BorderRadius.circular(12),
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            if (icone != null) ...[
              Icon(icone, size: 16, color: cor),
              const SizedBox(width: 6),
            ],
            if (expandir) Flexible(child: texto) else texto,
          ],
        ),
      ),
    );
  }
}

class _BotaoDeRemover extends StatelessWidget {
  const _BotaoDeRemover({
    super.key,
    required this.habilitado,
    required this.onTap,
  });

  final bool habilitado;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => Semantics(
    button: true,
    enabled: habilitado,
    label: translate(context, 'Remover'),
    child: GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: habilitado ? onTap : null,
      child: Container(
        width: 44,
        height: 40,
        alignment: Alignment.center,
        decoration: BoxDecoration(
          color: AmColors.chip.withValues(alpha: habilitado ? 1 : 0.5),
          borderRadius: BorderRadius.circular(12),
        ),
        child: Icon(
          CupertinoIcons.trash,
          size: 18,
          color: habilitado
              ? AmColors.pink
              : AmColors.muted.withValues(alpha: 0.5),
        ),
      ),
    ),
  );
}

/// A CONTA DO EIXO, uma so para o desenho e para o dedo.
class _Geometria {
  const _Geometria(this.tamanho, this.duracao, this.yMin, this.yMax);

  static const esquerda = 44.0;
  static const direita = 18.0;
  static const topo = 14.0;
  static const baixo = 24.0;

  final Size tamanho;
  final Duration duracao;
  final double yMin;
  final double yMax;

  double get largura => math.max(1.0, tamanho.width - esquerda - direita);
  double get altura => math.max(1.0, tamanho.height - topo - baixo);
  double get segundos => math.max(1e-6, _segundos(duracao));
  double get faixa => math.max(1e-6, yMax - yMin);

  Rect get area => Rect.fromLTWH(esquerda, topo, largura, altura);

  double xDosSegundos(double s) => esquerda + s / segundos * largura;
  double x(Duration t) => xDosSegundos(_segundos(t));
  double y(double v) => topo + (1 - (v - yMin) / faixa) * altura;
  Offset ponto(PontoDeTempo p) => Offset(x(p.t), y(p.v));

  Duration tempo(double px) => Duration(
    microseconds: ((px - esquerda) / largura * segundos * 1000000).round(),
  );

  double get segundosPorPixelX => segundos / largura;
  double get valorPorPixelY => faixa / altura;
}

/// Passo "redondo" de grade para cerca de [alvo] divisoes.
double _passoDaGrade(double faixa, int alvo) {
  const passos = [
    0.1, 0.2, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, //
    300.0, 600.0, 1800.0, 3600.0,
  ];
  final bruto = faixa / math.max(1, alvo);
  for (final p in passos) {
    if (p >= bruto) return p;
  }
  return passos.last * (bruto / passos.last).ceil();
}

String _rotuloDaGrade(double s) {
  final r = (s * 100).round() / 100;
  if (r == 0) return '0 s';
  final inteiro = r == r.roundToDouble();
  final umaCasa = (r * 10) == (r * 10).roundToDouble();
  final texto = inteiro
      ? r.toStringAsFixed(0)
      : (umaCasa ? r.toStringAsFixed(1) : r.toStringAsFixed(2));
  return '$texto s';
}

class _PintorDaCurva extends CustomPainter {
  _PintorDaCurva({
    required this.geometria,
    required this.pontos,
    required this.curva,
    required this.selecionado,
    required this.cabecote,
    required this.inicioDoVideo,
    required this.fimDoVideo,
    required this.textos,
  });

  final _Geometria geometria;
  final List<PontoDeTempo> pontos;
  final AnimatedDouble curva;
  final int? selecionado;
  final Duration? cabecote;
  final double? inicioDoVideo;
  final double? fimDoVideo;
  final ({String video, String clipe, String inicio, String fim}) textos;

  @override
  void paint(Canvas canvas, Size size) {
    final g = geometria;
    final area = g.area;
    final grade = Paint()
      ..color = const Color(0x14FFFFFF)
      ..strokeWidth = 1;

    // GRADE E ROTULOS: segundos do video a esquerda, do clipe embaixo.
    final passoY = _passoDaGrade(g.faixa, 5);
    var v = (g.yMin / passoY).ceil() * passoY;
    for (var n = 0; v <= g.yMax && n < 60; n++, v += passoY) {
      final y = g.y(v);
      canvas.drawLine(Offset(area.left, y), Offset(area.right, y), grade);
      _texto(
        canvas,
        _rotuloDaGrade(v),
        Offset(area.left - 6, y),
        const Alignment(1, 0),
      );
    }
    final passoX = _passoDaGrade(g.segundos, 5);
    var s = 0.0;
    for (var n = 0; s <= g.segundos + 1e-9 && n < 60; n++, s += passoX) {
      final x = g.xDosSegundos(s);
      canvas.drawLine(Offset(x, area.top), Offset(x, area.bottom), grade);
      _texto(
        canvas,
        _rotuloDaGrade(s),
        Offset(x, area.bottom + 4),
        const Alignment(0, -1),
      );
    }
    _texto(
      canvas,
      textos.video,
      Offset(area.left + 4, area.top + 2),
      const Alignment(-1, -1),
    );
    _texto(
      canvas,
      textos.clipe,
      Offset(area.right - 4, area.bottom - 2),
      const Alignment(1, 1),
    );

    canvas.save();
    canvas.clipRect(area.inflate(16));

    // A reta de velocidade normal (1x), fraca e tracejada.
    final guia = Paint()
      ..color = const Color(0x33FFFFFF)
      ..strokeWidth = 1.2;
    _tracejada(
      canvas,
      Offset(g.x(Duration.zero), g.y(0)),
      Offset(g.x(g.duracao), g.y(g.segundos)),
      guia,
    );

    // Onde o arquivo comeca e acaba.
    final limite = Paint()
      ..color = AmColors.muted.withValues(alpha: 0.55)
      ..strokeWidth = 1;
    for (final (valor, rotulo) in [
      (inicioDoVideo, textos.inicio),
      (fimDoVideo, textos.fim),
    ]) {
      if (valor == null || valor < g.yMin || valor > g.yMax) continue;
      final y = g.y(valor);
      _tracejada(canvas, Offset(area.left, y), Offset(area.right, y), limite);
      _texto(
        canvas,
        rotulo,
        Offset(area.right - 4, y - 3),
        const Alignment(1, 1),
      );
    }

    // Cabecote.
    final t = cabecote;
    if (t != null) {
      final x = g.x(t);
      canvas.drawLine(
        Offset(x, area.top),
        Offset(x, area.bottom),
        Paint()
          ..color = AmColors.pink.withValues(alpha: 0.85)
          ..strokeWidth = 1.5,
      );
    }

    // A curva, pelo nucleo: o mesmo valor que a previa e a exportacao.
    if (pontos.length >= 2) {
      final caminho = Path();
      final passos = area.width.ceil().clamp(2, 2400);
      for (var k = 0; k <= passos; k++) {
        final instante = Duration(
          microseconds: (g.duracao.inMicroseconds * k / passos).round(),
        );
        final p = Offset(
          g.x(instante),
          g.y(coreValue(curva, instante, linear: true)),
        );
        if (k == 0) {
          caminho.moveTo(p.dx, p.dy);
        } else {
          caminho.lineTo(p.dx, p.dy);
        }
      }
      canvas.drawPath(
        caminho,
        Paint()
          ..color = AmColors.accent
          ..strokeWidth = 3
          ..style = PaintingStyle.stroke
          ..strokeJoin = StrokeJoin.round,
      );

      // Velocidade de cada trecho, quando cabe.
      for (var i = 0; i + 1 < pontos.length; i++) {
        final xa = g.x(pontos[i].t), xb = g.x(pontos[i + 1].t);
        if (xb - xa < 44) continue;
        final velocidade = velocidadeDoTrecho(pontos, i) ?? 0;
        final meio = pontos[i].t + (pontos[i + 1].t - pontos[i].t) ~/ 2;
        final y = g.y(coreValue(curva, meio, linear: true));
        final acima = y - 24 > area.top;
        _texto(
          canvas,
          '${(velocidade * 100).round()}%',
          Offset((xa + xb) / 2, acima ? y - 10 : y + 10),
          Alignment(0, acima ? 1 : -1),
          cor: AmColors.text.withValues(alpha: 0.75),
          tamanho: 11,
        );
      }
    }

    // Pontos: 8 px, o selecionado 11 com anel. Sem alcas.
    final contorno = Paint()
      ..color = AmColors.bg
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2;
    for (var i = 0; i < pontos.length; i++) {
      final c = g.ponto(pontos[i]);
      final eh = i == selecionado;
      if (eh) {
        canvas.drawCircle(
          c,
          18,
          Paint()..color = AmColors.accent.withValues(alpha: 0.18),
        );
        canvas.drawCircle(
          c,
          15,
          Paint()
            ..color = AmColors.accent
            ..style = PaintingStyle.stroke
            ..strokeWidth = 2,
        );
      }
      final raio = eh ? 11.0 : 8.0;
      canvas.drawCircle(
        c,
        raio,
        Paint()..color = eh ? AmColors.accent : AmColors.text,
      );
      canvas.drawCircle(c, raio, contorno);
      if (pontos[i].modo == ModoDoPonto.segurar) {
        canvas.drawRect(
          Rect.fromCenter(center: c, width: 6, height: 6),
          Paint()..color = AmColors.bg,
        );
      }
    }
    canvas.restore();
  }

  void _tracejada(Canvas canvas, Offset a, Offset b, Paint paint) {
    final total = (b - a).distance;
    if (total <= 0 || !total.isFinite) return;
    final direcao = (b - a) / total;
    for (var d = 0.0; d < total && d < 20000; d += 10) {
      canvas.drawLine(
        a + direcao * d,
        a + direcao * math.min(d + 6, total),
        paint,
      );
    }
  }

  /// Texto ancorado: [alinhamento] diz qual ponto do texto cai na ancora
  /// (-1 = esquerda/topo, 1 = direita/base).
  void _texto(
    Canvas canvas,
    String texto,
    Offset ancora,
    Alignment alinhamento, {
    Color cor = AmColors.muted,
    double tamanho = 10,
  }) {
    final tp = TextPainter(
      text: TextSpan(
        text: texto,
        style: TextStyle(color: cor, fontSize: tamanho),
      ),
      textDirection: TextDirection.ltr,
      maxLines: 1,
    )..layout();
    tp.paint(
      canvas,
      Offset(
        ancora.dx - tp.width * (alinhamento.x + 1) / 2,
        ancora.dy - tp.height * (alinhamento.y + 1) / 2,
      ),
    );
    tp.dispose();
  }

  @override
  bool shouldRepaint(covariant _PintorDaCurva oldDelegate) => true;
}
