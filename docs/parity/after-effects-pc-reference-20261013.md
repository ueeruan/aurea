# After Effects como referência para AUREA PC beta 0.0.1

Análise com a skill `reverse-engineer-anything` e inventário PE do REA em 13/10/2026. Este relatório projeta quatro layouts publicados no DLL original e separa suas observações estáticas da interpretação espacial. Capturas Windows complementares da investigação principal devem ser avaliadas separadamente.

## Artefato e evidência

- Origem: `C:\Program Files\Adobe\Adobe After Effects 2026\Support Files\AfterFXLib.dll`.
- Tamanho: 49.345.032 bytes; formato PE32+, machine 34404 (x64).
- Versão de arquivo 26.5.0.89, produto 26.5.0, conforme metadados Windows complementares do DLL (`Adobe DVA 2026`).
- SHA-256 integral: `7b178d77982db943511a5f5f662dc73e8c6253e1c20cf7a5f09b5aaa73994551`.
- REA `inspect_pe_resources`, `max_file_bytes=67108864`, `max_entries=16384`.
- Evidence ID: `ev_1eb2b91893ebc4338433cb0a4ef0a6021a526a8c39053338ae0dfd7b56247ab6`.
- Cobertura: 3.015 recursos e 6.041 entradas examinadas, árvore completa dentro do orçamento.

Resposta integral: `build/reference/adobe-pc-20261013/after-effects/rea-lib-resources-complete.json`. O REA retorna tipo, nome, linguagem, intervalos de arquivo e SHA-256 dos payloads. Payloads XML são opacos para esse leitor; sua interpretação abaixo foi feita por um parser complementar.

Materialização restrita a quatro recursos de tipo nomeado `XML`, linguagem ID 1033. O script `extract-workspace-payloads.py` verifica tamanho e SHA-256 integral do DLL antes de cortar os intervalos exatos fornecidos pelo REA, verifica cada SHA-256 de payload e escreve somente os quatro XMLs em `build/reference/`. `workspace-extraction-manifest.json` preserva os registros originais de recursos e essa procedência. `parse-workspace-payloads.py` verifica novamente os payloads e registra a projeção em `workspace-projection.json`.

| Recurso | Índice REA | Offset original | Bytes | SHA-256 do payload |
| --- | ---: | ---: | ---: | --- |
| WS_ANIMATION | 2998 | 37341744 | 57438 | `0c88cd92be29d1b5b68782d4d26beb6cc12b7dbe037371d8ce022c5e82b33594` |
| WS_DEFAULT | 3000 | 37458392 | 70595 | `41383fc25d996b4b5d127d520a19d1f51b8212be144cecff7d7f009de6e8b0b5` |
| WS_SMALLSCREEN | 3009 | 37997512 | 70600 | `fb01bf8a7054789c09aab6a339276f11fc191f49f45a89e87fea0da71fe1ea18` |
| WS_STANDARD | 3010 | 38068112 | 40844 | `8055a88e39592a8bda0ec922b754b788f11be8f7b28bc0b3910b708837ff5305` |

O DLL não foi alterado nem duplicado integralmente. Nenhum ícone, fonte, shader ou código Adobe foi materializado para uso no produto.

## Observado no layout Default

O recurso contém `DVA_Wrkspce=1.2`, árvore de `Splitter`, valores booleanos `Orient`, proporções `Place`, flags `Sub1Vis`/`Sub2Vis`, `Frame`/`FrameProxy` e IDs de abas. Os títulos traduzidos e a interpretação espacial são decisões derivadas desses IDs e da hierarquia, não prova de execução.

| Região interpretada | IDs de abas na ordem publicada | Estado do recurso |
| --- | --- | --- |
| Alto, esquerdo | AE Project; AE Effect Controls | Frame, CurrTab 0 |
| Alto, centro | AE Composition | Frame, CurrTab 0 |
| Baixo, área principal | AE Timeline | Frame, CurrTab 0 |
| Faixa direita, topo | AEGP_TimePal | Frame, CurrTab 0 |
| Faixa direita, restante | AEGP_PropertiesPal; AEGP_Align; AEGP_AudioPal; AE Effects Palette | Frame, CurrTab 0 |
| Ramos ocultos | AE Footage; AE Layer | FrameProxy, ramos sem visibilidade |
| Outro ramo oculto | AE Lumetri Scopes; AEGP_Capsules | FrameProxy, ramo sem visibilidade |

