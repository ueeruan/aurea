# Diagnóstico: "imagem inapropriada" ao abrir Efeitos e pedido de vídeos de exemplo por efeito

Data: 2026-10-18. Branch `claude/release-2191`. Só leitura: nada foi editado, compilado ou executado em aparelho.
Legenda: **Observado** = lido no código/arquivo; **Inferido** = dedução do código, não executada; **Não comprovado** = precisa de teste no aparelho.

## 1. Resumo

- **Causa mais provável (alta confiança, por código + inspeção do arquivo): a foto de amostra das prévias de efeito.**
  `previa_efeitos.jpg` (320x320, 43 KB, idêntica em Android e iOS) mostra uma mulher de top transparente e decotado, em pose sugestiva.
  O motor desenha **todo cartão de efeito** em cima dela (lado "Antes"/"Depois"). Ela aparece no navegador "Adicionar efeito", na busca, na ficha do efeito e nas miniaturas de 40 dp da pilha do painel Efeitos. Por isso surge "toda vez" que se abre Efeitos e atrapalha escolher o efeito.
- Não é anúncio, nem conteúdo da comunidade, nem servidor: o painel Efeitos não faz nenhuma requisição de rede nem mostra imagem remota.
- Não existe, hoje, como desligar essas prévias nem trocar a foto sem novo build.
- Há uma lacuna separada e real no "feed" da Comunidade (aba da Home): avatares enviados por usuários, sem denunciar, bloquear ou ocultar. Não é Efeitos, mas é a segunda candidata se o usuário chamar a Comunidade de "feed".
- O arquivo não tem registro de origem/licença no repositório (busca em `docs/` e `android/app/src/main/assets/licenses/`; a fonte NotoSansJP tem `provenance-NotoSansJP.json`, a foto não). Origem e direito de imagem da pessoa: **não comprovados**.

## 2. Tarefa A: onde o Efeitos mostra imagem ou "feed"

### 2.1 A foto de amostra (causa provável)

| Item | Android | iOS |
|---|---|---|
| Arquivo | `android/app/src/main/assets/previa_efeitos.jpg` | `engine/platform/ios/app/previa_efeitos.jpg` (bytes idênticos, `cmp` igual) |
| Carregamento para o motor | `state/EditorStore.kt:1005-1019` (`ensureEffectPreviewPhoto`, chamado em `:523`) | `app/EffectPreviewStore.swift:106-124` (`ensureSource`) |
| Fila e cache | `effects/EffectPreview.kt:67-79` (`load`), WebP q90 em `cache/motor/previas/<versão>` (`:116`) | `EffectPreviewStore.swift:34` (`effect-previews/<CFBundleVersion>-compare-v2`), PNG |
| Cartão da grade "Adicionar efeito" | `effects/EffectPicker.kt:337-369` (`EffectPickCard`; usado em `:236` e `:521`) | `app/EffectsBrowser.swift:677-696` (`EffectPickerCard`), `:741-749` (`EffectBrowserPreview`) |
| Ficha do efeito (prévia 1,6:1) | `effects/EffectsCatalogView.kt:76-98` (aberta de `editor/panels/EffectsPanel.kt:714`) | `EffectsBrowser.swift:809` |
| Miniatura de 40 dp na pilha do painel | `editor/panels/EffectsPanel.kt:840-846` (usada em `:960`) | `app/EffectsView.swift:1411-1432` (usada em `:329`) |
| Porta de entrada | `EffectsPanel.kt:474` e `EffectsBrowser.kt:50` abrem `EffectAddSheet` | `EffectsBrowser.swift:428` |

Como a foto vira imagem do cartão (motor C++ compartilhado):
- `engine/src/render/Renderer.cpp:7915-7953`: se há foto, ela é subida, recortada para 320x200 e usada como entrada de cada efeito. O recorte é puxado "para o terço de cima (o rosto fica no card)" (`:7945`, fator 0,35). Em 320x320 isso mostra de cerca de 13% a 76% da altura: rosto, ombros e busto.
- `Renderer.cpp:7994` e `:8098`: modo `comparison`, shader `effects_preview_compare_frag`, divide o cartão em antes/depois da mesma foto.
- Foi introduzida no commit `1683db85` (2026-09-22, "Prévias de efeito sobre a foto do app"). Antes disso (commit `471f0047`), as prévias usavam a cartela procedural `engine/shaders/effects/preview_plate.frag`, sem foto de pessoa. A cartela continua no motor como fallback (`Renderer.cpp:7957-7959`).
- A ação "Limpar cache" (`home/SettingsTab.kt:198`, `AureaModel.swift:3560`) só apaga e regenera, com a mesma foto. Não há preferência para desligar prévias (`effects/EffectPrefs.kt` só guarda favoritos e recentes).

