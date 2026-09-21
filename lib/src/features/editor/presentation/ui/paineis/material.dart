import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/layer.dart';
import '../../../domain/scene3d.dart';
import 'elemento3d.dart' show showElement3DSheet;
import '../../widgets/gizmo_da_cena_overlay.dart'
    show noDaCenaSelecionadoProvider;
import '../shell/contrato.dart';
import 'comum.dart';
import 'comum_3d.dart';

String rotuloDoTipoDeMaterial(MaterialKind k) => switch (k) {
  MaterialKind.pbr => 'Realista',
  MaterialKind.unlit => 'Sem luz',
  MaterialKind.transparent => 'Transparente',
  MaterialKind.cutout => 'Recorte',
};

/// A PREDEFINICAO QUE O MATERIAL AINDA E: a que bate com cor, metal,
/// rugosidade, brilho, opacidade e tipo. Mexer num numero depois de
/// escolher tira a marca — o material deixou de ser aquele preset.
MaterialPreset3D? predefinicaoDoMaterial(Material3D m) {
  for (final p in MaterialPreset3D.values) {
    final r = materialFromPreset(p);
    if (r.baseColor == m.baseColor &&
        r.metallic == m.metallic &&
        r.roughness == m.roughness &&
        r.emissive == m.emissive &&
        r.opacity == m.opacity &&
        r.kind == m.kind) {
      return p;
    }
  }
  return null;
}

/// MATERIAL — o material do objeto 3D escolhido e, numa camada de Texto
/// 3D, o metal da letra.
///
///  * Texto 3D: predefinicao, cor base, metal, rugosidade e brilho proprio
///    (as mesmas linhas da aba Material do painel Texto 3D);
///  * cena/modelo: origem (do arquivo ou proprio), predefinicao, cor,
///    metal, rugosidade, brilho, opacidade, tipo, corte e face dupla, do
///    objeto que o gizmo esta segurando;
///  * elemento 3D: a ficha do elemento, que ja existe.
class PainelMaterial extends ConsumerWidget {
  const PainelMaterial({super.key, required this.layerId});

  final String layerId;

  static const _titulo = 'Material';

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, layerId);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final chave = 'painel-${PainelId.material.name}';
    if (camada is Element3DLayer) {
      return PainelDePortas(
        titulo: _titulo,
        chave: chave,
        portas: [
          LinhaDePorta(
            rotulo: 'Cor e material do elemento',
            icone: CupertinoIcons.circle_grid_hex,
            aoTocar: () {
              escopo.playback.pause();
              showElement3DSheet(context, ref, layerId);
            },
          ),
        ],
      );
    }
    if (camada is! Scene3DLayer) {
      return PainelDePortas(
        titulo: _titulo,
        chave: chave,
        aviso: 'Esta camada não é 3D.',
        portas: const [],
      );
    }
    return AureaPanel(
      titulo: _titulo,
      chave: chave,
      aoFechar: escopo.fecharPainel,
      corpo: CorpoDoMaterial3D(layerId: layerId),
    );
  }
}

