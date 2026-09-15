import 'essential_warp_pass.dart';
import 'composition_frame.dart';

import 'dart:io';
import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/foundation.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import 'caption_highlight_painter.dart';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:video_player/video_player.dart';

import '../../application/texture_cache.dart';
import '../../application/blob_track_service.dart';
import '../../application/editor_controller.dart';
import '../../application/freehand_session.dart' show onionSkinProvider;
export '../../application/freehand_session.dart' show onionSkinProvider;
import '../../application/playback_controller.dart';
import '../../application/preview_stats.dart';
import '../../application/video_layer_manager.dart';
import '../shell/cromo_editor.dart' show zoomDoPalcoProvider;
import '../../domain/ajuste_da_midia.dart';
import '../../domain/keyframe.dart' show AnimatedDouble;
import '../../domain/cut.dart';
import '../../domain/cut_ops.dart';
import '../../domain/effect.dart';
import '../../domain/fx.dart';
import '../../domain/oscillate.dart';
import 'motion_tile_pass.dart';
import '../../domain/gear.dart';
import '../../domain/text_animator.dart' show valueNoise01;
import '../../domain/caption_highlight.dart';
import '../../domain/grid_rig.dart';
import '../../domain/layer.dart';
import '../../domain/layer_meta.dart';
import '../../domain/mask.dart';
import '../../domain/camera3d.dart';
import '../../domain/scene3d.dart';
import '../../domain/shape.dart';
import '../../domain/selection_geometry.dart';
import '../../domain/video_project.dart';
import 'animated_text.dart';
import 'blend_mask.dart';
import 'gradient4_painter.dart';
import 'custom_blend.dart';
import 'linear_light.dart';
import 'pixel_effect_engine.dart';
import '../../domain/pixel_effect.dart';
import '../../domain/bloom.dart';
import '../../domain/coloring.dart';
import '../../domain/efeitos_do_after.dart';
import '../../domain/one_frame.dart';
import '../../domain/time_slice.dart';
import '../../application/quadros_de_video.dart';
import '../../application/proxy_service.dart';
import '../../domain/color_space.dart';
import 'mask_node_editor.dart';
import 'world3d_painter.dart';
import 'extrude_painter.dart';
import 'vignette_painter.dart';
import 'freehand_overlay.dart';
import '../../application/mesh_cache.dart';
import 'masked_box.dart';
import 'dither_layer.dart';
import 'preview_raster.dart';
import '../../application/ui/preview_resolution.dart';
import '../../application/ui/opcoes_de_visualizacao.dart';
import 'fx_lote2.dart';
import 'particles_painter.dart';
import '../../application/scene3d_gpu.dart';
import 'scene3d_painter.dart';
import 'scene3d_gpu_view.dart';
import 'package:aurea/src/core/l10n/app_language.dart';

// Photos decode asynchronously and videos update their external textures.
// An automatic snapshot of either can retain a placeholder/previous frame.
// Keep imported media on the live compositor, including inside precomps.
bool _containsRasterMedia(List<Layer> layers) => layers.any(
  (layer) =>
      layer is ImageLayer ||
      layer is VideoLayer ||
      (layer is GroupLayer && _containsRasterMedia(layer.children)),
);

/// Palco: composicao renderizada em coordenadas logicas, escalada para
/// caber. Gestos editam a camada selecionada.
/// A alca que o dedo pegou: canto de baixo e a direita redimensiona,
/// canto de cima e a direita gira.
enum _Alca { escala, giro }

class PreviewStage extends ConsumerStatefulWidget {
  const PreviewStage({super.key, required this.playback, required this.videos});

  final PlaybackController playback;
  final VideoLayerManager videos;

  @override
  ConsumerState<PreviewStage> createState() => _PreviewStageState();
}

class _PreviewStageState extends ConsumerState<PreviewStage> {
  @override
  void initState() {
    super.initState();
    widget.playback.playing.addListener(_playbackChanged);
  }

