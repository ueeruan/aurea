import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/am_colors.dart';
import '../../application/editor_controller.dart';
import '../../application/model_import_service.dart';
import '../../domain/camera3d.dart';
import '../../domain/element3d.dart';
import '../../domain/estudio_ux.dart';
import '../../domain/layer.dart';
import '../../domain/scene3d.dart';
import '../widgets/campo_de_valor.dart';
import '../widgets/escolha_de_cor.dart';
import '../widgets/fita_de_ajuste.dart';
import 'estado_do_estudio.dart';
import 'scene3d_theme.dart';
import 'folha_de_objetos.dart';
import 'folha_de_adicionar.dart';
import 'folha_de_camera.dart';

export 'folha_de_animacao.dart';
export 'folha_de_objetos.dart';
export 'folha_de_adicionar.dart';
export 'folha_de_camera.dart';
export 'folha_de_luzes.dart';
export 'folha_de_exportar.dart';

/// Modal estilizado moderno para o Scene 3D
Future<T?> mostrarFolhaScene3D<T>(
  BuildContext context, {
  required String title,
  required Widget body,
  VoidCallback? onBack,
  Widget? trailing,
  double maxHeightFactor = 0.85,
}) => showModalBottomSheet<T>(
  context: context,
  backgroundColor: Scene3DTheme.panel,
  isScrollControlled: true,
  useSafeArea: true,
  shape: const RoundedRectangleBorder(
    borderRadius: BorderRadius.vertical(top: Radius.circular(24)),
  ),
  builder: (ctx) => ConstrainedBox(
    constraints: BoxConstraints(
      maxHeight: MediaQuery.of(ctx).size.height * maxHeightFactor,
    ),
    child: Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        const SizedBox(height: 8),
        Center(
          child: Container(
            width: 36,
            height: 4,
            decoration: BoxDecoration(
              color: Scene3DTheme.border,
              borderRadius: BorderRadius.circular(2),
            ),
          ),
        ),
        Scene3DSheetHeader(
          title: title,
          onBack: onBack,
          onClose: () => Navigator.of(ctx).pop(),
          trailing: trailing,
        ),
        Flexible(
          child: SingleChildScrollView(
            physics: const BouncingScrollPhysics(),
            child: body,
          ),
        ),
      ],
    ),
  ),
);

/// QUEM ESCOLHE O ARQUIVO DO MODELO.
///
/// Provider, e nao chamada direta, pelo mesmo motivo das fontes: em
/// teste o seletor do sistema nao existe, e sem uma porta de troca o
/// caminho inteiro de importacao ficaria sem cobertura.
final escolherModeloProvider = Provider<Future<List<String>> Function()>(
  (ref) => _escolherDoSistema,
);

Future<List<String>> _escolherDoSistema() async {
  final r = await FilePicker.platform.pickFiles(
    type: FileType.custom,
    allowedExtensions: const ['glb', 'gltf', 'obj', 'fbx', 'bin', 'mtl',
      'png', 'jpg', 'jpeg'],
    allowMultiple: true,
  );
  return [
    for (final f in r?.files ?? const <PlatformFile>[])
      if (f.path != null) f.path!,
  ];
}

/// UMA FOLHA DE BAIXO.
///
/// O estudio inteiro fala por folhas: e o gesto que o polegar alcanca
/// num celular, e e o unico jeito de a vista continuar sendo o elemento
/// principal (`docs/estudio-da-cena.md`). Ela nunca passa de 70% da
/// tela — atras dela a cena tem de continuar visivel, senao a pessoa
/// perde a referencia do que esta editando.
Future<T?> mostrarFolha<T>(
  BuildContext context, {
  required String titulo,
  required Widget Function(BuildContext) corpo,
}) => showModalBottomSheet<T>(
  context: context,
  backgroundColor: AmColors.panel,
  isScrollControlled: true,
  useSafeArea: true,
  shape: const RoundedRectangleBorder(
    borderRadius: BorderRadius.vertical(top: Radius.circular(18)),
  ),
  builder: (ctx) => Semantics(
    container: true,
    label: 'Folha: $titulo',
    child: ConstrainedBox(
      constraints: BoxConstraints(
        maxHeight: MediaQuery.of(ctx).size.height * .7,
      ),
      child: Padding(
        padding: const EdgeInsets.fromLTRB(14, 10, 14, 14),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Center(
              child: Container(
                width: 34,
                height: 4,
                decoration: BoxDecoration(
                  color: AmColors.hairline,
                  borderRadius: BorderRadius.circular(2),
                ),
              ),
            ),
            const SizedBox(height: 10),
            Flexible(child: corpo(ctx)),
          ],
        ),
      ),
    ),
  ),
);

// ------------------------------------------------------- as folhas

/// `+` — TUDO QUE SE PODE CRIAR, num menu so (Tela 5: Adicionar Objeto).
Future<void> abrirFolhaDeAdicionar(
  BuildContext context,
  WidgetRef ref, {
  required String layerId,
  required Duration tempo,
  required NavegacaoDaVista? navegacao,
  required void Function(String) aoAvisar,
}) => abrirFolhaDeAdicionarNovo(
  context,
  ref,
  layerId: layerId,
  tempo: tempo,
  aoAvisar: aoAvisar,
);

class _FolhaDeAdicionar extends ConsumerStatefulWidget {
  const _FolhaDeAdicionar({
    required this.layerId,
    required this.tempo,
    required this.navegacao,
    required this.aoAvisar,
  });

