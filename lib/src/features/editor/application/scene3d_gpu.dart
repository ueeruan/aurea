import 'dart:async';
import 'dart:io';


import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/foundation.dart';
import 'package:flutter_scene/scene.dart' as fs;
import 'package:vector_math/vector_math.dart' as vm;

import '../domain/camera3d.dart';
import '../domain/element3d.dart';
import '../domain/environment_radiance.dart';
import '../domain/geometria_gpu.dart';
import '../domain/orcamento_render.dart';
import '../domain/panorama3d.dart';
import '../domain/scene3d.dart';
import '../domain/preview_quality.dart';
import 'motor3d_modo.dart';
import 'preview_stats.dart';
import 'qualidade3d_controller.dart';
import 'texture_cache.dart';
import 'registro_de_travadas.dart';
import 'perfil3d.dart';
import 'camera_ortografica.dart';
import 'fonte_de_malha.dart';

/// O MOTOR 3D EM GPU.
///
/// A cena do dominio (`Scene3D`, `SceneNode`, `Material3D`, `Light3D`,
/// `Camera3D`) continua sendo a verdade — e o que o editor edita, o que o
/// projeto salva, o que o pintor em CPU desenha. Esta classe e a PONTE:
/// traduz essa cena para o `flutter_scene` (Flutter GPU / Impeller), que
/// renderiza com buffer de profundidade, materiais fisicos, luz por
/// imagem, sombras em cascata, neblina e profundidade de campo — o que o
/// pintor em CPU nao tem como fazer.
///
/// O que fica do lado de ca: a geometria dos nos vira `MeshGeometry`
/// (uma primitiva por material), a transformacao de cada no vira a
/// matriz local, as texturas sobem para a GPU uma vez, as luzes viram
/// componentes, o ambiente vira ceu procedural ou mapa equiretangular.
/// Tudo e cacheado por assinatura: um quadro novo so mexe nas matrizes;
/// geometria e material so sao reconstruidos quando o que os descreve
/// muda.
///
/// A RECEITA DE QUALIDADE ([ReceitaDeQualidade]) entra em tudo que custa
/// memoria ou tempo de GPU: resolucao e numero de sombras, MSAA, bloom,
/// profundidade de campo, teto de textura, LOD e escala do alvo. Quem
/// escolhe a receita e o [ControladorDeQualidade3D], pelo orcamento do
/// aparelho e pelos sinais de pressao — este arquivo so obedece.
///
/// Quando o Flutter GPU nao esta disponivel (aparelho sem suporte, ou
/// os testes, que rodam em Skia) [Scene3DGpu.pronto] nunca vira true e
/// quem desenha e o pintor de sempre.
/// O QUE FAZ UMA SINCRONIA SER NECESSARIA.
///
/// O widget da cena 3D e reconstruido por muito mais motivo do que a
/// cena mudar: um painel que abre, uma textura que acabou de subir e
/// pediu repintura, o controlador de qualidade avisando o mesmo nivel de
/// novo, qualquer ancestral que se reconstroi. Cada uma dessas
/// reconstrucoes reandava a cena inteira — nos, luzes, ambiente, neblina
/// e pos-processamento — para chegar exatamente ao mesmo resultado.
///
/// Esta chave e o resumo do que, mudando, obriga a refazer o trabalho.
/// Duas chaves iguais significam um quadro identico ao anterior.
@immutable
class ChaveDeSincronia {
  const ChaveDeSincronia({
    required this.cena,
    required this.t,
    required this.rascunho,
    required this.nivel,
    required this.texturaMax,
  });

  /// A cena entra por IDENTIDADE, nao por conteudo.
  ///
  /// [Scene3D] e imutavel: editar produz um objeto novo. Entao o mesmo
  /// objeto e, por construcao, a mesma cena — e comparar conteudo
  /// custaria mais do que a sincronia que se quer evitar. O preco desta
  /// escolha e conservador na direcao certa: duas cenas de conteudo
  /// igual mas objetos diferentes sincronizam de novo, o que desperdica
  /// trabalho mas nunca mostra um quadro velho.
  final Scene3D? cena;

  /// O instante da linha do tempo.
  final Duration t;

  /// Rascunho muda o pos-processamento mesmo com a cena igual.
  final bool rascunho;

  /// O nivel de qualidade adaptativa.
  final Qualidade3D nivel;

  /// O teto de tamanho de textura da receita.
  final int texturaMax;

  bool mesmoQue(ChaveDeSincronia o) =>
      identical(cena, o.cena) &&
      t == o.t &&
      rascunho == o.rascunho &&
      nivel == o.nivel &&
      texturaMax == o.texturaMax;
}

class Scene3DGpu {
  Scene3DGpu();

  static Future<void>? _preparo;
  static bool _prontoParaRender = false;
  static bool _falhou = false;
  static String _motivo = '';
  // Uploads include an RGBA readback and mip generation. Serializing them
  // across views prevents a textured import from allocating all copies at once.
  static Future<void> _uploads = Future<void>.value();

  /// Carrega os shaders e recursos estaticos do motor. Falha em silencio
  /// (com log) onde nao ha GPU: [pronto] fica false e o pintor em CPU
  /// continua sendo usado.
  static Future<void> preparar() => _preparo ??= _prepararDeVerdade();

  static Future<void> _prepararDeVerdade() async {
    // Nos testes (Skia) e fora de iOS/Android o Flutter GPU nao existe:
    // nem tentar, para nao vazar a excecao do motor no laco de teste.
    if (kIsWeb ||
        Platform.environment.containsKey('FLUTTER_TEST') ||
        !(Platform.isIOS || Platform.isAndroid)) {
      _falhou = true;
      return;
    }
    // A MIGALHA: se a sessao anterior nao voltou de um quadro em GPU,
    // esta desenha em CPU. Ver Motor3DPreferencia.
    final pref = Motor3DPreferencia.instancia;
    if (pref != null && !pref.permiteGpu) {
      _falhou = true;
      _motivo = pref.motivoDeNaoTentar;
      return;
    }
    try {
      // initializeStaticResources engole a falha (sem GPU) e devolve
      // normalmente; a verdade esta em isReadyToRender.
      await fs.Scene.initializeStaticResources();
      _prontoParaRender = fs.Scene.isReadyToRender;
      _falhou = !_prontoParaRender;
      if (_falhou) {
        _motivo = 'os recursos do motor nao carregaram';
        debugPrint('Motor 3D em GPU indisponivel; fica o pintor em CPU.');
      }
    } catch (e, st) {
      _falhou = true;
      _motivo = '$e';
      debugPrint('Motor 3D em GPU indisponivel; pintor em CPU: $e\n$st');
    }
  }

