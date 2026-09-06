import 'dart:convert';
import 'dart:io';
import 'dart:isolate';

import '../../editor/domain/project_store.dart';
import '../domain/abyss_cinematic_template.dart';

const bundledAbyssProjectId = 'abyss_cinematic_template';

/// Install the authored example once, not on every launch. The receipt is
/// separate from the project so edits and intentional deletions are respected.
/// The template in Models remains available even after deleting this copy.
Future<void> installBundledAbyss(Directory directory) async {
  final receipt = File('${directory.path}/.installed-abyss-v1');
  if (await receipt.exists()) return;
  final destination = File('${directory.path}/$bundledAbyssProjectId.json');
  if (!await destination.exists()) {
    // Mesh generation / JSON encoding must not stall the home screen.
    final encoded = await Isolate.run(() {
      final data = projectToJson(buildAbyssCinematicTemplate());
      data['createdAt'] = DateTime.now().toIso8601String();
      return jsonEncode(data);
    });
    final staging = File('${directory.path}/.install-abyss-v1.tmp');
    await staging.writeAsString(encoded, flush: true);
    // Recheck after asynchronous work; never replace a project saved meanwhile.
    if (!await destination.exists()) {
      await staging.rename(destination.path);
    } else {
      await staging.delete();
    }
  }
  await receipt.writeAsString(bundledAbyssProjectId, flush: true);
}
