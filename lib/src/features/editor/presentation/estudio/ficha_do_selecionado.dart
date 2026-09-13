import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/am_colors.dart';
import '../../application/editor_controller.dart';
import '../../domain/camera3d.dart';
import '../../domain/element3d.dart';
import '../../domain/estudio_ux.dart';
import '../../domain/layer.dart';
import '../../domain/scene3d.dart';
import '../widgets/editor_de_curva.dart';
import '../widgets/linha_de_parametro.dart';
import 'estado_do_estudio.dart';
import 'folhas_do_estudio.dart';
import 'folha_de_propriedades.dart';

export 'folha_de_propriedades.dart';

/// AS MARCAS DO QUE ESTA SELECIONADO — a regua da faixa de tempo.
///
/// Sem isto a faixa seria uma linha lisa e ninguem saberia onde as
/// marcas cairam.
List<Duration> marcasDaSelecao(WidgetRef ref, Scene3DLayer camada) {
  final c = ref.read(editorControllerProvider.notifier);
  final noId = ref.watch(noSelecionadoProvider);
  if (noId != null) {
    final n = camada.scene.nodeById(noId);
    if (n == null) return const [];
    final todas = <Duration>{};
    for (final p in PropDoNo.values) {
      todas.addAll(c.sceneNodeKeyframeTimes(n, p));
    }
    return todas.toList()..sort();
  }
  final luzId = ref.watch(luzSelecionadaProvider);
  if (luzId != null) {
    final l = camada.scene.lights.where((x) => x.id == luzId).firstOrNull;
    if (l == null) return const [];
    return c.sceneLightKeyframeTimes(l, PropDaLuz.intensidade);
  }
  final camId = ref.watch(cameraSelecionadaProvider);
  if (camId != null) {
    final cam = camada.allCameras.where((x) => x.id == camId).firstOrNull;
    if (cam == null) return const [];
    final todas = <Duration>{};
    for (final p in PropDaCamera.values) {
      todas.addAll(c.sceneCameraKeyframeTimes(cam, p));
    }
    return todas.toList()..sort();
  }
  return const [];
}

/// A FAIXA CONTEXTUAL: so existe quando ha selecao.
///
/// E o segundo nivel do desenho. Enquanto nada esta escolhido ela nao
/// ocupa um pixel — porque um botao "Mover" sem objeto para mover e uma
/// pergunta sem resposta.
class BarraDeContexto extends ConsumerWidget {
  const BarraDeContexto({
    super.key,
    required this.layerId,
    required this.camada,
    required this.tempo,
    required this.aoFocar,
  });

  final String layerId;
  final Scene3DLayer camada;
  final Duration tempo;
  final VoidCallback aoFocar;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final alvo = _alvoAtual(ref, camada);
    if (alvo == null) return const SizedBox.shrink();
    final extras = ref.watch(selecaoDaCenaProvider).length;

    return Container(
      height: 48,
      decoration: const BoxDecoration(
        color: AmColors.panelHigh,
        border: Border(top: BorderSide(color: AmColors.hairline)),
      ),
      // `SingleChildScrollView` + `Row`, e NAO `ListView` horizontal: a
      // lista preguicosa nao constroi o que esta fora da tela, e nos
      // testes as chaves do fim da barra simplesmente nao existiriam.
      child: SingleChildScrollView(
        scrollDirection: Axis.horizontal,
        padding: const EdgeInsets.symmetric(horizontal: 10),
        child: Row(
          children: [
            Icon(alvo.icone, size: 16, color: AmColors.accent),
            const SizedBox(width: 6),
            ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 130),
              child: AppText(
                extras > 0 ? '${alvo.nome} +$extras' : alvo.nome,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(
                  fontSize: 12.5,
                  fontWeight: FontWeight.w700,
                  color: AmColors.text,
                ),
              ),
            ),
            const SizedBox(width: 10),
            // AS ACOES MAIS FACEIS DE ACHAR quando algo esta escolhido.
            // Sao estas seis, nesta ordem, e nao trinta botoes na vista.
            if (alvo.tipo == TipoDeItem.no) ...[
              for (final f in [
                FerramentaDoEstudio.mover,
                FerramentaDoEstudio.girar,
                FerramentaDoEstudio.escalar,
              ])
                _Acao(
                  rotulo: ferramentaLabel(f),
                  icone: switch (f) {
                    FerramentaDoEstudio.mover => Icons.open_with_rounded,
                    FerramentaDoEstudio.girar => Icons.rotate_right_rounded,
                    FerramentaDoEstudio.escalar => Icons.aspect_ratio_rounded,
                    FerramentaDoEstudio.selecionar => Icons.touch_app_outlined,
                  },
                  escolhida: ref.watch(ferramentaProvider) == f,
                  aoTocar: () =>
                      ref.read(ferramentaProvider.notifier).state = f,
                ),
            ],
            _Acao(
              rotulo: 'Focar',
              icone: Icons.center_focus_strong_rounded,
              aoTocar: aoFocar,
            ),
            _Acao(
              rotulo: 'Animar',
              icone: Icons.animation_rounded,
              aoTocar: () => abrirFichaDoSelecionado(
                context,
                ref,
                layerId: layerId,
                tempo: tempo,
                aba: AbaDaFicha.animar,
              ),
            ),
            _Acao(
              rotulo: 'Mais',
              icone: Icons.tune_rounded,
              aoTocar: () => abrirFichaDoSelecionado(
                context,
                ref,
                layerId: layerId,
                tempo: tempo,
              ),
            ),
          ],
        ),
      ),
    );
  }
}