/// O CORPO DO MATERIAL, usado pelo painel Material e pela aba Material do
/// inspector da cena. Tem estado so por causa da [FilaDoTexto3D]: o metal
/// da letra refaz a malha.
class CorpoDoMaterial3D extends ConsumerStatefulWidget {
  const CorpoDoMaterial3D({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<CorpoDoMaterial3D> createState() => _CorpoDoMaterial3DState();
}

class _CorpoDoMaterial3DState extends ConsumerState<CorpoDoMaterial3D> {
  late final FilaDoTexto3D _fila;

  @override
  void initState() {
    super.initState();
    _fila = FilaDoTexto3D(
      controlador: ref.read(editorControllerProvider.notifier),
      lerProjeto: () => ref.read(editorControllerProvider),
      aoMudar: () {
        if (mounted) setState(() {});
      },
    );
  }

  @override
  void dispose() {
    _fila.descartar();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final camada = camadaVisivel(ref, widget.layerId);
    if (camada is! Scene3DLayer) return const SizedBox.shrink();
    final c = ref.read(editorControllerProvider.notifier);

    // TEXTO 3D: o material da letra e do Texto 3D, nao do no.
    final texto = texto3DEmTela(camada, _fila);
    final noTexto = noDoTexto3D(camada);
    if (texto != null && noTexto != null) {
      return ListView(
        key: const ValueKey('material-texto3d'),
        padding: paddingDoPainel,
        children: [
          if (_fila.aviso != null) AureaAvisoDoPainel(texto: _fila.aviso!),
          ...linhasDoMaterialDoTexto3D(
            context,
            c: c,
            camada: camada,
            noId: noTexto,
            params: texto.params,
            estilo: texto.estilo,
            fila: _fila,
          ),
        ],
      );
    }

    final no = noEmFoco(camada.scene, ref.watch(noDaCenaSelecionadoProvider));
    return ListView(
      key: const ValueKey('material-cena'),
      padding: paddingDoPainel,
      children: [
        EscolhaDoObjeto3D(cena: camada.scene, escolhido: no),
        if (no == null)
          const AureaAvisoDoPainel(
            texto: 'Esta cena não tem objeto para pintar.',
          )
        else
          ...linhasDoMaterialDoNo(context, c, camada, no),
      ],
    );
  }
}

/// AS LINHAS DO MATERIAL DE UM OBJETO DA CENA.
List<Widget> linhasDoMaterialDoNo(
  BuildContext context,
  EditorController c,
  Scene3DLayer camada,
  SceneNode n,
) {
  final m = n.material;
  final doArquivo = n.modelAsset != null;
  final proprio = !doArquivo || !n.useModelMaterials;
  void mat(Material3D Function(Material3D) f) =>
      c.setSceneNodeMaterial(camada.id, n.id, f(n.material));
  return [
    if (doArquivo)
      LinhaDeFichas<bool>(
        rotulo: 'Origem',
        chave: 'cena3d-origem',
        valores: const [true, false],
        rotuloDe: (v) => v ? 'Do arquivo' : 'Próprio',
        escolhido: n.useModelMaterials,
        chaveDe: (v) => 'cena3d-material-${v ? 'arquivo' : 'proprio'}',
        aoEscolher: (v) => c.updateSceneNode(
          camada.id,
          n.id,
          (x) => x.copyWith(useModelMaterials: v),
        ),
      ),
    if (!proprio)
      const AureaAvisoDoPainel(
        texto:
            'O objeto usa os materiais que vieram no arquivo. Escolha '
            '"Próprio" para pintar aqui.',
      ),
    if (proprio) ...[
      LinhaDeFichas<MaterialPreset3D>(
        rotulo: 'Predefinição',
        chave: 'cena3d-preset',
        valores: MaterialPreset3D.values,
        rotuloDe: materialPresetLabel,
        escolhido: predefinicaoDoMaterial(m),
        chaveDe: (p) => 'cena3d-preset-${p.name}',
        aoEscolher: (p) => c.applySceneNodeMaterialPreset(camada.id, n.id, p),
      ),
      linhaDeCor(
        context,
        rotulo: 'Cor',
        chave: 'cena3d-cor-base',
        cor: m.baseColor,
        aoMudar: (cor) => mat((x) => x.copyWith(baseColor: cor)),
      ),
      linhaSemLosango(
        c,
        rotulo: 'Metal',
        chave: 'cena3d-metal',
        valor: m.metallic * 100,
        min: 0,
        max: 100,
        unidade: '%',
        aoMudar: (v) => mat((x) => x.copyWith(metallic: v / 100)),
      ),
      linhaSemLosango(
        c,
        rotulo: 'Rugosidade',
        chave: 'cena3d-rugosidade',
        valor: m.roughness * 100,
        min: 0,
        max: 100,
        unidade: '%',
        aoMudar: (v) => mat((x) => x.copyWith(roughness: v / 100)),
      ),
      linhaSemLosango(
        c,
        rotulo: 'Brilho próprio',
        chave: 'cena3d-brilho',
        valor: m.emissive * 100,
        min: 0,
        max: 400,
        unidade: '%',
        aoMudar: (v) => mat((x) => x.copyWith(emissive: v / 100)),
      ),
      linhaDeCor(
        context,
        rotulo: 'Cor do brilho',
        chave: 'cena3d-cor-emissiva',
        cor: m.emissiveColor ?? m.baseColor,
        aoMudar: (cor) => mat((x) => x.copyWith(emissiveColor: cor)),
      ),
      linhaSemLosango(
        c,
        rotulo: 'Opacidade',
        chave: 'cena3d-opacidade',
        valor: m.opacity * 100,
        min: 0,
        max: 100,
        unidade: '%',
        aoMudar: (v) => mat((x) => x.copyWith(opacity: v / 100)),
      ),
      LinhaDeFichas<MaterialKind>(
        rotulo: 'Tipo',
        chave: 'cena3d-tipo',
        valores: MaterialKind.values,
        rotuloDe: rotuloDoTipoDeMaterial,
        escolhido: m.kind,
        chaveDe: (k) => 'cena3d-tipo-${k.name}',
        aoEscolher: (k) => mat((x) => x.copyWith(kind: k)),
      ),
      if (m.kind == MaterialKind.cutout)
        linhaSemLosango(
          c,
          rotulo: 'Corte do alfa',
          chave: 'cena3d-corte',
          valor: m.alphaCutoff * 100,
          min: 0,
          max: 100,
          unidade: '%',
          aoMudar: (v) => mat((x) => x.copyWith(alphaCutoff: v / 100)),
        ),
      linhaDeInterruptor(
        rotulo: 'Face dupla',
        chave: 'cena3d-face-dupla',
        valor: m.doubleSided,
        aoMudar: (v) => mat((x) => x.copyWith(doubleSided: v)),
      ),
    ],
  ];
}