  final String layerId;
  final Duration tempo;
  final NavegacaoDaVista? navegacao;
  final void Function(String) aoAvisar;

  @override
  ConsumerState<_FolhaDeAdicionar> createState() => _FolhaDeAdicionarState();
}

class _FolhaDeAdicionarState extends ConsumerState<_FolhaDeAdicionar> {
  /// Nulo = o menu de cima. Preenchido = a lista daquele grupo.
  String? _grupo;
  bool _importando = false;
  String? _erro;

  EditorController get _c => ref.read(editorControllerProvider.notifier);

  Future<void> _importar() async {
    setState(() {
      _importando = true;
      _erro = null;
    });
    try {
      final caminhos = await ref.read(escolherModeloProvider)();
      if (caminhos.isEmpty) {
        if (mounted) setState(() => _importando = false);
        return;
      }
      final modelo = await readModel3DFiles(caminhos);
      if (!mounted) return;
      final id = _c.addModel3D(widget.layerId, modelo);
      if (id.isEmpty) {
        setState(() {
          _importando = false;
          _erro = 'Esta camada nao e uma cena 3D.';
        });
        return;
      }
      ref.read(noSelecionadoProvider.notifier).state = id;
      widget.aoAvisar('${modelo.name} importado.');
      Navigator.of(context).pop();
    } catch (e) {
      // UM ARQUIVO QUE NAO DA CERTO NAO PODE DERRUBAR A FOLHA: o motivo
      // aparece na propria linha, e a pessoa tenta outro.
      if (!mounted) return;
      setState(() {
        _importando = false;
        _erro = '$e'.replaceFirst('Exception: ', '');
      });
    }
  }

  void _criouNo(String aviso) {
    final camada = ref.read(editorControllerProvider).layerById(widget.layerId);
    if (camada is Scene3DLayer && camada.scene.nodes.isNotEmpty) {
      // O que acabou de nascer ja entra selecionado: o proximo gesto da
      // pessoa e quase sempre sobre ele.
      ref.read(noSelecionadoProvider.notifier).state =
          camada.scene.nodes.last.id;
      ref.read(selecaoDaCenaProvider.notifier).state = const {};
      ref.read(luzSelecionadaProvider.notifier).state = null;
    }
    widget.aoAvisar(aviso);
    Navigator.of(context).pop();
  }

  @override
  Widget build(BuildContext context) {
    if (_grupo == 'objeto') {
      return Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        mainAxisSize: MainAxisSize.min,
        children: [
          _Voltar(aoTocar: () => setState(() => _grupo = null)),
          const TituloDaFolha('Objeto 3D'),
          Flexible(
            child: SingleChildScrollView(
              child: Wrap(
                spacing: 6,
                runSpacing: 6,
                children: [
                  for (final k in Element3DKind.values)
                    ChipDaFolha(
                      rotulo: element3DLabel(k),
                      escolhida: false,
                      aoTocar: () {
                        _c.addSceneNode(widget.layerId, k);
                        _criouNo('${element3DLabel(k)} criado.');
                      },
                    ),
                ],
              ),
            ),
          ),
        ],
      );
    }
    if (_grupo == 'luz') {
      return Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        mainAxisSize: MainAxisSize.min,
        children: [
          _Voltar(aoTocar: () => setState(() => _grupo = null)),
          const TituloDaFolha('Luz'),
          for (final k in Light3DKind.values)
            AcaoDaFolha(
              icone: switch (k) {
                Light3DKind.directional => Icons.wb_sunny_rounded,
                Light3DKind.point => Icons.lightbulb_rounded,
                Light3DKind.ambient => Icons.blur_on_rounded,
                Light3DKind.spot => Icons.highlight_rounded,
              },
              rotulo: luzLabel(k),
              aoTocar: () {
                _c.addSceneLight(widget.layerId, k);
                final camada = ref
                    .read(editorControllerProvider)
                    .layerById(widget.layerId);
                if (camada is Scene3DLayer && camada.scene.lights.isNotEmpty) {
                  ref.read(luzSelecionadaProvider.notifier).state =
                      camada.scene.lights.last.id;
                  ref.read(noSelecionadoProvider.notifier).state = null;
                }
                widget.aoAvisar('${luzLabel(k)} criada.');
                Navigator.of(context).pop();
              },
            ),
        ],
      );
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      mainAxisSize: MainAxisSize.min,
      children: [
        const TituloDaFolha('Adicionar'),
        AcaoDaFolha(
          icone: Icons.view_in_ar_rounded,
          rotulo: 'Objeto 3D',
          detalhe: '${Element3DKind.values.length} formas prontas',
          aoTocar: () => setState(() => _grupo = 'objeto'),
        ),
        AcaoDaFolha(
          icone: Icons.videocam_rounded,
          rotulo: 'Camera',
          detalhe: 'Nasce com o enquadramento de agora',
          aoTocar: () {
            final id = _c.addScene3DCamera(widget.layerId);
            if (id.isNotEmpty) {
              _c.setCameraShot(widget.layerId, widget.tempo, id);
              ref.read(cameraSelecionadaProvider.notifier).state = id;
              widget.navegacao?.verVista(SceneView.camera);
            }
            widget.aoAvisar('Camera criada e no ar.');
            Navigator.of(context).pop();
          },
        ),
        AcaoDaFolha(
          icone: Icons.light_mode_rounded,
          rotulo: 'Luz',
          detalhe: '4 tipos',
          aoTocar: () => setState(() => _grupo = 'luz'),
        ),
        AcaoDaFolha(
          icone: Icons.control_camera_rounded,
          rotulo: 'Nulo',
          detalhe: 'Um pivo para prender outros objetos',
          aoTocar: () {
            _c.addSceneNull(widget.layerId);
            _criouNo('Nulo criado.');
          },
        ),
        // IMPORTAR MODELO: quatro formatos, leitura em isolate, e a
        // tela NAO trava enquanto le. `addModel3D` existia sem um
        // chamador em lugar nenhum.
        AcaoDaFolha(
          icone: _importando
              ? Icons.hourglass_top_rounded
              : Icons.upload_file_rounded,
          rotulo: _importando ? 'Importando...' : 'Importar modelo',
          detalhe: _erro ?? 'GLB, glTF, OBJ ou FBX',
          aoTocar: _importando ? () {} : _importar,
        ),
        AcaoDaFolha(
          icone: Icons.public_rounded,
          rotulo: 'Ambiente',
          detalhe: 'Ceu, chao, neblina e reflexo',
          aoTocar: () {
            Navigator.of(context).pop();
            abrirFolhaDoMundo(context, ref, layerId: widget.layerId);
          },
        ),
      ],
    );
  }
}