typedef _Alvo = ({String id, String nome, TipoDeItem tipo, IconData icone});

_Alvo? _alvoAtual(WidgetRef ref, Scene3DLayer camada) {
  final noId = ref.watch(noSelecionadoProvider);
  if (noId != null) {
    final n = camada.scene.nodeById(noId);
    if (n != null) {
      return (
        id: n.id,
        nome: n.name,
        tipo: TipoDeItem.no,
        icone: n.isNull
            ? Icons.control_camera_rounded
            : Icons.view_in_ar_rounded,
      );
    }
  }
  final luzId = ref.watch(luzSelecionadaProvider);
  if (luzId != null) {
    final l = camada.scene.lights.where((x) => x.id == luzId).firstOrNull;
    if (l != null) {
      return (
        id: l.id,
        nome: luzLabel(l.kind),
        tipo: TipoDeItem.luz,
        icone: Icons.light_mode_rounded,
      );
    }
  }
  final camId = ref.watch(cameraSelecionadaProvider);
  if (camId != null) {
    final c = camada.allCameras.where((x) => x.id == camId).firstOrNull;
    if (c != null) {
      return (
        id: c.id,
        nome: c.name,
        tipo: TipoDeItem.camera,
        icone: Icons.videocam_rounded,
      );
    }
  }
  return null;
}

class _Acao extends StatelessWidget {
  const _Acao({
    required this.rotulo,
    required this.icone,
    required this.aoTocar,
    this.escolhida = false,
  });

  final String rotulo;
  final IconData icone;
  final VoidCallback aoTocar;
  final bool escolhida;

  @override
  Widget build(BuildContext context) => Semantics(
    container: true,
    excludeSemantics: true,
    button: true,
    selected: escolhida,
    label: rotulo,
    child: GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: aoTocar,
      child: Container(
        height: 34,
        margin: const EdgeInsets.only(right: 6),
        padding: const EdgeInsets.symmetric(horizontal: 10),
        decoration: BoxDecoration(
          color: escolhida ? AmColors.accentDim : AmColors.chip,
          borderRadius: BorderRadius.circular(9),
        ),
        child: Row(
          children: [
            Icon(
              icone,
              size: 15,
              color: escolhida ? AmColors.accent : AmColors.text,
            ),
            const SizedBox(width: 5),
            AppText(
              rotulo,
              style: TextStyle(
                fontSize: 11.5,
                fontWeight: FontWeight.w600,
                color: escolhida ? AmColors.accent : AmColors.text,
              ),
            ),
          ],
        ),
      ),
    ),
  );
}

/// As abas da ficha. `mais` e a ultima de proposito: o pedido diz que
/// tudo que for menos usado pode ficar num "Mais", e que nada
/// importante pode ficar atras de quatro toques.
enum AbaDaFicha { transformar, material, animar, mais }

String abaLabel(AbaDaFicha a) => switch (a) {
  AbaDaFicha.transformar => 'Transformar',
  AbaDaFicha.material => 'Material',
  AbaDaFicha.animar => 'Animar',
  AbaDaFicha.mais => 'Mais',
};

/// A FICHA DO QUE ESTA SELECIONADO (Tela 4: Propriedades / Inspector).
Future<void> abrirFichaDoSelecionado(
  BuildContext context,
  WidgetRef ref, {
  required String layerId,
  required Duration tempo,
  AbaDaFicha aba = AbaDaFicha.transformar,
}) => abrirFolhaDePropriedadesNova(
  context,
  ref,
  layerId: layerId,
  tempo: tempo,
  aba: aba,
);

class FichaDoSelecionado extends ConsumerStatefulWidget {
  const FichaDoSelecionado({
    super.key,
    required this.layerId,
    required this.tempo,
    required this.abaInicial,
  });

  final String layerId;
  final Duration tempo;
  final AbaDaFicha abaInicial;

  @override
  ConsumerState<FichaDoSelecionado> createState() => _FichaState();
}

class _FichaState extends ConsumerState<FichaDoSelecionado> {
  late AbaDaFicha _aba = widget.abaInicial;
  bool _avancado = false;

