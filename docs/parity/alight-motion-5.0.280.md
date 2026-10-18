# Timeline, keyframes e máscaras — referência Alight Motion 5.0.280

Data: 13/10/2026. Pedido: analisar o APK fornecido usando REA e levar os comportamentos e a organização visual dos três sistemas ao AUREA, em Android e iOS. Correção adicional do usuário: o primeiro losango deve coincidir com a borda temporal do corpo do clipe, sem aparecer sobre a tampa/alça anterior à barra.

## Identidade e método

- APK fornecido: `Alight_Motion_5.0.280_1789568706_latestmodapks.com.apk`.
- SHA-256: `4caeb6dea44e8f123b88833aef7b28dd54f4a48fb0cc6a9e5085ba4c1bbf4159`.
- Manifesto observado: `com.alightcreative.motion`, versão `5.0.280`, código `1028519`, Android mínimo 24, alvo 34.
- REA 6.4.0 e JADX headless MCP 0.7.1. JAR conferido contra o SHA-256 conhecido pelo pacote REA: `6e5eacf500b64292bfb73c49797c1958f6ee44646e43e868039ae7feb573ff75`.
- Evidências completas, APK, layouts decodificados, scripts de inspeção e capturas ficam exclusivamente em `build/reference/alight-motion-5.0.280/`. `evidence-index.json` liga arquivos a IDs de evidência.
- AAPT2 complementa o REA para dimensões e layouts binários. Os 17 layouts selecionados nessa versão são byte a byte iguais aos respectivos layouts da inspeção anterior 5.0.272; isso não estabelece equivalência de código, execução ou pixels entre versões.
- O nome do arquivo indica distribuição modificada. Não foi estabelecida autenticidade do APK nem validada sua assinatura. Observação de execução usa um emulador com dados novos e rede desligada.

Nenhum código decompilado, shader, ícone, imagem ou outro asset do aplicativo foi incorporado ao produto. A decompilação serve para descrever comportamento; a implementação usa o motor e os componentes próprios do AUREA.

## Evidência e tradução em comportamento

| Sistema | Observação da referência | Implementação AUREA |
| --- | --- | --- |
| Timeline | `TimelineLayoutManager` usa recursos para régua, cabeçote, escala e alças. Recursos: topo 42dp, linha 28dp, cabeçalho expandido 70dp, zoom inicial 100dp/s, alça 30dp, margem de autoscroll 38dp e velocidade 120dp/s. Corpo com margens verticais 2,5dp e raio especificado de 1,5dp. | Tokens correspondentes Android/Swift; olho e miniatura no cabeçalho, régua e relógio discretos, mantendo scrub central, pinça, reordenação e edição existentes. |
| Keyframes | `KeyframeView` posiciona cada marcador com a mesma transformação temporal; distingue propriedade ativa/alternativa e marcador destacado. Curvas são um editor separado com controle de overshoot e callbacks de gesto. | Mantidos losangos, edição, cópia/cola e curvas próprios. Consulta compartilhada determina keyframe no instante, anterior/próximo e trecho local da curva, com conversão para tempo de composição e limites visíveis do clipe. |
| Grupo de máscara | Layout de seleção apresenta ações distintas de agrupar, máscara e exclusão. `ElementGroupingKt.groupElements` recebe o modo de blend e usa o último elemento selecionado como fonte de máscara. | Comando C++ atômico seleciona a camada visual mais alta na ordem real da composição; cria grupo com blend Mask/Exclude na fonte e preserva conteúdo, ordem e tempos. Um único undo desfaz a operação. |
| Composição da máscara | O modo de máscara/exclusão pertence à composição de camadas, sem precisar de uma relação matte individual por alvo. | Reutilizado compositor AUREA: recorte aplicado uma vez ao composto abaixo da fonte. Teste com conteúdo sobreposto e máscara de alpha 0,5 diferencia esse comportamento de mascarar cada camada isoladamente. |
| Relações matte ao agrupar | Agrupar cria identidades internas e uma nova composição. | Correção própria do AUREA: remapear fontes matte internas para os novos handles; referências externas que se tornam inválidas são removidas. |

Evidências REA principais: manifesto `ev_8d05436844cd2556ce076aa3e3e5d2096f98ae7150e33f1834ae2ddf46513eb3`; classes de timeline `ev_30f645e8fa8ec924de41c7e40ba68a35881ce3d5223a1a98295dde7af49a358f`; keyframes `ev_a1d16acff08899ab093a9f684e3c7df6ba33cff27bc944ec10a6b764b61a8e88`; inicialização da timeline `ev_728417b81ecb253c185ec432d15a1be4c1807c83adb5c3f10f6d9c8bfc7ed9ee`; agrupamento `ev_f9f4452d97597dc0cf74f60188ca397984dbf383c741d5af620e7ba25f7f030d`.

Limites: nomes obfuscados, relações recuperadas e decompilação são evidência estática derivada. Não comprovam todos os gestos em execução. Máscaras de caminhos, alpha/luma e demais ferramentas próprias já existentes no AUREA são preservadas; não se atribui ao Alight Motion um recurso apenas por existir no AUREA.

## Correção da borda indicada pelo usuário

O tempo da barra é sempre `[x(start), x(end)]`. A alça inicial fica antes de `x(start)`; a final, depois de `x(end)`. Losangos usam a transformação temporal do motor, inclusive o deslocamento de recorte. Nenhum deslocamento artificial de um losango é usado para disfarçar a diferença. A largura mínima de aparência não pode deslocar o fim temporal de um clipe curto.

