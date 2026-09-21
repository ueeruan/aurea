// O ANIMADOR DE TEXTO VIROU EFEITO (20/09, pedido do dono).
//
// O painel proprio saiu — posicoes, grade de miniaturas, modo avancado,
// aba gigante. Ficou um cartao na pilha de efeitos, com as linhas de
// parametro e o losango da casa.
//
// Este teste guarda as duas coisas que nao podem regredir:
//
//   1. O MOTOR e um so. Os dez presets sao dez receitas passando por
//      `animadorDaReceita`; nenhum tem codigo proprio.
//   2. O OFFSET PERCORRE AS LETRAS. De -100 a 100 a janela do seletor
//      atravessa a frase, e da para MEDIR isso por caractere.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/effect_preset_store.dart';
import 'package:aurea/src/features/editor/domain/animador_de_texto.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/text_animator.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/efeitos.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/efeitos/miniatura_do_efeito.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'ui/paineis/apoio_paineis.dart';

/// A cobertura de cada unidade do texto, na base do seletor.
List<double> _coberturaPorUnidade(
  TextAnimator a,
  String texto, [
  Duration t = Duration.zero,
]) {
  final u = TextUnits.of(texto);
  return [for (var i = 0; i < u.length; i++) u.coverageFor(a.selectors, i, t)];
}

/// O animador de uma receita com o offset PARADO no valor pedido (em %).
TextAnimator _comOffset(double porcento, {SelectorShape? forma}) {
  return animadorDaReceita(
    ReceitaDoAnimador(
      forma: forma ?? SelectorShape.triangle,
      offset: porcento,
      varredura: Duration.zero,
      opacidade: 0,
    ),
  );
}