  @override
  Widget build(BuildContext context) {
    final projeto = ref.watch(projetoVisivelProvider);
    final bruta = projeto.layerById(widget.layerId);
    if (bruta is! Scene3DLayer) return const SizedBox.shrink();
    final camada = bruta;
    final local = camada.localTime(widget.tempo);
    final alvo = _alvoAtual(ref, camada);

    if (alvo == null) {
      // NADA SELECIONADO: o pedido diz o que mostrar aqui — os caminhos
      // de criar, e nao uma ficha vazia.
      return _NadaSelecionado(layerId: widget.layerId, tempo: widget.tempo);
    }

    final abas = switch (alvo.tipo) {
      TipoDeItem.no => AbaDaFicha.values,
      TipoDeItem.luz => [AbaDaFicha.transformar, AbaDaFicha.animar],
      TipoDeItem.camera => [
        AbaDaFicha.transformar,
        AbaDaFicha.animar,
        AbaDaFicha.mais,
      ],
    };
    final aba = abas.contains(_aba) ? _aba : abas.first;

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      mainAxisSize: MainAxisSize.min,
      children: [
        Row(
          children: [
            Icon(alvo.icone, size: 17, color: AmColors.accent),
            const SizedBox(width: 7),
            Expanded(
              child: AppText(alvo.nome,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(
                  fontSize: 14,
                  fontWeight: FontWeight.w700,
                  color: AmColors.text,
                ),
              ),
            ),
          ],
        ),
        const SizedBox(height: 10),
        Wrap(
          spacing: 0,
          runSpacing: 6,
          children: [
            for (final a in abas)
              Padding(
                padding: const EdgeInsets.only(right: 6),
                child: ChipDaFolha(
                  rotulo: abaLabel(a),
                  escolhida: a == aba,
                  aoTocar: () => setState(() => _aba = a),
                ),
              ),
          ],
        ),
        const SizedBox(height: 6),
        Flexible(
          child: SingleChildScrollView(
            child: switch ((alvo.tipo, aba)) {
              (TipoDeItem.no, AbaDaFicha.transformar) => _TransformarNo(
                layerId: widget.layerId,
                camada: camada,
                nodeId: alvo.id,
                tempo: widget.tempo,
                local: local,
              ),
              (TipoDeItem.no, AbaDaFicha.material) => _MaterialDoNo(
                layerId: widget.layerId,
                camada: camada,
                nodeId: alvo.id,
                avancado: _avancado,
                aoAvancar: () => setState(() => _avancado = !_avancado),
              ),
              (TipoDeItem.no, AbaDaFicha.animar) => _AnimarNo(
                layerId: widget.layerId,
                camada: camada,
                nodeId: alvo.id,
                tempo: widget.tempo,
                local: local,
              ),
              (TipoDeItem.no, AbaDaFicha.mais) => _MaisDoNo(
                layerId: widget.layerId,
                camada: camada,
                nodeId: alvo.id,
              ),
              (TipoDeItem.luz, _) => _FichaDaLuz(
                layerId: widget.layerId,
                camada: camada,
                lightId: alvo.id,
                tempo: widget.tempo,
                local: local,
                avancado: _avancado,
                aoAvancar: () => setState(() => _avancado = !_avancado),
              ),
              (TipoDeItem.camera, _) => _FichaDaCamera(
                layerId: widget.layerId,
                camada: camada,
                cameraId: alvo.id,
                tempo: widget.tempo,
                local: local,
                aba: aba,
              ),
            },
          ),
        ),
      ],
    );
  }
}

class _NadaSelecionado extends ConsumerWidget {
  const _NadaSelecionado({required this.layerId, required this.tempo});

  final String layerId;
  final Duration tempo;

  @override
  Widget build(BuildContext context, WidgetRef ref) => Column(
    crossAxisAlignment: CrossAxisAlignment.stretch,
    mainAxisSize: MainAxisSize.min,
    children: [
      const TituloDaFolha('Nada selecionado'),
      const Padding(
        padding: EdgeInsets.only(bottom: 8),
        child: AppText('Toque num objeto da vista para escolher, ou crie um.',
          style: TextStyle(fontSize: 11.5, color: AmColors.muted),
        ),
      ),
      AcaoDaFolha(
        icone: Icons.add_box_rounded,
        rotulo: 'Adicionar',
        aoTocar: () {
          Navigator.of(context).pop();
          abrirFolhaDeAdicionar(
            context,
            ref,
            layerId: layerId,
            tempo: tempo,
            navegacao: null,
            aoAvisar: (t) =>
                ref.read(recadoDoEstudioProvider.notifier).state = t,
          );
        },
      ),
      AcaoDaFolha(
        icone: Icons.public_rounded,
        rotulo: 'Mundo e ambiente',
        aoTocar: () {
          Navigator.of(context).pop();
          abrirFolhaDoMundo(context, ref, layerId: layerId);
        },
      ),
      AcaoDaFolha(
        icone: Icons.hd_rounded,
        rotulo: 'Render',
        aoTocar: () {
          Navigator.of(context).pop();
          abrirFolhaDeRender(context, ref, layerId: layerId);
        },
      ),
    ],
  );
}

// ------------------------------------------------------------- objeto

class _TransformarNo extends ConsumerWidget {
  const _TransformarNo({
    required this.layerId,
    required this.camada,
    required this.nodeId,
    required this.tempo,
    required this.local,
  });

  final String layerId;
  final Scene3DLayer camada;
  final String nodeId;
  final Duration tempo;
  final Duration local;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final n = camada.scene.nodeById(nodeId);
    if (n == null) return const SizedBox.shrink();
    final c = ref.read(editorControllerProvider.notifier);

