// Alinhamento do texto no arquivo: centro (o padrao) nao se grava; o
// resto volta do JSON e sobrevive a duplicar e a outras edicoes.
import 'dart:convert';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

class _Projetos extends ProjectsController {
  @override
  List<VideoProject> build() => const [];
}

ProviderContainer _container() {
  final c = ProviderContainer(
    overrides: [projectsControllerProvider.overrideWith(_Projetos.new)],
  );
  addTearDown(c.dispose);
  return c;
}

TextLayer _texto(ProviderContainer c) =>
    c.read(editorControllerProvider).layers.whereType<TextLayer>().first;

void main() {
  test('o alinhamento volta do arquivo (centro nao se grava)', () {
    final c = _container();
    final e = c.read(editorControllerProvider.notifier);
    e.addTextLayer(Duration.zero);
    final id = _texto(c).id;
    final semAlinhamento = jsonEncode(
      projectToJson(c.read(editorControllerProvider)),
    );
    expect(semAlinhamento.contains('"align"'), isFalse);

    e.editTextLayer(id, alinhamento: TextAlign.right);
    final volta = projectFromJson(projectToJson(c.read(editorControllerProvider)));
    expect(volta.layers.whereType<TextLayer>().single.alinhamento, TextAlign.right);
    // Duplicar e editar outra coisa nao perdem o alinhamento.
    expect(_texto(c).duplicated().alinhamento, TextAlign.right);
    e.editTextLayer(id, fontSize: 50);
    expect(_texto(c).alinhamento, TextAlign.right);
  });
}