Interpretando `Orient=true` como esquerda/direita e `false` como superior/inferior, a faixa direita ocupa 17,7222% da largura (`Place=0.82277775`). Dentro dessa faixa, TimePal recebe aproximadamente 5,2533% da altura; as abas restantes ocupam o outro ramo. O significado visual de TimePal como painel de prévia/tempo precisa ser corroborado na interface nativa.

A área principal à esquerda ocupa 82,2778% da largura; nela, o topo recebe 68,3865% da altura (`Place=0.68386489`) e a timeline 31,6135%. No topo principal, Project/Effect Controls recebe 21,8096% da largura e Composition o restante. Assim, as larguras interpretadas em relação à janela são aproximadamente 17,94% para Projeto, 64,33% para Composição e 17,72% para a faixa direita. Ramos ocultos não são tratados como ocupantes visíveis da área principal. Um ramo externo `Place=0.5` tem `Sub2Vis=false` e não implica dividir a janela atual pela metade.

## Outros layouts publicados

**Small Screen:** a árvore de painéis e divisores projetada coincide com Default. Os payloads completos têm hashes e tamanhos distintos, portanto isso não significa que todos os estados gravados ou bytes dos dois recursos sejam iguais.

**Standard:** a divisão superior/inferior interpretada usa `Place=0.67427385`: topo 67,4274% e timeline 32,5726%. O topo possui Projeto/Controles de efeitos à esquerda, Composição no centro e uma faixa direita. Proporções combinadas aproximadas: 12,62%, 75,55% e 11,84% da largura. A direita superior tem abas `AEGP_Info`, `AEGP_TimePal`, `AEGP_Align`, `AEGP_AudioPal`; a inferior tem `AEGP_PropertiesPal`, `AE Effects Palette`, `AE CEP Extension-com.adobe.DesignLibraries.angular`. O recurso grava `AE Timeline` com `CurrTab=4`; não foi comprovada a interpretação desse índice para abas dinâmicas de composições.

**Animation:** o topo interpretado ocupa 63,9213% da altura e a timeline 36,0787%. No topo, as larguras combinadas são aproximadamente Projeto 8,41%, Composição 83,39%, faixa direita 8,20%. A faixa direita possui abas `AEGP_PropertiesPal`, `AEGP_Info`, `AEGP_TimePal`, `AE Effects Palette`, `AEGP_MotionSketch`, `AEGP_Wiggler`, `AEGP_Smoother`, `AEGP_AudioPal`. Os IDs e sua ordem estão comprovados no recurso; o comportamento de cada ferramenta não foi inspecionado.

## Aplicação no AUREA PC

Usar componentes próprios para Projeto/Mídia, Composição, Controles de efeitos/Propriedades e timeline de camadas. Permitir alternar uma organização de Edição inspirada no Premiere com uma organização de Motion inspirada no After Effects. Divisores e abas precisam de proporções adaptáveis, foco por teclado e nomes acessíveis. O AUREA beta não deve assumir que extensões instaladas pelo usuário pertencem ao workspace padrão da Adobe.

O painel **Neucurve**, visível na captura complementar da instalação do usuário, não está nos IDs dos quatro layouts padrão selecionados. Deve ser registrado como extensão/addon daquele ambiente; não entra na especificação básica do produto. Essa busca é limitada aos quatro recursos analisados e não demonstra ausência em toda a instalação ou em workspaces do usuário.

## Limites e cobertura

O inventário PE do REA é estático; não observa carregamento de janela, foco, arraste, interações, renderização ou fallback de linguagem. Não foi recuperado pseudocódigo de funções nativas e não se usou sessão MCP ativa compartilhada. O recurso permite especificar agrupamento, ordem e valores publicados dos divisores; não comprova cores, fontes, alturas de barra, aparência de keyframes, arraste de curvas, algoritmo de máscara ou dock por arraste.

| Pergunta | Resultado |
| --- | --- |
| Layouts Default, Standard, Animation e Small Screen publicados? | Respondida: payloads vinculados ao inventário REA e hashes verificados. |
| Hierarquia, abas e valores de divisores? | Valores observados; interpretação espacial derivada/inferida. |
| Interface nativa atual do usuário idêntica aos recursos padrão? | Não estabelecido pelos XMLs; complementação Windows separada, incluindo extensões. |
| Comportamento dos keyframes, curvas, máscaras e ferramentas? | Não analisado nesta extração; implementações próprias precisam de testes. |
| Uso de código ou assets Adobe no AUREA? | Nenhum: somente especificação de estrutura. |