    Widget linha(String rotulo, PropDoNo p, double porPixel, int casas) =>
        LinhaDaTrilha(
          rotulo: rotulo,
          nome: '$rotulo de ${n.name}',
          valor: c.sceneNodeValueAt(n, p, local),
          porPixel: porPixel,
          casas: casas,
          temMarcaAqui: c
              .sceneNodeKeyframeTimes(n, p)
              .any((t) => (t - local).abs() < const Duration(milliseconds: 8)),
          anima: c.sceneNodeKeyframeTimes(n, p).isNotEmpty,
          aoComecar: c.beginGesture,
          aoTerminar: c.endGesture,
          aoMudar: (v) => c.editSceneNodeProp(layerId, nodeId, p, tempo, v),
          aoAlternarMarca: () =>
              c.toggleSceneNodeKeyframe(layerId, nodeId, p, tempo),
          aoAbrirCurva: () => abrirCurvaDoNo(ref, layerId, n, p, local),
          aoZerar: () => c.editSceneNodeProp(
            layerId,
            nodeId,
            p,
            tempo,
            p == PropDoNo.escala ? 1 : 0,
          ),
        );

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      mainAxisSize: MainAxisSize.min,
      children: [
        const TituloDaFolha('Posicao'),
        linha('X', PropDoNo.x, 1, 1),
        linha('Y', PropDoNo.y, 1, 1),
        linha('Z', PropDoNo.z, 1, 1),
        const TituloDaFolha('Giro'),
        linha('X', PropDoNo.giroX, .5, 1),
        linha('Y', PropDoNo.giroY, .5, 1),
        linha('Z', PropDoNo.giroZ, .5, 1),
        const TituloDaFolha('Escala'),
        linha('Escala', PropDoNo.escala, .01, 2),
        const TituloDaFolha('Tamanho'),
        LinhaDeParametro(
          rotulo: 'Tamanho',
          nome: 'Tamanho de ${n.name}',
          valor: n.size,
          porPixel: .5,
          casas: 0,
          escolhida: true,
          aoComecar: c.beginGesture,
          aoTerminar: c.endGesture,
          aoMudar: (v) => c.setSceneNodeSize(layerId, nodeId, v),
          aoDigitar: (v) => c.setSceneNodeSize(layerId, nodeId, v),
        ),
      ],
    );
  }
}

class _MaterialDoNo extends ConsumerWidget {
  const _MaterialDoNo({
    required this.layerId,
    required this.camada,
    required this.nodeId,
    required this.avancado,
    required this.aoAvancar,
  });

  final String layerId;
  final Scene3DLayer camada;
  final String nodeId;
  final bool avancado;
  final VoidCallback aoAvancar;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final n = camada.scene.nodeById(nodeId);
    if (n == null) return const SizedBox.shrink();
    final c = ref.read(editorControllerProvider.notifier);
    final m = n.material;

    void mudar(Material3D novo) =>
        c.setSceneNodeMaterial(layerId, nodeId, novo);

    Widget numero(
      String rotulo,
      double valor,
      double porPixel,
      void Function(double) aplicar,
    ) => LinhaDeParametro(
      rotulo: rotulo,
      nome: '$rotulo do material',
      valor: valor,
      porPixel: porPixel,
      escolhida: true,
      aoComecar: c.beginGesture,
      aoTerminar: c.endGesture,
      aoMudar: aplicar,
      aoDigitar: aplicar,
    );

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      mainAxisSize: MainAxisSize.min,
      children: [
        // OS 12 PRONTOS PRIMEIRO. Eram codigo morto — `materialFromPreset`
        // existia, era testado e nao tinha chamador — e sao o caminho
        // mais curto entre "cubo cinza" e "cubo de cromo".
        const TituloDaFolha('Prontos'),
        Wrap(
          spacing: 6,
          runSpacing: 6,
          children: [
            for (final p in MaterialPreset3D.values)
              ChipDaFolha(
                rotulo: materialPresetLabel(p),
                escolhida: false,
                aoTocar: () =>
                    c.applySceneNodeMaterialPreset(layerId, nodeId, p),
              ),
          ],
        ),
        const TituloDaFolha('Cor'),
        AmostraDeCor(
          cor: m.baseColor,
          rotulo: 'Cor do material',
          aoEscolher: (cor) => mudar(m.copyWith(baseColor: cor)),
        ),
        numero(
          'Metal',
          m.metallic,
          .004,
          (v) => mudar(m.copyWith(metallic: v.clamp(0.0, 1.0))),
        ),
        numero(
          'Aspereza',
          m.roughness,
          .004,
          (v) => mudar(m.copyWith(roughness: v.clamp(0.0, 1.0))),
        ),
        InterruptorDaFolha(
          rotulo: 'Avancado',
          icone: Icons.tune_rounded,
          ligado: avancado,
          aoTocar: aoAvancar,
        ),
        if (avancado) ...[
          const TituloDaFolha('Tipo'),
          Wrap(
            spacing: 6,
            runSpacing: 6,
            children: [
              for (final k in MaterialKind.values)
                ChipDaFolha(
                  rotulo: switch (k) {
                    MaterialKind.pbr => 'PBR',
                    MaterialKind.unlit => 'Sem luz',
                    MaterialKind.transparent => 'Transparente',
                    MaterialKind.cutout => 'Recorte',
                  },
                  escolhida: m.kind == k,
                  aoTocar: () => mudar(m.copyWith(kind: k)),
                ),
            ],
          ),
          numero(
            'Emissao',
            m.emissive,
            .004,
            (v) => mudar(m.copyWith(emissive: v.clamp(0.0, 8.0))),
          ),
          numero(
            'Opacidade',
            m.opacity,
            .004,
            (v) => mudar(m.copyWith(opacity: v.clamp(0.0, 1.0))),
          ),
          numero(
            'Reflexo',
            m.reflectivity,
            .004,
            (v) => mudar(m.copyWith(reflectivity: v.clamp(0.0, 1.0))),
          ),
          numero(
            'Oclusao',
            m.occlusionStrength,
            .004,
            (v) => mudar(m.copyWith(occlusionStrength: v.clamp(0.0, 2.0))),
          ),
          if (m.kind == MaterialKind.cutout)
            numero(
              'Corte do alfa',
              m.alphaCutoff,
              .004,
              (v) => mudar(m.copyWith(alphaCutoff: v.clamp(0.0, 1.0))),
            ),
          InterruptorDaFolha(
            rotulo: 'Desenhar os dois lados',
            icone: Icons.flip_rounded,
            ligado: m.doubleSided,
            aoTocar: () => mudar(m.copyWith(doubleSided: !m.doubleSided)),
          ),
        ],
      ],
    );
  }
}

