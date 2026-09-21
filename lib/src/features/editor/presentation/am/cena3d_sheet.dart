import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/l10n/app_language.dart';
import '../../../../core/ui/tocavel.dart';
import '../../application/editor_controller.dart';
import '../../application/interacao.dart';
import '../../domain/element3d.dart';
import '../../domain/gizmo_da_cena3d.dart';
import '../../domain/layer.dart';
import '../../domain/model_asset3d.dart';
import '../../domain/scene3d.dart';
import '../context/parameter_row.dart';
import '../widgets/gizmo_da_cena_overlay.dart';
import 'am_colors.dart';
import 'am_widgets.dart';
import 'color_picker_sheet.dart';

/// O PAINEL "3D" — o objeto da cena editado COM OS COMPONENTES DA CASA.
///
/// ======================= O QUE FOI JOGADO FORA ========================
///
/// A versao anterior desta folha era um painel proprio: cartao com
/// borda, abas inventadas, botoes de cor e interruptores desenhados a
/// mao, fundo e tipografia so dela. O dono olhou e disse o obvio — "nao
/// tem nada a ver com a identidade visual do Aurea", "parece plugin
/// externo". Estava certo: eram componentes NOVOS para resolver
/// problemas que o painel de Efeitos ja resolve ha meses.
///
/// Agora nao ha um unico widget visual inventado aqui. A casca e a de
/// todo painel de parametro ([showParamSheet]: mesma barra de cima,
/// mesmo fundo, mesmas tres alturas, deslizar para fechar). O corpo e a
/// planta do painel de Efeitos (`effects_panel.dart`): um CARTAO por
/// assunto, que abre e fecha pelo triangulo, com cabeca de grupo
/// discreta dentro. E toda linha e a linha do app — [ParameterRow],
/// [ParameterToggleRow], [ParameterColorRow], [ParameterCustomRow] — com
/// o MESMO losango de keyframe ([KeyframeState]), a mesma regua, o mesmo
/// campo numerico que abre o teclado do Aurea.
///
/// ====================== O PREVIEW E O PREVIEW =========================
///
/// Nao ha viewport aqui, nem previa, nem moldura. Esta folha e
/// PERSISTENTE (nao modal): o palco continua vivo atras dela, o gizmo
/// continua sobre o objeto e cada toque aqui aparece na cena de verdade.
/// O objeto editado e o MESMO que o gizmo esta segurando
/// ([noDaCenaSelecionadoProvider]): escolher aqui move o gizmo, e tocar
/// um objeto no palco troca o que esta aqui.
///
/// O QUE NAO APARECE: controle que o motor IGNORA. Neblina, panorama,
/// sonda de reflexo, reflexo do piso, tonemap, fundo, grade do chao,
/// `reflectivity` e imagem por face entram na chave do quadro e nunca
/// sao enviados — uma regua que nao muda um pixel e pior do que regua
/// nenhuma, porque faz duvidar do resto.
Future<void> showCena3DSheet(
  BuildContext context,
  WidgetRef ref, {
  required String sceneId,
  Duration? playhead,
}) => showParamSheet(
  context,
  title: translate(context, '3D'),
  heightFactor: 0.62,
  builder: (_) => _Painel3D(sceneId: sceneId, playhead: playhead),
);

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

String _rotuloDoDetalhe(MeshLod3D l) => switch (l) {
  MeshLod3D.auto => 'Automático',
  MeshLod3D.high => 'Alto',
  MeshLod3D.medium => 'Médio',
  MeshLod3D.low => 'Baixo',
};

class _Painel3D extends ConsumerStatefulWidget {
  const _Painel3D({required this.sceneId, required this.playhead});

  final String sceneId;

  /// O instante de quem abriu, quando ele soube passar. O palco publica o
  /// cabecote em [cabecoteDoPalco] e e ele que manda enquanto a folha
  /// esta aberta — assim o losango acompanha o transporte sem a folha
  /// precisar do `PlaybackController` (que o menu da camada nao passa).
  final Duration? playhead;

  @override
  ConsumerState<_Painel3D> createState() => _Painel3DState();
}

class _Painel3DState extends ConsumerState<_Painel3D> {
  /// Os cartoes ABERTOS. Transformar abre sozinho: e o que a pessoa
  /// procura primeiro depois de tocar num objeto.
  final Set<String> _abertos = {'transformar'};

  String? _luzEscolhida;

  EditorController get _c => ref.read(editorControllerProvider.notifier);

  void _alternar(String cartao) => setState(() {
    if (!_abertos.remove(cartao)) _abertos.add(cartao);
  });

