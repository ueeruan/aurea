import 'package:integration_test/integration_test.dart';

import '../test/glow_particles_regressions_test.dart' as regressions;

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();
  regressions.main();
}
