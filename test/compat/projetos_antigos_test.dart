// PROJETOS ANTIGOS ABREM NA VERSAO DE HOJE — A LEITURA.
//
// Regra do dono para a UI nova: "NAO QUEBRE projetos existentes. Os
// projetos beta antigos precisam abrir na nova UI. O modelo de dados nao
// precisa mudar." Este arquivo prova a metade do DADO (a tela e o
// `abrir_na_casca_test.dart`):
//
//   * cada fixture ABRE pelo caminho do app (repositorio ou template),
//     sem a leitura tolerante engolir camada nenhuma;
//   * salvar e reabrir preserva o essencial (camadas, keyframes, efeitos,
//     mascaras, vinculos, cena 3D, legendas);
//   * REGRAVAR NAO PERDE NADA DO ARQUIVO ANTIGO: toda chave e todo valor
//     que o arquivo tinha voltam iguais no arquivo regravado;
//   * o projeto rico que o controlador monta hoje e o MESMO da fixture
//     congelada no formato do beta a02.
//
// As fixtures:
//   - `repositorio_beta_2026-09-08`: projeto gravado pelo app num aparelho
//     do beta (Android, 08/09), copiado byte a byte;
//   - `repositorio_rico_beta_a02`: o projeto rico, gravado pelo
//     `ProjectRepository` com o serializador do checkpoint
//     `antes-da-ui-nova` (projeto + modelos separados, como no aparelho);
//   - os exemplos de `output/` (fora do git, 4 a 60 MB): templates e
//     projeto de 06/09 e 12/09 com cena 3D e modelo GLB. Pulados quando
//     o arquivo nao existe na maquina.
import 'dart:convert';
import 'dart:io';

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/projects/application/project_repository.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio_compat.dart';

/// Quantas camadas o ARQUIVO tem (com os filhos de grupo), antes de
/// qualquer leitura: a leitura tolerante pula camada ilegivel em silencio,
/// e "abriu" com uma camada a menos e o pior tipo de quebra.
int _camadasNoJson(Object? layers) {
  var n = 0;
  for (final l in (layers as List? ?? const [])) {
    n++;
    if (l is Map && l['children'] is List) n += _camadasNoJson(l['children']);
  }
  return n;
}