  /// COMO A CENA 3D ESTA SENDO DESENHADA, em duas letras.
  ///
  /// Isto aparece no app de proposito. Um motor que cai para o pintor em
  /// CPU nao pode ser segredo: a diferenca entre os dois e a diferenca
  /// entre uma cena que roda e uma que engasga, e quem esta com o
  /// aparelho na mao e a unica pessoa que consegue ver qual dos dois
  /// esta valendo.
  static String get comoDesenha => _prontoParaRender
      ? 'GPU'
      : _falhou
      ? 'CPU'
      : '...';

  /// Por que caiu para o pintor em CPU, quando caiu.
  static String get motivo => _motivo;

  /// O motor esta pronto para desenhar?
  static bool get pronto => _prontoParaRender;

  /// A preparacao ja foi tentada e falhou (sem GPU).
  static bool get indisponivel => _falhou;

  final fs.Scene cena = fs.Scene();

  final Map<String, _NoGpu> _nos = {};
  final Map<String, fs.Texture2D> _texturas = {};
  final Map<String, Future<void>> _texturasACaminho = {};
  final List<fs.Node> _luzes = [];

  /// Um objeto de luz do motor por luz da cena (nulo para as que nao
  /// viram objeto, como a ambiente), na ordem de `scene.lights`.
  final List<Object?> _luzObjetos = [];
  String? _assinaturaLuzes;
  String? _chaveAmbiente;

  /// O mapa de radiancia de cada tipo de ambiente, subido uma vez so.
  /// Estatico porque o mapa depende so do tipo e a GPU nao o libera:
  /// duas cenas abertas na mesma sessao dividem o mesmo atlas.
  static final Map<EnvironmentKind, fs.EnvironmentMap> _mapasDeAmbiente = {};

  /// O mesmo, para panoramas de arquivo, guardados pelo caminho.
  static final Map<String, fs.EnvironmentMap> _panoramasDeAmbiente = {};

  /// Os mapas que ainda estao subindo. Duas sincronias pedindo o mesmo
  /// tipo antes de a primeira terminar dividem o mesmo trabalho — sem
  /// isto, alternar entre ambientes gerava a mesma radiancia varias
  /// vezes antes de qualquer uma chegar ao cache.
  static final Map<EnvironmentKind, Future<fs.EnvironmentMap>> _mapasACaminho =
      {};

  /// O ceu procedural desta cena. Guardado para o bake ser fatiado a
  /// partir da segunda vez — ver [_sincronizarAmbiente].
  fs.SkyEnvironment? _ceuDoAmbiente;
  int _epocaAmbiente = 0;
  bool _descartado = false;
  fs.Node? _probeNode;
  fs.ReflectionProbeComponent? _probe;
  ReflectionProbe3D? _probeSettings;

  ReceitaDeQualidade _receita = ReceitaDeQualidade.alta;
  int _capDasTexturas = ReceitaDeQualidade.alta.texturaMax;
  fs.AntiAliasingMode? _aaAplicado;
  Scene3D? _ultimaCena;
  Duration _ultimoT = Duration.zero;

  /// A chave da ultima sincronia feita de verdade.
  ChaveDeSincronia? _ultimaChave;
  Scene3D? _cenaRegistrada;
  ui.Size _areaRegistrada = ui.Size.zero;
  bool _dofPedido = false;
  int _tilesDeSombra = 0;
  ui.Size _ultimoAlvo = ui.Size.zero;
  double _ultimaEscala = 1;

  /// A receita em vigor neste motor.
  ReceitaDeQualidade get receita => _receita;

  /// Os objetos de luz do motor, para os testes provarem que uma
  /// intensidade animada NAO recria a luz (e o mapa de sombra dela).
  @visibleForTesting
  List<Object?> get luzesDoMotor => List.unmodifiable(_luzObjetos);

  /// Sincroniza a cena da GPU com [scene] no instante [t].
  ///
  /// Devolve na hora com o que ja esta pronto. Texturas e o ambiente por
  /// imagem chegam depois, em segundo plano, e chamam [onMudou] para o
  /// quadro ser redesenhado.
  ///
  /// [receita] e o nivel de qualidade; sem ela vale a do controlador.
  void sincronizar(
    Scene3D scene,
    Duration t, {
    VoidCallback? onMudou,
    bool rascunho = false,
    ReceitaDeQualidade? receita,
  }) {
    if (_descartado) return;
    final nova = receita ?? ControladorDeQualidade3D.instancia.receita;
    final mesmoNivel = nova.nivel == _receita.nivel;
    // NADA MUDOU: nao ha o que refazer. Ver [ChaveDeSincronia].
    //
    // Textura que sobe depois nao depende desta porta: ela se aplica
    // direto no material e so pede repintura.
    final chave = ChaveDeSincronia(
      cena: scene,
      t: t,
      rascunho: rascunho,
      nivel: nova.nivel,
      texturaMax: nova.texturaMax,
    );
    final anterior = _ultimaChave;
    if (anterior != null &&
        chave.mesmoQue(anterior) &&
        _capDasTexturas == _receita.texturaMax) {
      Perfil3D.contar('sincronia.evitada');
      return;
    }
    if (!mesmoNivel) {
      _receita = nova;
      // Sombras e MSAA mudam com o nivel: as luzes sao refeitas.
      _assinaturaLuzes = null;
    }
    _ultimaChave = chave;
    _ultimaCena = scene;
    _ultimoT = t;
    _aplicarAntialias();
    // O TETO DE TEXTURA SO VALE PARA O QUE AINDA NAO SUBIU.
    //
    // Antes, mudar o teto rederrubava todas as texturas e subia todas de
    // novo no tamanho novo. A intencao era boa e o efeito era o oposto:
    // o flutter_scene NAO LIBERA memoria de GPU (bdero/flutter_scene#285
    // — texturas, cenas e shaders ficam retidos ate o processo morrer),
    // entao as antigas continuavam ocupando lugar e as novas se somavam
    // a elas.
    //
    // Isso fechava um ciclo que so piorava: pressao de memoria faz o
    // controlador baixar o teto, baixar o teto subia TUDO de novo, subir
    // tudo de novo aumentava a memoria, e a memoria maior baixava o teto
    // outra vez — ate o sistema matar o aplicativo. E a morte por
    // memoria deixa a mesma migalha de uma queda do motor, o que jogava
    // as tres sessoes seguintes no pintor de CPU. O relato "trava mesmo
    // em celular potente" nasce ai.
    //
    // Enquanto o motor nao souber liberar, cada textura sobe UMA VEZ, no
    // teto que valia quando ela foi pedida. O teto novo continua valendo
    // para as proximas.
    _capDasTexturas = _receita.texturaMax;
    // AS MARCAS COBREM A SINCRONIA INTEIRA, e nao so os nos.
    //
    // Antes so `nos` era marcado. Se o tempo estivesse no ambiente (que
    // gera o mapa de radiancia) ou nas luzes (que refazem as sombras em
    // cascata), a travada aparecia como `nada marcado` — e `nada
    // marcado` so vale como pista se o que sobrou for pequeno.
    RegistroDeTravadas.marcando('cena 3D: sincronizar', () {
      RegistroDeTravadas.marcando(
        'cena 3D: sincronizar > nos',
        () => Perfil3D.fase(
          'sincronia.nos',
          () => _sincronizarNos(scene, t, onMudou),
        ),
      );
      RegistroDeTravadas.marcando(
        'cena 3D: sincronizar > luzes',
        () =>
            Perfil3D.fase('sincronia.luzes', () => _sincronizarLuzes(scene, t)),
      );
      RegistroDeTravadas.marcando(
        'cena 3D: sincronizar > ambiente',
        () => Perfil3D.fase(
          'sincronia.ambiente',
          () => _sincronizarAmbiente(scene, t, onMudou),
        ),
      );
      RegistroDeTravadas.marcando('cena 3D: sincronizar > nevoa e pos', () {
        _sincronizarReflexos(scene);
        _sincronizarNevoa(scene);
        _sincronizarPos(scene, rascunho);
      });
    });
    Perfil3D.quadro();
  }