/// A CAMERA A UM TOQUE (Tela 6: Câmera).
Future<void> abrirFolhaDeCameras(
  BuildContext context,
  WidgetRef ref, {
  required String layerId,
  required NavegacaoDaVista navegacao,
  required Duration tempo,
}) => abrirFolhaDeCameraNova(
  context,
  ref,
  layerId: layerId,
  tempo: tempo,
);


/// A CENA / OBJETOS (Tela 2: Objetos - Outliner).
Future<void> abrirFolhaDaCena(
  BuildContext context,
  WidgetRef ref, {
  required String layerId,
  required Duration tempo,
}) => abrirFolhaDeObjetos(
  context,
  ref,
  layerId: layerId,
  tempo: tempo,
);

class _FolhaDaCena extends ConsumerStatefulWidget {
  const _FolhaDaCena({required this.layerId, required this.tempo});

  final String layerId;
  final Duration tempo;

  @override
  ConsumerState<_FolhaDaCena> createState() => _FolhaDaCenaState();
}

class _FolhaDaCenaState extends ConsumerState<_FolhaDaCena> {
  String _busca = '';

  @override
  Widget build(BuildContext context) {
    final bruta = ref.watch(editorControllerProvider).layerById(widget.layerId);
    if (bruta is! Scene3DLayer) return const SizedBox.shrink();
    final camada = bruta;
    final c = ref.read(editorControllerProvider.notifier);
    final local = camada.localTime(widget.tempo);

    final todos = hierarquiaDaCena(
      camada.scene,
      luzes: camada.scene.lights,
      cameras: camada.allCameras,
      cameraAtiva: cameraNoAr(camada, local).id,
      selecionado:
          ref.watch(noSelecionadoProvider) ??
          ref.watch(luzSelecionadaProvider),
    );
    final itens = buscarNaCena(todos, _busca);

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      mainAxisSize: MainAxisSize.min,
      children: [
        const TituloDaFolha('Cena'),
        // A BUSCA SO APARECE QUANDO A CENA E GRANDE. Numa cena de tres
        // objetos ela seria uma caixa de texto pedindo para ser
        // ignorada.
        if (todos.length > 8)
          Padding(
            padding: const EdgeInsets.only(bottom: 8),
            child: Semantics(
              container: true,
              textField: true,
              label: 'Procurar na cena',
              child: TextField(
                onChanged: (v) => setState(() => _busca = v),
                style: const TextStyle(fontSize: 12.5, color: AmColors.text),
                decoration: InputDecoration(
                  isDense: true,
                  hintText: translate(context, 'Procurar na cena...'),
                  hintStyle: const TextStyle(
                    fontSize: 12.5,
                    color: AmColors.muted,
                  ),
                  filled: true,
                  fillColor: AmColors.campo,
                  border: OutlineInputBorder(
                    borderRadius: BorderRadius.circular(9),
                    borderSide: BorderSide.none,
                  ),
                  contentPadding: const EdgeInsets.symmetric(
                    horizontal: 10,
                    vertical: 10,
                  ),
                ),
              ),
            ),
          ),
        if (itens.isEmpty)
          const AvisoDaFolha('A cena esta vazia. Toque no + para criar.')
        else
          Flexible(
            child: SingleChildScrollView(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                mainAxisSize: MainAxisSize.min,
                children: [
                  for (final i in itens)
                    _LinhaDaHierarquia(
                      item: i,
                      aoTocar: () {
                        switch (i.tipo) {
                          case TipoDeItem.no:
                            ref.read(noSelecionadoProvider.notifier).state =
                                i.id;
                            ref.read(luzSelecionadaProvider.notifier).state =
                                null;
                            ref
                                .read(cameraSelecionadaProvider.notifier)
                                .state = null;
                          case TipoDeItem.luz:
                            ref.read(luzSelecionadaProvider.notifier).state =
                                i.id;
                            ref.read(noSelecionadoProvider.notifier).state =
                                null;
                            ref
                                .read(cameraSelecionadaProvider.notifier)
                                .state = null;
                          case TipoDeItem.camera:
                            ref
                                .read(cameraSelecionadaProvider.notifier)
                                .state = i.id;
                            ref.read(noSelecionadoProvider.notifier).state =
                                null;
                            ref.read(luzSelecionadaProvider.notifier).state =
                                null;
                        }
                        ref.read(selecaoDaCenaProvider.notifier).state =
                            const {};
                        Navigator.of(context).pop();
                      },
                      // O OLHO E O CADEADO SO NOS NOS: luz e camera nao
                      // tem `visible` nem `locked` no motor, e um botao
                      // que nao faz nada e pior que botao nenhum.
                      aoAlternarOlho: i.tipo != TipoDeItem.no
                          ? null
                          : () => c.setSceneNodeVisible(
                              widget.layerId,
                              i.id,
                              !i.visivel,
                            ),
                      aoAlternarCadeado: i.tipo != TipoDeItem.no
                          ? null
                          : () => c.setSceneNodeLocked(
                              widget.layerId,
                              i.id,
                              !i.travado,
                            ),
                    ),
                ],
              ),
            ),
          ),
      ],
    );
  }
}

