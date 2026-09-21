// O FLUXO COMPLETO DE EDICAO PELA UI NOVA — no aparelho, com o motor real.
//
// "Nao conclua apenas porque compilou." Este teste faz o que uma pessoa faz:
// Inicio -> Novo projeto -> editor -> importar video -> scrub -> aparar ->
// dividir -> texto (digitado no teclado de verdade) -> animar a posicao ->
// curva -> efeitos (busca e categoria) -> solido 3D -> exportar -> desfazer.
// Cada passo e um toque ou um arrasto em widget de verdade; o que so o
// sistema faz (o seletor de arquivos, o teclado, a foto da tela) e pedido ao
// PC pelo logcat e feito pelo adb (integration_test/fluxo_completo_vigia.py):
//
//   FLUXO-HOST captura <nome>          adb exec-out screencap
//   FLUXO-HOST video <nome>            o mp4 de prova entra em files/fluxo
//   FLUXO-HOST digitar <nome> <texto>  input text no campo focado
//
// e o PC responde criando files/fluxo/ok_<nome> (run-as).
//
// O teste NAO para no primeiro passo que falha: cada passo registra PASS ou
// FAIL, o tempo e o que se viu, e o relatorio sai no fim (FLUXO-RELATORIO).
//
// Rodar (a partir do drive A:, com o vigia ligado antes; o flutter test
// desinstala o app no fim, e o vigia sai sozinho com o teste):
//   python integration_test/fluxo_completo_vigia.py <pasta-das-fotos>
//   flutter test integration_test/fluxo_completo_test.dart -d emulator-5554
import 'dart:async';
import 'dart:io';

import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/core/theme/app_theme.dart';
import 'package:aurea/src/core/theme/aurea_paleta.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/font_service.dart';
import 'package:aurea/src/features/editor/application/motor3d_modo.dart';
import 'package:aurea/src/features/editor/application/qualidade3d_controller.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/editor_shell.dart';
import 'package:aurea/src/features/editor/presentation/ui/timeline/timeline.dart';
import 'package:aurea/src/features/editor/presentation/ui/toolbar/galeria.dart';
import 'package:aurea/src/features/export/presentation/export_video_screen.dart';
import 'package:aurea/src/features/media/application/media_import_service.dart';
import 'package:aurea/src/features/media/application/midias_recentes.dart';
import 'package:aurea/src/features/projects/presentation/boas_vindas.dart';
import 'package:aurea/src/features/projects/presentation/home_shell.dart';
import 'package:aurea/src/features/projects/presentation/release_notice.dart';
import 'package:aurea/src/features/settings/application/settings_controller.dart';
import 'package:flutter/cupertino.dart' show CupertinoIcons;
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:image_picker/image_picker.dart' show XFile;
import 'package:integration_test/integration_test.dart';
import 'package:path_provider/path_provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// O app como o `AureaApp` o monta, sem a porta da conta (o teste nao cria
/// conta nem digita credencial): tema, idioma e a Inicio.
class _AppDoTeste extends ConsumerWidget {
  const _AppDoTeste();

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final modo = ref.watch(
      settingsControllerProvider.select((s) => s.themeMode),
    );
    final paleta = AureaPaleta.de(
      AureaPaleta.resolver(
        modo,
        WidgetsBinding.instance.platformDispatcher.platformBrightness,
      ),
    );
    return MaterialApp(
      key: ValueKey('tema-${paleta.id.name}'),
      title: 'Aurea',
      locale: Locale(ref.watch(appLanguageProvider)),
      supportedLocales: [for (final code in appLanguages.keys) Locale(code)],
      localizationsDelegates: GlobalMaterialLocalizations.delegates,
      builder: (context, child) => Directionality(
        textDirection: TextDirection.ltr,
        child: child ?? const SizedBox.shrink(),
      ),
      debugShowCheckedModeBanner: false,
      theme: AppTheme.tema(paleta: paleta),
      home: const HomeShell(),
    );
  }
}

class _Passo {
  _Passo(this.n, this.titulo, this.ok, this.obs, this.tempo);
  final String n;
  final String titulo;
  final bool ok;
  final String obs;
  final Duration tempo;
}

/// Linha para o logcat (o PC le daqui). `print` dentro do teste vai para o
/// canal do test runner; a zona raiz vai para o console do aparelho.
void _console(String s) => Zone.root.print(s);

