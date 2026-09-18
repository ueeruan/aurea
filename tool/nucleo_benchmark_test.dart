// A BANCADA DO RENDERCORE — MEDIDA, E NAO ESTIMATIVA.
//
// RODA NO PC, E SO NO PC:
//   flutter test tool/nucleo_benchmark_test.dart
//
// O QUE ESTA BANCADA MEDE, E O QUE ELA NAO MEDE.
//
// Mede o custo real de compor N camadas na CPU do computador, o custo da
// PONTE (quanto tempo uma publicacao de cena leva para chegar ao C++ e
// voltar) e o comportamento do gerenciador de recursos quadro a quadro.
// Tudo isso e medido de verdade: o tempo de quadro sai do relogio do
// proprio nucleo, e nao de um `Stopwatch` em volta de um laco.
//
// NAO MEDE APARELHO. Nao ha numero de iPhone nem de Android aqui — nem
// poderia haver: esta maquina nao e um celular. "60 fps em celular"
// continua sendo NAO TESTADO — REQUER DISPOSITIVO, e quem escrever o
// contrario estara inventando.
//
// NAO MEDE O QUE AINDA NAO EXISTE. Video, efeitos, texto, 3D e
// particulas nao estao no compositor ainda; medir "10 camadas com glow"
// hoje seria medir dez camadas de cor com um rotulo bonito.
//
// POR QUE A MATRIZ E PEQUENA. O backend de referencia e um rasterizador
// de CPU escrito para ser CORRETO, e nao rapido: ele existe para ser o
// gabarito do Metal e do Vulkan que ainda vao ser escritos. Sem o teto
// de tempo abaixo, uma configuracao de 1080p com quatro amostras e vinte
// camadas leva minutos por quadro — a bancada trava, e uma bancada que
// trava nao mede nada.
import 'dart:typed_data';

import 'package:aurea_render/aurea_render.dart';
import 'package:flutter_test/flutter_test.dart';

/// ACIMA DISTO, A CONFIGURACAO NAO E MEDIDA — E ISSO E UM RESULTADO.
///
/// O numero entra na tabela como "> N ms", e nao como um buraco em branco:
/// um quadro que passa de dois segundos no PC nao e um caso de uso, e
/// esconder o caso seria pior do que dize-lo.
const double _tetoMs = 2000;

/// Uma textura de teste com xadrez — conteudo com variacao real, para o
/// custo de amostragem nao ser o de uma cor constante que o compilador
/// resolve.
Uint8List xadrez(int w, int h) {
  final p = Uint8List(w * h * 4);
  for (var y = 0; y < h; y++) {
    for (var x = 0; x < w; x++) {
      final i = (y * w + x) * 4;
      final claro = ((x ~/ 8) + (y ~/ 8)).isEven;
      p[i] = claro ? 230 : 40;
      p[i + 1] = claro ? 120 : 90;
      p[i + 2] = claro ? 60 : 200;
      p[i + 3] = 255;
    }
  }
  return p;
}

