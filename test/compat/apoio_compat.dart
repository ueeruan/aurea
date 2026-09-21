// APOIO DOS TESTES DE COMPATIBILIDADE: as fixtures de projeto antigo, o
// projeto rico montado pelo controlador e o "essencial" que a ida e volta
// tem de preservar.
import 'dart:convert';
import 'dart:io';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/caption.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/mask.dart';
import 'package:aurea/src/features/editor/domain/modelo_do_texto3d.dart';
import 'package:aurea/src/features/editor/domain/obj_import3d.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/template_pack.dart';
import 'package:aurea/src/features/editor/domain/text_anim.dart';
import 'package:aurea/src/features/editor/domain/texto3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter/painting.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

const pastaDasFixtures = 'test/compat/fixtures';

/// O PROJETO RICO, congelado no formato de hoje — o do beta a02: o
/// serializador nao mudou uma chave desde o `apk-93` e e identico ao do
/// checkpoint `antes-da-ui-nova`. E uma PASTA DE REPOSITORIO de verdade
/// (`<id>.json` + `modelos/<peso>.json`), gravada pelo `ProjectRepository`
/// como o aparelho grava. Regerar SO de proposito:
/// `AUREA_GERAR_FIXTURE=1 flutter test test/compat/gerar_fixture_test.dart`.
const repositorioRico = '$pastaDasFixtures/repositorio_rico_beta_a02';

/// Um projeto GRAVADO PELO APP num aparelho do beta (08/09, Android):
/// duas camadas de audio, uma com efeito de audio. Copiado byte a byte de
/// `tmp/beta-audio-project.json`, no lugar em que o app o guardaria.
const repositorioBetaAudio = '$pastaDasFixtures/repositorio_beta_2026-09-08';

/// Copia uma pasta de fixture para um lugar temporario: o repositorio
/// cria `modelos/` quando precisa, e o teste nao escreve no git.
Directory copiaTemporaria(String pasta) {
  final destino = Directory.systemTemp.createTempSync('aurea_compat_');
  void copiar(Directory de, Directory para) {
    for (final e in de.listSync()) {
      final nome = e.uri.pathSegments.lastWhere((s) => s.isNotEmpty);
      if (e is File) {
        e.copySync('${para.path}/$nome');
      } else if (e is Directory) {
        final sub = Directory('${para.path}/$nome')..createSync();
        copiar(e, sub);
      }
    }
  }

  copiar(Directory(pasta), destino);
  return destino;
}

/// O JSON cru de cada projeto da pasta (sem os pesos): para contar as
/// camadas que o ARQUIVO tinha, antes de qualquer leitura tolerante.
List<Map<String, dynamic>> jsonsDaPasta(String pasta) => [
  for (final f in Directory(pasta).listSync())
    if (f is File && f.path.endsWith('.json'))
      (jsonDecode(f.readAsStringSync()) as Map).cast<String, dynamic>(),
];

/// PROJETOS DE EXEMPLO ANTIGOS EM DISCO (fora do git: `output/` e
/// ignorado). Gravados pelo serializador de 06/09 e 12/09 — cenas 3D com
/// modelo importado de verdade (GLB), cameras, luzes e keyframes. Pesam
/// de 4 a 60 MB, por isso ficam onde estao; o teste que os usa e pulado
/// quando o arquivo nao existe.
const projetosEmDisco = <String, String>{
  'FLOR (template 06/09)': 'output/flor/FLOR.aurea',
  'VOID (template 06/09)': 'output/void/VOID.aurea',
  'DEMONSTRACAO (template 06/09)': 'output/tutorial-scene3d/DEMONSTRACAO.aurea',
  'CAMPO (projeto 06/09)': 'output/campo/campo-project.json',
  'LUMEN (template 12/09)':
      'output/floresta-magica/LUMEN-Floresta-Magica.aurea',
};

/// O JSON de um projeto em disco: projeto solto ou envelope de template
/// (`{"aurea": "template", "project": {...}}`).
Map<String, dynamic> lerJsonDoProjeto(String caminho) {
  final bruto = jsonDecode(File(caminho).readAsStringSync()) as Map;
  final m = bruto.cast<String, dynamic>();
  final projeto = m['aurea'] == 'template' ? m['project'] : m;
  return (projeto as Map).cast<String, dynamic>();
}

