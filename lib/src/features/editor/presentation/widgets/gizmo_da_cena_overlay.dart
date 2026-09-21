import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import 'package:aurea/src/core/l10n/app_language.dart';

import '../../application/editor_controller.dart';
import '../../application/interacao.dart';
import '../../domain/gizmo3d.dart';
import '../../domain/gizmo_da_cena3d.dart';
import '../../domain/layer.dart';
import '../../domain/scene3d.dart';
import '../../domain/video_project.dart';
import '../am/am_colors.dart';
import 'gizmo3d_painter.dart';
import 'preview_stage.dart' show cameraDaCena, cenaComNulosDaComposicao;

/// O GIZMO DO OBJETO 3D, SOBRE O PALCO.
///
/// ============================ POR QUE EXISTE ==========================
///
/// Ate aqui o unico gizmo que o palco desenhava era o da CAMADA: mover a
/// camada de cena em 2D e orbitar a camera. Nenhum gesto chegava a um
/// objeto DENTRO da cena — a API de no do controlador (`editSceneNodeProp`,
/// `toggleSceneNodeKeyframe`) existia, era testada, e nao tinha um unico
/// chamador na interface. Importar um modelo dava um objeto que so se
/// mexia pela folha, por numero.
///
/// NAO HA VIEWPORT NOVO, nem segunda previa, nem moldura: o preview
/// principal continua sendo o preview. Esta camada so pinta os eixos SOBRE
/// o pivo do objeto e recebe o dedo ali. Ela vive dentro do `Stack` da
/// composicao, em pixels de composicao — e por isso acompanha objeto,
/// camera e zoom sozinha.
///
/// O DEDO SO E ROUBADO ONDE HA ALCA: [_AreaDoGizmo] recusa o teste de
/// toque fora dos bracos, dos aneis e da alca de escala, entao arrastar o
/// palco, selecionar outra camada e a pinca continuam chegando ao gesto de
/// sempre. Uma area cheia que engolisse tudo seria o jeito rapido de
/// quebrar o editor inteiro para entregar um gizmo.

/// A FERRAMENTA NA MAO. Num celular os tres conjuntos juntos nao cabem no
/// dedo: as fichas sobre o palco escolhem um por vez.
enum ModoDoGizmo3D { mover, girar, escalar }

String rotuloDoModoDoGizmo(ModoDoGizmo3D m) => switch (m) {
  ModoDoGizmo3D.mover => 'Mover',
  ModoDoGizmo3D.girar => 'Girar',
  ModoDoGizmo3D.escalar => 'Escalar',
};

final modoDoGizmo3DProvider = StateProvider<ModoDoGizmo3D>(
  (ref) => ModoDoGizmo3D.mover,
);

/// O OBJETO DA CENA QUE ESTA SELECIONADO. Nulo = o primeiro da cena (uma
/// cena com um objeto so nao precisa de escolha).
final noDaCenaSelecionadoProvider = StateProvider<String?>((ref) => null);

/// O CABECOTE QUE O PALCO ESTA MOSTRANDO.
///
/// O menu da camada abre as folhas sem passar o relogio (`showCena3DSheet`
/// nao recebe o `PlaybackController`), e o Inspector precisa do instante
/// para por keyframe no lugar certo. Enquanto essa assinatura nao muda —
/// o arquivo e de outra frente nesta rodada — o palco publica aqui o que
/// esta desenhando, e a folha LE uma vez ao abrir. Ninguem escuta, entao
/// nao ha reconstrucao durante o build.
final ValueNotifier<Duration> cabecoteDoPalco = ValueNotifier(Duration.zero);

/// O OBJETO EM FOCO desta camada de cena, ja com o padrao resolvido.
String? noAtivoDaCena(Scene3D cena, String? escolhido) {
  final objetos = objetosDaCena(cena);
  if (objetos.isEmpty) return null;
  if (escolhido != null && objetos.any((n) => n.id == escolhido)) {
    return escolhido;
  }
  return noPadraoDaCena(cena);
}