Outros consumidores da mesma foto (importam na hora de trocar):
- IA de profundidade da prévia: `engine/src/render/RendererDepth.cpp:258-262`. Sem foto, o efeito "Mapa de profundidade" devolve a entrada (before = after).
- Testes que dependem de "pessoa no centro, fundo escuro nos cantos": `engine/tests/test_ai.cpp:81-130` (região 96..160 x 96..192 contra cantos, exige `person > background * 1.3f`), `engine/tests/test_gpu.cpp:11889-11924,11977` (`depth_photo`), `android/app/src/androidTest/.../LocalAiDeviceTest.kt:140`.
- Números de erro do mapa de profundidade medidos nessa foto: `engine/assets/ai/README.md:76-90`.
- Verificadores do iOS que exigem a foto e a igualdade byte a byte com a do Android: `engine/platform/ios/verify/check_shared_assets.py:21`, `check_ipa.py:42-43`, `check_bundle.swift:51-54`. O `project.pbxproj` referencia o arquivo (`:91,206,335,502`).

Assimetria entre plataformas (Observado):
- Android: se o asset faltar, `runCatching` engole o erro e o motor usa a cartela procedural (`EditorStore.kt:1008-1019`).
- iOS: se a foto faltar, `load` devolve `nil` (`EffectPreviewStore.swift:90-91`, comentário "not silently replaced by another image"). Todo cartão cairia em "Prévia indisponível". Isso precisa mudar junto para existir fallback com paridade.
- Cache velho: Android apaga pastas de outras versões (`EffectPreview.kt:98-103`). No iOS só `clear()` apaga arquivos `.png` da pasta atual (`EffectPreviewStore.swift:63-74`); pastas de versões anteriores ficam órfãs com a foto antiga (Inferido).

### 2.2 Anúncios (terceiros): não aparecem em Efeitos

- Android usa LevelPlay (`android/app/src/main/res/values/ads_config.xml:21-24`, `ads_provider = levelplay`). App Open do AdMob está desligado (ID vazio, `ads_config.xml:10`; LevelPlay não tem App Open). Formatos usados: interstitial pós-exportação e rewarded do AI Video.
  - Interstitial: `state/Exporter.kt:485` (pré-carga) e `:521` (exibe com o vídeo já salvo); regras em `ads/AdsFrequencyController.kt` (`exportBlock`).
  - Rewarded: `ai/AureaAi.kt:103-120`.
  - Ligação ao ciclo do app: `MainActivity.kt:181-222` (`onAppLoaded`, `onForeground` só chamam App Open, que está sem ID).
- iOS: LevelPlay, mesmos formatos (`engine/platform/ios/app/Ads/README.md`; interstitial em `AureaModel.swift:4279,4291,4308`; rewarded em `Ai/AureaAiState.swift:429-444`). O AdMob direto está fora do alvo do Xcode.
- Nenhum `AdView`, banner, nativo ou `WebView` de anúncio existe em `EffectsPanel.kt`, `EffectsHuman.kt`, `EffectSections.kt`, `effects/*` nem nos equivalentes Swift (grep sem ocorrências).
- O criativo mostrado é de terceiros (rede mediada pelo LevelPlay/Unity Ads). Filtro de categoria e qualidade de anúncio ficam no painel do LevelPlay, fora do repositório: **não comprovado** daqui. Só dá para afirmar que ele não é disparado ao abrir Efeitos.

### 2.3 Comunidade ("feed" da Home): conteúdo de usuário, sem moderação

