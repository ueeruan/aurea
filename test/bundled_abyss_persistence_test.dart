import 'dart:io';
import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/projects/domain/abyss_cinematic_template.dart';
import 'package:aurea/src/features/projects/application/bundled_project_installation.dart';
import 'package:aurea/src/features/projects/application/project_repository.dart';

void main() {
  late Directory folder;
  setUp(() async {
    folder = await Directory.systemTemp.createTemp('aurea-abyss-');
  });
  tearDown(() async {
    await folder.delete(recursive: true);
  });

  test('existing project is not overwritten before the installation receipt exists', () async {
    final file = File('${folder.path}/$bundledAbyssProjectId.json');
    final existing = buildAbyssCinematicTemplate().copyWith(
      name: 'Ja editado antes da atualizacao',
    );
    final encoded = jsonEncode(projectToJson(existing));
    await file.writeAsString(encoded);
    final loaded = await ProjectRepository(directory: folder).loadAll();
    expect(loaded.single.name, existing.name);
    expect(await file.readAsString(), encoded);
  });

  test(
    'ABISMO is saved on first launch with geometry, rig and all four cameras',
    () async {
      final repo = ProjectRepository(directory: folder);
      final projects = await repo.loadAll();
      final project = projects.single;
      expect(project.id, bundledAbyssProjectId);
      final layer = project.layers.single as Scene3DLayer;
      expect(layer.allCameras.length, 4);
      expect(
        layer.scene.nodeById('abyss_explorer')!.modelAsset!.joints.length,
        17,
      );
      expect(
        File('${folder.path}/$bundledAbyssProjectId.json').existsSync(),
        isTrue,
      );
      expect((await repo.loadAll()).length, 1);
    },
  );

  test('next launch preserves edits and unrelated projects', () async {
    final repo = ProjectRepository(directory: folder);
    final original = (await repo.loadAll()).single;
    await repo.save(original.copyWith(name: 'Minha versao do ABISMO'));
    await repo.save(VideoProject.empty('Meu outro projeto'));
    final projects = await ProjectRepository(directory: folder).loadAll();
    expect(projects.length, 2);
    expect(
      projects.singleWhere((p) => p.id == bundledAbyssProjectId).name,
      'Minha versao do ABISMO',
    );
    expect(projects.any((p) => p.name == 'Meu outro projeto'), isTrue);
  });

  test(
    'deleting the saved example does not respawn it on next launch',
    () async {
      final repo = ProjectRepository(directory: folder);
      await repo.loadAll();
      await repo.delete(bundledAbyssProjectId);
      expect(await ProjectRepository(directory: folder).loadAll(), isEmpty);
    },
  );

  test(
    'parallel reads share one install and leave one valid project',
    () async {
      final repo = ProjectRepository(directory: folder);
      final results = await Future.wait([repo.loadAll(), repo.loadAll()]);
      expect(results.every((p) => p.length == 1), isTrue);
      expect(results[0].single.id, results[1].single.id);
      expect(
        folder.listSync().whereType<File>().where(
          (f) => f.path.endsWith('.tmp'),
        ),
        isEmpty,
      );
    },
  );
}
