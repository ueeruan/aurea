import 'package:flutter/gestures.dart';
import 'package:flutter/physics.dart';
import 'package:flutter/rendering.dart' show ScrollCacheExtent;
import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../application/editor_controller.dart';
import '../../../application/keyframe_clipboard.dart';
import '../../../application/perfil3d.dart';
import '../../../application/playback_controller.dart';
import '../shell/contrato.dart';
import 'cabecote.dart';
import 'estado_da_timeline.dart';
import 'guias.dart';
import 'linha_da_camada.dart';
import 'linha_da_propriedade.dart';
import 'linhas.dart';
import 'regua.dart';
import 'reordenar.dart';

export 'estado_da_timeline.dart' show EstadoDaTimeline, SondaDaTimeline;
export 'linhas.dart' show camadasExpandidasProvider;

// ===========================================================================
// A TIMELINE DO EDITOR
// ===========================================================================
//
//   regua 42 · linhas de 28 (cabecalho 70 fixo + clipe 23) · cabecote FIXO
//   no centro (1,5; toque 100 x 38) · rolar = scrub (o tempo corre sob ele)
//
// MAPA DOS GESTOS — quem ganha a arena, e por que nao ha briga.
//
// A arena do Flutter da o ponteiro a quem ACEITA primeiro; num empate de
// evento, ao mais FUNDO (quem recebe o movimento antes). A timeline usa isso
// de proposito, em tres camadas:
//
//  1. RAIZ (esta tela): UM reconhecedor de escala faz tudo o que e "no
//     vazio" — 1 dedo na horizontal = scrub (com inercia), 1 dedo na
//     vertical = rolar as camadas (com inercia), 2 dedos = pinca de zoom
//     ANCORADA no instante sob os dedos. O eixo se decide UMA vez, pelo
//     caminho que o dedo fez ate vencer a arena, e nao muda no meio do
//     gesto. O folga de arrasto dele e a mesma de um arrasto comum (e nao
//     a dobrada da escala), senao o clipe escolhido e a raiz empatariam
//     com vantagem errada. Um toque que nao vira nada solta a selecao.
//
//  2. ZONAS CALCULADAS de cada linha (`AreaDeToqueCalculada`): o corpo do
//     clipe, as alcas de trim (30 FORA do clipe) e os losangos. Elas so
//     "existem" para o toque onde o pintor desenhou alguma coisa, com a
//     vista de agora; fora disso o dedo cai direto na raiz. Dentro:
//       - toque no clipe ............. escolhe a camada
//       - toque longo no clipe ....... menu da camada
//       - arrastar o clipe ESCOLHIDO . move no tempo (ima + guia + toque).
//         Clipe NAO escolhido nao arrasta: o arrasto e da raiz (scrub).
//       - arrastar a alca ............ apara a ponta (ima)
//       - toque no losango ........... escolhe a marca e leva o cabecote
//       - arrastar o losango ......... move a marca (ima no cabecote)
//       - toque longo no losango ..... editor de curva
//     Sao mais fundas que a raiz: num arrasto horizontal elas aceitam no
//     MESMO evento que a raiz e ganham por ordem. Um arrasto VERTICAL sobre
//     o clipe nao interessa a elas (so tem arrasto horizontal) e vai para a
//     raiz: rola as camadas.
//
//  3. CABECALHO (70, por cima da ponta esquerda de cada linha): toque
//     escolhe (na ja escolhida e animada, abre as propriedades); toque no
//     olho (44) mostra/oculta; toque LONGO + arrastar reordena, e a alca de
//     35 da linha escolhida reordena direto no arrasto vertical. O toque
//     longo vence a raiz porque ela so aceita com movimento; mexer antes
//     dos 500 ms e rolar.
//
// Toda edicao de um gesto e UM passo de desfazer (`SessaoDeGesto`), e cada
// passo marca a interacao (o palco desenha em rascunho enquanto o dedo
// anda).
//
// DESEMPENHO: a raiz observa so a ESTRUTURA (a lista de linhas, por
// valor). Cada linha observa so a propria camada. O relogio nao reconstroi
// nada: a vista (`EstadoDaTimeline`) e um notificador que os pintores
// escutam. A lista e virtual (so as linhas da tela, com folga pequena) e
// cada pintor so desenha a janela visivel.

