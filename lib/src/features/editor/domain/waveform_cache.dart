import 'dart:typed_data';

/// O CACHE DA FORMA DE ONDA EM DISCO (versao 2).
///
/// A v1 (`.pk`) guardava so o envelope de 100 picos por segundo, e a
/// piramide era remontada dele como se cada pico fosse uma amostra: o
/// balde mais fino virava 0,64 s. A v2 guarda tambem a BASE de desenho —
/// baldes de 4 ms com minimo, maximo e RMS — e diz se o arquivo tem som,
/// para um video mudo nao rodar o FFmpeg de novo a cada sessao.
///
/// Layout (little endian):
///   'AWF2' | versao u32 | taxa u32 | amostras/balde u32 | flags u32
///   | lufs f64 (NaN = nenhum) | picos/s u32 | nPicos u32 | nBase u32
///   | picos f32 x nPicos | min i8 x nBase | max i8 x nBase | rms u8 x nBase
///
/// min/max sao quantizados para 8 bits: e so para desenhar (2,7 MB por
/// hora de audio em disco). O envelope de 100/s, que os detectores de
/// silencio e batida leem, fica em float.
class WaveformCacheData {
  const WaveformCacheData({
    required this.hasAudio,
    required this.sampleRate,
    required this.samplesPerBucket,
    required this.lufs,
    required this.peaksPerSecond,
    required this.peaks,
    required this.baseMin,
    required this.baseMax,
    required this.baseRms,
  });

  factory WaveformCacheData.semAudio() => WaveformCacheData(
    hasAudio: false,
    sampleRate: 16000,
    samplesPerBucket: 64,
    lufs: null,
    peaksPerSecond: 100,
    peaks: Float32List(0),
    baseMin: Float32List(0),
    baseMax: Float32List(0),
    baseRms: Float32List(0),
  );

  final bool hasAudio;
  final int sampleRate;
  final int samplesPerBucket;
  final double? lufs;
  final int peaksPerSecond;
  final Float32List peaks;
  final Float32List baseMin;
  final Float32List baseMax;
  final Float32List baseRms;
}

const _magic = 0x32465741; // 'AWF2'
const _versao = 1;
const _cabecalho = 40;

Uint8List encodeWaveformCache(WaveformCacheData d) {
  final nP = d.peaks.length, nB = d.baseMax.length;
  final out = Uint8List(_cabecalho + nP * 4 + nB * 3);
  final bd = ByteData.sublistView(out);
  bd.setUint32(0, _magic, Endian.little);
  bd.setUint32(4, _versao, Endian.little);
  bd.setUint32(8, d.sampleRate, Endian.little);
  bd.setUint32(12, d.samplesPerBucket, Endian.little);
  bd.setUint32(16, d.hasAudio ? 1 : 0, Endian.little);
  bd.setFloat64(20, d.lufs ?? double.nan, Endian.little);
  bd.setUint32(28, d.peaksPerSecond, Endian.little);
  bd.setUint32(32, nP, Endian.little);
  bd.setUint32(36, nB, Endian.little);
  var o = _cabecalho;
  for (var i = 0; i < nP; i++, o += 4) {
    bd.setFloat32(o, d.peaks[i], Endian.little);
  }
  int q8(double v) => (v.clamp(-1.0, 1.0) * 127).round();
  for (var i = 0; i < nB; i++) {
    bd.setInt8(o + i, q8(d.baseMin[i]));
    bd.setInt8(o + nB + i, q8(d.baseMax[i]));
    bd.setUint8(o + 2 * nB + i, (d.baseRms[i].clamp(0.0, 1.0) * 255).round());
  }
  return out;
}

/// Devolve nulo se o arquivo nao for um cache v2 valido (truncado,
/// versao desconhecida): quem chama refaz a analise.
WaveformCacheData? decodeWaveformCache(Uint8List bytes) {
  if (bytes.length < _cabecalho) return null;
  final bd = ByteData.sublistView(bytes);
  if (bd.getUint32(0, Endian.little) != _magic) return null;
  if (bd.getUint32(4, Endian.little) != _versao) return null;
  final nP = bd.getUint32(32, Endian.little);
  final nB = bd.getUint32(36, Endian.little);
  if (bytes.length != _cabecalho + nP * 4 + nB * 3) return null;
  final lufs = bd.getFloat64(20, Endian.little);
  final peaks = Float32List(nP);
  var o = _cabecalho;
  for (var i = 0; i < nP; i++, o += 4) {
    peaks[i] = bd.getFloat32(o, Endian.little);
  }
  final mn = Float32List(nB), mx = Float32List(nB), rm = Float32List(nB);
  for (var i = 0; i < nB; i++) {
    mn[i] = bd.getInt8(o + i) / 127.0;
    mx[i] = bd.getInt8(o + nB + i) / 127.0;
    rm[i] = bd.getUint8(o + 2 * nB + i) / 255.0;
  }
  return WaveformCacheData(
    hasAudio: bd.getUint32(16, Endian.little) & 1 == 1,
    sampleRate: bd.getUint32(8, Endian.little),
    samplesPerBucket: bd.getUint32(12, Endian.little),
    lufs: lufs.isNaN ? null : lufs,
    peaksPerSecond: bd.getUint32(28, Endian.little),
    peaks: peaks,
    baseMin: mn,
    baseMax: mx,
    baseRms: rm,
  );
}

/// Ganho de EXIBICAO: leva o percentil 99 dos picos a 90% da faixa, entre
/// 1x e 12x. Uma fala gravada baixa continua legivel sem que um estalo
/// isolado achate o resto.
double waveformDisplayGain(Float32List peaks) {
  if (peaks.isEmpty) return 1;
  final ordenados = Float32List.fromList(peaks)..sort();
  final p99 = ordenados[((ordenados.length - 1) * 0.99).round()];
  if (p99 <= 1e-4) return 1;
  return (0.9 / p99).clamp(1.0, 12.0);
}
