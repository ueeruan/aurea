import 'dart:typed_data';

import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../domain/layer.dart';
import '../../domain/video_project.dart';
import '../editor_controller.dart';
import '../media_preview_service.dart';

/// O QUE A PREVIA MOSTRA (menu ⋮ da timeline).
///
/// Ajuda de trabalho, nunca resultado: so o PALCO obedece. A exportacao
/// le o projeto cru, e por isso "sem efeitos" nunca sai num arquivo.
enum ModoDePrevia { resultadoFinal, semEfeitos, meioTransparente }

String rotuloDoModoDePrevia(ModoDePrevia m) => switch (m) {
  ModoDePrevia.resultadoFinal => 'Resultado final',
  ModoDePrevia.semEfeitos => 'Sem efeitos',
  ModoDePrevia.meioTransparente => 'Selecionada a 50%',
};

/// AS OPCOES DE VISUALIZACAO: a coluna que o botao da barra de
/// reproducao abre na borda direita do palco.
class OpcoesDeVisualizacao {
  const OpcoesDeVisualizacao({
    this.aberta = false,
    this.pixels = false,
    this.grade = false,
    this.visaoDaCamera = true,
    this.modo = ModoDePrevia.resultadoFinal,
  });

  /// A coluna esta a mostra.
  final bool aberta;

  /// PIXELS DE VERDADE: a previa ignora a resolucao reduzida e, com o
  /// palco aproximado, desenha a grade de pixels da composicao.
  final bool pixels;

  /// A grade de terços e oitavos sobre o quadro.
  final bool grade;

  /// Desligada, a cena aparece sem a camera: e a vista de montagem, em
  /// que se enxerga o que esta fora do enquadramento dela.
  final bool visaoDaCamera;

  final ModoDePrevia modo;

  OpcoesDeVisualizacao copyWith({
    bool? aberta,
    bool? pixels,
    bool? grade,
    bool? visaoDaCamera,
    ModoDePrevia? modo,
  }) => OpcoesDeVisualizacao(
    aberta: aberta ?? this.aberta,
    pixels: pixels ?? this.pixels,
    grade: grade ?? this.grade,
    visaoDaCamera: visaoDaCamera ?? this.visaoDaCamera,
    modo: modo ?? this.modo,
  );
}

class OpcoesDeVisualizacaoNotifier extends Notifier<OpcoesDeVisualizacao> {
  @override
  OpcoesDeVisualizacao build() => const OpcoesDeVisualizacao();

  void reset() => state = const OpcoesDeVisualizacao();

  void alternarColuna() => state = state.copyWith(aberta: !state.aberta);

  void alternarPixels() => state = state.copyWith(pixels: !state.pixels);

  void alternarGrade() => state = state.copyWith(grade: !state.grade);

  void alternarVisaoDaCamera() =>
      state = state.copyWith(visaoDaCamera: !state.visaoDaCamera);

  void definirModo(ModoDePrevia modo) => state = state.copyWith(modo: modo);
}

final opcoesDeVisualizacaoProvider =
    NotifierProvider<OpcoesDeVisualizacaoNotifier, OpcoesDeVisualizacao>(
      OpcoesDeVisualizacaoNotifier.new,
    );

/// O PROJETO COMO O PALCO O DESENHA: o visivel, menos o que as opcoes
/// de visualizacao tiram. Com tudo no padrao e o mesmo objeto.
final projetoDoPalcoProvider = Provider<VideoProject>((ref) {
  final projeto = ref.watch(projetoVisivelProvider);
  final opcoes = ref.watch(opcoesDeVisualizacaoProvider);
  return projetoParaOPalco(projeto, opcoes);
});