Future<List<VideoProject>> _abrirPeloRepositorio(String pasta) async {
  final copia = copiaTemporaria(pasta);
  addTearDown(() => copia.deleteSync(recursive: true));
  return ProjectRepository(directory: copia).loadAll();
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('o projeto rico montado pelo controlador', () {
    late VideoProject rico;

    setUpAll(() async {
      final container = ProviderContainer();
      rico = await construirProjetoRico(container);
      container.dispose();
    });

    test('tem tudo o que o pedido lista', () {
      final camadas = todasAsCamadas(rico.layers).toList();
      final tipos = {for (final l in camadas) l.runtimeType};
      expect(tipos, {
        VideoLayer,
        ImageLayer,
        TextLayer,
        ShapeLayer,
        AudioLayer,
        NullLayer,
        CameraLayer,
        GroupLayer,
        Scene3DLayer,
        CaptionLayer,
        ParticulasLayer,
        Element3DLayer,
        AdjustmentLayer,
      }, reason: 'um de cada tipo de camada do app');

      final video = camadas.whereType<VideoLayer>().single;
      expect(video.timeRemap?.keyframes.length, 3, reason: 'Time Remap');
      expect(video.masks.single.feather.isAnimated, isTrue, reason: 'mascara');
      // O Time Remap e o cartao de efeito da trilha de tempo (20/09).
      expect(video.effects.map((e) => e.type), [
        EffectType.timeRemap,
        EffectType.motionTile,
      ]);
      expect(
        video.effects.last.params.values.any((p) => p.keyframes.length == 2),
        isTrue,
        reason: 'Motion Tile com keyframes',
      );
      final imagem = camadas.whereType<ImageLayer>().single;
      expect(imagem.effects.single.type, EffectType.deepGlow);
      expect(
        imagem.effects.single.params.values.any((p) => p.keyframes.length == 2),
        isTrue,
        reason: 'Deep Glow com keyframes',
      );

      final texto = camadas.whereType<TextLayer>().single;
      expect(texto.anims.map((a) => a.slot).toSet(), hasLength(2));
      expect(texto.animators, hasLength(1), reason: 'Animador de Texto');
      expect(texto.position.keyframes.length, 2);

      final nulo = camadas.whereType<NullLayer>().single;
      expect(nulo.rotation.keyframes.length, 2);
      final filha = camadas.whereType<ShapeLayer>().firstWhere(
        (l) => l.name.startsWith('Filha'),
      );
      expect(filha.opacity.keyframes.length, 2);
      expect(
        rico.links.where(
          (v) =>
              v.targetLayerId == filha.id &&
              v.sourceLayerId == nulo.id &&
              v.targetProp == LayerProp.parent,
        ),
        hasLength(1),
        reason: 'nulo como pai',
      );

      final audio = camadas.whereType<AudioLayer>().single;
      expect(audio.audio.volumeAnimado?.keyframes.length, 3);
      expect(camadas.whereType<CameraLayer>().single.zoom.keyframes.length, 2);
      expect(camadas.whereType<GroupLayer>().single.children, hasLength(2));

      final cenas = camadas.whereType<Scene3DLayer>().toList();
      final modelo = cenas
          .expand((c) => c.scene.nodes)
          .firstWhere((n) => n.modelAsset != null && n.texto3d == null);
      expect(modelo.modelAsset!.triangleCount, greaterThan(0));
      expect(modelo.material.name, 'Ouro escovado');
      expect(modelo.x.keyframes.length, 2);
      final letra = cenas
          .expand((c) => c.scene.nodes)
          .firstWhere((n) => n.texto3d != null);
      expect(letra.texto3d!.ajustes.single.trilhas, hasLength(2));

      expect(camadas.whereType<CaptionLayer>().single.cues, hasLength(2));
      final ajuste = camadas.whereType<AdjustmentLayer>().single;
      expect(ajuste.effects.map((e) => e.type), [
        EffectType.levels,
        EffectType.gaussianBlur,
      ]);
      expect(
        ajuste.effects.first.params.values.any((p) => p.keyframes.length == 2),
        isTrue,
        reason: 'Levels com keyframes',
      );
      expect(rico.markers, hasLength(2));
    });

    test('ida e volta preserva o essencial (inline e com modelos a parte)', () {
      expect(essencial(salvarEReabrir(rico)), essencial(rico));
      final pesados = <String, Object>{};
      final separado = jsonDecode(
        jsonEncode(projectToJsonSeparado(rico, pesados: pesados)),
      ) as Map;
      expect(pesados, isNotEmpty, reason: 'os modelos saem do projeto');
      final lido = projectFromJsonComPesos(
        separado.cast<String, dynamic>(),
        (jsonDecode(jsonEncode(pesados)) as Map).cast<String, Object>(),
      );
      expect(essencial(lido), essencial(rico));
    });

    test(
      'e o MESMO projeto da fixture congelada no formato do beta a02',
      () async {
        final lidos = await _abrirPeloRepositorio(repositorioRico);
        expect(lidos, hasLength(1));
        expect(
          essencial(lidos.single, comIds: false),
          essencial(rico, comIds: false),
        );
      },
    );
  });

  group('fixtures de projeto antigo (repositorio, como no aparelho)', () {
    final casos = {
      'aparelho do beta, 08/09 (audio)': repositorioBetaAudio,
      'projeto rico, formato beta a02': repositorioRico,
    };
    for (final caso in casos.entries) {
      test('${caso.key}: abre sem perder camada', () async {
        final json = jsonsDaPasta(caso.value).single;
        final lidos = await _abrirPeloRepositorio(caso.value);
        expect(lidos, hasLength(1), reason: 'o repositorio leu o projeto');
        final p = lidos.single;
        expect(p.id, json['id']);
        expect(
          todasAsCamadas(p.layers).length,
          _camadasNoJson(json['layers']),
          reason: 'nenhuma camada pode ser pulada na leitura',
        );
      });

      test(
        '${caso.key}: regravar nao muda nem perde nada do arquivo',
        () async {
          final json = jsonsDaPasta(caso.value).single;
          final p = (await _abrirPeloRepositorio(caso.value)).single;
          final pesados = <String, Object>{};
          final regravado = jsonDecode(
            jsonEncode(projectToJsonSeparado(p, pesados: pesados)),
          );
          // Os modelos ganham id novo a cada gravacao limpa; a referencia
          // muda de nome, o conteudo nao. Compara-se o projeto com as
          // referencias trocadas pelo que elas apontam.
          final antes = _semFonteSemCaminho(
            _semReferencias(json, _pesosDaPasta(caso.value)),
          );
          final depois = _semReferencias(
            regravado,
            (jsonDecode(jsonEncode(pesados)) as Map).cast<String, Object>(),
          );
          expect(divergencias(antes, depois), isEmpty);
          expect(essencial(salvarEReabrir(p)), essencial(p));
        },
      );
    }

    test('o arquivo do aparelho abre com o que tinha: duas faixas de audio, '
        'caminho do Android, efeito de atraso', () async {
      final p = (await _abrirPeloRepositorio(repositorioBetaAudio)).single;
      final audios = p.layers.whereType<AudioLayer>().toList();
      expect(audios, hasLength(2));
      expect(
        audios.every(
          (a) => a.sourcePath.startsWith('/data/user/0/com.aurea.aurea/'),
        ),
        isTrue,
      );
      expect(audios.last.audio.processing.effects, hasLength(1));
      expect(audios.last.audio.duckAmount, 0.7);
    });
  });

  group('projetos de exemplo antigos em disco (output/, fora do git)', () {
    for (final caso in projetosEmDisco.entries) {
      final existe = File(caso.value).existsSync();
      test('${caso.key}: abre, nao perde camada e regravar nao muda nada', () {
        final json = lerJsonDoProjeto(caso.value);
        final p = abrirProjetoDoDisco(caso.value);
        expect(todasAsCamadas(p.layers).length, _camadasNoJson(json['layers']));
        final cenas = p.layers.whereType<Scene3DLayer>().toList();
        expect(cenas, isNotEmpty);
        expect(
          cenas.expand((c) => c.scene.nodes).any((n) => n.modelAsset != null),
          isTrue,
          reason: 'o modelo importado (GLB) veio junto',
        );
        final regravado = jsonDecode(jsonEncode(projectToJson(p)));
        expect(divergencias(json, regravado), isEmpty);
        expect(essencial(salvarEReabrir(p)), essencial(p));
      }, skip: existe ? false : '${caso.value} nao existe nesta maquina');
    }
  });

  group('efeito que saiu do catalogo (projeto beta de antes de 16/09)', () {
    // ACHADO DESTA RODADA (anterior a UI nova; `qa_1_0_persistencia_test`
    // ja esta vermelho por isto no HEAD): o tipo sem ficha e GRAVADO com
    // `kind` derivado do nome (`effectIdOf` -> `_idDerivado`) e o indice do
    // enum DE HOJE, mas e LIDO so pelas fichas + aliases e, sem achar, pelo
    // indice da ORDEM LEGADA. O indice de hoje nao e o legado: 64 dos 89
    // tipos sem ficha voltam como OUTRO tipo, e 6 viram um efeito VIVO
    // (blobTracker -> pixelSort, bend -> motionTile, forceMotionBlur ->
    // glitchify, colorTune -> brightnessContrast, timeSlice -> twitch,
    // sFlicker -> hueSaturation). O projeto antigo abre certo na primeira
    // vez; e a primeira gravacao da versao nova que troca o efeito.
    // Correcao (fora do escopo desta rodada: `effect.dart`/`project_store`):
    // `effectTypeFromId` reconhecer tambem `effectIdOf(t)` de todo
    // `EffectType` antes de cair no indice legado.
    test(
      'salvar e reabrir duas vezes mantem o TIPO de cada efeito sem ficha',
      () {
        final trocados = <String>[];
        for (final tipo in EffectType.values) {
          if (specDe(tipo) != null) continue;
          final camada = ShapeLayer(
            id: 'f',
            name: 'F',
            startTime: Duration.zero,
            duration: const Duration(seconds: 5),
            effects: [EffectInstance(type: tipo)],
          );
          final p = VideoProject(
            name: 'efeito sem ficha',
            createdAt: DateTime(2026, 9, 21),
            layers: [camada],
          );
          final volta = salvarEReabrir(salvarEReabrir(p));
          final lido = volta.layers.single.effects.map((e) => e.type).toList();
          if (lido.length != 1 || lido.single != tipo) {
            trocados.add('${tipo.name} -> ${lido.map((t) => t.name)}');
          }
        }
        expect(trocados, isEmpty, reason: trocados.join('\n'));
      },
    );
  });
}