/// A LEITURA DE VERDADE de cada formato: o template passa pelo
/// `TemplatePack.decode` (o caminho do app, que engole erro e devolve
/// nulo) e o projeto solto pelo `projectFromJson` (o do repositorio).
VideoProject abrirProjetoDoDisco(String caminho) {
  final texto = File(caminho).readAsStringSync();
  if (caminho.endsWith('.aurea')) {
    final pacote = TemplatePack.decode(texto);
    if (pacote == null) {
      // Refaz a leitura sem o `catch` do pacote para o erro aparecer.
      return projectFromJson(lerJsonDoProjeto(caminho));
    }
    return pacote.project;
  }
  return projectFromJson(lerJsonDoProjeto(caminho));
}

/// Salvar e reabrir como o app faz: JSON em TEXTO e de volta.
VideoProject salvarEReabrir(VideoProject p) => projectFromJson(
  (jsonDecode(jsonEncode(projectToJson(p))) as Map).cast<String, dynamic>(),
);

/// Todas as camadas, com os filhos de grupo (precomp) dentro.
Iterable<Layer> todasAsCamadas(List<Layer> camadas) sync* {
  for (final l in camadas) {
    yield l;
    if (l is GroupLayer) yield* todasAsCamadas(l.children);
  }
}

// ------------------------------------------------------------ essencial

int _marcas(AnimatedDouble a) => a.keyframes.length;

/// O ESSENCIAL de um projeto, comparavel com `equals`: o que a pessoa
/// perderia se sumisse. Nao e o JSON (esse e conferido por chave em
/// outro teste); e o que a tela mostra e o render usa.
///
/// [comIds] falso troca os ids (uuid, novos a cada montagem) pelos nomes:
/// e assim que o projeto montado hoje se compara com a fixture congelada.
Map<String, Object?> essencial(VideoProject p, {bool comIds = true}) {
  final nomes = {for (final l in todasAsCamadas(p.layers)) l.id: l.name};
  String ref(String id) => comIds ? id : (nomes[id] ?? '?');
  return {
    'nome': p.name,
    'fps': p.fps,
    'proporcao': p.aspectRatio,
    'altura': p.resolutionHeight,
    'marcadores': p.markers.length,
    'vinculos': [
      for (final v in p.links)
        '${ref(v.targetLayerId)}<-${ref(v.sourceLayerId)}:${v.targetProp.name}',
    ],
    'camadas': [
      for (final l in todasAsCamadas(p.layers))
        {if (comIds) 'id': l.id, ..._essencialDaCamada(l, comIds)},
    ],
  };
}

Map<String, Object?> _essencialDaCamada(Layer l, bool comIds) => {
  'tipo': l.runtimeType.toString(),
  'nome': l.name,
  'inicio': l.startTime.inMicroseconds,
  'duracao': l.duration.inMicroseconds,
  'posicao': l.position.keyframes.length,
  'rotacao': _marcas(l.rotation),
  'opacidade': _marcas(l.opacity),
  'opacidadeBase': l.opacity.base,
  'mascaras': [
    for (final m in l.masks)
      '${m.name}:${m.path.keyframes.length}:${_marcas(m.feather)}',
  ],
  'efeitos': [
    for (final e in l.effects)
      '${e.type.name}:${e.enabled}:'
          '${e.params.values.where((a) => a.isAnimated).length}',
  ],
  ...switch (l) {
    VideoLayer() => {
      'fonte': l.sourcePath,
      'timeRemap': l.timeRemap?.keyframes.length,
    },
    ImageLayer() => {'fonte': l.sourcePath},
    AudioLayer() => {
      'fonte': l.sourcePath,
      'volume': l.volume,
      'envelope': l.audio.volumeAnimado?.keyframes.length,
      'efeitosDeAudio': l.audio.processing.effects.length,
    },
    TextLayer() => {
      'texto': l.text,
      'anims': [for (final a in l.anims) '${a.slot.name}:${a.specId}'],
      'animadores': l.animators.length,
    },
    ShapeLayer() => {'conteudo': l.contents.length},
    GroupLayer() => {'filhos': l.children.length},
    CaptionLayer() => {
      'legendas': [for (final c in l.cues) c.text],
    },
    CameraLayer() => {'lente': l.zoom.keyframes.length},
    Scene3DLayer() => {
      'nos': [
        for (final n in l.scene.nodes)
          {
            if (comIds) 'id': n.id,
            'nome': n.name,
            'material':
                '${n.material.name}:${n.material.metallic}:'
                '${n.material.roughness}',
            'modelo': n.modelAsset?.triangleCount,
            'x': _marcas(n.x),
            'texto3d': n.texto3d?.texto,
            'ajustes': n.texto3d?.ajustes.length,
          },
      ],
      'cameras': l.allCameras.length,
      'luzes': l.scene.lights.length,
    },
    _ => const <String, Object?>{},
  },
};