void main() {
  final binding = IntegrationTestWidgetsFlutterBinding.ensureInitialized();
  binding.framePolicy = LiveTestWidgetsFlutterBindingFramePolicy.fullyLive;

  testWidgets('fluxo completo de edicao pela UI nova', timeout: const Timeout(
    Duration(minutes: 45),
  ), (tester) async {
    // ------------------------------------------------ preparo (como o main)
    final prefs = await SharedPreferences.getInstance();
    // Quem ja passou pelas boas-vindas e pelas novidades desta versao.
    await prefs.setString(chaveDoAceite, DateTime.now().toIso8601String());
    await prefs.setString(releaseNoticeSeenKey, releaseNoticeRevision);
    await Motor3DPreferencia.carregar(prefs);
    await ControladorDeQualidade3D.instancia.carregar(prefs);
    await FontService.instance.loadAll();

    final suporte = await getApplicationSupportDirectory();
    final pasta = Directory('${suporte.path}/fluxo')..createSync(recursive: true);
    for (final f in pasta.listSync()) {
      try {
        f.deleteSync(recursive: true);
      } catch (_) {}
    }

    final container = ProviderContainer(
      overrides: [sharedPreferencesProvider.overrideWithValue(prefs)],
    );
    addTearDown(container.dispose);

    // ----------------------------------------------------------- utilidades
    final resultados = <_Passo>[];
    var avisos = <String>[];
    void aviso(String s) {
      avisos.add(s);
      _console('FLUXO-AVISO $s');
    }

    Future<void> espera([int ms = 400]) async {
      final fim = DateTime.now().add(Duration(milliseconds: ms));
      while (DateTime.now().isBefore(fim)) {
        await tester.pump(const Duration(milliseconds: 50));
      }
    }

    Future<bool> ate(
      bool Function() cond, {
      Duration limite = const Duration(seconds: 10),
    }) async {
      final fim = DateTime.now().add(limite);
      while (DateTime.now().isBefore(fim)) {
        if (cond()) return true;
        await tester.pump(const Duration(milliseconds: 100));
      }
      return cond();
    }

    /// Pede ao PC e espera a resposta (o arquivo ok_<nome>).
    var pcVivo = true;
    Future<bool> host(
      String cmd,
      String nome, {
      String arg = '',
      Duration limite = const Duration(seconds: 30),
    }) async {
      if (!pcVivo) {
        aviso('sem o PC: "$cmd $nome" nao foi feito');
        return false;
      }
      final marca = File('${pasta.path}/ok_$nome');
      if (marca.existsSync()) marca.deleteSync();
      _console('FLUXO-HOST $cmd $nome${arg.isEmpty ? '' : ' $arg'}');
      final ok = await ate(marca.existsSync, limite: limite);
      if (!ok) aviso('o PC nao respondeu a "$cmd $nome"');
      return ok;
    }

    Future<void> foto(String nome) async {
      await espera(500);
      await host('captura', nome);
    }

    VideoProject projeto() => container.read(editorControllerProvider);
    Layer? camada(String id) => projeto().layerById(id);
    String? escolhida() => container.read(selectedLayerProvider);

    Size tela() => tester.view.physicalSize / tester.view.devicePixelRatio;
    double teclado() =>
        tester.view.viewInsets.bottom / tester.view.devicePixelRatio;

    /// O CENTRO DO ALVO RECEBE O TOQUE? (nada por cima, dentro da tela)
    bool acertavel(Finder f) {
      if (f.evaluate().isEmpty) return false;
      final ro = tester.renderObject(f.first);
      if (ro is! RenderBox || !ro.attached || !ro.hasSize) return false;
      final c = tester.getRect(f.first).center;
      final t = tela();
      if (c.dx < 0 || c.dy < 0 || c.dx > t.width || c.dy > t.height) {
        return false;
      }
      final res = tester.hitTestOnBinding(c);
      return res.path.any((e) => identical(e.target, ro));
    }

    /// ROLA COMO O DEDO: arrasta a lista que contem [f] ate o alvo aparecer.
    Future<int> rolarAte(Finder f, String nome, {Finder? em}) async {
      final lista =
          em ??
          find.ancestor(of: f.first, matching: find.byType(Scrollable)).first;
      if (lista.evaluate().isEmpty) return 0;
      final estado = tester.state<ScrollableState>(lista);
      final horizontal =
          axisDirectionToAxis(estado.axisDirection) == Axis.horizontal;
      var n = 0;
      while (!acertavel(f) && n < 14) {
        final vr = tester.getRect(lista);
        final tr = f.evaluate().isEmpty ? null : tester.getRect(f.first);
        final frente = tr == null
            ? true
            : horizontal
            ? tr.center.dx > vr.center.dx
            : tr.center.dy > vr.center.dy;
        final d = horizontal
            ? Offset(frente ? -140 : 140, 0)
            : Offset(0, frente ? -140 : 140);
        await tester.timedDragFrom(
          vr.center,
          d,
          const Duration(milliseconds: 350),
        );
        await espera(350);
        n++;
      }
      if (n > 0) aviso('$nome: escondido, precisou rolar ($n arrasto(s))');
      return n;
    }

    Future<void> tocar(
      Finder f,
      String nome, {
      Finder? rolarEm,
      int depois = 450,
      bool medir = true,
    }) async {
      await ate(
        () => f.evaluate().isNotEmpty,
        limite: Duration(seconds: rolarEm == null ? 6 : 2),
      );
      // Numa lista preguicosa o item fora da tela nem existe: rola ate ele.
      if (f.evaluate().isEmpty && rolarEm != null) {
        await rolarAte(f, nome, em: rolarEm);
      }
      if (f.evaluate().isEmpty) throw StateError('nao achei "$nome" na tela');
      if (!acertavel(f)) await rolarAte(f, nome, em: rolarEm);
      final r = tester.getRect(f.first);
      if (medir && r.shortestSide < 28) {
        aviso(
          '$nome: alvo de ${r.width.toStringAsFixed(0)}x'
          '${r.height.toStringAsFixed(0)} dp',
        );
      }
      if (!acertavel(f)) aviso('$nome: o centro nao recebe o toque');
      final k = teclado();
      if (k > 0 && r.center.dy > tela().height - k) {
        aviso('$nome: esta atras do teclado');
      }
      await tester.tapAt(r.center);
      await espera(depois);
    }

    Future<void> arrastar(
      Offset de,
      Offset total, {
      int passos = 10,
      int ms = 24,
    }) async {
      final g = await tester.startGesture(de);
      final cada = total / passos.toDouble();
      for (var i = 0; i < passos; i++) {
        await g.moveBy(cada);
        await tester.pump(Duration(milliseconds: ms));
      }
      await g.up();
      await espera(400);
    }

    EditorShell casca() => tester.widget<EditorShell>(find.byType(EditorShell));
    Duration relogio() => casca().playback.time.value;
    String textoDoRelogio() =>
        tester.widget<Text>(find.byKey(const ValueKey('transporte-tempo'))).data ??
        '';
    EstadoDaTimeline vista() => tester
        .state<TimelineDoEditorState>(find.byType(TimelineDoEditor))
        .estado;

    Rect linha(String id) =>
        tester.getRect(find.byKey(ValueKey('linha-$id')));

    /// O ponto da linha de [id] no instante global [s] (segundos).
    Offset noTempo(String id, double s) {
      final l = linha(id);
      return Offset(l.left + vista().xDoTempo(s * 1e6), l.center.dy);
    }

    String seg(Duration d) => (d.inMicroseconds / 1e6).toStringAsFixed(2);

    /// Um vazio da timeline: abaixo da ultima linha, acima da barra da base.
    Offset vazio() {
      final zona = tester.getRect(find.byKey(const ValueKey('zona-timeline')));
      var fundo = zona.top + 42.0;
      for (final l in projeto().layers) {
        final f = find.byKey(ValueKey('linha-${l.id}'));
        if (f.evaluate().isNotEmpty) {
          final r = tester.getRect(f.first);
          if (r.bottom > fundo) fundo = r.bottom;
        }
      }
      return Offset(zona.center.dx - 30, fundo + 22);
    }

    /// SCRUB: arrastar no vazio. Devolve quanto o relogio andou.
    Future<Duration> scrub(double dx) async {
      final antes = relogio();
      await arrastar(vazio(), Offset(dx, 0), passos: 12);
      return relogio() - antes;
    }

    Future<void> passo(
      String n,
      String titulo,
      Future<String> Function() corpo,
    ) async {
      avisos = [];
      final sw = Stopwatch()..start();
      _console('FLUXO-PASSO-INICIO $n $titulo');
      var ok = true;
      var obs = '';
      try {
        obs = await corpo();
      } catch (e, st) {
        ok = false;
        obs = 'FALHOU: $e';
        _console('FLUXO-ERRO $n $e\n$st');
      }
      if (!ok) {
        await foto('falha-$n');
        // O passo seguinte nao pode herdar uma folha aberta por este.
        for (final k in ['adicionar-fechar', 'curva-fechar', 'folha-fechar']) {
          final f = find.byKey(ValueKey(k));
          if (f.evaluate().isNotEmpty) {
            try {
              await tester.tap(f.first);
              await espera(500);
            } catch (_) {}
          }
        }
      }
      final ex = tester.takeException();
      if (ex != null) {
        avisos.add('excecao do framework: $ex');
      }
      sw.stop();
      final texto = [obs, ...avisos].where((s) => s.isNotEmpty).join(' | ');
      resultados.add(_Passo(n, titulo, ok, texto, sw.elapsed));
      _console(
        'FLUXO-PASSO $n ${ok ? 'PASS' : 'FAIL'} ${sw.elapsedMilliseconds}ms '
        '$titulo :: $texto',
      );
    }

    // O PC esta ouvindo? Sem ele nao ha foto, teclado nem video.
    final pcOuvindo = await host(
      'captura',
      '00-inicio',
      limite: const Duration(seconds: 40),
    );
    _console('FLUXO-PC ouvindo=$pcOuvindo');
    pcVivo = pcOuvindo;

    // ------------------------------------------------------------ o fluxo
    String? videoId;
    String? video2Id;
    String? textoId;
    String? solidoId;

    await passo('1', 'Inicio -> Novo projeto -> 9:16 1080p 30 fps -> editor',
        () async {
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: container,
          child: const _AppDoTeste(),
        ),
      );
      await espera(3000);
      await foto('01a-inicio');
      await tocar(find.byKey(const ValueKey('novo-projeto')), 'Novo projeto');
      await espera(500);
      await foto('01b-folha-novo-projeto');
      await tocar(find.byKey(const ValueKey('formato-9:16')), '9:16');
      await tocar(find.byKey(const ValueKey('resolucao-1080')), '1080p');
      await tocar(find.byKey(const ValueKey('fps-30')), '30 fps');
      final ficha =
          tester.widget<Text>(find.byKey(const ValueKey('projeto-ficha'))).data;
      expect(ficha, '1080 × 1920 · 30 fps');
      await tocar(find.byKey(const ValueKey('criar-projeto')), 'Criar projeto');
      final abriu = await ate(
        () => find.byType(EditorShell).evaluate().isNotEmpty,
      );
      expect(abriu, isTrue, reason: 'o editor nao abriu');
      await espera(1500);
      final p = projeto();
      expect(p.outputWidth, 1080);
      expect(p.outputHeight, 1920);
      expect(p.fps, 30);
      await foto('01c-editor-vazio');
      return 'projeto "${p.name}" ${p.outputWidth}x${p.outputHeight} '
          '${p.fps} fps';
    });

    await passo('2', 'Importar video (funcao da folha) e ver na timeline',
        () async {
      await tocar(find.byKey(const ValueKey('editor-adicionar')), '+');
      await espera(1200);
      await foto('02a-folha-adicionar-midia');
      // Com recentes a aba abre neles; a galeria e o segundo item do trilho.
      final galeria = find.byType(GalleryPanel);
      if (galeria.evaluate().isEmpty) {
        await tocar(
          find.byKey(const ValueKey('adicionar-midia-galeria')),
          'Galeria',
        );
        await espera(2500);
        await foto('02b-galeria');
      }
      expect(galeria, findsOneWidget, reason: 'a aba Midia nao mostrou a galeria');
      // O ARQUIVO: o seletor do sistema nao e automatizavel; o mp4 chega pelo
      // adb e passa pelo MESMO caminho do seletor: persist (copia para
      // imported_media) -> onImport da folha -> registrar nos recentes.
      expect(await host('video', 'entrada'), isTrue);
      final bruto = File('${pasta.path}/entrada.mp4');
      expect(bruto.existsSync() && bruto.lengthSync() > 0, isTrue);
      final salvo = await container
          .read(mediaImportServiceProvider)
          .persist(XFile(bruto.path));
      final painel = tester.widget<GalleryPanel>(galeria);
      await painel.onImport(salvo, true, Duration.zero);
      await registrarMidiaImportada(
        container.read(midiasRecentesProvider.notifier),
        caminho: salvo.path,
        nome: salvo.name,
        video: true,
      );
      await espera(800);
      final fechou = find.byKey(const ValueKey('folha-de-adicionar'))
          .evaluate()
          .isEmpty;
      final v = projeto().layers.whereType<VideoLayer>().toList();
      expect(v, hasLength(1), reason: 'a camada de video nao entrou');
      videoId = v.single.id;
      final durou = await ate(
        () => (camada(videoId!)?.duration ?? Duration.zero) >
            const Duration(milliseconds: 4800),
        limite: const Duration(seconds: 20),
      );
      expect(durou, isTrue, reason: 'a duracao do video nao chegou (sonda)');
      expect(find.byKey(ValueKey('linha-$videoId')), findsOneWidget);
      await espera(2500);
      await foto('02c-video-na-timeline');
      return 'video ${seg(camada(videoId!)!.duration)} s na timeline; '
          'folha fechou=$fechou; selecionado=${escolhida() == videoId}';
    });

    await passo('3', 'Scrub no vazio da timeline', () async {
      final antes = textoDoRelogio();
      final andou = await scrub(-150);
      final depois = textoDoRelogio();
      expect(andou, greaterThan(const Duration(milliseconds: 300)));
      expect(depois, isNot(antes));
      return 'relogio "$antes" -> "$depois" (+${seg(andou)} s)';
    });

    await passo('4', 'Trim: selecionar e arrastar a alca da direita', () async {
      final id = videoId!;
      // O fim do clipe precisa estar na tela: mais scrub.
      await scrub(-200);
      final t = relogio();
      // TOCAR NO CLIPE, um pouco antes do cabecote (dentro dele).
      await tester.tapAt(noTempo(id, t.inMicroseconds / 1e6 - 0.3));
      await espera(600);
      expect(escolhida(), id, reason: 'tocar no clipe nao o escolheu');
      expect(find.byKey(ValueKey('alcas-$id')), findsOneWidget);
      final antes = camada(id)!;
      final fim = antes.endTime.inMicroseconds / 1e6;
      var alca = noTempo(id, fim) + const Offset(15, 0);
      // A ALCA DE REORDENAR (35 na ponta direita da linha escolhida) fica
      // por cima da zona de toque do trim quando o fim esta perto da borda.
      final reordenar = find.descendant(
        of: find.byKey(ValueKey('linha-$id')),
        matching: find.byKey(const ValueKey('alca-reordenar')),
      );
      var sobreposto = false;
      if (reordenar.evaluate().isNotEmpty &&
          tester.getRect(reordenar.first).contains(alca)) {
        sobreposto = true;
        aviso(
          'com o fim do clipe a ${(tela().width - noTempo(id, fim).dx).toStringAsFixed(0)} dp '
          'da borda, a alca de trim (x=${alca.dx.toStringAsFixed(0)}) fica sob '
          'a alca de reordenar (${tester.getRect(reordenar.first).left.toStringAsFixed(0)}-'
          '${tester.getRect(reordenar.first).right.toStringAsFixed(0)}): o arrasto vira scrub',
        );
        // O que a pessoa faz: rola ate o fim ficar no meio da tela.
        final alvoX = tela().width * .6;
        await scrub(-(noTempo(id, fim).dx - alvoX));
        alca = noTempo(id, fim) + const Offset(15, 0);
      }
      if (alca.dx > tela().width - 4) {
        throw StateError('a alca do fim esta fora da tela (x=${alca.dx})');
      }
      await arrastar(alca, const Offset(-50, 0), passos: 6);
      final depois = camada(id)!;
      expect(depois.duration, lessThan(antes.duration));
      expect(depois.startTime, antes.startTime);
      await foto('04-aparado');
      return 'duracao ${seg(antes.duration)} s -> ${seg(depois.duration)} s '
          '(pps ${vista().pps.value.toStringAsFixed(0)}; '
          'alca sob o reordenar na 1a posicao=$sobreposto)';
    });

    await passo('5', 'Dividir no cabecote (barra contextual)', () async {
      final id = videoId!;
      final alvo = camada(id)!;
      var t = relogio();
      if (!(t > alvo.startTime && t < alvo.endTime)) {
        // O trim grudou o fim no cabecote (o ima): o cabecote volta para
        // dentro do clipe, arrastando o vazio para a direita.
        final meta =
            alvo.startTime.inMicroseconds + alvo.duration.inMicroseconds * .7;
        await scrub((t.inMicroseconds - meta) / 1e6 * vista().pps.value + 8);
        t = relogio();
      }
      expect(t > alvo.startTime && t < alvo.endTime, isTrue,
          reason: 'cabecote fora do clipe (${seg(t)} s)');
      expect(escolhida(), id);
      await tocar(
        find.byKey(const ValueKey('ferramenta-dividir'), skipOffstage: false),
        'Dividir',
        rolarEm: find
            .descendant(
              of: find.byKey(const ValueKey('barra-contextual')),
              matching: find.byType(Scrollable),
            )
            .first,
      );
      final v = projeto().layers.whereType<VideoLayer>().toList();
      expect(v, hasLength(2));
      v.sort((a, b) => a.startTime.compareTo(b.startTime));
      video2Id = v.last.id;
      videoId = v.first.id;
      expect(v.first.endTime, v.last.startTime);
      for (final l in v) {
        expect(find.byKey(ValueKey('linha-${l.id}')), findsOneWidget);
      }
      await foto('05-dividido');
      return 'duas camadas: ${seg(v.first.startTime)}-${seg(v.first.endTime)} '
          'e ${seg(v.last.startTime)}-${seg(v.last.endTime)} s';
    });

    await passo('6', '+ -> Texto -> Texto; digitar no painel Texto', () async {
      await tocar(find.byKey(const ValueKey('editor-adicionar')), '+');
      await tocar(find.byKey(const ValueKey('adicionar-aba-texto')), 'aba Texto');
      await tocar(find.byKey(const ValueKey('adicionar-texto')), 'Texto');
      final t = projeto().layers.whereType<TextLayer>().toList();
      expect(t, hasLength(1));
      textoId = t.single.id;
      expect(escolhida(), textoId);
      await tocar(find.byKey(const ValueKey('ferramenta-texto')), 'ferramenta Texto');
      expect(find.byKey(const ValueKey('painel-texto')), findsOneWidget);
      await tocar(find.byKey(const ValueKey('texto-campo')), 'campo do texto');
      await espera(1200);
      final comTeclado = teclado();
      await host('digitar', 'texto1', arg: 'Ola Aurea');
      var chegou = await ate(
        () => (camada(textoId!) as TextLayer?)?.text == 'Ola Aurea',
        limite: const Duration(seconds: 12),
      );
      var via = 'teclado do sistema (adb input text)';
      if (!chegou) {
        aviso('o teclado real nao escreveu (texto="${(camada(textoId!) as TextLayer).text}")');
        tester.testTextInput.register();
        await tester.enterText(find.byKey(const ValueKey('texto-campo')), 'Ola Aurea');
        tester.testTextInput.unregister();
        await espera(500);
        chegou = (camada(textoId!) as TextLayer).text == 'Ola Aurea';
        via = 'canal de texto do teste (fallback)';
      }
      await foto('06a-texto-com-teclado');
      final campo = tester.getRect(find.byKey(const ValueKey('texto-campo')));
      if (comTeclado > 0 && campo.bottom > tela().height - teclado()) {
        aviso('o campo do texto fica atras do teclado');
      }
      final fechar = find.byKey(const ValueKey('texto-fechar-teclado'));
      if (fechar.evaluate().isNotEmpty) {
        await tocar(fechar, 'fechar teclado');
      }
      await espera(800);
      await foto('06b-texto-sem-teclado');
      expect(chegou, isTrue);
      return 'texto="${(camada(textoId!) as TextLayer).text}" via $via; '
          'teclado ${comTeclado.toStringAsFixed(0)} dp; '
          'campo ${campo.top.toStringAsFixed(0)}-${campo.bottom.toStringAsFixed(0)}';
    });

    await passo('7', 'Animar posicao: losango, mover cabecote, X, losango',
        () async {
      final id = textoId!;
      // O painel Texto cobre a barra: fechar pelo ✓ e abrir Transformar.
      await tocar(find.byKey(const ValueKey('painel-texto-fechar')), '✓ do Texto');
      await tocar(
        find.byKey(const ValueKey('ferramenta-transformar'), skipOffstage: false),
        'ferramenta Transformar',
        rolarEm: find
            .descendant(
              of: find.byKey(const ValueKey('barra-contextual')),
              matching: find.byType(Scrollable),
            )
            .first,
      );
      expect(find.byKey(const ValueKey('painel-transformar')), findsOneWidget);
      await tocar(find.byKey(const ValueKey('kf-posicao')), 'losango Posicao');
      final l1 = camada(id)!;
      expect(l1.positionTimesUs, hasLength(1));
      final t1 = relogio();
      // MOVER O CABECOTE com o painel aberto: arrastar a regua.
      final regua = tester.getRect(find.byKey(const ValueKey('timeline-regua')));
      // Longe do centro: ali mora a pega do proprio cabecote.
      await arrastar(
        regua.center + const Offset(90, 0),
        const Offset(-100, 0),
        passos: 10,
      );
      var t2 = relogio();
      var como = 'regua';
      if ((t2 - t1).abs() < const Duration(milliseconds: 200)) {
        aviso('arrastar a regua nao moveu o cabecote');
        // O que sobra da timeline acima do painel.
        final painel = tester.getRect(find.byKey(const ValueKey('painel-transformar')));
        await arrastar(
          Offset(regua.center.dx + 90, (regua.bottom + painel.top) / 2),
          const Offset(-100, 0),
        );
        t2 = relogio();
        como = 'faixa de linhas acima do painel';
      }
      expect(t2 - t1, greaterThan(const Duration(milliseconds: 300)));
      final xAntes = container
          .read(projetoVisivelProvider)
          .layerById(id)!
          .position
          .valueAt(camada(id)!.localTime(t2))
          .dx;
      final caixa = find.byKey(const ValueKey('valor-posicao-x'));
      await arrastar(tester.getCenter(caixa), const Offset(120, 0), passos: 12);
      final xDepois = container
          .read(projetoVisivelProvider)
          .layerById(id)!
          .position
          .valueAt(camada(id)!.localTime(t2))
          .dx;
      expect(xDepois, greaterThan(xAntes + 20), reason: 'o X nao mudou');
      await tocar(find.byKey(const ValueKey('kf-posicao')), 'losango Posicao (2)');
      final l = camada(id)!;
      final marcas = l.positionTimesUs.toList()..sort();
      expect(marcas, hasLength(2), reason: 'marcas: $marcas');
      final v0 = l.position.valueAt(Duration(microseconds: marcas.first));
      final v1 = l.position.valueAt(Duration(microseconds: marcas.last));
      expect(v1.dx, greaterThan(v0.dx + 20));
      await foto('07-posicao-animada');
      return 'cabecote ${seg(t1)} -> ${seg(t2)} s pela $como; X '
          '${v0.dx.toStringAsFixed(0)} -> ${v1.dx.toStringAsFixed(0)}; '
          '2 marcas em ${marcas.map((u) => (u / 1e6).toStringAsFixed(2)).join(', ')} s';
    });

    await passo('8', 'Curva: toque longo no losango -> Ease In', () async {
      final id = textoId!;
      final losango = find.byKey(const ValueKey('kf-posicao'));
      await tester.longPressAt(tester.getCenter(losango));
      await espera(900);
      expect(find.byKey(const ValueKey('curva')), findsOneWidget,
          reason: 'o editor de curva nao abriu');
      await foto('08a-curva');
      await tocar(find.byKey(const ValueKey('curva-preset-ease-in')), 'Ease In');
      final l = camada(id)!;
      final marcas = l.positionTimesUs.toList()..sort();
      final ease = l.position.easeAt(Duration(microseconds: marcas.first));
      expect(ease.mesmoPresetQue(Easing.easeIn), isTrue,
          reason: 'o trecho nao ficou Ease In');
      await foto('08b-curva-ease-in');
      await tocar(find.byKey(const ValueKey('curva-fechar')), '✓ da curva');
      expect(find.byKey(const ValueKey('curva')), findsNothing);
      return 'trecho 1 = Ease In';
    });

    await passo('9', 'Efeitos: Deep Glow, Motion Tile (busca) e Time Remap',
        () async {
      final fecharT = find.byKey(const ValueKey('painel-transformar-fechar'));
      if (fecharT.evaluate().isNotEmpty) await tocar(fecharT, '✓ do Transformar');
      // O pedaco de video debaixo do cabecote.
      final id = video2Id!;
      final l = camada(id)!;
      final t = relogio();
      final s = t > l.startTime && t < l.endTime
          ? (t.inMicroseconds / 1e6) - 0.2
          : (l.startTime.inMicroseconds + l.duration.inMicroseconds / 2) / 1e6;
      await tester.tapAt(noTempo(id, s));
      await espera(600);
      expect(escolhida(), id, reason: 'tocar no pedaco de video nao escolheu');
      await tocar(find.byKey(const ValueKey('ferramenta-efeitos')), 'ferramenta Efeitos');
      expect(find.byKey(const ValueKey('painel-efeitos')), findsOneWidget);

      Future<void> buscarEAplicar(String texto, EffectType tipo, String n) async {
        await tocar(find.byKey(const ValueKey('efeitos-adicionar')), '+ do Efeitos');
        await tocar(find.byKey(const ValueKey('catalogo-busca')), 'busca');
        await espera(800);
        await host('digitar', 'busca$n', arg: texto);
        final tile = find.byKey(
          ValueKey('catalogo-efeito-${effectSpecs[tipo]!.id}'),
          skipOffstage: false,
        );
        var achou = await ate(() => tile.evaluate().isNotEmpty,
            limite: const Duration(seconds: 8));
        if (!achou) {
          aviso('busca por teclado real nao filtrou "$texto"; canal do teste');
          tester.testTextInput.register();
          await tester.enterText(find.byKey(const ValueKey('catalogo-busca')), texto);
          tester.testTextInput.unregister();
          await espera(600);
          achou = tile.evaluate().isNotEmpty;
        }
        expect(achou, isTrue, reason: '"$texto" nao apareceu no catalogo');
        await foto('09-busca-$n');
        await tocar(tile, texto);
        await espera(600);
      }

      await buscarEAplicar('Deep Glow', EffectType.deepGlow, '1');
      await buscarEAplicar('Motion Tile', EffectType.motionTile, '2');
      // TEMPO: a categoria, sem busca.
      await tocar(find.byKey(const ValueKey('efeitos-adicionar')), '+ do Efeitos');
      final filtros = find
          .descendant(
            of: find.byKey(const ValueKey('catalogo-filtros')),
            matching: find.byType(Scrollable),
          )
          .first;
      await tocar(
        find.byKey(const ValueKey('catalogo-cat-Time'), skipOffstage: false),
        'categoria Tempo',
        rolarEm: filtros,
      );
      await tocar(
        find.byKey(
          ValueKey('catalogo-efeito-${effectSpecs[EffectType.timeRemap]!.id}'),
          skipOffstage: false,
        ),
        'Time Remap',
      );
      await espera(800);
      final efeitos = camada(id)!.effects;
      final tipos = efeitos.map((e) => e.type).toList();
      _console('FLUXO-EFEITOS ${tipos.map((t) => t.name).toList()}');
      expect(tipos, containsAll([
        EffectType.deepGlow,
        EffectType.motionTile,
        EffectType.timeRemap,
      ]));
      // OS CARTOES: a pilha e uma lista preguicosa; o de baixo so existe
      // depois de rolar ate ele (como o dedo faria).
      final pilha = find
          .descendant(
            of: find.byKey(const ValueKey('pilha-de-efeitos')),
            matching: find.byType(Scrollable),
          )
          .first;
      final cartoes = <String>[];
      var vistos = 0;
      for (final e in efeitos) {
        final c = find.byKey(ValueKey('cartao-${e.id}'), skipOffstage: false);
        if (c.evaluate().isEmpty) {
          await rolarAte(c, 'cartao ${e.type.name}', em: pilha);
        }
        final n = c.evaluate().length;
        if (n == 1) vistos++;
        cartoes.add('${e.type.name}=$n');
      }
      _console('FLUXO-CARTOES $cartoes');
      expect(vistos, efeitos.length, reason: 'cartoes na pilha: $cartoes');
      await foto('09b-pilha-de-efeitos');
      final depois = camada(id)!;
      return 'pilha: ${tipos.map((t) => t.name).join(', ')} (3 cartoes); '
          'clipe ${seg(depois.startTime)}-${seg(depois.endTime)} s '
          '(antes ${seg(l.startTime)}-${seg(l.endTime)})';
    });

    await passo('10', '+ -> 3D -> Solido 3D; posicao X e escala', () async {
      final fecharE = find.byKey(const ValueKey('painel-efeitos-fechar'));
      if (fecharE.evaluate().isNotEmpty) await tocar(fecharE, '✓ do Efeitos');
      await tocar(find.byKey(const ValueKey('editor-adicionar')), '+');
      await tocar(find.byKey(const ValueKey('adicionar-aba-3d')), 'aba 3D');
      await foto('10a-folha-3d');
      await tocar(find.byKey(const ValueKey('adicionar-3d-solido')), 'Solido 3D');
      final s = projeto().layers.whereType<Element3DLayer>().toList();
      expect(s, hasLength(1));
      solidoId = s.single.id;
      expect(escolhida(), solidoId);
      await espera(1500);
      await foto('10b-solido-criado');
      await tocar(find.byKey(const ValueKey('ferramenta-transformar')), 'Transformar');
      final t = relogio();
      final antes = camada(solidoId!)!;
      final local = antes.localTime(t);
      final x0 = antes.position.valueAt(local).dx;
      await arrastar(
        tester.getCenter(find.byKey(const ValueKey('valor-posicao-x'))),
        const Offset(100, 0),
        passos: 12,
      );
      await tocar(find.byKey(const ValueKey('painel-transformar-aba-1')), 'aba Escala');
      final e0 = camada(solidoId!)!.scaleX.valueAt(local);
      await arrastar(
        tester.getCenter(find.byKey(const ValueKey('prop-escala'))),
        const Offset(80, 0),
        passos: 12,
      );
      final depois = camada(solidoId!)!;
      final x1 = depois.position.valueAt(local).dx;
      final e1 = depois.scaleX.valueAt(local);
      expect(x1, greaterThan(x0 + 20), reason: 'X nao mudou');
      expect((e1 - e0).abs(), greaterThan(0.05), reason: 'escala nao mudou');
      await espera(1200);
      await foto('10c-solido-transformado');
      await tocar(find.byKey(const ValueKey('painel-transformar-fechar')), '✓ do Transformar');
      return 'X ${x0.toStringAsFixed(0)} -> ${x1.toStringAsFixed(0)}; '
          'escala ${(e0 * 100).toStringAsFixed(0)}% -> '
          '${(e1 * 100).toStringAsFixed(0)}%';
    });

    await passo('11', 'Exportar pela barra do topo (480p)', () async {
      final docs = await getApplicationDocumentsDirectory();
      final saidas = Directory('${docs.path}/exports');
      final antes = saidas.existsSync()
          ? saidas.listSync().map((f) => f.path).toSet()
          : <String>{};
      await tocar(find.byKey(const ValueKey('topo-exportar')), 'Exportar');
      await ate(() => find.byType(ExportVideoScreen).evaluate().isNotEmpty);
      await espera(1200);
      await tocar(
        find.byKey(const ValueKey('export-predefinicao-personalizado')),
        'Personalizado',
      );
      final p480 = find.descendant(
        of: find.byKey(const ValueKey('export-ajuste-tamanho')),
        matching: find.text('480p'),
      );
      await tocar(p480, '480p', medir: false);
      await foto('11a-exportar-ajustes');
      await tocar(find.byKey(const ValueKey('export-exportar')), 'botao Exportar');
      final relogioDoRender = Stopwatch()..start();
      final terminou = await ate(
        () =>
            find.byKey(const ValueKey('export-titulo-do-fim')).evaluate().isNotEmpty ||
            find.byKey(const ValueKey('export-tentar-de-novo')).evaluate().isNotEmpty,
        limite: const Duration(minutes: 15),
      );
      relogioDoRender.stop();
      await foto('11b-exportar-fim');
      expect(terminou, isTrue, reason: 'a exportacao nao terminou em 15 min');
      final erro = find.byKey(const ValueKey('export-tentar-de-novo'));
      if (erro.evaluate().isNotEmpty) {
        final textos = find
            .descendant(of: find.byType(ExportVideoScreen), matching: find.byType(Text))
            .evaluate()
            .map((e) => (e.widget as Text).data ?? '')
            .where((s) => s.length > 12)
            .join(' / ');
        throw StateError('a exportacao falhou: $textos');
      }
      final titulo = tester
          .widget<Text>(find.byKey(const ValueKey('export-titulo-do-fim')))
          .data;
      final galeria = find.byKey(const ValueKey('export-galeria'));
      final motivo = galeria.evaluate().isEmpty
          ? ''
          : ' (${tester.widget<Text>(galeria).data})';
      final novos = saidas.existsSync()
          ? saidas
                .listSync()
                .whereType<File>()
                .where((f) => !antes.contains(f.path) && f.path.endsWith('.mp4'))
                .toList()
          : <File>[];
      expect(novos, isNotEmpty, reason: 'nenhum mp4 novo em exports/');
      final bytes = novos.first.lengthSync();
      expect(bytes, greaterThan(0));
      // O PC copia o arquivo para conferir os quadros (ffmpeg).
      final relativo = novos.first.path.substring(
        novos.first.path.indexOf('/app_flutter/') + 1,
      );
      await host('puxar', 'exportado', arg: relativo);
      // Fecha a tela pelo X.
      await tocar(
        find.descendant(
          of: find.byType(ExportVideoScreen),
          matching: find.byIcon(CupertinoIcons.xmark),
        ),
        'X da exportacao',
        medir: false,
      );
      await ate(() => find.byType(ExportVideoScreen).evaluate().isEmpty);
      return '"$titulo"$motivo; ${novos.first.uri.pathSegments.last} '
          '$bytes bytes; render ${relogioDoRender.elapsed.inSeconds} s';
    });

    await passo('12', 'Mover um clipe e desfazer num passo', () async {
      final id = textoId!;
      final l = camada(id)!;
      final t = relogio();
      final dentro = t > l.startTime && t < l.endTime
          ? t.inMicroseconds / 1e6 + 0.2
          : (l.startTime.inMicroseconds + l.duration.inMicroseconds / 2) / 1e6;
      await tester.tapAt(noTempo(id, dentro));
      await espera(600);
      expect(escolhida(), id, reason: 'tocar no clipe de texto nao escolheu');
      final inicio0 = camada(id)!.startTime;
      final quantas = projeto().layers.length;
      final g = await tester.startGesture(noTempo(id, dentro));
      final trilha = <String>[];
      for (var i = 0; i < 6; i++) {
        await g.moveBy(const Offset(12, 0));
        await tester.pump(const Duration(milliseconds: 100));
        trilha.add(seg(camada(id)!.startTime));
      }
      await g.up();
      await espera(600);
      final inicio1 = camada(id)!.startTime;
      expect(inicio1, isNot(inicio0), reason: 'o arrasto nao moveu o clipe');
      await tocar(find.byKey(const ValueKey('transporte-desfazer')), 'Desfazer');
      final inicio2 = camada(id)!.startTime;
      expect(inicio2, inicio0, reason: 'um desfazer nao devolveu o inicio');
      expect(projeto().layers.length, quantas);
      await foto('12-desfeito');
      return 'inicio ${seg(inicio0)} -> ${seg(inicio1)} -> desfazer -> '
          '${seg(inicio2)} s (72 dp; a cada 12 dp: ${trilha.join(' ')}; '
          'pps ${vista().pps.value.toStringAsFixed(0)})';
    });

    await host('fim', 'fim', limite: const Duration(seconds: 5));

    // ----------------------------------------------------------- relatorio
    final b = StringBuffer('FLUXO-RELATORIO\n');
    for (final r in resultados) {
      b.writeln(
        '| ${r.n} | ${r.ok ? 'PASS' : 'FAIL'} | '
        '${(r.tempo.inMilliseconds / 1000).toStringAsFixed(1)} s | '
        '${r.titulo} | ${r.obs} |',
      );
    }
    for (final linha in b.toString().split('\n')) {
      _console(linha);
    }
    // ignore: avoid_print
    print(b);
    final falhas = resultados.where((r) => !r.ok).map((r) => r.n).toList();
    expect(falhas, isEmpty, reason: 'passos que falharam: $falhas');
  });
}