class GizmoDaCenaOverlay extends ConsumerStatefulWidget {
  const GizmoDaCenaOverlay({
    super.key,
    required this.tempo,
    required this.escala,
  });

  /// O cabecote do palco.
  final ValueListenable<Duration> tempo;

  /// O fator composicao -> tela (o mesmo do `FittedBox` do palco).
  final double escala;

  @override
  ConsumerState<GizmoDaCenaOverlay> createState() => _GizmoDaCenaOverlayState();
}

class _GizmoDaCenaOverlayState extends ConsumerState<GizmoDaCenaOverlay> {
  // O GIZMO CONGELADO NO INICIO DO GESTO: recalculado a cada quadro ele
  // mudaria debaixo do dedo, e o eixo fugiria de quem o pegou.
  GizmoNaTela? _gizmoInicial;
  EixoDoGizmo? _eixo;
  EixoDoGizmo? _anel;
  bool _escalando = false;

  /// O BRACO PEGO ESTA ESTICANDO, E NAO ANDANDO. No modo Escalar os
  /// mesmos tres bracos existem, com a ponta em cubo: sem esta marca o
  /// arrasto cairia no ramo de mover e o objeto sairia do lugar.
  bool _escalaPorEixo = false;

  double _valorInicial = 0;
  double _giroAcumulado = 0;
  Offset _deltaAcumulado = Offset.zero;
  Offset _dedoAnterior = Offset.zero;
  double _distanciaInicial = 1;

  double get _escalaDoPalco => widget.escala <= 0 ? 1 : widget.escala;

  double get _braco => 86 / _escalaDoPalco;

  /// A FOLGA DO DEDO, em pixels de COMPOSICAO. 26 px de TELA e a metade
  /// do alvo minimo do app (`AureaTokens.minTap`): a alca e uma linha, e
  /// o que se mira e a faixa em volta dela.
  double get _folga => 26 / _escalaDoPalco;

  /// O ultimo cabecote ainda nao publicado. Um so agendamento por rajada:
  /// durante a reproducao o valor muda a cada quadro, e agendar uma
  /// chamada por quadro seria trabalho de graca.
  Duration? _cabecotePendente;

  void _publicarCabecote(Duration t) {
    final primeiro = _cabecotePendente == null;
    _cabecotePendente = t;
    if (!primeiro) return;
    WidgetsBinding.instance.addPostFrameCallback((_) {
      final v = _cabecotePendente;
      _cabecotePendente = null;
      if (v != null && cabecoteDoPalco.value != v) cabecoteDoPalco.value = v;
    });
  }

  /// O RAIO DO ANEL EM UNIDADES DA CENA (ver [Gizmo3DPainter.pixelsPorUnidade]):
  /// 118 px de tela viram px de composicao e depois unidades, para o anel
  /// ter o mesmo tamanho no dedo em qualquer distancia de camera.
  double _raioDoAnel(GizmoNaTela g) =>
      118 / _escalaDoPalco / (g.escala.abs() < 1e-6 ? 1 : g.escala);

