import 'dart:math' as math;

class ProbePoint3D {
  const ProbePoint3D(this.x, this.y, this.z);

  final double x;
  final double y;
  final double z;

  static const zero = ProbePoint3D(0, 0, 0);
}

/// Os seis ambientes prontos do Nivel 8.
enum PanoramaPreset { estudio, porDoSol, noite, neon, branco, interior }

String panoramaPresetLabel(PanoramaPreset preset) => switch (preset) {
  PanoramaPreset.estudio => 'Estudio',
  PanoramaPreset.porDoSol => 'Por do sol',
  PanoramaPreset.noite => 'Noite',
  PanoramaPreset.neon => 'Neon',
  PanoramaPreset.branco => 'Branco',
  PanoramaPreset.interior => 'Interior',
};

enum PanoramaSource { preset, file, camera, layer }

/// Ambiente estatico. A imagem e decodificada/consolidada uma vez pelo cache
/// de panorama; o render so consulta o resultado pronto.
class Panorama3D {
  const Panorama3D({
    this.preset = PanoramaPreset.estudio,
    this.source = PanoramaSource.preset,
    this.sourcePath,
    this.sourceLayerId,
    this.rotationDegrees = 0,
    this.intensity = 1,
    this.backgroundBlur = 0,
    this.showBackground = false,
    this.highlightBoost = 0,
    this.approximate = false,
    this.coverageDegrees = 360,
    this.mirrorTo360 = false,
    this.seamSoftness = 0,
    this.fillZenithNadir = false,
    this.convertedAtImport = false,
  });

  final PanoramaPreset preset;
  final PanoramaSource source;
  final String? sourcePath;
  final String? sourceLayerId;

  /// Um unico angulo move reflexo e luz ambiente.
  final double rotationDegrees;
  final double intensity;
  final double backgroundBlur;
  final bool showBackground;
  final double highlightBoost;

  /// Fotos comuns do telefone cobrem so parte da esfera e nao sao HDR.
  final bool approximate;
  final double coverageDegrees;
  final bool mirrorTo360;
  final double seamSoftness;
  final bool fillZenithNadir;

  /// Sinaliza que a preparacao custosa ja ocorreu na importacao.
  final bool convertedAtImport;

  bool get hasImage => sourcePath != null && sourcePath!.isNotEmpty;
  bool get comesFromLayer =>
      source == PanoramaSource.layer && sourceLayerId != null;

  Panorama3D copyWith({
    PanoramaPreset? preset,
    PanoramaSource? source,
    String? sourcePath,
    String? sourceLayerId,
    double? rotationDegrees,
    double? intensity,
    double? backgroundBlur,
    bool? showBackground,
    double? highlightBoost,
    bool? approximate,
    double? coverageDegrees,
    bool? mirrorTo360,
    double? seamSoftness,
    bool? fillZenithNadir,
    bool? convertedAtImport,
    bool clearSource = false,
    bool clearLayer = false,
  }) => Panorama3D(
    preset: preset ?? this.preset,
    source: source ?? this.source,
    sourcePath: clearSource ? null : (sourcePath ?? this.sourcePath),
    sourceLayerId: clearLayer ? null : (sourceLayerId ?? this.sourceLayerId),
    rotationDegrees: rotationDegrees ?? this.rotationDegrees,
    intensity: intensity ?? this.intensity,
    backgroundBlur: backgroundBlur ?? this.backgroundBlur,
    showBackground: showBackground ?? this.showBackground,
    highlightBoost: highlightBoost ?? this.highlightBoost,
    approximate: approximate ?? this.approximate,
    coverageDegrees: coverageDegrees ?? this.coverageDegrees,
    mirrorTo360: mirrorTo360 ?? this.mirrorTo360,
    seamSoftness: seamSoftness ?? this.seamSoftness,
    fillZenithNadir: fillZenithNadir ?? this.fillZenithNadir,
    convertedAtImport: convertedAtImport ?? this.convertedAtImport,
  );
}

/// Plano deterministico do tratamento feito uma unica vez na importacao.
/// O render nao decide essas correcoes por quadro.
Panorama3D preparePanorama({
  required String path,
  double coverageDegrees = 360,
  bool capturedWithPhone = false,
}) {
  final coverage = coverageDegrees.clamp(1.0, 360.0).toDouble();
  final incomplete = coverage < 300;
  return Panorama3D(
    source: capturedWithPhone ? PanoramaSource.camera : PanoramaSource.file,
    sourcePath: path,
    approximate: capturedWithPhone || incomplete,
    coverageDegrees: coverage,
    mirrorTo360: incomplete,
    seamSoftness: incomplete ? 0.16 : 0.02,
    fillZenithNadir: capturedWithPhone || incomplete,
    highlightBoost: capturedWithPhone ? 0.35 : 0,
    convertedAtImport: true,
  );
}

enum ProbeQuality { low, medium, high }