// ------------------------------------------------------------ chaves

/// O ESQUEMA de um JSON: o conjunto de caminhos de chave, com os indices
/// de lista colapsados (`layers[].effects[].params`). Serve para provar
/// que nenhuma chave de um arquivo sumiu na regravacao.
Set<String> caminhosDeChave(Object? json, [String prefixo = '']) {
  final out = <String>{};
  if (json is Map) {
    for (final e in json.entries) {
      final aqui = prefixo.isEmpty ? '${e.key}' : '$prefixo.${e.key}';
      out.add(aqui);
      out.addAll(caminhosDeChave(e.value, aqui));
    }
  } else if (json is List) {
    for (final v in json) {
      out.addAll(caminhosDeChave(v, '$prefixo[]'));
    }
  }
  return out;
}

/// O QUE O ARQUIVO ANTIGO DIZIA E O NOVO NAO DIZ MAIS (ou diz diferente).
///
/// Anda pelo JSON antigo: toda chave tem de existir no novo com o MESMO
/// valor (numero com folga de arredondamento), toda lista com o mesmo
/// tamanho. Chave NOVA no regravado e permitida — o formato cresce por
/// adicao —, mas nada do que a pessoa tinha pode mudar ou sumir. Devolve
/// os caminhos que divergiram (vazio = regravar nao perdeu nada).
List<String> divergencias(Object? antigo, Object? novo, [String aqui = r'$']) {
  final out = <String>[];
  void anda(Object? a, Object? b, String p) {
    if (out.length > 40) return;
    if (a is Map) {
      if (b is! Map) {
        out.add('$p: era mapa, virou ${b.runtimeType}');
        return;
      }
      for (final e in a.entries) {
        if (!b.containsKey(e.key)) {
          out.add('$p.${e.key}: sumiu');
        } else {
          anda(e.value, b[e.key], '$p.${e.key}');
        }
      }
    } else if (a is List) {
      if (b is! List || b.length != a.length) {
        out.add(
          '$p: lista de ${a.length} virou '
          '${b is List ? 'lista de ${b.length}' : b.runtimeType}',
        );
        return;
      }
      for (var i = 0; i < a.length; i++) {
        anda(a[i], b[i], '$p[$i]');
      }
    } else if (a is num) {
      final ok =
          b is num &&
          ((a - b).abs() <= 1e-9 * (1 + a.abs()) || (a.isNaN && b.isNaN));
      if (!ok) out.add('$p: $a -> $b');
    } else if (a != b) {
      out.add('$p: $a -> $b');
    }
  }

  anda(antigo, novo, aqui);
  return out;
}

// ------------------------------------------------------------ o rico

const _s1 = Duration(seconds: 1);
const _s2 = Duration(seconds: 2);

const _cuboObj =
    'o Cubo\n'
    'v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\n'
    'v -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\n'
    'f 1 2 3 4\nf 5 8 7 6\nf 1 5 6 2\nf 2 6 7 3\nf 3 7 8 4\nf 5 1 4 8\n';

Layer _camada(EditorController c, String id) => c.state.layerById(id)!;

String _ultimaCriada(EditorController c, Set<String> antes) =>
    c.state.layers.map((l) => l.id).firstWhere((id) => !antes.contains(id));

Set<String> _ids(EditorController c) => {for (final l in c.state.layers) l.id};

