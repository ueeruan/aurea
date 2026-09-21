import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/ui/snack.dart';
import '../../../application/editor_controller.dart';
import '../../../application/interacao.dart';
import '../../../domain/keyframe.dart';
import '../../../domain/layer.dart';
import '../../../domain/video_project.dart';
import '../shell/contrato.dart';
import 'comum.dart';
import 'comum_3d.dart';

/// CAMERA — a camera da composicao, em tres abas:
///
///   Lente    perspectiva ou ortografica, angulo de visao e lente (a MESMA
///            trilha: o losango de uma crava a outra)
///   Foco     desfoque por distancia: onde fica o plano nitido, quanto
///            borra e a espessura da faixa nitida
///   Neblina  cor, onde comeca e onde cobre tudo
///
/// Posicao, giro 3D e Z da camera moram em Transformar. So camadas com o
/// 3D ligado sao vistas pela camera — e so elas ganham foco e neblina.
class PainelCamera extends ConsumerStatefulWidget {
  const PainelCamera({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<PainelCamera> createState() => _PainelCameraState();
}

class _PainelCameraState extends ConsumerState<PainelCamera> {
  static const _titulo = 'Câmera';
  static const _abas = ['Lente', 'Foco', 'Neblina'];

  int _aba = 0;

  /// O aviso "tem keyframes, use o losango" sai UMA vez por arrasto.
  bool _avisou = false;

  @override
  Widget build(BuildContext context) {
    final escopo = EscopoDoEditor.of(context);
    final id = widget.layerId;
    final visivel = camadaVisivel(ref, id);
    final gravada = camadaGravada(ref, id);
    if (visivel == null || gravada == null) {
      return const PainelSemCamada(titulo: _titulo);
    }
    final chave = 'painel-${PainelId.camera.name}';
    if (visivel is! CameraLayer || gravada is! CameraLayer) {
      return PainelDePortas(
        titulo: _titulo,
        chave: chave,
        aviso: 'Esta camada não é uma câmera.',
        portas: const [],
      );
    }
    final largura = ref.watch(
      editorControllerProvider.select((p) => p.outputWidth.toDouble()),
    );
    final c = ref.read(editorControllerProvider.notifier);
    return AureaPanel(
      titulo: _titulo,
      chave: chave,
      abas: _abas,
      abaAtiva: _aba,
      aoTrocarAba: (i) => setState(() => _aba = i),
      aoFechar: escopo.fecharPainel,
      corpo: NoCabecote(
        construir: (context, t) => ListView(
          key: ValueKey('camera-aba-$_aba'),
          padding: paddingDoPainel,
          children: switch (_aba) {
            0 => _lente(c, visivel, gravada, largura, t),
            1 => _foco(c, visivel, gravada, t),
            _ => _neblina(c, visivel, gravada, t),
          },
        ),
      ),
    );
  }

  /// O LOSANGO DA LENTE: angulo e lente sao a mesma trilha.
  ({KeyframeState estado, VoidCallback? anterior, VoidCallback? proximo})
  _kfDaLente(EditorController c, CameraLayer gravada, Duration t) =>
      losangoDasMarcas(
        marcasUs: marcasDaTrilha(gravada.zoom),
        camada: gravada,
        t: t,
        playback: EscopoDoEditor.of(context).playback,
        aoAlternar: () => c.toggleCameraZoomKeyframe(gravada.id, t),
      );

  List<Widget> _lente(
    EditorController c,
    CameraLayer visivel,
    CameraLayer gravada,
    double largura,
    Duration t,
  ) {
    final local = visivel.localTime(t);
    final zoom = visivel.zoom.valueAt(local);
    final o = visivel.opcoes;
    final kf = _kfDaLente(c, gravada, t);
    return [
      LinhaDeFichas<bool>(
        rotulo: 'Projeção',
        chave: 'camera-projecao',
        valores: const [false, true],
        rotuloDe: (orto) => orto ? 'Ortográfica' : 'Perspectiva',
        escolhido: o.ortografica,
        chaveDe: (orto) => orto ? 'camera-ortografica' : 'camera-perspectiva',
        aoEscolher: (v) => c.atualizarOpcoesDaCamera(
          gravada.id,
          (x) => x.copyWith(ortografica: v),
        ),
      ),
      if (!o.ortografica)
        AureaPropertyRow(
          rotulo: 'Ângulo de visão',
          chave: 'camera-angulo',
          valor: anguloDaLente(largura, zoom).clamp(1.0, 170.0).toDouble(),
          min: 1,
          max: 170,
          casas: 0,
          unidade: '°',
          keyframe: kf.estado,
          aoAnterior: kf.anterior,
          aoProximo: kf.proximo,
          aoComecarGesto: c.beginGesture,
          aoTerminarGesto: c.endGesture,
          aoMudar: (v) {
            Interacao.marcar();
            c.editCameraFov(gravada.id, t, v);
          },
        ),
      AureaPropertyRow(
        rotulo: 'Lente',
        chave: 'camera-lente',
        valor: zoom.clamp(60.0, 12000.0).toDouble(),
        min: 60,
        max: 12000,
        casas: 0,
        sensibilidade: 4,
        keyframe: kf.estado,
        aoAnterior: kf.anterior,
        aoProximo: kf.proximo,
        aoResetar: () =>
            c.editCameraZoom(gravada.id, t, CameraLayer.lenteNeutra),
        aoComecarGesto: c.beginGesture,
        aoTerminarGesto: c.endGesture,
        aoMudar: (v) {
          Interacao.marcar();
          c.editCameraZoom(gravada.id, t, v);
        },
      ),
    ];
  }

  /// UMA TRILHA DAS OPCOES DA CAMERA: o losango crava nela; editar segue
  /// a regra de todo numero animado (fora de marca, o losango decide).
  AureaPropertyRow _opcao(
    EditorController c, {
    required CameraLayer visivel,
    required CameraLayer gravada,
    required Duration t,
    required String rotulo,
    required String chave,
    required AnimatedDouble Function(OpcoesDaCamera) trilhaDe,
    required OpcoesDaCamera Function(OpcoesDaCamera, AnimatedDouble) poe,
    required double min,
    required double max,
    double? sensibilidade,
  }) {
    final local = visivel.localTime(t);
    final vista = trilhaDe(visivel.opcoes);
    final marcada = trilhaDe(gravada.opcoes);
    void mudar(OpcoesDaCamera Function(OpcoesDaCamera) f) =>
        c.atualizarOpcoesDaCamera(gravada.id, f);
    final kf = losangoDasMarcas(
      marcasUs: marcasDaTrilha(marcada),
      camada: gravada,
      t: t,
      playback: EscopoDoEditor.of(context).playback,
      aoAlternar: () => mudar((x) {
        final tr = trilhaDe(x);
        return poe(
          x,
          tr.hasKeyframeAt(local)
              ? tr.withoutKeyframe(local)
              : tr.withKeyframe(local, tr.valueAt(local)),
        );
      }),
    );
    return AureaPropertyRow(
      rotulo: rotulo,
      chave: chave,
      valor: vista.valueAt(local).clamp(min, max).toDouble(),
      min: min,
      max: max,
      casas: 0,
      unidade: 'px',
      sensibilidade: sensibilidade,
      keyframe: kf.estado,
      aoAnterior: kf.anterior,
      aoProximo: kf.proximo,
      aoComecarGesto: () {
        _avisou = false;
        c.beginGesture();
      },
      aoTerminarGesto: c.endGesture,
      aoMudar: (v) {
        Interacao.marcar();
        // RELIDA A CADA PASSO: a trilha do build ja ficou velha no segundo
        // pixel do arrasto.
        final atual = ref.read(editorControllerProvider).layerById(gravada.id);
        if (atual is! CameraLayer) return;
        final tr = trilhaDe(atual.opcoes);
        if (!tr.aceitaEdicaoEm(local)) {
          if (_avisou) return;
          _avisou = true;
          AureaSnack.show(
            context,
            'Esta opção tem keyframes: toque no losango para marcar este '
            'instante',
          );
          return;
        }
        mudar((x) => poe(x, trilhaDe(x).edited(local, v)));
      },
    );
  }

  List<Widget> _foco(
    EditorController c,
    CameraLayer visivel,
    CameraLayer gravada,
    Duration t,
  ) {
    final o = visivel.opcoes;
    return [
      linhaDeInterruptor(
        rotulo: 'Desfoque de foco',
        chave: 'camera-foco',
        valor: o.focoLigado,
        aoMudar: (v) => c.atualizarOpcoesDaCamera(
          gravada.id,
          (x) => x.copyWith(focoLigado: v),
        ),
      ),
      if (o.focoLigado) ...[
        _opcao(
          c,
          visivel: visivel,
          gravada: gravada,
          t: t,
          rotulo: 'Distância do foco',
          chave: 'camera-foco-distancia',
          trilhaDe: (x) => x.distanciaDoFoco,
          poe: (x, a) => x.copyWith(distanciaDoFoco: a),
          min: 1,
          max: 20000,
          sensibilidade: 6,
        ),
        _opcao(
          c,
          visivel: visivel,
          gravada: gravada,
          t: t,
          rotulo: 'Intensidade',
          chave: 'camera-foco-intensidade',
          trilhaDe: (x) => x.intensidadeDoFoco,
          poe: (x, a) => x.copyWith(intensidadeDoFoco: a),
          min: 0,
          max: 100,
        ),
        _opcao(
          c,
          visivel: visivel,
          gravada: gravada,
          t: t,
          rotulo: 'Profundidade de campo',
          chave: 'camera-foco-profundidade',
          trilhaDe: (x) => x.profundidadeDeCampo,
          poe: (x, a) => x.copyWith(profundidadeDeCampo: a),
          min: 1,
          max: 10000,
          sensibilidade: 4,
        ),
      ] else
        const AureaAvisoDoPainel(
          texto:
              'Ligado, o plano da composição fica a 1200 do olho da '
              'câmera: camadas 3D mais longe ou mais perto desfocam.',
        ),
    ];
  }

  List<Widget> _neblina(
    EditorController c,
    CameraLayer visivel,
    CameraLayer gravada,
    Duration t,
  ) {
    final o = visivel.opcoes;
    return [
      linhaDeInterruptor(
        rotulo: 'Neblina',
        chave: 'camera-neblina',
        valor: o.neblinaLigada,
        aoMudar: (v) => c.atualizarOpcoesDaCamera(
          gravada.id,
          (x) => x.copyWith(neblinaLigada: v),
        ),
      ),
      if (o.neblinaLigada) ...[
        linhaDeCor(
          context,
          rotulo: 'Cor da neblina',
          chave: 'camera-neblina-cor',
          cor: o.corDaNeblina,
          aoMudar: (cor) => c.atualizarOpcoesDaCamera(
            gravada.id,
            (x) => x.copyWith(corDaNeblina: cor),
          ),
        ),
        _opcao(
          c,
          visivel: visivel,
          gravada: gravada,
          t: t,
          rotulo: 'Começa em',
          chave: 'camera-neblina-perto',
          trilhaDe: (x) => x.neblinaPerto,
          poe: (x, a) => x.copyWith(neblinaPerto: a),
          min: 0,
          max: 50000,
          sensibilidade: 10,
        ),
        _opcao(
          c,
          visivel: visivel,
          gravada: gravada,
          t: t,
          rotulo: 'Cobre tudo em',
          chave: 'camera-neblina-longe',
          trilhaDe: (x) => x.neblinaLonge,
          poe: (x, a) => x.copyWith(neblinaLonge: a),
          min: 0,
          max: 50000,
          sensibilidade: 10,
        ),
      ],
    ];
  }
}
