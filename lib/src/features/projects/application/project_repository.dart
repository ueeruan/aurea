import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';

import '../../editor/application/registro_de_travadas.dart';
import '../../editor/domain/project_store.dart';
import '../../editor/domain/video_project.dart';

/// Persistencia dos projetos: um JSON por projeto em
/// `<documentos do app>/projects/<id>.json`, escrito de forma atomica
/// (.tmp -> rename) para nunca deixar arquivo pela metade.
class ProjectRepository {
  ProjectRepository({Directory? directory}) : _cached = directory;

  Directory? _cached;
  final Map<String, Future<void>> _writes = {};

  Future<void> _enqueue(String id, Future<void> Function() action) {
    final previous = _writes[id] ?? Future<void>.value();
    final next = previous.catchError((Object _) {}).then((_) => action());
    _writes[id] = next;
    return next.whenComplete(() {
      if (identical(_writes[id], next)) _writes.remove(id);
    });
  }

  Future<Directory> _dir() async {
    if (_cached != null) return _cached!;
    final docs = await getApplicationDocumentsDirectory();
    final d = Directory('${docs.path}/projects');
    if (!d.existsSync()) d.createSync(recursive: true);
    return _cached = d;
  }

  Future<List<VideoProject>> loadAll() async {
    final dir = await _dir();
    await Future.wait(
      _writes.values.toList().map((f) => f.catchError((Object _) {})),
    );
    final out = <VideoProject>[];
    // UM ISOLATE PARA A LISTA INTEIRA, nunca um por projeto: abrir um
    // isolate custa ~1,4 s SINCRONOS no iPhone (medido em 09/2026), e
    // era um por arquivo — dez projetos seguravam a Home por mais de
    // dez segundos "carregando". O lote le e decodifica tudo numa
    // viagem so; arquivo corrompido vira nulo e nao derruba os outros.
    final paths = [
      for (final f in dir.listSync())
        if (f is File && f.path.endsWith('.json')) f.path,
    ];
    if (paths.isNotEmpty) {
      // O PEDACO SINCRONO do compute (abrir o isolate e copiar a mensagem)
      // vai para o registro de travadas: no iPhone 13 abrir isolate ja
      // custou 1,4 s no fio da interface, e este caminho nunca foi medido.
      final jsons = await RegistroDeTravadas.marcando(
        'projetos: abrir isolate de leitura',
        () => compute(_readProjectJsons, paths),
      );
      for (var i = 0; i < paths.length; i++) {
        final json = jsons[i];
        if (json == null) {
          debugPrint('Projeto ilegivel ${paths[i]}');
          continue;
        }
        try {
          out.add(await _comOsPesos(json));
        } catch (e) {
          debugPrint('Projeto ilegivel ${paths[i]}: $e');
        }
      }
    }
    out.sort((a, b) => b.createdAt.compareTo(a.createdAt));
    return out;
  }

  /// O id vira NOME DE ARQUIVO. Ele nasce de um uuid, mas um projeto
  /// vindo de fora (importacao, arquivo editado na mao) poderia trazer
  /// "../" e escrever fora da pasta — entao so passa o que e seguro.
  static String _safeId(String id) {
    final clean = id.replaceAll(RegExp(r'[^A-Za-z0-9_-]'), '');
    return clean.isEmpty ? 'projeto' : clean;
  }

  Future<void> flush() async {
    await Future.wait(_writes.values.toList());
  }

  /// QUANDO O PROJETO FOI GRAVADO PELA ULTIMA VEZ — a "ultima edicao" que
  /// a Inicio mostra no cartao.
  ///
  /// O modelo nao guarda essa data (so `createdAt`), e o arquivo e gravado
  /// a cada edicao: a data dele E a ultima edicao. Mora aqui porque so o
  /// repositorio sabe onde o arquivo fica. Um `stat` sincrono, barato;
  /// nulo enquanto a pasta nao e conhecida (antes do primeiro [loadAll])
  /// ou o arquivo ainda nao existe.
  DateTime? editadoEm(String id) {
    final dir = _cached;
    if (dir == null) return null;
    try {
      final stat = File('${dir.path}/${_safeId(id)}.json').statSync();
      if (stat.type == FileSystemEntityType.notFound) return null;
      return stat.modified;
    } catch (_) {
      return null;
    }
  }

  /// A pasta dos PESOS: um arquivo por modelo importado, ao lado dos
  /// projetos. Um modelo nao muda depois de importado, entao ele e
  /// escrito uma vez e nunca mais — e o salvamento automatico volta a
  /// mexer so nos poucos quilobytes do resto do projeto.
  Future<Directory> _dirDosPesos() async {
    final dir = await _dir();
    final d = Directory('${dir.path}/modelos');
    if (!d.existsSync()) d.createSync(recursive: true);
    return d;
  }