/// UM PROJETO COM TUDO, montado pelo CONTROLADOR (o mesmo caminho dos
/// botoes): video com Time Remap, mascara e Motion Tile animado; imagem
/// com Deep Glow animado; texto com animacoes de entrada/saida e Animador
/// de Texto; forma com opacidade animada filha de um nulo; audio com
/// keyframe de volume; camera com lente animada; grupo (precomp) com duas
/// formas; cena 3D com modelo importado (OBJ) e material; Texto 3D com
/// ajuste de caractere animado; legendas; particulas; objeto 3D; camada
/// de ajuste com Levels animado e um efeito que saiu do catalogo; desenho
/// livre; marcadores.
///
/// Precisa de relogio de verdade (o Texto 3D le a fonte dos assets): no
/// `testWidgets`, chamar dentro de `tester.runAsync`.
Future<VideoProject> construirProjetoRico(ProviderContainer container) async {
  final c = container.read(editorControllerProvider.notifier);
  c.openProject(
    VideoProject(
      name: 'Rico (formato beta a02)',
      createdAt: DateTime(2026, 9, 20, 18, 17),
    ),
  );

  // VIDEO: Time Remap (campo da camada desde 17/09), mascara, Motion Tile.
  final video = c.addVideoLayer(
    Duration.zero,
    '/data/user/0/com.aurea.aurea/files/imported_media/clipe.mp4',
    'Clipe',
    const Duration(seconds: 6),
  );
  c.definirTrilhaDeTempo(
    video,
    AnimatedDouble(0, [
      const Keyframe(time: Duration.zero, value: 0),
      const Keyframe(time: _s2, value: 4, ease: Easing.easeInOut),
      const Keyframe(time: Duration(seconds: 4), value: 5),
    ]),
  );
  c.addMask(
    video,
    LayerMask(
      name: 'Oval',
      path: AnimatedPath(BezierPath.ellipse(600, 400)),
      feather: AnimatedDouble(0)
          .withKeyframe(Duration.zero, 0)
          .withKeyframe(_s1, 40),
    ),
  );
  c.addEffect(video, EffectType.motionTile);
  _animarEfeito(c, video);

  // IMAGEM com Deep Glow animado.
  final imagem = c.addImageLayer(
    Duration.zero,
    '/data/user/0/com.aurea.aurea/files/imported_media/foto.jpg',
    'Foto',
    proporcao: 1.5,
  );
  c.addEffect(imagem, EffectType.deepGlow);
  _animarEfeito(c, imagem);

  // TEXTO com animacoes do catalogo e um Animador de Texto.
  var antes = _ids(c);
  c.addTextLayer(Duration.zero, text: 'Aurea beta');
  final texto = _ultimaCriada(c, antes);
  final entrada = textAnimCatalog.firstWhere(
    (s) => s.slots.contains(TextAnimSlot.entrada),
  );
  final saida = textAnimCatalog.firstWhere(
    (s) => s.slots.contains(TextAnimSlot.saida),
  );
  c.setTextAnim(texto, TextAnimSlot.entrada, entrada.id);
  c.setTextAnim(texto, TextAnimSlot.saida, saida.id);
  c.addTextAnimator(texto);
  // Keyframe e EXPLICITO (editar valor nunca crava): o losango nos dois
  // instantes, e o valor editado no segundo.
  c.toggleKeyframe(texto, Duration.zero, LayerProp.position);
  c.toggleKeyframe(texto, _s1, LayerProp.position);
  c.editPosition(texto, _s1, const Offset(300, 200));

  // NULO com uma FORMA filha (parent), e a forma com opacidade animada.
  antes = _ids(c);
  c.addNullLayer(Duration.zero);
  final nulo = _ultimaCriada(c, antes);
  c.toggleKeyframe(nulo, Duration.zero, LayerProp.rotation);
  c.toggleKeyframe(nulo, _s2, LayerProp.rotation);
  c.editRotation(nulo, _s2, 90);
  antes = _ids(c);
  c.addShapeLayer(Duration.zero, name: 'Filha');
  final forma = _ultimaCriada(c, antes);
  c.toggleKeyframe(forma, Duration.zero, LayerProp.opacity);
  c.toggleKeyframe(forma, _s1, LayerProp.opacity);
  c.editOpacity(forma, _s1, 0.25);
  c.linkProperty(forma, LayerProp.parent, nulo, Duration.zero);

  // AUDIO com keyframe de volume (envelope).
  final audio = c.addAudioLayer(
    Duration.zero,
    '/data/user/0/com.aurea.aurea/files/imported_media/trilha.m4a',
    'Trilha',
    const Duration(seconds: 6),
  );
  c.updateAudioSpec(
    audio,
    (a) => a.copyWith(
      volumeAnimado: AnimatedDouble(1)
          .withKeyframe(Duration.zero, 0)
          .withKeyframe(_s1, 1)
          .withKeyframe(const Duration(seconds: 5), 0.2),
    ),
  );

  // CAMERA com lente animada.
  antes = _ids(c);
  c.addCameraLayer(Duration.zero);
  final camera = _ultimaCriada(c, antes);
  c.toggleCameraZoomKeyframe(camera, Duration.zero);
  c.toggleCameraZoomKeyframe(camera, _s2);
  c.editCameraZoom(camera, _s2, 2400);

  // GRUPO (a precomp do app) com duas formas dentro.
  antes = _ids(c);
  c.addShapeLayer(Duration.zero, name: 'No grupo A');
  final a = _ultimaCriada(c, antes);
  antes = _ids(c);
  c.addShapeLayer(Duration.zero, name: 'No grupo B');
  final b = _ultimaCriada(c, antes);
  c.groupLayers([a, b]);

  // CENA 3D com MODELO IMPORTADO e material proprio, e keyframe no no.
  final noModelo = c.addImportedModel3D(
    Duration.zero,
    importObj3D(_cuboObj, name: 'Cubo importado'),
  );
  final cenaDoModelo = c.state.layers
      .whereType<Scene3DLayer>()
      .firstWhere((l) => l.scene.nodeById(noModelo) != null)
      .id;
  c.setSceneNodeMaterial(
    cenaDoModelo,
    noModelo,
    const Material3D(
      name: 'Ouro escovado',
      baseColor: Color(0xFFD4AF37),
      metallic: 1,
      roughness: 0.35,
    ),
  );
  c.toggleSceneNodeKeyframe(cenaDoModelo, noModelo, PropDoNo.x, Duration.zero);
  c.toggleSceneNodeKeyframe(cenaDoModelo, noModelo, PropDoNo.x, _s2);
  c.editSceneNodeProp(cenaDoModelo, noModelo, PropDoNo.x, _s2, 120);

  // TEXTO 3D com ajuste de caractere (o "B" sobe em Z, com keyframes).
  // As marcas ficam LINEARES de proposito: a curva do caractere so passou
  // a ser gravada depois do beta a02 (terceiro item opcional da marca), e
  // esta fixture e o formato do beta. A curva tem teste proprio em
  // `texto3d_curva_do_caractere_test.dart`.
  final noTexto = (await c.addTexto3D(
    Duration.zero,
    'AB',
    EstiloDoTexto3D.cromo,
  ))!;
  final cenaDoTexto = c.state.layers
      .whereType<Scene3DLayer>()
      .firstWhere((l) => l.scene.nodeById(noTexto) != null)
      .id;
  c.ajustarCaracteresDoTexto3D(cenaDoTexto, noTexto, [
    AjusteDeCaracteres(
      inicio: 1,
      fim: 1,
      trilhas: {
        MedidaDoCaractere.z: AnimatedDouble(0)
            .comMarcaInserida(Duration.zero, 0)
            .comMarcaInserida(_s1, 60),
        MedidaDoCaractere.girY: AnimatedDouble(25),
      },
    ),
  ]);

  // LEGENDAS.
  c.addCaptionLayer([
    Cue(start: Duration.zero, end: _s1, text: 'Primeira legenda'),
    Cue(start: _s1, end: _s2, text: 'Segunda legenda'),
  ]);

  // PARTICULAS, OBJETO 3D, AJUSTE com efeito, DESENHO LIVRE.
  c.addParticulasLayer(Duration.zero);
  c.addElement3DLayer(Duration.zero, Element3DKind.cube);
  antes = _ids(c);
  c.addAdjustmentLayer(Duration.zero);
  final ajuste = _ultimaCriada(c, antes);
  c.addEffect(ajuste, EffectType.levels);
  _animarEfeito(c, ajuste);
  // Um efeito que SAIU do catalogo no corte de 16/09 (projeto beta antigo
  // o traz): a instancia fica inerte, e a camada e o painel nao caem.
  c.addEffect(ajuste, EffectType.gaussianBlur);
  c.criarCamadaDeDesenho(Duration.zero);

  c.addMarkers([_s1, _s2], label: 'beta');
  return c.state;
}

/// Um keyframe no 0 e outro em 2 s no primeiro parametro do ultimo efeito.
void _animarEfeito(EditorController c, String layerId) {
  final efeito = _camada(c, layerId).effects.last;
  if (efeito.params.isEmpty) {
    throw StateError('${efeito.type.name} sem parametro para animar');
  }
  c.toggleEffectKeyframe(layerId, efeito.id, Duration.zero);
  c.toggleEffectKeyframe(layerId, efeito.id, _s2);
  final chave = efeito.params.keys.first;
  final base = efeito.params[chave]!.base;
  c.editEffectParam(layerId, efeito.id, chave, _s2, base + 1);
}