/// Os pesos (modelos) gravados ao lado do projeto na pasta da fixture.
Map<String, Object> _pesosDaPasta(String pasta) {
  final d = Directory('$pasta/modelos');
  if (!d.existsSync()) return const {};
  return {
    for (final f in d.listSync())
      if (f is File && f.path.endsWith('.json'))
        f.uri.pathSegments.last.replaceAll('.json', ''):
            jsonDecode(f.readAsStringSync()) as Object,
  };
}

/// A UNICA PERDA CONHECIDA NA REGRAVACAO, anterior a UI nova (02/09):
/// o no de modelo que nao veio de arquivo (bytes escolhidos pelo dono,
/// OBJ, Texto 3D) nasce com `ModelSource3D(path: '')`; o gravador escreve
/// esse bloco e o leitor (`_asModelSource`) descarta fonte sem caminho.
/// O que se perde e so a ficha da importacao (triangulos, avisos) — o
/// unico leitor dela e o motor, que so olha `path` e trata vazio como
/// ausente. Nada na tela nem no render muda; por isso a comparacao tira
/// esse bloco do arquivo antigo, e SO quando o caminho e vazio.
Object? _semFonteSemCaminho(Object? json) {
  if (json is Map) {
    return {
      for (final e in json.entries)
        if (!(e.key == 'modelSource' &&
            e.value is Map &&
            ((e.value as Map)['path'] ?? '') == ''))
          e.key: _semFonteSemCaminho(e.value),
    };
  }
  if (json is List) return [for (final v in json) _semFonteSemCaminho(v)];
  return json;
}

/// O projeto com cada `{"ref": id}` trocado pelo conteudo do peso.
Object? _semReferencias(Object? json, Map<String, Object> pesos) {
  if (json is Map) {
    if (json.length == 1 && json['ref'] is String) {
      return _semReferencias(pesos[json['ref']], pesos);
    }
    return {
      for (final e in json.entries) e.key: _semReferencias(e.value, pesos),
    };
  }
  if (json is List) return [for (final v in json) _semReferencias(v, pesos)];
  return json;
}