  /// Camera do motor a partir da camera resolvida do dominio, para uma
  /// area de [tamanho] pixels. O campo de visao do dominio e HORIZONTAL;
  /// o do motor, vertical.
  fs.Camera camera(RenderCamera cam, ui.Size tamanho) {
    final basis = cameraBasis(cam);
    final aspecto = tamanho.height <= 0
        ? 16 / 9
        : tamanho.width / tamanho.height;
    // VISTA FIXA (Frente, Topo, Lado): lente ortografica de verdade, na
    // GPU. Ate aqui ela nao existia no motor e a vista inteira caia no
    // pintor de processador — a travada que a bancada mediu em ate 44 ms
    // por quadro.
    if (cam.orthographic) {
      return cameraOrtograficaDoMotor(
        cam,
        tamanho.height <= 0 ? 700 : tamanho.height,
      );
    }
    final fovX = cam.fovRadians;
    final fovY = 2 * math.atan(math.tan(fovX / 2) / aspecto);
    return fs.PerspectiveCamera(
      position: _v(cam.position),
      target: _v(cam.target),
      up: _v(basis.up),
      fovRadiansY: fovY.clamp(0.05, 3.0),
      fovNear: math.max(1.0, cam.near),
      // Uma cena espacial poe estrelas a dezenas de milhares de
      // unidades; o plano distante do dominio e 100 mil.
      fovFar: math.min(cam.far, 120000),
    );
  }

  /// Profundidade de campo do motor a partir da da camera do dominio.
  ///
  /// A receita manda: abaixo de "media" a profundidade de campo nao
  /// entra — sao dois alvos a mais por quadro.
  void configurarProfundidadeDeCampo(
    Camera3D camera,
    Duration t, {
    bool rascunho = false,
  }) {
    final dof = camera.dof;
    _dofPedido = dof.enabled;
    final ligada = dof.enabled && !rascunho && _receita.dof;
    cena.depthOfField.enabled = ligada;
    if (!ligada) return;
    final focal = camera.focalLength.valueAt(t);
    cena.depthOfField
      ..focusDistance = math.max(1.0, dof.focusDistance.valueAt(t))
      ..fStop = dof.fStopFor(focal, t).clamp(0.5, 32.0)
      ..blurScale = (dof.blurLevel.valueAt(t) / 100).clamp(0.0, 2.0)
      ..bladeCount = irisSides(dof.irisShape)
      ..bladeRotation = dof.irisRotation.valueAt(t) * math.pi / 180;
  }

  /// Desenha a cena em [canvas], dentro de [area].
  ///
  /// No preview a escala do alvo e a menor entre a do rascunho (720 no
  /// lado maior enquanto toca), a da receita e o teto de 1080/1440. Na
  /// EXPORTACAO a resolucao e a da composicao, salvo quando nem a
  /// receita de emergencia cabe na memoria — ai a escala desce ate
  /// caber, e o quadro e ampliado de volta: um quadro menos nitido vale
  /// mais que um app morto no meio da exportacao.
  void desenhar(
    ui.Canvas canvas,
    ui.Rect area,
    fs.Camera camera, {
    bool rascunho = false,
    bool exporting = false,
    double previewScale = 1,
  }) {
    if (!pronto) return;
    _registrarSeMudou(area.size);
    final controlador = ControladorDeQualidade3D.instancia;
    double escala;
    if (exporting) {
      final ex = controlador.paraExportacao(area.width, area.height);
      if (ex.nivel != _receita.nivel) {
        _receita = ReceitaDeQualidade.de(ex.nivel);
        _assinaturaLuzes = null;
        final cena3d = _ultimaCena;
        if (cena3d != null) {
          _aplicarAntialias();
          _sincronizarLuzes(cena3d, _ultimoT);
          _sincronizarPos(cena3d, false);
        }
      }
      escala = ex.escala;
    } else {
      escala = math.min(
        scenePreviewScale(
          area.width,
          area.height,
          interacting: rascunho,
          exporting: false,
        ),
        escalaDoPreview(area.width, area.height, _receita),
      );
    }
    if (!exporting) escala *= previewScale.clamp(.125, 1);
    cena.renderScale = escala;
    _ultimoAlvo = area.size;
    _ultimaEscala = escala;
    cena.render(camera, canvas, viewport: area, pixelRatio: 1.0);
    _publicarEstatisticas();
  }

  /// Conta a cena para o controlador quando a cena (ou a area) muda.
  void _registrarSeMudou(ui.Size area) {
    final cena3d = _ultimaCena;
    if (cena3d == null || area.isEmpty) return;
    final mudouArea =
        (area.width - _areaRegistrada.width).abs() > 2 ||
        (area.height - _areaRegistrada.height).abs() > 2;
    if (identical(cena3d, _cenaRegistrada) && !mudouArea) return;
    _cenaRegistrada = cena3d;
    _areaRegistrada = area;
    ControladorDeQualidade3D.instancia.registrarCena(
      PerfilDaCena.de(cena3d, lod: _receita.lod).comDof(_dofPedido),
      area.width,
      area.height,
    );
  }

  void _publicarEstatisticas() {
    var tri = 0;
    var chamadas = 0;
    for (final n in _nos.values) {
      tri += n.triangulos;
      chamadas += n.geometrias.length;
    }
    final e = Estatisticas3D(
      motor: 'GPU',
      nivel: _receita.nivel,
      triangulos: tri,
      chamadas: chamadas,
      texturas: _texturas.length,
      tilesDeSombra: _tilesDeSombra,
      larguraPx: (_ultimoAlvo.width * _ultimaEscala).round(),
      alturaPx: (_ultimoAlvo.height * _ultimaEscala).round(),
      escala: _ultimaEscala,
    );
    if (PreviewStats.cena3d.value != e) PreviewStats.cena3d.value = e;
  }

