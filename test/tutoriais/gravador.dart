// O GRAVADOR DE TUTORIAIS: a "camera" que filma o proprio app.
//
// Nao e um teste: e a maquinaria que os gravadores usam. Ela toca no
// app de verdade (os mesmos widgets, a mesma cena, o mesmo pintor),
// tira um PNG por quadro e anota, quadro a quadro, onde o dedo estava e
// que legenda vale. O montador em Python desenha o dedo e a faixa de
// legenda e junta tudo num MP4.
//
// Ver docs/tutorial-em-video.md.
import 'dart:convert';
import 'dart:io';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/am_widgets.dart';
import 'package:aurea/src/features/projects/application/project_repository.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

const chaveDaGravacao = ValueKey('gravacao');

/// Uma lista de projetos que so vive na memoria.
class ProjetosNaMemoria extends ProjectsController {
  @override
  List<VideoProject> build() => [];
}

/// Sem disco: o repositorio de verdade grava por isolate, e isolate nao
/// anda no relogio de mentira do testWidgets.
class RepositorioNulo extends ProjectRepository {
  RepositorioNulo()
    : super(directory: Directory.systemTemp);

  @override
  Future<List<VideoProject>> loadAll() async => const [];

  @override
  Future<void> save(VideoProject project) async {}

  @override
  Future<void> delete(String id) async {}
}

/// As fontes de verdade, sob todos os nomes que o app usa: sem isto o
/// texto sai como caixinhas.
Future<void> carregarFontes() async {
  for (final family in [
    'Aurea Motion Sans',
    'Roboto',
    'CupertinoSystemText',
    'CupertinoSystemDisplay',
    '.SF Pro Text',
    '.SF Pro Display',
    '.SF UI Text',
    '.SF UI Display',
    '.AppleSystemUIFont',
    // A FONTE PADRAO DO PROPRIO TESTE. Sem ela, o texto do palco (que
    // nao tem familia escolhida, e cai no padrao) saia como uma fileira
    // de quadradinhos brancos — no tutorial do texto, justamente o que
    // o video existe para mostrar.
    'FlutterTest',
    'Ahem',
  ]) {
    await (FontLoader(family)..addFont(
          rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
        ))
        .load();
  }
  await (FontLoader('MaterialIcons')
        ..addFont(rootBundle.load('fonts/MaterialIcons-Regular.otf')))
      .load();
  await (FontLoader('packages/cupertino_icons/CupertinoIcons')..addFont(
        rootBundle.load('packages/cupertino_icons/assets/CupertinoIcons.ttf'),
      ))
      .load();
}

class Gravador {
  Gravador(this.tester, {required this.saida});

  final WidgetTester tester;

  /// A pasta onde os quadros e o roteiro sao escritos.
  final String saida;

  final quadros = <Map<String, Object?>>[];
  final cenas = <Map<String, Object?>>[];

  /// Onde o dedo esta agora (null = sem dedo na tela).
  Offset? dedo;
  double tempo = 0;
  int _n = 0;

  /// A FILA DE IMAGENS AINDA NAO GRAVADAS.
  ///
  /// Gravar o PNG exige `runAsync`, e `runAsync` no meio de um gesto
  /// mata o gesto. Entao o quadro e capturado de forma sincrona
  /// (`toImageSync`) e o disco espera o dedo levantar.
  final _fila = <(String, ui.Image)>[];

  Future<void> preparar() async {
    Directory('$saida/quadros').createSync(recursive: true);
    for (final f in Directory('$saida/quadros').listSync()) {
      f.deleteSync();
    }
  }

  /// Um quadro: repinta tudo, guarda a imagem e anota dedo, cena e duracao.
  Future<void> quadro({double dur = 1 / 12}) async {
    void repintar(RenderObject o) {
      o.markNeedsPaint();
      o.visitChildren(repintar);
    }

    repintar(tester.renderObject(find.byKey(chaveDaGravacao)));
    await tester.pump();
    final nome = '${_n.toString().padLeft(4, '0')}.png';
    final boundary = tester.renderObject<RenderRepaintBoundary>(
      find.byKey(chaveDaGravacao),
    );
    _fila.add((nome, boundary.toImageSync(pixelRatio: 2)));
    quadros.add({
      'arquivo': nome,
      'dur': dur,
      'dedo': dedo == null ? null : [dedo!.dx, dedo!.dy],
      'cena': cenas.length,
    });
    tempo += dur;
    _n++;
    // Fora de gesto, escoa a fila: cada quadro guardado sao ~5 MB.
    if (dedo == null && _fila.length >= 8) await descarregar();
  }

  /// Grava no disco o que estiver na fila.
  Future<void> descarregar() async {
    if (_fila.isEmpty) return;
    final lote = [..._fila];
    _fila.clear();
    await tester.runAsync(() async {
      for (final (nome, imagem) in lote) {
        final bytes = await imagem.toByteData(format: ui.ImageByteFormat.png);
        await File('$saida/quadros/$nome').writeAsBytes(
          bytes!.buffer.asUint8List(),
        );
        imagem.dispose();
      }
    });
  }

  Future<void> segurar(double segundos) => quadro(dur: segundos);

  /// Alguns quadros seguidos, para uma transicao (folha subindo, rota).
  Future<void> assentar({int quadros = 5, int ms = 90}) async {
    for (var i = 0; i < quadros; i++) {
      await tester.pump(Duration(milliseconds: ms));
      await quadro(dur: ms / 1000);
    }
  }

  /// Abre um passo: o texto vira a legenda daqui em diante.
  void cena(String texto) {
    cenas.add({'n': cenas.length + 1, 'texto': texto, 'inicio': tempo});
  }

