# UI compacta — 08/10/2026

## Implementação

- Android/Kotlin e iOS/Swift: a fileira rápida da camada mostra somente cortar à esquerda, dividir e cortar à direita. O cabeçote antes do início substitui os cortes por **Esticar início**; depois do fim, por **Esticar fim**. Nas bordas exatas, continuam os três cortes, sem extensão sem efeito.
- O motor C++ decide as ações disponíveis (`query_clip_time_actions`). As operações 8/9 de `edit_clip_time` estendem até o cabeçote mantendo a outra borda e os vizinhos fixos, inclusive com Linha Magnética ligada. Continuam valendo bloqueio, limites da fonte, relógio da animação e desfazer.
- Velocidade e entrar/desagrupar foram transferidos para o menu da camada. As fichas de edição permanecem abaixo dos cortes.
- Seleção múltipla: removidas as faixas de distribuição no tempo e escalonamento de camadas/keyframes riscadas na referência. Permanecem a fileira de cortes/alinhamento temporal e a fileira de alinhamento espacial. A reserva do painel caiu para 132 dp/pt, incluindo puxador e margens.
- Categorias de adicionar: fichas e dicas determinam a altura do painel, com rolagem ao atingir o limite da janela. Forma, mídia e presets mantêm uma área para suas grades/galerias. Áudio e Texto ocupam somente uma fileira, sem a área vazia anterior.
- Rótulos visíveis e descrições de acessibilidade foram adicionados aos sete catálogos Android e ao catálogo iOS gerado.
- A referência veio das duas imagens fornecidas pelo usuário. Nenhum código, shader ou asset de outro aplicativo foi incorporado.

## Compilação

| Alvo | Resultado |
| --- | --- |
| Android `uiTest`, x86_64, motor JNI e APK de instrumentação | Compilado; instalado no emulador API 35. |
| Motor C++ / testes host Release | Compilado com MSVC. |
| iOS / SwiftUI + Objective-C++ + Metal | Implementado, sem compilação nativa neste Windows; requer macOS/Xcode. |

## Validação

- Android/JVM: 13 testes de `EditorLayoutTest` passaram.
- Motor C++: 49 testes, 4.183 verificações e zero falhas nos grupos `ClipTime`, `ClipEdit` e `TimeArrangement`, incluindo preservação da fonte, extensão magnética, bloqueio e desfazer.
- Android nativo: quatro testes de instrumentação passaram no emulador `Aurea_API35`. Cobrem os controles reais, cortes e desfazer, extensão até o cabeçote com Linha Magnética ligada, tela de 320×480 dp com fonte 130%, alinhamento/corte em lote, ausência das faixas removidas e altura do painel de Áudio.
- Auditorias iOS: `check_api_swift.py`, `check_symbols.py` e `check_pbxproj.py` passaram. `check_editor_layout.py` conferiu a regra estática; os casos Swift não foram executados porque não há `swiftc` neste ambiente.
- A auditoria de escopo geral já aponta `test_text.cpp` referenciando `platform/ios`; esse arquivo não foi alterado por este trabalho.
- Aparelhos físicos Android e iPhone: **não validados**. Emulador Android, testes do motor e auditorias estáticas não comprovam execução nativa iOS.
- A busca da skill UI/UX não trouxe uma recomendação específica verificável para a altura destes painéis. O comportamento foi definido pelas referências do usuário; as regras gerais de toque e acessibilidade foram usadas como orientação complementar.

## Revisão interativa

A instalação descartável `com.aurea.aurea.uitest` inclui `UiReviewActivity`, uma entrada diretamente no editor de produção com três formas e intervalos diferentes. Está disponível somente no source set `uiTest`, sem inclusão nos APKs debug/release. Não usa login nem serviços externos. Para reabrir a demonstração:

```powershell
adb -s emulator-5554 shell am start -n com.aurea.aurea.uitest/com.aurea.aurea.editor.UiReviewActivity
```

APKs, capturas e logs desta validação ficam em `build/android/` e `build/validation/compact-ui-*`.


## Atualização da entrega 0.0.8 / 2147

A compilação iOS Release passou no macOS/Xcode 26 em 09/10/2026, na execução
[37955312771](https://github.com/ueeruan/aurea/actions/runs/37955312771).
Bundle e IPA validados; 166/166 shaders aprovados pelo compilador/linker Metal
da Apple. As referências a compilação pendente acima registram o estado na
validação original. Execução em simulador iOS/iPhone continua pendente;
compilação não comprova execução das interfaces.