  @override
  Widget build(BuildContext context) => SafeArea(
    top: false,
    child: ValueListenableBuilder<Duration>(
      valueListenable: cabecoteDoPalco,
      builder: (context, doPalco, _) {
        // O CABECOTE DO PALCO MANDA; o instante de abertura so vale
        // enquanto o palco ainda nao publicou nenhum (teste no PC).
        final t = doPalco == Duration.zero
            ? (widget.playhead ?? Duration.zero)
            : doPalco;
        final projeto = ref.watch(editorControllerProvider);
        final camada = projeto.layerById(widget.sceneId);
        if (camada is! Scene3DLayer) return const SizedBox.shrink();
        final cena = camada.scene;
        final local = camada.localTime(t);
        final objetos = objetosDaCena(cena);
        final noId = noAtivoDaCena(
          cena,
          ref.watch(noDaCenaSelecionadoProvider),
        );
        final no = noId == null ? null : cena.nodeById(noId);

        return ListView(
          key: const ValueKey('cena3d-painel'),
          padding: const EdgeInsets.fromLTRB(12, 4, 12, 20),
          children: [
            if (objetos.length > 1) _escolhaDeObjeto(objetos, no),
            _Cartao(
              chave: 'transformar',
              titulo: 'Transformar',
              aberto: _abertos.contains('transformar'),
              aoAlternar: () => _alternar('transformar'),
              linhas: no == null
                  ? [_vazio('Esta cena não tem objeto para transformar.')]
                  : _transformar(no, t, local),
            ),
            _Cartao(
              chave: 'material',
              titulo: 'Material',
              aberto: _abertos.contains('material'),
              aoAlternar: () => _alternar('material'),
              linhas: no == null
                  ? [_vazio('Esta cena não tem objeto para pintar.')]
                  : _material(no),
            ),
            _Cartao(
              chave: 'iluminacao',
              titulo: 'Iluminação',
              aberto: _abertos.contains('iluminacao'),
              aoAlternar: () => _alternar('iluminacao'),
              linhas: _iluminacao(cena, t, local),
            ),
            _Cartao(
              chave: 'ambiente',
              titulo: 'Ambiente',
              aberto: _abertos.contains('ambiente'),
              aoAlternar: () => _alternar('ambiente'),
              linhas: _ambiente(cena),
            ),
            _Cartao(
              chave: 'animacao',
              titulo: 'Animação',
              aberto: _abertos.contains('animacao'),
              aoAlternar: () => _alternar('animacao'),
              linhas: no == null
                  ? [_vazio('Esta cena não tem objeto para animar.')]
                  : _animacao(no),
            ),
            _Cartao(
              chave: 'propriedades',
              titulo: 'Propriedades',
              aberto: _abertos.contains('propriedades'),
              aoAlternar: () => _alternar('propriedades'),
              linhas: no == null
                  ? [_vazio('Esta cena não tem objeto.')]
                  : _propriedades(cena, no),
            ),
          ],
        );
      },
    ),
  );

  // ------------------------------------------------------ qual objeto

  /// AS FICHAS DOS OBJETOS — as mesmas pilulas da linha de escolha do
  /// painel de Efeitos. Escolher aqui move o gizmo do palco, porque os
  /// dois leem [noDaCenaSelecionadoProvider].
  Widget _escolhaDeObjeto(List<SceneNode> objetos, SceneNode? escolhido) =>
      Padding(
        padding: const EdgeInsets.only(bottom: 4, top: 4),
        child: _Pilulas<SceneNode>(
          valores: objetos,
          nome: (n) => n.name,
          escolhido: escolhido,
          chave: (n) => ValueKey('cena3d-objeto-${n.id}'),
          aoEscolher: (n) =>
              ref.read(noDaCenaSelecionadoProvider.notifier).state = n.id,
        ),
      );

  // ------------------------------------------------------- transformar