Critérios de regressão: keyframe no início e no fim da barra; keyframe zero oculto quando o início local foi recortado; mesmo mapa de desenho e toque nos modos geral, trilhas e compacto; alças externas; clipes curtos; instante final inclusive para edição de keyframes. Em aglomerações, as fronteiras são desenhadas primeiro e o keyframe selecionado/arrastado por último, sem deslocar o tempo. Esse ajuste corrige uma sobreposição detectada pelo teste de pixels Android em execução.

## Estado de implementação, compilação e execução

| Plataforma | Implementação | Compilação | Validação nativa |
| --- | --- | --- | --- |
| Motor compartilhado | APIs de grupo e navegação, remap de matte ao agrupar/desagrupar, recusa de desagrupar quando o recorte afetaria conteúdo externo. | Release MSVC aprovado; 8 testes novos, 239 verificações, mais 8 regressões distintas aprovadas. | GPU Vulkan real: máscara alpha 128, exclusão alpha 127, conteúdo sobreposto recortado uma vez. Logs em `build/validation/alight-editor-20261013/`. |
| Android/Kotlin/JNI | Timeline, ações de grupo, controles de keyframe e ponte para o motor. | Kotlin/JNI x86_64 e release ARM64 aprovados; 167 testes de timeline/painéis aprovados. | 9 testes nativos distintos aprovados no emulador API31 x86_64. Nenhum aparelho físico ARM64 conectado; o release ARM64 foi compilado, sem execução em telefone. |
| iOS/Swift/Objective-C++ | Mesmos controles, tokens e regras, incluindo a prioridade visual de keyframe selecionado, com ponte para o mesmo motor. | 13 contratos estáticos, APIs, projeto, textos, símbolos e tipos auditados. Compilação Xcode depende de Mac. | Não validado em aparelho ou simulador iOS neste host Windows. Casos Swift extraídos não executados sem `swiftc`. Detalhes em `docs/ios-reference-editor-validation.md`. |

A comparação visual usa capturas explicitamente identificadas pelo aplicativo e pela versão; capturas antigas do AUREA em pastas de referência não são tratadas como screenshots do Alight Motion. Igualdade de dimensões e testes do motor não estabelecem equivalência visual integral ou execução nativa das duas interfaces.

### Android: evidência de execução e entrega

O teste usa `EditorScreen`, `EditorStore`, JNI e motor reais, no pacote isolado `com.aurea.aurea.uitest`. Oito testes passaram em `build/reference/alight-motion-5.0.280/aurea-validation/device-tests-final.log`. O teste do editor de referência foi repetido separadamente, após corrigir sua seleção de camada superior: `query_layers` publica frente para fundo e `zIndex == 0` é a camada de cima. A correção afetou somente a fixture do teste; o produto instalado permaneceu o mesmo. Resultado final: `OK (1 test)`, 10,717s, em `device-tests-reference-final.log`. Isso totaliza nove testes distintos aprovados.

As primeiras execuções detectaram uma falha de captura do emulador sem janela e uma sobreposição do keyframe selecionado por fronteiras. A captura passou a usar `screencap` nativo quando `UiAutomation.takeScreenshot` retorna nulo. A prioridade de pintura foi corrigida nas duas plataformas; o teste de pixels passou. A execução final também confirmou os cliques nos keyframes inicial/final, a navegação compartilhada, as ações Mask/Exclude (blends 22/23) pelo menu e o undo atômico.

Capturas nativas inspecionadas, sem montagem: `aurea-validation/screenshots/ui-reference-first-key.png`, `ui-reference-last-key.png`, `ui-reference-mask-group.png` e `ui-reference-exclude-group-contents.png`. `screenshot-index.json` registra pacote, serial e SHA-256 dos sete PNGs. O losango inicial coincide com a borda esquerda da faixa colorida; a tampa branca termina nesse ponto. O losango final coincide com a borda direita. REA também registrou pacote instalado, janela e uma captura posterior ao teste; essa captura posterior não substitui as capturas tomadas durante os testes.

APK de entrega: `build/validation/alight-editor-20261013/AUREA-timeline-keyframes-masks-arm64.apk`, 75.814.751 bytes, pacote `com.aurea.aurea`, versão `0.1.4 BETA 2`, código 2175, somente `arm64-v8a`. SHA-256: `ECB19A9F445EA741478780D951A6103293AFD20F1C2A181AD9AE682C236C5C1B`. REA inventariou esse artefato em `rea-aurea-release-artifact.json`; AAPT2 complementou os metadados do pacote. A verificação Android da assinatura aprovou os esquemas v2/v3, com um signatário; isso verifica integridade da assinatura do build, sem atestar uma identidade externa. Log de compilação: `build/reference/android-editor-arm64-final.log`.

O `Engine.cpp` final usado na entrega possui SHA-256 `3459F40CE3A6E7951206B6648DFBC0D66EAA7194F60E62C17858AF7B2EEDC8D3`, correspondente ao código compilado e testado no host e nos dois builds Android.

## Observação do aplicativo de referência

REA registrou identidade do emulador, lançamento, captura e janela focada. A tela inicial modificada exigiu aceitar os termos; o usuário autorizou explicitamente clicar em Get Started. O aplicativo abriu Home e o menu Settings respondeu. Botões Compose de novo projeto/Projects não alteraram a tela nesta combinação do APK modificado, emulador API31 e rede desligada, inclusive após nova tentativa com entrada diferente. A causa não foi estabelecida. Não se atribui esse comportamento ao Alight Motion oficial e não se declara observação ao vivo do seu editor. A especificação da timeline/curvas/grupo permanece apoiada na evidência estática descrita acima.
