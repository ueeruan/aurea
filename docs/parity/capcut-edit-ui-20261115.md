# Referência CapCut mobile para Edição do Aurea

## Escopo e procedência

Investigação estática e observação em execução de 15/11/2026 usando REA 6.5.0, provedor JADX headless 0.7.1 e provedor ADB. Alvo fornecido pelo usuário em `C:/Users/Ruan/Downloads/CapCut v19.8.0 (Pro).apk`, pacote `com.lemon.lvoverseas`, versão 19.8.0, código 19600200, mínimo Android 23, alvo 36. O nome Pro identifica o arquivo recebido. A autenticidade da distribuição e a equivalência com uma versão oficial permanecem sem comprovação.

SHA256 do original `1dfe4135180d9c6803c821c4956baf384f57d6f4ee4f699983c9449d73c92a8f`. `build/reference/capcut-ui-20261115/target-identity.json` comprova igualdade entre original e derivado existente para manifesto, recursos, todos os 40 DEX e os nove XML inspecionados. O derivado permite análise estática. A divisão por DEX reduz o custo do REA e limita a contagem de classes ao respectivo fragmento.

O agente principal instalou com sucesso o APK original completo em um emulador de referência novo e isolado, `emulator-5556`, e iniciou o aplicativo pelo REA. A observação usou uma imagem local de `engine/tests/forma_00`, importada como clipe de três segundos, com trecho final que levou a sequência a cinco segundos. O fluxo ocorreu sem conta. Nenhum recurso extraído integra o produto.

O pacote apresentou divulgação LITEAPKS/Telegram e o botão `Export Draft Zip`. Esses elementos pertencem à distribuição recebida e sua procedência oficial permanece desconhecida. Eles foram excluídos da especificação de adaptação do Aurea.

## Estrutura observada

| Área | Evidência e geometria |
|---|---|
| Topo | `EditTopBarNitaView` resolve `0x7f0c0524`. XML com botões 40 × 50 dp e margem inicial de 16 dp. Há ações adicionais ocultas por padrão. Identificação visual de cada ícone depende da ligação funcional. |
| Preview | `EditPreviewNitaView` resolve `0x7f0c0522`. `SurfaceView` ocupa o host, com `PreviewOperationLayout` sobreposto e indicadores condicionais de processamento. |
| Transporte | `EditPlayerControlFragment.t4` escolhe cinco variantes por configuração. Variante padrão `0x7f0c10f9`, `res/_/lwe.xml`, tem altura 40 dp, fundo `#181818`, tempo atual e total à esquerda, play 30 × 30 dp centralizado. À direita ficam linkage, keyframe, previous, next e fullscreen, com caixas de 30 × 30 dp. |
| Timeline | `EditTrackFragment` escolhe `0x7f0c0525` ou `0x7f0c0526` por configuração. `HorizontalScrollContainer` contém régua 20 dp, `FrameScroller`, trilhas visuais, áudio e grupo multitrilha. Fundo `#1c1c1c`. |
| Mute e capa | Dentro do conteúdo do `FrameScroller` ficam mute com largura 32 dp, capa de 50 × 50 dp e `MultiTrackLayout` em sequência. Capa usa 42 dp em sw320. O método de inicialização nomeia explicitamente `tvMute`, `tvCoverEdit` e `ivCoverLayout`. |
| Adicionar | `ivAdd` 36 × 36 dp é irmão do `FrameScroller`, alinhado ao seu fim e centro vertical, com margem final 8 dp. Essa hierarquia mantém o botão independente do deslocamento do conteúdo da faixa. |
| Indicador temporal | `ivPlayHead` é irmão das faixas, alinhado ao centro horizontal do pai e com altura declarada de 148 dp. |
| Dock inferior | `DockGroupView` cria linha horizontal por nível. Nível secundário inclui voltar de 32 × 58 dp com margem inicial de 10 dp. A altura final do dock depende de estado e constante do aplicativo. |

## Ferramentas observadas

O provedor raiz declara Editar, Áudio, Texto, Stickers, Camada, Efeitos, Legendas, Filtros, Ajustar, Proporção, Plano de fundo e Modelos. Também existem categorias de serviços e IA. A ordem de declaração constitui referência parcial. A árvore de componentes determina a ordem efetiva em execução.

O provedor de vídeo principal oferece Dividir, Velocidade, Animações, Efeitos, Excluir, Remover fundo, Máscara, Ajustar, Filtros, Volume, Redução de ruído, Substituir, Duplicar, Congelar, Reverter, Opacidade, Transformar, Estabilização, Extrair áudio, Batidas, mover para Camada, Keyframe, efeitos de áudio, Motion Blur e reenquadramento. O provedor especializado acrescenta separador e Gráficos.

Cada nível do dock mantém uma view reutilizável. O fluxo de retorno percorre os níveis. A existência de provedores por tipo apoia a troca contextual das ferramentas quando muda a seleção.

## Observado em execução

As capturas REA estão em `build/reference/capcut-ui-20261115/screens/`. Os PNG têm 1080 × 2340 pixels. As medidas em dp da tabela anterior continuam vinculadas aos recursos estáticos e podem corresponder a variantes diferentes.

