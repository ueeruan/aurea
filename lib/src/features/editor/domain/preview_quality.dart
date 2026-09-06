import 'dart:math' as math;

/// A preview allocation budget, independent from project/export resolution.
double scenePreviewScale(
  double width,
  double height, {
  required bool interacting,
  bool exporting = false,
}) {
  if (exporting) return 1;
  final longest = math.max(width, height);
  if (!longest.isFinite || longest <= 0) return 1;
  final limit = interacting ? 720.0 : 1080.0;
  return math.min(1.0, limit / longest);
}
