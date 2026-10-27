# Aurea 0.3.5 BETA (versionCode 2202) — correções da 0.3.4 e grade controlada por nulo

Android: AAB (arm64-v8a + armeabi-v7a). iOS: IPA sem assinatura (CI do GitHub).

Obrigado a **hanszx_** pela colaboração na nova interface.

## Google Play (pt-BR)

```
Novidades da 0.3.5 BETA:
• Grade controlada por um nulo: gire, mova e escale a grade inteira, em cadeias sem limite
• Converter forma em contorno (menu ⋮ da camada)
• AureaHyper liberado em qualquer aparelho, por sua conta e risco
• Exportar quadro, sequência PNG e ZIP no formato escolhido
• Projetos de versões anteriores voltam a abrir
• Áudio aparece na seleção de mídia
• Campos de rotação, X/Y/Z, fps e resolução respondendo ao toque
• Rotobrush, prévia em 1/4, câmera da cena 3D e gráfico de valor corrigidos
• Exportação mais estável em aparelhos com pouca memória
```

## Google Play (en-US)

```
New in 0.3.5 BETA:
• Grid controlled by a null: rotate, move and scale the whole grid, in chains of any length
• Convert a shape to an outline (layer ⋮ menu)
• AureaHyper unlocked on every device, at your own risk
• Current frame, PNG sequence and ZIP export in the chosen format
• Projects from older versions open again
• Audio shows up in media selection
• Rotation, X/Y/Z, fps and resolution fields respond to touch
• Rotobrush, 1/4 preview, 3D scene camera and value graph fixed
• More stable export on low-memory devices
```

## Versão completa

### Novidades
- **Grade controlada por um nulo**: no efeito da grade, "Controlado por" escolhe um nulo (ou "Criar
  nulo de controle", que cria e já vincula). Mover, girar em X/Y/Z, escalar e mudar o pivô do nulo
  transforma a grade inteira, no preview e no export. Vínculos encadeados sem limite (nulo → nulo →
  …), várias grades no mesmo nulo e grade dentro de grade; um vínculo que fecharia um ciclo é
  recusado com aviso.
- **Converter em contorno** no menu ⋮ de uma forma: a forma vira um vetor editável (Editar pontos),
  com preenchimento, degradê e contorno mantidos, num passo de desfazer.
- **AureaHyper em qualquer aparelho**: Turbo, Lite e Max sempre disponíveis. Sem estimativa de
  tempo, aparece "Tempo desconhecido"; em aparelhos mais fracos, um aviso de que pode ficar lento ou
  fechar (por sua conta e risco).
- **Cores das camadas na timeline**: cor sólida por tipo de camada, na identidade do AUREA.
- **Novidades do app** atualizadas (estavam mostrando as da 0.3.2).

### Correções
1. **Exportação**: Quadro atual (PNG), Sequência PNG/ZIP e GIF saíam como MP4 no Android — o botão
   Exportar usava as opções da primeira abertura da página. Agora usa o tipo escolhido.
2. **Importar projeto**: arquivos `.aureaproj`/`.pjf` de versões anteriores voltam a abrir, inclusive
   recompactados por outro programa; "Abrir com" funciona no Android e no iPhone; no iPhone, o botão
   "Arquivos" da gaveta voltou a abrir o seletor.
3. **Plugins (Android)**: o navegador de efeitos usa chaves únicas por tipo de item; instalar vários
   plugins não derruba mais a lista.
4. **Seleção de mídia**: a prévia mostra a mídia tocada por último, não a anterior.
5. **Áudio na seleção de mídia**: "Outros arquivos e nuvem" abre o seletor de arquivos com imagem,
   vídeo e áudio; o áudio entra como camada de áudio (sem pedir acesso à música do aparelho).
6. **Interface**: o painel de efeitos rola no iPhone; a roda de rotação e os campos X/Y/Z respondem a
   um toque normal; as caixas de fps e resolução abrem no Novo projeto e nos Ajustes do projeto.
7. **Rotobrush**: depois de uma falha, um traço novo, desfazer ou "Propagar clipe" tenta de novo, e o
   aviso diz o motivo.
8. **Prévia em 1/4**: uma cópia de prévia verde ou parada é descartada e a camada volta ao vídeo
   original; as cópias antigas são refeitas.
9. **Cena 3D**: sem seleção, arrastar gira a câmera e a pinça aproxima; tocar de novo no objeto
   escolhido tira a seleção.
10. **Gráfico de valor**: os pontos baixos não ficam mais embaixo da faixa ◀ ▶, e dá para trocar de
    trilha e arrastar no mesmo gesto.
11. **Converter forma em contorno** (ver Novidades).
12. **Exportação que fechava no meio**: a limpeza de memória do renderizador agora roda em qualquer
    exportação longa, GIF e sequência respeitam o aviso de memória do sistema, e o serviço de
    exportação do Android voltou a iniciar no Android 14+ (era recusado e o sistema podia encerrar a
    exportação).
- Painel de legendas do iPhone abria as legendas da camada anterior ao trocar de camada.
