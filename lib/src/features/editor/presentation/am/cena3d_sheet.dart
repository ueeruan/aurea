import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/l10n/app_language.dart';
import '../../application/editor_controller.dart';
import '../../application/interacao.dart';
import '../../domain/element3d.dart';
import '../../domain/gizmo_da_cena3d.dart';
import '../../domain/layer.dart';
import '../../domain/scene3d.dart';
import '../context/parameter_row.dart';
import '../widgets/gizmo_da_cena_overlay.dart';
import 'am_colors.dart';
import 'color_picker_sheet.dart';

/// O INSPECTOR 3D — o painel de metade de baixo do objeto selecionado.
///
/// ============================ POR QUE EXISTE ==========================
///
/// O controlador ja tinha a API inteira de no, luz e ambiente da cena
/// (`editSceneNodeProp`, `toggleSceneNodeKeyframe`, `setSceneNodeMaterial`,
/// `addSceneLight`, `setSceneEnvironment`...), testada, e sem um unico
/// chamador na interface. A folha antiga mostrava cinco reguas soltas:
/// ambiente, tamanho e tres numeros de material. Transformar um objeto,
/// por keyframe nele, mexer numa luz ou enquadrar a camera nao tinham
/// porta nenhuma.
///
/// A FOLHA OCUPA A METADE DE BAIXO, e nao a tela: o palco continua a
/// vista por cima, e cada toque aqui aparece na cena de verdade — nao ha
/// previa separada para divergir do que vai para o video. O objeto que se
/// edita e o MESMO que o gizmo do palco esta segurando
/// ([noDaCenaSelecionadoProvider]): escolher aqui move o gizmo, e escolher
/// no palco troca o que esta aqui.
///
/// O QUE NAO APARECE: controle que o motor nativo IGNORA. Neblina,
/// panorama, sonda de reflexo, reflexo do piso, tonemap, fundo, grade do
/// chao, `reflectivity`, `normalStrength` e imagem por face entram na
/// chave do quadro mas nunca sao enviados — uma regua que nao muda um
/// pixel e pior do que regua nenhuma, porque faz duvidar do resto.
Future<void> showCena3DSheet(
  BuildContext context,
  WidgetRef ref, {
  required String sceneId,
  Duration? playhead,
}) async {
  await showCupertinoModalPopup<void>(
    context: context,
    barrierColor: const Color(0x33000000),
    builder: (_) => _Cena3DSheet(
      sceneId: sceneId,
      // O CABECOTE: quem abre a folha (o menu da camada) ainda nao passa o
      // relogio, entao vale o que o palco esta desenhando. Sem instante
      // certo, o losango poria keyframe no zero.
      playhead: playhead ?? cabecoteDoPalco.value,
    ),
  );
}

enum _Aba { transformar, material, iluminacao, ambiente }

String _rotuloDaAba(_Aba a) => switch (a) {
  _Aba.transformar => 'Transformar',
  _Aba.material => 'Material',
  _Aba.iluminacao => 'Iluminação',
  _Aba.ambiente => 'Ambiente',
};

String _rotuloDaLuz(Light3DKind k) => switch (k) {
  Light3DKind.directional => 'Direcional',
  Light3DKind.point => 'Ponto',
  Light3DKind.ambient => 'Ambiente',
  Light3DKind.spot => 'Foco',
};

String _rotuloDoMaterial(MaterialKind k) => switch (k) {
  MaterialKind.pbr => 'Realista',
  MaterialKind.unlit => 'Sem luz',
  MaterialKind.transparent => 'Transparente',
  MaterialKind.cutout => 'Recorte',
};

class _Cena3DSheet extends ConsumerStatefulWidget {
  const _Cena3DSheet({required this.sceneId, required this.playhead});
  final String sceneId;
  final Duration playhead;

  @override
  ConsumerState<_Cena3DSheet> createState() => _Cena3DSheetState();
}

class _Cena3DSheetState extends ConsumerState<_Cena3DSheet> {
  _Aba _aba = _Aba.transformar;
  String? _luzEscolhida;

  EditorController get _c => ref.read(editorControllerProvider.notifier);