  @override
  Widget build(BuildContext context) {
    return Positioned.fill(
      child: ValueListenableBuilder<Duration>(
        valueListenable: widget.tempo,
        builder: (context, tempo, _) {
          // O CABECOTE VIAJA DEPOIS DO QUADRO, e nao dentro dele.
          //
          // A ficha 3D ESCUTA [cabecoteDoPalco] para o losango seguir o
          // transporte. Publicar aqui dentro marcaria a ficha suja no meio
          // da construcao da arvore — o "setState() called during build",
          // que derruba o editor inteiro em vez de so atrasar um numero.
          if (cabecoteDoPalco.value != tempo) _publicarCabecote(tempo);
          final projeto = ref.watch(editorControllerProvider);
          final id = ref.watch(selectedLayerProvider);
          final camada = id == null ? null : projeto.layerById(id);
          if (camada is! Scene3DLayer || !camada.activeAt(tempo)) {
            return const SizedBox.shrink();
          }
          if (projeto.isHidden(camada.id)) return const SizedBox.shrink();
          final bloqueada = projeto.metaOf(camada.id).locked;

          final local = camada.localTime(tempo);
          final cena = cenaComNulosDaComposicao(projeto, camada, local, tempo);
          final camera = orbitarCamera(
            cameraDaCena(projeto, camada, local, tempo) ??
                camada.cameraAt(local),
            -camada.rotationX.valueAt(local),
            -camada.rotationY.valueAt(local),
          );
          final palco = Size(
            projeto.outputWidth.toDouble(),
            projeto.outputHeight.toDouble(),
          );
          final objetos = objetosDaCena(cena);
          if (objetos.isEmpty) return const SizedBox.shrink();
          final noId = noAtivoDaCena(
            cena,
            ref.watch(noDaCenaSelecionadoProvider),
          );
          if (noId == null) return const SizedBox.shrink();

          final gizmo = gizmoDoNo(
            projeto,
            camada,
            noId,
            tempo,
            palco,
            cena: cena,
            camera: camera,
          );
          if (gizmo == null) return const SizedBox.shrink();

          final modo = ref.watch(modoDoGizmo3DProvider);
          final g = _gizmoInicial ?? gizmo;

          return Stack(
            clipBehavior: Clip.none,
            children: [
              Positioned.fill(
                child: IgnorePointer(
                  child: CustomPaint(
                    key: const ValueKey('gizmo-da-cena'),
                    painter: Gizmo3DPainter(
                      gizmo: g,
                      escala: _escalaDoPalco,
                      comprimento: 86,
                      raio: 118,
                      eixoAtivo: _eixo,
                      anelAtivo: _anel,
                      ativo: !bloqueada,
                      // OS BRACOS APARECEM EM MOVER E EM ESCALAR: no
                      // segundo eles sao as alcas de escala POR EIXO (a
                      // ponta vira cubo). No modo Girar sairiam de graca
                      // por cima dos aneis e roubariam o dedo.
                      eixos: modo != ModoDoGizmo3D.girar,
                      aneis: modo == ModoDoGizmo3D.girar,
                      alcaDeEscala: modo == ModoDoGizmo3D.escalar,
                      pontaQuadrada: modo == ModoDoGizmo3D.escalar,
                      escalaEmUso: _escalando,
                      pixelsPorUnidade: g.escala,
                    ),
                  ),
                ),
              ),
              if (!bloqueada)
                Positioned.fill(
                  child: _AreaDoGizmo(
                    pega: (p) => _pega(
                      p,
                      gizmo,
                      modo,
                      projeto,
                      camada,
                      tempo,
                      palco,
                      cena,
                      camera,
                      objetos.length,
                    ),
                    child: GestureDetector(
                      key: const ValueKey('gizmo-da-cena-gesto'),
                      behavior: HitTestBehavior.opaque,
                      onTapUp: (d) => _escolherNo(
                        d.localPosition,
                        projeto,
                        camada,
                        tempo,
                        palco,
                        cena,
                        camera,
                      ),
                      onPanStart: (d) => _comecar(
                        d.localPosition,
                        gizmo,
                        modo,
                        camada,
                        noId,
                        local,
                        cena,
                      ),
                      onPanUpdate: (d) => _andar(
                        d,
                        camada.id,
                        noId,
                        tempo,
                      ),
                      onPanEnd: (_) => _soltar(),
                      onPanCancel: _soltar,
                    ),
                  ),
                ),
              _fichasDeModo(modo),
            ],
          );
        },
      ),
    );
  }