  @override
  void didUpdateWidget(PreviewStage oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.playback != widget.playback) {
      oldWidget.playback.playing.removeListener(_playbackChanged);
      widget.playback.playing.addListener(_playbackChanged);
    }
  }

  void _playbackChanged() {
    if (mounted) setState(() {});
  }

  @override
  void dispose() {
    widget.playback.playing.removeListener(_playbackChanged);
    super.dispose();
  }

  double _startScale = 1;
  double _startRotation = 0;
  double _stageScale = 1;

  /// Canto de cima e da esquerda do quadro da composicao dentro do
  /// palco: com ele o toque na tela vira ponto na composicao.
  Offset _stageOrigin = Offset.zero;

  /// O tamanho da area do palco, para prender as alcas dentro dela.
  Size _tamanhoDoPalco = Size.zero;

  /// A alca que o dedo pegou neste gesto (null = arrastar a camada).
  _Alca? _alca;
  Offset _dragStartPos = Offset.zero;

  /// O pivo no inicio do gesto: e em torno dele (posicao + pivo) que as
  /// alcas giram e escalam. No grupo ele fica no centro dos filhos.
  Offset _dragStartPivot = Offset.zero;
  Offset _dragAccum = Offset.zero;

  /// O ponto tocado, em coordenadas da COMPOSICAO.
  Offset _naComposicao(Offset local) =>
      (local - _stageOrigin) / (_stageScale <= 0 ? 1 : _stageScale);

  /// A camada mais de cima cujo quadro contem o ponto (null = nenhuma).
  ///
  /// E o que faltava para o app parecer um editor: o testador toca no
  /// objeto na tela e ele fica selecionado, sem precisar descobrir que
  /// a barrinha da timeline e que seleciona.
  String? _camadaNoPonto(Offset comp) {
    final project = ref.read(editorControllerProvider);
    final controller = ref.read(editorControllerProvider.notifier);
    final t = widget.playback.time.value;
    final ordered = depthSortPaintOrder(
      project.layers.reversed.toList(),
      t,
      project: project,
    ).reversed;
    for (final l in [
      ...ordered.where((l) => l is! NullLayer),
      ...ordered.whereType<NullLayer>(),
    ]) {
      // Nulos ficam por ultimo: o gizmo pode ser escolhido sem bloquear midia.
      if (l is AudioLayer || l is AdjustmentLayer) continue;
      if (!l.activeAt(t)) continue;
      if (project.isHidden(l.id)) continue;
      if (project.metaOf(l.id).locked) continue;
      // A caixa do grupo envolve os filhos (e nao a composicao inteira):
      // tocar no vazio ao lado do conteudo nao pega o grupo.
      final caixa = controller.layerBoxRect(l, t, scaled: false);
      if (caixa.isEmpty) continue;
      final matrix = selectionTransform(project, l, t);
      final inverse = Matrix4.tryInvert(matrix);
      if (inverse == null) continue;
      final p = MatrixUtils.transformPoint(inverse, comp);
      // Uma folga de 12 px: alvo pequeno tambem tem de dar para pegar.
      if (caixa.contains(p)) return l.id;
      final nearest = Offset(
        p.dx.clamp(caixa.left, caixa.right),
        p.dy.clamp(caixa.top, caixa.bottom),
      );
      if ((MatrixUtils.transformPoint(matrix, nearest) - comp).distance *
              _stageScale <=
          12) {
        return l.id;
      }
    }
    return null;
  }

  /// Onde ficam as alcas da selecao, em coordenadas do PALCO.
  ({Offset escala, Offset giro, Rect quadro})? _alcasDaSelecao() {
    final id = ref.read(selectedLayerProvider);
    if (id == null) return null;
    final project = ref.read(editorControllerProvider);
    final l = project.layerById(id);
    if (l == null || l is AudioLayer) return null;
    final t = widget.playback.time.value;
    if (!l.activeAt(t)) return null;
    final matrix = selectionTransform(project, l, t);
    // Camada que passou da camera nao aparece: sem alcas soltas no canto.
    if (matrix.storage.every((v) => v == 0)) return null;
    final caixa = ref
        .read(editorControllerProvider.notifier)
        .layerBoxRect(l, t, scaled: false);
    if (caixa.isEmpty) return null;
    final centro =
        _stageOrigin +
        MatrixUtils.transformPoint(matrix, caixa.center) * _stageScale;

    // AS DUAS ALCAS NUNCA ENCOSTAM UMA NA OUTRA.
    //
    // Elas ficam nos cantos de cima e de baixo da selecao. Num objeto
    // pequeno — ou num palco baixo, que e o caso desde que a altura do
    // preview passou a sair da proporcao da composicao — esses dois
    // cantos caem a poucos pixels de distancia. Como quem procura a alca
    // testa a de ESCALA primeiro, ela ganhava as duas, e girar deixava
    // de existir: era o "a rotacao nao gira" do relato, agora por outro
    // caminho. Um afastamento minimo em pixels DE TELA resolve, e nao
    // mexe em objeto grande, onde os cantos ja estao longe.
    const afastamentoMinimo = 30.0;

    // AS ALCAS FICAM DENTRO DO PALCO, sempre.
    //
    // Um objeto maior que o quadro empurra o canto para fora da area
    // visivel, e ali a alca nao existe para o dedo. Presas na borda, com
    // margem para o circulo caber, continuam onde se espera.
    final palco = Offset.zero & _tamanhoDoPalco;
    Offset presa(Offset p) => palco.isEmpty
        ? p
        : Offset(
            p.dx.clamp(palco.left + 22, palco.right - 22),
            p.dy.clamp(palco.top + 22, palco.bottom - 22),
          );

    Offset noPalco(Offset canto) {
      var delta =
          (MatrixUtils.transformPoint(matrix, canto) * _stageScale +
              _stageOrigin) -
          centro;
      if (delta.distance < afastamentoMinimo && delta.distance > 1e-6) {
        delta *= afastamentoMinimo / delta.distance;
      }
      return presa(centro + delta);
    }

    var escala = noPalco(caixa.bottomRight);
    var giro = noPalco(caixa.topRight);
    if ((escala - giro).distance < 60) {
      final middle = (escala + giro) / 2;
      escala = presa(middle + const Offset(0, 30));
      giro = presa(middle - const Offset(0, 30));
    }
    return (
      escala: escala,
      giro: giro,
      quadro: MatrixUtils.transformRect(matrix, caixa),
    );
  }

  /// ONDE O ENCAIXE PEGOU, em coordenadas da composicao. Nulo = solto.
  ///
  /// Sao estes dois numeros que a linha de apoio desenha. Ela existia
  /// antes como uma cruz permanente no meio do quadro, ligada so ao
  /// fato de haver camada selecionada — e ai o beta relatou o que era
  /// inevitavel: "passa o dedo por cima e para de mexer". A linha nao
  /// parava nada (ela e desenhada dentro de um IgnorePointer), quem
  /// parava era o ENCAIXE, invisivel, agarrando o objeto ao passar pelo
  /// centro. Duas coisas erradas de uma vez: uma marca que nao explicava
  /// nada e uma forca que nao aparecia.
  ///
  /// Agora a linha SO existe enquanto o dedo esta movendo o objeto, e so
  /// no eixo em que ele de fato encaixou. Ver a linha aparecer no
  /// instante em que o objeto para e o que transforma "travou" em
  /// "alinhou".
  double? _encaixeX;
  double? _encaixeY;

  /// O DESLOCAMENTO DO PALCO COM ZOOM (trilho da direita): arrastar o
  /// vazio com o palco aproximado passeia pela composicao. Zera quando
  /// o zoom volta ao ajustado.
  Offset _panDoPalco = Offset.zero;

  /// O zoom do palco quando a pinca comecou (dois dedos no vazio).
  double _zoomNoInicioDaPinca = 1.0;

  /// TOQUE DUPLO COM DOIS DEDOS: os dedos no palco, onde desceram, se
  /// andaram, e quando foi o ultimo toque de dois dedos.
  final Map<int, Offset> _dedosNoPalco = {};
  Duration? _desceramOsDois;
  bool _osDoisAndaram = false;
  Duration? _ultimoToqueDeDois;

  void _dedoDesceu(PointerDownEvent e) {
    _dedosNoPalco[e.pointer] = e.position;
    if (_dedosNoPalco.length == 2) {
      _desceramOsDois = e.timeStamp;
      _osDoisAndaram = false;
    } else if (_dedosNoPalco.length > 2) {
      _desceramOsDois = null;
    }
  }

  void _dedoAndou(PointerMoveEvent e) {
    final origem = _dedosNoPalco[e.pointer];
    if (origem != null && (e.position - origem).distance > 12) {
      _osDoisAndaram = true;
    }
  }

  void _dedoSubiu(PointerEvent e) {
    final eramDois = _dedosNoPalco.length == 2;
    _dedosNoPalco.remove(e.pointer);
    final desceram = _desceramOsDois;
    if (!eramDois || desceram == null) return;
    _desceramOsDois = null;
    if (_osDoisAndaram ||
        e.timeStamp - desceram > const Duration(milliseconds: 300)) {
      return;
    }
    final anterior = _ultimoToqueDeDois;
    if (anterior != null &&
        e.timeStamp - anterior <= const Duration(milliseconds: 450)) {
      _ultimoToqueDeDois = null;
      final zoom = ref.read(zoomDoPalcoProvider.notifier);
      zoom.state = zoom.state == 1.0 ? 2.0 : 1.0;
    } else {
      _ultimoToqueDeDois = e.timeStamp;
    }
  }

  /// O NUMERO QUE O DEDO ESTA MUDANDO, na barra de informacoes.
  void _informar(String id) {
    final camada = ref.read(editorControllerProvider).layerById(id);
    if (camada == null) return;
    final local = camada.localTime(widget.playback.time.value);
    final pos = camada.position.valueAt(local);
    final escala = camada.scaleX.valueAt(local);
    final giro = camada.rotation.valueAt(local);
    ref.read(infobarProvider.notifier).state = DadosDaInfobar.pares([
      ('X', pos.dx.toStringAsFixed(0)),
      ('Y', pos.dy.toStringAsFixed(0)),
      ('Escala', '${(escala * 100).toStringAsFixed(0)}%'),
      ('Rotação', '${giro.toStringAsFixed(1)}°'),
    ]);
  }

  void _limparInfobar() {
    if (ref.read(infobarProvider) != null) {
      ref.read(infobarProvider.notifier).state = null;
    }
  }

  void _limparEncaixe() {
    if (_encaixeX == null && _encaixeY == null) return;
    setState(() {
      _encaixeX = null;
      _encaixeY = null;
    });
  }

  void _onScaleStart(ScaleStartDetails d) {
    _zoomNoInicioDaPinca = ref.read(zoomDoPalcoProvider);
    // O dedo pegou uma alca? (raio generoso: 28 px)
    _alca = null;
    final alcas = _alcasDaSelecao();
    if (alcas != null && d.pointerCount < 2) {
      if ((d.localFocalPoint - alcas.escala).distance <= 28) {
        _alca = _Alca.escala;
      } else if ((d.localFocalPoint - alcas.giro).distance <= 28) {
        _alca = _Alca.giro;
      }
    }
    // Tocou fora de qualquer alca: seleciona o que estiver embaixo do
    // dedo (e nada, se for o vazio).
    if (_alca == null && d.pointerCount < 2) {
      final alvo = _camadaNoPonto(_naComposicao(d.localFocalPoint));
      // MODO SELECIONAR: tocar na camada marca e desmarca; o palco nao
      // arrasta nada enquanto se escolhe.
      if (ref.read(modoSelecionarProvider)) {
        _selecionandoNoPalco = true;
        if (alvo != null) {
          final r = alternarNaSelecao(
            ref.read(multiSelectProvider),
            ref.read(selectedLayerProvider),
            alvo,
          );
          ref.read(multiSelectProvider.notifier).state = r.multi;
          ref.read(selectedLayerProvider.notifier).state = r.principal;
        }
        return;
      }
      _selecionandoNoPalco = false;
      final atual = ref.read(selectedLayerProvider);
      if (alvo != atual) {
        ref.read(multiSelectProvider.notifier).state = const {};
        ref.read(selectedLayerProvider.notifier).state = alvo;
      }
    }
    final id = ref.read(selectedLayerProvider);
    if (id == null) return;
    final layer = ref.read(editorControllerProvider).layerById(id);
    if (layer == null) return;
    final t = layer.localTime(widget.playback.time.value);
    _startScale = layer.scaleX.valueAt(t);
    _startRotation = layer.rotation.valueAt(t);
    _dragStartPos = layer.position.valueAt(t);
    _dragStartPivot = layer.pivot.valueAt(t);
    _dragAccum = Offset.zero;
  }

  /// O toque atual comecou no modo Selecionar: nao move camada nenhuma.
  bool _selecionandoNoPalco = false;

  void _onScaleUpdate(ScaleUpdateDetails d) {
    if (_selecionandoNoPalco) return;
    final id = ref.read(selectedLayerProvider);
    // COM ZOOM E NADA SELECIONADO, o arrasto passeia pelo palco.
    if (id == null) {
      final zoom = ref.read(zoomDoPalcoProvider);
      // DOIS DEDOS NO VAZIO: a pinca aproxima e o par de dedos passeia,
      // como numa foto. Perto de 100% o palco gruda no ajustado.
      if (d.pointerCount >= 2) {
        var novo = (_zoomNoInicioDaPinca * d.scale).clamp(0.25, 4.0);
        if ((novo - 1.0).abs() < 0.04) novo = 1.0;
        if (novo != zoom) {
          ref.read(zoomDoPalcoProvider.notifier).state = novo;
        }
        if (novo != 1.0) {
          setState(() => _panDoPalco += d.focalPointDelta);
        }
        return;
      }
      if (zoom != 1.0 && d.pointerCount == 1) {
        setState(() => _panDoPalco += d.focalPointDelta);
      }
      return;
    }
    final controller = ref.read(editorControllerProvider.notifier);
    final project = ref.read(editorControllerProvider);
    final t = widget.playback.time.value;

    if (d.pointerCount >= 2) {
      controller.editScaleUniform(
        id,
        t,
        (_startScale * d.scale).clamp(0.05, 8.0),
      );
      controller.editRotation(
        id,
        t,
        _startRotation + d.rotation * 180 / math.pi,
      );
      _informar(id);
      return;
    }

    // ALCAS: um dedo so, mas nao arrasta a camada — redimensiona ou gira.
    if (_alca != null) {
      final centro =
          _stageOrigin + (_dragStartPos + _dragStartPivot) * _stageScale;
      final v = d.localFocalPoint - centro;
      if (_alca == _Alca.giro) {
        final ang = math.atan2(v.dy, v.dx) * 180 / math.pi;
        // O canto de cima e a direita comeca a 45 graus do centro.
        controller.editRotation(id, t, ang + 45);
        _informar(id);
      } else {
        final camada = project.layerById(id);
        if (camada != null) {
          // A CAIXA SEM ESCALA, senao a conta briga consigo mesma.
          //
          // A escala nova e "distancia do dedo ao centro dividida pela
          // meia diagonal da caixa". Com a caixa JA ESCALADA, a meia
          // diagonal cresce junto com a escala que acabou de ser
          // aplicada: o proximo evento divide por um numero maior e
          // devolve uma escala menor, que encolhe a caixa, que devolve
          // uma escala maior... O objeto tremia entre dois tamanhos e
          // nunca acompanhava o dedo — era o "da ghost no zoom e nao da
          // zoom de verdade" do beta. Contra a caixa base, a escala e
          // funcao so de onde o dedo esta.
          final caixa = controller.layerBoxSize(camada, t, scaled: false);
          final meia = math.max(
            1.0,
            Offset(caixa.width / 2, caixa.height / 2).distance,
          );
          controller.editScaleUniform(
            id,
            t,
            (v.distance / _stageScale / meia).clamp(0.05, 8.0),
          );
          _informar(id);
        }
      }
      return;
    }
    final deltaLogical = d.focalPointDelta / _stageScale;
    if (deltaLogical == Offset.zero) return;
    _dragAccum += deltaLogical;

    // ALINHAMENTO: enquanto o dedo anda so num eixo, o outro fica quieto
    // — o arrasto nao "sai torto" ao comecar.
    //
    // O CRITERIO E ABSOLUTO, e nao uma proporcao. Antes bastava um eixo
    // ser 2,5 vezes maior que o outro: depois de arrastar duzentos
    // pixels para o lado, era preciso descer OITENTA para o objeto voltar
    // a subir e descer. No meio do gesto isso e indistinguivel de um
    // travamento, e foi parte do que o beta chamou de "para de mexer".
    // Com um limite em pixels, o eixo se solta assim que a pessoa move
    // de verdade para o outro lado, nao importa o quanto ja tenha
    // andado.
    const folgaDoEixo = 12.0;
    var target = _dragStartPos + _dragAccum;
    final adx = _dragAccum.dx.abs();
    final ady = _dragAccum.dy.abs();
    if (adx > 24 && ady < folgaDoEixo) {
      target = Offset(target.dx, _dragStartPos.dy);
    } else if (ady > 24 && adx < folgaDoEixo) {
      target = Offset(_dragStartPos.dx, target.dy);
    }

    // ENCAIXE (PR-X2): centro e bordas da composicao, centros e bordas
    // das OUTRAS camadas, e as guias. O primeiro alvo dentro da
    // tolerancia vence, por eixo.
    // DEZ PIXELS DE TELA, e nao dezesseis. A tolerancia e uma zona morta:
    // dentro dela o objeto fica parado enquanto o dedo anda. Dezesseis
    // para cada lado davam trinta e dois pixels de "nao acontece nada" —
    // um terco de polegada em que o app parece quebrado. Dez ainda pega
    // o alinhamento e some antes de virar queixa.
    final snap = 10 / _stageScale;

    // QUEM PASSA CORRENDO NAO ESTA MIRANDO.
    //
    // Esta e a metade que faltava do relato "passa o dedo por cima e
    // para de mexer". Quem arrasta depressa de um lado ao outro nao quer
    // alinhar nada — quer chegar do outro lado — e o encaixe agarrando
    // no meio do caminho e puro estorvo. Quem quer alinhar chega devagar.
    //
    // O limite e a propria tolerancia: se o dedo atravessa a zona
    // inteira num unico evento, ele estava passando, e nao mirando.
    // Assim a regra se ajusta sozinha a qualquer zoom do palco, sem mais
    // um numero magico para manter.
    if (deltaLogical.distance > snap) {
      controller.editPosition(id, t, target);
      _limparEncaixe();
      _informar(id);
      return;
    }
    final self = ref.read(editorControllerProvider.notifier);
    final size = self.layerBoxSize(
      ref.read(editorControllerProvider).layerById(id)!,
      t,
    );
    final half = Offset(size.width / 2, size.height / 2);

    final xs = <double>[
      project.outputWidth / 2,
      half.dx,
      project.outputWidth - half.dx,
      ...project.guides.vertical,
      ...project.guides.vertical.map((g) => g + half.dx),
      ...project.guides.vertical.map((g) => g - half.dx),
    ];
    final ys = <double>[
      project.outputHeight / 2,
      half.dy,
      project.outputHeight - half.dy,
      ...project.guides.horizontal,
      ...project.guides.horizontal.map((g) => g + half.dy),
      ...project.guides.horizontal.map((g) => g - half.dy),
    ];
    for (final other in project.layers) {
      if (other.id == id || !other.activeAt(t)) continue;
      final oc = other.position.valueAt(other.localTime(t));
      final os = self.layerBoxSize(other, t);
      final oh = Offset(os.width / 2, os.height / 2);
      xs
        ..add(oc.dx)
        ..add(oc.dx - oh.dx + half.dx)
        ..add(oc.dx + oh.dx - half.dx)
        ..add(oc.dx - oh.dx - half.dx)
        ..add(oc.dx + oh.dx + half.dx);
      ys
        ..add(oc.dy)
        ..add(oc.dy - oh.dy + half.dy)
        ..add(oc.dy + oh.dy - half.dy)
        ..add(oc.dy - oh.dy - half.dy)
        ..add(oc.dy + oh.dy + half.dy);
    }
    double? pegouX;
    double? pegouY;
    for (final x in xs) {
      if ((target.dx - x).abs() < snap) {
        target = Offset(x, target.dy);
        pegouX = x;
        break;
      }
    }
    for (final y in ys) {
      if ((target.dy - y).abs() < snap) {
        target = Offset(target.dx, y);
        pegouY = y;
        break;
      }
    }
    // A LINHA APARECE COM O ENCAIXE E SOME COM ELE. Nada de cruz
    // permanente: o que ela mostra e "seu objeto esta alinhado com
    // isto", e essa frase so faz sentido no instante em que e verdade.
    if (pegouX != _encaixeX || pegouY != _encaixeY) {
      setState(() {
        _encaixeX = pegouX;
        _encaixeY = pegouY;
      });
    }

    controller.editPosition(id, t, target);
    _informar(id);
  }

  @override
  Widget build(BuildContext context) {
    // ZOOM DE VOLTA AO AJUSTADO: o passeio zera junto.
    ref.listen<double>(zoomDoPalcoProvider, (antes, agora) {
      if (agora == 1.0 && _panDoPalco != Offset.zero) {
        setState(() => _panDoPalco = Offset.zero);
      }
    });
    final project = ref.watch(projetoDoPalcoProvider);
    final opcoes = ref.watch(opcoesDeVisualizacaoProvider);
    // PIXELS REAIS ignora a resolucao reduzida: o que se ve e o que sai.
    final resolution = opcoes.pixels
        ? PreviewResolution.full
        : ref.watch(previewResolutionProvider);
    final padGuides = ref.watch(transformGuidesProvider);
    final selectedId = ref.watch(selectedLayerProvider);
    final onion = ref.watch(onionSkinProvider);
    final drawing = ref.watch(freehandRequestProvider);
    final compW = project.outputWidth.toDouble();
    final compH = project.outputHeight.toDouble();
    final useDither =
        DitherLayer.comoFiltro && !_containsRasterMedia(project.layers);

    return Listener(
      onPointerDown: _dedoDesceu,
      onPointerMove: _dedoAndou,
      onPointerUp: _dedoSubiu,
      onPointerCancel: _dedoSubiu,
      child: GestureDetector(
      behavior: HitTestBehavior.opaque,
      onScaleStart: drawing ? null : _onScaleStart,
      onScaleUpdate: drawing ? null : _onScaleUpdate,
      // SOLTOU O DEDO, SOME A LINHA. Ela e um sinal do gesto em
      // andamento; deixada na tela depois vira decoracao que confunde.
      //
      // SO `onScaleEnd`. Um `onTapUp` aqui parecia inofensivo e nao era:
      // ele poe um reconhecedor de toque na mesma arena, e o toque
      // simples passava a ser dele em vez de chegar ao `onScaleStart` —
      // que e quem seleciona a camada embaixo do dedo. O palco parou de
      // selecionar por causa de uma limpeza que ja acontecia sozinha.
      onScaleEnd: drawing
          ? null
          : (_) {
              _limparEncaixe();
              _limparInfobar();
            },
      child: Stack(
        fit: StackFit.expand,
        children: [
          ColoredBox(
            color: Colors.black,
            child: LayoutBuilder(
              builder: (context, constraints) {
                final frame = compositionRect(
                  constraints.biggest,
                  Size(compW, compH),
                );
                // O ZOOM DO PALCO (trilho da direita) multiplica o
                // ajuste; com 1.0 o quadro fica identico ao de sempre.
                final zoomDoPalco = ref.watch(zoomDoPalcoProvider);
                final ajuste = frame.width / compW;
                final scale = ajuste * zoomDoPalco;
                // O passeio nao deixa a composicao fugir da janela.
                final folgaX =
                    ((compW * scale - constraints.maxWidth) / 2).clamp(
                          0.0,
                          double.infinity,
                        ) +
                        48;
                final folgaY =
                    ((compH * scale - constraints.maxHeight) / 2).clamp(
                          0.0,
                          double.infinity,
                        ) +
                        48;
                final pan = zoomDoPalco == 1.0
                    ? Offset.zero
                    : Offset(
                        _panDoPalco.dx.clamp(-folgaX, folgaX),
                        _panDoPalco.dy.clamp(-folgaY, folgaY),
                      );
                _stageScale = scale;
                _stageOrigin =
                    Offset(
                      (constraints.maxWidth - compW * scale) / 2,
                      (constraints.maxHeight - compH * scale) / 2,
                    ) +
                    pan;
                _tamanhoDoPalco = Size(
                  constraints.maxWidth,
                  constraints.maxHeight,
                );
                return Stack(
                  clipBehavior: Clip.hardEdge,
                  children: [
                    Positioned(
                      left: _stageOrigin.dx,
                      top: _stageOrigin.dy,
                      width: compW * scale,
                      height: compH * scale,
                      child: CompositionFrame(
                      key: const ValueKey('composition-frame'),
                      // O filho precisa ter também a área de toque da
                      // composição. Transform + OverflowBox só escalava a
                      // pintura e descartava gestos fora do canto superior.
                      child: FittedBox(
                        fit: BoxFit.contain,
                        alignment: Alignment.topLeft,
                        child: MediaQuery(
                          // FOTOS NA RESOLUCAO DA TELA. Tudo que fotografa a
                          // composicao (efeitos, mescla, dithering) le a razao
                          // de pixels daqui: com a do aparelho, cada foto saia
                          // em 1080x1920 x DPR — 75 MB por efeito por quadro no
                          // iPhone, e o iOS fechava o app na primeira animacao.
                          data: MediaQuery.of(context).copyWith(
                            devicePixelRatio: previewRasterRatio(
                              maxSidePx: widget.playback.playing.value
                                  ? 1080
                                  : 2160,
                              compWidth: compW,
                              compHeight: compH,
                              stageScale: scale * resolution.scale,
                              devicePixelRatio: MediaQuery.devicePixelRatioOf(
                                context,
                              ),
                            ),
                          ),
                          child: SizedBox(
                            width: compW,
                            height: compH,
                            // O FUNDO DA COMPOSICAO (⚙ Projeto): a cor
                            // escolhida, atras de todas as camadas.
                            child: ColoredBox(
                              color: project.backgroundColor,
                              child: Stack(
                                clipBehavior: Clip.none,
                                children: [
                                  // Automatic dithering is only a live GPU pass
                                  // for graphics. Never snapshot the preview:
                                  // asynchronous image decoding and nested video
                                  // textures must repaint without a clock change.
                                  // Export still dithers fully decoded frames.
                                  ValueListenableBuilder<Duration>(
                                    valueListenable: widget.playback.time,
                                    builder: (context, t, child) => !useDither
                                        ? child!
                                        : DitherLayer(
                                            time: t,
                                            // Escala do palco x DPR de verdade: e o
                                            // tamanho da textura do filtro. O AJUSTE
                                            // (sem o zoom do trilho) — aproximar o
                                            // palco nao pode quadruplicar a textura.
                                            pixelRatio:
                                                ajuste *
                                                MediaQuery.devicePixelRatioOf(
                                                  context,
                                                ),
                                            child: child!,
                                          ),
                                    child: CompositionView(
                                      time: widget.playback.time,
                                      videos: widget.videos,
                                      selectedId: selectedId,
                                      vistaDoPalco: true,
                                    ),
                                  ),
                                  // CASCA DE CEBOLA: os quadros vizinhos,
                                  // fantasmas, ATRAS do quadro atual. Passado
                                  // puxado para o vermelho, futuro para o
                                  // verde — e como se sabe de que lado esta.
                                  if (onion > 0)
                                    Positioned.fill(
                                      child: IgnorePointer(
                                        child: ValueListenableBuilder<Duration>(
                                          valueListenable: widget.playback.time,
                                          builder: (context, t, _) {
                                            final passo = Duration(
                                              microseconds:
                                                  1000000 ~/
                                                  (project.fps < 1
                                                      ? 30
                                                      : project.fps),
                                            );
                                            return Stack(
                                              clipBehavior: Clip.none,
                                              children: [
                                                for (var k = onion; k >= 1; k--)
                                                  for (final lado in const [
                                                    -1,
                                                    1,
                                                  ])
                                                    _Fantasma(
                                                      time:
                                                          t +
                                                          passo * (k * lado),
                                                      videos: widget.videos,
                                                      opacity: 0.34 / k,
                                                      futuro: lado > 0,
                                                    ),
                                              ],
                                            );
                                          },
                                        ),
                                      ),
                                    ),
                                  // GUIAS, GRADE, AREAS SEGURAS e mascara de
                                  // enquadramento (PR-X3): vivem ACIMA da
                                  // composicao e nunca entram no render final.
                                  Positioned.fill(
                                    child: IgnorePointer(
                                      child: CustomPaint(
                                        key: const ValueKey(
                                          'composition-guides',
                                        ),
                                        painter: _GuidesPainter(
                                          previewScale: scale,
                                          guides: project.guides,
                                          compSize: Size(compW, compH),
                                          encaixeX: _encaixeX ?? padGuides.x,
                                          encaixeY: _encaixeY ?? padGuides.y,
                                        ),
                                      ),
                                    ),
                                  ),
                                  // GRADE E PIXELS (opcoes de visualizacao):
                                  // ajudas por cima, fora do render final.
                                  if (opcoes.grade ||
                                      (opcoes.pixels && scale >= 6))
                                    Positioned.fill(
                                      child: IgnorePointer(
                                        child: CustomPaint(
                                          key: const ValueKey('palco-grade'),
                                          painter: _GradeDoPalcoPainter(
                                            compSize: Size(compW, compH),
                                            escala: scale,
                                            grade: opcoes.grade,
                                            pixels:
                                                opcoes.pixels && scale >= 6,
                                          ),
                                        ),
                                      ),
                                    ),
                                  // NOS DA MASCARA: quando alguem esta editando
                                  // o caminho, o dedo passa a mexer nos nos em
                                  // vez de mover a camada. Fora disso o widget
                                  // nao existe e nao intercepta nada.
                                  Positioned.fill(
                                    child: MaskNodeEditor(
                                      time: widget.playback.time,
                                      stageScale: () => _stageScale,
                                    ),
                                  ),
                                  // DESENHO LIVRE: por cima de tudo enquanto o
                                  // pedido do menu estiver ligado.
                                  Positioned.fill(
                                    child: FreehandOverlay(
                                      key: ValueKey(project.id),
                                      playback: widget.playback,
                                    ),
                                  ),
                                ],
                              ), // fechado-bg
                            ),
                          ),
                        ),
                      ),
                    ),
                  ),
                  ],
                );
              },
            ),
          ),
          Positioned(
            right: 4,
            top: 4,
            child: Material(
              color: const Color(0xCC171D25),
              borderRadius: BorderRadius.circular(6),
              child: PopupMenuButton<PreviewResolution>(
                key: const ValueKey('preview-resolution'),
                tooltip: 'Resolução da prévia',
                initialValue: resolution,
                onSelected: (v) =>
                    ref.read(previewResolutionProvider.notifier).state = v,
                itemBuilder: (_) => [
                  for (final v in PreviewResolution.values)
                    PopupMenuItem(value: v, child: AppText(v.label)),
                ],
                child: Padding(
                  padding: const EdgeInsets.symmetric(
                    horizontal: 10,
                    vertical: 8,
                  ),
                  child: AppText(resolution.label,
                    style: const TextStyle(color: Colors.white, fontSize: 12),
                  ),
                ),
              ),
            ),
          ),
          // ALCAS DA SELECAO: marcadores sem desenho visivel
          if (!drawing)
            Builder(
              builder: (context) {
                final a = _alcasDaSelecao();
                if (a == null) return const SizedBox.shrink();
                Widget alca(Offset p, IconData icone, String chave) =>
                    Positioned(
                      left: p.dx,
                      top: p.dy,
                      child: SizedBox(
                        key: ValueKey(chave),
                        width: 0,
                        height: 0,
                      ),
                    );
                return Stack(
                  clipBehavior: Clip.none,
                  children: [
                    alca(
                      a.escala,
                      CupertinoIcons.arrow_up_left_arrow_down_right,
                      'alca-escala',
                    ),
                    alca(
                      a.giro,
                      CupertinoIcons.arrow_2_circlepath,
                      'alca-giro',
                    ),
                  ],
                );
              },
            ),
          if (drawing)
            Positioned(
              top: 4,
              left: 8,
              right: 8,
              child: Material(
                color: const Color(0xE620242B),
                borderRadius: BorderRadius.circular(8),
                child: Row(
                  children: [
                    const SizedBox(width: 10),
                    const Expanded(
                      child: AppText('Desenho livre · arraste na prévia',
                        maxLines: 2,
                        style: TextStyle(color: Colors.white, fontSize: 12),
                      ),
                    ),
                    IconButton(
                      tooltip: 'Cancelar desenho livre',
                      onPressed: () =>
                          ref.read(freehandRequestProvider.notifier).state =
                              false,
                      icon: const Icon(
                        Icons.close,
                        color: Colors.white,
                        size: 20,
                      ),
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

/// Recorta uma FAIXA horizontal (dano digital): topo e altura em fracao
/// da caixa.
class _BandClipper extends CustomClipper<Rect> {
  const _BandClipper(this.top, this.height);

  final double top;
  final double height;

  @override
  Rect getClip(Size size) =>
      Rect.fromLTWH(0, size.height * top, size.width, size.height * height);

  @override
  bool shouldReclip(_BandClipper old) => old.top != top || old.height != height;
}

/// GRAO DE FILME: ruido puro por (semente, posicao, tempo) — nada
/// acumula, entao o frame 200 e igual direto ou depois de reproduzir.
/// O AZULEJO DE GRAO, montado uma vez.
///
/// O pintor de grao desenhava um `drawRect` POR CELULA, a cada quadro:
/// num preview de 390x700 com celula de 3 px isso da trinta mil chamadas
/// de desenho por quadro, por camada. Medido em
/// `test/ferramenta_custo_preview_test.dart`, custava 3,6 ms por camada
/// — mais do que compor QUARENTA camadas sem efeito.
///
/// Grao e ruido, e ruido nao precisa ser redesenhado celula a celula
/// toda vez. Aqui ele vira um azulejo pequeno, montado uma vez por
/// (semente, celula, intensidade) e guardado. O quadro so repete o
/// azulejo com um deslocamento — e o deslocamento por quadro que faz o
/// grao ferver como grao de filme, sem redesenhar nada.
class _GrainTile {
  _GrainTile._();

  static const int lado = 192;
  static final Map<String, ui.Image> _cache = {};

  static ui.Image para(int seed, double step, double amount) {
    final chave =
        '$seed|${step.toStringAsFixed(2)}|'
        '${amount.toStringAsFixed(3)}';
    final pronto = _cache[chave];
    if (pronto != null) return pronto;

    final rec = ui.PictureRecorder();
    final canvas = Canvas(rec);
    final paint = Paint();
    for (var y = 0.0; y < lado; y += step) {
      for (var x = 0.0; x < lado; x += step) {
        final n = fxHash01(seed, 0, (x * 7919 + y * 104729).toInt());
        final v = (n - 0.5) * amount;
        paint.color = v > 0
            ? Color.fromRGBO(255, 255, 255, (v * 1.6).clamp(0.0, 1.0))
            : Color.fromRGBO(0, 0, 0, (-v * 1.6).clamp(0.0, 1.0));
        canvas.drawRect(Rect.fromLTWH(x, y, step, step), paint);
      }
    }
    final img = rec.endRecording().toImageSync(lado, lado);
    // Poucas combinacoes vivem ao mesmo tempo; o teto evita que uma
    // intensidade animada encha a memoria de azulejos.
    if (_cache.length >= 8) {
      final velha = _cache.remove(_cache.keys.first);
      velha?.dispose();
    }
    _cache[chave] = img;
    return img;
  }
}

class _GrainPainter extends CustomPainter {
  const _GrainPainter({
    required this.amount,
    required this.size,
    required this.seed,
    required this.time,
  });

  final double amount;
  final double size;
  final int seed;
  final Duration time;

  @override
  void paint(Canvas canvas, Size canvasSize) {
    if (canvasSize.isEmpty) return;
    final step = size.clamp(0.5, 6.0) * 3;
    final frame = time.inMilliseconds ~/ 33;
    final azulejo = _GrainTile.para(seed, step, amount);
    // O DESLOCAMENTO E A ANIMACAO. Dois primos diferentes nos dois
    // eixos: o padrao nunca volta ao mesmo lugar dentro de um plano, e
    // o olho le movimento e nao repeticao.
    final dx = (frame * 37 % _GrainTile.lado).toDouble();
    final dy = (frame * 53 % _GrainTile.lado).toDouble();
    final m = Matrix4.identity()..translateByDouble(dx, dy, 0, 1);
    canvas.drawRect(
      Offset.zero & canvasSize,
      Paint()
        ..shader = ui.ImageShader(
          azulejo,
          TileMode.repeated,
          TileMode.repeated,
          m.storage,
        ),
    );
  }

  @override
  bool shouldRepaint(_GrainPainter old) =>
      old.amount != amount ||
      old.seed != seed ||
      old.size != size ||
      old.time.inMilliseconds ~/ 33 != time.inMilliseconds ~/ 33;
}

/// RUIDO FRACTAL: soma de oitavas de ruido de valor, com EVOLUCAO —
/// funcao pura de (semente, posicao, tempo), como manda a invariante I1.
class _FractalNoisePainter extends CustomPainter {
  const _FractalNoisePainter({
    required this.scale,
    required this.octaves,
    required this.contrast,
    required this.evolution,
    required this.seed,
    required this.color,
    required this.time,
  });

  final double scale;
  final int octaves;
  final double contrast;
  final double evolution;
  final int seed;
  final Color color;
  final Duration time;

  @override
  void paint(Canvas canvas, Size size) {
    final cell = (18 / scale.clamp(0.02, 1.0)).clamp(6.0, 90.0);
    final z = evolution * time.inMicroseconds / 1e6;
    final paint = Paint();
    for (var y = 0.0; y < size.height; y += cell) {
      for (var x = 0.0; x < size.width; x += cell) {
        var v = 0.0;
        var amp = 1.0;
        var freq = 1.0;
        var norm = 0.0;
        for (var o = 0; o < octaves.clamp(1, 6); o++) {
          v +=
              amp *
              valueNoise01(seed + o, x / cell * freq + z, y / cell * freq + z);
          norm += amp;
          amp *= 0.5;
          freq *= 2;
        }
        v = ((v / norm - 0.5) * contrast + 0.5).clamp(0.0, 1.0);
        paint.color = color.withValues(alpha: v);
        canvas.drawRect(Rect.fromLTWH(x, y, cell + 1, cell + 1), paint);
      }
    }
  }

  @override
  bool shouldRepaint(_FractalNoisePainter old) =>
      old.scale != scale ||
      old.octaves != octaves ||
      old.contrast != contrast ||
      old.evolution != evolution ||
      old.seed != seed ||
      old.color != color ||
      (evolution > 0 && old.time != time);
}

/// GUIAS E GRADE (PR-X3): guias arrastaveis, grade de layout com
/// colunas/medianiz/margem, areas seguras de titulo e acao, e a mascara
/// de enquadramento que mostra como o quadro fica cortado noutra
/// proporcao — sem alterar o projeto.
/// A GRADE DO PALCO: tercos marcados, oitavos apagados; com pixels reais
/// e o palco bem aproximado, uma linha por pixel da composicao.
class _GradeDoPalcoPainter extends CustomPainter {
  const _GradeDoPalcoPainter({
    required this.compSize,
    required this.escala,
    required this.grade,
    required this.pixels,
  });

  final Size compSize;
  final double escala;
  final bool grade;
  final bool pixels;

  @override
  void paint(Canvas canvas, Size size) {
    final w = compSize.width;
    final h = compSize.height;
    final fio = 1 / math.max(escala, .001);
    if (pixels) {
      final p = Paint()
        ..color = const Color(0x1FFFFFFF)
        ..strokeWidth = fio;
      for (var x = 1; x < w; x++) {
        canvas.drawLine(Offset(x.toDouble(), 0), Offset(x.toDouble(), h), p);
      }
      for (var y = 1; y < h; y++) {
        canvas.drawLine(Offset(0, y.toDouble()), Offset(w, y.toDouble()), p);
      }
    }
    if (grade) {
      final fina = Paint()
        ..color = const Color(0x26FFFFFF)
        ..strokeWidth = fio;
      final forte = Paint()
        ..color = const Color(0x66FFFFFF)
        ..strokeWidth = fio * 1.5;
      for (var i = 1; i < 24; i++) {
        final ehTerco = i % 8 == 0;
        final x = w * i / 24;
        final y = h * i / 24;
        if (i % 3 != 0 && !ehTerco) continue;
        canvas.drawLine(Offset(x, 0), Offset(x, h), ehTerco ? forte : fina);
        canvas.drawLine(Offset(0, y), Offset(w, y), ehTerco ? forte : fina);
      }
    }
  }

  @override
  bool shouldRepaint(_GradeDoPalcoPainter old) =>
      old.compSize != compSize ||
      old.escala != escala ||
      old.grade != grade ||
      old.pixels != pixels;
}

class _GuidesPainter extends CustomPainter {
  const _GuidesPainter({
    required this.guides,
    required this.compSize,
    this.encaixeX,
    this.encaixeY,
    this.previewScale = 1,
  });

  final GuidesSpec guides;
  final Size compSize;

  /// Onde o objeto encaixou agora, em coordenadas da composicao. Nulo em
  /// cada eixo que nao encaixou — e nulo nos dois quando ninguem esta
  /// arrastando.
  final double? encaixeX;
  final double? encaixeY;
  final double previewScale;

  @override
  void paint(Canvas canvas, Size size) {
    final w = compSize.width;
    final h = compSize.height;
    // A LINHA DE APOIO do encaixe, so no eixo que pegou.
    if (encaixeX != null || encaixeY != null) {
      final paint = Paint()
        ..color = const Color(0xCCFF6B6B)
        ..strokeWidth = 1.5 / math.max(previewScale, .001);
      if (encaixeX != null) {
        canvas.drawLine(Offset(encaixeX!, 0), Offset(encaixeX!, h), paint);
      }
      if (encaixeY != null) {
        canvas.drawLine(Offset(0, encaixeY!), Offset(w, encaixeY!), paint);
      }
    }

    // Grade de layout.
    if (guides.columns > 0) {
      final paint = Paint()..color = const Color(0x22B8FF3D);
      final usable = w - guides.margin * 2;
      final colW =
          (usable - guides.gutter * (guides.columns - 1)) / guides.columns;
      for (var i = 0; i < guides.columns; i++) {
        final x = guides.margin + i * (colW + guides.gutter);
        canvas.drawRect(Rect.fromLTWH(x, 0, colW, h), paint);
      }
    }

    // Areas seguras: titulo (80%) e acao (90%).
    if (guides.showSafeAreas) {
      final stroke = Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 2
        ..color = const Color(0x66FFFFFF);
      for (final f in const [0.9, 0.8]) {
        canvas.drawRect(
          Rect.fromCenter(
            center: Offset(w / 2, h / 2),
            width: w * f,
            height: h * f,
          ),
          stroke,
        );
      }
    }

    // Guias.
    final guide = Paint()
      ..color = const Color(0xAA35C4E7)
      ..strokeWidth = 2;
    for (final x in guides.vertical) {
      canvas.drawLine(Offset(x, 0), Offset(x, h), guide);
    }
    for (final y in guides.horizontal) {
      canvas.drawLine(Offset(0, y), Offset(w, y), guide);
    }

    // Mascara de enquadramento: escurece o que sai do corte.
    final fp = guides.framePreview;
    if (fp != null && fp > 0) {
      final cropW = fp >= w / h ? w : h * fp;
      final cropH = fp >= w / h ? w / fp : h;
      final crop = Rect.fromCenter(
        center: Offset(w / 2, h / 2),
        width: cropW,
        height: cropH,
      );
      final shade = Paint()..color = const Color(0x99000000);
      canvas.drawPath(
        Path.combine(
          PathOperation.difference,
          Path()..addRect(Rect.fromLTWH(0, 0, w, h)),
          Path()..addRect(crop),
        ),
        shade,
      );
      canvas.drawRect(
        crop,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 2
          ..color = const Color(0xCCB8FF3D),
      );
    }
  }

  @override
  bool shouldRepaint(_GuidesPainter old) =>
      old.guides != guides ||
      old.compSize != compSize ||
      old.encaixeX != encaixeX ||
      old.encaixeY != encaixeY ||
      old.previewScale != previewScale;
}

/// Estado do portao de recomposicao — um por app (ha um preview). Vive
/// fora do widget porque _CompositionView e recriado a cada build do
/// pai; widgets sao configuracoes imutaveis e reusa-los e valido.
/// A marcha vigente — UMA POR VISTA.
///
/// Ela ja foi global, e isso era um defeito serio: o preview, a tela de
/// exportacao e cada quadro fantasma da casca de cebola sao vistas
/// DIFERENTES, com projeto e instante proprios, e todas liam e escreviam
/// o mesmo estado. Uma via a leitura da outra.
class _CompositionGate {
  VideoProject? project;
  GearDecision? decision;
}

/// Reconstroi por tick do clock; midia isolada em RepaintBoundary.
/// A COMPOSICAO em si — as camadas empilhadas no tempo [time]. E a
/// mesma arvore usada no preview e na EXPORTACAO: exportar renderiza
/// exatamente o que se ve, porque e o mesmo codigo.
/// CASCA DE CEBOLA: quantos quadros fantasma aparecem de cada lado.
/// Zero = desligada.
///
/// Animar a mao sem ver o quadro anterior e desenhar no escuro: o
/// espacamento entre poses e o que da o ritmo, e ele so se enxerga
/// vendo os quadros vizinhos ao mesmo tempo.
/// Um quadro vizinho, esmaecido e tingido.
class _Fantasma extends StatefulWidget {
  const _Fantasma({
    required this.time,
    required this.videos,
    required this.opacity,
    required this.futuro,
  });

  final Duration time;
  final VideoLayerManager videos;
  final double opacity;
  final bool futuro;

  @override
  State<_Fantasma> createState() => _FantasmaState();
}

class _FantasmaState extends State<_Fantasma> {
  late final ValueNotifier<Duration> _t = ValueNotifier(widget.time);

  @override
  void didUpdateWidget(_Fantasma old) {
    super.didUpdateWidget(old);
    _t.value = widget.time;
  }

  @override
  void dispose() {
    _t.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (widget.time < Duration.zero) return const SizedBox.shrink();
    return Positioned.fill(
      child: Opacity(
        opacity: widget.opacity.clamp(0.05, 0.6),
        child: ColorFiltered(
          colorFilter: ColorFilter.mode(
            widget.futuro ? const Color(0x666BFF8A) : const Color(0x66FF6B6B),
            BlendMode.modulate,
          ),
          child: CompositionView(
            time: _t,
            videos: widget.videos,
            selectedId: null,
          ),
        ),
      ),
    );
  }
}

class CompositionView extends ConsumerStatefulWidget {
  const CompositionView({
    super.key,
    required this.time,
    required this.videos,
    required this.selectedId,
    this.exportFrames,
    this.exporting = false,
    this.quadroDeVideoEm,
    this.vistaDoPalco = false,
  });

  /// E O PALCO DO EDITOR: obedece as opcoes de visualizacao (sem efeitos,
  /// sem camera, selecionada a 50%). Exportacao, miniaturas e fantasmas
  /// desenham o projeto como ele e.
  final bool vistaDoPalco;

  final ValueListenable<Duration> time;
  final VideoLayerManager videos;
  final String? selectedId;

  /// Exportando: o quadro de uma camada de video em OUTRO instante da
  /// composicao — faixas do Time Slice, degrau do Posterize Time, copias
  /// do Echo. A exportacao decodifica esses quadros antes de desenhar.
  final ui.Image? Function(VideoLayer layer, Duration tempo)? quadroDeVideoEm;

  /// Na exportacao, o quadro ja decodificado de cada camada de video —
  /// textura de plataforma nao entra em `toImage`, entao o video chega
  /// aqui como imagem.
  final Map<String, ui.Image>? exportFrames;

  /// Exportando: nunca reusa arvore em cache, porque cada quadro e
  /// diferente mesmo quando a "assinatura" da cena nao muda.
  final bool exporting;

  @override
  ConsumerState<CompositionView> createState() => _CompositionViewState();
}

class _CompositionViewState extends ConsumerState<CompositionView> {
  /// PREVIEW EM RASCUNHO: o dedo esta no comando agora.
  ///
  /// Medido em `test/ferramenta_custo_preview_test.dart`, o glow e o
  /// deep glow custam ~3 ms POR CAMADA por quadro num desktop — tres a
  /// quatro vezes mais num celular. Cada nivel da piramide deles e uma
  /// camada de desfoque com alvo de render proprio; e trabalho legitimo,
  /// e nao desperdicio como era o grao.
  ///
  /// Entao aqui nao se corta trabalho inutil: escolhe-se o que mostrar
  /// ENQUANTO SE INTERAGE. Tocando, a piramide vai a dois niveis; parado,
  /// volta inteira. Exportando, nunca — la a qualidade e o unico
  /// criterio. E a mesma regra do 3D em rascunho, e a mesma razao: um
  /// preview que engasga nao serve para animar nada.
  bool get _rascunho =>
      !widget.exporting && PlaybackController.tocandoAgora.value;
  final _gate = _CompositionGate();

  ValueListenable<Duration> get time => widget.time;
  VideoLayerManager get videos => widget.videos;
  String? get selectedId => widget.selectedId;
  Map<String, ui.Image>? get exportFrames => widget.exportFrames;
  bool get exporting => widget.exporting;

  /// O instante que o relogio mostra agora. Camada montada em OUTRO
  /// instante (e com [_pedindoQuadros]) pede o quadro de video daquele
  /// instante, e nao o do tocador.
  Duration _tempoVivo = Duration.zero;
  bool _pedindoQuadros = false;

  /// Monta [f] pedindo os quadros de video do instante em que as camadas
  /// forem montadas (Time Slice, Posterize Time, Echo).
  T _emOutroTempo<T>(T Function() f) {
    final antes = _pedindoQuadros;
    _pedindoQuadros = true;
    try {
      return f();
    } finally {
      _pedindoQuadros = antes;
    }
  }

  /// TIME SLICE: [atual] e a camada (ou o composto abaixo da camada de
  /// ajuste) no instante [t]; [emTempo] monta o mesmo conteudo noutro
  /// instante da composicao. Faixas com o mesmo atraso dividem a mesma
  /// montagem.
  Widget _timeSlice(
    VideoProject project,
    EffectInstance efeito,
    Layer layer,
    Duration t,
    Widget atual,
    Widget Function(Duration tempo) emTempo,
  ) {
    final local = layer.localTime(t);
    final mistura = (efeito.paramAt('mix', local) / 100).clamp(0.0, 1.0);
    if (mistura <= .001) return atual;
    final atrasos = atrasosDoEfeito(efeito, local);
    final n = atrasos.length;
    final tamanho = Size(
      project.outputWidth.toDouble(),
      project.outputHeight.toDouble(),
    );
    final angulo = efeito.paramAt('angle', local);
    final vao = pxAt1080(
      efeito.paramAt('gap', local).clamp(0.0, 10.0),
      fxWidth,
      fxHeight,
    );
    final porAtraso = <int, Widget>{};
    final faixas = <Widget>[
      for (var k = 0; k < n; k++)
        Positioned.fill(
          child: ClipPath(
            clipper: _RecorteDaFaixa(
              faixaDoTimeSlice(
                tamanho,
                anguloGraus: angulo,
                k: k,
                n: n,
                vao: vao,
              ),
            ),
            child: porAtraso[atrasos[k]] ??= Stack(
              clipBehavior: Clip.none,
              children: [
                atrasos[k] == 0
                    ? atual
                    : emTempo(
                        layer.startTime +
                            localDeslocado(layer, local, atrasos[k], fxFps),
                      ),
              ],
            ),
          ),
        ),
    ];
    final fatiado = Stack(clipBehavior: Clip.none, children: faixas);
    if (mistura >= .999) return fatiado;
    return Stack(
      clipBehavior: Clip.none,
      children: [
        atual,
        Positioned.fill(child: Opacity(opacity: mistura, child: fatiado)),
      ],
    );
  }

  /// Opcao "selecionada a 50%": a camada escolhida deixa ver o que ha
  /// atras dela enquanto se ajusta.
  bool _metadeNaSelecao = false;

  @override
  Widget build(BuildContext context) {
    final project = widget.vistaDoPalco
        ? ref.watch(projetoDoPalcoProvider)
        : ref.watch(projetoVisivelProvider);
    _metadeNaSelecao =
        widget.vistaDoPalco &&
        ref.watch(
              opcoesDeVisualizacaoProvider.select((o) => o.modo),
            ) ==
            ModoDePrevia.meioTransparente;

    // O RASCUNHO PRECISA DE QUEM O ESCUTE. Sem este ouvinte, a
    // qualidade cheia so voltaria no proximo quadro — e ao pausar nao ha
    // proximo quadro, entao o preview ficaria parado no rascunho.
    return ValueListenableBuilder<bool>(
      valueListenable: PlaybackController.tocandoAgora,
      builder: (context, _, _) => ValueListenableBuilder<Duration>(
        valueListenable: time,
        builder: (context, t, _) {
          _tempoVivo = t;
          if (exporting) {
            return Stack(
              clipBehavior: Clip.none,
              children: _buildLayers(
                project,
                project.layers,
                t,
                resolveLinks: true,
              ),
            );
          }
          // MARCHA (PR-G1): o classificador e ESTRUTURAL — roda quando a
          // cena muda (identidade do projeto), nunca por quadro.
          //
          // O QUE SAIU DAQUI, E POR QUE: existia um cache que reusava a
          // arvore composta enquanto "nada parecesse evoluir no tempo".
          // Decidir isso exige manter a lista de tudo que varia com o
          // tempo, e essa lista nunca fica completa — ficaram de fora os
          // efeitos com fase propria, o rastreio, o pulso na batida, o
          // corte de camera. E o preco do erro e o pior que existe: o
          // preview congela, e sem preview vivo nao da para animar, que e
          // para o que o aplicativo serve. Montar a arvore e barato;
          // congelar o preview nao tem preco que pague.
          if (!identical(project, _gate.project)) {
            _gate.project = project;
            _gate.decision = classifyGear(project);
            PreviewStats.setGear(_gate.decision!);
          }
          final kids = _buildLayers(
            project,
            project.layers,
            t,
            resolveLinks: true,
          );
          PreviewStats.tick(kids.length);
          return Stack(clipBehavior: Clip.none, children: kids);
        },
      ),
    );
  }

  /// Constroi as camadas em ordem de pintura, com ordenacao 3D por trecho
  /// (D2) e vinculos de propriedade resolvidos (D4).
  List<Widget> _buildLayers(
    VideoProject project,
    List<Layer> layers,
    Duration t, {
    required bool resolveLinks,
  }) {
    // Camadas usadas como MATTE ficam ocultas na composicao (PR-M5). A
    // base de uma MASCARA DE RECORTE continua a vista.
    final matteSourceIds = <String>{
      for (final l in layers)
        if (matteEscondeAFonte(l.matteMode) && l.matteSourceId != null)
          l.matteSourceId!,
    };
    final transitions = transitionContextsAt(layers, t);
    final paintOrder = [
      for (final layer in layers.reversed)
        if (layer is! AudioLayer &&
            visibleForCut(layers, layer, t, contexts: transitions) &&
            !matteSourceIds.contains(layer.id) &&
            // SOLO (PR-X26): havendo solo, so os solos renderizam.
            project.rendersInPreview(layer.id))
          layer,
    ];
    final sorted = transitionPaintOrder(
      depthSortPaintOrder(paintOrder, t, project: project),
      transitions,
    );
    final transitionIds = {
      for (final c in transitions) ...[c.outgoing.id, c.incoming.id],
    };

    // MUNDO 3D: solidos VIZINHOS na pilha viram uma cena so, com a
    // profundidade compartilhada — um entra dentro do outro, passa por
    // tras, o vidro deixa ver o que esta atras. So entra quem nao tem
    // efeito, mascara, blend, estilo ou vinculo de propriedade; esses
    // seguem pelo caminho normal, sozinhos.
    final mundoInicio = <String, List<Element3DLayer>>{};
    final mundoMembro = <String>{};
    {
      var i = 0;
      while (i < sorted.length) {
        final l = sorted[i];
        if (l is Element3DLayer &&
            !transitionIds.contains(l.id) &&
            _mundoElegivel(project, l)) {
          var j = i + 1;
          while (j < sorted.length &&
              sorted[j] is Element3DLayer &&
              !transitionIds.contains(sorted[j].id) &&
              _mundoElegivel(project, sorted[j] as Element3DLayer)) {
            j++;
          }
          if (j - i >= 2) {
            final fila = [
              for (var k = i; k < j; k++) sorted[k] as Element3DLayer,
            ];
            mundoInicio[l.id] = fila;
            for (final f in fila) {
              mundoMembro.add(f.id);
            }
          }
          i = j;
        } else {
          i++;
        }
      }
    }

    // Modulo Grid: mapeia assetId -> (nulo, rig, indice, total) neste
    // escopo de camadas (funciona tambem dentro de grupos).
    final rigMembers = <String, (NullLayer, GridRig, int, int)>{};
    for (final l in layers) {
      if (l is NullLayer && l.grid != null && l.grid!.assets.isNotEmpty) {
        final ids = l.grid!.assets;
        for (var i = 0; i < ids.length; i++) {
          rigMembers[ids[i]] = (l, l.grid!, i, ids.length);
        }
      }
    }

    final children = <Widget>[];
    for (final layer in sorted) {
      // CAMADA DE AJUSTE (AUREA-2 §1): aplica a pilha dela ao COMPOSTO
      // de tudo abaixo; mascaras recortam a regiao (na posicao da
      // camada), opacidade dosa a mistura e o blend devolve o resultado.
      // Pilha vazia nao muda um pixel (I2).
      if (layer is AdjustmentLayer) {
        final local = layer.localTime(t);
        final hasWork = layer.effects.any((e) => e.enabled);
        if (!hasWork || children.isEmpty) continue;

        Widget adjusted = Stack(
          clipBehavior: Clip.none,
          children: List<Widget>.of(children),
        );
        // TEMPO DO QUE ESTA EMBAIXO: Posterize Time e Time Slice numa
        // camada de ajuste remontam as camadas de baixo em outros
        // instantes — e assim que o edit fatia o clipe inteiro.
        final abaixo = layers.sublist(layers.indexOf(layer) + 1);
        var tAjuste = t;
        final posterAjuste = efeitoDeTempo(layer, EffectType.posterizeTime);
        if (posterAjuste != null) {
          final degrau = localPosterizado(
            local,
            posterAjuste.paramAt('rate', local),
            posterAjuste.paramAt('phase', local),
          );
          if (degrau != local) {
            tAjuste = layer.startTime + degrau;
            adjusted = Stack(
              clipBehavior: Clip.none,
              children: _emOutroTempo(
                () => _buildLayers(
                  project,
                  abaixo,
                  tAjuste,
                  resolveLinks: resolveLinks,
                ),
              ),
            );
          }
        }
        final fatiasAjuste = efeitoDeTempo(layer, EffectType.timeSlice);
        if (fatiasAjuste != null) {
          adjusted = _timeSlice(
            project,
            fatiasAjuste,
            layer,
            tAjuste,
            adjusted,
            (tempo) => Stack(
              clipBehavior: Clip.none,
              children: _emOutroTempo(
                () => _buildLayers(
                  project,
                  abaixo,
                  tempo,
                  resolveLinks: resolveLinks,
                ),
              ),
            ),
          );
        }
        adjusted = _applyEffects(layer.effects, adjusted, local);
        if (layer.masks.isNotEmpty) {
          final pos = layer.position.valueAt(local);
          final compCenter = Offset(
            project.outputWidth / 2,
            project.outputHeight / 2,
          );
          final shift = pos - compCenter;
          adjusted = MaskedBox(
            specs: [
              for (final m in layer.masks)
                MaskSpec(
                  path: m.path.valueAt(local).build().shift(shift),
                  closed: m.path.valueAt(local).closed,
                  mode: m.mode,
                  inverted: m.inverted,
                  opacity: m.opacity.valueAt(local).clamp(0.0, 1.0),
                  feather: m.feather.valueAt(local),
                  featherY: m.featherY?.valueAt(local),
                  expansion: m.expansion.valueAt(local),
                ),
            ],
            child: adjusted,
          );
        }
        final op = layer.opacity.valueAt(local).clamp(0.0, 1.0);
        final plain =
            layer.masks.isEmpty &&
            op >= 0.999 &&
            layer.blendMode == BlendMode.srcOver;
        if (plain) {
          // O ajustado SUBSTITUI o acumulado (como no AE) — empilhar por
          // cima duplicava o conteudo no preview.
          children
            ..clear()
            ..add(Positioned.fill(child: IgnorePointer(child: adjusted)));
        } else {
          // Com mascara/opacidade/blend, o ajustado mistura POR CIMA do
          // original (dentro da mascara ele cobre o mesmo conteudo).
          children.add(
            Positioned.fill(
              child: IgnorePointer(
                child: BlendMask(
                  blendMode: layer.blendMode,
                  child: Opacity(opacity: op, child: adjusted),
                ),
              ),
            ),
          );
        }
        continue;
      }

      if (mundoMembro.contains(layer.id)) {
        final fila = mundoInicio[layer.id];
        // Quem nao abre a fila ja foi pintado na cena do primeiro.
        if (fila == null) continue;
        children.add(_buildWorld3D(project, fila, t, resolveLinks));
        continue;
      }

      // Eco/rastro: re-renderiza a camada INTEIRA em tempos anteriores
      // (deterministico — trilha de movimento dos keyframes), atras da
      // copia atual e com opacidade decaindo.
      EffectInstance? echoFx;
      for (final e in layer.effects) {
        if (e.enabled && e.type == EffectType.echo) echoFx = e;
      }
      if (echoFx != null) {
        final local = layer.localTime(t);
        final n = echoFx.paramAt('ecos', local).round().clamp(1, 8);
        final gapUs = (echoFx.paramAt('intervalo', local) * 1e6).round();
        final decay = echoFx.paramAt('decaimento', local).clamp(0.05, 0.95);
        final hueStep = echoFx.paramAt('matiz', local);
        for (var i = n; i >= 1; i--) {
          final et = t - Duration(microseconds: gapUs * i);
          if (!layer.activeAt(et)) continue;
          Widget copy = _emOutroTempo(
            () => _buildLayer(
              project,
              layer,
              et,
              resolveLinks,
              rig: rigMembers,
              opacityMul: math.pow(decay, i).toDouble(),
            ),
          );
          // Rastro COLORIDO (item 16): cada copia com matiz proprio.
          if (hueStep > 0.5) {
            copy = Positioned.fill(
              child: ColorFiltered(
                colorFilter: ColorFilter.matrix(hueRotateMatrix(hueStep * i)),
                child: Stack(clipBehavior: Clip.none, children: [copy]),
              ),
            );
          }
          children.add(copy);
        }
      }

      // FORCE MOTION BLUR (nivel 2): borra com MAIS amostras do que a
      // composicao permite, e funciona sem keyframe de transform. Como o
      // eco, ele precisa re-renderizar a camada em outros instantes —
      // por isso mora aqui, e nao na pilha de efeitos, que so recebe o
      // widget pronto.
      EffectInstance? forceMb;
      for (final e in layer.effects) {
        if (e.enabled && e.type == EffectType.forceMotionBlur) forceMb = e;
      }

      // POSTERIZE TIME: a camada INTEIRA (conteudo, efeitos e
      // transformacao) anda em degraus, como no After Effects.
      var tCamada = t;
      final posterFx = efeitoDeTempo(layer, EffectType.posterizeTime);
      if (posterFx != null) {
        final localCamada = layer.localTime(t);
        tCamada =
            layer.startTime +
            localPosterizado(
              localCamada,
              posterFx.paramAt('rate', localCamada),
              posterFx.paramAt('phase', localCamada),
            );
      }
      Widget montar(Duration tempo) => forceMb == null
          ? _buildLayer(project, layer, tempo, resolveLinks, rig: rigMembers)
          : _forceMotionBlur(
              project,
              layer,
              tempo,
              forceMb,
              resolveLinks,
              rigMembers,
            );
      var w = tCamada == t ? montar(t) : _emOutroTempo(() => montar(tCamada));

      // TIME SLICE na propria camada: faixas da camada inteira em outros
      // instantes, recortadas no espaco da composicao.
      final fatiasFx = efeitoDeTempo(layer, EffectType.timeSlice);
      if (fatiasFx != null) {
        w = Positioned.fill(
          child: _timeSlice(
            project,
            fatiasFx,
            layer,
            tCamada,
            Stack(clipBehavior: Clip.none, children: [w]),
            (tempo) => _emOutroTempo(() => montar(tempo)),
          ),
        );
      }

      // Uma camada curta pode participar de duas janelas ao mesmo tempo
      // (entra de A e ja sai para C). Cada contexto precisa ser composto;
      // usar o primeiro contexto global deixava uma das juncoes sem efeito.
      for (final transition in transitions) {
        if (transition.isParticipant(layer.id)) {
          w = _transitionVisual(project, transition, layer, t, w);
        }
      }

      // MOTION BLUR DA COMPOSICAO (nivel 1): a camada e desenhada varias
      // vezes ao longo da JANELA DE EXPOSICAO e as copias sao mediadas.
      //
      // A janela vem do angulo e da FASE do obturador. Fase -90 centra o
      // borrao no quadro; fase 0 arrasta para frente — sao imagens
      // visivelmente diferentes, e e por isso que a fase existe.
      if (project.motionBlur.enabled && project.metaOf(layer.id).motionBlur) {
        w = _comMotionBlur(project, layer, t, w, resolveLinks, rigMembers);
      }

      // Matte: a fonte recorta esta camada, num grupo isolado.
      if (layer.matteMode != MatteMode.none) {
        Layer? src;
        for (final l in layers) {
          if (l.id == layer.matteSourceId) src = l;
        }
        // Matte ligado sem fonte valida/ativa equivale a alfa zero. Deixar
        // o alvo inteiro visivel mascara um link quebrado e produz um salto
        // justamente quando a fonte entra ou sai do seu intervalo.
        final sourceActive = src != null && src.activeAt(t);
        final source = sourceActive
            ? _buildLayer(project, src, t, resolveLinks, rig: rigMembers)
            : const SizedBox.shrink();
        // Luma precisa da cor E do alfa original. A matriz de cor trabalha
        // em RGBA nao-premultiplicado, entao uma segunda pintura da mesma
        // fonte preserva sua transparencia depois de extrair a luminancia.
        final alphaForLuma =
            sourceActive &&
                (layer.matteMode == MatteMode.luma ||
                    layer.matteMode == MatteMode.lumaInvert)
            ? _buildLayer(project, src, t, resolveLinks, rig: rigMembers)
            : null;
        final matte = _matteFiltered(
          layer.matteMode,
          source,
          alphaForLuma: alphaForLuma,
        );
        w = Positioned.fill(
          child: BlendMask(
            blendMode: BlendMode.srcOver,
            isolate: true,
            child: Stack(clipBehavior: Clip.none, children: [w, matte]),
          ),
        );
      }
      // MESCLA PROPRIA: os modos que o Flutter nao tem precisam ver o
      // que ja esta embaixo. A pilha se parte aqui — o acumulado vira o
      // andar de baixo, esta camada o de cima — e o compositor devolve
      // um widget so, sobre o qual as proximas continuam empilhando.
      final custom = layer.customBlend;
      if (custom != null && children.isNotEmpty) {
        final base = Stack(
          clipBehavior: Clip.none,
          children: List<Widget>.of(children),
        );
        children
          ..clear()
          ..add(
            Positioned.fill(
              child: CustomBlendBox(
                mode: custom,
                seed: (t.inMilliseconds % 4096).toDouble(),
                base: base,
                top: Stack(clipBehavior: Clip.none, children: [w]),
              ),
            ),
          );
        continue;
      }

      children.add(w);
    }
    return children;
  }

  Widget _transitionVisual(
    VideoProject project,
    ClipTransitionContext context,
    Layer layer,
    Duration globalTime,
    Widget child,
  ) {
    final incoming = layer.id == context.incoming.id;
    final p = context.progress;
    final canvas = Stack(clipBehavior: Clip.none, children: [child]);
    Widget out = Opacity(
      opacity: context.opacityFor(layer.id).clamp(0.0, 1.0),
      child: canvas,
    );
    switch (context.transition.type) {
      case ClipTransitionType.dissolve:
      case ClipTransitionType.black:
        break;
      case ClipTransitionType.wipe:
        if (incoming) {
          out = ClipRect(
            child: Align(
              alignment: Alignment.centerLeft,
              widthFactor: p.clamp(0.001, 1.0),
              child: out,
            ),
          );
        }
      case ClipTransitionType.zoomWarp:
        final scale = incoming ? 0.82 + p * 0.18 : 1 + p * 0.2;
        out = Transform.scale(scale: scale, child: out);
      case ClipTransitionType.whip:
        final distance = project.outputWidth.toDouble();
        out = Transform.translate(
          offset: Offset(incoming ? (1 - p) * distance : -p * distance, 0),
          child: out,
        );
      case ClipTransitionType.glitch:
        final phase = globalTime.inMicroseconds / 1000000.0;
        final envelope = 1 - (2 * p - 1).abs();
        final jump = math.sin(phase * 97.0) * envelope * 24;
        out = Transform.translate(offset: Offset(jump, 0), child: out);
      case ClipTransitionType.effect:
        final effect = context.transition.effect;
        if (effect != null) {
          final amount = context.transition.amountAt(
            globalTime,
            context.incoming.startTime,
          );
          final local = globalTime - context.window.start;
          if (essentialWarpTypes.contains(effect.type)) {
            final reveal =
                effect.type == EffectType.venetianBlinds ||
                effect.type == EffectType.blockDissolve;
            if (reveal && !incoming) {
              out = canvas;
            } else {
              final params = {...effect.params};
              if (effect.type == EffectType.offset) {
                for (final key in ['center_x', 'center_y']) {
                  params[key] = AnimatedDouble(
                    .5 + (effect.paramAt(key, local) - .5) * amount,
                  );
                }
              } else {
                params['amount'] = AnimatedDouble(
                  reveal ? 1 - p : effect.paramAt('amount', local) * amount,
                );
              }
              out = Opacity(
                opacity: reveal ? 1 : context.opacityFor(layer.id),
                child: _applyEffects(
                  [effect.copyWith(params: params)],
                  canvas,
                  local,
                ),
              );
            }
            break;
          }
          final effected = _applyEffects([effect], canvas, local);
          out = Opacity(
            opacity: context.opacityFor(layer.id).clamp(0.0, 1.0),
            child: Stack(
              fit: StackFit.expand,
              children: [
                canvas,
                Opacity(opacity: amount, child: effected),
              ],
            ),
          );
        }
    }
    return Positioned.fill(child: out);
  }

  /// FORCE MOTION BLUR: borra a camada com as amostras que o efeito
  /// pedir, independente do que a composicao permite.
  ///
  /// `Native Motion Blur` decide o que fazer com o borrao da composicao:
  /// Off ignora, On soma os dois, Only usa so o da composicao (e ai este
  /// efeito nao faz nada).
  Widget _forceMotionBlur(
    VideoProject project,
    Layer layer,
    Duration t,
    EffectInstance fx,
    bool resolveLinks,
    Map<String, (NullLayer, GridRig, int, int)> rig,
  ) {
    final local = layer.localTime(t);
    final nativo = fx.paramAt('native_motion_blur', local).round();
    if (nativo == 2) {
      // "Only": quem borra e a composicao.
      return _buildLayer(project, layer, t, resolveLinks, rig: rig);
    }

    final n = fx.paramAt('samples', local).round().clamp(2, 64);
    final angulo = fx.paramAt('shutter_angle', local).clamp(0.0, 720.0);
    if (angulo < 0.5) {
      return _buildLayer(project, layer, t, resolveLinks, rig: rig);
    }

    final fps = project.fps < 1 ? 30 : project.fps;
    final quadroUs = 1000000 / fps;
    // Centrado no quadro, como a fase -90 da composicao.
    final metadeUs = angulo / 360 / 2 * quadroUs;

    final copias = <Widget>[];
    for (var i = 0; i < n; i++) {
      final f = n == 1 ? 0.0 : (i / (n - 1)) * 2 - 1;
      final ti = t + Duration(microseconds: (f * metadeUs).round());
      final amostra = ti < Duration.zero
          ? _buildLayer(project, layer, t, resolveLinks, rig: rig)
          : _buildLayer(project, layer, ti, resolveLinks, rig: rig);
      // Media corrente: todas as amostras com o mesmo peso.
      // A amostra ja vem como Positioned: a opacidade entra por dentro
      // de um Positioned.fill (Opacity direto dentro do Stack quebrava o
      // ParentData e derrubava o preview com Force Motion Blur).
      copias.add(
        Positioned.fill(
          child: Opacity(
            opacity: 1 / (i + 1),
            child: Stack(clipBehavior: Clip.none, children: [amostra]),
          ),
        ),
      );
    }
    return Stack(clipBehavior: Clip.none, children: copias);
  }

  /// Quanto a camada SE MOVE dentro da janela, em pixels aproximados.
  ///
  /// Serve para o limite adaptativo: camada parada nao gasta amostra
  /// nenhuma, e camada que anda tres pixels nao precisa de dezesseis.
  double _movimentoNaJanela(
    VideoProject project,
    Layer layer,
    Duration a,
    Duration b,
  ) {
    final ta = effectiveTransform(project, layer, a);
    final tb = effectiveTransform(project, layer, b);
    final d = (tb.pos - ta.pos).distance;
    final giro = (tb.rot - ta.rot).abs();
    final escala = (tb.scale - ta.scale).abs();
    // Giro e escala viram pixel pelo tamanho aproximado da camada.
    final tamanho = project.outputWidth * 0.5;
    return d + giro / 90 * tamanho * 0.5 + escala * tamanho;
  }

  /// A camada borrada pelo movimento.
  ///
  /// As copias sao mediadas com opacidade 1/(i+1): isso e a MEDIA
  /// CORRENTE, e da o mesmo peso a todas as amostras. Empilhar todas com
  /// 1/N daria peso maior as ultimas, e o borrao sairia puxado para um
  /// lado.
  Widget _comMotionBlur(
    VideoProject project,
    Layer layer,
    Duration t,
    Widget nitida,
    bool resolveLinks,
    Map<String, (NullLayer, GridRig, int, int)> rig,
  ) {
    final mb = project.motionBlur;
    final fps = project.fps < 1 ? 30 : project.fps;
    final quadroUs = 1000000 / fps;
    final (ini, fim) = mb.exposureWindow();
    final janelaUs = (fim - ini) * quadroUs;
    if (janelaUs.abs() < 1) return nitida;

    final inicio = t + Duration(microseconds: (ini * quadroUs).round());
    final termino = t + Duration(microseconds: (fim * quadroUs).round());

    // LIMITE ADAPTATIVO: parada, a camada nao borra; andando pouco,
    // poucas amostras bastam. Dezesseis amostras de uma camada parada
    // seriam dezesseis renderizacoes identicas.
    final movimento = _movimentoNaJanela(project, layer, inicio, termino);
    if (movimento < 0.6) return nitida;

    final pedidas = mb.samples.clamp(2, mb.adaptiveLimit);
    final n = movimento < 3
        ? 2
        : (movimento < 12 ? 4 : pedidas).clamp(2, pedidas);

    final copias = <Widget>[];
    for (var i = 0; i < n; i++) {
      final f = n == 1 ? 0.5 : i / (n - 1);
      final ti =
          inicio +
          Duration(
            microseconds: ((termino - inicio).inMicroseconds * f).round(),
          );
      final amostra = ti < Duration.zero
          ? nitida
          : _buildLayer(project, layer, ti, resolveLinks, rig: rig);
      // A amostra ja vem como Positioned: a opacidade entra por dentro
      // de um Positioned.fill (Opacity direto dentro do Stack quebrava o
      // ParentData e derrubava o preview com Force Motion Blur).
      copias.add(
        Positioned.fill(
          child: Opacity(
            opacity: 1 / (i + 1),
            child: Stack(clipBehavior: Clip.none, children: [amostra]),
          ),
        ),
      );
    }
    return Stack(clipBehavior: Clip.none, children: copias);
  }

  /// Converte a fonte do matte no canal certo e composita com dstIn.
  Widget _matteFiltered(MatteMode mode, Widget matte, {Widget? alphaForLuma}) {
    const lumaM = <double>[
      0, 0, 0, 0, 0, //
      0, 0, 0, 0, 0, //
      0, 0, 0, 0, 0, //
      0.299, 0.587, 0.114, 0, 0,
    ];
    const lumaInvM = <double>[
      0, 0, 0, 0, 0, //
      0, 0, 0, 0, 0, //
      0, 0, 0, 0, 0, //
      -0.299, -0.587, -0.114, 0, 255,
    ];
    const alphaInvM = <double>[
      0, 0, 0, 0, 0, //
      0, 0, 0, 0, 0, //
      0, 0, 0, 0, 0, //
      0, 0, 0, -1, 255,
    ];
    // O widget do matte e um Positioned: ele precisa ser filho DIRETO de
    // um Stack — o filtro de cor envolve o Stack, nunca o Positioned.
    Widget content = Stack(clipBehavior: Clip.none, children: [matte]);
    switch (mode) {
      case MatteMode.alpha:
      case MatteMode.recorte:
      case MatteMode.none:
        break;
      case MatteMode.alphaInvert:
        content = ColorFiltered(
          colorFilter: const ColorFilter.matrix(alphaInvM),
          child: content,
        );
      case MatteMode.luma:
        content = ColorFiltered(
          colorFilter: const ColorFilter.matrix(lumaM),
          child: content,
        );
      case MatteMode.lumaInvert:
        content = ColorFiltered(
          colorFilter: const ColorFilter.matrix(lumaInvM),
          child: content,
        );
    }

    // ColorFilter.matrix substitui o alfa pelo luma em espaco
    // nao-premultiplicado. Intersectar com uma segunda pintura da fonte
    // produz luma * alfa (e (1-luma) * alfa no invertido), sem recuperar
    // pixels originalmente transparentes.
    if (alphaForLuma != null &&
        (mode == MatteMode.luma || mode == MatteMode.lumaInvert)) {
      content = BlendMask(
        blendMode: BlendMode.srcOver,
        isolate: true,
        child: Stack(
          clipBehavior: Clip.none,
          children: [
            Positioned.fill(child: content),
            Positioned.fill(
              child: BlendMask(
                blendMode: BlendMode.dstIn,
                child: Stack(clipBehavior: Clip.none, children: [alphaForLuma]),
              ),
            ),
          ],
        ),
      );
    }
    return Positioned.fill(
      child: IgnorePointer(
        child: BlendMask(blendMode: BlendMode.dstIn, child: content),
      ),
    );
  }

  /// Pode entrar no mundo 3D compartilhado? Sem efeito ligado, mascara,
  /// blend, estilo, matte ou vinculo de propriedade (o vinculo de PAI e
  /// aceito: a cadeia de nulos e resolvida pelo effectiveTransform).
  bool _mundoElegivel(VideoProject project, Element3DLayer l) {
    if (l.effects.any((e) => e.enabled)) return false;
    if (l.masks.isNotEmpty) return false;
    if (l.blendMode != BlendMode.srcOver || l.customBlend != null) {
      return false;
    }
    if (l.matteMode != MatteMode.none) return false;
    if (!project.metaOf(l.id).styles.isEmpty) return false;
    for (final prop in [
      LayerProp.position,
      LayerProp.rotation,
      LayerProp.opacity,
      LayerProp.scale,
    ]) {
      if (project.linkFor(l.id, prop) != null) return false;
    }
    return true;
  }

  /// A cena unica de uma fila de solidos: cada um com seu transform
  /// efetivo (posicao, Z, rotacoes, escala, opacidade), projetados pela
  /// mesma camera no centro da composicao.
  Widget _buildWorld3D(
    VideoProject project,
    List<Element3DLayer> fila,
    Duration t,
    bool resolveLinks,
  ) {
    final items = <World3DItem>[];
    for (final l in fila) {
      final local = l.localTime(t);
      final eff = resolveLinks
          ? effectiveTransform(project, l, t)
          : LayerTransform(
              pos: l.position.valueAt(local),
              rot: l.rotation.valueAt(local),
              rotX: l.rotationX.valueAt(local),
              rotY: l.rotationY.valueAt(local),
              scale: l.scaleX.valueAt(local),
              z: l.positionZ.valueAt(local),
            );
      final rawSx = l.scaleX.valueAt(local);
      final ratio = rawSx.abs() < 1e-6 ? 1.0 : eff.scale / rawSx;
      final proprioZ = l.positionZ.valueAt(local);
      final temZ = l.is3D || (eff.z - proprioZ).abs() > 1e-6;
      items.add(
        World3DItem(
          layer: l,
          center: eff.pos,
          z: temZ ? eff.z.clamp(-1100.0, 100000.0) : 0,
          scaleX: eff.scale,
          scaleY: l.scaleY.valueAt(local) * ratio,
          rotXDeg: eff.rotX,
          rotYDeg: eff.rotY,
          rotZDeg: eff.rot,
          opacity: l.opacity.valueAt(local).clamp(0.0, 1.0),
          selected: l.id == selectedId,
          material: l.material,
          gradient: l.gradient,
          shininess: l.shininess,
        ),
      );
    }
    return Positioned.fill(
      child: IgnorePointer(
        child: ListenableBuilder(
          listenable: Listenable.merge([
            TextureCache.instance.revision,
            MeshCache.instance.revision,
          ]),
          builder: (_, _) => CustomPaint(painter: World3DPainter(items: items)),
        ),
      ),
    );
  }

  Widget _buildLayer(
    VideoProject project,
    Layer layer,
    Duration t,
    bool resolveLinks, {
    double opacityMul = 1,
    Map<String, (NullLayer, GridRig, int, int)>? rig,
  }) {
    final local = layer.localTime(t);

    // REMAPEAR TEMPO (igual ao AE): muda QUAL instante da camada aparece
    // agora, sem tocar nos keyframes de transformacao — que continuam
    // lendo o tempo da composicao. E o que permite congelar, voltar e
    // fazer rampa de velocidade com keyframes de tempo.
    final contentLocal = _remappedTime(layer, local);

    // ---- propriedades efetivas (com pickwhip quando ha vinculo) ----
    var pos = layer.position.valueAt(local);
    var rotationDeg = layer.rotation.valueAt(local);
    var opacityV = layer.opacity.valueAt(local);
    var sx = layer.scaleX.valueAt(local);
    var sy = layer.scaleY.valueAt(local);
    var extraZ = 0.0;
    var extraRotX = 0.0;
    var extraRotY = 0.0;

    if (resolveLinks) {
      Layer? src(PropertyLink? l) =>
          l == null ? null : project.layerById(l.sourceLayerId);

      final pl = project.linkFor(layer.id, LayerProp.position);
      final ps = src(pl);
      if (pl != null && ps != null) {
        final sourceTime = t - pl.delay;
        pos =
            ps.position.valueAt(ps.localTime(sourceTime)) +
            Offset(pl.offsetX, pl.offsetY);
      }
      final rl = project.linkFor(layer.id, LayerProp.rotation);
      final rs = src(rl);
      if (rl != null && rs != null) {
        final sourceTime = t - rl.delay;
        rotationDeg =
            rs.rotation.valueAt(rs.localTime(sourceTime)) * rl.scale +
            rl.offsetX;
      }
      final ol = project.linkFor(layer.id, LayerProp.opacity);
      final os = src(ol);
      if (ol != null && os != null) {
        final sourceTime = t - ol.delay;
        opacityV = (os.opacity.valueAt(os.localTime(sourceTime)) + ol.offsetX)
            .clamp(0, 1);
      }
      final sl = project.linkFor(layer.id, LayerProp.scale);
      final ss = src(sl);
      if (sl != null && ss != null) {
        final sourceTime = t - sl.delay;
        final f = ss.scaleX.valueAt(ss.localTime(sourceTime)) * sl.offsetX;
        sx = f;
        sy = f;
      }

      // Parenting (objeto nulo / camada pai): resolve a CADEIA inteira
      // (objeto -> nulo 1 -> nulo 2 -> ...) recursivamente. O filho segue
      // o delta de posicao/rotacao(3D)/escala acumulado — semantica AE:
      // nada pula ao parear, e girar o nulo em X/Y/Z orbita o filho.
      final par = project.linkFor(layer.id, LayerProp.parent);
      if (par != null) {
        final eff = effectiveTransform(project, layer, t);
        final rawScale = layer.scaleX.valueAt(local);
        final ratio = rawScale.abs() < 1e-6 ? 1.0 : eff.scale / rawScale;
        pos = eff.pos;
        rotationDeg = eff.rot;
        extraRotX = eff.rotX - layer.rotationX.valueAt(local);
        extraRotY = eff.rotY - layer.rotationY.valueAt(local);
        extraZ = eff.z - layer.positionZ.valueAt(local);
        sx *= ratio;
        sy *= ratio;
      } else if (layer.is3D &&
          layer is! CameraLayer &&
          rig?[layer.id] == null) {
        // CAMADA 3D SEM PAI tambem ve a camera da composicao. So o
        // caminho do pai passava por ela: mover a camera nao mexia na
        // camada solta, e a moldura de selecao ficava em outro lugar.
        final cam = cameraAtivaEm(project, t);
        if (cam != null) {
          final vista = vistoPelaCamera(
            project,
            cam,
            t,
            LayerTransform(
              pos: pos,
              rot: rotationDeg,
              rotX: layer.rotationX.valueAt(local),
              rotY: layer.rotationY.valueAt(local),
              scale: 1,
              z: layer.positionZ.valueAt(local),
            ),
          );
          pos = vista.pos;
          rotationDeg = vista.rot;
          extraRotX = vista.rotX - layer.rotationX.valueAt(local);
          extraRotY = vista.rotY - layer.rotationY.valueAt(local);
          extraZ = vista.z - layer.positionZ.valueAt(local);
          sx *= vista.scale;
          sy *= vista.scale;
        }
      }
    }

    // ---- Modulo Grid: a camada e ASSET de uma grade num nulo ----
    // O rig calcula posicao/rotacao/escala base; a transform propria da
    // camada e aplicada POR CIMA como offset — mover uma camada
    // manualmente nunca desloca as outras.
    final rigInfo = rig?[layer.id];
    if (rigInfo != null) {
      final (nullL, g, idx, count) = rigInfo;
      if (nullL.activeAt(t)) {
        // Nulo CONTROLADOR (alem do dono): o transform dele modula os
        // parametros — escala x espacamento/raio, rotZ + rotacao da
        // grade, rotY + twist. Animar o nulo anima a grade.
        var spacingMul = 1.0, rotationAdd = 0.0, twistAdd = 0.0;
        final ctrlId = g.controllerId;
        if (ctrlId != null) {
          final ctrl = project.layerById(ctrlId);
          if (ctrl != null && ctrl.activeAt(t)) {
            final ce = effectiveTransform(project, ctrl, t);
            spacingMul = ce.scale;
            rotationAdd = ce.rot;
            twistAdd = ce.rotY;
          }
        }
        final place = gridPlacementAt(
          g,
          idx,
          count,
          nullL.localTime(t),
          spacingMul: spacingMul,
          rotationAdd: rotationAdd,
          twistAdd: twistAdd,
        );
        final ne = effectiveTransform(project, nullL, t);

        // Layout girado/escalado pelo transform 3D do nulo controlador.
        final vx = place.pos.dx * ne.scale;
        final vy = place.pos.dy * ne.scale;
        final vz = place.z * ne.scale;
        final dRx = ne.rotX * math.pi / 180;
        final dRy = ne.rotY * math.pi / 180;
        final dRz = ne.rot * math.pi / 180;
        final cxr = math.cos(dRx), sxr = math.sin(dRx);
        final y1 = vy * cxr - vz * sxr;
        final z1 = vy * sxr + vz * cxr;
        final cyr = math.cos(dRy), syr = math.sin(dRy);
        final x1 = vx * cyr + z1 * syr;
        final z2 = -vx * syr + z1 * cyr;
        final czr = math.cos(dRz), szr = math.sin(dRz);

        final compCenter = Offset(
          project.outputWidth / 2,
          project.outputHeight / 2,
        );
        final authoredOffset = pos - compCenter;
        // Orbita 3D de verdade. A perspectiva fica para a projecao unica
        // la embaixo (ponto de fuga no centro): projetar aqui tambem dobrava.
        pos =
            ne.pos +
            Offset(x1 * czr - y1 * szr, x1 * szr + y1 * czr) +
            authoredOffset;
        extraZ = ne.z + z2 - layer.positionZ.valueAt(local);
        rotationDeg += place.rotationDeg + ne.rot;
        extraRotX += ne.rotX;
        extraRotY += ne.rotY;
        sx *= place.scale * ne.scale;
        sy *= place.scale * ne.scale;
        opacityV *= place.opacity;
      }
    }

    // ---- 3D: profundidade de verdade ----
    // Recuar em Z encolhe E leva a camada para o ponto de fuga (centro da
    // composicao), a mesma conta dos solidos 3D. So encolher deixava o Z
    // com cara de zoom: a camada ficava parada no lugar.
    final camAtiva = (layer.is3D || extraZ != 0) && layer is! CameraLayer
        ? cameraAtivaEm(project, t)
        : null;
    if (layer.is3D || extraZ != 0) {
      final vista = projetarProfundidade(
        project,
        pos,
        layer.positionZ.valueAt(local) + extraZ,
        ortografica: camAtiva?.opcoes.ortografica ?? false,
      );
      // Passou da camera: nao aparece (como no After Effects).
      if (vista == null) {
        return const Positioned(left: 0, top: 0, child: SizedBox.shrink());
      }
      pos = vista.pos;
      sx *= vista.escala;
      sy *= vista.escala;
    }

    final rotation = rotationDeg * math.pi / 180;
    final skewX = layer.skewX.valueAt(local) * math.pi / 180;
    final skewY = layer.skewY.valueAt(local) * math.pi / 180;
    final pivot = layer.pivot.valueAt(local);
    final metade = _metadeNaSelecao && layer.id == selectedId ? .5 : 1.0;
    final opacity = (opacityV * opacityMul * metade).clamp(0.0, 1.0);

    // Particulas vivem em espaco 3D proprio: a rotacao do sistema (da
    // camada + herdada do nulo pai) e resolvida DENTRO do simulador — a
    // nuvem gira no espaco, nada de inclinar o canvas como um cartao.
    final isParticles = layer is ParticlesLayer || layer is Element3DLayer;
    Widget content = _LayerContent(
      layer: layer,
      project: project,
      exportFrames: exportFrames,
      exporting: exporting,
      tempoAlheio: _pedindoQuadros && t != _tempoVivo ? t : null,
      quadroEm: widget.quadroDeVideoEm,
      compWidth: project.outputWidth.toDouble(),
      videos: videos,
      localTime: contentLocal,
      particlesRotX: isParticles
          ? layer.rotationX.valueAt(local) + extraRotX
          : 0,
      particlesRotY: isParticles
          ? layer.rotationY.valueAt(local) + extraRotY
          : 0,
      // VINCULOS DENTRO DO GRUPO: os filhos resolvem pai e pickwhip entre
      // eles. Com `resolveLinks: false` o filho preso a um nulo do mesmo
      // grupo seguia o nulo so enquanto se estava "dentro" do grupo — fora,
      // o vinculo morria (o "as vezes buga" do testador).
      buildChildren: (childLayers, childT) => _buildLayers(
        project.copyWith(layers: childLayers),
        childLayers,
        childT,
        resolveLinks: true,
      ),
    );

    if (layer is VideoLayer && layer.speedBlur) {
      final rate = videoPlaybackRateAt(layer, local).abs();
      final sigma = ((rate - 1).abs() * 2.4).clamp(0.0, 18.0);
      if (sigma > 0.05) {
        content = ImageFiltered(
          imageFilter: ui.ImageFilter.blur(sigmaX: sigma, sigmaY: sigma * 0.35),
          child: content,
        );
      }
    }

    // Mascaras cortam o alfa da propria camada ANTES dos efeitos (AE).
    if (layer.masks.isNotEmpty) {
      content = MaskedBox(
        specs: [
          for (final m in layer.masks)
            MaskSpec(
              path: m.path.valueAt(local).build(),
              closed: m.path.valueAt(local).closed,
              mode: m.mode,
              inverted: m.inverted,
              opacity: m.opacity.valueAt(local).clamp(0.0, 1.0),
              feather: m.feather.valueAt(local),
              featherY: m.featherY?.valueAt(local),
              expansion: m.expansion.valueAt(local),
            ),
        ],
        child: content,
      );
    }

    content = _applyEffects(layer.effects, content, local);

    // ESTILOS DE CAMADA (PR-X10): aplicam DEPOIS dos efeitos e
    // acompanham a forma da camada — e o que os diferencia de efeito.
    final styles = project.metaOf(layer.id).styles;
    if (!styles.isEmpty) {
      content = _applyLayerStyles(
        styles,
        content,
        local,
        Size(project.outputWidth.toDouble(), project.outputHeight.toDouble()),
      );
    }

    // FOCO E NEBLINA DA CAMERA, pela distancia da camada ao olho dela. So
    // quando ligados: cada um e um passe a mais na GPU por camada.
    if (camAtiva != null && layer.is3D && !camAtiva.opcoes.neutra) {
      final distancia =
          CameraLayer.lenteNeutra + layer.positionZ.valueAt(local) + extraZ;
      final localDaCamera = camAtiva.localTime(t);
      final sigma = camAtiva.opcoes.desfoqueEm(distancia, localDaCamera);
      if (sigma > .3) {
        content = ImageFiltered(
          key: ValueKey('foco-${layer.id}'),
          imageFilter: ui.ImageFilter.blur(
            sigmaX: sigma,
            sigmaY: sigma,
            tileMode: TileMode.decal,
          ),
          child: content,
        );
      }
      final nevoa = camAtiva.opcoes.neblinaEm(distancia, localDaCamera);
      if (nevoa > .004) {
        content = ColorFiltered(
          key: ValueKey('neblina-${layer.id}'),
          colorFilter: ColorFilter.matrix(
            matrizDaNeblina(camAtiva.opcoes.corDaNeblina, nevoa),
          ),
          child: content,
        );
      }
    }

    // Selecao desenhada DEPOIS dos efeitos: blur/glow nao pegam a borda.
    // Copias de eco (opacityMul < 1) nao ganham borda de selecao.
    final contentSemSelecao = content;
    if (layer.id == selectedId && opacityMul == 1) {
      content = Stack(
        clipBehavior: Clip.none,
        children: [
          content,
          Positioned.fill(
            child: IgnorePointer(
              child: Container(
                decoration: BoxDecoration(
                  border: Border.all(color: Colors.white, width: 4),
                ),
              ),
            ),
          ),
        ],
      );
    }

    // Rotacao 3D (eixos X/Y) — inclui o delta herdado do pai 3D.
    // Particulas NAO entram aqui: a rotacao delas e 3D real no painter.
    final rx = (layer.rotationX.valueAt(local) + extraRotX) * math.pi / 180;
    final ry = (layer.rotationY.valueAt(local) + extraRotY) * math.pi / 180;
    final tilt3D = (rx != 0 || ry != 0) && !isParticles;

    // ORDEM CONSISTENTE (triagem 3D §5 itens 7/11): o vetor recebe
    // S -> Skew -> Rx -> Ry -> Rz — a MESMA ordem da matematica de
    // orbita (Rz*Ry*Rx). Misturar ordens era o que cisalhava as formas
    // ao girar o nulo. Com tilt 3D, o Rz sobe para a matriz de
    // perspectiva; sem tilt, tudo segue no caminho 2D de sempre.
    final m = Matrix4.identity()..translateByDouble(pivot.dx, pivot.dy, 0, 1);
    if (!tilt3D) m.rotateZ(rotation);
    m
      ..multiply(Matrix4.skew(skewX, skewY))
      ..scaleByDouble(sx, sy, 1, 1)
      ..translateByDouble(-pivot.dx, -pivot.dy, 0, 1);

    Widget composed = Transform(
      transform: m,
      alignment: Alignment.center,
      child: Opacity(opacity: opacity, child: content),
    );

    if (tilt3D) {
      final pm = Matrix4.identity()
        ..setEntry(
          3,
          2,
          (camAtiva?.opcoes.ortografica ?? false) ? 0 : -1 / 1200,
        )
        ..rotateZ(rotation)
        ..rotateY(ry)
        ..rotateX(rx);
      // EXTRUDE 3D: fatias da camada empilhadas em Z atras da frente,
      // escurecidas — a espessura aparece quando a camada inclina. Video
      // e particulas ficam de fora (textura e simulacao nao se repetem).
      final extrude = project.metaOf(layer.id).extrude;
      if (extrude > 0.5 &&
          layer is! VideoLayer &&
          layer is! ParticlesLayer &&
          layer is! Element3DLayer) {
        // Uma FOTO da camada, desenhada N vezes recuando em Z num canvas
        // so (sem camadas do compositor — ver ExtrudeSnapshotPainter).
        // Fatias a ~1,5 px na tela: quanto mais de lado a camada esta,
        // mais fatias, senao a lateral sai listrada.
        final inclinacao = math.max(math.sin(rx).abs(), math.sin(ry).abs());
        final passos = (extrude * math.max(inclinacao, 0.15) / 1.5)
            .ceil()
            .clamp(2, 120);
        composed = FxSnapshot(
          painter: ExtrudeSnapshotPainter(
            perspective: pm,
            transform2d: m,
            opacity: opacity,
            passos: passos,
            passo: extrude / passos,
          ),
          child: contentSemSelecao,
        );
        if (!identical(content, contentSemSelecao)) {
          // A borda de selecao fica so na frente, sem virar caixa 3D.
          composed = Stack(
            clipBehavior: Clip.none,
            children: [
              composed,
              Positioned.fill(
                child: IgnorePointer(
                  child: Transform(
                    transform: pm,
                    alignment: Alignment.center,
                    child: Transform(
                      transform: m,
                      alignment: Alignment.center,
                      child: Container(
                        decoration: BoxDecoration(
                          border: Border.all(color: Colors.white, width: 4),
                        ),
                      ),
                    ),
                  ),
                ),
              ),
            ],
          );
        }
      } else {
        composed = Transform(
          transform: pm,
          alignment: Alignment.center,
          child: composed,
        );
      }
    }

    if (layer.blendMode != BlendMode.srcOver) {
      composed = BlendMask(blendMode: layer.blendMode, child: composed);
    }

    return Positioned(
      left: pos.dx,
      top: pos.dy,
      child: FractionalTranslation(
        translation: const Offset(-0.5, -0.5),
        child: composed,
      ),
    );
  }

  /// ESTILOS DE CAMADA (PR-X10). Sombra e brilho usam a SILHUETA da
  /// camada (o alfa), nao uma caixa — por isso o desenho e uma copia
  /// tingida e borrada por baixo do original.
  Widget _applyLayerStyles(
    LayerStyles s,
    Widget child,
    Duration local,
    Size compositionSize,
  ) {
    var out = child;

    // Sobreposicoes pintam POR CIMA, respeitando o alfa.
    if (s.colorOverlay?.enabled ?? false) {
      final o = s.colorOverlay!;
      out = Stack(
        clipBehavior: Clip.none,
        children: [
          out,
          Positioned.fill(
            child: IgnorePointer(
              child: Opacity(
                opacity: o.opacity.valueAt(local).clamp(0.0, 1.0),
                child: BlendMask(
                  blendMode: BlendMode.srcIn,
                  child: ColoredBox(color: o.color),
                ),
              ),
            ),
          ),
        ],
      );
    }
    if (s.gradientOverlay?.enabled ?? false) {
      final g = s.gradientOverlay!;
      final rad = g.angleDeg.valueAt(local) * math.pi / 180;
      out = Stack(
        clipBehavior: Clip.none,
        children: [
          out,
          Positioned.fill(
            child: IgnorePointer(
              child: Opacity(
                opacity: g.opacity.valueAt(local).clamp(0.0, 1.0),
                child: BlendMask(
                  blendMode: BlendMode.srcIn,
                  child: DecoratedBox(
                    decoration: BoxDecoration(
                      gradient: LinearGradient(
                        begin: Alignment(-math.cos(rad), -math.sin(rad)),
                        end: Alignment(math.cos(rad), math.sin(rad)),
                        colors: [g.colorA, g.colorB],
                      ),
                    ),
                  ),
                ),
              ),
            ),
          ),
        ],
      );
    }

    // AS BORDAS, na ordem da lista: FORA e a silhueta dilatada por tras;
    // DENTRO e a silhueta tingida menos a silhueta erodida, por cima;
    // CENTRO e metade de cada.
    for (final st in s.bordas) {
      if (!st.enabled) continue;
      // TETO DA DILATACAO. O dilate do Impeller e um laco de 2r+1
      // leituras por pixel, por eixo, na resolucao inteira — nao ha a
      // reducao que o desfoque tem. Sem teto, um contorno largo demais
      // (ou um keyframe passando por um valor alto) vira segundos de GPU
      // por quadro, e o iPhone reinicia. Cem pixels e o mesmo teto do
      // espalhamento da sombra, e ja e mais grosso que qualquer contorno
      // legivel.
      final w = st.width.valueAt(local).clamp(0.0, 100.0).toDouble();
      if (w <= 0.01) continue;
      final opacidade = st.opacity.valueAt(local).clamp(0.0, 1.0);
      final fora = switch (st.posicao) {
        PosicaoDaBorda.fora => w,
        PosicaoDaBorda.centro => w / 2,
        PosicaoDaBorda.dentro => 0.0,
      };
      final dentro = switch (st.posicao) {
        PosicaoDaBorda.dentro => w,
        PosicaoDaBorda.centro => w / 2,
        PosicaoDaBorda.fora => 0.0,
      };
      out = Stack(
        clipBehavior: Clip.none,
        children: [
          if (fora > 0.01)
            Positioned.fill(
              child: IgnorePointer(
                child: Opacity(
                  opacity: opacidade,
                  child: ImageFiltered(
                    imageFilter: ui.ImageFilter.dilate(
                      radiusX: fora,
                      radiusY: fora,
                    ),
                    child: _tinted(child, st.color),
                  ),
                ),
              ),
            ),
          out,
          if (dentro > 0.01)
            Positioned.fill(
              child: IgnorePointer(
                child: Opacity(
                  opacity: opacidade,
                  child: BlendMask(
                    blendMode: BlendMode.srcOver,
                    isolate: true,
                    child: Stack(
                      clipBehavior: Clip.none,
                      children: [
                        _tinted(child, st.color),
                        Positioned.fill(
                          child: BlendMask(
                            blendMode: BlendMode.dstOut,
                            child: ImageFiltered(
                              imageFilter: ui.ImageFilter.erode(
                                radiusX: dentro,
                                radiusY: dentro,
                              ),
                              child: child,
                            ),
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
              ),
            ),
        ],
      );
    }

    // Brilho externo: silhueta borrada e tingida, por tras.
    if (s.outerGlow?.enabled ?? false) {
      final g = s.outerGlow!;
      final size = g.size.valueAt(local);
      if (size > 0.01) {
        out = Stack(
          clipBehavior: Clip.none,
          children: [
            Positioned.fill(
              child: IgnorePointer(
                child: Opacity(
                  opacity: g.opacity.valueAt(local).clamp(0.0, 1.0),
                  child: ImageFiltered(
                    imageFilter: ui.ImageFilter.blur(
                      sigmaX: size / 2,
                      sigmaY: size / 2,
                      tileMode: TileMode.decal,
                    ),
                    child: _tinted(child, g.color),
                  ),
                ),
              ),
            ),
            out,
          ],
        );
      }
    }

    // Sombra projetada: silhueta deslocada, borrada e tingida, por tras.
    if (s.dropShadow?.enabled ?? false) {
      final d = s.dropShadow!;
      final off = d.offsetAt(local);
      final size = d.size.valueAt(local);
      final spread = d.spread.valueAt(local).clamp(0.0, 100.0);
      final filtered = size > 0.01 || spread > 0.01;
      out = Stack(
        clipBehavior: Clip.none,
        children: [
          Positioned.fill(
            child: IgnorePointer(
              child: Transform.translate(
                offset: off,
                child: Opacity(
                  opacity: d.opacity.valueAt(local).clamp(0.0, 1.0),
                  child: filtered
                      ? ImageFiltered(
                          imageFilter: _shadowImageFilter(
                            size,
                            spread,
                            compositionSize,
                          ),
                          child: _tinted(child, d.color),
                        )
                      : _tinted(child, d.color),
                ),
              ),
            ),
          ),
          out,
        ],
      );
    }

    // Sombra interna: mancha escura recortada pelo proprio alfa.
    if (s.innerShadow?.enabled ?? false) {
      final d = s.innerShadow!;
      final off = d.offsetAt(local);
      final size = d.size.valueAt(local);
      final spread = d.spread.valueAt(local).clamp(0.0, 100.0);
      out = Stack(
        clipBehavior: Clip.none,
        children: [
          out,
          Positioned.fill(
            child: IgnorePointer(
              child: Opacity(
                opacity: d.opacity.valueAt(local).clamp(0.0, 1.0),
                child: BlendMask(
                  blendMode: BlendMode.srcATop,
                  child: ImageFiltered(
                    imageFilter: _shadowImageFilter(
                      size,
                      spread,
                      compositionSize,
                    ),
                    child: Transform.translate(
                      offset: off,
                      child: _invertedSilhouette(child, d.color),
                    ),
                  ),
                ),
              ),
            ),
          ),
        ],
      );
    }
    return out;
  }

  /// A silhueta cresce antes do blur (espalhamento), e o conjunto roda
  /// em espaco linear para nao criar faixas/cinza nas bordas suaves.
  static ui.ImageFilter _shadowImageFilter(
    double blurSize,
    double spread,
    Size compositionSize,
  ) {
    final blur = ui.ImageFilter.blur(
      sigmaX: math.max(0.1, blurSize / 2),
      sigmaY: math.max(0.1, blurSize / 2),
      tileMode: TileMode.decal,
    );
    final filter = spread <= 0.01
        ? blur
        : ui.ImageFilter.compose(
            outer: blur,
            inner: ui.ImageFilter.dilate(radiusX: spread, radiusY: spread),
          );
    return LinearLight.wrap(filter, compositionSize);
  }

  /// Silhueta da camada pintada de uma cor so (usa o alfa como forma).
  static Widget _tinted(Widget child, Color color) => ColorFiltered(
    colorFilter: ColorFilter.mode(color, BlendMode.srcIn),
    child: child,
  );

  /// Negativo do alfa: onde a camada NAO esta, na cor dada — e o que
  /// forma a mancha da sombra interna.
  static Widget _invertedSilhouette(Widget child, Color color) => ColorFiltered(
    colorFilter: ColorFilter.mode(color, BlendMode.srcOut),
    child: child,
  );

  /// Tempo de CONTEUDO da camada depois do remapeamento (se houver).
  static Duration _remappedTime(Layer layer, Duration local) {
    // Video pergunta ao MESMO mapeamento da previa e da exportacao
    // (velocidade, reverso e extrapolacao inclusos); as demais camadas
    // seguram as pontas. As duas respostas vem do nucleo C++.
    if (layer is VideoLayer) {
      return hasTimeRemap(layer) ? videoSourceTimeAt(layer, local) : local;
    }
    for (final e in layer.effects) {
      if (!e.enabled || e.type != EffectType.timeRemap) continue;
      return remappedContentTime(e.track('tempo'), local, bakeUntil: layer.duration);
    }
    return local;
  }

  /// Tamanho da composicao — o raio dos efeitos de luz e uma FRACAO do
  /// menor lado, e o shader de gama precisa do tamanho para achar o
  /// pixel.
  int get fxWidth => ref.read(editorControllerProvider).outputWidth;
  int get fxHeight => ref.read(editorControllerProvider).outputHeight;

  /// Quadros por segundo da composicao — os efeitos de "one frame" contam
  /// quadros, nao segundos.
  int get fxFps {
    final f = ref.read(editorControllerProvider).fps;
    return f < 1 ? 30 : f;
  }

  /// BORDAS de uma camada deslocada: refletir (copias espelhadas coladas a
  /// cada lado), repetir (copias transladas) ou nada. Mesma regra do
  /// Shake: a borda do quadro continua parecendo imagem.
  Widget _comBordas(Widget c, int modo) => switch (modo) {
    0 => Stack(
      clipBehavior: Clip.none,
      children: [
        Transform.scale(scaleX: -1, alignment: Alignment.centerLeft, child: c),
        Transform.scale(scaleX: -1, alignment: Alignment.centerRight, child: c),
        Transform.scale(scaleY: -1, alignment: Alignment.topCenter, child: c),
        Transform.scale(scaleY: -1, alignment: Alignment.bottomCenter, child: c),
        c,
      ],
    ),
    1 => Stack(
      clipBehavior: Clip.none,
      children: [
        Transform.translate(offset: Offset(-fxWidth.toDouble(), 0), child: c),
        Transform.translate(offset: Offset(fxWidth.toDouble(), 0), child: c),
        Transform.translate(offset: Offset(0, -fxHeight.toDouble()), child: c),
        Transform.translate(offset: Offset(0, fxHeight.toDouble()), child: c),
        c,
      ],
    ),
    _ => c,
  };

  Size get fxSize => Size(fxWidth.toDouble(), fxHeight.toDouble());

  Widget _applyEffects(
    List<EffectInstance> effects,
    Widget child,
    Duration local,
  ) {
    var out = child;
    for (final effect in effects) {
      if (!effect.enabled) continue;
      if (PixelEffectEngine.ready && pixelKernels.containsKey(effect.type)) {
        out = PixelEffectPass(
          key: ValueKey('pixel-effect-${effect.id}'),
          frame: PixelEffectFrame.of(
            effect,
            local,
            pixelScale: math.min(fxWidth, fxHeight) / 1080.0,
          ),
          child: out,
        );
        continue;
      }
      switch (effect.type) {
        // EFEITOS QUE SO EXISTEM NO SHADER. Recorte por pixel e contorno
        // por vizinhanca nao tem versao em CPU que valha: seria um laco
        // sobre dois milhoes de pixels por quadro, na thread de UI — o
        // que trava o app. Sem o motor de pixel (aparelho sem suporte a
        // shader como filtro) o efeito fica NEUTRO, e a ficha do efeito
        // avisa que ele precisa da GPU.
        case EffectType.chromaKey:
        case EffectType.lumaKey:
        case EffectType.colorKey:
        case EffectType.findEdges:
        // Cor seletiva depende da faixa de CADA pixel (qual canal manda,
        // quanto e branco, neutro ou preto): nao ha matriz honesta.
        case EffectType.selectiveColor:
        // Fatias de glitch deslocam linhas inteiras de pixels: so no shader.
        case EffectType.sliceGlitch:
        // S_MathOps le a camada desfocada e a mascara de luma em volta de
        // cada pixel: a operacao nao cabe numa matriz de cor.
        case EffectType.mathOps:
        // S_Sharpen compara cada pixel com o desfoque em anel: so no shader.
        case EffectType.sSharpen:
          break;

        case EffectType.gaussianBlur:
          // NIVEL 3: o raio e pixel (pensado em 1080p), a borda decide o
          // que existe fora da camada, e a qualidade escolhe entre a
          // conta em espaco linear (certa) e o desfoque direto (barato).
          final sigma = pxAt1080(
            effect.paramAt('raio', local).clamp(0.0, 500.0),
            fxWidth,
            fxHeight,
          );
          if (sigma > 0.01) {
            final tile = switch (effect
                .paramAt('borda', local)
                .round()
                .clamp(0, 2)) {
              1 => ui.TileMode.repeated,
              2 => ui.TileMode.mirror,
              _ => ui.TileMode.decal,
            };
            out = effect.paramAt('qualidade', local) > 0.5
                ? LinearLight.blurred(
                    sigmaX: sigma,
                    sigmaY: sigma,
                    size: fxSize,
                    tileMode: tile,
                    child: out,
                  )
                : ImageFiltered(
                    imageFilter: ui.ImageFilter.blur(
                      sigmaX: sigma,
                      sigmaY: sigma,
                      tileMode: tile,
                    ),
                    child: out,
                  );
          }

        case EffectType.lightGlow:
          // NIVEL 3. Tres numeros no montar: limite (%), raio (px) e
          // intensidade (%, ate 400 — estourar e uma escolha). No
          // avancado entram a mesclagem, a piramide e o multiplicador
          // por canal.
          //
          // A PIRAMIDE e o que faz halo grande sem pagar o raio inteiro:
          // cada nivel dobra o sigma e vale metade, somando um halo
          // largo e barato por cima do nucleo apertado.
          final intensidade = (effect.paramAt('intensity', local) / 100).clamp(
            0.0,
            4.0,
          );
          if (intensidade > 0.004) {
            final raio = pxAt1080(
              effect.paramAt('raio', local).clamp(0.0, 500.0),
              fxWidth,
              fxHeight,
            );
            final sigma = math.max(0.6, raio);
            final th = (effect.paramAt('threshold', local) / 100).clamp(
              0.0,
              0.98,
            );
            final niveis = effect
                .paramAt('piramide', local)
                .round()
                .clamp(1, _rascunho ? 2 : 5);
            final multR = effect.paramAt('mult_r', local).clamp(0.0, 2.0);
            final multG = effect.paramAt('mult_g', local).clamp(0.0, 2.0);
            final multB = effect.paramAt('mult_b', local).clamp(0.0, 2.0);
            final modo = switch (effect
                .paramAt('mesclagem', local)
                .round()
                .clamp(0, 2)) {
              1 => BlendMode.screen,
              2 => BlendMode.lighten,
              _ => BlendMode.plus,
            };

            // O LIMITE: o que esta abaixo vai a zero ANTES do desfoque —
            // brilho e o que passa de um ponto, nao a imagem inteira
            // borrada. O ganho da intensidade entra aqui junto, para o
            // halo nascer forte e nao ser multiplicado depois de somado.
            final e = 1 / (1 - th);
            final g = e * intensidade;
            final o = -th * 255 * e * intensidade;
            Widget fonte = ColorFiltered(
              colorFilter: ColorFilter.matrix(<double>[
                g * multR,
                0,
                0,
                0,
                o,
                0,
                g * multG,
                0,
                0,
                o,
                0,
                0,
                g * multB,
                0,
                o,
                0,
                0,
                0,
                1,
                0,
              ]),
              child: out,
            );
            if (PixelEffectEngine.ready) {
              fonte = PixelEffectPass(
                frame: PixelEffectFrame(21, [
                  th,
                  .25,
                  0,
                  0,
                  multR,
                  multG,
                  multB,
                  0,
                  0,
                  1,
                ]),
                child: out,
              );
              fonte = PixelEffectPass(
                frame: PixelEffectFrame(22, [
                  math.log(intensidade) / math.ln2,
                  3,
                  0,
                ]),
                child: fonte,
              );
            }
            final tingido = ColorFiltered(
              // srcATop substituia o RGB extraido pela cor solida e
              // ressuscitava pixels abaixo do threshold. Multiplicar
              // preserva o preto (sem luz) e o ganho da intensidade.
              colorFilter: ColorFilter.mode(effect.color, BlendMode.modulate),
              child: fonte,
            );

            final pesos = <double>[
              for (var k = 0; k < niveis; k++) 1 / (1 << k),
            ];
            final soma = pesos.fold<double>(0, (a, b) => a + b);
            // O TETO. O preset Neon (raio 60, piramide 4) pedia sigma
            // 480 no ultimo nivel; o Sonho, 2400. Cada um desses e uma
            // textura de centenas de megabytes na GPU — e o app fechava
            // antes de desenhar o quadro. Ver [sigmaTeto].
            final piramide = piramideAteOTeto(
              [for (var k = 0; k < niveis; k++) sigma * (1 << k)],
              [for (var k = 0; k < niveis; k++) pesos[k] / soma],
              sigmaTeto(fxWidth, fxHeight),
            );
            out = Stack(
              clipBehavior: Clip.none,
              children: [
                out,
                for (final nivel in piramide)
                  BlendMask(
                    blendMode: modo,
                    margem: 3 * nivel.sigma + 4,
                    child: Opacity(
                      opacity: nivel.peso.clamp(0.0, 1.0),
                      // EM ESPACO LINEAR: glow SOMA luz, e soma de luz em
                      // sRGB da o halo lavado com borda escura de sempre.
                      child: LinearLight.blurred(
                        sigmaX: nivel.sigma,
                        sigmaY: nivel.sigma,
                        size: fxSize,
                        child: tingido,
                      ),
                    ),
                  ),
              ],
            );
          }

        case EffectType.flicker:
          // FLICKER: a camada pisca. Aleatorio e a lampada ruim; strobe e
          // a balada; senoide e a respiracao. Age na opacidade ou no
          // brilho — no brilho a camada nao some, so escurece.
          final amt = effect.paramAt('amount', local).clamp(0.0, 1.0);
          if (amt > 0.004) {
            final freq = effect.paramAt('frequency', local).clamp(0.5, 60.0);
            final estilo = effect.paramAt('style', local).round().clamp(0, 2);
            final alvo = effect.paramAt('target', local).round().clamp(0, 1);
            final seedF = effect.paramAt('seed', local).round();
            final x = local.inMicroseconds / 1e6 * freq;
            final onda = switch (estilo) {
              1 => (x - x.floor()) < 0.5 ? 1.0 : -1.0,
              2 => math.sin(x * 2 * math.pi),
              _ => fxNoiseSigned(seedF + 7, 3, x),
            };
            // 0..1: quanto da camada FICA neste instante.
            final k = (1 - amt * (0.5 - 0.5 * onda)).clamp(0.0, 1.0);
            if (alvo == 0) {
              out = Opacity(opacity: k, child: out);
            } else {
              out = ColorFiltered(
                colorFilter: ColorFilter.matrix(<double>[
                  k, 0, 0, 0, 0, //
                  0, k, 0, 0, 0,
                  0, 0, k, 0, 0,
                  0, 0, 0, 1, 0,
                ]),
                child: out,
              );
            }
          }

        // S_FLICKER: o ganho RGB do quadro e uma conta pura de (tempo,
        // semente) e entra como UMA matriz de cor — exato na previa, na
        // exportacao e no teste, sem textura a mais. As frequencias sao
        // integradas no tempo: animar a frequencia acelera sem tranco.
        case EffectType.sFlicker:
          final ganhoDoPisca = ganhoDoSFlicker(
            faseAleatoria: integratedPhase(effect.track('rand_freq'), local),
            faseDaOnda: integratedPhase(effect.track('wave_freq'), local),
            amplitude: effect.paramAt('amplitude', local),
            brilhoAleatorio: effect.paramAt('rand_luma_amp', local),
            corAleatoria: effect.paramAt('rand_color_amp', local),
            amplitudeDaOnda: effect.paramAt('wave_amp', local),
            faseR: effect.paramAt('wave_red_phase', local),
            faseG: effect.paramAt('wave_green_phase', local),
            faseB: effect.paramAt('wave_blue_phase', local),
            forcaR: effect.paramAt('red_amp', local),
            forcaG: effect.paramAt('green_amp', local),
            forcaB: effect.paramAt('blue_amp', local),
            brilho: effect.paramAt('brightness', local),
            semente: effect.paramAt('seed', local).round(),
          );
          // Ganho 1 nos tres canais nao embrulha nada: um filtro a menos
          // na arvore.
          if ((ganhoDoPisca.r - 1).abs() > 1e-6 ||
              (ganhoDoPisca.g - 1).abs() > 1e-6 ||
              (ganhoDoPisca.b - 1).abs() > 1e-6) {
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(matrizDeGanho(ganhoDoPisca)),
              child: out,
            );
          }

        case EffectType.gradient4:
          // GRADIENTE DE QUATRO CORES sobre a camada, preso ao alfa dela
          // (srcATop): cada canto uma cor. Girar troca os cantos de lugar.
          final opG = effect.paramAt('opacity', local).clamp(0.0, 1.0);
          if (opG > 0.004) {
            final mescla = effect.paramAt('blend', local).round().clamp(0, 3);
            final giro = effect.paramAt('angle', local) * math.pi / 180;
            final cores = [
              effect.color,
              effect.extraColor(0),
              effect.extraColor(2),
              effect.extraColor(1),
            ];
            final modoG = switch (mescla) {
              1 => BlendMode.multiply,
              2 => BlendMode.screen,
              3 => BlendMode.overlay,
              _ => BlendMode.srcATop,
            };
            out = Stack(
              clipBehavior: Clip.none,
              children: [
                out,
                Positioned.fill(
                  child: IgnorePointer(
                    child: BlendMask(
                      blendMode: mescla == 0 ? BlendMode.srcATop : modoG,
                      child: mescla == 0
                          ? Transform.rotate(
                              angle: giro,
                              child: CustomPaint(
                                painter: Gradient4Painter(
                                  topLeft: cores[0],
                                  topRight: cores[1],
                                  bottomLeft: cores[2],
                                  bottomRight: cores[3],
                                  opacity: opG,
                                ),
                              ),
                            )
                          // Com mescla, o gradiente ainda fica preso ao alfa
                          // da camada: srcATop por dentro, mescla por fora.
                          : BlendMask(
                              blendMode: BlendMode.srcATop,
                              child: Transform.rotate(
                                angle: giro,
                                child: CustomPaint(
                                  painter: Gradient4Painter(
                                    topLeft: cores[0],
                                    topRight: cores[1],
                                    bottomLeft: cores[2],
                                    bottomRight: cores[3],
                                    opacity: opG,
                                  ),
                                ),
                              ),
                            ),
                    ),
                  ),
                ),
              ],
            );
          }

        case EffectType.liquidGlass:
          // LIQUID GLASS: a camada vira uma placa de vidro sobre o que
          // esta atras — desfoque do fundo, leve LENTE (o fundo cresce
          // um pouco por baixo do vidro), tingimento, brilho especular
          // correndo pela borda de cima e sombra por baixo. A camada em
          // si (texto, icone) fica por cima, nitida.
          final blurLG = effect.paramAt('blur', local).clamp(0.0, 40.0);
          final saturacaoLG =
              effect.paramAt('saturation', local).clamp(0.0, 200.0) / 100;
          final brilhoLG =
              effect.paramAt('brightness', local).clamp(0.0, 200.0) / 100;
          final grainLG = effect.paramAt('grain', local).clamp(0.0, 0.12);
          final refr = effect.paramAt('refraction', local).clamp(0.0, 1.0);
          final rimLG = effect.paramAt('rim', local).clamp(0.0, 1.0);
          final tintLG = effect.paramAt('tint', local).clamp(0.0, 1.0);
          final raioLG = effect.paramAt('radius', local).clamp(0.0, 200.0);
          final sombraLG = effect.paramAt('shadow', local).clamp(0.0, 1.0);
          final folgaLG = effect.paramAt('padding', local).clamp(0.0, 120.0);
          final bordaLG = BorderRadius.circular(raioLG);
          // O caminho rapido so vale quando TODO parametro que pinta pixels
          // esta neutro. Blur/cor neutros nao podem desligar grao, tinta,
          // borda, refracao ou sombra configurados no painel avancado.
          final neutroLG =
              blurLG <= 0.001 &&
              (saturacaoLG - 1).abs() <= 0.001 &&
              (brilhoLG - 1).abs() <= 0.001 &&
              grainLG <= 0.0001 &&
              refr <= 0.001 &&
              rimLG <= 0.001 &&
              tintLG <= 0.001 &&
              sombraLG <= 0.001;
          if (!neutroLG) {
            out = Stack(
              clipBehavior: Clip.none,
              children: [
                Positioned(
                  left: -folgaLG,
                  top: -folgaLG,
                  right: -folgaLG,
                  bottom: -folgaLG,
                  child: IgnorePointer(
                    child: LayoutBuilder(
                      builder: (context, c) {
                        final w = c.maxWidth, h = c.maxHeight;
                        final k = 1 + refr * 0.12;
                        // Lente: escala o fundo em torno do centro da placa.
                        final lente = Matrix4.identity()
                          ..translateByDouble(w / 2, h / 2, 0, 1)
                          ..scaleByDouble(k, k, 1, 1)
                          ..translateByDouble(-w / 2, -h / 2, 0, 1);
                        final optico = ui.ImageFilter.compose(
                          outer: ui.ImageFilter.blur(
                            sigmaX: blurLG,
                            sigmaY: blurLG,
                            tileMode: TileMode.mirror,
                          ),
                          inner: ui.ImageFilter.matrix(
                            lente.storage,
                            filterQuality: FilterQuality.medium,
                          ),
                        );
                        return Stack(
                          children: [
                            if (sombraLG > 0.01)
                              Positioned.fill(
                                child: DecoratedBox(
                                  decoration: BoxDecoration(
                                    borderRadius: bordaLG,
                                    boxShadow: [
                                      BoxShadow(
                                        color: Colors.black.withValues(
                                          alpha: 0.45 * sombraLG,
                                        ),
                                        blurRadius: 28,
                                        offset: const Offset(0, 12),
                                      ),
                                    ],
                                  ),
                                ),
                              ),
                            Positioned.fill(
                              child: ClipRRect(
                                borderRadius: bordaLG,
                                child: ColorFiltered(
                                  colorFilter: ColorFilter.matrix(
                                    _saturationBrightnessMatrix(
                                      saturacaoLG,
                                      brilhoLG,
                                    ),
                                  ),
                                  child: BackdropFilter(
                                    // O blur acontece entre as curvas sRGB/linear;
                                    // o ajuste de cor e aplicado ao passe pronto.
                                    filter: LinearLight.wrap(
                                      optico,
                                      Size(w, h),
                                    ),
                                    child: DecoratedBox(
                                      decoration: BoxDecoration(
                                        borderRadius: bordaLG,
                                        color: effect.color.withValues(
                                          alpha: tintLG,
                                        ),
                                        border: Border.all(
                                          color: Colors.white.withValues(
                                            alpha: 0.55 * rimLG,
                                          ),
                                          width: 1.2,
                                        ),
                                      ),
                                    ),
                                  ),
                                ),
                              ),
                            ),
                            // O reflexo especular: claro em cima e a esquerda,
                            // um fio claro embaixo — a luz passando pela curva.
                            Positioned.fill(
                              child: DecoratedBox(
                                decoration: BoxDecoration(
                                  borderRadius: bordaLG,
                                  gradient: LinearGradient(
                                    begin: Alignment.topLeft,
                                    end: Alignment.bottomRight,
                                    colors: [
                                      Colors.white.withValues(
                                        alpha: 0.38 * rimLG,
                                      ),
                                      Colors.white.withValues(
                                        alpha: 0.06 * rimLG,
                                      ),
                                      Colors.transparent,
                                      Colors.white.withValues(
                                        alpha: 0.14 * rimLG,
                                      ),
                                    ],
                                    stops: const [0, 0.3, 0.7, 1],
                                  ),
                                ),
                              ),
                            ),
                            if (grainLG > 0.0001)
                              Positioned.fill(
                                child: ClipRRect(
                                  borderRadius: bordaLG,
                                  child: IgnorePointer(
                                    child: BlendMask(
                                      blendMode: BlendMode.overlay,
                                      child: CustomPaint(
                                        painter: _GrainPainter(
                                          amount: grainLG,
                                          size: 0.7,
                                          seed: 8606,
                                          time: local,
                                        ),
                                      ),
                                    ),
                                  ),
                                ),
                              ),
                          ],
                        );
                      },
                    ),
                  ),
                ),
                out,
              ],
            );
          }

        case EffectType.tint:
          final forcaTint = effect.paramAt('strength', local).clamp(0.0, 1.0);
          // Em forca zero o srcATop ja devolvia o destino intacto — mas
          // pagava uma camada de composicao para isso.
          if (forcaTint > 0.004) {
            out = ColorFiltered(
              colorFilter: ColorFilter.mode(
                effect.color.withValues(alpha: forcaTint),
                BlendMode.srcATop,
              ),
              child: out,
            );
          }

        case EffectType.glowVol:
          // DEEP GLOW: piramide de bloom com pesos NORMALIZADOS, em
          // espaco linear. "Conservacao de energia" quer dizer isto: a
          // soma dos pesos e 1, entao acrescentar nivel deixa o glow
          // mais suave sem deixa-lo mais claro.
          final exposure = effect.paramAt('exposure', local);
          final ganho = exposureGain(exposure).clamp(0.0, 8.0);
          if (ganho > 0.01) {
            final r = effect.paramAt('radius', local).clamp(0.0, 1.0);
            final raioPx = radiusToPixels(
              r,
              fxWidth,
              fxHeight,
            ).clamp(1.0, 2000.0);
            final quality = effect
                .paramAt('quality', local)
                .round()
                .clamp(0, 2);
            final niveis = _rascunho
                ? math.min(2, bloomLevels(quality))
                : bloomLevels(quality);
            final pesos = bloomWeights(niveis);
            final sigmas = bloomSigmas(raioPx, niveis);

            final limiar = effect.paramAt('threshold', local);
            final suavidade = effect.paramAt('threshold_softness', local);
            final aspecto = effect
                .paramAt('aspect_ratio', local)
                .clamp(0.1, 10.0);
            final satur = effect.paramAt('glow_saturation', local) / 100.0;
            final tintAmt = effect.paramAt('tint_amount', local);
            final tintMode = effect
                .paramAt('tint_mode', local)
                .round()
                .clamp(0, 3);
            final multR = effect.paramAt('red_radius_multiplier', local);
            final multG = effect.paramAt('green_radius_multiplier', local);
            final multB = effect.paramAt('blue_radius_multiplier', local);
            final porCanal =
                (multR - multG).abs() > 0.01 || (multG - multB).abs() > 0.01;
            final soGlow = effect.paramAt('glow_only', local) >= 0.5;
            final blend = effect
                .paramAt('blend_mode', local)
                .round()
                .clamp(0, 2);
            final anguloOn = effect.paramAt('enable_angle', local) >= 0.5;
            final anguloRad = effect.paramAt('angle', local) * math.pi / 180;
            // 0 = Luminance, 1 = Chrominance.
            final modoLimiar = effect
                .paramAt('threshold_mode', local)
                .round()
                .clamp(0, 1);
            // 0 = Exponential, 1 = Iris.
            final modoGlow = effect
                .paramAt('glow_mode', local)
                .round()
                .clamp(0, 1);
            final reducaoRuido = effect
                .paramAt('noise_reduction', local)
                .clamp(0.0, 100.0);
            final reducao = effect.paramAt('downsample', local).clamp(1.0, 8.0);
            final tonemapping = effect
                .paramAt('tonemapping', local)
                .round()
                .clamp(0, 3);
            final lensDirt = effect
                .paramAt('lens_dirt_amount', local)
                .clamp(0.0, 200.0);

            // LIMIAR: so o que passa do valor vira glow. A rampa suave
            // evita a linha reta onde o brilho cruza o limiar — com
            // limiar duro, o glow "liga" de repente no meio do degrade.
            Widget fonte = out;

            // REDUCAO DE RUIDO, antes do limiar.
            //
            // Granulacao de sensor tem pixels isolados acima do limiar, e
            // cada um vira uma estrelinha cintilando de quadro em quadro.
            // Um desfoque minimo antes do corte tira o pixel solto e
            // deixa passar o que e area clara de verdade.
            if (reducaoRuido > 0.5) {
              final sr = reducaoRuido / 100 * 3.0;
              fonte = LinearLight.blurred(
                sigmaX: sr,
                sigmaY: sr,
                size: fxSize,
                child: fonte,
              );
            }

            if (PixelEffectEngine.ready) {
              // Per-pixel luminance/chroma selection preserves alpha. Tone
              // response is applied to the source before the SDR blur pyramid;
              // this is deliberately not described as an HDR intermediate.
              fonte = PixelEffectPass(
                frame: PixelEffectFrame(
                  21,
                  [
                    limiar,
                    suavidade,
                    0,
                    modoLimiar.toDouble(),
                    1,
                    1,
                    1,
                    tintMode.toDouble(),
                    tintAmt,
                    satur,
                  ],
                  color: [
                    effect.color.r,
                    effect.color.g,
                    effect.color.b,
                    effect.color.a,
                  ],
                  extraColors: [
                    for (final c in effect.extraColors) ...[c.r, c.g, c.b, c.a],
                  ],
                ),
                child: fonte,
              );
              fonte = PixelEffectPass(
                frame: PixelEffectFrame(22, [
                  exposure.clamp(-8.0, 3.0),
                  tonemapping.toDouble(),
                  lensDirt,
                ]),
                child: fonte,
              );
            } else if (limiar > 0.01) {
              // O limiar REMAPEIA (limiar -> 0, branco -> 1); a conta
              // mora em bloom.dart, onde da para testa-la.
              final (escala, desl) = glowThresholdMatrix(
                limiar,
                suavidade,
                ganho,
              );
              // O QUE O LIMIAR MEDE.
              //
              // Luminancia: passa o que e CLARO — o caso comum, e o que
              // faz o glow morar nos realces.
              // Crominancia: passa o que e COLORIDO, subtraindo o cinza
              // de cada canal. Um neon saturado sobre fundo claro nao
              // ganha glow por luminancia (o fundo e tao claro quanto);
              // por crominancia, so o neon brilha.
              fonte = ColorFiltered(
                colorFilter: ColorFilter.matrix(
                  modoLimiar == 1
                      ? <double>[
                          escala * (1 - 0.2126), -escala * 0.7152,
                          -escala * 0.0722, 0, desl, //
                          -escala * 0.2126, escala * (1 - 0.7152),
                          -escala * 0.0722, 0, desl,
                          -escala * 0.2126, -escala * 0.7152,
                          escala * (1 - 0.0722), 0, desl,
                          0, 0, 0, 1, 0,
                        ]
                      : <double>[
                          escala, 0, 0, 0, desl, //
                          0, escala, 0, 0, desl,
                          0, 0, escala, 0, desl,
                          0, 0, 0, 1, 0,
                        ],
                ),
                child: fonte,
              );
            }

            // DOWNSAMPLE: quanto detalhe o halo guarda.
            //
            // Encolher e devolver ao tamanho apaga o detalhe fino do
            // halo — que e o mesmo resultado de calcular o glow numa
            // resolucao menor, que e o que o nome promete. Em 1 nao
            // acontece nada.
            if (reducao > 1.01) {
              fonte = ImageFiltered(
                imageFilter: ui.ImageFilter.compose(
                  outer: ui.ImageFilter.matrix(
                    Matrix4.diagonal3Values(reducao, reducao, 1).storage,
                    filterQuality: FilterQuality.low,
                  ),
                  inner: ui.ImageFilter.matrix(
                    Matrix4.diagonal3Values(
                      1 / reducao,
                      1 / reducao,
                      1,
                    ).storage,
                    filterQuality: FilterQuality.low,
                  ),
                ),
                child: fonte,
              );
            }
            if (!PixelEffectEngine.ready && (satur - 1).abs() > 0.01) {
              fonte = ColorFiltered(
                colorFilter: ColorFilter.matrix(_saturationMatrix(satur)),
                child: fonte,
              );
            }
            if (!PixelEffectEngine.ready && tintMode != 0 && tintAmt > 0.01) {
              fonte = ColorFiltered(
                colorFilter: ColorFilter.mode(
                  effect.color.withValues(alpha: tintAmt),
                  BlendMode.srcATop,
                ),
                child: fonte,
              );
            }

            Widget borra(Widget c, double sigma, double mult) {
              // ASPECTO e ANGULO: um glow anamorfico se espalha mais num
              // eixo. O angulo gira a fonte, borra e desgira.
              final sx = sigma * mult * aspecto;
              final sy = sigma * mult / aspecto;
              Widget alvo = c;
              if (anguloOn && anguloRad.abs() > 0.001) {
                alvo = Transform.rotate(angle: -anguloRad, child: alvo);
              }
              if (modoGlow == 1) {
                // IRIS: o halo ganha as PONTAS da abertura da lente.
                //
                // Bloom exponencial e redondo por construcao — e o que
                // uma gaussiana faz. A estrela que se ve em foto vem das
                // laminas do diafragma, e se reproduz somando desfoques
                // muito alongados em direcoes diferentes. Tres eixos ja
                // dao a leitura de seis pontas.
                Widget lamina(double giro) {
                  final r = LinearLight.blurred(
                    sigmaX: math.max(0.1, sx * 2.2),
                    sigmaY: math.max(0.1, sy * 0.18),
                    size: fxSize,
                    child: Transform.rotate(angle: -giro, child: alvo),
                  );
                  return Transform.rotate(angle: giro, child: r);
                }

                final alcanceLamina = 3 * sx * 2.2 + 4;
                alvo = Stack(
                  clipBehavior: Clip.none,
                  children: [
                    lamina(0),
                    BlendMask(
                      blendMode: BlendMode.plus,
                      margem: alcanceLamina,
                      child: lamina(math.pi / 3),
                    ),
                    BlendMask(
                      blendMode: BlendMode.plus,
                      margem: alcanceLamina,
                      child: lamina(2 * math.pi / 3),
                    ),
                  ],
                );
              } else {
                alvo = LinearLight.blurred(
                  sigmaX: math.max(0.1, sx),
                  sigmaY: math.max(0.1, sy),
                  size: fxSize,
                  child: alvo,
                );
              }
              if (anguloOn && anguloRad.abs() > 0.001) {
                alvo = Transform.rotate(angle: anguloRad, child: alvo);
              }
              return alvo;
            }

            // ALCANCE do halo de um nivel: ate onde o desfoque chega fora
            // da caixa. E a margem que a foto da mescla precisa ter.
            double alcance(double sigma) {
              final mult = math.max(
                math.max(multR, multG),
                math.max(multB, 1.0),
              );
              final eixo = math.max(aspecto, 1 / aspecto);
              final bruto =
                  3 * sigma * mult * eixo * (modoGlow == 1 ? 2.2 : 1.0) + 4;
              // A margem multiplicava o sigma pelo canal (ate 2x), pela
              // proporcao (ate 10x) e pelo modo (2,2x): um raio grande
              // pedia noventa mil pixels de margem de cada lado. Nada do
              // que passa da composicao inteira aparece — o resto e so
              // textura que o aparelho nao tem.
              return math.min(bruto, math.max(fxWidth, fxHeight) * 1.0);
            }

            Widget nivel(double sigma, double peso) {
              final w = porCanal
                  ? Stack(
                      clipBehavior: Clip.none,
                      children: [
                        borra(_channelIso(fonte, 0), sigma, multR),
                        BlendMask(
                          blendMode: BlendMode.plus,
                          margem: alcance(sigma),
                          child: borra(_channelIso(fonte, 1), sigma, multG),
                        ),
                        BlendMask(
                          blendMode: BlendMode.plus,
                          margem: alcance(sigma),
                          child: borra(_channelIso(fonte, 2), sigma, multB),
                        ),
                      ],
                    )
                  : borra(fonte, sigma, 1);
              // O ganho ja entrou na FONTE, junto do limiar; aqui so
              // o peso do nivel. Multiplicar de novo seria contar a
              // exposicao duas vezes.
              return Opacity(opacity: peso.clamp(0.0, 1.0), child: w);
            }

            // O TETO, igual ao do Glow: com qualidade Alta a piramide
            // vai a cinco niveis e o ultimo sigma e dezesseis vezes o
            // raio. Ver [sigmaTeto] e [piramideAteOTeto].
            final piramide = piramideAteOTeto(
              sigmas,
              pesos,
              sigmaTeto(fxWidth, fxHeight),
            );
            final camadas = <(Widget, double)>[
              for (final n in piramide)
                (nivel(n.sigma, n.peso), alcance(n.sigma)),
            ];

            // BLEND: Add e o padrao — luz soma. Screen e mais suave nas
            // altas; Normal cobre.
            final modo = switch (blend) {
              1 => BlendMode.screen,
              2 => BlendMode.srcOver,
              _ => BlendMode.plus,
            };

            out = soGlow
                // GLOW ONLY: so o brilho, sem a fonte. Serve para mandar
                // o glow para outra camada e mesclar la. Os niveis se
                // somam entre si.
                ? Stack(
                    clipBehavior: Clip.none,
                    children: [
                      camadas.first.$1,
                      for (final (c, a) in camadas.skip(1))
                        BlendMask(
                          blendMode: BlendMode.plus,
                          margem: a,
                          child: c,
                        ),
                    ],
                  )
                // A FONTE EMBAIXO, O GLOW POR CIMA. Com a fonte por
                // ultimo, o solido cobria o brilho e o glow so aparecia
                // pela borda de fora — um contorno, nao um glow. O Deep
                // Glow soma luz em cima de tudo: o miolo claro tambem
                // acende, e e isso que faz um texto branco "queimar".
                : Stack(
                    clipBehavior: Clip.none,
                    children: [
                      out,
                      for (final (c, a) in camadas)
                        BlendMask(blendMode: modo, margem: a, child: c),
                    ],
                  );
          }

        // COLORING SEM O MOTOR DE PIXEL (aparelho sem shader como
        // filtro). Com o motor, estes efeitos nem chegam aqui: rodam no
        // shader, modos 37 a 43. Sem ele, a parte LINEAR de cada conta
        // vira matriz de cor — brilho/contraste, mistura de canais e o
        // filtro de foto saem exatos; o que depende da faixa tonal (cor
        // seletiva, sombras x altas, Soft Light) fica aproximado ou
        // neutro, e nunca some com a camada.
        case EffectType.brightnessContrast:
          out = ColorFiltered(
            colorFilter: ColorFilter.matrix(
              brightnessContrastMatrix(
                effect.paramAt('brightness', local),
                effect.paramAt('contrast', local),
              ),
            ),
            child: out,
          );

        case EffectType.channelMixer:
          out = ColorFiltered(
            colorFilter: ColorFilter.matrix(
              channelMixerMatrix(
                vermelho: [
                  effect.paramAt('red_red', local),
                  effect.paramAt('red_green', local),
                  effect.paramAt('red_blue', local),
                  effect.paramAt('red_const', local),
                ],
                verde: [
                  effect.paramAt('green_red', local),
                  effect.paramAt('green_green', local),
                  effect.paramAt('green_blue', local),
                  effect.paramAt('green_const', local),
                ],
                azul: [
                  effect.paramAt('blue_red', local),
                  effect.paramAt('blue_green', local),
                  effect.paramAt('blue_blue', local),
                  effect.paramAt('blue_const', local),
                ],
                monocromatico: effect.paramAt('monochrome', local) >= .5,
              ),
            ),
            child: out,
          );

        case EffectType.photoFilter:
          // Sem shader nao ha como devolver a luminancia pixel a pixel:
          // preservar so suaviza o filtro.
          final preservaFiltro = effect.paramAt('preserve_luminosity', local);
          out = ColorFiltered(
            colorFilter: ColorFilter.matrix(
              photoFilterMatrix(
                temperatura: effect.paramAt('mode', local) >= .5,
                densidade: effect.paramAt('density', local) *
                    (preservaFiltro >= .5 ? .8 : 1),
                kelvin: effect.paramAt('temperature', local),
                cor: (r: effect.color.r, g: effect.color.g, b: effect.color.b),
              ),
            ),
            child: out,
          );

        case EffectType.colorBalance:
          out = ColorFiltered(
            colorFilter: ColorFilter.matrix(
              colorBalanceMatrix(
                sombras: [
                  effect.paramAt('shadow_red', local),
                  effect.paramAt('shadow_green', local),
                  effect.paramAt('shadow_blue', local),
                ],
                meios: [
                  effect.paramAt('midtone_red', local),
                  effect.paramAt('midtone_green', local),
                  effect.paramAt('midtone_blue', local),
                ],
                altas: [
                  effect.paramAt('highlight_red', local),
                  effect.paramAt('highlight_green', local),
                  effect.paramAt('highlight_blue', local),
                ],
                preservarLuminosidade:
                    effect.paramAt('preserve_luminosity', local) >= .5,
              ),
            ),
            child: out,
          );

        case EffectType.colorTune:
          RodaDeCor roda(String nome) => RodaDeCor(
            matiz: effect.paramAt('${nome}_hue', local),
            saturacao: effect.paramAt('${nome}_saturation', local),
            luminancia: effect.paramAt('${nome}_luminance', local),
          );
          // O gamma nao cabe numa matriz: sem shader, so a luminancia
          // dele entra, como ganho medio.
          final gammaRoda = roda('gamma');
          final ganho = roda('gain');
          out = ColorFiltered(
            colorFilter: ColorFilter.matrix(
              colorTuneMatrix(
                lift: roda('lift'),
                gain: RodaDeCor(
                  matiz: ganho.matiz,
                  saturacao: ganho.saturacao,
                  luminancia: ganho.luminancia + gammaRoda.luminancia * .25,
                ),
                offset: roda('offset'),
              ),
            ),
            child: out,
          );

        case EffectType.gradientMap:
          // Duas paradas no modo Normal e exato; os outros modos ficam
          // aproximados por ele, com metade da opacidade.
          final modoMapa = effect.paramAt('blend_mode', local).round();
          final meioTom = effect.paramAt('midtones', local) >= .5;
          // A reta que substitui as tres paradas passa pelo meio-tom tanto
          // mais cedo quanto mais baixo o ponto medio.
          final pesoDaLuz =
              1 - (effect.paramAt('balance', local) / 100).clamp(.05, .95) * .5;
          final sombra = effect.color;
          final luz = effect.extraColor(1);
          final meio = effect.extraColor(0);
          out = ColorFiltered(
            colorFilter: ColorFilter.matrix(
              gradientMapMatrix(
                sombra: (r: sombra.r, g: sombra.g, b: sombra.b),
                luz: meioTom
                    ? (
                        r: meio.r + (luz.r - meio.r) * pesoDaLuz,
                        g: meio.g + (luz.g - meio.g) * pesoDaLuz,
                        b: meio.b + (luz.b - meio.b) * pesoDaLuz,
                      )
                    : (r: luz.r, g: luz.g, b: luz.b),
                opacidade:
                    effect.paramAt('opacity', local) *
                    (modoMapa == 0 ? 1 : .5),
              ),
            ),
            child: out,
          );

        // LOOKS: uma matriz por look, misturada com a identidade pela forca.
        case EffectType.looks:
          out = ColorFiltered(
            colorFilter: ColorFilter.matrix(
              matrizDoLook(
                effect.paramAt('look', local).round(),
                effect.paramAt('forca', local),
              ),
            ),
            child: out,
          );

        // HUE/SATURATION SEM O MOTOR DE PIXEL: a matriz aproximada. A conta
        // exata (saturacao pelo croma, colorir pelo HSL) roda no modo 45.
        case EffectType.hueSaturation:
          out = ColorFiltered(
            colorFilter: ColorFilter.matrix(
              hueSaturationMatrix(
                matiz: effect.paramAt('master_hue', local),
                saturacao: effect.paramAt('master_saturation', local),
                luminosidade: effect.paramAt('master_lightness', local),
                colorir: effect.paramAt('colorize', local) >= .5,
                matizColorir: effect.paramAt('colorize_hue', local),
                saturacaoColorir: effect.paramAt('colorize_saturation', local),
              ),
            ),
            child: out,
          );

        // ---------------------------------------------- ONE FRAME EDITS
        // Contam QUADROS da composicao (segurar, cair, repetir, sortear).
        // Tudo e funcao pura do quadro: o scrub cai sempre no mesmo lugar.
        case EffectType.flash:
          final tauFlash = quadrosDesdeODisparo(
            f: quadroLocal(local, fxFps),
            gatilho: effect.paramAt('trigger', local).round().clamp(0, 3),
            periodo: effect.paramAt('period', local).round().clamp(1, 120),
            probabilidade: effect.paramAt('probability', local),
            semente: effect.paramAt('seed', local).round(),
          );
          if (tauFlash != null) {
            // ESCURO PRIMEIRO: o quadro da batida apaga e o clarao vem no
            // seguinte — o "flash invertido".
            final escuroPrimeiro = effect.paramAt('dark_first', local) >= .5;
            final envelope = envelopeHoldDecay(
              escuroPrimeiro ? tauFlash - 1 : tauFlash,
              hold: effect.paramAt('hold', local).round().clamp(1, 8),
              decay: effect.paramAt('decay', local).round().clamp(0, 24),
              gama: effect.paramAt('curve', local),
            );
            if (escuroPrimeiro && tauFlash == 0) {
              out = ColorFiltered(
                colorFilter: const ColorFilter.matrix(<double>[
                  0, 0, 0, 0, 0, //
                  0, 0, 0, 0, 0,
                  0, 0, 0, 0, 0,
                  0, 0, 0, 1, 0,
                ]),
                child: out,
              );
            } else if (envelope > .002) {
              final sigmaFlash = pxAt1080(
                effect.paramAt('blur', local).clamp(0.0, 60.0) * envelope,
                fxWidth,
                fxHeight,
              );
              if (sigmaFlash > .3) {
                out = ImageFiltered(
                  imageFilter: ui.ImageFilter.blur(
                    sigmaX: sigmaFlash,
                    sigmaY: sigmaFlash,
                  ),
                  child: out,
                );
              }
              out = ColorFiltered(
                colorFilter: ColorFilter.matrix(
                  matrizDoFlash(
                    effect.paramAt('mode', local).round().clamp(0, 4),
                    (effect.paramAt('intensity', local) / 100).clamp(0.0, 1.0) *
                        envelope,
                    r: effect.color.r,
                    g: effect.color.g,
                    b: effect.color.b,
                    stops: effect.paramAt('stops', local).clamp(0.0, 6.0) *
                        envelope,
                  ),
                ),
                child: out,
              );
            }
          }

        case EffectType.strobe:
          final acesoStrobe = strobeAceso(
            f: quadroLocal(local, fxFps),
            aleatorio: effect.paramAt('mode', local) >= .5,
            periodo: effect.paramAt('period', local).round().clamp(1, 30),
            duracao: effect.paramAt('duration', local).round().clamp(1, 30),
            probabilidade: effect.paramAt('probability', local),
            semente: effect.paramAt('seed', local).round(),
          );
          if (acesoStrobe) {
            final original = (effect.paramAt('blend', local) / 100).clamp(
              0.0,
              1.0,
            );
            final forca = 1 - original;
            final operacao = effect.paramAt('operation', local).round().clamp(
              0,
              4,
            );
            final stopsStrobe = effect.paramAt('stops', local).clamp(0.0, 6.0);
            if (operacao == 0) {
              out = Opacity(opacity: original, child: out);
            } else {
              final cor = operacao == 4
                  ? const Color(0xFF000000)
                  : effect.color;
              out = ColorFiltered(
                colorFilter: ColorFilter.matrix(
                  matrizDoFlash(
                    switch (operacao) {
                      2 => 4,
                      3 => 3,
                      _ => 0,
                    },
                    forca,
                    r: cor.r,
                    g: cor.g,
                    b: cor.b,
                    stops: stopsStrobe * forca,
                  ),
                ),
                child: out,
              );
            }
          }

        case EffectType.zoomPunch:
          final quadrosPunch = quadrosDesdeODisparo(
            f: quadroLocal(local, fxFps),
            gatilho: effect.paramAt('trigger', local).round().clamp(0, 2),
            periodo: effect.paramAt('period', local).round().clamp(1, 120),
            probabilidade: effect.paramAt('probability', local),
            semente: effect.paramAt('seed', local).round(),
          );
          if (quadrosPunch != null) {
            final picoPunch = effect.paramAt('peak', local);
            final ataquePunch = effect.paramAt('attack', local).round().clamp(0, 8);
            final holdPunch = effect.paramAt('hold', local).round().clamp(0, 8);
            final solturaPunch = effect.paramAt('release', local).round().clamp(
              0,
              30,
            );
            final curvaPunch = effect.paramAt('curve', local).round().clamp(0, 2);
            double escalaEm(int tau) => escalaDoSoco(
              tau,
              pico: picoPunch,
              ataque: ataquePunch,
              hold: holdPunch,
              soltura: solturaPunch,
              curva: curvaPunch,
              fps: fxFps,
            );
            final escalaAgora = escalaEm(quadrosPunch);
            if ((escalaAgora - 1).abs() > .0005) {
              final ancora = Alignment(
                effect.paramAt('center_x', local).clamp(0.0, 1.0) * 2 - 1,
                effect.paramAt('center_y', local).clamp(0.0, 1.0) * 2 - 1,
              );
              final rastro = effect.paramAt('zoom_blur', local).clamp(0.0, 1.0);
              final escalaAntes = escalaEm(quadrosPunch - 1);
              if (rastro > .01 && (escalaAgora - escalaAntes).abs() > .002) {
                // RASTRO DE ZOOM: copias entre a escala do quadro anterior e
                // a de agora. Opacidade 1/(k+1) em ordem da a MEDIA exata
                // das copias sobre fundo opaco.
                const copias = 5;
                final inicio = escalaAgora + (escalaAntes - escalaAgora) * rastro;
                out = Stack(
                  clipBehavior: Clip.none,
                  children: [
                    for (var k = 0; k < copias; k++)
                      Opacity(
                        opacity: 1 / (k + 1),
                        child: Transform.scale(
                          scale: inicio + (escalaAgora - inicio) * k / (copias - 1),
                          alignment: ancora,
                          child: out,
                        ),
                      ),
                  ],
                );
              } else {
                out = Transform.scale(
                  scale: escalaAgora,
                  alignment: ancora,
                  child: out,
                );
              }
            }
          }

        case EffectType.twitch:
          // TWITCH: cinco operadores, cada um com pulso proprio (fluxo de
          // sorteio separado). O operador de TEMPO da referencia nao
          // existe aqui: pediria outro quadro do video.
          final quantidadeTwitch = (effect.paramAt('amount', local) / 100)
              .clamp(0.0, 2.0);
          if (quantidadeTwitch > .001) {
            final tTwitch = local.inMicroseconds / 1e6;
            final velocidadeTwitch = effect.paramAt('speed', local);
            final quietudeTwitch = effect.paramAt('stillness', local).clamp(
              0.0,
              1.0,
            );
            final minimoTwitch = (effect.paramAt('randomize_min', local) / 100)
                .clamp(0.0, 1.0);
            final duracaoTwitch =
                effect.paramAt('duration', local).clamp(1.0, 12.0) / fxFps;
            final subidaTwitch = effect.paramAt('ease_in', local).clamp(0.0, 1.0);
            final descidaTwitch = effect.paramAt('ease_out', local).clamp(
              0.0,
              1.0,
            );
            final sementeTwitch = effect.paramAt('seed', local).round();
            PulsoTwitch pulso(int operador) => pulsoTwitch(
              semente: sementeTwitch,
              fluxo: 1000 * operador,
              t: tTwitch,
              velocidade: velocidadeTwitch,
              quietude: quietudeTwitch,
              minimo: minimoTwitch,
              duracaoSeg: duracaoTwitch,
              easeIn: subidaTwitch,
              easeOut: descidaTwitch,
              fps: fxFps,
            );
            final bordasTwitch = effect.paramAt('edges', local).round().clamp(
              0,
              2,
            );

            if (effect.paramAt('enable_slide', local) >= .5) {
              final p = pulso(2);
              final v = p.v * quantidadeTwitch;
              if (v > .001) {
                final distancia =
                    v *
                    effect.paramAt('slide_amount', local).clamp(0.0, 50.0) /
                    100 *
                    fxWidth;
                final lado =
                    (p.sorteio3 +
                            effect
                                .paramAt('slide_tendency', local)
                                .clamp(-1.0, 1.0)) >=
                        0
                    ? 1.0
                    : -1.0;
                final angulo =
                    (effect.paramAt('slide_direction', local) +
                        effect.paramAt('slide_spread', local).clamp(0.0, 180.0) *
                            p.sorteio4) *
                    math.pi /
                    180;
                final d =
                    Offset(math.cos(angulo), math.sin(angulo)) * distancia * lado;
                final k = effect.paramAt('slide_rgb_split', local).clamp(0.0, 1.0);
                if (k > .01) {
                  final alcance = distancia * (1 + k) + 4;
                  out = Stack(
                    clipBehavior: Clip.none,
                    children: [
                      Transform.translate(
                        offset: d * (1 + k),
                        child: _channelIso(out, 0),
                      ),
                      BlendMask(
                        blendMode: BlendMode.plus,
                        margem: alcance,
                        child: Transform.translate(
                          offset: d,
                          child: _channelIso(out, 1),
                        ),
                      ),
                      BlendMask(
                        blendMode: BlendMode.plus,
                        margem: alcance,
                        child: Transform.translate(
                          offset: d * (1 - k),
                          child: _channelIso(out, 2),
                        ),
                      ),
                    ],
                  );
                } else {
                  out = Transform.translate(offset: d, child: out);
                }
                out = _comBordas(out, bordasTwitch);
              }
            }

            if (effect.paramAt('enable_scale', local) >= .5) {
              final v = pulso(3).v * quantidadeTwitch;
              if (v > .001) {
                out = Transform.scale(
                  scale:
                      1 +
                      v * effect.paramAt('scale_amount', local).clamp(0.0, 100.0) / 100,
                  child: out,
                );
              }
            }

            if (effect.paramAt('enable_blur', local) >= .5) {
              final v = pulso(4).v * quantidadeTwitch;
              final raio = pxAt1080(
                v * effect.paramAt('blur_amount', local).clamp(0.0, 200.0),
                fxWidth,
                fxHeight,
              );
              if (raio > .3) {
                final aspecto = effect.paramAt('blur_aspect', local).clamp(-1.0, 1.0);
                final sx = raio * (1 - math.max(0.0, aspecto));
                final sy = raio * (1 - math.max(0.0, -aspecto));
                out = ImageFiltered(
                  imageFilter: ui.ImageFilter.blur(
                    sigmaX: math.max(sx, .01),
                    sigmaY: math.max(sy, .01),
                  ),
                  child: out,
                );
              }
            }

            if (effect.paramAt('enable_light', local) >= .5) {
              final p = pulso(5);
              final v = p.v * quantidadeTwitch;
              if (v > .001) {
                final sentido = switch (effect
                    .paramAt('light_behaviour', local)
                    .round()
                    .clamp(0, 2)) {
                  0 => 1.0,
                  1 => -1.0,
                  _ => p.sorteio3 >= 0 ? 1.0 : -1.0,
                };
                final ganho = math
                    .pow(
                      2,
                      v *
                          effect.paramAt('light_amount', local).clamp(0.0, 4.0) *
                          sentido,
                    )
                    .toDouble();
                out = ColorFiltered(
                  colorFilter: ColorFilter.matrix(<double>[
                    ganho, 0, 0, 0, 0, //
                    0, ganho, 0, 0, 0,
                    0, 0, ganho, 0, 0,
                    0, 0, 0, 1, 0,
                  ]),
                  child: out,
                );
              }
            }

            if (effect.paramAt('enable_color', local) >= .5) {
              final p = pulso(6);
              final v = p.v * quantidadeTwitch;
              if (v > .001) {
                final sorteada = corDoMatiz(p.sorteio7);
                final mistura = effect.paramAt('color_randomize', local).clamp(
                  0.0,
                  1.0,
                );
                final c = effect.color;
                out = ColorFiltered(
                  colorFilter: ColorFilter.matrix(
                    matrizDeColorir(
                      v * effect.paramAt('color_amount', local).clamp(0.0, 100.0) / 100,
                      r: c.r + (sorteada.r - c.r) * mistura,
                      g: c.g + (sorteada.g - c.g) * mistura,
                      b: c.b + (sorteada.b - c.b) * mistura,
                    ),
                  ),
                  child: out,
                );
              }
            }
          }

        case EffectType.twirl:
        case EffectType.fisheye:
        case EffectType.kaleidoscope:
        case EffectType.venetianBlinds:
        case EffectType.blockDissolve:
        case EffectType.offset:
        case EffectType.invert:
        case EffectType.waveWarp:
          out = EssentialWarpPass(effect: effect, time: local, child: out);

        case EffectType.oscillate:
          out = Transform.translate(
            offset: oscillationOffset(
              effect,
              local,
              pixelScale: math.min(fxWidth, fxHeight) / 1080,
            ),
            child: out,
          );

        case EffectType.tremor:
          // SHAKE: fase INTEGRADA no tempo, componentes aleatoria e de
          // onda separadas por eixo, e canais RGB com fase propria.
          // A AMPLITUDE ERA PIXEL CRU, e a ficha ja dizia que era
          // relativa. Um tremor de "1" sacudia 60 px em qualquer
          // resolucao: em 4K isso e metade do tremor aparente de 1080p,
          // e o mesmo projeto exportado em duas resolucoes tremia
          // diferente. Em 1080p o resultado continua identico.
          final amp = pxAt1080(
            effect.paramAt('amplitude', local) * 60,
            fxWidth,
            fxHeight,
          );
          final style = effect.paramAt('style', local).round().clamp(0, 2);
          final seed = effect.paramAt('seed', local).round();
          final phase =
              integratedPhase(effect.track('frequency'), local) +
              effect.paramAt('phase', local) / 360.0;

          // ENVELOPE DE EDIT: continuo (como sempre foi), impacto no inicio
          // da camada, ou impacto a cada N quadros. O soco de zoom anda
          // junto com o mesmo relogio de quadros.
          final envelopeShake = effect.paramAt('envelope', local).round().clamp(
            0,
            2,
          );
          final quadroShake = quadroLocal(local, fxFps);
          final tauShake = envelopeShake == 2
              ? quadroShake %
                    effect.paramAt('impact_period', local).round().clamp(1, 120)
              : quadroShake;
          final forcaShake = envelopeShake == 0
              ? 1.0
              : envelopeDeImpacto(
                  tauShake,
                  ataque: effect.paramAt('attack_frames', local),
                  meiaVida: effect.paramAt('half_life', local),
                );
          final socoShake =
              effect.paramAt('zoom_punch', local).clamp(0.0, 50.0) /
              100 *
              socoDoShake(tauShake, effect.paramAt('punch_frames', local));
          final oitavasShake = effect.paramAt('octaves', local);
          final serrilhadoShake = effect.paramAt('jaggedness', local);

          ShakeAxis eixo(String pre) => ShakeAxis(
            randomAmplitude: effect.paramAt('${pre}_random_amplitude', local),
            randomFrequency: effect.paramAt('${pre}_random_frequency', local),
            waveAmplitude: effect.paramAt('${pre}_wave_amplitude', local),
            waveFrequency: effect.paramAt('${pre}_wave_frequency', local),
            phaseDeg: effect.paramAt('${pre}_phase', local),
            octaves: oitavasShake,
            jaggedness: serrilhadoShake,
          );

          final ex = eixo('x');
          final ey = eixo('y');
          final ez = eixo('z');
          final et = eixo('tilt');

          TremorSample sampleAt(double shift) => tremorSample(
            amplitudePx: amp * forcaShake,
            phase: phase + shift,
            style: style,
            seed: seed,
            x: ex,
            y: ey,
            z: ez,
            tilt: et,
            stillness: effect.paramAt('stillness', local),
            twitchFrequency: effect.paramAt('twitch_frequency', local),
            drift: effect.paramAt('drift', local),
            centerBias: effect.paramAt('center_bias', local),
            zDistance: effect.paramAt('z_distance', local),
          );

          // BORDAS: refletir e a escolha certa por padrao — a imagem
          // sacode e a borda continua parecendo imagem, em vez de virar
          // faixa preta.
          final bordas = effect.paramAt('edges', local).round().clamp(0, 2);
          // As copias ficam FORA da caixa, coladas a cada lado: e o que
          // preenche o vao que o tremor abre na borda. Espelhar sobre o
          // proprio centro (como era) punha a copia EM CIMA da original —
          // texto saia com um gemeo invertido por cima.
          Widget comBorda(Widget c) => switch (bordas) {
            0 => Stack(
              clipBehavior: Clip.none,
              children: [
                Transform.scale(
                  scaleX: -1,
                  alignment: Alignment.centerLeft,
                  child: c,
                ),
                Transform.scale(
                  scaleX: -1,
                  alignment: Alignment.centerRight,
                  child: c,
                ),
                Transform.scale(
                  scaleY: -1,
                  alignment: Alignment.topCenter,
                  child: c,
                ),
                Transform.scale(
                  scaleY: -1,
                  alignment: Alignment.bottomCenter,
                  child: c,
                ),
                c,
              ],
            ),
            1 => Stack(
              clipBehavior: Clip.none,
              children: [
                Transform.translate(
                  offset: Offset(-fxWidth.toDouble(), 0),
                  child: c,
                ),
                Transform.translate(
                  offset: Offset(fxWidth.toDouble(), 0),
                  child: c,
                ),
                Transform.translate(
                  offset: Offset(0, -fxHeight.toDouble()),
                  child: c,
                ),
                Transform.translate(
                  offset: Offset(0, fxHeight.toDouble()),
                  child: c,
                ),
                c,
              ],
            ),
            _ => c,
          };

          Widget shaken(TremorSample s, Widget c) => Transform(
            transform: Matrix4.identity()
              ..translateByDouble(s.dx, s.dy, 0, 1)
              ..rotateZ(s.rotationDeg * math.pi / 180)
              ..scaleByDouble(
                s.scale * (1 + socoShake),
                s.scale * (1 + socoShake),
                1,
                1,
              ),
            alignment: Alignment.center,
            child: c,
          );

          final s0 = sampleAt(0);
          final rgbAleatorio = effect
              .paramAt('rgb_randomness', local)
              .clamp(0.0, 1.0);
          final rgbFreq = effect.paramAt('rgb_frequency', local);
          final ampR = effect.paramAt('red_amplitude', local);
          final ampG = effect.paramAt('green_amplitude', local);
          final ampB = effect.paramAt('blue_amplitude', local);
          final separaCanais =
              rgbAleatorio > 0.001 ||
              (ampR - ampG).abs() > 0.001 ||
              (ampG - ampB).abs() > 0.001 ||
              effect.paramAt('red_phase', local).abs() > 0.5 ||
              effect.paramAt('green_phase', local).abs() > 0.5 ||
              effect.paramAt('blue_phase', local).abs() > 0.5;

          if (!s0.isNeutral || separaCanais || socoShake > .0005) {
            if (separaCanais) {
              // FASE POR CANAL: desloca o canal NO TEMPO. O vermelho se
              // move antes, os outros seguem — franja organica, que um
              // deslocamento estatico nao consegue imitar.
              double faseDe(String p, double amplitude) =>
                  effect.paramAt(p, local) / 360.0 +
                  (rgbAleatorio <= 0
                      ? 0.0
                      : fxNoiseSigned(seed + 31, 9, phase * rgbFreq) *
                            rgbAleatorio *
                            0.15);

              TremorSample canal(String p, double a) {
                final base = sampleAt(faseDe(p, a));
                return TremorSample(
                  base.dx * a,
                  base.dy * a,
                  base.scale,
                  base.rotationDeg,
                );
              }

              // A margem da foto cobre ate onde o tremor pode levar o
              // canal: um quinto do quadro e mais do que qualquer tremor.
              final alcanceTremor = fxSize.longestSide * 0.2;
              out = comBorda(
                Stack(
                  clipBehavior: Clip.none,
                  children: [
                    shaken(canal('red_phase', ampR), _channelIso(out, 0)),
                    BlendMask(
                      blendMode: BlendMode.plus,
                      margem: alcanceTremor,
                      child: shaken(
                        canal('green_phase', ampG),
                        _channelIso(out, 1),
                      ),
                    ),
                    BlendMask(
                      blendMode: BlendMode.plus,
                      margem: alcanceTremor,
                      child: shaken(
                        canal('blue_phase', ampB),
                        _channelIso(out, 2),
                      ),
                    ),
                  ],
                ),
              );
            } else {
              out = comBorda(shaken(s0, out));
            }

            // MOTION BLUR do proprio Shake: amostras ao longo do rastro
            // do tremor. Sem ele, um tremor forte vira imagem picotada.
            if (effect.paramAt('motion_blur', local) >= 0.5) {
              final comprimento = effect
                  .paramAt('blur_length', local)
                  .clamp(0.0, 10.0);
              if (comprimento > 0.01) {
                const n = 5;
                final copias = <Widget>[];
                for (var k = 0; k < n; k++) {
                  final f = (k / (n - 1) - 0.5) * comprimento * 0.02;
                  copias.add(
                    Opacity(
                      opacity: 1 / (k + 1),
                      child: shaken(sampleAt(f), out),
                    ),
                  );
                }
                out = Stack(clipBehavior: Clip.none, children: copias);
              }
            }
          }

        case EffectType.glitch:
          // Modulador mestre + operadores com tiques puros (PR-FX4).
          final master = effect.paramAt('quantidade', local).clamp(0.0, 2.0);
          if (master > 0.001) {
            final tau = integratedPhase(effect.track('velocidade'), local);
            final st = glitchState(
              master: master,
              tau: tau,
              intervalSec: effect.paramAt('intervalo', local),
              seed: effect.paramAt('semente', local).round(),
              slide: effect.paramAt('deslize', local),
              scaleAmt: effect.paramAt('escala', local),
              colorAmt: effect.paramAt('cor', local),
              lightAmt: effect.paramAt('luz', local),
              blurAmt: effect.paramAt('desfoque', local),
              rgbAmt: effect.paramAt('rgb', local),
            );
            if (!st.isNeutral) {
              var g = out;
              if (st.hueDeg.abs() > 0.5) {
                g = ColorFiltered(
                  colorFilter: ColorFilter.matrix(hueRotateMatrix(st.hueDeg)),
                  child: g,
                );
              }
              if (st.brightness > 0.01) {
                final b = 1 + st.brightness;
                g = ColorFiltered(
                  colorFilter: ColorFilter.matrix(<double>[
                    b, 0, 0, 0, 0, //
                    0, b, 0, 0, 0, //
                    0, 0, b, 0, 0, //
                    0, 0, 0, 1, 0,
                  ]),
                  child: g,
                );
              }
              if (st.blurSigma > 0.2) {
                g = ImageFiltered(
                  imageFilter: ui.ImageFilter.blur(
                    sigmaX: st.blurSigma,
                    sigmaY: st.blurSigma * 0.4,
                    tileMode: TileMode.decal,
                  ),
                  child: g,
                );
              }
              Widget moved(double extraDx, Widget c) => Transform(
                transform: Matrix4.identity()
                  ..translateByDouble(st.dx + extraDx, st.dy, 0, 1)
                  ..scaleByDouble(st.scale, st.scale, 1, 1),
                alignment: Alignment.center,
                child: c,
              );
              if (st.rgbSep > 0.2) {
                out = Stack(
                  clipBehavior: Clip.none,
                  children: [
                    moved(-st.rgbSep, _channelIso(g, 0)),
                    BlendMask(
                      blendMode: BlendMode.plus,
                      margem: st.rgbSep.abs() + 4,
                      child: moved(0, _channelIso(g, 1)),
                    ),
                    BlendMask(
                      blendMode: BlendMode.plus,
                      margem: st.rgbSep.abs() + 4,
                      child: moved(st.rgbSep, _channelIso(g, 2)),
                    ),
                  ],
                );
              } else {
                out = moved(0, g);
              }
            }
          }

        case EffectType.rgbSplit:
          // Mesma historia do tremor: deslocamento em pixel cru, com a
          // ficha dizendo relativo. Em 1080p nada muda.
          final d = pxAt1080(
            effect.paramAt('deslocamento', local).clamp(0.0, 100.0),
            fxWidth,
            fxHeight,
          );
          if (d > 0.2) {
            final ang = effect.paramAt('angulo', local) * math.pi / 180;
            final off = Offset(math.cos(ang) * d, math.sin(ang) * d);
            // QUAIS CANAIS se afastam (avancado): o par decide a cor das
            // franjas. O terceiro fica parado, no lugar da imagem.
            final (antes, meio, depois) = switch (effect
                .paramAt('canais', local)
                .round()
                .clamp(0, 2)) {
              1 => (0, 2, 1),
              2 => (1, 0, 2),
              _ => (0, 1, 2),
            };
            final suave = effect.paramAt('suavizar', local).clamp(0.0, 1.0);
            final sigma = suave * d * 0.35;
            Widget canal(int i, Offset deslocamento) {
              Widget w = _channelIso(out, i);
              if (sigma > 0.05) {
                w = LinearLight.blurred(
                  sigmaX: sigma,
                  sigmaY: sigma,
                  size: fxSize,
                  child: w,
                );
              }
              return deslocamento == Offset.zero
                  ? w
                  : Transform.translate(offset: deslocamento, child: w);
            }

            final margem = off.distance + 3 * sigma + 4;
            out = Stack(
              clipBehavior: Clip.none,
              children: [
                canal(antes, -off),
                BlendMask(
                  blendMode: BlendMode.plus,
                  margem: margem,
                  child: canal(meio, Offset.zero),
                ),
                BlendMask(
                  blendMode: BlendMode.plus,
                  margem: margem,
                  child: canal(depois, off),
                ),
              ],
            );
          }

        case EffectType.echo:
          // Tratado no nivel da camada (_buildLayers): aqui e neutro.
          break;

        case EffectType.spatialEcho:
          // Repeticao no ESPACO com transformacao progressiva (item 28).
          final n = effect.paramAt('copias', local).round().clamp(1, 12);
          if (n > 1) {
            final dx = effect.paramAt('dx', local);
            final dy = effect.paramAt('dy', local);
            final scaleStep = effect.paramAt('escala', local) / 100.0;
            final rotStep = effect.paramAt('rotacao', local) * math.pi / 180;
            final decay = effect.paramAt('decaimento', local).clamp(0.05, 1.0);
            final hueStep = effect.paramAt('matiz', local);
            final copies = <Widget>[];
            for (var c = n - 1; c >= 0; c--) {
              Widget w = out;
              if (hueStep > 0.5 && c > 0) {
                w = ColorFiltered(
                  colorFilter: ColorFilter.matrix(hueRotateMatrix(hueStep * c)),
                  child: w,
                );
              }
              copies.add(
                Opacity(
                  opacity: math.pow(decay, c).toDouble().clamp(0.0, 1.0),
                  child: Transform(
                    transform: Matrix4.identity()
                      ..translateByDouble(dx * c, dy * c, 0, 1)
                      ..rotateZ(rotStep * c)
                      ..scaleByDouble(
                        math.pow(scaleStep, c).toDouble(),
                        math.pow(scaleStep, c).toDouble(),
                        1,
                        1,
                      ),
                    alignment: Alignment.center,
                    child: w,
                  ),
                ),
              );
            }
            out = Stack(clipBehavior: Clip.none, children: copies);
          }

        case EffectType.radialAberration:
          // Cresce do centro para a borda, como lente real (item 14):
          // cada canal amostrado com uma ESCALA levemente diferente.
          final amt = effect.paramAt('quantidade', local).clamp(0.0, 1.0);
          if (amt > 0.01) {
            final spread = amt * 0.06;
            Widget scaled(double s, int ch) => Transform.scale(
              scale: s,
              alignment: Alignment.center,
              child: _channelIso(out, ch),
            );
            out = Stack(
              clipBehavior: Clip.none,
              children: [
                scaled(1 - spread, 0),
                BlendMask(
                  blendMode: BlendMode.plus,
                  margem: fxSize.longestSide * spread + 4,
                  child: scaled(1.0, 1),
                ),
                BlendMask(
                  blendMode: BlendMode.plus,
                  margem: fxSize.longestSide * spread + 4,
                  child: scaled(1 + spread, 2),
                ),
              ],
            );
          }

        // ------------------- catalogo, lote 1 -------------------

        case EffectType.levels:
          // Entrada -> gama -> saida, por canal, em matriz.
          final inMin = effect.paramAt('entradaMin', local);
          final inMax = effect.paramAt('entradaMax', local);
          final gamma = effect.paramAt('gama', local);
          final outMin = effect.paramAt('saidaMin', local);
          final outMax = effect.paramAt('saidaMax', local);
          final span = (inMax - inMin).abs() < 1e-4 ? 1e-4 : inMax - inMin;
          final scale = (outMax - outMin) / span;
          final shift = outMin - inMin * scale;
          // CANAL (avancado): a mesma curva num canal so.
          final canal = effect.paramAt('canal', local).round().clamp(0, 3);
          if ((scale - 1).abs() > 1e-4 || shift.abs() > 1e-4) {
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(
                _scaleShiftMatrix(scale, shift, canal: canal),
              ),
              child: out,
            );
          }
          // Gama por aproximacao: uma segunda passada de ganho.
          if ((gamma - 1).abs() > 0.01) {
            final g = 1 / gamma;
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(
                _scaleShiftMatrix(g, (1 - g) * 0.18, canal: canal),
              ),
              child: out,
            );
          }

        case EffectType.corrections:
          // CORRECOES (o "basico" do Lumetri): exposicao e contraste numa
          // matriz, sombras/altas como pe e topo da curva, temperatura e
          // verde/magenta como ganho por canal, saturacao e gama.
          final ev = effect.paramAt('exposicao', local);
          final ctr = effect.paramAt('contraste', local).clamp(-1.0, 1.0);
          final altas = effect.paramAt('altas', local).clamp(-1.0, 1.0);
          final sombras = effect.paramAt('sombras', local).clamp(-1.0, 1.0);
          final temp = effect.paramAt('temperatura', local).clamp(-1.0, 1.0);
          final verdeMag = effect.paramAt('matiz', local).clamp(-1.0, 1.0);
          final satC = effect.paramAt('saturacao', local).clamp(-1.0, 1.0);
          final gamaC = effect.paramAt('gama', local).clamp(0.3, 3.0);
          final ganho = math.pow(2.0, ev).toDouble();
          final cC = 1 + ctr * 0.9;
          final escala = ganho * cC;
          final desloc = (1 - cC) * 0.5 * ganho;
          if ((escala - 1).abs() > 1e-4 || desloc.abs() > 1e-4) {
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(
                _scaleShiftMatrix(escala, desloc),
              ),
              child: out,
            );
          }
          // Sombras levantam o preto (branco fica); altas esticam ou
          // comprimem o topo (preto fica).
          final pe = sombras * 0.22;
          final topo = altas * 0.22;
          if (pe.abs() > 1e-4 || topo.abs() > 1e-4) {
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(
                _scaleShiftMatrix((1 - pe) * (1 + topo), pe),
              ),
              child: out,
            );
          }
          if (temp.abs() > 1e-4 || verdeMag.abs() > 1e-4) {
            final rG = (1 + temp * 0.18) * (1 + verdeMag * 0.05);
            final gG = 1 - verdeMag * 0.14;
            final bG = (1 - temp * 0.18) * (1 + verdeMag * 0.05);
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(<double>[
                rG, 0, 0, 0, 0, //
                0, gG, 0, 0, 0,
                0, 0, bG, 0, 0,
                0, 0, 0, 1, 0,
              ]),
              child: out,
            );
          }
          if (satC.abs() > 1e-4) {
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(_saturationMatrix(1 + satC)),
              child: out,
            );
          }
          if ((gamaC - 1).abs() > 0.01) {
            final g = 1 / gamaC;
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(
                _scaleShiftMatrix(g, (1 - g) * 0.18),
              ),
              child: out,
            );
          }

        case EffectType.curves:
          final contrast = effect.paramAt('contraste', local);
          final bright = effect.paramAt('brilho', local);
          final lift = effect.paramAt('sombras', local);
          final pull = effect.paramAt('altas', local);
          final c = 1 + contrast;
          final b = bright * 0.5 + lift * 0.25 - pull * 0.25;
          if ((c - 1).abs() > 1e-4 || b.abs() > 1e-4) {
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(
                _scaleShiftMatrix(c, b + (1 - c) * 0.5),
              ),
              child: out,
            );
          }

        case EffectType.vibrance:
          final vib = effect.paramAt('vibracao', local);
          final sat = effect.paramAt('saturacao', local);
          final skin = effect.paramAt('protecaoPele', local).clamp(0.0, 1.0);
          // Vibracao sobe mais o que esta POUCO saturado; a protecao de
          // pele segura o ganho no canal vermelho, que e onde o tom de
          // pele vive — sem isso o rosto fica laranja.
          final amount = sat + vib * 0.6 * (1 - skin * 0.7);
          if (amount.abs() > 1e-4) {
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(
                _saturationMatrix(1 + amount, redGuard: skin * vib),
              ),
              child: out,
            );
          }

        case EffectType.whiteBalance:
          final temp = effect.paramAt('temperatura', local);
          final tintV = effect.paramAt('matiz', local);
          if (temp.abs() > 1e-4 || tintV.abs() > 1e-4) {
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(<double>[
                1 + temp * 0.3,
                0,
                0,
                0,
                0,
                0,
                1 + tintV * 0.2,
                0,
                0,
                0,
                0,
                0,
                1 - temp * 0.3,
                0,
                0,
                0,
                0,
                0,
                1,
                0,
              ]),
              child: out,
            );
          }

        case EffectType.colorWheels:
          // Sombras = deslocamento (lift); altas = ganho (gain).
          final sr = effect.paramAt('sombrasR', local);
          final sg = effect.paramAt('sombrasG', local);
          final sb = effect.paramAt('sombrasB', local);
          final hr = effect.paramAt('altasR', local);
          final hg = effect.paramAt('altasG', local);
          final hb = effect.paramAt('altasB', local);
          if ([sr, sg, sb, hr, hg, hb].any((v) => v.abs() > 1e-4)) {
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(<double>[
                1 + hr,
                0,
                0,
                0,
                sr * 255,
                0,
                1 + hg,
                0,
                0,
                sg * 255,
                0,
                0,
                1 + hb,
                0,
                sb * 255,
                0,
                0,
                0,
                1,
                0,
              ]),
              child: out,
            );
          }

        case EffectType.unmult:
          // O preto vira TRANSPARENTE: a luminancia entra no alfa. E o
          // que faz overlay de fogo/fumaca/faisca funcionar direto.
          final soft = effect.paramAt('suavidade', local).clamp(0.0, 1.0);
          final k = 0.7 + soft * 0.6;
          // O LIMIAR EXISTIA NA TELA E NAO EXISTIA NA CONTA.
          //
          // O controle estava exposto, com nome e faixa, e o codigo nunca
          // o lia: mexer nele nao mudava um pixel. Agora ele entra como
          // deslocamento constante da linha de alfa — que e como um
          // limiar se escreve numa matriz de cor, onde nao cabe
          // comparacao. Cinza abaixo do limiar vai a zero; acima, sobra
          // o que passou dele.
          final limiar = effect.paramAt('limiar', local).clamp(0.0, 1.0);
          out = ColorFiltered(
            colorFilter: ColorFilter.matrix(<double>[
              1,
              0,
              0,
              0,
              0,
              0,
              1,
              0,
              0,
              0,
              0,
              0,
              1,
              0,
              0,
              0.2126 * k,
              0.7152 * k,
              0.0722 * k,
              0,
              -limiar * 255,
            ]),
            child: out,
          );

        case EffectType.vignette:
          final amt = effect.paramAt('quantidade', local).clamp(0.0, 1.0);
          if (amt > 0.01) {
            out = Stack(
              clipBehavior: Clip.none,
              children: [
                out,
                Positioned.fill(
                  child: IgnorePointer(
                    child: CustomPaint(
                      painter: VignettePainter(
                        amount: amt,
                        radius: effect.paramAt('raio', local),
                        softness: effect
                            .paramAt('suavidade', local)
                            .clamp(0.0, 1.0),
                        color: effect.color,
                        retangular: effect.paramAt('forma', local) > 0.5,
                        center: Offset(
                          effect.paramAt('centroX', local).clamp(-1.0, 2.0),
                          effect.paramAt('centroY', local).clamp(-1.0, 2.0),
                        ),
                      ),
                    ),
                  ),
                ),
              ],
            );
          }

        case EffectType.directionalBlur:
          final len = effect.paramAt('comprimento', local);
          if (len > 0.5) {
            final ang = effect.paramAt('angulo', local) * math.pi / 180;
            // Blur anisotropico girado: sigma no eixo do movimento.
            out = Transform.rotate(
              angle: -ang,
              child: LinearLight.blurred(
                // Tambem em linear: e desfoque, e desfoque em sRGB
                // escurece a media entre claro e escuro — a franja
                // suja na borda do movimento vem daí.
                sigmaX: len / 3,
                sigmaY: 0.01,
                size: fxSize,
                child: Transform.rotate(angle: ang, child: out),
              ),
            );
          }

        case EffectType.radialBlur:
          final amt = effect.paramAt('quantidade', local).clamp(0.0, 1.0);
          if (amt > 0.01) {
            final zoom = effect.paramAt('modo', local) < 0.5;
            final n = effect.paramAt('amostras', local).round().clamp(2, 16);
            final layers = <Widget>[];
            for (var i = 0; i < n; i++) {
              final f = i / (n - 1);
              final o = 1.0 / n;
              layers.add(
                Opacity(
                  opacity: o * 1.6,
                  child: zoom
                      ? Transform.scale(scale: 1 + amt * 0.25 * f, child: out)
                      : Transform.rotate(angle: amt * 0.4 * f, child: out),
                ),
              );
            }
            out = Stack(clipBehavior: Clip.none, children: layers);
          }

        case EffectType.lightRays:
          final len = effect.paramAt('comprimento', local);
          if (len > 0.01) {
            final n = effect.paramAt('amostras', local).round().clamp(2, 20);
            final gain = effect.paramAt('intensidade', local);
            final cx = effect.paramAt('centroX', local);
            final cy = effect.paramAt('centroY', local);
            final origin = Alignment(cx * 2 - 1, cy * 2 - 1);
            final rays = <Widget>[];
            for (var i = 1; i <= n; i++) {
              final s = 1 + len * 0.6 * i / n;
              rays.add(
                Opacity(
                  opacity: (gain / n).clamp(0.0, 1.0),
                  child: Transform.scale(
                    scale: s,
                    alignment: origin,
                    child: ColorFiltered(
                      colorFilter: ColorFilter.mode(
                        effect.color,
                        BlendMode.srcATop,
                      ),
                      child: out,
                    ),
                  ),
                ),
              );
            }
            out = Stack(
              clipBehavior: Clip.none,
              children: [
                BlendMask(
                  blendMode: BlendMode.plus,
                  child: Stack(clipBehavior: Clip.none, children: rays),
                ),
                out,
              ],
            );
          }

        case EffectType.mosaic:
          final blocks = effect.paramAt('blocos', local).clamp(3.0, 160.0);
          // Reduz e amplia SEM interpolacao: e o pixelate de verdade.
          out = ImageFiltered(
            imageFilter: ui.ImageFilter.compose(
              outer: ui.ImageFilter.matrix(
                Matrix4.diagonal3Values(blocks / 3, blocks / 3, 1).storage,
                filterQuality: FilterQuality.none,
              ),
              inner: ui.ImageFilter.matrix(
                Matrix4.diagonal3Values(3 / blocks, 3 / blocks, 1).storage,
                filterQuality: FilterQuality.none,
              ),
            ),
            child: out,
          );

        case EffectType.posterize:
          final levels = effect.paramAt('niveis', local).round().clamp(2, 32);
          // Aproximacao por quantizacao de contraste em degraus.
          out = ColorFiltered(
            colorFilter: ColorFilter.matrix(
              _posterizeMatrix(levels.toDouble()),
            ),
            child: out,
          );

        case EffectType.filmGrain:
          final amt = effect.paramAt('intensidade', local).clamp(0.0, 1.0);
          if (amt > 0.01) {
            out = Stack(
              clipBehavior: Clip.none,
              children: [
                out,
                Positioned.fill(
                  child: IgnorePointer(
                    child: BlendMask(
                      blendMode: BlendMode.overlay,
                      child: CustomPaint(
                        painter: _GrainPainter(
                          amount: amt,
                          size: effect.paramAt('tamanho', local),
                          seed: effect.paramAt('semente', local).round(),
                          time: local,
                        ),
                      ),
                    ),
                  ),
                ),
              ],
            );
          }

        case EffectType.fractalNoise:
          final op = effect.paramAt('opacidade', local).clamp(0.0, 1.0);
          if (op > 0.01) {
            out = Stack(
              clipBehavior: Clip.none,
              children: [
                out,
                Positioned.fill(
                  child: IgnorePointer(
                    child: Opacity(
                      opacity: op,
                      child: BlendMask(
                        blendMode: BlendMode.screen,
                        child: CustomPaint(
                          painter: _FractalNoisePainter(
                            scale: effect.paramAt('escala', local),
                            octaves: effect
                                .paramAt('complexidade', local)
                                .round(),
                            contrast: effect.paramAt('contraste', local),
                            evolution: effect.paramAt('evolucao', local),
                            seed: effect.paramAt('semente', local).round(),
                            color: effect.color,
                            time: local,
                          ),
                        ),
                      ),
                    ),
                  ),
                ),
              ],
            );
          }

        case EffectType.digitalDamage:
          final n = effect.paramAt('blocos', local).round().clamp(1, 24);
          final interval = effect.paramAt('intervalo', local);
          final seed = effect.paramAt('semente', local).round();
          final tick = interval <= 0
              ? 0
              : (local.inMicroseconds / 1e6 / interval).floor();
          final shift = effect.paramAt('deslocamento', local);
          final colorAmt = effect.paramAt('cor', local);
          final h = effect.paramAt('altura', local);
          final slices = <Widget>[];
          for (var i = 0; i < n; i++) {
            final r = fxHash01(seed, tick, i * 7 + 3);
            final r2 = fxHash01(seed, tick, i * 7 + 11);
            if (r > 0.55) continue;
            final top = r2.clamp(0.0, 1 - h);
            slices.add(
              Positioned.fill(
                child: ClipRect(
                  clipper: _BandClipper(top, h),
                  child: Transform.translate(
                    offset: Offset((r - 0.275) * 4 * shift * 200, 0),
                    child: colorAmt > 0.05 ? _channelIso(out, (i % 3)) : out,
                  ),
                ),
              ),
            );
          }
          if (slices.isNotEmpty) {
            out = Stack(clipBehavior: Clip.none, children: [out, ...slices]);
          }

        case EffectType.zoomWarp:
          final amt = effect.paramAt('quantidade', local);
          if (amt.abs() > 0.005) {
            final trail = effect.paramAt('rastro', local).clamp(0.0, 1.0);
            final n = effect.paramAt('amostras', local).round().clamp(2, 12);
            if (trail < 0.02) {
              out = Transform.scale(scale: 1 + amt, child: out);
            } else {
              final layers = <Widget>[];
              for (var i = 0; i < n; i++) {
                final f = i / (n - 1);
                layers.add(
                  Opacity(
                    opacity: 1.0 / n * 1.8,
                    child: Transform.scale(
                      scale: 1 + amt * (1 - trail * f),
                      child: out,
                    ),
                  ),
                );
              }
              out = Stack(clipBehavior: Clip.none, children: layers);
            }
          }

        // O remapeamento de tempo nao pinta nada: ele ja mudou QUAL
        // instante da camada foi montado, la em cima.
        case EffectType.opticalFlow:
        case EffectType.timeRemap:
        // TIME SLICE e POSTERIZE TIME remontam a camada inteira em outros
        // instantes: quem os aplica e o compositor (_buildLayers).
        case EffectType.timeSlice:
        case EffectType.posterizeTime:
        // FORCE MOTION BLUR nao acontece aqui: ele precisa re-renderizar
        // a camada em outros instantes, e a pilha de efeitos so recebe o
        // widget ja pronto. Quem o aplica e o compositor.
        case EffectType.forceMotionBlur:
          break;

        case EffectType.turbulentDisplace:
          final amt = effect.paramAt('quantidade', local);
          if (amt.abs() > 0.5) {
            out = FxSnapshot(
              painter: TurbulentDisplacePainter(
                amount: amt,
                scale: effect.paramAt('tamanho', local),
                complexity: effect.paramAt('complexidade', local),
                evolution: effect.paramAt('evolucao', local),
                seed: effect.paramAt('semente', local).round(),
              ),
              child: out,
            );
          }

        case EffectType.bend:
          final amt = effect.paramAt('quantidade', local);
          if (amt.abs() > 0.5) {
            out = FxSnapshot(
              painter: BendPainter(
                amount: amt,
                vertical: effect.paramAt('eixo', local).round() == 1,
                curvature: effect.paramAt('curvatura', local),
                anchor: effect.paramAt('ancora', local).clamp(0.0, 1.0),
              ),
              child: out,
            );
          }

        case EffectType.pixelSort:
          // BLEND WITH ORIGINAL em 1 e o "desligado" da ficha: a saida
          // tem de ser identica a entrada.
          final mistura = effect.paramAt('blend_with_original', local);
          final compr = effect.paramAt('radius_length', local);
          if (compr > 0.001 && mistura < 0.999) {
            out = FxSnapshot(
              painter: PixelSortPainter(
                // A instancia do efeito e a chave do cache do resultado.
                cacheKey: effect.id,
                mode: effect.paramAt('mode', local).round().clamp(0, 2),
                sortAngle: effect.paramAt('sort_angle', local),
                threshold: effect.paramAt('threshold', local),
                aboveThreshold: effect.paramAt('direction', local) >= 0.5,
                reverse: effect.paramAt('reverse_sort', local) >= 0.5,
                sortBy: effect.paramAt('sort_by', local).round().clamp(0, 2),
                length: compr,
                randomRestart: effect.paramAt('random_restart', local),
                seed: effect.paramAt('seed', local).round(),
                sortResolution: effect.paramAt('sort_resolution', local),
                downsample: effect.paramAt('downsample', local),
                matteBlur: effect.paramAt('blur_threshold_matte', local),
                blendWithOriginal: mistura,
                show: effect.paramAt('show', local).round().clamp(0, 3),
                softEdges: effect.paramAt('soft_edges', local) >= 0.5,
                centerX: effect.paramAt('center_x', local),
                centerY: effect.paramAt('center_y', local),
                startAngle: effect.paramAt('start_angle', local),
                degreesSorted: effect.paramAt('degrees_sorted', local),
                innerRadius: effect.paramAt('inner_radius', local),
                radiusVariation: effect.paramAt('radius_variation', local),
                startVariation: effect.paramAt('start_variation', local),
                thickness: effect.paramAt('thickness', local),
              ),
              child: out,
            );
          }

        case EffectType.ccScatterize:
          final sp = effect.paramAt('dispersao', local);
          if (sp > 0.5) {
            out = FxSnapshot(
              painter: ScatterizePainter(
                spread: sp,
                grain: effect.paramAt('grao', local),
                rotation: effect.paramAt('rotacao', local),
                transfer: effect
                    .paramAt('transferencia', local)
                    .clamp(0.0, 1.0),
                gravity: effect.paramAt('gravidade', local),
                seed: effect.paramAt('semente', local).round(),
              ),
              child: out,
            );
          }

        case EffectType.motionTile:
          final ladoW = effect.paramAt('tile_width', local);
          final ladoH = effect.paramAt('tile_height', local);
          final saidaW = effect.paramAt('output_width', local);
          final saidaH = effect.paramAt('output_height', local);
          final fase = effect.paramAt('phase', local);
          final espelha = effect.paramAt('mirror_edges', local) >= 0.5;
          // IDENTIDADE do After Effects: ladrilho de 100% num quadro de
          // 100%, sem fase e sem espelho, e a propria camada. Passar por
          // aqui assim mesmo custava uma FOTO da camada inteira por
          // quadro — e foto e justamente o que congelava a camada.
          final identidade =
              (ladoW - 100).abs() < 0.01 &&
              (ladoH - 100).abs() < 0.01 &&
              (saidaW - 100).abs() < 0.01 &&
              (saidaH - 100).abs() < 0.01 &&
              (effect.paramAt('tile_center', local) - 0.5).abs() < 0.001 &&
              (effect.paramAt('tile_center_y', local) - 0.5).abs() < 0.001 &&
              fase.abs() < 0.01 &&
              !espelha;
          if (!identidade) {
            out = MotionTilePass(effect: effect, time: local, child: out);
          }

        case EffectType.ccSplit:
          final sp = effect.paramAt('divisao', local);
          if (sp.abs() > 0.5) {
            out = FxSnapshot(
              painter: SplitPainter(
                split: sp,
                angleDeg: effect.paramAt('angulo', local),
                center: effect.paramAt('centro', local).clamp(0.0, 1.0),
                softness: effect.paramAt('suavidade', local).clamp(0.0, 1.0),
              ),
              child: out,
            );
          }

        case EffectType.unsharpMask:
          final amt = effect.paramAt('quantidade', local);
          if (amt > 0.01) {
            out = FxSnapshot(
              painter: UnsharpMaskPainter(
                amount: amt,
                radius: effect.paramAt('raio', local).clamp(0.5, 40.0),
                threshold: effect.paramAt('limiar', local).clamp(0.0, 0.95),
              ),
              child: out,
            );
          }

        case EffectType.glitchify:
          final amt = effect.paramAt('intensidade', local);
          if (amt > 0.01) {
            out = FxSnapshot(
              painter: GlitchifyPainter(
                intensity: amt,
                blocks: effect.paramAt('blocos', local),
                shift: effect.paramAt('deslocamento', local),
                colorSplit: effect.paramAt('cor', local),
                lineNoise: effect.paramAt('ruidoLinha', local),
                speed: effect.paramAt('velocidade', local),
                time: local,
                seed: effect.paramAt('semente', local).round(),
              ),
              child: out,
            );
          }

        case EffectType.vhs:
          final amt = effect.paramAt('intensidade', local).clamp(0.0, 1.0);
          if (amt > 0.01) {
            final bleed = effect.paramAt('sangramento', local);
            final jitter = effect.paramAt('tremor', local);
            // Tremor horizontal por linha: e o que denuncia a fita.
            final shake = jitter <= 0.01
                ? 0.0
                : (fxNoise(
                            (local.inMilliseconds / 40).floorToDouble(),
                            0,
                            effect.paramAt('semente', local).round(),
                          ) -
                          0.5) *
                      2 *
                      jitter *
                      14;
            var body = out;
            if (bleed > 0.02) {
              body = Stack(
                clipBehavior: Clip.none,
                children: [
                  Transform.translate(
                    offset: Offset(-bleed * 6, 0),
                    child: _channelIso(body, 0),
                  ),
                  Transform.translate(
                    offset: Offset(bleed * 6, 0),
                    child: _channelIso(body, 2),
                  ),
                  body,
                ],
              );
            }
            final fade = effect.paramAt('desbotar', local).clamp(0.0, 1.0);
            if (fade > 0.02) {
              body = ColorFiltered(
                colorFilter: ColorFilter.matrix(
                  _saturationMatrix(1 - fade * 0.55),
                ),
                child: body,
              );
            }
            out = Stack(
              clipBehavior: Clip.none,
              children: [
                Transform.translate(offset: Offset(shake, 0), child: body),
                Positioned.fill(
                  child: IgnorePointer(
                    child: CustomPaint(
                      painter: VhsPainter(
                        intensity: amt,
                        lines: effect.paramAt('linhas', local),
                        noise: effect.paramAt('ruido', local),
                        time: local,
                        seed: effect.paramAt('semente', local).round(),
                      ),
                    ),
                  ),
                ),
              ],
            );
          }

        case EffectType.filmDamage:
          final flick = effect.paramAt('cintilacao', local).clamp(0.0, 1.0);
          final jump = effect.paramAt('salto', local).clamp(0.0, 1.0);
          final seed = effect.paramAt('semente', local).round();
          final frame = (local.inMilliseconds / 1000.0 * 16).floor();
          // Cintilacao e salto de quadro andam no relogio do projetor.
          final lum = flick <= 0.01
              ? 1.0
              : 1 + (fxNoise(frame.toDouble(), 0, seed) - 0.5) * flick * 0.4;
          final dy = jump <= 0.01
              ? 0.0
              : (fxNoise(frame.toDouble(), 1, seed + 5) - 0.5) * jump * 10;
          // S_FILMDAMAGE 2: todos nascem neutros, e neutro nao embrulha
          // nada — projeto antigo desenha a mesma arvore de antes.
          final balancoFilme = effect.paramAt('balanco', local).clamp(0.0, 1.0);
          final dx = balancoFilme <= 0.001
              ? 0.0
              : (fxValueNoise(local.inMicroseconds / 1e6 * 3, 11, seed) - 0.5) *
                    balancoFilme *
                    12;
          final desfoqueFilme = effect
              .paramAt('desfoque', local)
              .clamp(0.0, 1.0);
          final saturacaoFilme = effect
              .paramAt('saturacao', local)
              .clamp(0.0, 2.0);
          final sepiaFilme = effect.paramAt('sepia', local).clamp(0.0, 1.0);
          var body = out;
          if (desfoqueFilme > 0.001) {
            final sigmaFilme = pxAt1080(desfoqueFilme * 4, fxWidth, fxHeight);
            body = ImageFiltered(
              imageFilter: ui.ImageFilter.blur(
                sigmaX: sigmaFilme,
                sigmaY: sigmaFilme,
              ),
              child: body,
            );
          }
          if ((lum - 1).abs() > 0.005) {
            body = ColorFiltered(
              colorFilter: ColorFilter.matrix(_scaleShiftMatrix(lum, 0)),
              child: body,
            );
          }
          out = Stack(
            clipBehavior: Clip.none,
            children: [
              Transform.translate(offset: Offset(dx, dy), child: body),
              Positioned.fill(
                child: IgnorePointer(
                  child: CustomPaint(
                    painter: FilmDamagePainter(
                      dust: effect.paramAt('poeira', local),
                      scratches: effect.paramAt('riscos', local),
                      burn: effect.paramAt('queimado', local),
                      time: local,
                      seed: seed,
                      hairs: effect.paramAt('fios', local).clamp(0.0, 10.0),
                      vignette: effect.paramAt('vinheta', local).clamp(0.0, 1.0),
                      dustSize: effect
                          .paramAt('tamanho_poeira', local)
                          .clamp(0.5, 3.0),
                    ),
                  ),
                ),
              ),
              if (effect.paramAt('granulacao', local) > 0.01)
                Positioned.fill(
                  child: IgnorePointer(
                    child: BlendMask(
                      blendMode: BlendMode.overlay,
                      child: CustomPaint(
                        painter: _GrainPainter(
                          amount: effect.paramAt('granulacao', local),
                          size: 1,
                          seed: seed,
                          time: local,
                        ),
                      ),
                    ),
                  ),
                ),
            ],
          );
          // A cor da copia por cima de tudo, como no shader: a poeira e os
          // riscos tambem ficam sepia.
          if ((saturacaoFilme - 1).abs() > 0.001 || sepiaFilme > 0.001) {
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(
                matrizDaCopiaDeFilme(
                  saturacao: saturacaoFilme,
                  sepia: sepiaFilme,
                ),
              ),
              child: out,
            );
          }

        // --------------------------- lote AE do dono (15/09/2026)
        case EffectType.lightSweep:
          final inten = effect.paramAt('intensidade', local).clamp(0.0, 3.0);
          if (inten > 0.02) {
            final posicao = effect.paramAt('posicao', local);
            final anguloVar = effect.paramAt('angulo', local) * math.pi / 180;
            final larguraFaixa =
                effect.paramAt('largura', local).clamp(0.02, 0.6);
            final alfaFaixa = (0.55 * inten).clamp(0.0, 1.0);
            // A SILHUETA BRANCA da camada: a luz so existe onde a camada
            // existe — e o que faz parecer varredura NELA, e nao um
            // retangulo passando por cima.
            final silhueta = ColorFiltered(
              colorFilter: const ColorFilter.matrix(<double>[
                0, 0, 0, 0, 255, //
                0, 0, 0, 0, 255, //
                0, 0, 0, 0, 255, //
                0, 0, 0, 1, 0,
              ]),
              child: out,
            );
            final dir = Offset(math.cos(anguloVar), math.sin(anguloVar));
            final faixa = ShaderMask(
              shaderCallback: (rect) {
                final c = rect.center;
                final alcance = rect.longestSide;
                final centroDaFaixa =
                    c + dir * ((posicao - 0.5) * 1.5 * alcance);
                final metade = alcance * larguraFaixa;
                return ui.Gradient.linear(
                  centroDaFaixa - dir * metade,
                  centroDaFaixa + dir * metade,
                  [
                    const Color(0x00FFFFFF),
                    Colors.white.withValues(alpha: alfaFaixa),
                    Colors.white.withValues(alpha: alfaFaixa),
                    const Color(0x00FFFFFF),
                  ],
                  const [0.0, 0.38, 0.62, 1.0],
                );
              },
              blendMode: BlendMode.srcIn,
              child: silhueta,
            );
            out = Stack(
              clipBehavior: Clip.none,
              children: [
                out,
                BlendMask(
                  blendMode: BlendMode.plus,
                  margem: 4,
                  child: faixa,
                ),
              ],
            );
          }

        case EffectType.saber:
          final intenSabre =
              effect.paramAt('intensidade', local).clamp(0.0, 4.0);
          if (intenSabre > 0.02) {
            final matiz = effect.paramAt('matiz', local).clamp(0.0, 360.0);
            final raioSabre = pxAt1080(
              effect.paramAt('raio', local).clamp(2.0, 80.0),
              fxWidth,
              fxHeight,
            );
            final nucleo = effect.paramAt('nucleo', local).clamp(0.0, 2.0);
            final cor = HSVColor.fromAHSV(1, matiz % 360, 1, 1).toColor();
            // Aura: a silhueta pintada da cor, borrada e SOMADA — duas
            // passadas (larga fraca + estreita forte) dao o miolo denso
            // com a franja aberta que o Saber de verdade tem.
            Widget tinta(Color c, double alfa) => ColorFiltered(
              colorFilter: ColorFilter.matrix(<double>[
                0, 0, 0, 0, c.r * 255, //
                0, 0, 0, 0, c.g * 255, //
                0, 0, 0, 0, c.b * 255, //
                0, 0, 0, alfa.clamp(0.0, 1.0), 0,
              ]),
              child: out,
            );
            Widget borra(Widget w, double sigma) => ImageFiltered(
              imageFilter: ui.ImageFilter.blur(
                sigmaX: sigma,
                sigmaY: sigma,
                tileMode: TileMode.decal,
              ),
              child: w,
            );
            final margemSabre = raioSabre * 3 + 6;
            out = Stack(
              clipBehavior: Clip.none,
              children: [
                out,
                BlendMask(
                  blendMode: BlendMode.plus,
                  margem: margemSabre,
                  child: borra(
                    tinta(cor, (intenSabre * 0.5).clamp(0.0, 1.0)),
                    raioSabre,
                  ),
                ),
                BlendMask(
                  blendMode: BlendMode.plus,
                  margem: margemSabre,
                  child: borra(
                    tinta(cor, (intenSabre * 0.8).clamp(0.0, 1.0)),
                    raioSabre * 0.35,
                  ),
                ),
                if (nucleo > 0.02)
                  BlendMask(
                    blendMode: BlendMode.plus,
                    margem: margemSabre,
                    child: borra(
                      tinta(Colors.white, (nucleo * 0.9).clamp(0.0, 1.0)),
                      (raioSabre * 0.12).clamp(0.6, 4.0),
                    ),
                  ),
              ],
            );
          }

        case EffectType.lensBlur:
          final raioLente = pxAt1080(
            effect.paramAt('raio', local).clamp(0.0, 60.0),
            fxWidth,
            fxHeight,
          );
          if (raioLente > 0.3) {
            final brilho = effect.paramAt('brilho', local).clamp(0.0, 3.0);
            final limiar = effect.paramAt('limiar', local).clamp(0.3, 1.0);
            final desfocada = ImageFiltered(
              imageFilter: ui.ImageFilter.blur(
                sigmaX: raioLente,
                sigmaY: raioLente,
                tileMode: TileMode.decal,
              ),
              child: out,
            );
            if (brilho <= 0.02) {
              out = desfocada;
            } else {
              // O ESTOURO DOS CLAROS: passa-altas de cor (o que passa do
              // limiar sobra; o resto zera) borrado e SOMADO por cima.
              // E o estouro que separa "lente" de "borrao".
              final ganho = (1 / math.max(0.05, 1 - limiar)) *
                  brilho.clamp(0.0, 3.0);
              final realces = ImageFiltered(
                imageFilter: ui.ImageFilter.blur(
                  sigmaX: raioLente * 1.4,
                  sigmaY: raioLente * 1.4,
                  tileMode: TileMode.decal,
                ),
                child: ColorFiltered(
                  colorFilter: ColorFilter.matrix(<double>[
                    ganho, 0, 0, 0, -255 * limiar * ganho, //
                    0, ganho, 0, 0, -255 * limiar * ganho, //
                    0, 0, ganho, 0, -255 * limiar * ganho, //
                    0, 0, 0, 1, 0,
                  ]),
                  child: out,
                ),
              );
              out = Stack(
                clipBehavior: Clip.none,
                children: [
                  desfocada,
                  BlendMask(
                    blendMode: BlendMode.plus,
                    margem: raioLente * 3 + 6,
                    child: realces,
                  ),
                ],
              );
            }
          }

        case EffectType.bit8:
          final pixel = effect.paramAt('pixel', local).clamp(8.0, 200.0);
          final coresN = effect.paramAt('cores', local).round().clamp(2, 16);
          final viva = effect.paramAt('vivacidade', local).clamp(0.0, 1.0);
          // Pixel grande sem interpolacao + poucas cores + saturacao de
          // fliperama — a mecanica do mosaic e do posterize num efeito
          // so, com a cara da epoca.
          out = ImageFiltered(
            imageFilter: ui.ImageFilter.compose(
              outer: ui.ImageFilter.matrix(
                Matrix4.diagonal3Values(pixel / 3, pixel / 3, 1).storage,
                filterQuality: FilterQuality.none,
              ),
              inner: ui.ImageFilter.matrix(
                Matrix4.diagonal3Values(3 / pixel, 3 / pixel, 1).storage,
                filterQuality: FilterQuality.none,
              ),
            ),
            child: out,
          );
          out = ColorFiltered(
            colorFilter: ColorFilter.matrix(
              _posterizeMatrix(coresN.toDouble()),
            ),
            child: out,
          );
          if (viva > 0.01) {
            final sViva = 1 + viva;
            final inv = 1 - sViva;
            out = ColorFiltered(
              colorFilter: ColorFilter.matrix(<double>[
                0.213 * inv + sViva, 0.715 * inv, 0.072 * inv, 0, 0, //
                0.213 * inv, 0.715 * inv + sViva, 0.072 * inv, 0, 0, //
                0.213 * inv, 0.715 * inv, 0.072 * inv + sViva, 0, 0, //
                0, 0, 0, 1, 0,
              ]),
              child: out,
            );
          }

        case EffectType.smear:
          final compr = effect.paramAt('comprimento', local);
          final forca = effect.paramAt('intensidade', local);
          if (compr > 0.5 && forca > 0.001) {
            out = FxSnapshot(
              painter: SmearPainter(
                length: compr,
                angleDeg: effect.paramAt('angulo', local),
                intensity: forca.clamp(0.0, 1.0),
                stretch: effect.paramAt('esticar', local).clamp(0.0, 1.0),
              ),
              child: out,
            );
          }

        case EffectType.bubbleBlur:
          final raioDaBolha = effect.paramAt('tamanho', local);
          final bolhas = effect.paramAt('quantidade', local).round();
          if (raioDaBolha > 1 && bolhas >= 1) {
            out = FxSnapshot(
              painter: BubbleBlurPainter(
                radius: raioDaBolha,
                count: bolhas.clamp(1, 10),
                blur: effect.paramAt('desfoque', local).clamp(0.0, 60.0),
                magnify: effect.paramAt('aumento', local),
                phase: effect.paramAt('fase', local),
                seed: effect.paramAt('semente', local).round(),
              ),
              child: out,
            );
          }

        case EffectType.blobTracker:
          // As caixas vem da ANALISE ja gravada. Sem analise, o pintor
          // simula — para a pessoa ajustar a aparencia antes de gastar
          // o processamento.
          // Opacidade zero e o desligado do rastreio: sem isto ele
          // desenhava tudo para pintar com alfa zero em cima.
          if (effect.paramAt('opacity', local) <= 0.4) break;
          final rastreio = BlobTrackService.instance.dataFor(effect.id);
          // O DESENHO vem sem posicionamento: quem posiciona e a linha
          // la embaixo, DEPOIS da mescla. Um `Positioned` so vale como
          // filho DIRETO de um `Stack` — embrulhado num BlendMask ele
          // vira um widget de dado-do-pai perdido: em depuracao levanta
          // "Incorrect use of ParentDataWidget", e em producao a
          // sobreposicao fica sem tamanho e SOME. Era o que acontecia
          // com os modos de mescla Somar e Tela do rastreio.
          final desenhoDoRastreio = IgnorePointer(
            child: CustomPaint(
              painter: BlobTrackerPainter(
                track: rastreio,
                time: local,
                color: effect.color,
                style: effect.paramAt('style', local).round().clamp(0, 4),
                showCenter: effect.paramAt('show_center_marker', local) >= 0.5,
                showLines:
                    effect.paramAt('show_connecting_lines', local) >= 0.5,
                lineType: effect
                    .paramAt('line_type', local)
                    .round()
                    .clamp(0, 2),
                lineStyle: effect
                    .paramAt('line_style', local)
                    .round()
                    .clamp(0, 2),
                palette: effect.paramAt('palette', local).round().clamp(0, 2),
                thickness: effect.paramAt('thickness', local),
                opacity: effect.paramAt('opacity', local),
                fill: effect.paramAt('fill', local),
                cornerLength: effect.paramAt('corner_length', local),
                showCaption: effect.paramAt('show_caption', local) >= 0.5,
                captionContent: effect
                    .paramAt('caption_content', local)
                    .round()
                    .clamp(0, 2),
                captionPosition: effect
                    .paramAt('caption_position', local)
                    .round()
                    .clamp(0, 3),
                fontSize: effect.paramAt('font_size', local),
                seed: effect.paramAt('seed', local).round(),
                simulatedCount: effect
                    .paramAt('max_blobs', local)
                    .round()
                    .clamp(1, 16),
              ),
            ),
          );

          // OVERLAY ONLY: so as sobreposicoes, fundo transparente —
          // serve para levar o rastreio para outra camada.
          final soSobreposicao = effect.paramAt('overlay_only', local) >= 0.5;
          final modoBlob = effect
              .paramAt('blend_mode', local)
              .round()
              .clamp(0, 2);
          final misturado = switch (modoBlob) {
            1 => BlendMask(blendMode: BlendMode.plus, child: desenhoDoRastreio),
            2 => BlendMask(
              blendMode: BlendMode.screen,
              child: desenhoDoRastreio,
            ),
            _ => desenhoDoRastreio,
          };
          final camadaBlob = Positioned.fill(child: misturado);

          // O conteudo da camada FICA na pilha mesmo no modo "so
          // sobreposicao" — com opacidade zero, que o Flutter nao chega
          // a pintar. Ele esta ali para DAR TAMANHO: uma pilha cujos
          // filhos sao todos posicionados nao tem de onde tirar largura
          // e altura, e sob restricao livre (que e o caso de uma camada
          // com transformacao) isso quebra o quadro inteiro, nao so o
          // efeito.
          out = Stack(
            clipBehavior: Clip.none,
            children: [
              if (soSobreposicao) Opacity(opacity: 0, child: out) else out,
              camadaBlob,
            ],
          );
      }
    }
    return out;
  }

  /// Matriz de ganho+deslocamento igual nos tres canais.
  /// Escala e desloca. Com [canal] 1..3 mexe so em R, G ou B — e o
  /// "por canal" do Levels avancado.
  static List<double> _scaleShiftMatrix(
    double s,
    double shift, {
    int canal = 0,
  }) {
    final b = shift * 255;
    if (canal != 0) {
      final r = canal == 1, g = canal == 2, bl = canal == 3;
      return <double>[
        r ? s : 1, 0, 0, 0, r ? b : 0, //
        0, g ? s : 1, 0, 0, g ? b : 0,
        0, 0, bl ? s : 1, 0, bl ? b : 0,
        0, 0, 0, 1, 0,
      ];
    }
    return <double>[s, 0, 0, 0, b, 0, s, 0, 0, b, 0, 0, s, 0, b, 0, 0, 0, 1, 0];
  }

  /// Saturacao com guarda no vermelho (protecao de tom de pele).
  static List<double> _saturationMatrix(double sat, {double redGuard = 0}) {
    const lr = 0.2126, lg = 0.7152, lb = 0.0722;
    final s = sat;
    final rs = s - (s - 1) * redGuard.clamp(0.0, 1.0);
    return <double>[
      lr * (1 - rs) + rs,
      lg * (1 - rs),
      lb * (1 - rs),
      0,
      0,
      lr * (1 - s),
      lg * (1 - s) + s,
      lb * (1 - s),
      0,
      0,
      lr * (1 - s),
      lg * (1 - s),
      lb * (1 - s) + s,
      0,
      0,
      0,
      0,
      0,
      1,
      0,
    ];
  }

  /// Saturacao seguida de ganho de brilho, numa unica matriz. Quando
  /// ambos valem 1 a matriz e identidade; o alfa nunca e alterado.
  static List<double> _saturationBrightnessMatrix(
    double saturation,
    double brightness,
  ) {
    final m = _saturationMatrix(saturation);
    return <double>[
      m[0] * brightness,
      m[1] * brightness,
      m[2] * brightness,
      0,
      0,
      m[5] * brightness,
      m[6] * brightness,
      m[7] * brightness,
      0,
      0,
      m[10] * brightness,
      m[11] * brightness,
      m[12] * brightness,
      0,
      0,
      0,
      0,
      0,
      1,
      0,
    ];
  }

  /// Aproximacao de posterizacao: contraste alto centrado, que agrupa os
  /// tons em patamares visiveis.
  static List<double> _posterizeMatrix(double levels) {
    final c = 1 + (32 - levels) / 12;
    final b = (1 - c) * 0.5 * 255;
    return <double>[c, 0, 0, 0, b, 0, c, 0, 0, b, 0, 0, c, 0, b, 0, 0, 0, 1, 0];
  }

  /// Isola um canal (0=R, 1=G, 2=B) preservando o alfa — base da franja
  /// cromatica, da separacao RGB e da aberracao do glow.
  Widget _channelIso(Widget child, int channel) {
    const zeros = [0.0, 0.0, 0.0, 0.0, 0.0];
    final rows = [
      channel == 0 ? const [1.0, 0.0, 0.0, 0.0, 0.0] : zeros,
      channel == 1 ? const [0.0, 1.0, 0.0, 0.0, 0.0] : zeros,
      channel == 2 ? const [0.0, 0.0, 1.0, 0.0, 0.0] : zeros,
      const [0.0, 0.0, 0.0, 1.0, 0.0],
    ];
    return ColorFiltered(
      colorFilter: ColorFilter.matrix([for (final r in rows) ...r]),
      child: child,
    );
  }
}

/// A camera de uma Cena 3D com o nulo da COMPOSICAO ja aplicado.
///
/// A ponte entre as duas hierarquias: a arvore de camadas da composicao
/// e o grafo interno da cena. Quem conhece a cadeia de parenting de fora
/// e o compositor, entao o transform do nulo chega pronto aqui.
///
/// Devolve null quando nao ha nada a resolver — assim o pintor segue
/// pelo caminho barato de sempre.
RenderCamera? cameraDaCena(
  VideoProject project,
  Scene3DLayer l,
  Duration local,
  Duration global,
) {
  final paiId =
      l.cameraParentLayerId ??
      project.linkFor(l.id, LayerProp.parent)?.sourceLayerId;
  if (paiId == null) {
    return l.shots.isEmpty && l.scene.cameraParentId == null
        ? null
        : l.cameraAt(local);
  }
  final pai = project.layerById(paiId);
  if (pai == null) return l.cameraAt(local);

  // O transform EFETIVO do nulo (com a cadeia dele ja resolvida).
  final eff = effectiveTransform(project, pai, global);
  final centro = Offset(project.outputWidth / 2, project.outputHeight / 2);

  // Da composicao (Y para baixo, Z afastando) para a CENA (Y para cima,
  // camera olhando -Z): a MESMA meia-volta dos nos — X igual, Y e Z
  // trocam de sinal, rotX fica, rotY e rotZ invertem. Era daqui que
  // vinha o "nao segue o eixo": a camera recebia o nulo em coordenadas
  // da composicao cruas, entao subir o nulo descia a cena e o giro
  // orbitava para o lado errado. A ESCALA nao entra: camera nao tem
  // escala, e herdar e o bug que faz o enquadramento explodir.
  return l.cameraAt(
    local,
    external: NodeTransform(
      position: Vec3(
        eff.pos.dx - centro.dx,
        centro.dy - eff.pos.dy,
        -eff.z,
      ),
      rotX: eff.rotX,
      rotY: -eff.rotY,
      rotZ: -eff.rot,
    ),
  );
}

/// OS NOS DA CENA PRESOS A UM NULO DA COMPOSICAO (Texto 3D estilo Element
/// 3D). O nulo e uma camada da linha do tempo: quem conhece a cadeia de
/// vinculos dele e o compositor, entao o transform EFETIVO dele e composto
/// por fora do no aqui, e a cena entregue ao motor (GPU ou CPU) ja traz o
/// no no lugar. Os filhos do no vem junto pela cadeia normal da cena.
///
/// Da composicao (Y para baixo, Z positivo afastando) para a cena (Y para
/// cima, camera olhando para -Z): X igual, Y e Z trocam de sinal — o que
/// e uma meia volta em X, entao a rotacao em X fica e Y e Z invertem.
/// Um px da composicao vale uma unidade da cena, como na camera.
Scene3D cenaComNulosDaComposicao(
  VideoProject project,
  Scene3DLayer l,
  Duration local,
  Duration global,
) {
  if (!l.scene.nodes.any((n) => n.compParentLayerId != null)) {
    return _cenaComFimDaCamada(l);
  }
  final centro = Offset(project.outputWidth / 2, project.outputHeight / 2);
  SceneNode noNoLugar(SceneNode n) {
    final pai = project.layerById(n.compParentLayerId!);
    if (pai == null) return n;
    final eff = effectiveTransform(project, pai, global);
    final r = composeTransforms(
      NodeTransform(
        position: Vec3(
          eff.pos.dx - centro.dx,
          centro.dy - eff.pos.dy,
          -eff.z,
        ),
        rotX: eff.rotX,
        rotY: -eff.rotY,
        rotZ: -eff.rot,
        scale: eff.scale,
      ),
      NodeTransform(
        position: n.positionAt(local),
        rotX: n.rotX.valueAt(local),
        rotY: n.rotY.valueAt(local),
        rotZ: n.rotZ.valueAt(local),
        scale: n.scale.valueAt(local),
      ),
    );
    return n.copyWith(
      x: AnimatedDouble(r.position.x),
      y: AnimatedDouble(r.position.y),
      z: AnimatedDouble(r.position.z),
      rotX: AnimatedDouble(r.rotX),
      rotY: AnimatedDouble(r.rotY),
      rotZ: AnimatedDouble(r.rotZ),
      scale: AnimatedDouble(r.scale),
    );
  }

  return l.scene.copyWith(
    fimDaCamada: l.duration,
    nodes: [
      for (final n in l.scene.nodes)
        n.compParentLayerId == null ? n : noNoLugar(n),
    ],
  );
}

/// A cena com a duracao da camada carimbada (ancora da SAIDA do texto
/// animado) — mas SO quando algum texto animado precisa dela: carimbar
/// sempre criaria uma cena nova por quadro e mataria o cache de quadro
/// do pintor de CPU, que reconhece cena parada por identidade.
Scene3D _cenaComFimDaCamada(Scene3DLayer l) {
  if (!l.scene.nodes.any((n) => n.modelAsset?.temAnimacaoDeTexto ?? false)) {
    return l.scene;
  }
  return l.scene.copyWith(fimDaCamada: l.duration);
}

class _LayerContent extends StatelessWidget {
  const _LayerContent({
    this.exportFrames,
    required this.exporting,
    this.tempoAlheio,
    this.quadroEm,
    required this.layer,
    required this.project,
    required this.compWidth,
    required this.videos,
    required this.localTime,
    required this.buildChildren,
    this.particlesRotX = 0,
    this.particlesRotY = 0,
  });

  final Layer layer;
  final VideoProject project;
  final double compWidth;
  final VideoLayerManager videos;
  final Duration localTime;

  /// Rotacao 3D do sistema de particulas (graus), ja com o delta do pai.
  final double particlesRotX;
  final double particlesRotY;

  /// Quadro ja decodificado por camada de video (so na exportacao).
  final Map<String, ui.Image>? exportFrames;
  final bool exporting;

  /// Instante da composicao em que esta camada foi montada, quando e
  /// OUTRO que o do relogio e o efeito pediu o quadro daquele instante.
  final Duration? tempoAlheio;

  /// Exportando: o quadro de video de outro instante.
  final ui.Image? Function(VideoLayer layer, Duration tempo)? quadroEm;

  /// A CAIXA DA MIDIA nesta composicao, pelo ajuste da camada e pela
  /// proporcao do quadro EXIBIDO (ver ajuste_da_midia.dart).
  Size _caixa(AjusteDaMidia ajuste, double? proporcao) => caixaDaMidia(
    Size(compWidth, project.outputHeight.toDouble()),
    proporcao,
    ajuste,
  );

  Widget _quadroFixo(VideoLayer l, ui.Image img) {
    final caixa = _caixa(l.ajuste, img.width / img.height);
    return SizedBox(
      width: caixa.width,
      height: caixa.height,
      child: RawImage(
        image: img,
        fit: BoxFit.fill,
        filterQuality: FilterQuality.medium,
      ),
    );
  }

  /// A FOTO na caixa do ajuste. Pela largura e o de sempre; cobrindo ou
  /// contida, a caixa sai da proporcao guardada na importacao — e, sem
  /// ela, da propria composicao, com o encaixe feito pelo Image.
  Widget _imagem(ImageLayer l) {
    Widget quebrada(BuildContext _, Object _, StackTrace? _) =>
        _brokenMedia();
    if (l.ajuste == AjusteDaMidia.largura) {
      return Image.file(
        File(l.sourcePath),
        width: compWidth,
        fit: BoxFit.contain,
        errorBuilder: quebrada,
      );
    }
    final proporcao = proporcaoValida(l.proporcaoDaFonte);
    final caixa = _caixa(l.ajuste, proporcao);
    return SizedBox(
      width: caixa.width,
      height: caixa.height,
      child: Image.file(
        File(l.sourcePath),
        fit: proporcao != null
            ? BoxFit.fill
            : (l.ajuste == AjusteDaMidia.cobrir
                  ? BoxFit.cover
                  : BoxFit.contain),
        errorBuilder: quebrada,
      ),
    );
  }

  /// PREVIA de outro instante: o quadro extraido do arquivo, se ja veio;
  /// senao o tocador ao vivo, e a extracao e pedida.
  Widget _videoDeOutroTempo(VideoLayer l, Duration tempo) =>
      ValueListenableBuilder<int>(
        valueListenable: QuadrosDeVideo.instance.revision,
        builder: (context, _, _) {
          final arquivo = ProxyService.instance.playbackPath(l.sourcePath);
          final fonte = videoAbsoluteSourceTimeAt(l, l.localTime(tempo));
          final img = QuadrosDeVideo.instance.quadro(arquivo, fonte);
          if (img != null) return _quadroFixo(l, img);
          QuadrosDeVideo.instance.preparar(
            arquivo,
            l.sourceOffset,
            l.sourceOffset + videoSourceSpan(l),
          );
          return _videoAoVivo(l);
        },
      );

  Widget _videoAoVivo(VideoLayer l) => RepaintBoundary(
    child: ValueListenableBuilder<int>(
      valueListenable: videos.revision,
      builder: (context, _, _) {
        final controller = videos.controllerFor(l.id);
        if (controller == null || !controller.value.isInitialized) {
          // O LUGAR DO VIDEO enquanto ele abre ja tem o tamanho certo
          // (a proporcao veio do probe): nada pula quando o quadro chega.
          final caixa = _caixa(l.ajuste, l.proporcaoDaFonte);
          return SizedBox(
            width: caixa.width,
            height: caixa.height,
            child: const Center(
              child: Icon(CupertinoIcons.film, size: 60, color: Colors.white24),
            ),
          );
        }
        final caixa = _caixa(
          l.ajuste,
          proporcaoExibidaDoVideo(
            controller.value.aspectRatio,
            controller.value.rotationCorrection,
          ),
        );
        return SizedBox(
          width: caixa.width,
          height: caixa.height,
          child: VideoPlayer(controller),
        );
      },
    ),
  );

  /// Recursao do precomp: constroi as camadas filhas no tempo local.
  final List<Widget> Function(List<Layer> layers, Duration t) buildChildren;

  /// Caminho da camada de forma [id], ja avaliado no tempo — para o
  /// texto que segue uma forma desenhada no proprio projeto.
  static ui.Path? _pathOfShapeLayer(
    VideoProject project,
    String? id,
    Duration t,
  ) {
    if (id == null) return null;
    final l = project.layerById(id);
    if (l is! ShapeLayer) return null;
    final draws = evaluateShape(l.contents, l.localTime(t));
    if (draws.isEmpty) return null;
    final out = ui.Path();
    for (final d in draws) {
      out.addPath(d.path, Offset.zero);
    }
    return out;
  }

  @override
  Widget build(BuildContext context) {
    Widget child = switch (layer) {
      // Caminho rapido sem animador ativo (I2: linha inteira, com kerning).
      TextLayer l when l.hasTextAnimation => AnimatedTextView(
        layer: l,
        localTime: localTime,
        // Texto seguindo OUTRA camada: o widget nao sabe resolver id,
        // entao o caminho chega pronto de quem monta a composicao.
        pathOverride: _pathOfShapeLayer(
          project,
          l.textPath.shapeLayerId,
          localTime,
        ),
      ),
      TextLayer l => Text(
        l.text,
        textAlign: l.alinhamento,
        style: AnimatedTextView.styleFor(l),
      ),
      // Forma vetorial: arvore avaliada no tempo local, pintada por Path.
      ShapeLayer l => _ShapeView(layer: l, localTime: localTime),
      // CONTEINER CENA 3D: por fora e uma camada; por dentro roda o
      // proprio renderizador, com passe opaco e passe transparente
      // ordenados POR TRIANGULO.
      Scene3DLayer l => SizedBox(
        width: compWidth,
        height: project.outputHeight.toDouble(),
        child: ValueListenableBuilder<int>(
          valueListenable: TextureCache.instance.revision,
          builder: (_, _, _) {
            // A PONTE ENTRE AS DUAS HIERARQUIAS: o nulo da composicao
            // vira pai externo da camera da cena. Quem conhece a
            // cadeia de parenting de fora e o compositor, entao o
            // transform chega pronto aqui.
            final resolvida = cameraDaCena(
              project,
              l,
              localTime,
              layer.startTime + localTime,
            );
            final cena = cenaComNulosDaComposicao(
              project,
              l,
              localTime,
              layer.startTime + localTime,
            );
            // Ajudas NUNCA entram na exportacao — so no preview.
            final ajudas = !exporting && l.showHelpers;
            // RASCUNHO ENQUANTO TOCA. Em 33 ms nao cabe reflexo no
            // chao, sombra de contato e profundidade de campo de uma
            // cena inteira; num celular a conta passa de 300 ms por
            // quadro, e uma thread bloqueada por 300 ms e o que faz o
            // iOS matar o app. Quem aperta o play quer ver o
            // MOVIMENTO — a qualidade cheia volta na pausa e na
            // exportacao, que e onde ela e olhada de perto.
            return ValueListenableBuilder<bool>(
              valueListenable: PlaybackController.tocandoAgora,
              builder: (_, tocando, _) {
                final rascunho = tocando && !exporting;
                // O MOTOR EM GPU desenha a cena quando existe; sem ele
                // fica o pintor em CPU de sempre.
                //
                // AS AJUDAS NAO DESQUALIFICAM MAIS A GPU. A porta antiga
                // exigia `!ajudas`, e `showHelpers` nasce ligado em toda
                // cena nova: cada cena 3D recem-criada ia parar no
                // pintor de CPU sem ninguem saber por que — e um modelo
                // de loja no pintor de CPU e o app a 1 fps num iPhone 13.
                // As ajudas (grade, frustum, caixa) sao desenhadas por
                // cima do quadro da GPU, como o Filament ja fazia.
                if (!Scene3DGpu.indisponivel) {
                  return Scene3DGpuView(
                    exporting: exporting,
                    scene: cena,
                    camera: l.camera,
                    renderCamera: l.view == SceneView.camera
                        ? (resolvida ?? l.camera.renderAt(localTime))
                        : orthoViewCamera(l.view),
                    view: l.view,
                    time: localTime,
                    rascunho: rascunho,
                    showHelpers: ajudas,
                  );
                }
                return CustomPaint(
                  painter: Scene3DPainter(
                    scene: rascunho ? cena.copyWith(draftMode: true) : cena,
                    camera: l.camera,
                    resolvedCamera: resolvida,
                    view: l.view,
                    time: localTime,
                    showHelpers: ajudas,
                    // NA EXPORTACAO o quadro pode demorar o que precisar:
                    // ninguem esta esperando resposta ao dedo, e o
                    // arquivo entregue nunca leva substituto.
                    respeitarOrcamento: !exporting,
                  ),
                );
              },
            );
          },
        ),
      ),
      // Precomp: filhos compostos no tempo local do grupo.
      // PRECOMP: tempo proprio (com remapeamento), quadro proprio e a
      // opcao de colapsar — que e o que evita a forma vetorial pixelar
      // quando a precomp e ampliada.
      GroupLayer l => SizedBox(
        width: compWidth,
        height: project.outputHeight.toDouble(),
        child: ClipRect(
          clipBehavior: l.clipToComp && !l.collapse ? Clip.hardEdge : Clip.none,
          // GRUPO DE MASCARA / EXCLUSAO: um filho em dstIn/dstOut recorta
          // SO os irmaos. Sem o grupo isolado, recortava tambem o que ja
          // estava pintado por baixo do grupo.
          child: l.children.any(
                (c) =>
                    c.blendMode == BlendMode.dstIn ||
                    c.blendMode == BlendMode.dstOut,
              )
              ? BlendMask(
                  blendMode: BlendMode.srcOver,
                  isolate: true,
                  child: Stack(
                    clipBehavior: Clip.none,
                    children: buildChildren(
                      l.children,
                      l.contentTimeAt(localTime),
                    ),
                  ),
                )
              : Stack(
                  clipBehavior: Clip.none,
                  children: buildChildren(
                    l.children,
                    l.contentTimeAt(localTime),
                  ),
                ),
        ),
      ),
      ImageLayer l => RepaintBoundary(child: _imagem(l)),
      // EXPORTANDO: o quadro vem decodificado do disco. A textura do
      // player nunca entra num `toImage`, entao o video sairia preto.
      // OUTRO INSTANTE, EXPORTANDO: o quadro decodificado para ele.
      VideoLayer l
          when exporting &&
              tempoAlheio != null &&
              quadroEm?.call(l, tempoAlheio!) != null =>
        _quadroFixo(l, quadroEm!(l, tempoAlheio!)!),
      // OUTRO INSTANTE, NA PREVIA: o quadro extraido (ou o ao vivo).
      VideoLayer l when !exporting && tempoAlheio != null =>
        _videoDeOutroTempo(l, tempoAlheio!),
      VideoLayer l when exportFrames != null && exportFrames![l.id] != null =>
        _quadroFixo(l, exportFrames![l.id]!),
      VideoLayer l => _videoAoVivo(l),
      AudioLayer _ => const SizedBox.shrink(),
      // OBJETO NULO: o quadrado tracejado com o X e uma AJUDA — existe
      // para se ver o que se esta arrastando. O comentario aqui sempre
      // disse "so no editor", mas o desenho nao perguntava se estava
      // exportando: o gizmo saia no video entregue. Um nulo nao tem
      // pixel nenhum para dar; exportando, ele nao desenha nada.
      NullLayer _ when exporting => const SizedBox.shrink(),
      NullLayer _ => const IgnorePointer(
        child: CustomPaint(size: Size(220, 220), painter: NullGizmoPainter()),
      ),
      // Ajuste nao tem conteudo proprio: age no composto (interceptado
      // em _buildLayers); aqui rende so o gizmo de selecao.
      AdjustmentLayer _ => const SizedBox(width: 220, height: 220),
      // Particulas em espaco 3D: simulacao + projecao por particula.
      ParticlesLayer l => CustomPaint(
        size: const Size(420, 420),
        painter: ParticlesPainter(
          layer: l,
          time: localTime,
          rotXDeg: particlesRotX,
          rotYDeg: particlesRotY,
        ),
      ),
      // Elemento 3D: vertices girados no espaco dentro do pintor (como
      // as particulas) — nada de inclinar o canvas como um cartao.
      Element3DLayer l => ListenableBuilder(
        listenable: Listenable.merge([
          TextureCache.instance.revision,
          MeshCache.instance.revision,
        ]),
        builder: (_, _) => CustomPaint(
          size: const Size(620, 620),
          painter: World3DPainter(
            items: [
              World3DItem(
                layer: l,
                center: const Offset(310, 310),
                rotXDeg: particlesRotX,
                rotYDeg: particlesRotY,
                material: l.material,
                gradient: l.gradient,
                shininess: l.shininess,
              ),
            ],
          ),
        ),
      ),
      CaptionLayer l => Builder(
        builder: (context) {
          // ESTILO DESTAQUE: a frase inteira na tela, com a palavra dita
          // inflando no lugar dela. Precisa de tempo POR PALAVRA — sem
          // ele, cai para a legenda comum, que e falhar com dignidade.
          if (l.highlight.ativo && !l.highlight.isNeutro) {
            final frases = agruparEmFrases(l.cues);
            final frase = fraseEm(frases, localTime);
            if (frase != null) {
              return SizedBox(
                width: compWidth,
                height: project.outputHeight.toDouble(),
                child: CustomPaint(
                  painter: CaptionHighlightPainter(
                    frase: frase,
                    tempo: localTime,
                    estilo: l.highlight,
                    corpo: l.style.fontSize,
                  ),
                ),
              );
            }
            return const SizedBox.shrink();
          }
          final cue = l.cueAt(localTime);
          if (cue == null) return const SizedBox.shrink();
          return Container(
            constraints: BoxConstraints(maxWidth: compWidth * 0.86),
            padding: const EdgeInsets.symmetric(horizontal: 28, vertical: 14),
            decoration: BoxDecoration(
              color: l.style.backgroundColor.withValues(
                alpha: l.style.backgroundOpacity,
              ),
              borderRadius: BorderRadius.circular(14),
            ),
            child: Text(
              cue.text,
              textAlign: TextAlign.center,
              style: TextStyle(
                color: l.style.color,
                fontSize: l.style.fontSize,
                fontWeight: l.style.bold ? FontWeight.w700 : FontWeight.w400,
                height: 1.2,
              ),
            ),
          );
        },
      ),
      CameraLayer _ => const SizedBox.shrink(),
    };

    return child;
  }

  Widget _brokenMedia() => Container(
    width: 400,
    height: 300,
    color: Colors.white10,
    child: const Icon(
      CupertinoIcons.exclamationmark_triangle,
      color: Colors.white38,
    ),
  );
}

/// Pinta a arvore da forma (vetorial: nitida em qualquer escala).
class _ShapeView extends StatelessWidget {
  const _ShapeView({required this.layer, required this.localTime});

  final ShapeLayer layer;
  final Duration localTime;

  @override
  Widget build(BuildContext context) {
    final draws = evaluateShape(layer.contents, localTime);
    final bounds = shapeBounds(draws);
    return CustomPaint(
      size: bounds.size,
      painter: _ShapePainter(draws: draws, bounds: bounds),
    );
  }
}

class _ShapePainter extends CustomPainter {
  // Repinta quando uma textura termina de carregar: a foto de um
  // preenchimento por midia chega depois do primeiro quadro.
  _ShapePainter({required this.draws, required this.bounds})
    : super(repaint: TextureCache.instance.revision);

  final List<ShapeDraw> draws;
  final Rect bounds;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.translate(-bounds.left, -bounds.top);
    for (final d in draws) {
      final caminho = d.imagem;
      if (caminho == null) {
        canvas.drawPath(d.path, d.paint);
        continue;
      }
      final imagem = TextureCache.instance.imageFor(caminho);
      if (imagem == null) {
        canvas.drawPath(
          d.path,
          Paint()..color = Color.fromRGBO(255, 255, 255, .18 * d.paint.color.a),
        );
        continue;
      }
      final tamanho = Size(
        imagem.width.toDouble(),
        imagem.height.toDouble(),
      );
      final destino = destinoDaMidiaNaForma(
        d.path.getBounds(),
        tamanho,
        d.encaixe,
      );
      canvas
        ..save()
        ..clipPath(d.path)
        ..drawImageRect(
          imagem,
          Offset.zero & tamanho,
          destino,
          Paint()
            ..filterQuality = FilterQuality.medium
            ..color = Color.fromRGBO(255, 255, 255, d.paint.color.a),
        )
        ..restore();
    }
  }

  @override
  bool shouldRepaint(_ShapePainter old) => true;
}

/// O recorte de uma faixa do Time Slice: o poligono ja vem pronto, no
/// espaco da composicao.
class _RecorteDaFaixa extends CustomClipper<ui.Path> {
  _RecorteDaFaixa(this.caminho);
  final ui.Path caminho;

  @override
  ui.Path getClip(Size size) => caminho;

  @override
  bool shouldReclip(covariant _RecorteDaFaixa oldClipper) =>
      !identical(oldClipper.caminho, caminho);
}