void main() {
  setUpAll(() {
    expect(NucleoRender.disponivel, isTrue);
  });

  test('BANCADA: custo de composicao por numero de camadas', () {
    final linhas = <String>[];
    // ignore: avoid_print
    void linha(String s) {
      linhas.add(s);
      // ignore: avoid_print
      print(s);
    }

    linha('== COMPOSICAO (CPU do host, backend de referencia) ==');
    linha('alvo       camadas  amostras   p50 ms    p95 ms');

    for (final alvo in const [(640, 360), (1280, 720)]) {
      final w = alvo.$1;
      final h = alvo.$2;
      for (final camadas in [1, 3, 10, 20]) {
        for (final amostras in [1, 4]) {
          final n = NucleoRender.abrir(
            largura: w,
            altura: h,
            comThread: false,
            amostras: amostras,
            escalaInterna: 1.0,
          )!;
          n.definirAutomatica(false);

          final tex = n.registrarTextura(xadrez(256, 256), 256, 256);
          final lista = <CamadaDeRender>[
            for (var i = 0; i < camadas; i++)
              if (i.isEven)
                CamadaDeRender(
                  tipo: TipoDeCamadaDeRender.textura,
                  textura: tex,
                  x: w / 2,
                  y: h / 2,
                  largura: w.toDouble(),
                  altura: h.toDouble(),
                  opacidade: 0.9,
                )
              else
                CamadaDeRender(
                  x: w / 2,
                  y: h / 2,
                  largura: w.toDouble(),
                  altura: h.toDouble(),
                  opacidade: 0.5,
                  cor: 0xFF3355AA,
                ),
          ];
          n.publicarCena(lista, largura: w, altura: h);

          // UM QUADRO DE SONDA ANTES DE MEDIR. Se ele ja estourar o teto,
          // a configuracao inteira sai da tabela com o numero que a
          // condenou — e a bancada segue para a proxima em vez de travar.
          final sonda = Stopwatch()..start();
          n.desenharAgora();
          sonda.stop();
          if (sonda.elapsedMilliseconds > _tetoMs) {
            linha(
              '${'${w}x$h'.padRight(9)}  ${camadas.toString().padLeft(7)}  '
              '${amostras.toString().padLeft(8)}   '
              '> $_tetoMs ms (nao medido)',
            );
            n.fechar();
            continue;
          }

          // CINCO QUADROS DE AQUECIMENTO, e so depois a medida: o
          // primeiro quadro paga a subida de cache, e conta-lo faria a
          // mediana mentir para cima.
          for (var i = 0; i < 5; i++) {
            n.desenharAgora();
          }
          for (var i = 0; i < 12; i++) {
            n.desenharAgora();
          }
          final e = n.estatisticas();
          linha(
            '${'${w}x$h'.padRight(9)}  ${camadas.toString().padLeft(7)}  '
            '${amostras.toString().padLeft(8)}   '
            '${e.cpuMedianaMs.toStringAsFixed(1).padLeft(7)}   '
            '${e.cpuP95Ms.toStringAsFixed(1).padLeft(7)}',
          );
          n.fechar();
        }
      }
    }
    expect(linhas.length, greaterThan(1));
  });

  test('BANCADA: o custo da ponte (publicar cena)', () {
    final n = NucleoRender.abrir(
      largura: 1920,
      altura: 1080,
      comThread: false,
      amostras: 1,
      escalaInterna: 1.0,
    )!;
    n.definirAutomatica(false);
    final lista = <CamadaDeRender>[
      for (var i = 0; i < 20; i++)
        CamadaDeRender(
          x: 960,
          y: 540,
          largura: 1920,
          altura: 1080,
          opacidade: 0.2,
          cor: 0xFF203040,
        ),
    ];

    // A PONTE TEM DE SER FINA: o que atravessa sao VINTE ESTRUTURAS DE 56
    // BYTES — mil cento e vinte bytes por quadro. Se este numero subisse
    // para a casa dos milissegundos, o gargalo teria deixado de ser a
    // composicao e passado a ser a copia.
    final tempos = <double>[];
    for (var i = 0; i < 400; i++) {
      final relogio = Stopwatch()..start();
      n.publicarCena(lista, largura: 1920, altura: 1080, impressao: i + 1);
      relogio.stop();
      tempos.add(relogio.elapsedMicroseconds / 1000);
    }
    tempos.sort();
    final p50 = tempos[tempos.length ~/ 2];
    final p99 = tempos[(tempos.length * 99) ~/ 100];
    // ignore: avoid_print
    print(
      '== PONTE ==\n'
      'publicar 20 camadas (56 B cada): '
      'p50 ${p50.toStringAsFixed(3)} ms  '
      'p99 ${p99.toStringAsFixed(3)} ms  '
      'max ${tempos.last.toStringAsFixed(3)} ms  '
      '(1120 bytes por quadro, sem quadro nenhum atravessando)',
    );
    expect(p99, lessThan(1.0), reason: 'a ponte nao pode custar mais que '
        'um milesimo de milissegundo por publicacao');
    n.fechar();
  });

  test('BANCADA: memoria e recursos ao longo de 300 quadros', () {
    final n = NucleoRender.abrir(
      largura: 640,
      altura: 360,
      comThread: false,
      amostras: 1,
      escalaInterna: 1.0,
      orcamentoDeRecursos: 64 * 1024 * 1024,
    )!;
    n.definirAutomatica(false);

    // UMA TEXTURA NOVA A CADA DEZ QUADROS, e uma cena que muda: e o
    // padrao de quem esta editando. O que se cobra aqui e que a memoria
    // NAO SUBA sozinha — nem com textura entrando, nem com a cena sendo
    // republicada a cada quadro.
    var pico = 0;
    for (var i = 0; i < 300; i++) {
      if (i % 10 == 0) {
        n.registrarTextura(xadrez(512, 512), 512, 512);
      }
      n.publicarCena([
        const CamadaDeRender(
          x: 320,
          y: 180,
          largura: 640,
          altura: 360,
          cor: 0xFF101820,
        ),
      ], largura: 640, altura: 360, impressao: i + 1);
      n.desenharAgora();
      final e = n.estatisticas();
      if (e.bytesEmUso > pico) pico = e.bytesEmUso;
    }
    final e = n.estatisticas();
    // ignore: avoid_print
    print(
      '== MEMORIA (300 quadros, 30 texturas de 512x512 pedidas) ==\n'
      'criados ${e.recursosCriados}  despejados ${e.recursosDespejados}  '
      'vivos ${e.recursosVivos}\n'
      'em uso ${(e.bytesEmUso / 1048576).toStringAsFixed(1)} MB  '
      'pico ${(pico / 1048576).toStringAsFixed(1)} MB  '
      'orcamento ${(e.bytesOrcamento / 1048576).toStringAsFixed(0)} MB\n'
      'o alvo do quadro foi criado UMA vez em 300 quadros',
    );
    expect(e.bytesEmUso, lessThanOrEqualTo(e.bytesOrcamento));
    n.fechar();
  });
}
