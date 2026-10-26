# Aurea 0.3.2 (versionCode 2198) — Export Engine 2.0

Auditoria, arquitetura e resultados: `docs/diagnostico/export-engine-2.0.md`.

## Google Play (pt-BR)

```
Novidades da 0.3.2:
• Exportação continua com o app em segundo plano (Android)
• Export interrompido continua de onde parou
• Motion blur 3D até 24× mais rápido (modo vetores)
• O vídeo sai igual em qualquer aparelho ocupado ou não
• Aviso quando o aparelho precisou reduzir algo
• FFmpeg 9: codificação mais nova e estável
• Menos falsos "travou" em vídeos longos e efeitos de IA
```

## Google Play (en-US)

```
New in 0.3.2:
• Exports keep running with the app in the background (Android)
• An interrupted export continues where it stopped
• 3D motion blur up to 24× faster (vector mode)
• The video comes out the same whether the device is busy or not
• A notice when the device had to reduce something
• FFmpeg 9: newer, more stable encoding
• Fewer false "stuck" errors on long videos and AI effects
```

## Versão completa

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
- Um quadro com Roto Brush ou IA demorado não é mais tratado como travado.
- A gravação final de vídeos grandes não é mais confundida com o encoder parado.
- Cancelar durante a conferência final não publica mais o vídeo.
- Encoder do aparelho (modo ByteBuffer): o quadro vai com o tamanho certo.
- iPhone: o aviso de aparelho quente volta a funcionar.

### Observações
- Nada desta versão foi testado ainda em aparelho físico (ARM 32/64, vivo, Oppo,
  MediaTek, PowerVR, iPhone).
- A troca de encoder no meio do vídeo recomeça do início (fluxos de encoders
  diferentes não emendam).
