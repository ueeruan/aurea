import 'dart:convert';

import 'layer.dart';
import 'layer_meta.dart';
import 'project_store.dart';
import 'video_project.dart';

/// PACOTE DE TEMPLATE.
///
/// Um projeto que vira MODELO: o mesmo lower third, a mesma vinheta, com
/// outro texto e outra cor, sem ninguem abrir a linha do tempo e correr
/// o risco de mover a camada errada.
///
/// O pacote e o projeto inteiro mais um cabecalho com o que a pessoa
/// pode mexer. As propriedades expostas ja existiam no modelo — o que
/// faltava era um jeito de LEVAR o template para outro aparelho e de
/// aplicar sem estragar o resto.
class TemplatePack {
  const TemplatePack({
    required this.name,
    required this.project,
    this.author = '',
    this.notes = '',
    this.version = 1,
  });

  final String name;
  final String author;
  final String notes;
  final int version;

  /// O projeto completo, com as camadas e as propriedades expostas.
  final VideoProject project;

  List<ExposedProperty> get fields => project.exposed;

  /// Campos agrupados, na ordem em que foram expostos — e a ordem em
  /// que a pessoa preencheu o formulario ao montar o template.
  Map<String, List<ExposedProperty>> get byGroup {
    final out = <String, List<ExposedProperty>>{};
    for (final f in fields) {
      out.putIfAbsent(f.group, () => []).add(f);
    }
    return out;
  }

  Map<String, dynamic> toJson() => {
        'aurea': 'template',
        'v': version,
        'name': name,
        if (author.isNotEmpty) 'author': author,
        if (notes.isNotEmpty) 'notes': notes,
        'project': projectToJson(project),
      };

  String encode() => const JsonEncoder.withIndent('  ').convert(toJson());

  static TemplatePack? decode(String source) {
    try {
      final m = jsonDecode(source);
      if (m is! Map) return null;
      final mapa = m.cast<String, dynamic>();
      if (mapa['aurea'] != 'template') return null;
      final proj = mapa['project'];
      if (proj is! Map) return null;
      return TemplatePack(
        name: (mapa['name'] as String?) ?? 'Template',
        author: (mapa['author'] as String?) ?? '',
        notes: (mapa['notes'] as String?) ?? '',
        version: (mapa['v'] as num?)?.toInt() ?? 1,
        project: projectFromJson(proj.cast<String, dynamic>()),
      );
    } catch (_) {
      // Arquivo estragado nao pode derrubar o aplicativo — quem chamou
      // mostra "nao consegui ler" e segue.
      return null;
    }
  }
}

/// O que impede um template de funcionar na mao de outra pessoa.
///
/// Vale a MESMA regra do validador de Lottie: avisar ANTES, na hora de
/// empacotar, e melhor do que a pessoa descobrir que o campo nao mexe em
/// nada depois de mandar o arquivo.
class TemplateIssue {
  const TemplateIssue(this.message, {this.blocking = false});

  final String message;
  final bool blocking;
}

List<TemplateIssue> validateTemplate(VideoProject p) {
  final out = <TemplateIssue>[];

  if (p.layers.isEmpty) {
    out.add(const TemplateIssue('O projeto esta vazio.', blocking: true));
  }
  if (p.exposed.isEmpty) {
    out.add(const TemplateIssue(
        'Nenhuma propriedade exposta: quem receber nao vai poder mudar '
        'nada.',
        blocking: true));
  }

  // Campo apontando para camada que nao existe e o defeito classico de
  // template: some depois de alguem apagar a camada, e o formulario
  // continua mostrando o campo que nao faz nada.
  final ids = {
    for (final l in _todasAsCamadas(p.layers)) l.id,
  };
  for (final e in p.exposed) {
    if (!ids.contains(e.layerId)) {
      out.add(TemplateIssue(
          'O campo "${e.label}" aponta para uma camada que nao existe '
          'mais.',
          blocking: true));
    }
  }

  final rotulos = <String>{};
  for (final e in p.exposed) {
    if (!rotulos.add('${e.group}/${e.label}')) {
      out.add(TemplateIssue(
          'Dois campos chamados "${e.label}" no grupo "${e.group}" — '
          'quem preencher nao vai saber qual e qual.'));
    }
  }

  final midiaExterna = <String>{
    for (final l in _todasAsCamadas(p.layers))
      if (l is VideoLayer)
        l.sourcePath
      else if (l is AudioLayer)
        l.sourcePath
      else if (l is ImageLayer)
        l.sourcePath,
  };
  if (midiaExterna.isNotEmpty) {
    out.add(TemplateIssue(
        '${midiaExterna.length} arquivo(s) de midia ficam de fora do '
        'pacote: o template leva o projeto, nao os videos.'));
  }

  return out;
}

Iterable<Layer> _todasAsCamadas(List<Layer> layers) sync* {
  for (final l in layers) {
    yield l;
    if (l is GroupLayer) yield* _todasAsCamadas(l.children);
  }
}