  void _aplicarAntialias() {
    final modo = _receita.msaa
        ? fs.AntiAliasingMode.auto
        : fs.AntiAliasingMode.fxaa;
    if (_aaAplicado == modo) return;
    _aaAplicado = modo;
    cena.antiAliasingMode = modo;
  }

  void descartar() {
    _descartado = true;
    _epocaAmbiente++;
    if (_probeNode != null) cena.remove(_probeNode!);
    _probeNode = null;
    _probe = null;
    for (final n in _nos.values) {
      n.remover(cena);
    }
    _nos.clear();
    for (final l in _luzes) {
      cena.remove(l);
    }
    _luzes.clear();
    _luzObjetos.clear();
    _texturas.clear();
    _texturasACaminho.clear();
    cena.environment = null;
    cena.skyEnvironment = null;
    cena.skybox = null;
    cena.directionalLight = null;
  }

  // ------------------------------------------------------------- nos

  void _sincronizarNos(Scene3D scene, Duration t, VoidCallback? onMudou) {
    final vivos = <String>{};
    for (final node in scene.nodes) {
      if (!node.visible || node.isNull) continue;
      Perfil3D.contar('nos.vistos');
      final xf = Perfil3D.fase(
        'sincronia.transform',
        () => resolveNodeTransform(scene, node, t),
      );
      final malha = Perfil3D.fase('sincronia.malha', () => _malhaDe(node, t));
      if (malha == null) continue;
      vivos.add(node.id);
      var g = _nos[node.id];
      if (g == null || g.assinatura != malha.assinatura) {
        Perfil3D.contar('nos.reconstruidos');
        g?.remover(cena);
        g = Perfil3D.fase<_NoGpu>(
          'sincronia.construir',
          () => _construir(node, malha, onMudou),
        );
        _nos[node.id] = g;
      } else if (malha.dinamica && !identical(g.ultimaMalha, malha.malha)) {
        Perfil3D.contar('nos.remalhados');
        Perfil3D.fase(
          'sincronia.remalhar',
          () => _construir(node, malha, onMudou, existente: g),
        );
      }
      Perfil3D.fase(
        'sincronia.aplicarTransform',
        () => g!.transformar(xf, node),
      );
    }
    for (final id in _nos.keys.toList()) {
      if (!vivos.contains(id)) {
        _nos.remove(id)!.remover(cena);
      }
    }
    _malhas.manterApenas(vivos);
    final usadas = {for (final n in _nos.values) ...n.aplicadores.keys};
    _texturas.removeWhere((path, _) => !usadas.contains(path));
  }

  /// A malha do no neste instante: vertices, faces, normais e UVs por
  /// vertice quando existem (modelos), e o material de cada face.
  /// A malha de cada no, com memoria: um objeto parado passa a custar
  /// uma comparacao de identidade, e nao duas listas novas por quadro.
  /// Ver [CacheDeMalhas].
  final CacheDeMalhas _malhas = CacheDeMalhas();

  MalhaDoNo? _malhaDe(SceneNode node, Duration t) => _malhas.doNo(
    node,
    t,
    lodDaReceita: _lodPelaReceita,
    assinaturaDoMaterial: _assinaturaMaterial,
  );

  Element3DMesh? _lodPelaReceita(SceneNode node) => switch (_receita.lod) {
    MeshLod3D.high => node.mesh,
    MeshLod3D.medium => node.mediumMesh ?? node.mesh,
    MeshLod3D.low => node.lowMesh ?? node.mediumMesh ?? node.mesh,
    MeshLod3D.auto => lodAutomatico(node, false),
  };

  static String _assinaturaMaterial(Material3D m) =>
      '${m.baseColor.toARGB32()}:${m.metallic}:${m.roughness}:${m.emissive}:'
      '${m.opacity}:${m.kind.index}:${m.imagePath}:${m.doubleSided}:'
      '${m.alphaCutoff}';

  _NoGpu _construir(
    SceneNode node,
    MalhaDoNo fonte,
    VoidCallback? onMudou, {
    _NoGpu? existente,
  }) {
    // Faces agrupadas por material, em buffers tipados: uma primitiva
    // (uma chamada) por grupo. Ver geometria_gpu.dart.
    final grupos = montarGruposGpu(
      malha: fonte.malha,
      materiais: fonte.materiais,
      normais: fonte.normais,
      uvs: fonte.uvs,
    );

    final no = existente ?? _NoGpu(fonte.assinatura, fs.Node(name: node.name));
    no.ultimaMalha = fonte.malha;
    final instanciado = node.instances.isNotEmpty;
    // DESCARTE POR INSTANCIA: vale a pena quando as copias se espalham
    // pelo espaco e entram na camera em momentos diferentes — uma grade
    // grande, que e o que a ferramenta de array produz. Num punhado de
    // copias juntas o teste por copia custa mais do que economiza, e o
    // descarte do conjunto inteiro ja resolve. Por isso o corte por
    // quantidade em vez de ligar sempre.
    final descartarPorInstancia = node.instances.length >= 24;
    var triangulos = 0;
    for (final e in grupos.entries) {
      final g = e.value;
      triangulos += g.triangulos;
      final antiga = no.geometrias[e.key];
      if (g.indices == 0 && antiga == null) continue;
      if (antiga != null) {
        final positions = g.positions;
        if (listEquals(no.indices[e.key], g.indexList) &&
            no.tamanhos[e.key] == positions.length) {
          antiga.updatePositions(positions);
          antiga.updateNormals(g.normals);
          antiga.updateTexCoords(g.texCoords);
        } else {
          antiga.rebuild(
            positions: positions,
            normals: g.normals,
            texCoords: g.texCoords,
            indices: g.indexList,
          );
          no.indices[e.key] = g.indexList;
          no.tamanhos[e.key] = positions.length;
        }
        continue;
      }
      final material = _materialGpu(e.key, onMudou, no);
      final geometria = fs.MeshGeometry.fromArrays(
        storage: fonte.dinamica
            ? fs.GeometryStorage.updatable
            : fs.GeometryStorage.fixed,
        positions: g.positions,
        normals: g.normals,
        texCoords: g.texCoords,
        indices: g.indexList,
      );
      final primitive = fs.MeshPrimitive(geometria, material);
      no.geometrias[e.key] = geometria;
      no.indices[e.key] = g.indexList;
      no.tamanhos[e.key] = g.positions.length;
      // O CACHE DE VERTICES saiu daqui. Rodava em `compute` (um isolate
      // aberto por malha: ~1,4 s sincrono no iPhone 13) e depois reenviava
      // a geometria inteira. Agora a importacao ja entrega os triangulos
      // na ordem boa, soldados e com niveis de detalhe, uma vez so e fora
      // da thread da tela — ver domain/malha_importada.dart.
      if (instanciado) {
        final im = fs.InstancedMesh(
          geometry: geometria,
          material: material,
          cullInstances: descartarPorInstancia,
        );
        no.instancias.add(im);
        final filho = fs.Node()..addComponent(fs.InstancedMeshComponent(im));
        no.no.add(filho);
      } else {
        no.no.add(
          fs.Node()..addComponent(
            fs.MeshComponent(fs.Mesh.primitives(primitives: [primitive])),
          ),
        );
      }
    }
    no.triangulos = triangulos;
    if (existente == null) cena.add(no.no);
    return no;
  }