/// MUNDO: o ambiente da cena, longe das propriedades de objeto.
Future<void> abrirFolhaDoMundo(
  BuildContext context,
  WidgetRef ref, {
  required String layerId,
}) => mostrarFolha(
  context,
  titulo: 'Mundo',
  corpo: (ctx) => _FolhaDoMundo(layerId: layerId),
);

class _FolhaDoMundo extends ConsumerWidget {
  const _FolhaDoMundo({required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final bruta = ref.watch(editorControllerProvider).layerById(layerId);
    if (bruta is! Scene3DLayer) return const SizedBox.shrink();
    final s = bruta.scene;
    final c = ref.read(editorControllerProvider.notifier);

    return SingleChildScrollView(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        mainAxisSize: MainAxisSize.min,
        children: [
          const TituloDaFolha('Ambiente'),
          const AvisoDaFolha(
            'E o ambiente que os objetos refletem. Sem ele, metal nao '
            'parece metal — espelho de nada e preto.',
          ),
          Wrap(
            spacing: 6,
            runSpacing: 6,
            children: [
              for (final e in EnvironmentKind.values)
                ChipDaFolha(
                  rotulo: environmentLabel(e),
                  escolhida: s.environment == e,
                  aoTocar: () => c.setSceneEnvironment(layerId, e),
                ),
            ],
          ),
          _Numero(
            rotulo: 'Reflexo',
            nome: 'Forca do reflexo do ambiente',
            valor: s.envReflect,
            porPixel: .004,
            aoMudar: (v) => c.setSceneEnvReflect(layerId, v),
          ),
          _Numero(
            rotulo: 'Luz ambiente',
            nome: 'Luz ambiente da cena',
            valor: s.ambient,
            porPixel: .006,
            aoMudar: (v) => c.setSceneAmbient(layerId, v),
          ),
          const TituloDaFolha('Ceu e chao'),
          AmostraDeCor(
            cor: s.skyColor,
            rotulo: 'Cor do ceu',
            aoEscolher: (cor) => c.setSceneSkyColor(layerId, cor),
          ),
          AmostraDeCor(
            cor: s.groundColor,
            rotulo: 'Cor do chao',
            aoEscolher: (cor) => c.setSceneGroundColor(layerId, cor),
          ),
          const TituloDaFolha('Fundo'),
          InterruptorDaFolha(
            rotulo: 'Fundo proprio',
            icone: Icons.wallpaper_rounded,
            ligado: s.background != null,
            aoTocar: () => c.setSceneBackground(
              layerId,
              s.background == null ? const Color(0xFF101018) : null,
            ),
          ),
          if (s.background != null)
            AmostraDeCor(
              cor: s.background!,
              rotulo: 'Cor do fundo',
              aoEscolher: (cor) => c.setSceneBackground(layerId, cor),
            ),
          const TituloDaFolha('Chao'),
          InterruptorDaFolha(
            rotulo: 'Grade do chao',
            icone: Icons.grid_4x4_rounded,
            ligado: s.showFloorGrid,
            aoTocar: () => c.setSceneFloorGrid(layerId, !s.showFloorGrid),
          ),
          InterruptorDaFolha(
            rotulo: 'Piso espelhado',
            icone: Icons.water_rounded,
            ligado: s.planarFloorReflection,
            aoTocar: () => c.setScenePlanarFloor(
              layerId,
              !s.planarFloorReflection,
            ),
          ),
          if (s.planarFloorReflection)
            _Numero(
              rotulo: 'Aspereza',
              nome: 'Aspereza do piso espelhado',
              valor: s.planarFloorRoughness,
              porPixel: .004,
              aoMudar: (v) =>
                  c.setScenePlanarFloor(layerId, true, aspereza: v),
            ),
          const TituloDaFolha('Neblina'),
          _Numero(
            rotulo: 'Densidade',
            nome: 'Densidade da neblina',
            valor: s.fogDensity,
            porPixel: .004,
            aoMudar: (v) => c.setSceneFog(layerId, densidade: v),
          ),
          if (s.fogDensity > 0) ...[
            _Numero(
              rotulo: 'Comeco',
              nome: 'Onde a neblina comeca',
              valor: s.fogStart,
              porPixel: 4,
              casas: 0,
              aoMudar: (v) => c.setSceneFog(layerId, comeco: v),
            ),
            AmostraDeCor(
              cor: s.fogColor,
              rotulo: 'Cor da neblina',
              aoEscolher: (cor) => c.setSceneFog(layerId, cor: cor),
            ),
          ],
        ],
      ),
    );
  }
}

/// RENDER, separado da cena de proposito: e configuracao tecnica, e
/// mistura-la com "a cor deste cubo" e o que faz um painel virar sopa.
Future<void> abrirFolhaDeRender(
  BuildContext context,
  WidgetRef ref, {
  required String layerId,
}) => mostrarFolha(
  context,
  titulo: 'Render',
  corpo: (ctx) => _FolhaDeRender(layerId: layerId),
);