- `editor-root/screen.png` mostra preview acima do transporte, play central, fullscreen à esquerda, controles contextuais e desfazer/refazer à direita. O tempo atual e total ficam na região esquerda acima das faixas. A linha vertical do playhead está no centro horizontal da timeline.
- A timeline raiz mostra miniaturas do clipe principal, botão adicionar fixo à direita e prefixo com mute, AI clipper e capa à esquerda do início do clipe. Há atalhos inline `+ Add audio` e `+ Add text` abaixo do vídeo. As categorias visíveis são Edit, Audio, Text, Effects, Overlay, Captions e parte de Filter.
- `editor-selected/screen.png` mostra contorno branco do clipe selecionado, alças claras nas extremidades, duração de três segundos e dock contextual com voltar, Split, Animations, Effects e Delete entre opções condicionais. O preview recebe um contorno de seleção. Um tutorial de transição aparece sobre a timeline.
- `editor-scrolled/screen.png` mostra o tempo atual em dois segundos. O conteúdo e os atalhos inline deslocaram-se à esquerda. Mute e capa saíram da área visível. O playhead permaneceu central e adicionar permaneceu no mesmo ponto à direita. Essa sequência confirma o deslocamento do conteúdo sob o indicador fixo e a independência do botão adicionar.
- A ordem observada do dock confirma a dependência de configuração. A lista estática de provedores contém ferramentas adicionais cuja disponibilidade exige abrir o contexto correspondente.

## Gestos e limites

- Observado estaticamente o uso de scrollers sincronizáveis, propriedade `timelineScale`, limites de rolagem e listeners de alteração. A posição do playhead é declarada fora do conteúdo rolável.
- Confirmada a interação de deslocar conteúdo sob o playhead central pela sequência de capturas em execução. A física de rolagem e a resposta nos limites inicial e final permanecem sem medição.
- Não comprovados os limiares de pinça, pressão longa para rearranjo, hitboxes de trim, snapping, formas de onda efetivamente exibidas e resposta tátil. A presença de `AudioWaveCollectScroller` comprova uma região declarada de 34 dp para waveform, cuja visibilidade depende do estado.
- Não comprovados os pixels de outras variantes remotas, a ordem de menus fora dos estados registrados, a fluidez em aparelho físico e a equivalência visual com a versão oficial.

## Adaptação proposta para o Aurea

Organizar a edição em topo compacto, preview, transporte, timeline e dock inferior contextual. Manter as operações avançadas do Aurea acessíveis em Mais constitui escolha de adaptação. O agrupamento inicial dessas operações e uma altura de dock de 80 dp constituem decisões do Aurea.

O alinhamento da timeline deve manter o playhead central, limites de navegação finitos, ferramentas de capa e mute associadas ao início do conteúdo e adicionar fixo à direita. O funcionamento existente do motor permanece como dependência para todas as ações exibidas. A preservação da barra inferior da composição segue a orientação específica do usuário.

## Registro canônico REA

Arquivos novos em `build/reference/capcut-ui-20261115/`. Evidências anteriores verificadas pela identidade dos DEX em `build/reference/capcut-19.8.0/rea/`.

| Arquivo | Evidence ID |
|---|---|
| `package-shard14.json` | `ev_d58e7b24aa70b039861a1c3a2ea9408e894452ff3dd6c99b9627b31859d63b11` |
| `topbar-clinit.json` | `ev_7ca497917f5db9bba67ce915b24be02e00317e353f82ee0e9ae8a9dbe200e092` |
| `preview-clinit.json` | `ev_b70ce4a0d4d77a42ed44d5ba9738e6b3a825b318df3c3ecaaf24daaaf1f96964` |
| `transport-content-resource.json` | `ev_72a14f59b8d990bf30306e1aff2bdd834cd6360e6b91589a01cbafd672a60756` |
| `track-ctor.json` | `ev_dd90fc5879070d9b7a649daa0d365b348374914c66a0f6e93b975fdc5867a368` |
| `base-track-init.json` | `ev_1a64ee5e637a5283e0a8e159d5e735af93a7d5f2f4985810c86cb3481ee313e8` |
| `scroller-class.json` | `ev_7372ca4e869d719b4ccf5e52f5caae679f63e7f6eda835355fe0cfc20e9bd079` |
| Anterior `m-TopLevel-f.json` | `ev_8e54d50949f54355cc5ab44c7ff7ae49e139775ff6aeec921e4d964b6bfa7522` |
| Anterior `m-BaseMainVideo-f.json` | `ev_cf45d3d5ecf4b854950f0380f8fbba368f704246fe14386f7249ff5a6481aa52` |
| Anterior `m-MainVideoDock-F.json` | `ev_274de2ce1f7e4161e27253b77d249a7d17d44bab3c445ee017c24a0d9743f1d9` |
| Anterior `m-DockGroup-b.json` | `ev_db727c46db9076e61b5978a34764f6a830ad45af44c9a09fbf96093c3a4ce0fb` |
| `runtime-launch.json` | `ev_14d28eb6b341df027633b772bd4e026b04bb24df82cc0fe04772f7d0964cd2c6` |
| `runtime-editor-root.json` | `ev_01d44fb194e36d268759533fc26d871faaa6df787e1cb39971d022cbcf326bae` |
| `runtime-editor-selected.json` | `ev_4d9b0b229d4127887f150cb48281c18890f7f41bb7ba63415e0f074cb857f334` |
| `runtime-editor-scrolled.json` | `ev_fd74af2d66454fd71c691334e97109b74c2ee3fa1253156a65172831fb583bc4` |

Os dumps `topbar-layout.txt`, `preview-layout.txt`, `track-layout.txt`, `track-opt-layout.txt`, `transport-default-layout.txt` e `resource-map.txt` registram o complemento aapt2. `transport-content-layout.txt` corresponde ao controle fullscreen referenciado pelo ViewStub. O controle padrão de edição está em `transport-default-layout.txt`.

O registro anterior `SPEC.md` atribui um Evidence a um construtor cujo JSON disponível está vazio. Essa atribuição permanece sem verificação e foi excluída das conclusões diretas deste documento. O helper atual chama Node e o CLI REA diretamente para preservar argumentos de construtor no Windows.