/// ANIMAR: escolha uma propriedade e crave a marca. Sem procurar em
/// painel escondido — e a promessa literal do pedido.
class _AnimarNo extends ConsumerWidget {
  const _AnimarNo({
    required this.layerId,
    required this.camada,
    required this.nodeId,
    required this.tempo,
    required this.local,
  });

  final String layerId;
  final Scene3DLayer camada;
  final String nodeId;
  final Duration tempo;
  final Duration local;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final n = camada.scene.nodeById(nodeId);
    if (n == null) return const SizedBox.shrink();
    final c = ref.read(editorControllerProvider.notifier);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      mainAxisSize: MainAxisSize.min,
      children: [
        const AvisoDaFolha(
          'Mudar o valor NAO cria keyframe. So o losango cria.',
        ),
        for (final p in PropDoNo.values)
          LinhaDeAnimar(
            rotulo: propDoNoLabel(p),
            valor: c.sceneNodeValueAt(n, p, local),
            marcas: c.sceneNodeKeyframeTimes(n, p),
            local: local,
            aoAlternarMarca: () =>
                c.toggleSceneNodeKeyframe(layerId, nodeId, p, tempo),
            aoAbrirGrafico: () => abrirCurvaDoNo(ref, layerId, n, p, local),
          ),
      ],
    );
  }
}

class _MaisDoNo extends ConsumerWidget {
  const _MaisDoNo({
    required this.layerId,
    required this.camada,
    required this.nodeId,
  });

  final String layerId;
  final Scene3DLayer camada;
  final String nodeId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final n = camada.scene.nodeById(nodeId);
    if (n == null) return const SizedBox.shrink();
    final c = ref.read(editorControllerProvider.notifier);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      mainAxisSize: MainAxisSize.min,
      children: [
        if (!n.isNull && n.mesh == null) ...[
          const TituloDaFolha('Forma'),
          Wrap(
            spacing: 6,
            runSpacing: 6,
            children: [
              for (final k in Element3DKind.values)
                ChipDaFolha(
                  rotulo: element3DLabel(k),
                  escolhida: n.kind == k,
                  aoTocar: () => c.setSceneNodeKind(layerId, nodeId, k),
                ),
            ],
          ),
          const TituloDaFolha('Subdivisoes'),
          PassosDaFolha(
            valor: n.subdivisions,
            maximo: 4,
            rotulo: 'Subdivisoes',
            aoMudar: (v) => c.setSceneNodeSubdivisions(layerId, nodeId, v),
          ),
        ],
        const TituloDaFolha('Etiqueta'),
        AmostraDeCor(
          cor: n.colorTag,
          rotulo: 'Etiqueta de cor',
          aoEscolher: (cor) => c.setSceneNodeColorTag(layerId, nodeId, cor),
        ),
        const TituloDaFolha('Detalhe da malha'),
        Wrap(
          spacing: 6,
          runSpacing: 6,
          children: [
            for (final l in MeshLod3D.values)
              ChipDaFolha(
                rotulo: switch (l) {
                  MeshLod3D.auto => 'Automatico',
                  MeshLod3D.high => 'Alto',
                  MeshLod3D.medium => 'Medio',
                  MeshLod3D.low => 'Baixo',
                },
                escolhida: n.lod == l,
                aoTocar: () => c.setSceneNodeLod(layerId, nodeId, l),
              ),
          ],
        ),
        const TituloDaFolha('Acoes'),
        AcaoDaFolha(
          icone: Icons.copy_all_rounded,
          rotulo: 'Duplicar',
          aoTocar: () {
            final novo = c.duplicateSceneNode(layerId, nodeId);
            if (novo.isNotEmpty) {
              ref.read(noSelecionadoProvider.notifier).state = novo;
            }
            Navigator.of(context).pop();
          },
        ),
        AcaoDaFolha(
          icone: Icons.visibility_off_rounded,
          rotulo: n.visible ? 'Esconder' : 'Mostrar',
          aoTocar: () => c.setSceneNodeVisible(layerId, nodeId, !n.visible),
        ),
        AcaoDaFolha(
          icone: n.locked ? Icons.lock_rounded : Icons.lock_open_rounded,
          rotulo: n.locked ? 'Destravar' : 'Travar',
          aoTocar: () => c.setSceneNodeLocked(layerId, nodeId, !n.locked),
        ),
        AcaoDaFolha(
          icone: Icons.filter_center_focus_rounded,
          rotulo: 'Isolar (esconde o resto)',
          aoTocar: () => c.isolateSceneNode(layerId, nodeId),
        ),
        AcaoDaFolha(
          icone: Icons.delete_outline_rounded,
          rotulo: 'Apagar',
          aoTocar: () {
            c.removeSceneNode(layerId, nodeId);
            ref.read(noSelecionadoProvider.notifier).state = null;
            Navigator.of(context).pop();
          },
        ),
      ],
    );
  }
}

