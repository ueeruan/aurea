# Interfaces mobile do Aurea

## Referências

Levantamento de 15/11/2026 pelo REA, com Prisma3D 3.3.7 e o APK CapCut 19.8.0 fornecido pelo usuário. Os registros de identidade, evidências, execução e limites estão em [Prisma3D](D:/Users/Ruan/Documents/Aureabeta/docs/parity/prisma3d-scene-ui-20261115.md) e [CapCut mobile](D:/Users/Ruan/Documents/Aureabeta/docs/parity/capcut-edit-ui-20261115.md).

## Edição

- Topo compacto com nome do projeto, resolução, exportação por ícone e ações do projeto.
- Identidade visual Aurea aplicada após a revisão do usuário, com tokens de tema do app, destaques azuis, preview arredondado, relógio em chip e dock em cápsula.
- Preview, transporte, timeline e dock contextual. Paisagem organiza preview e edição lado a lado.
- Playhead central fixo, miniaturas contínuas, ondas de áudio e importação na borda direita.
- Início e mute acompanham o conteúdo temporal. O mute escreve o estado dos clipes da sequência em um passo de desfazer.
- Faixas ocupadas e a faixa recém-criada aparecem na timeline. Ferramentas avançadas, alvo, bloqueio, mute e solo ficam acessíveis em Mais.
- Atalhos de áudio e texto usam os fluxos existentes de importação e criação.
- O dock volta ao primeiro item ao mudar de nível.

## Cena 3D

- Viewport dominante com hierarquia recolhível e seleção de objetos.
- Dock contextual de transformação, material, animação, partes e pivô.
- Campos XYZ escrevem os parâmetros existentes do motor.
- Timeline compacta com 72 pontos por segundo e playhead central. A versão expandida oferece chaves e gráfico.
- Pose, IK, esqueleto, AutoRig, mocap, materiais, luzes e navegação de câmera continuam acessíveis pelos painéis e menus correspondentes.
- Hierarquia em paisagem usa a altura disponível da viewport e oferece rolagem até os objetos.

## Barra superior da camada

3D e Motion Blur aparecem diretamente na barra superior, conforme a captura enviada pelo usuário em 15/11/2026. O nome usa largura flexível e reticências. Os controles expõem seu estado para acessibilidade.

3D alterna a flag da camada compatível. Motion Blur abre seu painel por toque e alterna por pressão longa ou ação de acessibilidade. Os testes verificam as flags do motor e desfazer.

## Implementação e validação

As interfaces Android/Kotlin e iOS/Swift controlam os comandos existentes do motor C++. A geometria de pintura e toque usa a mesma transformação em cada timeline.

| Plataforma | Implementação | Compilação | Execução |
|---|---|---|---|
| Android | Edição, Cena 3D e barra superior implementadas | APKs x86_64 e arm64 compilados, assinatura e alinhamento 16 KB verificados | Emulador Android 15 validado em retrato, paisagem e fonte ampliada |
| iOS | Edição, Cena 3D e barra superior implementadas | Pendente em macOS/Xcode | Pendente em iOS |

A auditoria iOS passou em 26 contratos de interface, textos, símbolos da ponte, geometria, arrasto 3D e Motion Blur. A execução dos casos Swift depende de um host com Swift/Xcode.

Os resultados de compilação, testes, capturas e hashes ficam em `build/validation/mobile-ui-20261115/`. Os APKs de teste usam a versão existente `1.0.0-alpha`, build 2220, e ficam em `build/test-mobile-ui/`.

## Cobertura e histórico de correções

- Nove regressões JVM de timeline passaram.
- Quinze testes nativos passaram em retrato, cobrindo edição, grupos, criação de faixas, ondas, mute, correção de cor, cena, gestos, luz e controles superiores.
- A primeira execução encontrou tags antigas e estado de acessibilidade ausente no controle de sombras. Os controles de propriedades passaram a expor o estado de switch.
- A inspeção visual encontrou rolagem herdada entre níveis do dock. A troca de nível agora reposiciona o dock.
- As primeiras capturas nomeadas como paisagem mostravam edição e cena em retrato. O teste de cor usava paisagem efetiva. O harness passou a solicitar a orientação por atividade e verificar as dimensões das capturas.
- A execução efetiva em paisagem encontrou altura insuficiente na hierarquia. O painel recebeu altura responsiva e os testes passaram a rolar até o objeto antes de tocá-lo.

- A rodada final anterior à revisão visual passou em cinco testes efetivamente em paisagem e quatro com fonte a 200%.
- Após a identidade Aurea, passaram nove testes nativos de edição em retrato, três em paisagem com fonte a 130% e dois com fonte a 200%. Os nove testes JVM passaram novamente.
- O primeiro filtro JVM da rodada visual usou um nome de pacote incorreto e encontrou zero testes. O filtro foi corrigido para `com.aurea.aurea.editor.Bugs036EditorTest`, com nove casos executados e zero falhas.
- O APK final x86_64 com identidade Aurea foi instalado e iniciado no emulador visível, preservando os dados existentes.

A compilação e a execução nativa iOS continuam pendentes em macOS/Xcode. As verificações estáticas passaram em 26 contratos de interface, além das verificações de Motion Blur e arrasto 3D.