extension ProbeQualityInfo on ProbeQuality {
  int get faceResolution => switch (this) {
    ProbeQuality.low => 128,
    ProbeQuality.medium => 256,
    ProbeQuality.high => 512,
  };

  String get label => switch (this) {
    ProbeQuality.low => 'Baixa',
    ProbeQuality.medium => 'Media',
    ProbeQuality.high => 'Alta',
  };
}

enum ProbeUpdateMode { stopped, onMove, continuous }

extension ProbeUpdateModeInfo on ProbeUpdateMode {
  String get label => switch (this) {
    ProbeUpdateMode.stopped => 'Parado',
    ProbeUpdateMode.onMove => 'Ao mover',
    ProbeUpdateMode.continuous => 'Continuo',
  };
}

class ReflectionProbe3D {
  const ReflectionProbe3D({
    this.id = 'scene-probe',
    this.enabled = false,
    this.quality = ProbeQuality.low,
    this.updateMode = ProbeUpdateMode.onMove,
    this.perObject = false,
    this.position = ProbePoint3D.zero,
    this.includeNodeIds = const {},
    this.excludeNodeIds = const {},
  });

  final String id;
  final bool enabled;
  final ProbeQuality quality;
  final ProbeUpdateMode updateMode;
  final bool perObject;
  final ProbePoint3D position;
  final Set<String> includeNodeIds;
  final Set<String> excludeNodeIds;

  /// O proprio objeto nunca entra, mesmo se estiver explicitamente incluido.
  bool includes(String nodeId, {String? reflectiveNodeId}) {
    if (!enabled || nodeId == reflectiveNodeId) return false;
    if (excludeNodeIds.contains(nodeId)) return false;
    return includeNodeIds.isEmpty || includeNodeIds.contains(nodeId);
  }

  ReflectionProbe3D copyWith({
    bool? enabled,
    ProbeQuality? quality,
    ProbeUpdateMode? updateMode,
    bool? perObject,
    ProbePoint3D? position,
    Set<String>? includeNodeIds,
    Set<String>? excludeNodeIds,
  }) => ReflectionProbe3D(
    id: id,
    enabled: enabled ?? this.enabled,
    quality: quality ?? this.quality,
    updateMode: updateMode ?? this.updateMode,
    perObject: perObject ?? this.perObject,
    position: position ?? this.position,
    includeNodeIds: includeNodeIds ?? this.includeNodeIds,
    excludeNodeIds: excludeNodeIds ?? this.excludeNodeIds,
  );
}

/// Uma face do cubo por quadro; uma volta completa ocupa seis quadros.
class ProbeCapturePass {
  const ProbeCapturePass({
    required this.face,
    required this.resolution,
    required this.excludedNodeIds,
    this.includedNodeIds = const {},
    this.reflections = false,
    this.shadows = false,
    this.postEffects = false,
    this.lodBias = 1,
  });

  final int face;
  final int resolution;
  final Set<String> excludedNodeIds;
  final Set<String> includedNodeIds;
  final bool reflections;
  final bool shadows;
  final bool postEffects;
  final int lodBias;
}

/// Estado pequeno e independente do frame clock. Em modo "ao mover", depois
/// das seis faces e sem nova mudanca, [nextFrame] devolve null: custo zero.
class ReflectionProbeScheduler {
  ReflectionProbeScheduler({this.nextFace = 0, this._dirty = true});

  int nextFace;
  bool _dirty;

  bool get isDirty => _dirty;

  void markDirty() => _dirty = true;

  ProbeCapturePass? nextFrame(
    ReflectionProbe3D probe, {
    bool sceneChanged = false,
    bool moving = false,
    bool draftMode = false,
    String? reflectiveNodeId,
  }) {
    if (!probe.enabled || draftMode) return null;
    if (probe.updateMode == ProbeUpdateMode.onMove &&
        (sceneChanged || moving)) {
      _dirty = true;
    }
    final shouldRender = switch (probe.updateMode) {
      ProbeUpdateMode.stopped => _dirty,
      ProbeUpdateMode.onMove => _dirty,
      ProbeUpdateMode.continuous => true,
    };
    if (!shouldRender) return null;

    final excluded = <String>{...probe.excludeNodeIds};
    if (reflectiveNodeId != null) excluded.add(reflectiveNodeId);
    final pass = ProbeCapturePass(
      face: nextFace,
      resolution: probe.quality.faceResolution,
      excludedNodeIds: excluded,
      includedNodeIds: probe.includeNodeIds,
    );
    nextFace = (nextFace + 1) % 6;
    if (nextFace == 0 && probe.updateMode != ProbeUpdateMode.continuous) {
      _dirty = false;
    }
    return pass;
  }
}

/// Nivel de mip selecionado para o cubemap pre-filtrado.
double roughnessMip(double roughness, {int mipLevels = 8}) =>
    roughness.clamp(0.0, 1.0).toDouble() * math.max(0, mipLevels - 1);
