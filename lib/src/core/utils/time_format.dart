/// "m:ss.d" — segundos com um decimal (padrao de editores mobile).
String formatTime(Duration d) {
  final totalMs = d.inMilliseconds;
  final m = totalMs ~/ 60000;
  final s = (totalMs % 60000) ~/ 1000;
  final tenths = (totalMs % 1000) ~/ 100;
  return '$m:${s.toString().padLeft(2, '0')}.$tenths';
}

/// Timecode "MM:SS:FF" (frames), como no relogio central do editor.
String formatTimecode(Duration d, int fps) {
  final totalUs = d.inMicroseconds;
  final m = totalUs ~/ 60000000;
  final s = (totalUs % 60000000) ~/ 1000000;
  final frames = ((totalUs % 1000000) * fps ~/ 1000000);
  String two(int v) => v.toString().padLeft(2, '0');
  return '${two(m)}:${two(s)}:${two(frames)}';
}