void main() {
  setUpAll(() {
    EffectThumbnailCache.semDisco = true;
    EffectPresetStore.semArquivo = true;
  });

  group('o motor', () {
    test('aplicar o efeito cria o animador, pronto e com faixa', () {
      final c = ProviderContainer();
      addTearDown(c.dispose);
      final e = c.read(editorControllerProvider.notifier);
      e.addTextLayer(Duration.zero);
      final id = c.read(editorControllerProvider).layers.first.id;

      expect(
        (c.read(editorControllerProvider).layerById(id)! as TextLayer)
            .animators,
        isEmpty,
      );

      final animatorId = e.addTextAnimator(id);
      expect(animatorId, isNotNull);

      final camada =
          c.read(editorControllerProvider).layerById(id)! as TextLayer;
      expect(camada.animators, hasLength(1));
      final a = camada.animators.single;
      expect(a.name, nomeDoAnimadorDeTexto);
      expect(ehAnimadorDeTexto(a), isTrue, reason: 'tem faixa para editar');
      // NASCE PRONTO: ja tem propriedade e ja anda sozinho.
      expect(a.properties, isNotEmpty);
      expect(faixaDoAnimador(a)!.offset.isAnimated, isTrue);
    });

    test('o Offset de -100 a 100 percorre as letras', () {
      const texto = 'ABCDE';
      // A unidade MAIS coberta em cada offset. Com a forma triangulo o
      // pico da janela fica em offset+0.5 — e o que faz a animacao
      // "andar" pela frase em vez de ligar tudo de uma vez.
      // Nulo quando a janela ja saiu da frase (nada coberto).
      int? maisCoberta(double porcento) {
        final cob = _coberturaPorUnidade(_comOffset(porcento), texto);
        var melhor = 0;
        for (var i = 1; i < cob.length; i++) {
          if (cob[i] > cob[melhor]) melhor = i;
        }
        return cob[melhor] <= 1e-9 ? null : melhor;
      }

      expect(maisCoberta(-50), 0, reason: 'a janela entra pela primeira');
      expect(maisCoberta(0), 2, reason: 'no meio do caminho, a do meio');
      expect(maisCoberta(50), 4, reason: 'sai pela ultima');

      // E ANDA SEM VOLTAR: varrendo -100..100 o pico nunca recua, e
      // TODA letra chega a ser a mais coberta em algum momento.
      var anterior = -1;
      final visitadas = <int>{};
      for (var o = -100.0; o <= 100.0; o += 1) {
        final atual = maisCoberta(o);
        if (atual == null) continue;
        expect(
          atual,
          greaterThanOrEqualTo(anterior),
          reason: 'em offset $o o pico recuou',
        );
        anterior = atual;
        visitadas.add(atual);
      }
      expect(visitadas, {0, 1, 2, 3, 4}, reason: 'percorreu a frase inteira');

      // NAS PONTAS NAO SOBRA NADA: a janela saiu da frase inteira.
      expect(
        _coberturaPorUnidade(_comOffset(-100), texto).every((c) => c <= 0.001),
        isTrue,
      );
      expect(
        _coberturaPorUnidade(_comOffset(100), texto).every((c) => c <= 0.001),
        isTrue,
      );
    });

    test('cada forma tem o perfil que o nome promete', () {
      const texto = 'ABCDE';
      List<double> perfil(SelectorShape s) => _coberturaPorUnidade(
        animadorDaReceita(
          ReceitaDoAnimador(
            forma: s,
            // Square so vira degrau seco com suavidade zero.
            suavidade: s == SelectorShape.square ? 0 : 100,
            varredura: Duration.zero,
            opacidade: 0,
          ),
        ),
        texto,
      );

      final subindo = perfil(SelectorShape.rampUp);
      for (var i = 1; i < subindo.length; i++) {
        expect(subindo[i], greaterThan(subindo[i - 1]));
      }

      final descendo = perfil(SelectorShape.rampDown);
      for (var i = 1; i < descendo.length; i++) {
        expect(descendo[i], lessThan(descendo[i - 1]));
      }

      final quadrado = perfil(SelectorShape.square);
      expect(quadrado.every((c) => (c - 1).abs() < 1e-9), isTrue);

      // Triangulo, Round e Smooth sao morros: pico no meio e simetria.
      for (final s in [
        SelectorShape.triangle,
        SelectorShape.round,
        SelectorShape.smooth,
      ]) {
        final p = perfil(s);
        expect(p[2], greaterThan(p[0]), reason: '$s sobe ate o meio');
        expect(p[2], greaterThan(p[4]), reason: '$s desce depois do meio');
        expect(p[0], closeTo(p[4], 1e-9), reason: '$s e simetrico');
        expect(p[1], closeTo(p[3], 1e-9), reason: '$s e simetrico');
      }

      // E O MESMO SELETOR: so a forma muda entre eles.
      expect(perfil(SelectorShape.rampUp), isNot(perfil(SelectorShape.round)));
    });

    test('a unidade agrupa: palavra e linha movem o bloco inteiro', () {
      TextAnimator comUnidade(SelectorBasedOn u) => animadorDaReceita(
        ReceitaDoAnimador(
          unidade: u,
          forma: SelectorShape.rampUp,
          varredura: Duration.zero,
          opacidade: 0,
        ),
      );

      // "ab cd" → duas palavras; as letras de cada uma andam juntas.
      final porPalavra = _coberturaPorUnidade(
        comUnidade(SelectorBasedOn.words),
        'ab cd',
      );
      expect(porPalavra[0], closeTo(porPalavra[1], 1e-9));
      expect(porPalavra[3], closeTo(porPalavra[4], 1e-9));
      expect(porPalavra[3], greaterThan(porPalavra[0]));

      // Por caractere, as mesmas letras deixam de andar juntas.
      final porCaractere = _coberturaPorUnidade(
        comUnidade(SelectorBasedOn.characters),
        'ab cd',
      );
      expect(porCaractere[1], greaterThan(porCaractere[0]));

      // "ab\ncd" → duas linhas.
      final porLinha = _coberturaPorUnidade(
        comUnidade(SelectorBasedOn.lines),
        'ab\ncd',
      );
      expect(porLinha[0], closeTo(porLinha[1], 1e-9));
      expect(porLinha[3], closeTo(porLinha[4], 1e-9));
      expect(porLinha[3], greaterThan(porLinha[0]));
    });

    test('keyframe no Offset anima de verdade', () {
      final c = ProviderContainer();
      addTearDown(c.dispose);
      final e = c.read(editorControllerProvider.notifier);
      e.addTextLayer(Duration.zero);
      final id = c.read(editorControllerProvider).layers.first.id;
      e.editTextLayer(id, text: 'ABCDE');

      // Nasce parado de proposito: offset fixo, sem varredura.
      final animatorId = e.addTextAnimator(
        id,
        receita: const ReceitaDoAnimador(
          forma: SelectorShape.triangle,
          offset: -50,
          varredura: Duration.zero,
          opacidade: 0,
        ),
      )!;
      TextAnimator animador() =>
          (c.read(editorControllerProvider).layerById(id)! as TextLayer)
              .animators
              .single;
      final faixaId = faixaDoAnimador(animador())!.id;
      expect(faixaDoAnimador(animador())!.offset.isAnimated, isFalse);

      // O LOSANGO grava o instante; o segundo valor vira o segundo
      // keyframe pelo mesmo caminho de sempre.
      e.toggleSelectorParamKeyframe(
        id,
        animatorId,
        faixaId,
        'offset',
        Duration.zero,
      );
      expect(faixaDoAnimador(animador())!.offset.isAnimated, isTrue);

      // SEM AUTO KEYFRAME: mexer longe de uma marca deixa a edicao
      // PENDENTE; e o losango do instante que a crava. E a mesma regra
      // do resto do app, e o animador nao tem excecao.
      e.editSelectorParam(
        id,
        animatorId,
        faixaId,
        'offset',
        const Duration(seconds: 1),
        porcentoParaFracao(50),
      );
      expect(faixaDoAnimador(animador())!.offset.keyframes, hasLength(1));
      e.toggleSelectorParamKeyframe(
        id,
        animatorId,
        faixaId,
        'offset',
        const Duration(seconds: 1),
      );

      final offset = faixaDoAnimador(animador())!.offset;
      expect(offset.keyframes, hasLength(2));
      expect(
        offset.valueAt(Duration.zero),
        closeTo(porcentoParaFracao(-50), 1e-6),
      );
      expect(
        offset.valueAt(const Duration(seconds: 1)),
        closeTo(porcentoParaFracao(50), 1e-6),
      );

      // E a animacao acontece: a cobertura de uma letra muda no tempo.
      final inicio = _coberturaPorUnidade(animador(), 'ABCDE');
      final fim = _coberturaPorUnidade(
        animador(),
        'ABCDE',
        const Duration(seconds: 1),
      );
      expect(inicio[0], greaterThan(fim[0]));
      expect(fim[4], greaterThan(inicio[4]));
    });
  });

  group('os presets', () {
    test('sao os dez pedidos, e todos pelo mesmo caminho', () {
      expect(presetsDoAnimador.map((p) => p.nome).toList(), [
        'Palavra por palavra',
        'Letra por letra',
        'Pop',
        'Bounce',
        'Fade Up',
        'Fade Down',
        'Slide Left',
        'Slide Right',
        'Scale In',
        'Typewriter',
      ]);

      for (final p in presetsDoAnimador) {
        final a = p.construir();
        // UM SELETOR DE FAIXA, sempre: e o que o cartao edita. Preset
        // com seletor proprio seria implementacao separada.
        expect(a.selectors, hasLength(1), reason: p.nome);
        expect(a.selectors.single, isA<RangeSelector>(), reason: p.nome);
        // As propriedades sao sempre do conjunto do cartao.
        for (final prop in a.properties) {
          expect(propriedadesDoAnimador, contains(prop.type), reason: p.nome);
        }
        // O QUE `construir` DEVOLVE E O QUE `animadorDaReceita` MONTA.
        final direto = animadorDaReceita(p.receita, nome: p.nome);
        expect(a.name, direto.name);
        expect(a.properties.map((x) => x.type), direto.properties.map((x) => x.type));
        final f1 = faixaDoAnimador(a)!;
        final f2 = faixaDoAnimador(direto)!;
        expect(f1.shape, f2.shape);
        expect(f1.basedOn, f2.basedOn);
      }
    });

    test('cada preset produz valores diferentes', () {
      final assinaturas = <String, String>{};
      for (final p in presetsDoAnimador) {
        final a = p.construir();
        final f = faixaDoAnimador(a)!;
        final assinatura = [
          f.basedOn.name,
          f.shape.name,
          f.smoothness.valueAt(Duration.zero).toStringAsFixed(2),
          f.easeHigh.valueAt(Duration.zero).toStringAsFixed(2),
          f.easeLow.valueAt(Duration.zero).toStringAsFixed(2),
          for (final prop in propriedadesDoAnimador)
            valorDaPropriedade(a, prop, Duration.zero).toStringAsFixed(2),
        ].join('|');
        expect(
          assinaturas.containsKey(assinatura),
          isFalse,
          reason: '${p.nome} repete ${assinaturas[assinatura]}',
        );
        assinaturas[assinatura] = p.nome;
      }
      expect(assinaturas, hasLength(presetsDoAnimador.length));
    });

    test('trocar o preset nao troca a identidade do cartao', () {
      final c = ProviderContainer();
      addTearDown(c.dispose);
      final e = c.read(editorControllerProvider.notifier);
      e.addTextLayer(Duration.zero);
      final id = c.read(editorControllerProvider).layers.first.id;
      final animatorId = e.addTextAnimator(id)!;

      e.aplicarPresetNoAnimador(
        id,
        animatorId,
        presetsDoAnimador.firstWhere((p) => p.id == 'typewriter'),
      );
      final a = (c.read(editorControllerProvider).layerById(id)! as TextLayer)
          .animators
          .single;
      expect(a.id, animatorId, reason: 'o cartao continua o mesmo');
      expect(a.name, 'Typewriter');
      expect(faixaDoAnimador(a)!.shape, SelectorShape.square);
    });
  });

  // A PORTA (Efeitos -> Texto -> Animador de Texto) mora em
  // test/ui/paineis/efeitos_catalogo_test.dart.
  group('o cartao', () {
    testWidgets('mora na pilha de efeitos, com os parametros e o losango', (
      tester,
    ) async {
      final (b, id) = await montarPainel(
        tester,
        preparar: (c) {
          c.addTextLayer(Duration.zero);
          final id = c.projetoCompleto.layers.single.id;
          c.editTextLayer(id, text: 'AUREA');
          return id;
        },
        painel: (id) => PainelEfeitos(layerId: id),
        tamanho: const Size(430, 932),
        altura: 900,
      );
      // Aplicado com o painel ja montado: o recem-aplicado abre sozinho.
      final animatorId = b.c.addTextAnimator(id)!;
      await tester.pumpAndSettle();
      final k = 'animador-$animatorId';

      // O CABECALHO E O DOS OUTROS EFEITOS: nome e menu •••.
      expect(find.byKey(ValueKey('$k-cabecalho')), findsOneWidget);
      expect(find.byKey(ValueKey('$k-menu')), findsOneWidget);

      // A faixa nasce aberta, com as linhas pedidas.
      for (final chave in ['start', 'end', 'offset', 'unidade', 'forma']) {
        expect(
          find.byKey(ValueKey('prop-$k-$chave')),
          findsOneWidget,
          reason: 'a linha $chave tem de estar na ficha',
        );
      }

      // O LOSANGO E O DA CASA: a chave `kf-` vem da mesma linha de
      // propriedade que o app inteiro usa.
      final losango = find.byKey(ValueKey('kf-$k-start'));
      expect(losango, findsOneWidget);

      TextAnimator animador() =>
          (b.camada(id) as TextLayer).animators.single;
      expect(faixaDoAnimador(animador())!.start.isAnimated, isFalse);
      await tester.ensureVisible(losango);
      await tester.pumpAndSettle();
      await tester.tap(losango);
      await tester.pumpAndSettle();
      expect(faixaDoAnimador(animador())!.start.isAnimated, isTrue);

      // Apagar (no •••) tira o efeito da pilha, como qualquer outro.
      await tester.ensureVisible(find.byKey(ValueKey('$k-menu')));
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(ValueKey('$k-menu')));
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('menu-animador-apagar')));
      await tester.pumpAndSettle();
      expect((b.camada(id) as TextLayer).animators, isEmpty);
      expect(tester.takeException(), isNull);
    });
  });
}
