# Aurea 0.3.2 (versionCode 2198) — Export Engine 2.0

Auditoria, arquitetura e resultados: `docs/diagnostico/export-engine-2.0.md`.

## Google Play (pt-BR)

```
Novidades da 0.3.2:
• AureaHyper (grátis por enquanto): upscale, 60 fps, câmera lenta, limpeza e estabilização
• Exportação continua com o app em segundo plano (Android)
• Export interrompido continua de onde parou
• Motion blur 3D até 24× mais rápido (modo vetores)
• Marcadores com nome na timeline
• Aviso quando o aparelho precisou reduzir algo
• FFmpeg 9 e menos falsos "travou"
• Qualcomm (ex.: Moto g52): exportação não para mais no meio
```

## Google Play (en-US)

```
New in 0.3.2:
• AureaHyper (free for now): upscale, 60 fps, slow motion, cleanup and stabilization
• Exports keep running with the app in the background (Android)
• An interrupted export continues where it stopped
• 3D motion blur up to 24× faster (vector mode)
• Named markers on the timeline
• A notice when the device had to reduce something
• FFmpeg 9 and fewer false "stuck" errors
• Qualcomm (e.g. Moto g52): exports no longer stop midway
```

## Versão completa

### AureaHyper (novo — grátis por enquanto)
- Substitui o "Upscale com IA" no menu Aurea da Home. Em quatro passos:
  escolher o vídeo (galeria ou gravar agora), escolher o que fazer, ver a
  prévia antes/depois com o tempo estimado, e acompanhar o trabalho. O
  resultado vira um projeto.
- Receitas prontas: Restaurar vídeo antigo, Mais nítido 2× / 4×, 60 fps,
  Câmera lenta 2× / 4× / 8×, Anime e Estabilizar. Em "Ajustar": força da
  limpeza, blocos de compressão, entrelaçado, rostos, fps, câmera lenta e
  estabilização (inclusive gravar como keyframes).
- Três caminhos, escolhidos pelo aparelho (dá para trocar): **Turbo** (na GPU,
  qualquer aparelho, inclusive 32 bits), **Lite** (redes pequenas) e **Max** (a
  rede completa, para aparelho bom ou para deixar processando). Na primeira vez
  o app mede o aparelho por cerca de 10 s.
- No editor: efeitos de camada Hyper Upscale, Hyper Motion e Hyper Clean (a
  prévia na timeline usa sempre o Turbo).
- Dá para sair do app enquanto trabalha (Android continua em segundo plano;
  iPhone pausa e continua ao voltar).
- Câmera lenta sai sem som (o app avisa).
- Grátis nesta versão. Mais para a frente vira recurso pago (compra única,
  `aureahyper_lifetime`); nada é cobrado agora.
- O acesso vem de uma configuração remota assinada (`/api/app-config`, ECDSA
  P-256): o servidor pode deixar o Hyper livre (`free`), só para contas
  liberadas (`entitled`) ou desligado (`off`), e pode exigir uma versão mínima
  do app (tela "Atualização obrigatória"). O modo `free` só vale com uma
  configuração emitida há no máximo 7 dias: ficar offline não mantém o Hyper
  liberado, e a versão mínima bloqueia quem ficar na 0.3.2 depois que o Hyper
  passar a ser pago. Como usar: `docs/play-store/APP_CONFIG.md`.
- Nova tela "Licenças de código aberto" (Home e Ajustes) com tudo o que o
  AureaHyper usa.
- O Upscale saiu da tela de Exportar: agora ele está no AureaHyper e no efeito
  Hyper Upscale.

### Marcadores com nome
- O nome do marcador aparece na régua da timeline, ao lado dele.
- Tocar no marcador abre o editor, com o campo "Nome do marcador".

### Exportação em segundo plano e retomada
- **Android**: ir para outro app ou apagar a tela não cancela mais a exportação.
  Ela continua num serviço em primeiro plano, com a porcentagem na notificação.
