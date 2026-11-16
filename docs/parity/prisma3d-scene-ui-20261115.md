# Referência Prisma3D para a Cena 3D do Aurea

## Identidade e escopo

Análise em 15/11/2026 pelo REA CLI 6.5.0. O pacote Prisma3D 3.3.7 já estava em Downloads. Foi reutilizado o arquivo `Prisma3D+-+Animation,+Modeling_3.3.7_APKPure.xapk`, SHA256 `90e515e21db7b080913821d9ec2f6d083b3a460355e819cc6178726f93e22380`. Os arquivos de inspeção ficam em `build/reference/prisma3d-ui-20261115/`.

O manifesto e o REA identificam `com.prisma3D.prisma3D`, versão 3.3.7, código 200, Android mínimo 26 e alvo 36. Os APKs base e arm64 têm assinatura válida com certificado SHA256 `c12e7609e7b5c3f1c6ddd157ac4b991b42f77694de068a3068c57871ccab8cc3`. A aplicação usa Unity/IL2CPP. A análise Java cobre o invólucro Android. A observação em execução sustenta as conclusões sobre a interface Unity.

O site oficial `https://prisma3d.net/` aponta para o mesmo pacote na Play Store. A assinatura foi verificada localmente. Essa verificação confirma consistência entre os splits. A correspondência do certificado com uma distribuição obtida diretamente da Play Store permanece sem comparação independente.

## Execução observada

Instalado em um emulador separado, `AureaReference_API35`, Android 15, x86_64 com tradução arm64, tela 1080 × 2340 e densidade 440 dpi. O aplicativo foi iniciado pelo REA. Capturas de estados subsequentes também usam REA, com toques ADB como complemento.

| Estado | Observação direta |
|---|---|
| Cena vazia | Viewport ocupa a maior parte da tela. Início no alto à esquerda e exportar à direita. Lista de objetos recolhida em um botão superior direito. |
| Transporte | Desfazer e refazer à esquerda. Play central imediatamente acima da timeline. |
| Timeline | Faixa compacta inferior com régua, tempo atual e playhead branco central. Nome do objeto na margem esquerda. |
| Dock raiz | Linha horizontal de Modelos, Céu, Câmera, Luz e Mídia. Ícones sobre os nomes. |
| Objeto selecionado | Dock contextual com voltar fixo à esquerda, Mover, Girar, Escalar e Material. Rolagem revela outras ações. |
| Hierarquia aberta | Lista flutuante na região superior direita, com recolher, expandir objeto, ícone, nome e visibilidade. O viewport permanece no mesmo lugar. |
| Mover | Indicador compacto de valores X, Y e Z próximo da base do viewport. |
| Animação | Adicionar na faixa abre o seletor de animações. Criar produz um segmento selecionado de dois segundos e um botão de keyframe no transporte. |
| Mais ações | Rolagem do dock mantém voltar fixo e revela Renomear, Loop, Excluir e Exportar. |
| Renomear | O lápis junto do nome abre entrada de texto nativa. |

O emulador exibiu o fundo do viewport preto com eixos/gizmo vermelhos. A geometria do personagem não foi renderizada corretamente nessa configuração. Essa limitação impede conclusões sobre qualidade 3D, iluminação, precisão de seleção da malha ou desempenho do Prisma3D. Os controles e transições de interface listados foram observados.

A solicitação de rotação do emulador manteve a aplicação em retrato. O arquivo `screen-scene-landscape.json` documenta uma entrada de renomeação em retrato. Seu nome representa a intenção do teste e não uma interface landscape comprovada.

## Adaptação para o Aurea

Reconstruir a organização visual com viewport dominante, hierarquia recolhível e ferramentas inferiores contextuais. Reutilizar gestos, gizmos, seleção, transformações, partes, pivot, materiais, luzes, rig e keyframes do Aurea.

A altura de dock de aproximadamente 80 pontos, áreas de toque de pelo menos 44 pontos, timeline compacta expansível, controles adicionais em Mais e suporte landscape constituem decisões de adaptação. A organização também mantém acessíveis os recursos avançados do Aurea. As ações existentes de luz e ambiente definem o conteúdo disponível no dock.

Código, ícones, materiais, modelos e outros recursos extraídos da referência permanecem nos arquivos brutos de inspeção. A implementação do produto usa os componentes e o motor do Aurea.

## Registro REA

| Arquivo JSON | Evidence ID |
|---|---|
| `archive.json` | `ev_8a6fc4f7a47cfecf1508bc38df88df41c2855b1387d666d4c9689de682bb97ce` |
| Inventário interno | `ev_e46738c2ff0eaca8331106786c48630c72ab9b48e8b741f8e7e165443499e1ac` |
| `package.json` | `ev_1473a1c15280b6730077d876c51eb1470c3e7388f91d3d6207e72a08bd8eaa2a` |
| `launch.json` | `ev_e3c734c95b8636681c46323698fd460f79d9bd5ce69caa49126aaa737474b1c5` |
| `screen-editor-empty.json` | `ev_7f81c79b337d77b9f2aca41738ebde1360cdb96f48233c2da9dae6a118049c3a` |
| `screen-human-editor-2.json` | `ev_abe845cf2b5a1396de91a7bb88ebce2f1a1003ae88a3f1034b59cc072f423edc` |
| `screen-human-controls.json` | `ev_b37eee6f80086e64adbd981feec7449b7922816a04b1dcfc48f4a9da46d26f32` |
| `screen-object-list.json` | `ev_296b7f06a194f038c092bee52def27851aabe1b8fce3b36b9b90b655f84494bf` |
| `screen-move-panel.json` | `ev_d1807ab3eea9ff15bf5239eb09a7b34c6665e29dd579ca2f4dbecec87d770d5f` |
| `screen-keyframe-added.json` | `ev_b81565ecf95508d1cd4342856180b4bc1269923240531900a42566e50e850c5c` |
| `screen-animation-create.json` | `ev_5c91c3f927618b2ec0889a1a185dd98e7c921e7f18080f546628014b810b361b` |
| `screen-scene-landscape.json` | `ev_7d59f3c6ec03aed1fae9597998e88525e98410fe7a4d2bc8537088d6358908c7` |
| `screen-object-more.json` | `ev_6fedc4e5e8afacbacf3b5732099faa3ee940d598516028b2e6de504dabaef487` |

Os nomes de alguns arquivos de captura antecedem a identificação do estado exibido. As observações e a imagem de cada Evidence prevalecem sobre o nome do arquivo.

## Situação da implementação

Implementação, compilação e validação do Aurea serão registradas separadamente no relatório de entrega. As evidências acima descrevem o aplicativo de referência.