  // ------------------------------------------------------- materiais

  fs.Material _materialGpu(Material3D m, VoidCallback? onMudou, _NoGpu no) {
    final cor = _linear(m.baseColor);
    final opacidade = (m.baseColor.a * m.opacity).clamp(0.0, 1.0);
    final fator = vm.Vector4(cor.x, cor.y, cor.z, opacidade);
    if (m.kind == MaterialKind.unlit) {
      final u = fs.UnlitMaterial();
      u.baseColorFactor = fator;
      u.doubleSided = m.doubleSided;
      if (opacidade < .999) u.alphaMode = fs.AlphaMode.blend;
      _ligarTextura(
        m.imagePath,
        onMudou,
        no,
        (tex) => u.baseColorTexture = tex,
      );
      return u;
    }
    final p = fs.PhysicallyBasedMaterial();
    p.baseColorFactor = fator;
    p.metallicFactor = m.metallic.clamp(0.0, 1.0);
    p.roughnessFactor = m.roughness.clamp(0.04, 1.0);
    // EMISSIVO forte o bastante para o bloom pegar: o dominio guarda 0..1,
    // e um quadro de HDR acima de 1 e o que separa "claro" de "acende".
    if (m.emissive > 0) {
      final k = m.emissive * 6;
      p.emissiveFactor = vm.Vector4(cor.x * k, cor.y * k, cor.z * k, 1);
    }
    p.doubleSided = m.doubleSided;
    p.alphaMode = switch (m.kind) {
      MaterialKind.transparent => fs.AlphaMode.blend,
      MaterialKind.cutout => fs.AlphaMode.mask,
      _ => opacidade < .999 ? fs.AlphaMode.blend : fs.AlphaMode.opaque,
    };
    p.alphaCutoff = m.alphaCutoff;
    _ligarTextura(m.imagePath, onMudou, no, (tex) => p.baseColorTexture = tex);
    return p;
  }

  /// A textura de [path] sobe para a GPU uma vez; quem precisa dela
  /// recebe pelo [aplicar] — agora, se ja esta la, ou quando chegar.
  ///
  /// O [aplicar] fica guardado no no: quando a receita troca o teto de
  /// textura, a textura sobe de novo no tamanho novo e e reaplicada em
  /// todos os materiais que a usam, sem refazer geometria.
  void _ligarTextura(
    String? path,
    VoidCallback? onMudou,
    _NoGpu no,
    void Function(fs.Texture2D) aplicar,
  ) {
    if (path == null || path.isEmpty) return;
    (no.aplicadores[path] ??= []).add(aplicar);
    final pronta = _texturas[path];
    if (pronta != null) {
      aplicar(pronta);
      return;
    }
    _subirTextura(path, onMudou);
  }

  bool _alguemUsa(String path) =>
      _nos.values.any((n) => n.aplicadores.containsKey(path));

  void _subirTextura(String path, VoidCallback? onMudou) {
    if (_texturasACaminho.containsKey(path)) return;
    final teto = _receita.texturaMax;
    final upload = _uploads.then((_) async {
      try {
        if (_descartado || !_alguemUsa(path)) return;
        final imagem = await _decodificarComTeto(path, teto);
        if (imagem == null || _descartado) return;
        late final fs.Texture2D tex;
        try {
          tex = await fs.Texture2D.fromImage(imagem);
        } finally {
          imagem.dispose();
        }
        if (_descartado || !_alguemUsa(path)) return;
        _texturas[path] = tex;
        for (final n in _nos.values) {
          for (final f
              in n.aplicadores[path] ?? const <void Function(fs.Texture2D)>[]) {
            f(tex);
          }
        }
        onMudou?.call();
      } catch (e) {
        debugPrint('Textura 3D nao subiu para a GPU ($path): $e');
      } finally {
        _texturasACaminho.remove(path);
      }
    });
    _uploads = upload;
    _texturasACaminho[path] = upload;
  }

  /// A imagem de [path] com no maximo [teto] pixels no lado maior.
  ///
  /// Em 1024 (o teto do TextureCache) a decodificacao e reaproveitada;
  /// nos outros tetos decodifica direto no tamanho pedido — uma textura
  /// de 4096 decodificada inteira e 64 MB antes de qualquer GPU.
  Future<ui.Image?> _decodificarComTeto(String path, int teto) async {
    if (teto == 1024) {
      var imagem = TextureCache.instance.imageFor(path);
      if (imagem == null) {
        await TextureCache.instance.prepare(path);
        imagem = TextureCache.instance.imageFor(path);
      }
      // A decode eviction must not invalidate an upload that is in flight.
      return imagem?.clone();
    }
    ui.ImmutableBuffer? buffer;
    ui.ImageDescriptor? descriptor;
    ui.Codec? codec;
    try {
      final bytes = path.startsWith('data:')
          ? UriData.parse(path).contentAsBytes()
          : await File(path).readAsBytes();
      buffer = await ui.ImmutableBuffer.fromUint8List(bytes);
      descriptor = await ui.ImageDescriptor.encoded(buffer);
      final escala = math.min(
        1.0,
        teto / math.max(descriptor.width, descriptor.height),
      );
      codec = await descriptor.instantiateCodec(
        targetWidth: math.max(1, (descriptor.width * escala).round()),
        targetHeight: math.max(1, (descriptor.height * escala).round()),
      );
      final frame = await codec.getNextFrame();
      return frame.image;
    } catch (_) {
      return null;
    } finally {
      codec?.dispose();
      descriptor?.dispose();
      buffer?.dispose();
    }
  }

  // ------------------------------------------------------------ luzes

  /// O que muda a ESTRUTURA das luzes (e pede objetos novos). A
  /// intensidade fica de fora de proposito: animada, ela muda a cada
  /// quadro, e refazer a luz a cada quadro refazia o cache de sombra
  /// dela junto — era um atlas de sombra novo por quadro.
  /// O lado do tile de sombra para o alvo do ultimo quadro (a receita,
  /// limitada pelo que o alvo aproveita — ver [sombraEfetiva]).
  int get _ladoDaSombra {
    final maior =
        (math.max(_ultimoAlvo.width, _ultimoAlvo.height) * _ultimaEscala)
            .round();
    return sombraEfetiva(_receita, maior);
  }