Não está dentro de Efeitos (é a aba Comunidade, `home/HomeScreen.kt:78-79`, `HomeView.swift:309-310,542`), mas é o único "feed" do app:
- Conteúdo: posts (texto, anexo `.json` de preset ou `.aureaproj`) e **avatares** enviados por usuários (`community/CommunityApi.kt:101-114` baixa o avatar; `community/CommunityScreen.kt:228-232` desenha; iOS `CommunityView.swift:175-176`, `AsyncImage`). Só avatares são imagem; o resto é texto/arquivo.
- Servidor: `discovery/community.js:15-37` (`avatarMime`) valida só formato e dimensões; `:82-91` serve o avatar com `content-disposition: inline`. Não há análise de conteúdo.
- Moderação: **não existe**. Grep por report/denunciar/block/bloquear/moderar/hide/ocultar em `community/*.kt` e `CommunityView.swift` não retorna nada; `discovery/migrations/0004_comunidade.sql` não tem tabela de denúncias, bloqueios nem coluna de estado oculto; `community.js:55-76` (`posts()`) lista tudo. Só o dono apaga o próprio post (`CommunityScreen.kt:208-210`). Existe o canal genérico "Relatar um problema" (`discovery/relato.js`, `/api/report`, menu da Home), que não é por conteúdo.
- Pode ser desligado? Não. A aba é fixa no dock (`HomeScreen.kt:216-217`).
- Texto de política (Google Play "User Generated Content", App Store 1.2): é conhecimento geral, não lido do repositório. Ambos pedem, em essência, denúncia no app, bloqueio de usuário e remoção. Confirmar o texto vigente antes de citar.

### 2.4 Outros pontos de rede checados e descartados

`home/LiveNotices.kt:102` (mural de avisos: só texto e link), `home/DonationCard.kt` (PayPal, tela de exportação e Ajustes), `captions/CaptionPresetStore.kt:22` e `editor/panels/CaptionPresetBrowser.kt` (presets de legenda: autor, curtidas, downloads, só texto; usado só em `CaptionsPanel.kt:97`). Nenhum mostra imagem remota; nenhum está no painel Efeitos. O app não usa Coil nem `AsyncImage` no Android. No iOS, o único `AsyncImage` é o avatar da Comunidade.

### 2.5 Veredito

| Hipótese | Evidência | Confiança |
|---|---|---|
| H1: foto de amostra das prévias aparece em todos os cartões de Efeitos | Código acima + imagem inspecionada | Alta (falta confirmar em aparelho) |
| H2: avatar de usuário na Comunidade | Existe, sem moderação; não está em Efeitos | Baixa para a queixa; real como risco de loja |
| H3: anúncio | Só pós-exportação/rewarded; nada em Efeitos | Muito baixa |

Dúvida de campo que só o usuário responde: "log in Effects" provavelmente significa "abrir Efeitos". Se a imagem aparecer em outra tela (Home/Comunidade), vale pedir uma captura.

## 3. Tarefa B: vídeos de exemplo por efeito

### 3.1 O que existe hoje

- **Nenhum asset por efeito.** Não há GIF, vídeo nem miniatura embutida por efeito. Só: 1 foto compartilhada (43 KB), 1 cartela procedural (shader, 0 byte) e renderização em GPU sob demanda (`Engine::render_effect_preview`, `Engine.cpp:11647-11675`; uma por vez, fila própria, cartão fora da tela é cancelado).
- Catálogo: cerca de 220 efeitos (220 chaves únicas em `effects/EffectI18nTable.kt`; 219 declarações `EffectClass::` em `engine/src/effects/builtin/*.cpp`; contagem aproximada por grep).
- Cobertura de prévia estática (Observado no código):
  - ~200 efeitos visuais: prévia antes/depois de 1 quadro (320x200; 160 de lado máximo em aparelho de classe baixa, `DeviceCapabilities.cpp:635`).
  - 8 Temporais + 1 Global: sem prévia de um quadro (`Renderer.cpp:7869-7872`), cartão cai em "prévia na linha do tempo" (`EffectPicker.kt:379`).
  - 3 ferramentas (Legendas, Rastreio de câmera, Máscara): cartela com ícone (`EffectPicker.kt:361-363`).
  - 12 efeitos de áudio (classe `PerPixel`, `AudioPackEffects.cpp`): provavelmente mostram a foto inalterada. **Inferido, não executado.**