  List<Widget> _transformar(SceneNode n, Duration t, Duration local) => [
    _CabecaDeGrupo(rotulo: 'Posição', chave: 'cena3d-grupo-posicao'),
    _doNo(n, PropDoNo.x, 'X', t, local, -20000, 20000, 1, 4, padrao: 0),
    _doNo(n, PropDoNo.y, 'Y', t, local, -20000, 20000, 1, 4, padrao: 0),
    _doNo(n, PropDoNo.z, 'Z', t, local, -20000, 20000, 1, 4, padrao: 0),
    _CabecaDeGrupo(rotulo: 'Rotação', chave: 'cena3d-grupo-rotacao'),
    _doNo(
      n,
      PropDoNo.giroX,
      'X',
      t,
      local,
      -1440,
      1440,
      1,
      0.8,
      unidade: '°',
      padrao: 0,
    ),
    _doNo(
      n,
      PropDoNo.giroY,
      'Y',
      t,
      local,
      -1440,
      1440,
      1,
      0.8,
      unidade: '°',
      padrao: 0,
    ),
    _doNo(
      n,
      PropDoNo.giroZ,
      'Z',
      t,
      local,
      -1440,
      1440,
      1,
      0.8,
      unidade: '°',
      padrao: 0,
    ),
    _CabecaDeGrupo(rotulo: 'Escala', chave: 'cena3d-grupo-escala'),
    // A UNIFORME PRIMEIRO: e a que quase sempre se quer, e a unica que
    // desce pela cadeia de pais. As tres de eixo esticam so este objeto.
    _doNo(
      n,
      PropDoNo.escala,
      'Uniforme',
      t,
      local,
      1,
      2000,
      0,
      8,
      unidade: '%',
      fator: 100,
      padrao: 100,
    ),
    _doNo(
      n,
      PropDoNo.escalaX,
      'X',
      t,
      local,
      1,
      2000,
      0,
      8,
      unidade: '%',
      fator: 100,
      padrao: 100,
    ),
    _doNo(
      n,
      PropDoNo.escalaY,
      'Y',
      t,
      local,
      1,
      2000,
      0,
      8,
      unidade: '%',
      fator: 100,
      padrao: 100,
    ),
    _doNo(
      n,
      PropDoNo.escalaZ,
      'Z',
      t,
      local,
      1,
      2000,
      0,
      8,
      unidade: '%',
      fator: 100,
      padrao: 100,
    ),
    _CabecaDeGrupo(rotulo: 'Tamanho', chave: 'cena3d-grupo-tamanho'),
    _linha(
      'Tamanho',
      n.size,
      1,
      2000,
      (v) => _c.setSceneNodeSize(widget.sceneId, n.id, v),
    ),
    _Acoes(
      botoes: [
        (
          'cena3d-enquadrar',
          'Enquadrar',
          () => _c.frameSceneNode(widget.sceneId, n.id),
        ),
        (
          'cena3d-apontar',
          'Apontar a câmera',
          () => _c.focusCameraOnNode(widget.sceneId, n.id),
        ),
      ],
    ),
  ];