  Duration get _t => widget.playhead;

  @override
  Widget build(BuildContext context) {
    final projeto = ref.watch(editorControllerProvider);
    final camada = projeto.layerById(widget.sceneId);
    if (camada is! Scene3DLayer) return const SizedBox.shrink();
    final cena = camada.scene;
    final local = camada.localTime(_t);
    final objetos = objetosDaCena(cena);
    final noId = noAtivoDaCena(cena, ref.watch(noDaCenaSelecionadoProvider));
    final escolhido = noId == null ? null : cena.nodeById(noId);

    return Container(
      constraints: BoxConstraints(
        maxHeight: MediaQuery.sizeOf(context).height * 0.56,
      ),
      decoration: BoxDecoration(
        color: AmColors.panel,
        borderRadius: const BorderRadius.vertical(top: Radius.circular(18)),
      ),
      padding: const EdgeInsets.fromLTRB(14, 10, 14, 18),
      child: SafeArea(
        top: false,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Row(
              children: [
                Expanded(
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
                  child: Icon(
                    CupertinoIcons.xmark_circle_fill,
                    color: AmColors.muted,
                    size: 24,
                  ),
                ),
              ],
            ),
            if (objetos.length > 1)
              _chips<SceneNode>(
                objetos,
                (n) => n.name,
                escolhido,
                (n) => ref.read(noDaCenaSelecionadoProvider.notifier).state =
                    n.id,
                chave: (n) => ValueKey('cena3d-objeto-${n.id}'),
              ),
            _chips<_Aba>(
              _Aba.values,
              _rotuloDaAba,
              _aba,
              (a) => setState(() => _aba = a),
              chave: (a) => ValueKey('cena3d-aba-${a.name}'),
            ),
            Flexible(
              child: SingleChildScrollView(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: switch (_aba) {
                    _Aba.transformar => escolhido == null
                        ? [_vazio('Nenhum objeto nesta cena.')]
                        : _transformar(escolhido, local),
                    _Aba.material => escolhido == null
                        ? [_vazio('Nenhum objeto nesta cena.')]
                        : _material(escolhido),
                    _Aba.iluminacao => _iluminacao(cena, local),
                    _Aba.ambiente => _ambiente(cena),
                  },
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  // ------------------------------------------------------- transformar

  List<Widget> _transformar(SceneNode n, Duration local) => [
    _secao('Posição'),
    _doNo(n, PropDoNo.x, 'Posição X', local, -20000, 20000, 0, 4),
    _doNo(n, PropDoNo.y, 'Posição Y', local, -20000, 20000, 0, 4),
    _doNo(n, PropDoNo.z, 'Posição Z', local, -20000, 20000, 0, 4),
    _secao('Giro'),
    _doNo(n, PropDoNo.giroX, 'Giro X', local, -1440, 1440, 1, 0.8, unidade: '°'),
    _doNo(n, PropDoNo.giroY, 'Giro Y', local, -1440, 1440, 1, 0.8, unidade: '°'),
    _doNo(n, PropDoNo.giroZ, 'Giro Z', local, -1440, 1440, 1, 0.8, unidade: '°'),
    _secao('Tamanho'),
    _doNo(n, PropDoNo.escala, 'Escala', local, 0.01, 20, 2, 0.02),
    _linha(
      'Tamanho',
      n.size,
      1,
      2000,
      (v) => _c.setSceneNodeSize(widget.sceneId, n.id, v),
    ),
    Padding(
      padding: const EdgeInsets.only(top: 10),
      child: Row(
        children: [
          _botao(
            'Enquadrar',
            const ValueKey('cena3d-enquadrar'),
            () => _c.frameSceneNode(widget.sceneId, n.id),
          ),
          const SizedBox(width: 8),
          _botao(
            'Apontar a câmera',
            const ValueKey('cena3d-apontar'),
            () => _c.focusCameraOnNode(widget.sceneId, n.id),
          ),
        ],
      ),
    ),
  ];

  /// UMA TRILHA DO OBJETO, com losango. Mexer no valor NAO cria keyframe
  /// (a regra do app inteiro); o losango e que poe e tira.
  Widget _doNo(
    SceneNode n,
    PropDoNo p,
    String rotulo,
    Duration local,
    double minimo,
    double maximo,
    int casas,
    double porPixel, {
    String unidade = '',
  }) {
    final tempos = _c.sceneNodeKeyframeTimes(n, p);
    return ParameterRow(
      key: ValueKey('cena3d-linha-$rotulo'),
      label: rotulo,
      value: _c.sceneNodeValueAt(n, p, local),
      min: minimo,
      max: maximo,
      decimals: casas,
      unit: unidade,
      unitsPerPixel: porPixel,
      keyframe: KeyframeState(
        animated: tempos.isNotEmpty,
        here: tempos.any(
          (t) => (t - local).abs() < const Duration(milliseconds: 8),
        ),
        onToggle: () {
          _c.toggleSceneNodeKeyframe(widget.sceneId, n.id, p, _t);
          setState(() {});
        },
      ),
      onChanged: (v) {
        Interacao.marcar();
        _c.editSceneNodeProp(
          widget.sceneId,
          n.id,
          p,
          _t,
          v.clamp(minimo, maximo).toDouble(),
        );
      },
    );
  }

  // ---------------------------------------------------------- material

  List<Widget> _material(SceneNode n) {
    final m = n.material;
    final temMateriaisDoArquivo = n.modelAsset != null;
    final proprio = !temMateriaisDoArquivo || !n.useModelMaterials;
    void mat(Material3D Function(Material3D) f) =>
        _c.setSceneNodeMaterial(widget.sceneId, n.id, f(n.material));
    return [
      if (temMateriaisDoArquivo)
        _chips<bool>(
          const [true, false],
          (v) => v ? 'Material do arquivo' : 'Material próprio',
          n.useModelMaterials,
          (v) => _c.updateSceneNode(
            widget.sceneId,
            n.id,
            (x) => x.copyWith(useModelMaterials: v),
          ),
          chave: (v) =>
              ValueKey('cena3d-material-${v ? 'arquivo' : 'proprio'}'),
        ),
      if (n.texto3d != null)
        _vazio(
          'O metal e o chanfro das letras ficam na folha do Texto 3D.',
        ),
      if (proprio) ...[
        _secao('Predefinição'),
        _chips<MaterialPreset3D>(
          MaterialPreset3D.values,
          materialPresetLabel,
          null,
          (p) => _c.applySceneNodeMaterialPreset(widget.sceneId, n.id, p),
          chave: (p) => ValueKey('cena3d-preset-${p.name}'),
        ),
        _secao('Material'),
        _cor(
          'Cor',
          m.baseColor,
          (c) => mat((x) => x.copyWith(baseColor: c)),
          const ValueKey('cena3d-cor-base'),
        ),
        _linha(
          'Metal',
          m.metallic * 100,
          0,
          100,
          (v) => mat((x) => x.copyWith(metallic: v / 100)),
          unidade: '%',
        ),
        _linha(
          'Rugosidade',
          m.roughness * 100,
          0,
          100,
          (v) => mat((x) => x.copyWith(roughness: v / 100)),
          unidade: '%',
        ),
        _linha(
          'Brilho próprio',
          m.emissive * 100,
          0,
          400,
          (v) => mat((x) => x.copyWith(emissive: v / 100)),
          unidade: '%',
        ),
        _cor(
          'Cor do brilho',
          m.emissiveColor ?? m.baseColor,
          (c) => mat((x) => x.copyWith(emissiveColor: c)),
          const ValueKey('cena3d-cor-emissiva'),
        ),
        _secao('Tipo'),
        _chips<MaterialKind>(
          MaterialKind.values,
          _rotuloDoMaterial,
          m.kind,
          (k) => mat((x) => x.copyWith(kind: k)),
          chave: (k) => ValueKey('cena3d-tipo-${k.name}'),
        ),
        if (m.kind == MaterialKind.cutout)
          _linha(
            'Corte do alfa',
            m.alphaCutoff * 100,
            0,
            100,
            (v) => mat((x) => x.copyWith(alphaCutoff: v / 100)),
            unidade: '%',
          ),
        _interruptor(
          'Face dupla',
          m.doubleSided,
          (v) => mat((x) => x.copyWith(doubleSided: v)),
          const ValueKey('cena3d-face-dupla'),
        ),
      ],
    ];
  }

  // ------------------------------------------------------- iluminacao

  List<Widget> _iluminacao(Scene3D cena, Duration local) {
    final luzes = cena.lights;
    final luz =
        luzes.where((l) => l.id == _luzEscolhida).firstOrNull ??
        luzes.firstOrNull;
    return [
      Row(
        children: [
          Expanded(
            child: _chips<Light3D>(
              luzes,
              (l) => _rotuloDaLuz(l.kind),
              luz,
              (l) => setState(() => _luzEscolhida = l.id),
              chave: (l) => ValueKey('cena3d-luz-${l.id}'),
            ),
          ),
          _botao(
            '+ Luz',
            const ValueKey('cena3d-add-luz'),
            () => _c.addSceneLight(widget.sceneId, Light3DKind.point),
          ),
        ],
      ),
      if (luz == null)
        _vazio('Sem luzes. O ambiente ainda ilumina a cena.')
      else ...[
        _secao('Luz'),
        _chips<Light3DKind>(
          Light3DKind.values,
          _rotuloDaLuz,
          luz.kind,
          (k) => _c.setSceneLightKind(widget.sceneId, luz.id, k),
          chave: (k) => ValueKey('cena3d-luz-tipo-${k.name}'),
        ),
        _cor(
          'Cor',
          luz.color,
          (c) => _c.setSceneLightColor(widget.sceneId, luz.id, c),
          const ValueKey('cena3d-luz-cor'),
        ),
        _daLuz(luz, local),
        if (luz.kind == Light3DKind.point || luz.kind == Light3DKind.spot)
          _linha(
            'Alcance',
            luz.range,
            1,
            20000,
            (v) => _c.setSceneLightRange(widget.sceneId, luz.id, v),
          ),
        if (luz.kind == Light3DKind.spot)
          _linha(
            'Cone',
            luz.coneDegrees,
            1,
            179,
            (v) => _c.setSceneLightCone(widget.sceneId, luz.id, v),
            unidade: '°',
          ),
        _linha(
          'Suavidade',
          luz.softness * 100,
          0,
          100,
          (v) => _c.setSceneLightSoftness(widget.sceneId, luz.id, v / 100),
          unidade: '%',
        ),
        Padding(
          padding: const EdgeInsets.only(top: 10),
          child: Row(
            children: [
              _botao(
                'Remover luz',
                const ValueKey('cena3d-remove-luz'),
                () => _c.removeSceneLight(widget.sceneId, luz.id),
              ),
            ],
          ),
        ),
      ],
    ];
  }

  /// A INTENSIDADE E A UNICA TRILHA ANIMAVEL DE UMA LUZ — e agora tem
  /// losango, como o resto do app.
  Widget _daLuz(Light3D luz, Duration local) {
    final tempos = _c.sceneLightKeyframeTimes(luz, PropDaLuz.intensidade);
    return ParameterRow(
      key: const ValueKey('cena3d-linha-Intensidade'),
      label: 'Intensidade',
      value: _c.sceneLightValueAt(luz, PropDaLuz.intensidade, local) * 100,
      min: 0,
      max: 500,
      decimals: 0,
      unit: '%',
      unitsPerPixel: 500 / 260,
      keyframe: KeyframeState(
        animated: tempos.isNotEmpty,
        here: tempos.any(
          (t) => (t - local).abs() < const Duration(milliseconds: 8),
        ),
        onToggle: () {
          _c.toggleSceneLightKeyframe(
            widget.sceneId,
            luz.id,
            PropDaLuz.intensidade,
            _t,
          );
          setState(() {});
        },
      ),
      onChanged: (v) {
        Interacao.marcar();
        _c.editSceneLightProp(
          widget.sceneId,
          luz.id,
          PropDaLuz.intensidade,
          _t,
          v.clamp(0, 500) / 100,
        );
      },
    );
  }

  // ---------------------------------------------------------- ambiente

  List<Widget> _ambiente(Scene3D cena) => [
    _secao('Ambiente'),
    _chips<EnvironmentKind>(
      EnvironmentKind.values,
      environmentLabel,
      cena.environment,
      (k) => _c.setSceneEnvironment(widget.sceneId, k),
      chave: (k) => ValueKey('cena3d-ambiente-${k.name}'),
    ),
    _linha(
      'Reflexo do ambiente',
      cena.envReflect * 100,
      0,
      100,
      (v) => _c.setSceneEnvReflect(widget.sceneId, v / 100),
      unidade: '%',
    ),
    _linha(
      'Luz ambiente',
      cena.ambient * 100,
      0,
      300,
      (v) => _c.setSceneAmbient(widget.sceneId, v / 100),
      unidade: '%',
    ),
    _cor(
      'Cor do céu',
      cena.skyColor,
      (c) => _c.setSceneSkyColor(widget.sceneId, c),
      const ValueKey('cena3d-cor-ceu'),
    ),
    _cor(
      'Cor do chão',
      cena.groundColor,
      (c) => _c.setSceneGroundColor(widget.sceneId, c),
      const ValueKey('cena3d-cor-chao'),
    ),
  ];

  // ------------------------------------------------------------ pecas

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
    onChanged: (v) {
      Interacao.marcar();
      aoMudar(v.clamp(minimo, maximo).toDouble());
    },
  );

  Widget _secao(String titulo) => Padding(
    padding: const EdgeInsets.fromLTRB(2, 14, 2, 6),
    child: AppText(
      titulo.toUpperCase(),
      style: TextStyle(
        color: AmColors.muted,
        fontSize: 11,
        fontWeight: FontWeight.w600,
        letterSpacing: 0.8,
      ),
    ),
  );

  Widget _vazio(String texto) => Padding(
    padding: const EdgeInsets.symmetric(vertical: 18),
    child: AppText(
      texto,
      style: TextStyle(color: AmColors.muted, fontSize: 13),
    ),
  );

  Widget _botao(String rotulo, Key chave, VoidCallback aoTocar) =>
      GestureDetector(
        key: chave,
        onTap: aoTocar,
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 9),
          decoration: BoxDecoration(
            color: AmColors.chip,
            borderRadius: BorderRadius.circular(9),
          ),
          child: AppText(
            rotulo,
            style: TextStyle(color: AmColors.text, fontSize: 13),
          ),
        ),
      );

  /// A COR SE ESCOLHE NO SELETOR DA CASA, e ela chega VIVA: o palco
  /// atras da folha mostra o resultado enquanto o dedo anda.
  Widget _cor(
    String rotulo,
    Color atual,
    ValueChanged<Color> aoMudar,
    Key chave,
  ) => Padding(
    padding: const EdgeInsets.symmetric(vertical: 4),
    child: Row(
      children: [
        Expanded(
          child: AppText(
            rotulo,
            style: TextStyle(color: AmColors.text, fontSize: 13),
          ),
        ),
        GestureDetector(
          key: chave,
          onTap: () async {
            final nova = await showColorPicker(
              context,
              initial: atual,
              withAlpha: false,
              onChanged: aoMudar,
            );
            if (nova != null) aoMudar(nova);
          },
          child: Container(
            width: 46,
            height: 26,
            decoration: BoxDecoration(
              color: atual,
              borderRadius: BorderRadius.circular(7),
            ),
          ),
        ),
      ],
    ),
  );

  Widget _interruptor(
    String rotulo,
    bool ligado,
    ValueChanged<bool> aoMudar,
    Key chave,
  ) => Padding(
    padding: const EdgeInsets.symmetric(vertical: 2),
    child: Row(
      children: [
        Expanded(
          child: AppText(
            rotulo,
            style: TextStyle(color: AmColors.text, fontSize: 13),
          ),
        ),
        CupertinoSwitch(key: chave, value: ligado, onChanged: aoMudar),
      ],
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