  /// Os ids de peso que ja estao em disco. Lido uma vez e mantido, para
  /// nem montar o conteudo de um modelo que ja foi gravado.
  Set<String>? _pesosEmDisco;

  Future<Set<String>> _idsEmDisco() async {
    if (_pesosEmDisco != null) return _pesosEmDisco!;
    final d = await _dirDosPesos();
    return _pesosEmDisco = {
      for (final f in d.listSync())
        if (f is File && f.path.endsWith('.json'))
          f.uri.pathSegments.last.replaceAll('.json', ''),
    };
  }

  /// O projeto lido, com os modelos que moram nos arquivos ao lado.
  Future<VideoProject> _comOsPesos(Map<String, dynamic> json) async {
    final ids = refsDeProjeto(json);
    if (ids.isEmpty) return projectFromJson(json);
    final d = await _dirDosPesos();
    final pesados = <String, Object>{};
    for (final id in ids) {
      final f = File('${d.path}/${_safeId(id)}.json');
      if (!f.existsSync()) continue;
      try {
        pesados[id] = await RegistroDeTravadas.marcando(
          'projetos: abrir isolate do modelo',
          () => compute(_readPeso, f.path),
        );
      } catch (e) {
        // Um modelo ilegivel tira o modelo, nao o projeto.
        debugPrint('Modelo do projeto nao carregou ($id): $e');
      }
    }
    return projectFromJsonComPesos(json, pesados);
  }

  Future<void> save(VideoProject project) => _enqueue(
    _safeId(project.id),
    () async {
      final dir = await _dir();
      final path = '${dir.path}/${_safeId(project.id)}.json';
      final pastaDosPesos = await _dirDosPesos();
      final jaTem = await _idsEmDisco();
      // O MODELO SAI DO PROJETO. Antes, a geometria de um glTF importado
      // ia dentro do arquivo: 18,8 MB e quase 350 ms por salvamento
      // automatico num modelo de 60 mil triangulos, no fio que responde ao
      // toque. Agora o projeto guarda uma referencia e o modelo vai num
      // arquivo proprio, escrito uma unica vez.
      final pesados = <String, Object>{};
      final mapa = RegistroDeTravadas.marcando(
        'gravando o projeto',
        () =>
            projectToJsonSeparado(project, pesados: pesados, jaGravados: jaTem),
      );
      // OS PESOS PRIMEIRO, sempre. Se a gravacao morrer no meio, o que
      // sobra e um modelo orfao ocupando espaco — nunca um projeto
      // apontando para um modelo que nao existe.
      for (final e in pesados.entries) {
        // O modelo inteiro e COPIADO para o isolate aqui, de forma
        // sincrona. Uma vez por modelo, mas no fio da interface.
        await RegistroDeTravadas.marcando(
          'gravando o projeto: copiar modelo para o isolate',
          () => compute(_writePeso, (
            '${pastaDosPesos.path}/${_safeId(e.key)}.json',
            e.value,
          )),
        );
        jaTem.add(e.key);
      }
      await RegistroDeTravadas.marcando(
        'gravando o projeto: abrir isolate',
        () => compute(_writeProjectJson, (path, mapa)),
      );
    },
  );

  Future<void> delete(String id) => _enqueue(_safeId(id), () async {
    final dir = await _dir();
    final file = File('${dir.path}/${_safeId(id)}.json');
    if (await file.exists()) await file.delete();
  });
}

Map<String, dynamic> _readProjectJson(String path) =>
    jsonDecode(File(path).readAsStringSync()) as Map<String, dynamic>;

/// O LOTE do [loadAll]: todos os arquivos numa viagem de isolate so.
/// Nulo na posicao de um arquivo que nao deu para ler.
List<Map<String, dynamic>?> _readProjectJsons(List<String> paths) => [
  for (final p in paths)
    () {
      try {
        return _readProjectJson(p);
      } catch (_) {
        return null;
      }
    }(),
];

Object _readPeso(String path) => jsonDecode(File(path).readAsStringSync());

void _writePeso((String, Object) message) {
  final tmp = File('${message.$1}.tmp');
  tmp.writeAsStringSync(jsonEncode(message.$2), flush: true);
  tmp.renameSync(message.$1);
}

void _writeProjectJson((String, Map<String, dynamic>) message) {
  final tmp = File('${message.$1}.tmp');
  tmp.writeAsStringSync(jsonEncode(message.$2), flush: true);
  tmp.renameSync(message.$1);
}