  String _assinaturaDasLuzes(Scene3D scene) {
    final b = StringBuffer('${_receita.nivel.index}:$_ladoDaSombra;');
    for (final l in scene.lights) {
      b.write(
        '${l.kind.index}:${l.castsShadow}:${l.color.toARGB32()}:'
        '${l.direction.x},${l.direction.y},${l.direction.z}:'
        '${l.position.x},${l.position.y},${l.position.z}:'
        '${l.range}:${l.coneDegrees}:${l.softness};',
      );
    }
    return b.toString();
  }

  void _sincronizarLuzes(Scene3D scene, Duration t) {
    final assinatura = _assinaturaDasLuzes(scene);
    if (assinatura == _assinaturaLuzes &&
        _luzObjetos.length == scene.lights.length) {
      _atualizarIntensidades(scene, t);
      return;
    }
    _assinaturaLuzes = assinatura;
    for (final l in _luzes) {
      cena.remove(l);
    }
    _luzes.clear();
    _luzObjetos.clear();
    _tilesDeSombra = 0;

    // A principal e a primeira direcional que faz sombra (ou a primeira
    // direcional): so ela tem cascatas; as outras entram como componentes.
    fs.DirectionalLight? principal;
    var spotsComSombra = 0;
    final ladoDaSombra = math.max(256, _ladoDaSombra);
    for (final l in scene.lights) {
      final i = math.max(0.0, l.intensity.valueAt(t));
      final cor = _linear3(l.color);
      switch (l.kind) {
        case Light3DKind.directional:
          final sombra = l.castsShadow && _receita.sombras;
          final d = fs.DirectionalLight(
            direction: _v(l.direction).normalized(),
            color: cor,
            intensity: i * 2.6,
            castsShadow: sombra && i > 0,
            shadowSoftness: 3 + l.softness.clamp(0.0, 1.0) * 14,
            shadowMaxDistance: 5000,
            shadowCascadeCount: math.max(1, _receita.cascatas),
            shadowMapResolution: ladoDaSombra,
            shadowDepthBias: 0.8,
            shadowNormalBias: 0.8,
            shadowFadeRange: 400,
          );
          if (principal == null || (sombra && !principal.castsShadow)) {
            if (principal != null) {
              _luzes.add(
                _noDeLuz(
                  fs.DirectionalLightComponent.aimed(
                    principal,
                    principal.direction,
                  ),
                ),
              );
            }
            principal = d;
          } else {
            _luzes.add(
              _noDeLuz(fs.DirectionalLightComponent.aimed(d, d.direction)),
            );
          }
          _luzObjetos.add(d);
        case Light3DKind.point:
          final alcance = l.range <= 0 ? 1200.0 : l.range;
          final p = fs.PointLight(
            color: cor,
            // O dominio atenua (1 - d/alcance)^2; o motor, 1/d^2. Igualar
            // no meio do alcance: I = i * alcance^2 / 16.
            intensity: i * alcance * alcance / 16,
            range: alcance,
          );
          _luzes.add(_noDeLuz(fs.PointLightComponent(p), posicao: l.position));
          _luzObjetos.add(p);
        case Light3DKind.spot:
          final alcance = l.range <= 0 ? 1200.0 : l.range;
          final externo = l.coneDegrees.clamp(1.0, 179.0) * math.pi / 360;
          // SOMBRA DE SPOT E CARA: cada uma e um tile inteiro no atlas.
          // A receita diz quantas cabem; as outras iluminam sem sombra.
          final sombra =
              l.castsShadow &&
              _receita.sombras &&
              spotsComSombra < _receita.sombrasSpotMax;
          if (sombra) spotsComSombra++;
          final s = fs.SpotLight(
            color: cor,
            intensity: i * alcance * alcance / 16,
            range: alcance,
            direction: _v(l.direction).normalized(),
            innerConeAngle: externo * (1 - l.softness.clamp(0.0, 1.0) * .9),
            outerConeAngle: externo,
            castsShadow: sombra && i > 0,
            shadowMapResolution: math.min(512, ladoDaSombra),
            shadowSoftness: 2 + l.softness * 6,
          );
          _luzes.add(_noDeLuz(fs.SpotLightComponent(s), posicao: l.position));
          _luzObjetos.add(s);
        case Light3DKind.ambient:
          // Entra no ambiente (ver _sincronizarAmbiente).
          _luzObjetos.add(null);
      }
    }
    cena.directionalLight = principal;
    if (principal != null && principal.castsShadow) {
      _tilesDeSombra += principal.shadowCascadeCount;
    }
    _tilesDeSombra += spotsComSombra;
    for (final n in _luzes) {
      cena.add(n);
    }
  }

  /// So o que muda por quadro: a intensidade — e a sombra de quem
  /// apagou, que nao precisa ser desenhada.
  void _atualizarIntensidades(Scene3D scene, Duration t) {
    for (var k = 0; k < scene.lights.length; k++) {
      final l = scene.lights[k];
      final o = _luzObjetos[k];
      final i = math.max(0.0, l.intensity.valueAt(t));
      switch (o) {
        case fs.DirectionalLight d:
          d.intensity = i * 2.6;
          d.castsShadow = l.castsShadow && _receita.sombras && i > 0;
        case fs.PointLight p:
          final alcance = l.range <= 0 ? 1200.0 : l.range;
          p.intensity = i * alcance * alcance / 16;
        case fs.SpotLight s:
          final alcance = l.range <= 0 ? 1200.0 : l.range;
          s.intensity = i * alcance * alcance / 16;
          if (i <= 0) s.castsShadow = false;
        default:
          break;
      }
    }
  }

  fs.Node _noDeLuz(fs.Component componente, {Vec3? posicao}) {
    final no = fs.Node(
      localTransform: posicao == null
          ? null
          : vm.Matrix4.translation(_v(posicao)),
    );
    no.addComponent(componente);
    return no;
  }

  // --------------------------------------------------------- ambiente

  /// Captura HDR local de seis faces, prefiltrada pelo renderer PBR.
  /// Retida entre quadros: nao aloca cubemaps continuamente na reproducao.
  /// Atualizar reflexos na UI produz novas configuracoes e solicita captura.
  void _sincronizarReflexos(Scene3D scene) {
    final settings = scene.reflectionProbe;
    if (!settings.enabled) {
      if (_probeNode != null) cena.remove(_probeNode!);
      _probeNode = null;
      _probe = null;
      _probeSettings = null;
      return;
    }
    if (_probe == null) {
      _probe = fs.ReflectionProbeComponent(
        extents: vm.Vector3.all(5000),
        blendDistance: 2000,
        faceResolution: settings.quality.faceResolution,
      );
      _probeNode = fs.Node()..addComponent(_probe!);
      cena.add(_probeNode!);
    }
    final position = settings.position;
    _probeNode!.localTransform = vm.Matrix4.translation(
      vm.Vector3(position.x, position.y, position.z),
    );
    _probe!.weight = scene.envReflect.clamp(0.0, 1.0);
    if (!identical(_probeSettings, settings)) {
      _probe!.faceResolution = settings.quality.faceResolution;
      _probe!.requestCapture();
      _probeSettings = settings;
    }
  }