  /// UMA TRILHA DO OBJETO, na linha da casa e com o losango da casa.
  ///
  /// Mexer no valor NAO cria keyframe (a regra do app inteiro,
  /// `docs/keyframe-explicito.md`); o losango e que poe e tira.
  Widget _doNo(
    SceneNode n,
    PropDoNo p,
    String rotulo,
    Duration t,
    Duration local,
    double minimo,
    double maximo,
    int casas,
    double porPixel, {
    String unidade = '',
    double fator = 1,
    double? padrao,
  }) {
    final tempos = _c.sceneNodeKeyframeTimes(n, p);
    return ParameterRow(
      key: ValueKey('cena3d-linha-${p.name}'),
      valueKey: ValueKey('cena3d-valor-${p.name}'),
      label: rotulo,
      value: _c.sceneNodeValueAt(n, p, local) * fator,
      min: minimo,
      max: maximo,
      decimals: casas,
      unit: unidade,
      unitsPerPixel: porPixel,
      keyframe: KeyframeState(
        animated: tempos.isNotEmpty,
        here: tempos.any(
          (x) => (x - local).abs() < const Duration(milliseconds: 8),
        ),
        onToggle: () {
          _c.toggleSceneNodeKeyframe(widget.sceneId, n.id, p, t);
          setState(() {});
        },
      ),
      onReset: padrao == null
          ? null
          : () => _c.editSceneNodeProp(
              widget.sceneId,
              n.id,
              p,
              t,
              padrao / fator,
            ),
      onChanged: (v) {
        // GESTO CONTINUO: cada passo avisa o resto do editor que o dedo
        // esta na tela (o preview cede qualidade enquanto isso).
        Interacao.marcar();
        _c.editSceneNodeProp(
          widget.sceneId,
          n.id,
          p,
          t,
          v.clamp(minimo, maximo).toDouble() / fator,
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
        ParameterCustomRow(
          label: 'Origem',
          height: 46,
          child: _Pilulas<bool>(
            valores: const [true, false],
            nome: (v) => v ? 'Do arquivo' : 'Próprio',
            escolhido: n.useModelMaterials,
            chave: (v) =>
                ValueKey('cena3d-material-${v ? 'arquivo' : 'proprio'}'),
            aoEscolher: (v) => _c.updateSceneNode(
              widget.sceneId,
              n.id,
              (x) => x.copyWith(useModelMaterials: v),
            ),
          ),
        ),
      if (n.texto3d != null)
        _vazio('O metal e o chanfro das letras ficam na ficha do Texto 3D.'),
      if (proprio) ...[
        _CabecaDeGrupo(rotulo: 'Predefinição', chave: 'cena3d-grupo-preset'),
        _EnvolveWrap(
          child: _Pilulas<MaterialPreset3D>(
            valores: MaterialPreset3D.values,
            nome: materialPresetLabel,
            escolhido: null,
            chave: (p) => ValueKey('cena3d-preset-${p.name}'),
            aoEscolher: (p) =>
                _c.applySceneNodeMaterialPreset(widget.sceneId, n.id, p),
          ),
        ),
        _CabecaDeGrupo(rotulo: 'Superfície', chave: 'cena3d-grupo-superficie'),
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
        _linha(
          'Opacidade',
          m.opacity * 100,
          0,
          100,
          (v) => mat((x) => x.copyWith(opacity: v / 100)),
          unidade: '%',
        ),
        _CabecaDeGrupo(rotulo: 'Tipo', chave: 'cena3d-grupo-tipo'),
        _EnvolveWrap(
          child: _Pilulas<MaterialKind>(
            valores: MaterialKind.values,
            nome: _rotuloDoMaterial,
            escolhido: m.kind,
            chave: (k) => ValueKey('cena3d-tipo-${k.name}'),
            aoEscolher: (k) => mat((x) => x.copyWith(kind: k)),
          ),
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
        ParameterToggleRow(
          label: 'Face dupla',
          value: m.doubleSided,
          valueKey: const ValueKey('cena3d-face-dupla'),
          onChanged: (v) => mat((x) => x.copyWith(doubleSided: v)),
        ),
      ],
    ];
  }

  // -------------------------------------------------------- iluminacao

  List<Widget> _iluminacao(Scene3D cena, Duration t, Duration local) {
    final luzes = cena.lights;
    final luz =
        luzes.where((l) => l.id == _luzEscolhida).firstOrNull ??
        luzes.firstOrNull;
    return [
      if (luzes.length > 1) ...[
        _CabecaDeGrupo(rotulo: 'Qual luz', chave: 'cena3d-grupo-qual-luz'),
        // FILEIRA INTEIRA, e nao uma linha com rotulo a esquerda: a lista
        // de luzes cresce e um Wrap apertado num canto estoura a linha
        // num telefone de 375 px.
        _EnvolveWrap(
          child: _Pilulas<Light3D>(
            valores: luzes,
            nome: (l) => _rotuloDaLuz(l.kind),
            escolhido: luz,
            chave: (l) => ValueKey('cena3d-luz-${l.id}'),
            aoEscolher: (l) => setState(() => _luzEscolhida = l.id),
          ),
        ),
      ],
      if (luz == null)
        _vazio('Sem luzes. O ambiente ainda ilumina a cena.')
      else ...[
        _CabecaDeGrupo(rotulo: 'Tipo', chave: 'cena3d-grupo-luz-tipo'),
        _EnvolveWrap(
          child: _Pilulas<Light3DKind>(
            valores: Light3DKind.values,
            nome: _rotuloDaLuz,
            escolhido: luz.kind,
            chave: (k) => ValueKey('cena3d-luz-tipo-${k.name}'),
            aoEscolher: (k) =>
                _c.setSceneLightKind(widget.sceneId, luz.id, k),
          ),
        ),
        _CabecaDeGrupo(rotulo: 'Luz', chave: 'cena3d-grupo-luz'),
        _cor(
          'Cor',
          luz.color,
          (c) => _c.setSceneLightColor(widget.sceneId, luz.id, c),
          const ValueKey('cena3d-luz-cor'),
        ),
        _daLuz(luz, t, local),
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
        ParameterToggleRow(
          label: 'Sombra',
          value: luz.castsShadow,
          valueKey: const ValueKey('cena3d-luz-sombra'),
          onChanged: (v) =>
              _c.setSceneLightShadow(widget.sceneId, luz.id, v),
        ),
      ],
      _Acoes(
        botoes: [
          (
            'cena3d-add-luz',
            'Adicionar luz',
            () => _c.addSceneLight(widget.sceneId, Light3DKind.point),
          ),
          if (luz != null)
            (
              'cena3d-remove-luz',
              'Remover luz',
              () {
                _c.removeSceneLight(widget.sceneId, luz.id);
                setState(() => _luzEscolhida = null);
              },
            ),
        ],
      ),
    ];
  }

  /// A INTENSIDADE E A UNICA TRILHA ANIMAVEL DE UMA LUZ — e tem losango,
  /// como o resto do app.
  Widget _daLuz(Light3D luz, Duration t, Duration local) {
    final tempos = _c.sceneLightKeyframeTimes(luz, PropDaLuz.intensidade);
    return ParameterRow(
      key: const ValueKey('cena3d-linha-intensidade'),
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
          (x) => (x - local).abs() < const Duration(milliseconds: 8),
        ),
        onToggle: () {
          _c.toggleSceneLightKeyframe(
            widget.sceneId,
            luz.id,
            PropDaLuz.intensidade,
            t,
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
          t,
          v.clamp(0, 500) / 100,
        );
      },
    );
  }

  // ---------------------------------------------------------- ambiente

  List<Widget> _ambiente(Scene3D cena) => [
    _CabecaDeGrupo(rotulo: 'Estúdio', chave: 'cena3d-grupo-estudio'),
    _EnvolveWrap(
      child: _Pilulas<EnvironmentKind>(
        valores: EnvironmentKind.values,
        nome: environmentLabel,
        escolhido: cena.environment,
        chave: (k) => ValueKey('cena3d-ambiente-${k.name}'),
        aoEscolher: (k) => _c.setSceneEnvironment(widget.sceneId, k),
      ),
    ),
    _CabecaDeGrupo(rotulo: 'Luz do mundo', chave: 'cena3d-grupo-mundo'),
    _linha(
      'Reflexo',
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

  // ---------------------------------------------------------- animacao

  /// A ANIMACAO QUE VEIO DENTRO DO ARQUIVO (GLB/FBX). Nao ha controle
  /// inventado: quando o modelo nao traz clipe, a ficha diz isso em vez
  /// de oferecer reguas que nao mexem em nada.
  List<Widget> _animacao(SceneNode n) {
    final nomes = n.modelAsset?.clipNames ?? const <String>[];
    if (nomes.isEmpty) {
      return [
        _vazio(
          'Este objeto não traz animação própria. '
          'Anime pelos losangos da ficha Transformar.',
        ),
      ];
    }
    final movimento = n.modelMotion;
    void mexer(ModelMotion3D Function(ModelMotion3D) f) =>
        _c.setSceneNodeMotion(widget.sceneId, n.id, f(n.modelMotion));
    return [
      _CabecaDeGrupo(rotulo: 'Clipe', chave: 'cena3d-grupo-clipe'),
      _EnvolveWrap(
        child: _Pilulas<int>(
          valores: [-1, for (var i = 0; i < nomes.length; i++) i],
          nome: (i) => i < 0 ? 'Parado' : nomes[i],
          escolhido: movimento.clip,
          chave: (i) => ValueKey('cena3d-clipe-$i'),
          aoEscolher: (i) => mexer((m) => m.copyWith(clip: i)),
        ),
      ),
      _CabecaDeGrupo(rotulo: 'Tempo', chave: 'cena3d-grupo-tempo-clipe'),
      _linha(
        'Velocidade',
        movimento.speed * 100,
        -400,
        400,
        (v) => mexer((m) => m.copyWith(speed: v / 100)),
        unidade: '%',
      ),
      _linha(
        'Começar em',
        movimento.offset,
        0,
        600,
        (v) => mexer((m) => m.copyWith(offset: v)),
        unidade: 's',
        casas: 2,
      ),
      ParameterToggleRow(
        label: 'Repetir',
        value: movimento.loop,
        valueKey: const ValueKey('cena3d-clipe-loop'),
        onChanged: (v) => mexer((m) => m.copyWith(loop: v)),
      ),
    ];
  }

  // ------------------------------------------------------ propriedades

  List<Widget> _propriedades(Scene3D cena, SceneNode n) {
    final modelo = n.modelAsset;
    final pais = [
      for (final outro in cena.nodes)
        if (outro.id != n.id) outro,
    ];
    return [
      ParameterCustomRow(
        label: 'Nome',
        child: _Texto(
          chave: const ValueKey('cena3d-nome'),
          texto: n.name,
          aoTocar: () async {
            final novo = await _pedirNome(context, n.name);
            if (novo == null || novo.trim().isEmpty) return;
            _c.renameSceneNode(widget.sceneId, n.id, novo.trim());
          },
        ),
      ),
      ParameterToggleRow(
        label: 'Visível',
        value: n.visible,
        valueKey: const ValueKey('cena3d-visivel'),
        onChanged: (v) => _c.setSceneNodeVisible(widget.sceneId, n.id, v),
      ),
      ParameterToggleRow(
        label: 'Bloqueado',
        value: n.locked,
        valueKey: const ValueKey('cena3d-bloqueado'),
        onChanged: (v) => _c.setSceneNodeLocked(widget.sceneId, n.id, v),
      ),
      _cor(
        'Etiqueta',
        n.colorTag,
        (c) => _c.setSceneNodeColorTag(widget.sceneId, n.id, c),
        const ValueKey('cena3d-etiqueta'),
      ),
      if (pais.isNotEmpty) ...[
        _CabecaDeGrupo(rotulo: 'Pai na cena', chave: 'cena3d-grupo-pai'),
        _EnvolveWrap(
          child: _Pilulas<String?>(
            valores: [null, for (final p in pais) p.id],
            nome: (id) => id == null
                ? 'Sem pai'
                : (cena.nodeById(id)?.name ?? 'Objeto'),
            escolhido: n.parentId,
            chave: (id) => ValueKey('cena3d-pai-${id ?? 'nenhum'}'),
            aoEscolher: (id) =>
                _c.setSceneNodeParent(widget.sceneId, n.id, id),
          ),
        ),
      ],
      if (modelo != null) ...[
        _CabecaDeGrupo(
          rotulo: 'Nível de detalhe',
          chave: 'cena3d-grupo-modelo',
        ),
        _EnvolveWrap(
          child: _Pilulas<MeshLod3D>(
            valores: MeshLod3D.values,
            nome: _rotuloDoDetalhe,
            escolhido: n.lod,
            chave: (l) => ValueKey('cena3d-lod-${l.name}'),
            aoEscolher: (l) => _c.setSceneNodeLod(widget.sceneId, n.id, l),
          ),
        ),
        _ficha('Triângulos', _milhar(modelo.triangleCount)),
        if (!n.credit.isEmpty)
          _ficha(
            'Crédito',
            n.credit.author ?? n.credit.source ?? n.credit.license ?? '—',
          ),
      ] else
        _linha(
          'Subdivisões',
          n.subdivisions.toDouble(),
          0,
          4,
          (v) => _c.setSceneNodeSubdivisions(
            widget.sceneId,
            n.id,
            v.round(),
          ),
        ),
      _Acoes(
        botoes: [
          (
            'cena3d-duplicar',
            'Duplicar',
            () {
              final novo = _c.duplicateSceneNode(widget.sceneId, n.id);
              if (novo.isNotEmpty) {
                ref.read(noDaCenaSelecionadoProvider.notifier).state = novo;
              }
            },
          ),
          (
            'cena3d-apagar',
            'Apagar objeto',
            () {
              _c.removeSceneNode(widget.sceneId, n.id);
              ref.read(noDaCenaSelecionadoProvider.notifier).state = null;
            },
          ),
        ],
      ),
    ];
  }

  Future<String?> _pedirNome(BuildContext context, String inicial) {
    final campo = TextEditingController(text: inicial);
    return showCupertinoDialog<String>(
      context: context,
      builder: (dialogContext) => CupertinoAlertDialog(
        title: const AppText('Nome do objeto'),
        content: Padding(
          padding: const EdgeInsets.only(top: 12),
          child: CupertinoTextField(
            key: const ValueKey('cena3d-nome-campo'),
            controller: campo,
            autofocus: true,
            onSubmitted: (v) => Navigator.of(dialogContext).pop(v),
          ),
        ),
        actions: [
          CupertinoDialogAction(
            onPressed: () => Navigator.of(dialogContext).pop(),
            child: const AppText('Cancelar'),
          ),
          CupertinoDialogAction(
            isDefaultAction: true,
            onPressed: () => Navigator.of(dialogContext).pop(campo.text),
            child: const AppText('Salvar'),
          ),
        ],
      ),
    );
  }

  // --------------------------------------------------------------- pecas

  /// UMA LINHA NUMERICA SEM LOSANGO: o que nao anima (tamanho, material,
  /// ambiente). Mesma regua, mesmo campo de valor, mesmo teclado.
  Widget _linha(
    String rotulo,
    double valor,
    double minimo,
    double maximo,
    ValueChanged<double> aoMudar, {
    String unidade = '',
    int casas = 0,
  }) => ParameterRow(
    key: ValueKey('cena3d-linha-${_slug(rotulo)}'),
    valueKey: ValueKey('cena3d-valor-${_slug(rotulo)}'),
    label: rotulo,
    value: valor,
    min: minimo,
    max: maximo,
    unit: unidade,
    decimals: casas,
    unitsPerPixel: (maximo - minimo) / 260,
    onChanged: (v) {
      Interacao.marcar();
      aoMudar(v.clamp(minimo, maximo).toDouble());
    },
  );

  /// A COR SE ESCOLHE NO SELETOR DA CASA, e ela chega VIVA: o palco atras
  /// da folha mostra o resultado enquanto o dedo anda.
  Widget _cor(
    String rotulo,
    Color atual,
    ValueChanged<Color> aoMudar,
    Key chave,
  ) => ParameterColorRow(
    label: rotulo,
    color: atual,
    valueKey: chave,
    onTap: () async {
      final nova = await showColorPicker(
        context,
        initial: atual,
        withAlpha: false,
        onChanged: aoMudar,
      );
      if (nova != null) aoMudar(nova);
    },
  );

  Widget _ficha(String rotulo, String valor) => ParameterCustomRow(
    label: rotulo,
    child: Align(
      alignment: Alignment.centerLeft,
      child: AppText(
        valor,
        maxLines: 1,
        overflow: TextOverflow.ellipsis,
        style: const TextStyle(fontSize: 12.5, color: AmColors.text),
      ),
    ),
  );

  Widget _vazio(String texto) => Padding(
    padding: const EdgeInsets.fromLTRB(6, 10, 6, 14),
    child: AppText(
      texto,
      style: const TextStyle(fontSize: 12.5, color: AmColors.muted),
    ),
  );

  /// A CHAVE DA LINHA sai do rotulo, como no resto do app: minusculas,
  /// sem acento no meio da chave e sem espaco.
  static String _slug(String s) => s
      .toLowerCase()
      .replaceAll(RegExp(r'[^a-z0-9]+'), '-')
      .replaceAll(RegExp(r'^-|-$'), '');

  static String _milhar(int v) {
    final s = '$v';
    final saida = StringBuffer();
    for (var i = 0; i < s.length; i++) {
      if (i > 0 && (s.length - i) % 3 == 0) saida.write('.');
      saida.write(s[i]);
    }
    return saida.toString();
  }
}

/// O CARTAO DE UM ASSUNTO — a mesma peca do painel de Efeitos: triangulo
/// que recolhe, nome em 17/w600, fundo [AmColors.panelHigh] e canto 14.
/// Recolhido, o corpo nem e construido.
class _Cartao extends StatelessWidget {
  const _Cartao({
    required this.chave,
    required this.titulo,
    required this.aberto,
    required this.aoAlternar,
    required this.linhas,
  });

  final String chave;
  final String titulo;
  final bool aberto;
  final VoidCallback aoAlternar;
  final List<Widget> linhas;

  @override
  Widget build(BuildContext context) => Container(
    key: ValueKey('cena3d-cartao-$chave'),
    margin: const EdgeInsets.only(bottom: 10),
    padding: const EdgeInsets.fromLTRB(10, 2, 6, 8),
    decoration: BoxDecoration(
      color: AmColors.panelHigh,
      borderRadius: BorderRadius.circular(14),
    ),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        SizedBox(
          height: 48,
          child: Semantics(
            button: true,
            expanded: aberto,
            label: titulo,
            child: Tocavel(
              key: ValueKey('cena3d-cabecalho-$chave'),
              encolhe: 1,
              onTap: aoAlternar,
              child: Row(
                children: [
                  Icon(
                    aberto
                        ? CupertinoIcons.arrowtriangle_down_fill
                        : CupertinoIcons.arrowtriangle_right_fill,
                    size: 13,
                    color: AmColors.text,
                  ),
                  const SizedBox(width: 12),
                  Flexible(
                    child: AppText(
                      titulo,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(
                        fontSize: 17,
                        fontWeight: FontWeight.w600,
                        color: AmColors.text,
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
        if (aberto)
          Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: linhas),
      ],
    ),
  );
}

/// A CABECA DE UM GRUPO dentro do cartao — a mesma divisoria discreta do
/// painel de Efeitos (triangulo pequeno, texto apagado, fio a direita).
/// Aqui ela nao abre nem fecha: o cartao ja e o que recolhe, e um segundo
/// nivel de dobra so esconderia numero.
class _CabecaDeGrupo extends StatelessWidget {
  const _CabecaDeGrupo({required this.rotulo, required this.chave});

  final String rotulo;
  final String chave;

  @override
  Widget build(BuildContext context) => SizedBox(
    key: ValueKey(chave),
    height: 36,
    child: Row(
      children: [
        Expanded(
          child: AppText(
            rotulo.toUpperCase(),
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: const TextStyle(
              fontSize: 12,
              fontWeight: FontWeight.w700,
              letterSpacing: .6,
              color: AmColors.muted,
            ),
          ),
        ),
        Container(height: 1, width: 40, color: AmColors.hairline),
        const SizedBox(width: 6),
      ],
    ),
  );
}

/// AS PILULAS DE ESCOLHA — as mesmas da linha de escolha do painel de
/// Efeitos (fundo [AmColors.campo], escolhida em [AmColors.accentDim]
/// com o texto em [AmColors.accent]).
class _Pilulas<T> extends StatelessWidget {
  const _Pilulas({
    required this.valores,
    required this.nome,
    required this.escolhido,
    required this.chave,
    required this.aoEscolher,
  });

  final List<T> valores;
  final String Function(T) nome;
  final T? escolhido;
  final Key Function(T) chave;
  final ValueChanged<T> aoEscolher;

  @override
  Widget build(BuildContext context) => Wrap(
    spacing: 6,
    runSpacing: 6,
    children: [
      for (final v in valores)
        Tocavel(
          key: chave(v),
          onTap: () => aoEscolher(v),
          child: Container(
            // ALVO DE DEDO: 34 px de altura, como o campo de valor da
            // linha de parametro.
            height: 34,
            alignment: Alignment.center,
            padding: const EdgeInsets.symmetric(horizontal: 11),
            decoration: BoxDecoration(
              color: v == escolhido ? AmColors.accentDim : AmColors.campo,
              borderRadius: BorderRadius.circular(8),
            ),
            child: AppText(
              nome(v),
              maxLines: 1,
              style: TextStyle(
                fontSize: 12,
                color: v == escolhido ? AmColors.accent : AmColors.text,
              ),
            ),
          ),
        ),
    ],
  );
}

/// Um bloco de pilulas que ocupa a largura do cartao (sem rotulo a
/// esquerda), com a mesma folga vertical das linhas.
class _EnvolveWrap extends StatelessWidget {
  const _EnvolveWrap({required this.child});

  final Widget child;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(0, 2, 6, 8),
    child: child,
  );
}

/// O CAMPO DE TEXTO DE UMA LINHA: mesma caixa do valor numerico, so que
/// o toque abre o dialogo de nome em vez do teclado de numero.
class _Texto extends StatelessWidget {
  const _Texto({
    required this.chave,
    required this.texto,
    required this.aoTocar,
  });

  final Key chave;
  final String texto;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => GestureDetector(
    key: chave,
    behavior: HitTestBehavior.opaque,
    onTap: aoTocar,
    child: Container(
      height: 34,
      alignment: Alignment.centerLeft,
      padding: const EdgeInsets.symmetric(horizontal: 10),
      decoration: BoxDecoration(
        color: AmColors.campo,
        borderRadius: BorderRadius.circular(8),
      ),
      child: AppText(
        texto,
        maxLines: 1,
        overflow: TextOverflow.ellipsis,
        style: const TextStyle(fontSize: 12.5, color: AmColors.text),
      ),
    ),
  );
}

/// A FILEIRA DE ACOES no pe de um cartao — o mesmo botao cheio do
/// "+ Adicionar efeito".
class _Acoes extends StatelessWidget {
  const _Acoes({required this.botoes});

  final List<(String, String, VoidCallback)> botoes;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(0, 8, 6, 4),
    child: Wrap(
      spacing: 8,
      runSpacing: 8,
      children: [
        for (final (chave, rotulo, aoTocar) in botoes)
          Tocavel(
            key: ValueKey(chave),
            haptico: true,
            onTap: aoTocar,
            child: Container(
              height: 40,
              alignment: Alignment.center,
              padding: const EdgeInsets.symmetric(horizontal: 14),
              decoration: BoxDecoration(
                color: AmColors.chip,
                borderRadius: BorderRadius.circular(12),
              ),
              child: AppText(
                rotulo,
                style: TextStyle(
                  fontSize: 13,
                  fontWeight: FontWeight.w600,
                  color: AmColors.action,
                ),
              ),
            ),
          ),
      ],
    ),
  );
}
