# Premiere Pro como referência para AUREA PC beta 0.0.1

Análise estática com REA em 13/10/2026. Este relatório especifica a organização de painéis publicada na instalação local; não afirma que a interface do editor foi observada em execução.

## Alvo e procedência

- Executável original: `C:\Program Files\Adobe\Adobe Premiere Pro 2026\Adobe Premiere Pro.exe`.
- Tamanho: 780.886.536 bytes. Versão de arquivo 26.5.2.5, produto 26.5.2, conforme metadados Windows complementares.
- SHA-256: `b24e2b442a8e0350edbc0c214d2fcae2f68c7d9843ae399e5058d5b5a6786094`.
- Não foi duplicado, modificado ou executado nesta análise.
- REA: skill `reverse-engineer-anything`, CLI `inspect-pe-resources` e `inspect-artifact`.

Todos os resultados brutos, script de projeção e carrier ficam em `build/reference/adobe-pc-20261013/premiere/`. O carrier `selected-workspace-resources.zip` foi criado pelo analista com quatro XMLs selecionados da instalação. **Não é um pacote original Adobe.** O REA inventariou seus membros; o parser XML complementar confirmou que cada membro coincide byte a byte com o caminho original.

| XML original em `C:\Program Files\Adobe\Adobe Premiere Pro 2026\xml\` | Bytes | SHA-256 |
| --- | ---: | --- |
| EDITINGWORKSPACELAYOUT.xml | 689798 | `fc9c721ff4279a5e3bb53f0574be768075f4c128b5243757cecff8776094e87d` |
| EFFECTSWORKSPACELAYOUT.xml | 707163 | `48129fdcdae83ee53927267f106a26cd969480ebdc223bfe29e93e651934cfca` |
| ASSEMBLYWORKSPACELAYOUT.xml | 663726 | `2c48c753d546afe47a169cb9db6157a398da7e105aa14c6f1c36ca420c89887f` |
| PremierePro.xml | 126106 | `a6a91dd03d5a56622350ae4481e3fecff928e85953333fc3bcad475b4886e07b` |

O último XML é uma configuração EuCon de controles físicos, com comandos como `cmd.file.openproject`, `cmd.file.save`, `cmd.edit.undo`, `cmd.clip.insert` e `cmd.clip.overlay`. Não é um layout do editor nem prova sua execução.

## Evidência REA e limites

`workspace-artifact-rea.json` contém a resposta integral do REA, Evidence ID `ev_1ecb8acfe40b3f2891099ece3e6766bdcbddcafd458ee46c7313dd3d03900bc0`. O subpasso de inventário tem Evidence ID `ev_b6567d88da6bdc4861e323bdf18b08323d8b179d6dddbb55ad82dee07faaaa19` e Manifest ID `agm_7d0ce925e24e6df5de8a201948c321570a4279fd527f3440923c758872191e93`: cinco nós (carrier e quatro membros), hashes verificados, sem omissões. Carrier: 122.321 bytes, SHA-256 `b8a2e17604357bf3bca9ab4878d5ee0da04dcabc769e758329f9a434c2dd1568`. A inventariação de ZIP observa os membros do diretório central; não simula seu uso pelo Premiere. `workspace-projection.json` registra as árvores, abas, seleções e hashes projetados pelo parser complementar.

O leitor portátil de recursos PE do REA limita `max_file_bytes` a 536.870.912 bytes. A tentativa acima desse valor falhou na validação (`exe-pe-resources.json`); a tentativa com o máximo válido retornou `resource_constraint` porque o executável tem 780.886.536 bytes (`exe-pe-budget-limit.json`). Não houve inspeção parcial bem-sucedida do EXE.

`observe_native_ui` no REA deste ambiente exige alvo Mach-O e captura nativa macOS por PID/window ID. Isso não oferece captura do editor Windows. A investigação principal registrará separadamente qualquer captura Windows complementar; este relatório não a substitui. Ghidra/IDA não foram configurados para este alvo e nenhum pseudocódigo do executável foi recuperado.

## Organização publicada: Editing

Os IDs, ordem das abas, seleção `CurrTab = 0`, valores `Orient`/`Place` e flags de visibilidade abaixo foram observados nos XMLs. A interpretação de `Orient=true` como divisão esquerda/direita e `false` como divisão superior/inferior é uma inferência baseada nos grupos de IDs; requer corroborar o editor real.

| Região interpretada | IDs de abas na ordem publicada |
| --- | --- |
| Alto, esquerdo | Source Monitor; Effect Controls; AudioClipMixer; MetadataEditor |
| Alto, centro | Program Monitor |
| Alto, direito | Properties2; Search; dvauxpuiUXPPanel |
| Baixo, esquerdo | Project; Media Browser; Graphics; CsxsTabcom.adobe.DesignLibraries.angular; Info; Effects; MarkerList; History |
| Ao lado do projeto | Tools |
| Baixo, centro | Timeline |
| Baixo, extremo direito | MiniAudioMixer |

As abas `Project:` incluem um identificador de projeto gravado no recurso. O recurso contém `PrProductionFolder` em um `FrameProxy` com ramo oculto (`Sub1Vis=false`); portanto esse painel não entra na área principal visível desta interpretação. Um segundo ramo superior também está oculto.

A divisão principal publicada tem `Place=0.58216655`, interpretada como 58,22% para área superior e 41,78% para inferior. Dentro da parte superior, os divisores são `0.3923611` e `0.6257143`: após combinar os ramos, Source ocupa aproximadamente 39,24%, Program 38,02%, Properties 22,74%. Na parte inferior, o mixer reserva aproximadamente 5,78% do espaço; antes dele, Project/Tools ocupa 34,45% e Timeline 65,55%. Esses números são proporções de recurso, não medidas de pixels da janela atual do usuário.

## Outras áreas de trabalho

**Effects:** o grupo superior esquerdo inicia em Effect Controls, seguido de Scopes, Source Monitor e AudioClipMixer. Program Monitor permanece ao lado. Um ramo externo direito ocupa aproximadamente 22,51% da largura interpretada; nele, Effects/Graphics/Libraries/History fica acima de Properties2/Essential Sound/Color/UXP/MarkerList/Info. A divisão vertical desse ramo tem `Place=0.24509804`. A timeline e o mixer permanecem na parte inferior.

**Assembly:** Project/Media Browser/MarkerList/AudioClipMixer/UXP/Libraries fica em um ramo esquerdo de aproximadamente 39,58%; o ramo direito inclui Program Monitor/Source Monitor acima da timeline. Tools e MiniAudioMixer permanecem como faixas estreitas. Todas as primeiras abas são selecionadas no recurso.

## Especificação própria para o AUREA PC

Reproduzir a lógica de agrupamento e a hierarquia de painéis com componentes e ícones próprios: área de trabalho Edição, Projeto/Mídia, Fonte, Programa, Controles de efeitos/Propriedades, ferramentas e timeline com medidor de áudio. Permitir ajuste dos divisores e seleção de abas; oferecer área de trabalho Motion com propriedades animadas e curvas. As proporções publicadas servem de ponto de partida e devem adaptar-se à janela, densidade e acessibilidade do AUREA.

Os recursos analisados não comprovam cores, fontes, dimensões de controles, estados hover/focus, a geometria exata dos clipes, marcadores, keyframes, waveform, algoritmo de máscara, atalhos atuais de teclado ou comportamento de dock por arraste. Implementações desses elementos no beta são decisões próprias e precisam de validação funcional independente. Não foi incorporado código, shader, fonte, ícone ou asset Adobe ao produto.

## Perguntas e cobertura

| Pergunta | Estado |
| --- | --- |
| Quais painéis e abas são publicados para Editing, Effects e Assembly? | Respondida estaticamente pelos XMLs, vinculados ao inventário REA. |
| Qual a hierarquia e quais as proporções dos divisores? | Valores observados; interpretação espacial derivada/inferida, sem captura de execução neste relatório. |
| O executável expõe recursos que esclarecem a UI? | Não comprovado: limite do leitor PE menor que o arquivo. |
| A aparência e interação são idênticas ao Premiere Windows em execução? | Não comprovado aqui. |
| O AUREA usa código ou assets extraídos do Premiere? | Não: referência estrutural; implementação própria. |