/// O modo do gesto da raiz em curso.
enum _Modo { nenhum, scrub, rolagem, pinca }

class TimelineDoEditor extends ConsumerStatefulWidget {
  const TimelineDoEditor({
    super.key,
    required this.playback,
    this.aoScrub,
    this.aoTocarNoVazio,
    this.aoTocarNaCamada,
  });

  final PlaybackController playback;

  /// A cada passo de scrub (o gerente de video toca lasquinhas de som).
  final VoidCallback? aoScrub;

  /// Toque fora de qualquer clipe: a casca solta a selecao.
  final VoidCallback? aoTocarNoVazio;

  /// Toque num clipe (depois de selecionar).
  final ValueChanged<String>? aoTocarNaCamada;

  @override
  ConsumerState<TimelineDoEditor> createState() => TimelineDoEditorState();
}

class TimelineDoEditorState extends ConsumerState<TimelineDoEditor>
    with TickerProviderStateMixin {
  late final EstadoDaTimeline estado = EstadoDaTimeline(
    playback: widget.playback,
    vsync: this,
  );

  final ScrollController _rolagem = ScrollController();
  final GlobalKey _chaveDaLista = GlobalKey();
  late final ControleDeReordenar _reordenar;

  /// As linhas de agora (a estrutura da ultima montagem).
  List<LinhaDaTimeline> _linhas = const [];

  /// AS LINHAS MEMORIZADAS, pela chave: a lista devolve o MESMO widget, e
  /// o Flutter nem desce na linha — quem a reconstroi sao os `select` dela.
  final Map<String, Widget> _memo = {};
  final Map<String, int> _indiceDaChave = {};

  // ------------------------------------------------------ gesto da raiz
  _Modo _modo = _Modo.nenhum;
  int _dedos = 0;
  Offset? _pontoDoToque;
  bool _ignorarResto = false;
  double _vista0 = 0;
  double _x0 = 0;
  double _y0 = 0;
  double _rolagem0 = 0;
  double _pps0 = 0;
  double? _escala0;
  double _focoUs = 0;

  // ---------------------------------------------------- inercia do scrub
  late final Ticker _inercia = createTicker(_tiqueDaInercia);
  FrictionSimulation? _simulacao;
  double _vistaDaInercia = 0;

  @override
  void initState() {
    super.initState();
    _reordenar = ControleDeReordenar(
      estado: estado,
      lista: _rolagem,
      chaveDaLista: _chaveDaLista,
      linhas: () => _linhas,
      controlador: () => ref.read(editorControllerProvider.notifier),
    );
    estado.reordenar = _reordenar;
  }

  @override
  void dispose() {
    _reordenar.cancelar();
    _pararInercia();
    _inercia.dispose();
    if (_modo == _Modo.scrub || _modo == _Modo.pinca) estado.soltarVista();
    _rolagem.dispose();
    estado.dispose();
    super.dispose();
  }

  // =============================================================== gestos

  void _aoPousar(PointerDownEvent e) {
    _pararInercia();
    // Parar a inercia da lista tambem: o dedo que pousa segura tudo.
    if (_rolagem.hasClients && _rolagem.position.isScrollingNotifier.value) {
      _rolagem.jumpTo(_rolagem.offset);
    }
    if (_dedos == 0) {
      _pontoDoToque = e.localPosition;
      _ignorarResto = false;
    }
    _dedos++;
  }

  void _aoLevantar(PointerEvent _) {
    if (_dedos > 0) _dedos--;
  }

  void _inicio(ScaleStartDetails d) {
    _pararInercia();
    if (_ignorarResto) return;
    if (d.pointerCount >= 2) {
      _comecarPinca(d.localFocalPoint);
      return;
    }
    // O EIXO SAI DO CAMINHO que o dedo fez ate vencer a arena.
    final desde = _pontoDoToque ?? d.localFocalPoint;
    final v = d.localFocalPoint - desde;
    if (v.dx.abs() >= v.dy.abs()) {
      _modo = _Modo.scrub;
      widget.playback.pause();
      estado.segurarVista();
      _vista0 = estado.vistaUs.value;
      _x0 = d.localFocalPoint.dx;
    } else {
      _modo = _Modo.rolagem;
      _y0 = d.localFocalPoint.dy;
      _rolagem0 = _rolagem.hasClients ? _rolagem.offset : 0;
    }
  }

  void _comecarPinca(Offset foco) {
    _modo = _Modo.pinca;
    widget.playback.pause();
    estado.segurarVista();
    _pps0 = estado.pps.value;
    _escala0 = null;
    // O INSTANTE SOB OS DEDOS: e ele que fica sob os dedos ate o fim.
    _focoUs = estado.tempoDoX(foco.dx);
    HapticFeedback.selectionClick();
  }

  void _passo(ScaleUpdateDetails d) {
    switch (_modo) {
      case _Modo.scrub:
        // O CABECOTE E FIXO: arrastar o conteudo para a DIREITA traz o
        // passado para baixo dele — o tempo volta.
        estado.irPara(_vista0 - estado.usPorPx(d.localFocalPoint.dx - _x0));
      case _Modo.rolagem:
        if (!_rolagem.hasClients) return;
        final p = _rolagem.position;
        final alvo = (_rolagem0 - (d.localFocalPoint.dy - _y0)).clamp(
          p.minScrollExtent,
          p.maxScrollExtent,
        );
        if (alvo != p.pixels) _rolagem.jumpTo(alvo);
      case _Modo.pinca:
        if (d.pointerCount < 2) return;
        final base = _escala0 ??= d.scale;
        estado.zoom(_pps0 * d.scale / base);
        estado.irPara(
          _focoUs -
              (d.localFocalPoint.dx - estado.centro) / estado.pps.value * 1e6,
        );
      case _Modo.nenhum:
        break;
    }
  }

  void _fim(ScaleEndDetails d) {
    final modo = _modo;
    _modo = _Modo.nenhum;
    switch (modo) {
      case _Modo.scrub:
        final vx = d.velocity.pixelsPerSecond.dx;
        if (d.pointerCount == 0 && vx.abs() > kMinFlingVelocity) {
          // A INERCIA continua o mesmo gesto: a vista segue presa.
          _comecarInercia(vx);
        } else {
          estado.soltarVista();
        }
      case _Modo.rolagem:
        final vy = d.velocity.pixelsPerSecond.dy;
        final p = _rolagem.hasClients ? _rolagem.position : null;
        if (p is ScrollPositionWithSingleContext &&
            vy.abs() > kMinFlingVelocity) {
          p.goBallistic(-vy);
        }
      case _Modo.pinca:
        estado.soltarVista();
        // Os dedos que sobraram nao viram scrub de repente.
        if (d.pointerCount > 0) _ignorarResto = true;
      case _Modo.nenhum:
        break;
    }
  }

  void _comecarInercia(double vx) {
    _vistaDaInercia = estado.vistaUs.value;
    // O atrito do rolar do iOS: desliza e assenta, sem mola.
    _simulacao = FrictionSimulation(0.135, 0, vx);
    _inercia.start();
  }

  void _tiqueDaInercia(Duration t) {
    final s = _simulacao;
    if (s == null) return;
    final seg = t.inMicroseconds / 1e6;
    final antes = estado.vistaUs.value;
    estado.irPara(_vistaDaInercia - estado.usPorPx(s.x(seg)));
    final parou = estado.vistaUs.value == antes && seg > 0;
    if (s.isDone(seg) || parou) _pararInercia();
  }

  void _pararInercia() {
    if (_simulacao == null) return;
    _simulacao = null;
    if (_inercia.isActive) _inercia.stop();
    estado.soltarVista();
  }

  void _tocarNoVazio(TapUpDetails _) {
    if (ref.read(keyframesSelecionadosProvider).isNotEmpty) {
      ref.read(keyframesSelecionadosProvider.notifier).state = const {};
    }
    widget.aoTocarNoVazio?.call();
  }

  /// A RODA DO MOUSE (desktop, emulador): vertical rola as camadas,
  /// horizontal (ou com shift) anda no tempo.
  void _roda(PointerSignalEvent e) {
    if (e is! PointerScrollEvent) return;
    GestureBinding.instance.pointerSignalResolver.register(e, (ev) {
      final s = ev as PointerScrollEvent;
      final horizontal =
          s.scrollDelta.dx.abs() > s.scrollDelta.dy.abs() ||
          HardwareKeyboard.instance.isShiftPressed;
      if (horizontal) {
        final dx = s.scrollDelta.dx != 0 ? s.scrollDelta.dx : s.scrollDelta.dy;
        widget.playback.pause();
        estado.irPara(estado.vistaUs.value + estado.usPorPx(dx));
      } else if (_rolagem.hasClients) {
        final p = _rolagem.position;
        _rolagem.jumpTo(
          (p.pixels + s.scrollDelta.dy).clamp(
            p.minScrollExtent,
            p.maxScrollExtent,
          ),
        );
      }
    });
  }

  // =========================================================== as linhas

  Widget _linha(LinhaDaTimeline l) => _memo[l.chave] ??= l.ehCamada
      ? LinhaDaCamada(key: ValueKey(l.chave), layerId: l.layerId)
      : LinhaDaPropriedade(
          key: ValueKey(l.chave),
          layerId: l.layerId,
          chave: l.trilha!,
        );

  void _trocarLinhas(List<LinhaDaTimeline> linhas) {
    if (identical(linhas, _linhas)) return;
    _linhas = linhas;
    _indiceDaChave
      ..clear()
      ..addAll({for (var i = 0; i < linhas.length; i++) linhas[i].chave: i});
    // Quem saiu da lista sai do memo (senao ele cresce com a sessao).
    _memo.removeWhere((k, _) => !_indiceDaChave.containsKey(k));
  }

  /// A CAMADA ESCOLHIDA APARECE: escolher pelo palco (ou desfazer) nao
  /// pode deixar a linha dela escondida fora da lista. Com um painel
  /// aberto ([noTopo]) ela sobe para a primeira linha: o painel (200) cobre
  /// a parte de baixo da timeline, e o que sobra a vista e o topo.
  void _revelar(String? id, {bool noTopo = false}) {
    if (id == null) return;
    SchedulerBinding.instance.addPostFrameCallback((_) {
      if (!mounted || !_rolagem.hasClients) return;
      final i = _indiceDaChave['linha-$id'];
      if (i == null) return;
      final p = _rolagem.position;
      final topo = i * AureaDims.linhaDeCamada;
      final base = topo + AureaDims.linhaDeCamada;
      final alvo = noTopo || topo < p.pixels
          ? topo
          : base > p.pixels + p.viewportDimension
          ? base - p.viewportDimension
          : p.pixels;
      if (alvo != p.pixels) {
        _rolagem.jumpTo(alvo.clamp(p.minScrollExtent, p.maxScrollExtent));
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    SondaDaTimeline.buildsDaTimeline++;
    // A regua das reconstrucoes da casca (`Perfil3D`): um passo de slider
    // nao pode aparecer aqui.
    Perfil3D.contar('build.timeline');
    estado
      ..aoScrub = widget.aoScrub
      ..aoTocarNaCamada = widget.aoTocarNaCamada;
    final abertas = ref.watch(camadasExpandidasProvider);
    final estrutura = ref.watch(
      projetoVisivelProvider.select((p) => EstruturaDaTimeline.de(p, abertas)),
    );
    _trocarLinhas(estrutura.linhas);

    ref.listen<String?>(
      selectedLayerProvider,
      (_, id) => _revelar(id, noTopo: ref.read(painelAbertoProvider) != null),
    );
    ref.listen<PainelId?>(painelAbertoProvider, (antes, aberto) {
      // O PAINEL FECHOU: nenhuma propriedade em foco, tudo aceso de novo.
      if (aberto == null && ref.read(propriedadeAtivaProvider) != null) {
        ref.read(propriedadeAtivaProvider.notifier).state = null;
      }
      // O PAINEL ABRIU: a camada dele sobe para a linha que fica a vista.
      if (aberto != null && antes == null) {
        _revelar(ref.read(selectedLayerProvider), noTopo: true);
      }
    });

    final linhas = _linhas;
    return EscopoDaTimeline(
      estado: estado,
      child: ColoredBox(
        key: const ValueKey('timeline-nova'),
        color: AureaCores.cromo,
        child: LayoutBuilder(
          builder: (context, c) {
            estado.largura = c.maxWidth;
            final ajustes = MediaQuery.maybeGestureSettingsOf(context);
            // A FOLGA DO PAN DA RAIZ = a folga de um arrasto comum (a da
            // escala e o dobro): o scrub comeca junto com o arrasto do
            // clipe, e o mais fundo (o clipe) ganha o empate.
            final folga = ajustes?.touchSlop ?? kTouchSlop;
            return Listener(
              onPointerDown: _aoPousar,
              onPointerUp: _aoLevantar,
              onPointerCancel: _aoLevantar,
              onPointerSignal: _roda,
              child: RawGestureDetector(
                behavior: HitTestBehavior.opaque,
                gestures: {
                  ScaleGestureRecognizer:
                      GestureRecognizerFactoryWithHandlers<
                        ScaleGestureRecognizer
                      >(
                        () => ScaleGestureRecognizer(debugOwner: this),
                        (r) => r
                          ..gestureSettings = DeviceGestureSettings(
                            touchSlop: folga / 2,
                          )
                          ..onStart = _inicio
                          ..onUpdate = _passo
                          ..onEnd = _fim,
                      ),
                  TapGestureRecognizer:
                      GestureRecognizerFactoryWithHandlers<
                        TapGestureRecognizer
                      >(
                        () => TapGestureRecognizer(debugOwner: this),
                        (r) => r.onTapUp = _tocarNoVazio,
                      ),
                },
                child: Stack(
                  children: [
                    Column(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        ReguaDaTimeline(estado: estado),
                        Expanded(
                          child: linhas.isEmpty
                              ? Center(
                                  child: AppText(
                                    'Toque em + para adicionar a primeira camada',
                                    style: AureaEstilos.propriedade,
                                  ),
                                )
                              : ListView.builder(
                                  key: _chaveDaLista,
                                  controller: _rolagem,
                                  // QUEM ROLA E A RAIZ (o mesmo gesto que
                                  // decide entre scrub e rolagem); a lista
                                  // so obedece e da a inercia.
                                  physics: const NeverScrollableScrollPhysics(
                                    parent: ClampingScrollPhysics(),
                                  ),
                                  // O "+" (73) cobre a ponta de baixo: a
                                  // ultima linha pode subir acima dele.
                                  padding: const EdgeInsets.only(
                                    bottom: AureaDims.botaoAdicionar,
                                  ),
                                  itemExtent: AureaDims.linhaDeCamada,
                                  // FOLGA PEQUENA: duas linhas alem da tela.
                                  scrollCacheExtent:
                                      const ScrollCacheExtent.pixels(
                                        AureaDims.linhaDeCamada * 2,
                                      ),
                                  itemCount: linhas.length,
                                  // A linha arrastada (e a reordenada) mantem
                                  // o `State` — e o reconhecedor do dedo.
                                  findChildIndexCallback: (chave) =>
                                      chave is ValueKey<String>
                                      ? _indiceDaChave[chave.value]
                                      : null,
                                  itemBuilder: (context, i) =>
                                      _linha(linhas[i]),
                                ),
                        ),
                      ],
                    ),
                    Positioned(
                      left: 0,
                      right: 0,
                      top: AureaDims.regua,
                      bottom: 0,
                      child: CamadaDeGuias(estado: estado, lista: _rolagem),
                    ),
                    Positioned.fill(
                      child: CabecoteDaTimeline(
                        estado: estado,
                        playback: widget.playback,
                      ),
                    ),
                  ],
                ),
              ),
            );
          },
        ),
      ),
    );
  }
}