  /// Toque com o dedo a vista: aparece, toca, some.
  Future<void> tocar(Finder f) async {
    await tester.ensureVisible(f);
    await tester.pump();
    dedo = tester.getCenter(f);
    await quadro(dur: .3);
    await tester.tap(f, warnIfMissed: false);
    await tester.pump(const Duration(milliseconds: 60));
    await quadro(dur: .12);
    dedo = null;
  }

  /// ROLA A FOLHA ATE O ALVO APARECER.
  ///
  /// As listas das folhas sao preguicosas: o que esta fora da tela nem e
  /// construido, e um `find` por ele nao acha nada. Rolar tambem e o que
  /// a pessoa faria — entao os quadros do rolamento entram no video.
  Future<void> rolarAte(Finder alvo, {int tentativas = 10}) async {
    for (var i = 0; i < tentativas && alvo.evaluate().isEmpty; i++) {
      final lista = find.byType(Scrollable);
      if (lista.evaluate().isEmpty) break;
      await tester.drag(lista.last, const Offset(0, -150), warnIfMissed: false);
      await tester.pump(const Duration(milliseconds: 120));
      await quadro(dur: .12);
    }
    await descarregar();
  }

  /// ARRASTO EM PASSOS, cada passo um gesto INTEIRO (desce, anda, sobe).
  ///
  /// Um gesto unico com pausas entre os movimentos nao chega ao palco: o
  /// reconhecedor que ganha a arena depende do ritmo, e o arrasto do
  /// gravador (com um quadro gravado a cada passo) e lento demais.
  /// Gestos inteiros sempre valem — e como o efeito e cumulativo (girar
  /// soma graus, a regua soma passos), o resultado e o mesmo.
  Future<void> arrastar(Finder f, Offset delta, {int passos = 8}) async {
    // ROLA ATE ELE PRIMEIRO. Um widget pode existir na arvore e estar
    // fora da janela: o arrasto cai no vazio e o valor nao anda — foi o
    // que aconteceu com as reguas da animacao de texto, la embaixo num
    // painel alto.
    await tester.ensureVisible(f);
    await tester.pump();
    final inicio = tester.getCenter(f);
    final pedaco = delta / passos.toDouble();
    for (var i = 1; i <= passos; i++) {
      dedo = inicio + delta * (i / passos);
      await tester.drag(f, pedaco, warnIfMissed: false);
      await tester.pump(const Duration(milliseconds: 40));
      await quadro(dur: .08);
    }
    dedo = null;
    await tester.pump(const Duration(milliseconds: 60));
    await quadro(dur: .35);
    await descarregar();
  }

  /// ANDA UM NUMERO PELA REGUA, com o dedo deslizando junto.
  ///
  /// Chama o `onChanged` da propria regua em vez de arrastar o dedo de
  /// verdade: num painel mais alto que a tela o alvo pode estar fora da
  /// janela, e ai o arrasto cai no vazio e o numero nao anda. O que se
  /// ve e o mesmo — os riscos deslizam e o valor muda.
  Future<void> valorDaRegua(
    Finder regua,
    double de,
    double ate, {
    int passos = 8,
  }) async {
    await tester.ensureVisible(regua);
    await tester.pump();
    final r = tester.getRect(regua);
    for (var i = 1; i <= passos; i++) {
      final v = de + (ate - de) * i / passos;
      tester.widget<AmTickRuler>(regua).onChanged(v);
      // O dedo anda para o lado contrario do valor: puxar para a
      // esquerda aumenta, como na regua de verdade.
      final t = i / passos;
      dedo = Offset(
        r.center.dx - (ate > de ? 1 : -1) * (t - .5) * r.width * .7,
        r.center.dy,
      );
      await tester.pump(const Duration(milliseconds: 40));
      await quadro(dur: .09);
    }
    dedo = null;
    await tester.pump();
    await quadro(dur: .3);
    await descarregar();
  }

  /// A regua do tempo do Estudio, com o dedo andando junto do valor.
  Future<void> tempoDoEstudio(double de, double ate, {int passos = 8}) async {
    final regua = find.byKey(const ValueKey('scene-motion-time'));
    final r = tester.getRect(regua);
    final widget = tester.widget<AmTickRuler>(regua);
    for (var i = 1; i <= passos; i++) {
      final v = de + (ate - de) * i / passos;
      widget.onChanged(v);
      dedo = Offset(r.left + r.width * (i / passos), r.center.dy);
      await tester.pump(const Duration(milliseconds: 40));
      await quadro(dur: .09);
    }
    dedo = null;
    await tester.pump();
    await quadro(dur: .3);
  }

  /// Digita um numero num dos campos X/Y/Z da ferramenta ativa.
  ///
  /// O NUMERO, e nao o arrasto: no arrasto quem decide e onde o dedo
  /// encosta — fora do objeto, o palco orbita a camera. O campo e o
  /// caminho que sempre funciona, e e o que se ensina.
  Future<void> valorDoEixo(int eixo, String valor) async {
    await tocar(find.byKey(ValueKey('scene-transform-$eixo')));
    await assentar(quadros: 5);
    await tester.enterText(find.byKey(const ValueKey('valor-campo')), valor);
    await assentar(quadros: 3);
    tester.view.resetViewInsets();
    await assentar(quadros: 3);
    await tocar(find.text('OK'));
    await assentar(quadros: 5);
  }

  /// Fecha a folha que estiver por cima (as folhas do Estudio nao tem
  /// botao de fechar em todas as alturas).
  Future<void> fecharFolha() async {
    tester.state<NavigatorState>(find.byType(Navigator).last).pop();
    await assentar(quadros: 5);
  }

  Future<void> salvar() async {
    await descarregar();
    File('$saida/quadros.json').writeAsStringSync(jsonEncode(quadros));
    File('$saida/cenas.json').writeAsStringSync(jsonEncode(cenas));
  }
}
