/// VELOCIDADE DO CLIPE (v1.1.1): o que acontece com a barra quando a
/// velocidade muda.
///
/// Mudar a velocidade muda quanto tempo o mesmo trecho da fonte leva para
/// tocar. A diferenca tem de ir para algum lugar, e quem escolhe e a
/// pessoa:
///
/// * ESTENDER FIM — o comeco fica; a barra cresce ou encolhe pelo fim.
/// * ESTENDER INICIO — o fim fica; a barra cresce ou encolhe pelo comeco.
/// * CORTAR FIM — a barra fica do mesmo tamanho; o que sobra da fonte sai
///   do fim (ou, sem fonte bastante, a barra encurta pelo fim).
/// * CORTAR INICIO — a barra fica; o que sobra sai do comeco da fonte, e
///   o ultimo quadro continua no mesmo lugar.
enum CompensacaoDaVelocidade { estenderInicio, cortarInicio, cortarFim, estenderFim }

String rotuloDaCompensacao(CompensacaoDaVelocidade c) => switch (c) {
  CompensacaoDaVelocidade.estenderInicio => 'Estender início',
  CompensacaoDaVelocidade.cortarInicio => 'Cortar início',
  CompensacaoDaVelocidade.cortarFim => 'Cortar fim',
  CompensacaoDaVelocidade.estenderFim => 'Estender fim',
};

/// Os limites do motor: abaixo de 0,1x o decodificador repete quadro
/// demais; acima de 10x pula mais do que decodifica.
const velocidadeMinima = 0.1;
const velocidadeMaxima = 10.0;

/// A regua vai ate 4x (o resto se digita) e gruda nestes valores.
const velocidadeMaximaDaRegua = 4.0;
const imasDaVelocidade = <double>[0.25, 0.5, 0.75, 1, 2, 3];

/// O valor que a regua entrega: em passos de 0,05 e preso ao ima mais
/// proximo quando passa perto dele.
double velocidadeDaRegua(double bruto, {double passo = .05, double raio = .06}) {
  final v = bruto.clamp(velocidadeMinima, velocidadeMaximaDaRegua).toDouble();
  for (final ima in imasDaVelocidade) {
    if ((v - ima).abs() <= raio) return ima;
  }
  final degrau = (v / passo).roundToDouble() * passo;
  return double.parse(
    degrau.clamp(velocidadeMinima, velocidadeMaximaDaRegua).toStringAsFixed(2),
  );
}

/// Onde a barra e a fonte ficam depois da troca.
typedef EnquadramentoDaVelocidade = ({
  Duration inicio,
  Duration duracao,
  Duration deslocamento,
});

/// A CONTA dos quatro modos, sem camada nenhuma (entra e sai numero).
///
/// [inicio] e [duracao] sao a barra na linha do tempo; [deslocamento] e o
/// ponto de entrada na fonte; [fonte] e quanto o arquivo tem (nulo em
/// projeto antigo: sem teto). [atual] e a velocidade de agora. Devolve
/// nulo quando a barra ficaria curta demais para existir.
EnquadramentoDaVelocidade? enquadrarVelocidade({
  required Duration inicio,
  required Duration duracao,
  required Duration deslocamento,
  required Duration? fonte,
  required double atual,
  required double nova,
  required CompensacaoDaVelocidade modo,
}) {
  if (atual <= 0 || nova <= 0) return null;
  final durUs = duracao.inMicroseconds.toDouble();
  final trechoAtual = durUs * atual;
  final disponivel = fonte == null
      ? double.infinity
      : (fonte.inMicroseconds - deslocamento.inMicroseconds).toDouble();
  var novoInicio = inicio.inMicroseconds.toDouble();
  var novaDur = durUs;
  var novoDesl = deslocamento.inMicroseconds.toDouble();

  switch (modo) {
    case CompensacaoDaVelocidade.estenderFim:
      novaDur = trechoAtual / nova;
    case CompensacaoDaVelocidade.estenderInicio:
      novaDur = trechoAtual / nova;
      novoInicio = inicio.inMicroseconds + durUs - novaDur;
      if (novoInicio < 0) novoInicio = 0;
    case CompensacaoDaVelocidade.cortarFim:
      if (durUs * nova > disponivel) novaDur = disponivel / nova;
    case CompensacaoDaVelocidade.cortarInicio:
      final saida = deslocamento.inMicroseconds + trechoAtual;
      novoDesl = saida - durUs * nova;
      if (novoDesl < 0) {
        novoDesl = 0;
        novaDur = saida / nova;
        novoInicio = inicio.inMicroseconds + durUs - novaDur;
      }
  }
  if (novaDur < 50000 || !novaDur.isFinite) return null;
  return (
    inicio: Duration(microseconds: novoInicio.round()),
    duracao: Duration(microseconds: novaDur.round()),
    deslocamento: Duration(microseconds: novoDesl.round()),
  );
}
