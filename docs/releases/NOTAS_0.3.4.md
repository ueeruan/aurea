# Aurea 0.3.4 BETA (versionCode 2201) — plugins que criam qualquer coisa

Android: AAB (arm64-v8a + armeabi-v7a). iOS: IPA sem assinatura (CI do GitHub).

Obrigado a **hanszx_** pela colaboração na nova interface.

## Google Play (pt-BR)

```
Novidades da 0.3.4 BETA:
• Novo sistema de plugins: painéis, abas e botões novos no app
• Scripts no estilo do After Effects: camadas, keyframes e efeitos
• Plugins podem usar APIs da internet que você autorizar
• Painéis maiores e controles mais fáceis de tocar
• Ícones novos, minimalistas e limpos
• Painel de presets refeito
• iPhone: ajustes do projeto e do novo projeto voltaram a responder
• iPhone: tela de projetos mais leve na rolagem
```

## Google Play (en-US)

```
New in 0.3.4 BETA:
• New plugin system: new panels, tabs and buttons in the app
• After Effects style scripts: layers, keyframes and effects
• Plugins can use internet APIs you approve
• Bigger panels and easier-to-tap controls
• New minimalist, clean icons
• Redesigned presets panel
• iPhone: project and new project settings respond again
• iPhone: smoother scrolling on the projects screen
```

## Versão completa

### Plugins v2
- **Plugins de interface** (`"aureaPlugin": 2`, `"kind": "panel"`): um plugin pode criar
  **painéis no editor** (dentro da moldura dos painéis, abertos pelo botão de ferramentas no
  topo), uma **aba nova no dock da Home**, itens no menu do editor e no ⋮ da camada, e
  **botões na barra da camada**. Os controles (botão, régua, slider, cor, escolha, texto,
  lista, imagem e outros) são desenhados pelos componentes do próprio AUREA, em todos os
  idiomas do manifesto.
- **Scripts (AureaScript)**: JavaScript com uma API de objetos no estilo dos scripts do After
  Effects — projeto, composição, camadas, propriedades com keyframes e interpolação, efeitos,
  marcadores, cabeçote, seleção, render de um quadro, mensagens, armazenamento do plugin e
  console. Toda alteração passa pelos comandos do motor: **desfazer funciona**, e cada ação de
  um painel vira um passo (`app.beginUndoGroup`/`endUndoGroup` também existem).
- **APIs externas**: `fetch` só por HTTPS e só para os domínios que o plugin declara; área de
  transferência e arquivos escolhidos pela pessoa. Tudo pedido na **instalação**, com uma tela
  de consentimento, e ajustável depois por permissão. Permissão negada = erro claro no script.
- **Seguro**: cada chamada tem limite de tempo e memória, dá para **parar** um script travado,
  e um plugin com erro não fecha o app. O console mostra os erros.
- **Gerenciador** (Efeitos › Plugins): instalar, ver o que o plugin pede, ligar/desligar,
  abrir o painel, console.
- Documentação nova (seção 14 de `docs/plugins/README.md`) e dois exemplos prontos:
  *Texto animado* (cria camadas e keyframes) e *Clima em texto* (busca uma API pública).
- Os plugins de efeito de antes continuam funcionando igual.

### Editor
- **Painéis de propriedade mais altos**: sobem até logo abaixo da linha da camada na
  timeline, aproveitando o espaço que ficava vazio.
- **Controles maiores**: campos de posição, rotação e escala, rótulos, réguas, abas e a
  coluna da esquerda cresceram para ficar mais fáceis de ler e tocar.
- **Painel de presets refeito**: um título só, cabeçalho compacto e a grade de presets
  visível e rolável, com prévia e nome.
- Botão "Todas/Seleção" da timeline com visual limpo.

### Visual
- **Ícones minimalistas**: contorno com traço fino e uniforme no app inteiro (os cheios ficam
  só onde indicam estado, como play, keyframe e favorito).

### iPhone
- As caixas de escolha (fps, resolução, proporção) no **Novo projeto** e nos **Ajustes do
  projeto** voltaram a abrir e mudar o valor. O menu às vezes ficava invisível e "engolia"
  o toque seguinte.
- **Tela Projetos** mais leve: sombras, medições a cada quadro e a animação de carregamento
  sem fim deixaram de pesar na rolagem.