- Cache: memória LRU 16 MB (8 MB em aparelho baixo, `EffectPreview.kt:125`), disco WebP q90 por versão do app.
- Não há movimento: o `comparison` fixa `localTime = 18` e `clipProgress = 0,5` (`Renderer.cpp:7994`).
- Falha silenciosa: com o preview ao vivo do editor ativo, o motor devolve `Busy` (`Engine.cpp:11651,11667`); `EffectPreviewStore.load` devolve `null` sem repetir, e o cartão fica em "Prévia indisponível" até recompor. **Inferido do código**; relevante para "funciona em toda situação".
- Cobertura de teste: só alguns efeitos têm prévia testada (`test_gpu.cpp:8088-8138`).

### 3.2 Plano proposto (leve, local, sem asset de terceiros)

**Recomendação: gerar localmente pelo motor, com cenas-fonte nossas e animação só onde o usuário olha.**

1. **Motor C++ compartilhado** (regra de paridade: regra no motor, UIs só controlam):
   - Nova `Engine::render_effect_preview_sequence(typeId, w, h, frames, out)` reaproveitando `Renderer::render_effect_preview` num laço de N quadros (sugestão: 12 a 16 a 160x100, ~1 s em laço), variando `localTime`, `clipProgress` e um varrimento do parâmetro principal.
   - Declaração opcional por efeito (`demo_sweep`) para o que a prévia animada deve mexer. Para os 8 Temporais, uma fonte que se desloca por quadro dá histórico real. Isso também cobre a lacuna de "sem prévia".
   - Pontes: JNI (`aurea_jni.cpp:999`) e ObjC (`AureaEngine.mm:3976`).
2. **"Vários exemplos" = várias cenas-fonte** (cobre "toda situação"): 3 a 4 cenas neutras próprias (cartela atual; cena colorida de formas; cena de texto/alto contraste; cena escura de baixa luz). Preferência: procedurais em shader (0 byte); se precisar de imagem, WebP de até ~25 KB cada, criada por nós, com registro de origem em `assets/licenses/` (como `provenance-NotoSansJP.json`).
   - Sem decodificador de vídeo: quadros renderizados, não MP4. Não decodificar vídeo por cartão.
3. **UI (Kotlin e Swift no mesmo trabalho)**: grade continua com o quadro estático. Anima só o cartão tocado/segurado e a ficha do efeito (`EffectsCatalogView.kt:76-98`, `EffectsBrowser.swift:809`), com seletor de cena. Respeitar "reduzir movimento" do sistema e o novo ajuste "Prévias de efeitos" (item 4 do plano C). Textos e acessibilidade nos dois (`strings.xml` e `AureaStrings.swift`).
4. **Cache**: tira de quadros em WebP lossy na mesma pasta versionada, gerada só ao abrir a ficha, com teto de disco (sugestão 24 MB, LRU). Estimativa **não medida**: 12 quadros x ~6 KB ≈ 70 KB por efeito; ~15 MB se os ~220 forem abertos.
5. **Todas as situações**: manter o teto de lado por classe de aparelho e a regra de não rebaixar pelo calor; repetir com recuo quando vier `Busy`; cancelar ao sair da tela; falha cai na cartela, nunca em buraco. Teste de motor que percorre **todo** o registro e verifica que cada prévia sai com quadros não vazios e que os animados diferem entre si.

Comparação de opções (estimativas, não medidas):

| Opção | Impacto no app | Observação |
|---|---|---|
| Gerar no motor (recomendada) | +0 a ~0,1 MB; GPU sob demanda; cache ≤ ~24 MB | Funciona offline, igual nas duas plataformas, sem moderação |
| Embutir MP4 por efeito | ~220 x 150-300 KB = ~33-66 MB | Pesa no APK/IPA; decodificação por cartão; difícil manter em dia com efeitos novos |
| Baixar do servidor | +0 no app; precisa hospedar ~220 arquivos | Exige rede, hospedagem e curadoria; reabre o risco de conteúdo impróprio |

**Regras do projeto (AGENTS.md):** nada extraído de outro aplicativo pode entrar (nem imagem, nem vídeo, nem shader); referências externas só especificam comportamento; APKs extraídos e resultados brutos ficam só em `build/reference/`. As cenas-fonte têm de ser criadas por nós e ter registro de origem. Registrar separadamente implementação, compilação e validação em aparelho nas duas plataformas.

## 4. Tarefa C: plano de correção ranqueado (A), com paridade Android/iOS

