import '../../editor/domain/caption.dart';
import '../../editor/domain/keyframe.dart';
import 'autoedit_style.dart';

/// AS DECISOES DO AUTOEDIT, sem tocar em projeto nenhum.
///
/// Tudo aqui e conta pura: entram os silencios detectados e as falas
/// transcritas, saem os cortes e os keyframes. E o que permite provar as
/// regras que nao podem falhar — "nunca corta no meio de uma palavra",
/// "zoom so na troca de frase" — sem rodar o aplicativo.

/// A FOLGA que fica de silencio em cada ponta do corte.
///
/// Cortar rente ao limiar decepa o comeco da consoante e o fim da vogal,
/// e a fala sai picotada. Alem disso: silencio de fim de frase e
/// ENCURTADO, nao eliminado — corte grudado soa afobado.
const Duration kFolgaDoCorte = Duration(milliseconds: 120);

/// OS CORTES DE SILENCIO, ja seguros.
///
/// [ritmo] 0 devolve lista vazia: nao cortar nada e um resultado valido,
/// nao um caso degenerado. Em 1, corta tudo menos as folgas.
///
/// [palavras] sao as falas transcritas com tempo. Nenhum corte pode
/// invadir uma delas — o Whisper da o tempo por palavra exatamente para
/// isso, e cortar no meio de "obrigado" nao tem conserto depois.
List<(Duration, Duration)> cortesDeSilencio(
  List<(Duration, Duration)> silencios,
  List<Cue> palavras, {
  required double ritmo,
  Duration folga = kFolgaDoCorte,
}) {
  if (ritmo <= 0 || silencios.isEmpty) return const [];
  final r = ritmo.clamp(0.0, 1.0);

  final out = <(Duration, Duration)>[];
  for (final (inicio, fim) in silencios) {
    final total = fim - inicio;
    final minimo = folga * 2;
    if (total <= minimo) continue;

    // Quanto de silencio FICA: a folga das duas pontas, mais a parte que
    // o ritmo mandou preservar.
    final sobra = total - minimo;
    final manter = minimo + sobra * (1 - r);
    final cortar = total - manter;
    if (cortar <= Duration.zero) continue;

    // O corte sai do MEIO do silencio: tirar de uma ponta so deixaria a
    // respiracao toda de um lado.
    final meio = inicio + total * 0.5;
    var a = meio - cortar * 0.5;
    var b = meio + cortar * 0.5;

    // Nenhum corte encosta em palavra.
    for (final p in palavras) {
      if (p.end <= a || p.start >= b) continue;
      if (p.start <= a && p.end >= b) {
        a = b;
        break;
      }
      if (p.start > a && p.start < b) b = p.start;
      if (p.end > a && p.end < b) a = p.end;
    }
    if (b - a > const Duration(milliseconds: 40)) out.add((a, b));
  }
  return out;
}

/// O INICIO DE CADA FRASE, para o zoom se encaixar.
///
/// Duas palavras seguidas nao sao duas frases: so conta como troca quando
/// houve pausa de [pausaMinima] antes. Zoom no meio da frase enjoa, e e o
/// que faz um corte automatico parecer automatico.
List<Duration> trocasDeFrase(
  List<Cue> falas, {
  Duration pausaMinima = const Duration(milliseconds: 450),
}) {
  if (falas.isEmpty) return const [];
  final ordenadas = [...falas]..sort((a, b) => a.start.compareTo(b.start));
  final out = <Duration>[ordenadas.first.start];
  for (var i = 1; i < ordenadas.length; i++) {
    if (ordenadas[i].start - ordenadas[i - 1].end >= pausaMinima) {
      out.add(ordenadas[i].start);
    }
  }
  return out;
}

/// Quanto tempo o zoom leva para chegar. Curto: o movimento tem de
/// terminar dentro da primeira palavra, ou vira deslize.
const Duration kDuracaoDoZoom = Duration(milliseconds: 380);

/// OS KEYFRAMES DE ESCALA — o zoom das frases.
///
/// Nao e um efeito: sao keyframes de escala comuns, com mola, na camada
/// de video. Abrir no editor e ve-los ali e o que faz o AutoEdit ensinar
/// em vez de esconder.
///
/// Devolve vazio quando o zoom e nenhum: neutro tem de ser neutro.
List<Keyframe<double>> keyframesDeZoom(
  List<Duration> trocas,
  AutoEditZoom zoom, {
  Duration duracao = kDuracaoDoZoom,
  Duration? ate,
}) {
  if (zoom == AutoEditZoom.nenhum || trocas.isEmpty) return const [];
  final alvo = zoom.escala;

  final out = <Keyframe<double>>[];
  for (var i = 0; i < trocas.length; i++) {
    final t = trocas[i];
    if (ate != null && t >= ate) break;
    // Alterna: uma frase fecha, a proxima volta. Zoom que so fecha
    // termina o video de nariz colado na tela.
    final fecha = i.isEven;
    out.add(Keyframe(
      time: t,
      value: fecha ? 1.0 : alvo,
      ease: Easing.appleStandard,
    ));
    out.add(Keyframe(
      time: t + duracao,
      value: fecha ? alvo : 1.0,
      ease: Easing.interfaceSpring,
    ));
  }
  return out;
}

/// QUANTO TEMPO O VIDEO PERDE com os cortes — o numero que a tela do
/// trabalho mostra ("Cortando silencios  −18 s").
Duration tempoRemovido(List<(Duration, Duration)> cortes) {
  var total = Duration.zero;
  for (final (a, b) in cortes) {
    total += b - a;
  }
  return total;
}

/// O PLANO INTEIRO, pronto para ser aplicado.
///
/// Existe como objeto por um motivo: a tela de ajuste refaz o plano a
/// cada toque no Ritmo ou no Zoom, e so aplica quando a pessoa manda
/// abrir. Recalcular e barato; reprocessar o video nao seria.
class AutoEditPlan {
  const AutoEditPlan({
    required this.estilo,
    required this.cortes,
    required this.zoom,
    required this.falas,
    this.silencios = const [],
  });

  final AutoEditStyle estilo;
  final List<(Duration, Duration)> cortes;

  /// OS SILENCIOS BRUTOS, como o detector os viu.
  ///
  /// Ficam guardados porque a tela de ajuste refaz o plano a cada toque no
  /// Ritmo, e sem o bruto so daria para recortar o que ja foi cortado —
  /// aumentar o ritmo depois de baixar seria impossivel.
  final List<(Duration, Duration)> silencios;
  final List<Keyframe<double>> zoom;
  final List<Cue> falas;

  Duration get economia => tempoRemovido(cortes);

  /// Nao mexe em nada: nem corte, nem zoom, nem legenda.
  bool get isNeutro => cortes.isEmpty && zoom.isEmpty && falas.isEmpty;
}

/// Monta o plano a partir do que foi analisado.
AutoEditPlan planejar({
  required AutoEditStyle estilo,
  required List<(Duration, Duration)> silencios,
  required List<Cue> falas,
  Duration? duracao,
}) {
  final cortes = cortesDeSilencio(silencios, falas, ritmo: estilo.ritmo);
  return AutoEditPlan(
    estilo: estilo,
    cortes: cortes,
    silencios: silencios,
    zoom: keyframesDeZoom(
      trocasDeFrase(falas),
      estilo.zoom,
      ate: duracao,
    ),
    falas: estilo.legendar ? falas : const [],
  );
}
