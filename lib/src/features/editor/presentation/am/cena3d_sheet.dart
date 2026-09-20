import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/l10n/app_language.dart';
import '../../application/editor_controller.dart';
import '../../domain/element3d.dart';
import '../../domain/layer.dart';
import '../../domain/scene3d.dart';
import '../context/parameter_row.dart';
import 'am_colors.dart';

/// O EDITOR DA CENA 3D — o que se configura DEPOIS de importar um modelo.
///
/// ============================ POR QUE EXISTE ==========================
///
/// O editor de cena antigo saiu junto com o motor antigo, e o novo motor
/// ficou sem porta: importar um modelo dava um objeto que so aceitava mover,
/// girar e escalar. O ambiente que o metal reflete, a luz de preenchimento e
/// o material de cada objeto existiam no modelo de dados e nao tinham onde
/// ser mexidos.
///
/// A FOLHA OCUPA A METADE DE BAIXO, e nao a tela: o palco continua a vista
/// por cima, e cada toque aqui aparece na cena de verdade — nao ha previa
/// separada para divergir do que vai para o video.
///
/// OS CONTROLES SAO OS DA CASA (`ParameterRow`, fichas em chips), os mesmos
/// da folha do Texto 3D e das fichas do editor.
Future<void> showCena3DSheet(
  BuildContext context,
  WidgetRef ref, {
  required String sceneId,
}) async {
  await showCupertinoModalPopup<void>(
    context: context,
    barrierColor: const Color(0x33000000),
    builder: (_) => _Cena3DSheet(sceneId: sceneId),
  );
}

class _Cena3DSheet extends ConsumerStatefulWidget {
  const _Cena3DSheet({required this.sceneId});
  final String sceneId;

  @override
  ConsumerState<_Cena3DSheet> createState() => _Cena3DSheetState();
}

class _Cena3DSheetState extends ConsumerState<_Cena3DSheet> {
  String? _noEscolhido;

  EditorController get _c => ref.read(editorControllerProvider.notifier);

  void _cena(Scene3D Function(Scene3D) f) => _c.updateScene3D(widget.sceneId, f);

  void _no(String id, SceneNode Function(SceneNode) f) => _cena(
    (cena) => cena.copyWith(
      nodes: [for (final n in cena.nodes) n.id == id ? f(n) : n],
    ),
  );