class _FolhaDeRender extends ConsumerWidget {
  const _FolhaDeRender({required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final bruta = ref.watch(editorControllerProvider).layerById(layerId);
    if (bruta is! Scene3DLayer) return const SizedBox.shrink();
    final s = bruta.scene;
    final c = ref.read(editorControllerProvider.notifier);

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      mainAxisSize: MainAxisSize.min,
      children: [
        const TituloDaFolha('Render'),
        InterruptorDaFolha(
          rotulo: 'Suavizar as bordas (MSAA)',
          icone: Icons.blur_linear_rounded,
          ligado: s.msaa,
          aoTocar: () => c.setSceneMsaa(layerId, !s.msaa),
        ),
        InterruptorDaFolha(
          rotulo: 'Mapear o tom (ACES)',
          icone: Icons.gradient_rounded,
          ligado: s.tonemap,
          aoTocar: () => c.setSceneTonemap(layerId, !s.tonemap),
        ),
        InterruptorDaFolha(
          rotulo: 'Ajudas na vista (grade, eixo, frustum)',
          icone: Icons.straighten_rounded,
          ligado: bruta.showHelpers,
          aoTocar: () =>
              c.setScene3DHelpers(layerId, !bruta.showHelpers),
        ),
        const AvisoDaFolha(
          'A qualidade do 3D (sombra, textura e resolucao) e do aparelho '
          'inteiro, e nao desta cena: fica em Ajustes > Qualidade 3D.',
        ),
      ],
    );
  }
}

/// `⋮` — o que sobra, e nada aqui e essencial.
Future<void> abrirFolhaDeMais(
  BuildContext context,
  WidgetRef ref, {
  required String layerId,
  required NavegacaoDaVista navegacao,
  required Duration tempo,
}) => mostrarFolha(
  context,
  titulo: 'Mais',
  corpo: (ctx) => _FolhaDeMais(
    layerId: layerId,
    navegacao: navegacao,
    tempo: tempo,
  ),
);

class _FolhaDeMais extends ConsumerWidget {
  const _FolhaDeMais({
    required this.layerId,
    required this.navegacao,
    required this.tempo,
  });

  final String layerId;
  final NavegacaoDaVista navegacao;
  final Duration tempo;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final c = ref.read(editorControllerProvider.notifier);
    final avancado = ref.watch(avancadoProvider);
    final encaixe = ref.watch(encaixeProvider);

    return SingleChildScrollView(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        mainAxisSize: MainAxisSize.min,
        children: [
          AcaoDaFolha(
            icone: Icons.account_tree_rounded,
            rotulo: 'Cena',
            detalhe: 'Todos os objetos, luzes e cameras',
            aoTocar: () {
              Navigator.of(context).pop();
              abrirFolhaDaCena(context, ref, layerId: layerId, tempo: tempo);
            },
          ),
          AcaoDaFolha(
            icone: Icons.public_rounded,
            rotulo: 'Mundo',
            detalhe: 'Ambiente, ceu, chao e neblina',
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
          const Divider(height: 14, color: AmColors.hairline),
          const TituloDaFolha('Encaixe'),
          InterruptorDaFolha(
            rotulo: 'Encaixar na grade',
            icone: Icons.grid_on_rounded,
            ligado: encaixe,
            aoTocar: () => ref.read(encaixeProvider.notifier).state = !encaixe,
          ),
          const TituloDaFolha('Travar o eixo'),
          Wrap(
            spacing: 6,
            runSpacing: 6,
            children: [
              for (final e in EixoTravado.values)
                ChipDaFolha(
                  rotulo: eixoLabel(e),
                  escolhida: ref.watch(eixoProvider) == e,
                  aoTocar: () => ref.read(eixoProvider.notifier).state = e,
                ),
            ],
          ),
          const Divider(height: 14, color: AmColors.hairline),
          AcaoDaFolha(
            icone: Icons.fit_screen_rounded,
            rotulo: 'Alinhar a camera a esta vista',
            detalhe: 'Compromete o que se achou navegando livre',
            aoTocar: () {
              final bruta = ref
                  .read(editorControllerProvider)
                  .layerById(layerId);
              if (bruta is! Scene3DLayer) return;
              if (navegacao.pelaCamera) return;
              final local = bruta.localTime(tempo);
              final rc = navegacao.cameraDeRender(bruta, local);
              c.updateSceneCameraById(
                layerId,
                alignToView(cameraNoAr(bruta, local), rc),
              );
              navegacao.verVista(SceneView.camera);
              Navigator.of(context).pop();
            },
          ),
          const Divider(height: 14, color: AmColors.hairline),
          InterruptorDaFolha(
            rotulo: 'Modo avancado',
            icone: Icons.science_rounded,
            ligado: avancado,
            aoTocar: () =>
                ref.read(avancadoProvider.notifier).state = !avancado,
          ),
          const AvisoDaFolha(
            'O avancado nao esconde poder: ele adia. Tudo continua '
            'alcancavel pela ficha de cada coisa.',
          ),
        ],
      ),
    );
  }
}

// ------------------------------------------------------ as pecinhas

class TituloDaFolha extends StatelessWidget {
  const TituloDaFolha(this.texto, {super.key});

  final String texto;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(2, 12, 0, 6),
    child: AppText(texto,
      style: const TextStyle(
        fontSize: 11,
        fontWeight: FontWeight.w700,
        color: AmColors.muted,
      ),
    ),
  );
}

