import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/projects/domain/pindown_motion_template.dart';
import 'package:aurea/src/features/projects/domain/notes_motion_template.dart';

void main() {
  test('ids unicos', () {
    for (final p in [buildPindownMotionTemplate(), buildNotesMotionTemplate()]) {
      final vistos = <String>{};
      final dup = <String>[];
      for (final l in p.layers) {
        if (!vistos.add(l.id)) dup.add(l.id);
      }
      expect(dup, isEmpty, reason: '${p.name}: ids repetidos $dup');
    }
  });
}