  /// AS FICHAS Mover | Girar | Escalar, no pe do palco.
  ///
  /// Sao as MESMAS pilulas do painel de Efeitos — fundo [AmColors.chip],
  /// escolhida em [AmColors.accentDim] com o texto em [AmColors.accent].
  /// Nao ha componente novo aqui, so o grupo dentro de uma capsula, que e
  /// o que separa a ferramenta do video atras dela.
  ///
  /// Desenhadas em pixels de composicao e DESESCALADAS: sem isso elas
  /// encolheriam com o zoom do palco ate nao caberem no dedo, que e
  /// justamente o defeito que este gizmo veio corrigir. Cada ficha tem 44
  /// px de altura — o alvo minimo do app.
  Widget _fichasDeModo(ModoDoGizmo3D modo) => Align(
    alignment: Alignment.bottomCenter,
    child: Transform.scale(
      scale: 1 / _escalaDoPalco,
      alignment: Alignment.bottomCenter,
      child: Padding(
        padding: const EdgeInsets.only(bottom: 12),
        child: Container(
          padding: const EdgeInsets.all(4),
          decoration: BoxDecoration(
            color: AmColors.panel.withValues(alpha: 0.86),
            borderRadius: BorderRadius.circular(16),
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              for (final m in ModoDoGizmo3D.values)
                Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 2),
                  child: Semantics(
                    button: true,
                    selected: m == modo,
                    label: rotuloDoModoDoGizmo(m),
                    child: GestureDetector(
                      key: ValueKey('gizmo-modo-${m.name}'),
                      behavior: HitTestBehavior.opaque,
                      onTap: () =>
                          ref.read(modoDoGizmo3DProvider.notifier).state = m,
                      child: Container(
                        height: 44,
                        alignment: Alignment.center,
                        padding: const EdgeInsets.symmetric(horizontal: 16),
                        decoration: BoxDecoration(
                          color: m == modo
                              ? AmColors.accentDim
                              : AmColors.chip,
                          borderRadius: BorderRadius.circular(12),
                        ),
                        child: AppText(
                          rotuloDoModoDoGizmo(m),
                          style: TextStyle(
                            color: m == modo
                                ? AmColors.accent
                                : AmColors.text,
                            fontSize: 13,
                            fontWeight: FontWeight.w600,
                          ),
                        ),
                      ),
                    ),
                  ),
                ),
            ],
          ),
        ),
      ),
    ),
  );

  /// O DEDO CAIU EM ALGUMA ALCA? (ou no pivo de outro objeto, quando ha
  /// mais de um para escolher).
  bool _pega(
    Offset p,
    GizmoNaTela gizmo,
    ModoDoGizmo3D modo,
    VideoProject projeto,
    Scene3DLayer camada,
    Duration tempo,
    Size palco,
    Scene3D cena,
    RenderCamera camera,
    int quantosObjetos,
  ) {
    final folga = _folga;
    final pegou = switch (modo) {
      ModoDoGizmo3D.mover =>
        eixoNoDedo(gizmo, p, _braco, tolerancia: folga) != null,
      ModoDoGizmo3D.girar =>
        anelNoDedo(gizmo, p, _raioDoAnel(gizmo), tolerancia: folga) != null,
      // ESCALAR TEM QUATRO ALVOS: a alca branca da uniforme e os tres
      // bracos de eixo.
      ModoDoGizmo3D.escalar =>
        (p - pontoDaAlcaDeEscala(gizmo, 86, _escalaDoPalco)).distance <=
                folga ||
            eixoNoDedo(gizmo, p, _braco, tolerancia: folga) != null,
    };
    if (pegou) return true;
    if (quantosObjetos < 2) return false;
    final outro = noMaisPertoDoDedo(
      projeto,
      camada,
      tempo,
      palco,
      p,
      cena: cena,
      camera: camera,
      raio: 30 / _escalaDoPalco,
    );
    return outro != null;
  }

  void _escolherNo(
    Offset p,
    VideoProject projeto,
    Scene3DLayer camada,
    Duration tempo,
    Size palco,
    Scene3D cena,
    RenderCamera camera,
  ) {
    final alvo = noMaisPertoDoDedo(
      projeto,
      camada,
      tempo,
      palco,
      p,
      cena: cena,
      camera: camera,
      raio: 30 / _escalaDoPalco,
    );
    if (alvo != null) {
      ref.read(noDaCenaSelecionadoProvider.notifier).state = alvo;
    }
  }

  void _comecar(
    Offset dedo,
    GizmoNaTela gizmo,
    ModoDoGizmo3D modo,
    Scene3DLayer camada,
    String noId,
    Duration local,
    Scene3D cena,
  ) {
    _eixo = null;
    _anel = null;
    _escalando = false;
    _escalaPorEixo = false;
    // O GIZMO CONGELADO DE UM GESTO ANTERIOR NAO PODE SOBRAR: ele e o que
    // o pintor usa enquanto o dedo esta em cima, e um gizmo velho deixaria
    // os eixos parados enquanto o objeto anda.
    _gizmoInicial = null;
    final no = cena.nodeById(noId);
    if (no == null) return;
    final c = ref.read(editorControllerProvider.notifier);
    final folga = _folga;

    switch (modo) {
      case ModoDoGizmo3D.mover:
        final e = eixoNoDedo(gizmo, dedo, _braco, tolerancia: folga);
        if (e == null) return;
        _eixo = e;
        _valorInicial = c.sceneNodeValueAt(no, _propDeMover(e), local);
      case ModoDoGizmo3D.girar:
        final a = anelNoDedo(
          gizmo,
          dedo,
          _raioDoAnel(gizmo),
          tolerancia: folga,
        );
        if (a == null) return;
        _anel = a;
        _valorInicial = c.sceneNodeValueAt(no, _propDeGirar(a), local);
      case ModoDoGizmo3D.escalar:
        final alca = pontoDaAlcaDeEscala(gizmo, 86, _escalaDoPalco);
        if ((dedo - alca).distance <= folga) {
          _escalando = true;
          _valorInicial = c.sceneNodeValueAt(no, PropDoNo.escala, local);
          // A DISTANCIA DE PARTIDA NUNCA E ZERO: a razao dedo/inicio e o
          // fator de escala, e dividir por zero mandaria o objeto para o
          // infinito no primeiro pixel.
          _distanciaInicial = (dedo - gizmo.origem).distance.clamp(
            1.0,
            double.infinity,
          );
        } else {
          // ESCALA POR EIXO: o braco X estica so o X do objeto. O fator e
          // a razao entre onde o dedo esta AO LONGO DO EIXO e onde ele
          // comecou — a mesma mao de toda ferramenta 3D, e a unica que
          // nao inverte quando a camera passa para o outro lado.
          final e = eixoNoDedo(gizmo, dedo, _braco, tolerancia: folga);
          if (e == null) return;
          _eixo = e;
          _escalaPorEixo = true;
          _valorInicial = c.sceneNodeValueAt(no, _propDeEscalar(e), local);
          _distanciaInicial = _aoLongoDoEixo(
            gizmo,
            e,
            dedo,
          ).abs().clamp(12 / _escalaDoPalco, double.infinity);
        }
    }

    _gizmoInicial = gizmo;
    _deltaAcumulado = Offset.zero;
    _giroAcumulado = 0;
    _dedoAnterior = dedo;
    c.beginGesture();
    Interacao.marcar();
    setState(() {});
  }

  void _andar(
    DragUpdateDetails d,
    String camadaId,
    String noId,
    Duration tempo,
  ) {
    final g = _gizmoInicial;
    if (g == null) return;
    final c = ref.read(editorControllerProvider.notifier);
    Interacao.marcar();

    final eixo = _eixo;
    if (eixo != null && _escalaPorEixo) {
      final agora = _aoLongoDoEixo(g, eixo, d.localPosition);
      final fator = agora / _distanciaInicial;
      c.editSceneNodeProp(
        camadaId,
        noId,
        _propDeEscalar(eixo),
        tempo,
        (_valorInicial * fator).clamp(0.01, 100.0),
      );
      return;
    }
    if (eixo != null) {
      // O DELTA E ACUMULADO DESDE O INICIO: o valor vem sempre de
      // `inicial + total`, e um evento perdido nao deixa erro permanente.
      _deltaAcumulado += d.delta;
      final passo = avancoNoEixo(g.direcao(eixo), _deltaAcumulado);
      c.editSceneNodeProp(
        camadaId,
        noId,
        _propDeMover(eixo),
        tempo,
        _valorInicial + passo,
      );
      return;
    }

    final anel = _anel;
    if (anel != null) {
      _giroAcumulado +=
          giroEntre(g.origem, _dedoAnterior, d.localPosition) *
          sinalDoGiro(g, anel);
      _dedoAnterior = d.localPosition;
      c.editSceneNodeProp(
        camadaId,
        noId,
        _propDeGirar(anel),
        tempo,
        _valorInicial + _giroAcumulado,
      );
      return;
    }

    if (_escalando) {
      final agora = (d.localPosition - g.origem).distance;
      final fator = agora / _distanciaInicial;
      c.editSceneNodeProp(
        camadaId,
        noId,
        PropDoNo.escala,
        tempo,
        (_valorInicial * fator).clamp(0.01, 100.0),
      );
    }
  }

  void _soltar() {
    if (_eixo == null && _anel == null && !_escalando) return;
    _eixo = null;
    _anel = null;
    _escalando = false;
    _escalaPorEixo = false;
    _gizmoInicial = null;
    ref.read(editorControllerProvider.notifier).endGesture();
    Interacao.soltar();
    if (mounted) setState(() {});
  }

  PropDoNo _propDeMover(EixoDoGizmo e) => switch (e) {
    EixoDoGizmo.x => PropDoNo.x,
    EixoDoGizmo.y => PropDoNo.y,
    EixoDoGizmo.z => PropDoNo.z,
  };

  PropDoNo _propDeGirar(EixoDoGizmo e) => switch (e) {
    EixoDoGizmo.x => PropDoNo.giroX,
    EixoDoGizmo.y => PropDoNo.giroY,
    EixoDoGizmo.z => PropDoNo.giroZ,
  };

  PropDoNo _propDeEscalar(EixoDoGizmo e) => switch (e) {
    EixoDoGizmo.x => PropDoNo.escalaX,
    EixoDoGizmo.y => PropDoNo.escalaY,
    EixoDoGizmo.z => PropDoNo.escalaZ,
  };

  /// ONDE O DEDO ESTA AO LONGO DO EIXO, em pixels de composicao a partir
  /// do pivo. So a componente NO eixo conta: mover de lado nao estica.
  static double _aoLongoDoEixo(GizmoNaTela g, EixoDoGizmo e, Offset dedo) {
    final d = g.direcao(e);
    final modulo = d.distance;
    if (modulo < 1e-6) return 0;
    final rel = dedo - g.origem;
    return (rel.dx * d.dx + rel.dy * d.dy) / modulo;
  }
}

/// A AREA QUE SO EXISTE ONDE HA ALCA.
///
/// Um `GestureDetector` de tela cheia por cima do palco engoliria TODO
/// gesto: arrastar a camada, selecionar outra, a pinca de zoom. Aqui o
/// teste de toque pergunta antes se aquele ponto tem alca — e onde nao
/// tem, o dedo desce como se esta camada nao existisse.
class _AreaDoGizmo extends SingleChildRenderObjectWidget {
  const _AreaDoGizmo({required this.pega, super.child});

  final bool Function(Offset) pega;

  @override
  RenderObject createRenderObject(BuildContext context) =>
      _RenderAreaDoGizmo(pega);

  @override
  void updateRenderObject(BuildContext context, _RenderAreaDoGizmo r) {
    r.pega = pega;
  }
}

class _RenderAreaDoGizmo extends RenderProxyBox {
  _RenderAreaDoGizmo(this.pega);

  bool Function(Offset) pega;

  @override
  bool hitTest(BoxHitTestResult result, {required Offset position}) {
    if (!pega(position)) return false;
    return super.hitTest(result, position: position);
  }
}