// --------------------------------------------------------------- luz

class _FichaDaLuz extends ConsumerWidget {
  const _FichaDaLuz({
    required this.layerId,
    required this.camada,
    required this.lightId,
    required this.tempo,
    required this.local,
    required this.avancado,
    required this.aoAvancar,
  });

  final String layerId;
  final Scene3DLayer camada;
  final String lightId;
  final Duration tempo;
  final Duration local;
  final bool avancado;
  final VoidCallback aoAvancar;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final l = camada.scene.lights.where((x) => x.id == lightId).firstOrNull;
    if (l == null) return const SizedBox.shrink();
    final c = ref.read(editorControllerProvider.notifier);

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      mainAxisSize: MainAxisSize.min,
      children: [
        const TituloDaFolha('Tipo'),
        Wrap(
          spacing: 6,
          runSpacing: 6,
          children: [
            for (final k in Light3DKind.values)
              ChipDaFolha(
                rotulo: luzLabel(k),
                escolhida: l.kind == k,
                aoTocar: () => c.setSceneLightKind(layerId, lightId, k),
              ),
          ],
        ),
        const TituloDaFolha('Intensidade'),
        LinhaDaTrilha(
          rotulo: 'Intensidade',
          nome: 'Intensidade da luz',
          valor: c.sceneLightValueAt(l, PropDaLuz.intensidade, local),
          porPixel: .01,
          casas: 2,
          temMarcaAqui: c
              .sceneLightKeyframeTimes(l, PropDaLuz.intensidade)
              .any((t) => (t - local).abs() < const Duration(milliseconds: 8)),
          anima: c.sceneLightKeyframeTimes(l, PropDaLuz.intensidade).isNotEmpty,
          aoComecar: c.beginGesture,
          aoTerminar: c.endGesture,
          aoMudar: (v) => c.editSceneLightProp(
            layerId,
            lightId,
            PropDaLuz.intensidade,
            tempo,
            v,
          ),
          aoAlternarMarca: () => c.toggleSceneLightKeyframe(
            layerId,
            lightId,
            PropDaLuz.intensidade,
            tempo,
          ),
          aoAbrirCurva: null,
          aoZerar: () => c.editSceneLightProp(
            layerId,
            lightId,
            PropDaLuz.intensidade,
            tempo,
            1,
          ),
        ),
        const TituloDaFolha('Cor'),
        AmostraDeCor(
          cor: l.color,
          rotulo: 'Cor da luz',
          aoEscolher: (cor) => c.setSceneLightColor(layerId, lightId, cor),
        ),
        InterruptorDaFolha(
          rotulo: 'Avancado',
          icone: Icons.tune_rounded,
          ligado: avancado,
          aoTocar: aoAvancar,
        ),
        if (avancado) ...[
          InterruptorDaFolha(
            rotulo: 'Projeta sombra',
            icone: Icons.wb_shade_rounded,
            ligado: l.castsShadow,
            aoTocar: () =>
                c.setSceneLightShadow(layerId, lightId, !l.castsShadow),
          ),
          LinhaDeParametro(
            rotulo: 'Alcance',
            nome: 'Alcance da luz',
            valor: l.range,
            porPixel: 5,
            casas: 0,
            escolhida: true,
            aoComecar: c.beginGesture,
            aoTerminar: c.endGesture,
            aoMudar: (v) => c.setSceneLightRange(layerId, lightId, v),
            aoDigitar: (v) => c.setSceneLightRange(layerId, lightId, v),
          ),
          if (l.kind == Light3DKind.spot)
            LinhaDeParametro(
              rotulo: 'Cone',
              nome: 'Angulo do cone',
              valor: l.coneDegrees,
              porPixel: .4,
              casas: 0,
              sufixo: '°',
              escolhida: true,
              aoComecar: c.beginGesture,
              aoTerminar: c.endGesture,
              aoMudar: (v) => c.setSceneLightCone(layerId, lightId, v),
              aoDigitar: (v) => c.setSceneLightCone(layerId, lightId, v),
            ),
          LinhaDeParametro(
            rotulo: 'Suavidade',
            nome: 'Suavidade da borda',
            valor: l.softness,
            porPixel: .004,
            casas: 2,
            escolhida: true,
            aoComecar: c.beginGesture,
            aoTerminar: c.endGesture,
            aoMudar: (v) => c.setSceneLightSoftness(layerId, lightId, v),
            aoDigitar: (v) => c.setSceneLightSoftness(layerId, lightId, v),
          ),
          const TituloDaFolha('Direcao'),
          for (final eixo in const ['X', 'Y', 'Z'])
            LinhaDeParametro(
              rotulo: eixo,
              nome: 'Direcao $eixo da luz',
              valor: switch (eixo) {
                'X' => l.direction.x,
                'Y' => l.direction.y,
                _ => l.direction.z,
              },
              porPixel: .004,
              casas: 2,
              escolhida: true,
              aoComecar: c.beginGesture,
              aoTerminar: c.endGesture,
              aoMudar: (v) =>
                  c.setSceneLightDirection(layerId, lightId, switch (eixo) {
                    'X' => Vec3(v, l.direction.y, l.direction.z),
                    'Y' => Vec3(l.direction.x, v, l.direction.z),
                    _ => Vec3(l.direction.x, l.direction.y, v),
                  }),
            ),
          AcaoDaFolha(
            icone: Icons.delete_outline_rounded,
            rotulo: 'Apagar a luz',
            aoTocar: () {
              c.removeSceneLight(layerId, lightId);
              ref.read(luzSelecionadaProvider.notifier).state = null;
              Navigator.of(context).pop();
            },
          ),
        ],
      ],
    );
  }
}