**P0: trocar a foto por imagem neutra própria (hotfix, horas).**
- Criar nova `previa_efeitos.jpg`, original nossa, sem pessoas, com assunto central claro e fundo escuro nos cantos (mantém os testes de profundidade) e com registro de origem. Substituir **os dois** arquivos, byte a byte iguais (`check_shared_assets.py:21`, `check_ipa.py:42-43`).
- Subir a chave de cache `-compare-v2` para `-compare-v3`: `state/EditorStore.kt:977` e `EffectPreviewStore.swift:34`. No iOS, apagar pastas irmãs de versões antigas ao iniciar (hoje ficam órfãs com a foto antiga).
- Ajustar o recorte pensado em rosto (`Renderer.cpp:7942-7946`, fator 0,35; espelhado em `RendererDepth.cpp:262-264`) se a nova cena exigir; os dois precisam continuar iguais.
- Atualizar o que depende da foto: `test_ai.cpp:81-130`, `test_gpu.cpp:11889-11977`, `LocalAiDeviceTest.kt:140`, números de `engine/assets/ai/README.md:76-90` (reexecutar a conferência contra o onnxruntime).
- Alternativa mais rápida e ainda sem asset: remover a foto e usar a cartela procedural. Custo: o efeito "Mapa de profundidade" perde prévia (`RendererDepth.cpp:259-262`), e testes/verificadores precisam de outra fonte.

**P1: paridade do fallback (pequeno).** O iOS deve cair na cartela quando a foto faltar (`EffectPreviewStore.swift:90-91`, como o Android já faz em `EditorStore.kt:1008-1019`). Assim, uma foto ausente ou corrompida nunca zera todas as prévias.

**P2: ajuste do usuário "Prévias de efeitos" (ligado/desligado, pequeno).**
- Guardar em `EffectPrefs` (Android, `effects/EffectPrefs.kt`) e no equivalente iOS. Com a opção desligada, `rememberEffectPreviewState`/`EffectBrowserPreview` usam a cartela genérica e não pedem render.
- Adicionar em Ajustes, perto de "Limpar cache" (`home/SettingsTab.kt:198`; `AureaModel.clearHomeCaches`). Textos e acessibilidade nos dois idiomas/plataformas.

**P3: moderação da Comunidade (médio; risco de loja).**
- Servidor: migração `0006` com `community_reports`, `community_blocks` e estado oculto; `posts()` e `profile()` (`discovery/community.js:55-76`) filtram bloqueados e ocultos; endpoint de denúncia reaproveitando `relato.js`; fila de revisão para avatares.
- Android: ações "Denunciar" e "Bloquear" no cartão do post e no perfil (`community/CommunityScreen.kt` ~`:240-260`). iOS: `CommunityView.swift`, mesma regra.
- Opcional: avatar de quem não é seguido só carrega após toque.

**P4: anúncios (sem mudança de código para esta queixa).** Conferir no painel do LevelPlay os filtros de categoria/qualidade do criativo. Não tocar na frequência: o interstitial só roda após exportar (`Exporter.kt:521`).

**Depois do P0-P2:** iniciar o plano de prévias animadas da seção 3.2.

## 5. O que não foi feito ou não está comprovado

- Nada foi implementado, compilado nem validado em aparelho (Android ou iOS). A aparência do cartão com a foto vem da leitura do código e da inspeção do arquivo, não de captura de tela do app.
- Origem, licença e direito de imagem da foto: sem registro no repositório.
- Efeitos de áudio com prévia igual à entrada: inferido do código.
- Tamanhos de cache e de MP4 da seção 3: estimativas.
- Texto atual das políticas de Google Play e App Store: conhecimento geral, a confirmar.
- Criativos de anúncio e filtros do LevelPlay: fora do repositório.

## Correção registrada em 18/10/2026

O usuário confirmou que `previa_efeitos.jpg` é uma imagem gerada por IA, sem pessoa real.
Então não há questão de direito de imagem. O problema que sobra é o conteúdo: a pose e a roupa
incomodam parte dos usuários, e a foto aparece em todo cartão de efeito, sem como desligar.
A correção P0 continua valendo: trocar por uma imagem neutra (pode ser gerada por IA, com
assunto central e cantos escuros para os testes de profundidade) ou usar a cartela procedural.