  void _sincronizarAmbiente(Scene3D scene, Duration t, VoidCallback? onMudou) {
    var extra = 0.0;
    for (final l in scene.lights) {
      if (l.kind == Light3DKind.ambient) extra += l.intensity.valueAt(t);
    }
    final pano = scene.panorama;
    cena.environmentTransform = vm.Matrix3.rotationY(
      pano.rotationDegrees * math.pi / 180,
    );
    cena.environmentIntensity =
        ((scene.ambient + extra) / .28) * pano.intensity.clamp(0.0, 4.0);
    final caminho = pano.hasImage ? pano.sourcePath : null;
    final chave = caminho != null
        ? 'img:$caminho:${pano.showBackground}:${pano.backgroundBlur}'
        : 'ceu:${scene.environment.index}:${scene.skyColor.toARGB32()}:'
              '${scene.groundColor.toARGB32()}:${pano.showBackground}:${pano.backgroundBlur}';
    if (chave == _chaveAmbiente) return;
    _chaveAmbiente = chave;
    final epoca = ++_epocaAmbiente;

    if (caminho != null) {
      // O MESMO PANORAMA TAMBEM SOBE UMA VEZ SO. O mapa depende so do
      // arquivo: o desfoque do fundo e o mostrar/esconder vivem no
      // Skybox, nao nele. Sem isto, mexer no desfoque subia um atlas
      // novo a cada passo do controle, e nenhum deles era liberado.
      final guardado = _panoramasDeAmbiente[caminho];
      if (guardado != null) {
        cena.skyEnvironment = null;
        cena.environment = guardado;
        cena.skybox = pano.showBackground
            ? fs.Skybox(
                fs.EnvironmentSkySource(
                  blurriness: (pano.backgroundBlur / 30).clamp(0.0, 1.0),
                ),
              )
            : null;
        return;
      }
      () async {
        try {
          final bytes = await RegistroDeTravadas.marcandoAsync(
            'cena 3D: ambiente > ler panorama do disco',
            () => File(caminho).readAsBytes(),
          );
          final mapa = await RegistroDeTravadas.marcandoAsync(
            'cena 3D: ambiente > panorama na GPU',
            () => fs.EnvironmentMap.fromEquirectImageBytes(
              bytes: bytes,
              maxWidth: 2048,
            ),
          );
          _panoramasDeAmbiente[caminho] = mapa;
          if (_descartado || epoca != _epocaAmbiente) return;
          cena.skyEnvironment = null;
          cena.environment = mapa;
          _probe?.requestCapture();
          cena.skybox = pano.showBackground
              ? fs.Skybox(
                  fs.EnvironmentSkySource(
                    blurriness: (pano.backgroundBlur / 30).clamp(0.0, 1.0),
                  ),
                )
              : null;
          onMudou?.call();
        } catch (e) {
          debugPrint('Panorama nao carregou na GPU ($caminho): $e');
        }
      }();
      return;
    }

    // Bake the actual studio/neon/interior map, not a featureless gradient.
    // HDR highlights and roughness mip levels give metals readable reflections.
    if (scene.environment != EnvironmentKind.ceu) {
      final kind = scene.environment;
      // O MAPA DE CADA AMBIENTE SOBE UMA VEZ SO.
      //
      // Ele depende exclusivamente do tipo — sao sete no catalogo, e o
      // mesmo tipo sempre da o mesmo mapa. Antes, cada troca gerava um
      // `EnvironmentMap` novo, e o flutter_scene NAO LIBERA memoria de
      // GPU (bdero/flutter_scene#285): trocar de ambiente seis vezes
      // deixava seis atlas de radiancia retidos, alem do que o modelo ja
      // ocupa. E a mesma politica de "sobe uma vez" que as texturas ja
      // seguem, pelo mesmo motivo.
      final pronto = _mapasDeAmbiente[kind];
      if (pronto != null) {
        cena.skyEnvironment = null;
        cena.environment = pronto;
        cena.skybox = pano.showBackground
            ? fs.Skybox(
                fs.EnvironmentSkySource(
                  blurriness: (pano.backgroundBlur / 30).clamp(0.0, 1.0),
                ),
              )
            : null;
        return;
      }
      // O ISOLATE CUSTAVA 1.700 ms PARA POUPAR 40. NAO USE ISOLATE AQUI.
      //
      // Medido no iPhone 13, registro do build 69: a marca
      // `ambiente > abrir isolate` somou 12.470 ms em NOVE chamadas, com
      // pior de 1.960 ms — e isso e a parte SINCRONA de `Isolate.run`, no
      // mesmo fio que recebe o toque. Nao e a primeira que custa: a media
      // das nove foi 1.385 ms.
      //
      // O mesmo `Isolate.run` custa 2 ms num desktop
      // (`test/bancada_ambiente_test.dart`), entao o preco e do spawn em
      // AOT no iOS, e nao do calculo. E o calculo que ele evitava custa
      // 13-26 ms num desktop: abrir o isolate saia vinte vezes mais caro
      // do que simplesmente fazer a conta.
      //
      // Entao a conta e feita aqui mesmo, UMA VEZ POR TIPO. O custo passa
      // a ser um engasgo de algumas dezenas de milissegundos na primeira
      // vez que cada ambiente aparece, contra quase dois segundos de tela
      // parada toda vez que a camada 3D entrava em cena.
      final aCaminho = _mapasACaminho[kind] ??= () async {
        final pixels = RegistroDeTravadas.marcando(
          'cena 3D: ambiente > gerar radiancia',
          () => environmentRadiance(kind),
        );
        final map = await RegistroDeTravadas.marcandoAsync(
          'cena 3D: ambiente > mapa HDR na GPU',
          () => fs.EnvironmentMap.fromEquirectHdr(
            linearPixels: pixels,
            width: 512,
            height: 256,
          ),
        );
        _mapasDeAmbiente[kind] = map;
        return map;
      }();
      () async {
        try {
          final map = await aCaminho;
          if (_descartado || epoca != _epocaAmbiente) return;
          cena.skyEnvironment = null;
          cena.environment = map;
          _probe?.requestCapture();
          cena.skybox = pano.showBackground
              ? fs.Skybox(
                  fs.EnvironmentSkySource(
                    blurriness: (pano.backgroundBlur / 30).clamp(0.0, 1.0),
                  ),
                )
              : null;
          onMudou?.call();
        } catch (e) {
          // Deixa o tipo tentar de novo numa proxima vez.
          _mapasACaminho.remove(kind);
          debugPrint('Ambiente HDR: $e');
        }
      }();
      return;
    }

    // CEU PROCEDURAL a partir das cores do dominio: zenite = ceu, chao =
    // chao, horizonte no meio, e o sol na direcao da luz principal.
    //
    // Este bloco e 100% SINCRONO e ficou marcado a parte durante a caca
    // ao travamento. O culpado acabou sendo o spawn de isolate no
    // caminho HDR (ver acima), e nao ele — mas a marca fica, porque
    // custa um `if` e responde na hora se um dia a suspeita voltar.
    RegistroDeTravadas.marcando(
      'cena 3D: ambiente > ceu procedural',
      () => _ceuProcedural(scene, t, pano),
    );
  }