- **Retomada**: um export interrompido (app fechado pelo sistema, falha, iPhone
  em segundo plano) continua do último trecho salvo em vez de recomeçar do zero.
  Só retoma se o projeto, as opções e as mídias forem exatamente os mesmos; o
  arquivo final é montado e conferido antes de ir para a galeria.
- **iPhone**: o iOS não deixa usar a GPU em segundo plano. Ao sair do app a
  exportação pausa e continua sozinha quando você volta. A tela fica acesa
  durante a exportação.
- Cancelar de propósito apaga o trecho salvo.

### Motion blur 3D por vetores
- Opção nova em Motion blur → Avançado: **Motion blur 3D rápido (vetores)**. A
  cena 3D é renderizada uma vez e borrada pelo movimento de cada pixel — cerca
  de 24× mais rápido na GPU que as amostras.
- Objetos que deformam (animação de esqueleto, morph), transparentes, partículas
  e trajetos curvos continuam com amostras automaticamente.
- Projetos antigos continuam no modo de alta qualidade (amostras), sem mudar.

### O vídeo não depende mais da carga do aparelho
- Antes, quando um quadro demorava, o export reduzia o motion blur e a sombra no
  meio do vídeo, e o mesmo projeto saía diferente conforme o aparelho estivesse
  ocupado. Agora a qualidade é decidida antes de começar e vale do primeiro ao
  último quadro.
- Se a GPU não aguentar, a próxima tentativa já começa mais leve e a tela avisa.

### Avisos do que foi reduzido
Ao terminar, a exportação diz quando precisou:
- usar menos amostras de motion blur;
- sair no maior tamanho que o aparelho exporta;
- trocar a imagem HDR do ambiente 3D pelo ambiente neutro;
- usar o modelo 3D da prévia no lugar do Original.

### Mais estável
- **FFmpeg 9.0.2** nas três versões do Android (64 bits, 32 bits e emulador).
- **Celulares Qualcomm** (Moto g52 e outros com encoder `OMX.qcom`): no modo de
  taxa variável o encoder do aparelho descartava quadros e a exportação parava
  no meio (fila cheia). Agora esses encoders usam taxa constante na mesma taxa
  alvo; se o aparelho recusar, abre no modo de antes.
- Se a Surface do encoder parar no meio do vídeo, a próxima tentativa usa a
  entrada por buffer do mesmo encoder, sem baixar resolução.
- Uma retomada cujo trecho salvo foi gravado por outro caminho do encoder
  recomeça do zero, em vez de terminar em "encoder indisponível".
- Um quadro com Roto Brush ou IA demorado não é mais tratado como travado.
- A gravação final de vídeos grandes não é mais confundida com o encoder parado.
- Cancelar durante a conferência final não publica mais o vídeo.
- Encoder do aparelho (modo ByteBuffer): o quadro vai com o tamanho certo.
- iPhone: o aviso de aparelho quente volta a funcionar.

### Observações
- AureaHyper: o modelo de rostos (GFPGAN) é baixado sob demanda e ainda não
  está hospedado, então "Rostos" não faz nada até ele estar disponível.
- Testado em aparelho físico: Moto g52 (Snapdragon 680, Adreno 610, Android 13),
  no APK de 64 bits e no de 32 bits. Exportação (classe ExportV2, checkpoint/retomada), Home, notas da
  versão, portão do AureaHyper e jobs Hyper reais: Turbo 2× + 60 fps (11,9 s
  para 2 s de vídeo), câmera lenta 2× (6,7 s) e Lite 2× (funciona, mas lento
  nesse aparelho: ~18 s por quadro de 480×320 — a GPU não tem fp16).
- Não testado em aparelho: aparelho só de 32 bits, outros fabricantes (vivo, Oppo,
  MediaTek, PowerVR, Samsung) e iPhone. O iOS foi compilado pelo CI; a
  execução no iPhone não foi testada.
- A troca de encoder no meio do vídeo recomeça do início (fluxos de encoders
  diferentes não emendam).
