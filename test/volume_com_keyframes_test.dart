// VOLUME COM KEYFRAMES (v1.1.1): o som da camada ganha um envelope de
// volume por cima do ganho — o tocador e a exportacao leem os mesmos
// numeros, o arquivo guarda, dividir e aparar levam junto, e a folha do
// som tem o losango, copiar/colar o som e o aviso de camada sem audio.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/audio_mix.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/audio.dart'
    show volumeComKeyframeAlternado, volumeEditado;
import 'package:aurea/src/features/export/application/export_engine.dart';
import 'package:aurea/src/features/export/domain/export_settings.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

class _Projetos extends ProjectsController {
  @override
  List<VideoProject> build() => const [];
}

const _s = Duration(seconds: 1);

AnimatedDouble _subida() => AnimatedDouble(1)
    .withKeyframe(Duration.zero, 0)
    .withKeyframe(const Duration(seconds: 2), 1.5);

AudioLayer _musica({AudioSpec audio = const AudioSpec(), Duration inicio = Duration.zero}) =>
    AudioLayer(
      id: 'm',
      name: 'musica',
      startTime: inicio,
      duration: const Duration(seconds: 10),
      sourcePath: '/tmp/m.wav',
      audio: audio,
    );

void main() {
  test('o tocador multiplica o envelope no tempo do clipe', () {
    final l = _musica(
      audio: AudioSpec(volumeAnimado: _subida()),
      inicio: const Duration(seconds: 4),
    );
    expect(layerAudioGainAt(l, const Duration(seconds: 4)), 0);
    expect(layerAudioGainAt(l, const Duration(seconds: 5)), closeTo(.75, 1e-9));
    expect(layerAudioGainAt(l, const Duration(seconds: 9)), closeTo(1.5, 1e-9));
  });

  test('o envelope amostra os trechos e segura as pontas', () {
    final env = envelopeDoVolume(_subida(), const Duration(seconds: 10));
    expect(env.gainAt(Duration.zero), 0);
    expect(env.gainAt(const Duration(seconds: 1)), closeTo(.75, 1e-9));
    expect(env.gainAt(const Duration(seconds: 8)), 1.5);
    final deslocado = envelopeDoVolume(
      _subida(),
      const Duration(seconds: 10),
      deslocamento: const Duration(milliseconds: 500),
    );
    expect(deslocado.gainAt(const Duration(milliseconds: 1500)), closeTo(.75, 1e-9));
  });

  test('exportacao: parado entra no numero; animado vira expressao', () {
    ExportEngine motor(AudioSpec audio) => ExportEngine(
      VideoProject(
        name: 'p',
        createdAt: DateTime(2026, 9, 15),
        layers: [_musica(audio: audio)],
      ),
      const ExportSettings(),
    );
    final parado = motor(AudioSpec(volumeAnimado: AnimatedDouble(.5))).audioGraph(1);
    expect(parado.filter, contains('volume=0.500'));
    expect(parado.filter, isNot(contains("volume=volume='")));

    final animado = motor(AudioSpec(volumeAnimado: _subida())).audioGraph(1);
    expect(animado.filter, contains('volume=1.000'));
    expect(animado.filter, contains("volume=volume='"));
    expect(animado.filter, contains('eval=frame'));
    expect(animado.filter, contains('alimiter'), reason: 'o envelope passa de 100%');
  });

  test('o arquivo guarda; dividir leva o envelope para a segunda metade', () {
    final c = ProviderContainer(
      overrides: [projectsControllerProvider.overrideWith(_Projetos.new)],
    );
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.openProject(
      VideoProject(
        name: 'p',
        createdAt: DateTime(2026, 9, 15),
        layers: [_musica(audio: AudioSpec(volumeAnimado: _subida()))],
      ),
    );
    final volta = projectFromJson(projectToJson(c.read(editorControllerProvider)));
    final trilha = (volta.layers.single as AudioLayer).audio.volumeAnimado!;
    expect(trilha.keyframes, hasLength(2));
    expect(trilha.valueAt(_s), closeTo(.75, 1e-9));

    e.splitLayer('m', _s);
    final metades = c.read(editorControllerProvider).layers.cast<AudioLayer>();
    final segunda = metades.firstWhere((l) => l.startTime == _s);
    // No comeco da segunda metade o volume continua de onde estava.
    expect(segunda.audio.volumeEm(Duration.zero), closeTo(.75, 1e-9));
    expect(segunda.audio.volumeEm(_s), closeTo(1.5, 1e-9));
  });

  test('editar e o losango seguem a regra dos keyframes', () {
    var a = const AudioSpec();
    a = volumeEditado(a, _s, .5);
    expect(a.volumeAnimado!.isAnimated, isFalse);
    expect(a.volumeEm(Duration.zero), .5);
    a = volumeComKeyframeAlternado(a, _s);
    expect(a.volumeAnimado!.hasKeyframeAt(_s), isTrue);
    // Animado e fora de marca: a edicao nao muda nada.
    expect(volumeEditado(a, Duration.zero, 2).volumeAnimado!.keyframes, hasLength(1));
    a = volumeEditado(a, _s, 2);
    expect(a.volumeEm(_s), 2);
    // Tirar a ultima marca volta a um numero; em 100% some o envelope.
    a = volumeComKeyframeAlternado(a, _s);
    expect(a.volumeAnimado!.isAnimated, isFalse);
    a = volumeEditado(a, _s, 1);
    expect(a.volumeAnimado, isNull);
  });
}