  @override
  Widget build(BuildContext context) {
    final projeto = ref.watch(editorControllerProvider);
    final camada = projeto.layerById(widget.sceneId);
    if (camada is! Scene3DLayer) return const SizedBox.shrink();
    final cena = camada.scene;
    // OS OBJETOS QUE SE CONFIGURAM: o que tem geometria. Nulo e pai, e o
    // texto 3D tem a folha dele (e la que se troca o metal do texto).
    final objetos = [
      for (final n in cena.nodes)
        if (!n.isNull && n.texto3d == null) n,
    ];
    final escolhido = objetos.where((n) => n.id == _noEscolhido).firstOrNull ??
        objetos.firstOrNull;

    return Container(
      constraints: BoxConstraints(
        maxHeight: MediaQuery.sizeOf(context).height * 0.52,
      ),
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
            Row(
              children: [
                const Expanded(
                  child: AppText(
                    'Cena 3D',
                    style: TextStyle(
                      color: AmColors.text,
                      fontSize: 16,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                ),
                CupertinoButton(
                  key: const ValueKey('cena3d-fechar'),
                  padding: EdgeInsets.zero,
                  minimumSize: const Size(32, 32),
                  onPressed: () => Navigator.of(context).maybePop(),
                  child: const Icon(
                    CupertinoIcons.xmark_circle_fill,
                    color: AmColors.muted,
                    size: 24,
                  ),
                ),
              ],
            ),
            Flexible(
              child: SingleChildScrollView(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    _secao('Ambiente'),
                    _chips<EnvironmentKind>(
                      EnvironmentKind.values,
                      environmentLabel,
                      cena.environment,
                      (k) => _cena((s) => s.copyWith(environment: k)),
                      chave: (k) => ValueKey('cena3d-ambiente-${k.name}'),
                    ),
                    _linha(
                      'Reflexo do ambiente',
                      cena.envReflect * 100,
                      0,
                      100,
                      (v) => _cena((s) => s.copyWith(envReflect: v / 100)),
                      unidade: '%',
                    ),
                    _linha(
                      'Luz ambiente',
                      cena.ambient * 100,
                      0,
                      100,
                      (v) => _cena((s) => s.copyWith(ambient: v / 100)),
                      unidade: '%',
                    ),
                    if (objetos.isNotEmpty) ...[
                      _secao('Objeto'),
                      if (objetos.length > 1)
                        _chips<SceneNode>(
                          objetos,
                          (n) => n.name,
                          escolhido,
                          (n) => setState(() => _noEscolhido = n.id),
                          chave: (n) => ValueKey('cena3d-objeto-${n.id}'),
                        ),
                      if (escolhido != null) ..._doObjeto(escolhido),
                    ],
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  List<Widget> _doObjeto(SceneNode n) {
    final m = n.material;
    final temMateriaisDoArquivo = n.modelAsset != null;
    final proprio = !temMateriaisDoArquivo || !n.useModelMaterials;
    return [
      _linha(
        'Tamanho',
        n.size,
        10,
        1200,
        (v) => _no(n.id, (x) => x.copyWith(size: v)),
      ),
      if (temMateriaisDoArquivo)
        _chips<bool>(
          const [true, false],
          (v) => v ? 'Material do arquivo' : 'Material proprio',
          n.useModelMaterials,
          (v) => _no(n.id, (x) => x.copyWith(useModelMaterials: v)),
          chave: (v) => ValueKey('cena3d-material-${v ? 'arquivo' : 'proprio'}'),
        ),
      if (proprio) ...[
        _secao('Material'),
        _chips<MaterialPreset3D>(
          MaterialPreset3D.values,
          materialPresetLabel,
          null,
          (p) => _no(
            n.id,
            (x) => x.copyWith(
              material: materialFromPreset(p),
              useModelMaterials: false,
            ),
          ),
          chave: (p) => ValueKey('cena3d-preset-${p.name}'),
        ),
        _linha(
          'Metal',
          m.metallic * 100,
          0,
          100,
          (v) => _no(
            n.id,
            (x) => x.copyWith(material: x.material.copyWith(metallic: v / 100)),
          ),
          unidade: '%',
        ),
        _linha(
          'Rugosidade',
          m.roughness * 100,
          0,
          100,
          (v) => _no(
            n.id,
            (x) =>
                x.copyWith(material: x.material.copyWith(roughness: v / 100)),
          ),
          unidade: '%',
        ),
        _linha(
          'Brilho proprio',
          m.emissive * 100,
          0,
          400,
          (v) => _no(
            n.id,
            (x) => x.copyWith(material: x.material.copyWith(emissive: v / 100)),
          ),
          unidade: '%',
        ),
      ],
    ];
  }

  Widget _linha(
    String rotulo,
    double valor,
    double minimo,
    double maximo,
    ValueChanged<double> aoMudar, {
    String unidade = '',
  }) => ParameterRow(
    key: ValueKey('cena3d-linha-$rotulo'),
    label: rotulo,
    value: valor,
    min: minimo,
    max: maximo,
    unit: unidade,
    decimals: 0,
    unitsPerPixel: (maximo - minimo) / 260,
    onChanged: (v) => aoMudar(v.clamp(minimo, maximo).toDouble()),
  );

  Widget _secao(String titulo) => Padding(
    padding: const EdgeInsets.fromLTRB(2, 14, 2, 6),
    child: AppText(
      titulo.toUpperCase(),
      style: const TextStyle(
        color: AmColors.muted,
        fontSize: 11,
        fontWeight: FontWeight.w600,
        letterSpacing: 0.8,
      ),
    ),
  );

  Widget _chips<T>(
    List<T> valores,
    String Function(T) nome,
    T? escolhido,
    ValueChanged<T> aoEscolher, {
    required Key Function(T) chave,
  }) => Padding(
    padding: const EdgeInsets.only(bottom: 8),
    child: SingleChildScrollView(
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
                      color: v == escolhido ? Colors.white : AmColors.text,
                      fontSize: 13,
                    ),
                  ),
                ),
              ),
            ),
        ],
      ),
    ),
  );
}