class AvisoDaFolha extends StatelessWidget {
  const AvisoDaFolha(this.texto, {super.key});

  final String texto;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(2, 4, 0, 8),
    child: AppText(texto,
      style: const TextStyle(
        fontSize: 11,
        color: AmColors.muted,
        height: 1.4,
      ),
    ),
  );
}

class ChipDaFolha extends StatelessWidget {
  const ChipDaFolha({
    super.key,
    required this.rotulo,
    required this.escolhida,
    required this.aoTocar,
  });

  final String rotulo;
  final bool escolhida;
  final VoidCallback aoTocar;

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
        constraints: const BoxConstraints(minHeight: 40),
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
        decoration: BoxDecoration(
          color: escolhida ? AmColors.chip : null,
          borderRadius: BorderRadius.circular(8),
          border: escolhida ? null : Border.all(color: AmColors.hairline),
        ),
        child: Center(
          widthFactor: 1,
          child: AppText(
            rotulo,
            style: TextStyle(
              fontSize: 12,
              fontWeight: FontWeight.w600,
              color: escolhida ? AmColors.accent : AmColors.text,
            ),
          ),
        ),
      ),
    ),
  );
}

class AcaoDaFolha extends StatelessWidget {
  const AcaoDaFolha({
    super.key,
    required this.icone,
    required this.rotulo,
    required this.aoTocar,
    this.detalhe,
  });

  final IconData icone;
  final String rotulo;
  final VoidCallback aoTocar;
  final String? detalhe;

  @override
  Widget build(BuildContext context) => Semantics(
    container: true,
    excludeSemantics: true,
    button: true,
    label: rotulo,
    child: GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: aoTocar,
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: 11),
        child: Row(
          children: [
            Icon(icone, size: 19, color: AmColors.text),
            const SizedBox(width: 12),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  AppText(
                    rotulo,
                    style: const TextStyle(
                      fontSize: 13,
                      fontWeight: FontWeight.w600,
                      color: AmColors.text,
                    ),
                  ),
                  if (detalhe != null)
                    Padding(
                      padding: const EdgeInsets.only(top: 2),
                      child: AppText(
                        detalhe!,
                        style: TextStyle(
                          fontSize: 10,
                          color: AmColors.muted.withValues(alpha: .8),
                        ),
                      ),
                    ),
                ],
              ),
            ),
          ],
        ),
      ),
    ),
  );
}

class InterruptorDaFolha extends StatelessWidget {
  const InterruptorDaFolha({
    super.key,
    required this.rotulo,
    required this.icone,
    required this.ligado,
    required this.aoTocar,
  });

  final String rotulo;
  final IconData icone;
  final bool ligado;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => Semantics(
    container: true,
    excludeSemantics: true,
    button: true,
    toggled: ligado,
    label: rotulo,
    child: GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: aoTocar,
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: 11),
        child: Row(
          children: [
            Icon(
              icone,
              size: 19,
              color: ligado ? AmColors.accent : AmColors.text,
            ),
            const SizedBox(width: 12),
            Expanded(
              child: AppText(
                rotulo,
                style: TextStyle(
                  fontSize: 13,
                  fontWeight: FontWeight.w600,
                  color: ligado ? AmColors.accent : AmColors.text,
                ),
              ),
            ),
            Container(
              width: 34,
              height: 20,
              decoration: BoxDecoration(
                color: ligado ? AmColors.accentDim : AmColors.chip,
                borderRadius: BorderRadius.circular(10),
              ),
              child: Align(
                alignment: ligado
                    ? Alignment.centerRight
                    : Alignment.centerLeft,
                child: Container(
                  width: 16,
                  height: 16,
                  margin: const EdgeInsets.symmetric(horizontal: 2),
                  decoration: BoxDecoration(
                    color: ligado ? AmColors.accent : AmColors.muted,
                    shape: BoxShape.circle,
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    ),
  );
}

/// Um contador de 0 a [maximo] — para subdivisoes e afins.
class PassosDaFolha extends StatelessWidget {
  const PassosDaFolha({
    super.key,
    required this.valor,
    required this.maximo,
    required this.rotulo,
    required this.aoMudar,
  });

  final int valor;
  final int maximo;
  final String rotulo;
  final void Function(int) aoMudar;

  @override
  Widget build(BuildContext context) => Row(
    children: [
      for (var i = 0; i <= maximo; i++)
        Padding(
          padding: const EdgeInsets.only(right: 6),
          child: ChipDaFolha(
            rotulo: '$i',
            escolhida: i == valor,
            aoTocar: () => aoMudar(i),
          ),
        ),
    ],
  );
}

/// A cor, e a folha que a escolhe.
class AmostraDeCor extends StatelessWidget {
  const AmostraDeCor({
    super.key,
    required this.cor,
    required this.rotulo,
    required this.aoEscolher,
  });

  final Color cor;
  final String rotulo;
  final void Function(Color) aoEscolher;

  @override
  Widget build(BuildContext context) => Semantics(
    container: true,
    excludeSemantics: true,
    button: true,
    label: rotulo,
    child: GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: () => mostrarFolha<void>(
        context,
        titulo: rotulo,
        corpo: (ctx) => SingleChildScrollView(
          child: EscolhaDeCor(
            rotulo: rotulo,
            cor: cor,
            aoMudar: aoEscolher,
          ),
        ),
      ),
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: 9),
        child: Row(
          children: [
            Container(
              width: 26,
              height: 26,
              decoration: BoxDecoration(
                color: cor,
                borderRadius: BorderRadius.circular(7),
                border: Border.all(color: AmColors.hairline),
              ),
            ),
            const SizedBox(width: 11),
            Expanded(
              child: AppText(
                rotulo,
                style: const TextStyle(
                  fontSize: 13,
                  fontWeight: FontWeight.w600,
                  color: AmColors.text,
                ),
              ),
            ),
            const Icon(
              Icons.chevron_right_rounded,
              size: 18,
              color: AmColors.muted,
            ),
          ],
        ),
      ),
    ),
  );
}

/// UMA LINHA DE TRILHA: o numero, e o losango ao lado dele.
///
/// A diferenca para [LinhaDeParametro] e o losango. No estudio ele nao
/// pode morar num rail lateral — cada linha fala de uma trilha
/// diferente, e um rail so miraria uma. Entao cada linha carrega o seu.
class LinhaDaTrilha extends StatelessWidget {
  const LinhaDaTrilha({
    super.key,
    required this.rotulo,
    required this.nome,
    required this.valor,
    required this.porPixel,
    required this.casas,
    required this.temMarcaAqui,
    required this.anima,
    required this.aoMudar,
    required this.aoAlternarMarca,
    required this.aoAbrirCurva,
    required this.aoZerar,
    this.sufixo = '',
    this.aoComecar,
    this.aoTerminar,
  });

