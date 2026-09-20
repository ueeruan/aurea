import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/projects/application/project_repository.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  test('asynchronous autosaves stay ordered and deletion cannot resurrect a project', () async {
    final dir = await Directory.systemTemp.createTemp('aurea-repository-');
    addTearDown(() => dir.delete(recursive: true));
    final repo = ProjectRepository(
      directory: dir,
    );
    final project = VideoProject.empty('First');
    await Future.wait([
      for (var i = 0; i < 12; i++)
        repo.save(project.copyWith(name: 'Revision $i')),
    ]);
    expect((await repo.loadAll()).single.name, 'Revision 11');
    final saving = repo.save(project.copyWith(name: 'Before delete'));
    final removing = repo.delete(project.id);
    await Future.wait([saving, removing]);
    expect(await repo.loadAll(), isEmpty);
    expect(
      dir.listSync().whereType<File>().where((f) => f.path.endsWith('.tmp')),
      isEmpty,
    );
  });
}
