/// Feedback is explicitly labeled by the caller. Flutter raster duration does
/// not measure an external Filament GPU pass.
enum TimingSource { gpu, flutterFrame, submission }

class AdaptiveQuality {
  AdaptiveQuality({this.targetFps = 60});
  final int targetFps;
  int _level = 10,
      _slow = 0,
      _fast = 0,
      _nativeLevel = 10,
      _nativeSlow = 0,
      _nativeFast = 0;
  double? _average;
  double get scale => _level / 10;
  int get _effects => _level < _nativeLevel ? _level : _nativeLevel;
  bool get shadows => _effects >= 7;
  bool get ambientOcclusion => _effects >= 9;
  bool get msaa => _effects >= 9;
  int get lodBias => _effects >= 9 ? 0 : (_effects >= 7 ? 1 : 2);
  void observeNativeScale(double value) {
    if (!value.isFinite || value <= 0 || value > 1) return;
    _nativeSlow = value < .8 ? _nativeSlow + 1 : 0;
    _nativeFast = value > .95 ? _nativeFast + 1 : 0;
    if (_nativeSlow >= 12 && _nativeLevel > 5) {
      _nativeLevel--;
      _nativeSlow = 0;
    }
    if (_nativeFast >= 120 && _nativeLevel < 10) {
      _nativeLevel++;
      _nativeFast = 0;
    }
  }

  double resolution({bool exporting = false}) => exporting ? 1 : scale;

  bool sample(double milliseconds, {required TimingSource source}) {
    if (!milliseconds.isFinite ||
        milliseconds <= 0 ||
        source == TimingSource.submission) {
      return false;
    }
    final budget = 1000 / targetFps;
    _average = _average == null
        ? milliseconds
        : _average! * .85 + milliseconds * .15;
    _slow = _average! > budget * 1.10 ? _slow + 1 : 0;
    _fast = _average! < budget * .72 ? _fast + 1 : 0;
    if (_slow >= 12 && _level > 5) {
      _level--;
      _slow = 0;
      _fast = 0;
      return true;
    }
    if (_fast >= 120 && _level < 10) {
      _level++;
      _slow = 0;
      _fast = 0;
      return true;
    }
    return false;
  }

  void reset() {
    _level = 10;
    _slow = 0;
    _fast = 0;
    _average = null;
    _nativeLevel = 10;
    _nativeSlow = 0;
    _nativeFast = 0;
  }
}