  final String rotulo;
  final String nome;
  final double valor;
  final double porPixel;
  final int casas;
  final String sufixo;

  /// Ha marca EXATAMENTE no cabecote?
  final bool temMarcaAqui;

  /// A trilha tem alguma marca, em qualquer instante?
  final bool anima;

  final void Function(double) aoMudar;
  final VoidCallback aoAlternarMarca;
  final VoidCallback? aoAbrirCurva;
  final VoidCallback? aoZerar;
  final VoidCallback? aoComecar;
  final VoidCallback? aoTerminar;

  @override
  Widget build(BuildContext context) => SizedBox(
    height: 48,
    child: Row(
      children: [
        SizedBox(
          width: 52,
          child: AppText(
            rotulo,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: const TextStyle(
              fontSize: 11.5,
              fontWeight: FontWeight.w600,
              color: AmColors.text,
            ),
          ),
        ),
        Expanded(
          child: FitaDeAjuste(
            rotulo: nome,
            valor: valor,
            porPixel: porPixel,
            altura: 40,
            aoComecar: aoComecar,
            aoMudar: aoMudar,
            aoTerminar: aoTerminar,
          ),
        ),
        const SizedBox(width: 6),
        CampoDeValor(
          rotulo: '',
          nome: nome,
          valor: valor,
          casas: casas,
          sufixo: sufixo,
          largura: 62,
          aoDigitar: aoMudar,
        ),
        const SizedBox(width: 4),
        _Losango(
          rotulo: nome,
          cheio: temMarcaAqui,
          anima: anima,
          aoTocar: aoAlternarMarca,
        ),
      ],
    ),
  );
}

/// A linha da aba ANIMAR: a propriedade, se anima, e os dois botoes.
class LinhaDeAnimar extends StatelessWidget {
  const LinhaDeAnimar({
    super.key,
    required this.rotulo,
    required this.valor,
    required this.marcas,
    required this.local,
    required this.aoAlternarMarca,
    required this.aoAbrirGrafico,
  });

  final String rotulo;
  final double valor;
  final List<Duration> marcas;
  final Duration local;
  final VoidCallback aoAlternarMarca;
  final VoidCallback aoAbrirGrafico;

  @override
  Widget build(BuildContext context) {
    final aqui = marcas.any(
      (t) => (t - local).abs() < const Duration(milliseconds: 8),
    );
    return SizedBox(
      height: 46,
      child: Row(
        children: [
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                AppText(
                  rotulo,
                  style: const TextStyle(
                    fontSize: 12.5,
                    fontWeight: FontWeight.w600,
                    color: AmColors.text,
                  ),
                ),
                AppText(
                  marcas.isEmpty
                      ? 'sem keyframes'
                      : '${marcas.length} keyframes',
                  style: TextStyle(
                    fontSize: 10,
                    color: marcas.isEmpty
                        ? AmColors.muted.withValues(alpha: .7)
                        : AmColors.accent,
                  ),
                ),
              ],
            ),
          ),
          // GRAFICO: abre o editor de curva QUE JA EXISTE. O pedido e
          // explicito — nao criar um sistema novo.
          if (marcas.length > 1)
            Semantics(
              container: true,
              excludeSemantics: true,
              button: true,
              label: 'Grafico de $rotulo',
              child: GestureDetector(
                behavior: HitTestBehavior.opaque,
                onTap: aoAbrirGrafico,
                child: const SizedBox(
                  width: 42,
                  height: 46,
                  child: Icon(
                    Icons.timeline_rounded,
                    size: 18,
                    color: AmColors.muted,
                  ),
                ),
              ),
            ),
          _Losango(
            rotulo: rotulo,
            cheio: aqui,
            anima: marcas.isNotEmpty,
            aoTocar: aoAlternarMarca,
          ),
        ],
      ),
    );
  }
}

/// O LOSANGO. Cheio quando ha marca no cabecote, vazado quando a trilha
/// anima mas nao aqui, apagado quando nao anima nada. Ele diz o que o
/// toque vai fazer — sempre (`docs/keyframe-explicito.md`).
class _Losango extends StatelessWidget {
  const _Losango({
    required this.rotulo,
    required this.cheio,
    required this.anima,
    required this.aoTocar,
  });