  void _ceuProcedural(Scene3D scene, Duration t, Panorama3D pano) {
    final ceu = _linear3(scene.skyColor);
    final chao = _linear3(scene.groundColor);
    final horizonte = (ceu + chao) * .5;
    vm.Vector3 sol = vm.Vector3(0.4, 0.5, 0.6);
    vm.Vector3 corDoSol = vm.Vector3(3.0, 2.7, 2.2);
    for (final l in scene.lights) {
      if (l.kind == Light3DKind.directional && l.intensity.valueAt(t) > 0) {
        sol = (_v(l.direction) * -1).normalized();
        corDoSol = _linear3(l.color) * 3.0;
        break;
      }
    }
    final fonte = fs.GradientSkySource(
      zenithColor: ceu,
      horizonColor: horizonte,
      groundColor: chao,
      sunDirection: sol,
      sunColor: corDoSol,
    );
    cena.environment = null;
    // O MESMO SkyEnvironment, COM A FONTE TROCADA.
    //
    // O flutter_scene fatia o bake do ceu em um passe de GPU por quadro
    // — mas so a partir do SEGUNDO bake daquele objeto. O primeiro roda
    // inteiro numa chamada so, de proposito, para a cena nascer
    // iluminada. Criar um `SkyEnvironment` novo a cada mudanca de cor
    // fazia TODO bake ser o primeiro, e o pico voltava toda vez.
    //
    // Reaproveitando o objeto e pedindo `invalidate()`, so o primeiro
    // custa; os demais entram fatiados, como o pacote pretende.
    final ceuEnv = _ceuDoAmbiente;
    if (ceuEnv != null) {
      ceuEnv.source = fonte;
      ceuEnv.invalidate();
      cena.skyEnvironment = ceuEnv;
    } else {
      cena.skyEnvironment = _ceuDoAmbiente = fs.SkyEnvironment(fonte);
    }
    cena.skybox = pano.showBackground ? fs.Skybox(fonte) : null;
  }

  // ----------------------------------------------------------- neblina

  void _sincronizarNevoa(Scene3D scene) {
    final f = cena.fog;
    f.enabled = scene.fogDensity > 0;
    if (!f.enabled) return;
    f
      ..mode = fs.FogMode.exponential
      ..density = scene.fogDensity
      ..start = scene.fogStart
      ..color = _linear3(scene.fogColor)
      ..maxOpacity = 1.0;
  }

  // --------------------------------------------------------------- pos

  void _sincronizarPos(Scene3D scene, bool rascunho) {
    var emissivo = false;
    for (final n in scene.nodes) {
      if (n.material.emissive > 0) {
        emissivo = true;
        break;
      }
      final mats = n.modelAsset?.data['materials'] as List? ?? const [];
      for (final m in mats) {
        if (((m as Map)['emissive'] as num? ?? 0) > 0) {
          emissivo = true;
          break;
        }
      }
      if (emissivo) break;
    }
    // O bloom monta uma cadeia de mips do tamanho da tela a cada
    // quadro. Enquanto toca, isso e memoria e banda de GPU trocadas por
    // um brilho que ninguem esta olhando parado — e abaixo de "media" a
    // receita o tira de vez.
    cena.postProcess.bloom
      ..enabled = emissivo && !rascunho && _receita.bloom
      ..threshold = 1.0
      ..intensity = .45
      ..scatter = .7;
    cena.toneMapping = fs.ToneMappingMode.pbrNeutral;
  }

  // ---------------------------------------------------------- utilidades

  static vm.Vector3 _v(Vec3 v) => vm.Vector3(v.x, v.y, v.z);

  static double _lin(double c) =>
      c <= 0.04045 ? c / 12.92 : math.pow((c + 0.055) / 1.055, 2.4).toDouble();

  static vm.Vector3 _linear3(ui.Color c) =>
      vm.Vector3(_lin(c.r), _lin(c.g), _lin(c.b));

  static vm.Vector3 _linear(ui.Color c) => _linear3(c);
}

/// Um no do dominio ja traduzido: o no do motor e a assinatura do que
/// ele contem. Quando a assinatura muda, o no e refeito.
class _NoGpu {
  _NoGpu(this.assinatura, this.no);

  final String assinatura;
  final fs.Node no;
  final List<fs.InstancedMesh> instancias = [];
  final Map<Material3D, fs.MeshGeometry> geometrias = {};
  final Map<Material3D, List<int>> indices = {};
  final Map<Material3D, int> tamanhos = {};

  /// Por caminho de textura, quem a recebe (os materiais deste no).
  final Map<String, List<void Function(fs.Texture2D)>> aplicadores = {};
  int triangulos = 0;
  Element3DMesh? ultimaMalha;
  List<Vec3>? _ultimasInstancias;
  double? _ultimoTamanho;

  void transformar(NodeTransform xf, SceneNode node) {
    final rad = math.pi / 180;
    final m = vm.Matrix4.translation(
      vm.Vector3(xf.position.x, xf.position.y, xf.position.z),
    );
    m.multiply(vm.Matrix4.rotationZ(xf.rotZ * rad));
    m.multiply(vm.Matrix4.rotationY(xf.rotY * rad));
    m.multiply(vm.Matrix4.rotationX(xf.rotX * rad));
    if (instancias.isEmpty) {
      final s = node.size * xf.scale;
      m.multiply(vm.Matrix4.diagonal3Values(s, s, s));
      no.localTransform = m;
      return;
    }
    // Instancias: o no leva posicao, rotacao e escala; cada instancia
    // leva o deslocamento e o tamanho da malha unitaria.
    m.multiply(vm.Matrix4.diagonal3Values(xf.scale, xf.scale, xf.scale));
    no.localTransform = m;
    if (!identical(_ultimasInstancias, node.instances) ||
        _ultimoTamanho != node.size) {
      _ultimasInstancias = node.instances;
      _ultimoTamanho = node.size;
      for (final im in instancias) {
        im.clearInstances();
        for (final p in node.instances) {
          im.addInstance(
            vm.Matrix4.translation(vm.Vector3(p.x, p.y, p.z))..multiply(
              vm.Matrix4.diagonal3Values(node.size, node.size, node.size),
            ),
          );
        }
      }
    }
  }

  void remover(fs.Scene cena) {
    cena.remove(no);
    geometrias.clear();
  }
}