// ------------------------------------------------------------ camera

class _FichaDaCamera extends ConsumerWidget {
  const _FichaDaCamera({
    required this.layerId,
    required this.camada,
    required this.cameraId,
    required this.tempo,
    required this.local,
    required this.aba,
  });

  final String layerId;
  final Scene3DLayer camada;
  final String cameraId;
  final Duration tempo;
  final Duration local;
  final AbaDaFicha aba;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final cam = camada.allCameras.where((x) => x.id == cameraId).firstOrNull;
    if (cam == null) return const SizedBox.shrink();
    final c = ref.read(editorControllerProvider.notifier);

    Widget linha(
      String rotulo,
      PropDaCamera p,
      double porPixel,
      int casas, {
      String sufixo = '',
    }) => LinhaDaTrilha(
      rotulo: rotulo,
      nome: '$rotulo de ${cam.name}',
      valor: c.sceneCameraValueAt(cam, p, local),
      porPixel: porPixel,
      casas: casas,
      sufixo: sufixo,
      temMarcaAqui: c
          .sceneCameraKeyframeTimes(cam, p)
          .any((t) => (t - local).abs() < const Duration(milliseconds: 8)),
      anima: c.sceneCameraKeyframeTimes(cam, p).isNotEmpty,
      aoComecar: c.beginGesture,
      aoTerminar: c.endGesture,
      aoMudar: (v) => c.editSceneCameraProp(layerId, cameraId, p, tempo, v),
      aoAlternarMarca: () =>
          c.toggleSceneCameraKeyframe(layerId, cameraId, p, tempo),
      aoAbrirCurva: () => abrirCurvaDaCamera(ref, layerId, cam, p, local),
      aoZerar: null,
    );

    if (aba == AbaDaFicha.animar) {
      return Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        mainAxisSize: MainAxisSize.min,
        children: [
          const AvisoDaFolha(
            'Mudar o valor NAO cria keyframe. So o losango cria.',
          ),
          for (final p in PropDaCamera.values)
            LinhaDeAnimar(
              rotulo: propDaCameraLabel(p),
              valor: c.sceneCameraValueAt(cam, p, local),
              marcas: c.sceneCameraKeyframeTimes(cam, p),
              local: local,
              aoAlternarMarca: () =>
                  c.toggleSceneCameraKeyframe(layerId, cameraId, p, tempo),
              aoAbrirGrafico: () =>
                  abrirCurvaDaCamera(ref, layerId, cam, p, local),
            ),
        ],
      );
    }

    if (aba == AbaDaFicha.mais) {
      return Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        mainAxisSize: MainAxisSize.min,
        children: [
          const TituloDaFolha('Profundidade de campo'),
          InterruptorDaFolha(
            rotulo: 'Ligar a profundidade de campo',
            icone: Icons.blur_on_rounded,
            ligado: cam.dof.enabled,
            aoTocar: () =>
                c.setSceneCameraDofEnabled(layerId, cameraId, !cam.dof.enabled),
          ),
          if (cam.dof.enabled) ...[
            linha('Foco', PropDaCamera.foco, 2, 0),
            linha('Abertura', PropDaCamera.abertura, .05, 1),
            linha('Desfoque', PropDaCamera.desfoque, .01, 2),
            const TituloDaFolha('Iris'),
            Wrap(
              spacing: 6,
              runSpacing: 6,
              children: [
                for (final f in IrisShape.values)
                  ChipDaFolha(
                    rotulo: irisLabel(f),
                    escolhida: cam.dof.irisShape == f,
                    aoTocar: () => c.setSceneCameraIris(layerId, cameraId, f),
                  ),
              ],
            ),
            linha('Giro da iris', PropDaCamera.giroDaIris, .5, 0, sufixo: '°'),
            linha('Arredondar', PropDaCamera.arredondamentoDaIris, .5, 0),
            linha('Proporcao', PropDaCamera.proporcaoDaIris, .01, 2),
            linha('Franja', PropDaCamera.franja, .01, 2),
            const TituloDaFolha('Realce'),
            linha('Ganho', PropDaCamera.ganhoDoRealce, .02, 2),
            linha('Limiar', PropDaCamera.limiarDoRealce, .004, 2),
            linha('Cor', PropDaCamera.corDoRealce, .01, 2),
          ],
          const TituloDaFolha('Tipo'),
          Wrap(
            spacing: 6,
            runSpacing: 6,
            children: [
              for (final k in CameraKind.values)
                ChipDaFolha(
                  rotulo: k == CameraKind.twoNode
                      ? 'Dois nos (com alvo)'
                      : 'Um no (so orientacao)',
                  escolhida: cam.kind == k,
                  aoTocar: () => c.setSceneCameraKind(layerId, cameraId, k),
                ),
            ],
          ),
          LinhaDeParametro(
            rotulo: 'Filme',
            nome: 'Largura do filme',
            valor: cam.filmWidth,
            porPixel: .1,
            casas: 1,
            sufixo: 'mm',
            escolhida: true,
            aoComecar: c.beginGesture,
            aoTerminar: c.endGesture,
            aoMudar: (v) => c.setSceneCameraFilmWidth(layerId, cameraId, v),
            aoDigitar: (v) => c.setSceneCameraFilmWidth(layerId, cameraId, v),
          ),
          const TituloDaFolha('Olhar para'),
          Wrap(
            spacing: 6,
            runSpacing: 6,
            children: [
              ChipDaFolha(
                rotulo: 'Ninguem',
                escolhida: cam.lookAtNodeId == null,
                aoTocar: () => c.setCameraLookAt(layerId, cameraId, null),
              ),
              for (final n in camada.scene.nodes)
                ChipDaFolha(
                  rotulo: n.name,
                  escolhida: cam.lookAtNodeId == n.id,
                  aoTocar: () => c.setCameraLookAt(layerId, cameraId, n.id),
                ),
            ],
          ),
        ],
      );
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      mainAxisSize: MainAxisSize.min,
      children: [
        const TituloDaFolha('Posicao'),
        linha('X', PropDaCamera.posX, 1, 1),
        linha('Y', PropDaCamera.posY, 1, 1),
        linha('Z', PropDaCamera.posZ, 1, 1),
        if (cam.kind == CameraKind.twoNode) ...[
          const TituloDaFolha('Para onde olha'),
          linha('X', PropDaCamera.alvoX, 1, 1),
          linha('Y', PropDaCamera.alvoY, 1, 1),
          linha('Z', PropDaCamera.alvoZ, 1, 1),
        ] else ...[
          const TituloDaFolha('Giro'),
          linha('X', PropDaCamera.giroX, .5, 1),
          linha('Y', PropDaCamera.giroY, .5, 1),
          linha('Z', PropDaCamera.giroZ, .5, 1),
        ],
        const TituloDaFolha('Lente'),
        linha('Lente', PropDaCamera.lente, .5, 0, sufixo: 'mm'),
        AvisoDaFolha('Angulo de visao: ${cam.fovAt(local).round()}°'),
      ],
    );
  }
}