  final String rotulo;
  final bool cheio;
  final bool anima;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => Semantics(
    container: true,
    excludeSemantics: true,
    button: true,
    selected: cheio,
    label: cheio ? 'Tirar keyframe de $rotulo' : 'Keyframe de $rotulo',
    child: GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: aoTocar,
      child: SizedBox(
        width: 42,
        height: 46,
        child: Icon(
          cheio
              ? Icons.change_history_rounded
              : Icons.change_history_outlined,
          size: 19,
          color: cheio
              ? AmColors.accent
              : (anima ? AmColors.tealBright : AmColors.muted),
        ),
      ),
    ),
  );
}

class _Numero extends ConsumerWidget {
  const _Numero({
    required this.rotulo,
    required this.nome,
    required this.valor,
    required this.porPixel,
    required this.aoMudar,
    this.casas = 2,
  });

  final String rotulo;
  final String nome;
  final double valor;
  final double porPixel;
  final int casas;
  final void Function(double) aoMudar;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final c = ref.read(editorControllerProvider.notifier);
    return SizedBox(
      height: 48,
      child: Row(
        children: [
          SizedBox(
            width: 88,
            child: AppText(
              rotulo,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(
                fontSize: 11.5,
                fontWeight: FontWeight.w600,
                color: AmColors.text,
              ),
            ),
          ),
          Expanded(
            child: FitaDeAjuste(
              rotulo: nome,
              valor: valor,
              porPixel: porPixel,
              altura: 40,
              aoComecar: c.beginGesture,
              aoMudar: aoMudar,
              aoTerminar: c.endGesture,
            ),
          ),
          const SizedBox(width: 6),
          CampoDeValor(
            rotulo: '',
            nome: nome,
            valor: valor,
            casas: casas,
            largura: 62,
            aoDigitar: aoMudar,
          ),
        ],
      ),
    );
  }
}

class _Voltar extends StatelessWidget {
  const _Voltar({required this.aoTocar});

  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => Semantics(
    container: true,
    excludeSemantics: true,
    button: true,
    label: 'Voltar',
    child: GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: aoTocar,
      child: const SizedBox(
        height: 34,
        child: Row(
          children: [
            Icon(
              Icons.chevron_left_rounded,
              size: 20,
              color: AmColors.muted,
            ),
            AppText(
              'Voltar',
              style: TextStyle(fontSize: 12, color: AmColors.muted),
            ),
          ],
        ),
      ),
    ),
  );
}

class _LinhaDaHierarquia extends StatelessWidget {
  const _LinhaDaHierarquia({
    required this.item,
    required this.aoTocar,
    required this.aoAlternarOlho,
    required this.aoAlternarCadeado,
  });

  final ItemDaCena item;
  final VoidCallback aoTocar;
  final VoidCallback? aoAlternarOlho;
  final VoidCallback? aoAlternarCadeado;

  @override
  Widget build(BuildContext context) => SizedBox(
    height: 44,
    child: Row(
      children: [
        // A ARVORE APARECE. Um modelo importado tem corpo, cabeca,
        // olhos e cabelo — esconder isso obrigaria a adivinhar.
        SizedBox(width: 4.0 + item.nivel * 14),
        Expanded(
          child: Semantics(
            container: true,
            excludeSemantics: true,
            button: true,
            selected: item.ativo,
            label: 'Escolher ${item.nome}',
            child: GestureDetector(
              behavior: HitTestBehavior.opaque,
              onTap: aoTocar,
              child: Row(
                children: [
                  Icon(
                    switch (item.tipo) {
                      TipoDeItem.camera => Icons.videocam_rounded,
                      TipoDeItem.luz => Icons.light_mode_rounded,
                      TipoDeItem.no => item.grupo
                          ? Icons.control_camera_rounded
                          : Icons.view_in_ar_rounded,
                    },
                    size: 16,
                    color: item.ativo ? AmColors.accent : AmColors.muted,
                  ),
                  const SizedBox(width: 9),
                  Flexible(
                    child: AppText(item.nome,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(
                        fontSize: 12.5,
                        fontWeight: FontWeight.w600,
                        color: item.ativo
                            ? AmColors.accent
                            : (item.visivel
                                  ? AmColors.text
                                  : AmColors.muted),
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
        if (aoAlternarOlho != null)
          _IconeDaLinha(
            icone: item.visivel
                ? Icons.visibility_rounded
                : Icons.visibility_off_rounded,
            rotulo: item.visivel
                ? 'Esconder ${item.nome}'
                : 'Mostrar ${item.nome}',
            aoTocar: aoAlternarOlho!,
          ),
        if (aoAlternarCadeado != null)
          _IconeDaLinha(
            icone: item.travado
                ? Icons.lock_rounded
                : Icons.lock_open_rounded,
            rotulo: item.travado
                ? 'Destravar ${item.nome}'
                : 'Travar ${item.nome}',
            aoTocar: aoAlternarCadeado!,
          ),
      ],
    ),
  );
}

class _IconeDaLinha extends StatelessWidget {
  const _IconeDaLinha({
    required this.icone,
    required this.rotulo,
    required this.aoTocar,
  });

  final IconData icone;
  final String rotulo;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => Semantics(
    container: true,
    excludeSemantics: true,
    button: true,
    label: rotulo,
    child: GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: aoTocar,
      child: SizedBox(
        width: 40,
        height: 44,
        child: Icon(icone, size: 17, color: AmColors.muted),
      ),
    ),
  );
}