VideoProject projetoParaOPalco(VideoProject p, OpcoesDeVisualizacao o) {
  final tiraCamera = !o.visaoDaCamera && p.layers.any((l) => l is CameraLayer);
  final tiraEfeitos =
      o.modo == ModoDePrevia.semEfeitos && _temEfeito(p.layers);
  if (!tiraCamera && !tiraEfeitos) return p;

  List<Layer> limpar(List<Layer> camadas, {required bool raiz}) => [
    for (final l in camadas)
      // A camera ativa e procurada so no topo da pilha (`cameraAtivaEm`).
      if (!(raiz && tiraCamera && l is CameraLayer))
        switch (l) {
          GroupLayer g when tiraEfeitos => g.copyLayer(
            effects: const [],
            children: limpar(g.children, raiz: false),
          ),
          _ when tiraEfeitos && l.effects.isNotEmpty => l.copyLayer(
            effects: const [],
          ),
          _ => l,
        },
  ];

  return p.copyWith(layers: limpar(p.layers, raiz: true));
}

bool _temEfeito(List<Layer> camadas) => camadas.any(
  (l) => l.effects.isNotEmpty || (l is GroupLayer && _temEfeito(l.children)),
);

/// A BARRA DE INFORMACOES: toma o lugar da barra de reproducao enquanto
/// o dedo manipula alguma coisa e diz o numero que esta mudando.
class DadosDaInfobar {
  /// Arrastar no tempo: onde o item esta e quanto andou.
  const DadosDaInfobar.tempo({
    required Duration this.tempo,
    required Duration this.deslocamento,
  }) : pares = const [];

  /// Arrastar no palco: ate seis pares rotulo/valor.
  const DadosDaInfobar.pares(this.pares) : tempo = null, deslocamento = null;

  final Duration? tempo;
  final Duration? deslocamento;
  final List<(String, String)> pares;
}

final infobarProvider = StateProvider<DadosDaInfobar?>((ref) => null);

/// "1:02.35" — minutos, segundos e centesimos.
String tempoDaInfobar(Duration d) {
  final negativo = d.isNegative;
  final a = negativo ? -d : d;
  final cs = (a.inMilliseconds % 1000) ~/ 10;
  final s = a.inSeconds % 60;
  final m = a.inMinutes;
  final texto =
      '$m:${s.toString().padLeft(2, '0')}.${cs.toString().padLeft(2, '0')}';
  return negativo ? '-$texto' : texto;
}

/// O NIVEL DO SOM no instante [t], 0..1: o maior pico entre as camadas
/// de audio e video no ar, cada uma no seu volume.
///
/// Le o envelope de picos que a timeline ja calculou para desenhar a
/// onda; midia ainda sem onda simplesmente nao conta.
double nivelDeAudioEm(
  VideoProject projeto,
  Duration t, {
  Float32List? Function(String caminho)? picos,
}) {
  final picosDe = picos ?? MediaPreviewService.instance.peaksOf;
  var nivel = 0.0;
  void medir(Iterable<Layer> camadas, Duration agora) {
    for (final l in camadas) {
      if (!l.activeAt(agora) || projeto.isHidden(l.id)) continue;
      final (caminho, desvio, velocidade, volume) = switch (l) {
        AudioLayer a => (a.sourcePath, a.sourceOffset, a.speed, a.volume),
        VideoLayer v => (v.sourcePath, v.sourceOffset, v.speed, v.volume),
        _ => (null, Duration.zero, 1.0, 0.0),
      };
      if (l is GroupLayer) {
        medir(l.children, l.contentTimeAt(l.localTime(agora)));
        continue;
      }
      if (caminho == null || volume <= 0) continue;
      final envelope = picosDe(caminho);
      if (envelope == null || envelope.isEmpty) continue;
      final naMidia =
          desvio.inMicroseconds +
          l.localTime(agora).inMicroseconds * velocidade;
      final i = (naMidia * MediaPreviewService.peaksPerSecond / 1e6).floor();
      if (i < 0 || i >= envelope.length) continue;
      final v = envelope[i] * volume;
      if (v > nivel) nivel = v;
    }
  }

  medir(projeto.layers, t);
  return nivel.clamp(0.0, 1.0);
}