/// Abre o EDITOR DE CURVA que ja existe, na trilha 3D escolhida.
/// Nao ha um segundo sistema de curvas — o pedido pede exatamente isso.
void abrirCurvaDoNo(
  WidgetRef ref,
  String layerId,
  SceneNode n,
  PropDoNo p,
  Duration local,
) {
  final c = ref.read(editorControllerProvider.notifier);
  final tempos = c.sceneNodeKeyframeTimes(n, p);
  final comeco = _trechoQueComeca(tempos, local);
  if (comeco == null) return;
  ref.read(curvaEmEdicaoProvider.notifier).state = CurvaEmEdicao(
    titulo: '${propDoNoLabel(p)} de ${n.name}',
    atual: c.sceneNodeTrack(n, p).easeAt(comeco),
    aoAplicar: (e) => c.setSceneNodePropEase(layerId, n.id, p, comeco, e),
  );
  _mostrarEditorDeCurva(ref);
}

void abrirCurvaDaCamera(
  WidgetRef ref,
  String layerId,
  Camera3D cam,
  PropDaCamera p,
  Duration local,
) {
  final c = ref.read(editorControllerProvider.notifier);
  final tempos = c.sceneCameraKeyframeTimes(cam, p);
  final comeco = _trechoQueComeca(tempos, local);
  if (comeco == null) return;
  ref.read(curvaEmEdicaoProvider.notifier).state = CurvaEmEdicao(
    titulo: '${propDaCameraLabel(p)} de ${cam.name}',
    atual: c.sceneCameraTrack(cam, p).easeAt(comeco),
    aoAplicar: (e) => c.setSceneCameraPropEase(layerId, cam.id, p, comeco, e),
  );
  _mostrarEditorDeCurva(ref);
}

void _mostrarEditorDeCurva(WidgetRef ref) {
  final context = ref.context;
  final request = ref.read(curvaEmEdicaoProvider);
  showModalBottomSheet<void>(
    context: context,
    isScrollControlled: true,
    backgroundColor: AmColors.panel,
    builder: (sheetContext) => SafeArea(
      top: false,
      child: SizedBox(
        height: (MediaQuery.sizeOf(sheetContext).height * .55).clamp(
          220.0,
          400.0,
        ),
        child: EditorDeCurva(onClose: () => Navigator.of(sheetContext).pop()),
      ),
    ),
  ).whenComplete(() {
    if (context.mounted &&
        identical(ref.read(curvaEmEdicaoProvider), request)) {
      ref.read(curvaEmEdicaoProvider.notifier).state = null;
    }
  });
}

/// A curva pertence ao TRECHO entre duas marcas: sem trecho nao ha o
/// que curvar, e o botao fica apagado em vez de abrir um editor vazio.
Duration? _trechoQueComeca(List<Duration> tempos, Duration local) {
  Duration? antes;
  Duration? depois;
  for (final t in tempos) {
    if (t <= local) antes = t;
    if (t > local && depois == null) depois = t;
  }
  if (antes != null && depois != null) return antes;
  return tempos.length >= 2
      ? (local < tempos.first ? tempos.first : tempos[tempos.length - 2])
      : null;
}
