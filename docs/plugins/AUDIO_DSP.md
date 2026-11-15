# Plugins DSP de áudio

O Aurea 1.0.0 acrescenta `"kind": "audio"` ao formato 3. O grafo declarativo processa PCM e produz áudio na camada. Parâmetros, keyframes, expressões e controles usam o painel de plugins existente. Os arquivos de exemplo estão em `docs/plugins/examples/audio-*`.

## Entrada e execução

`AudioProcessor::process_block(input, output, frames, sampleRate, channels, parameters)` recebe buffers float intercalados. O formato do mixer é estéreo a 48.000 Hz. O retorno informa erros de formato, orçamento e resultado numérico inválido. Entrada e saída podem compartilhar o buffer.

O host chama o processador em sub-blocos alinhados de até 32 amostras e resolve os parâmetros no início do sub-bloco absoluto. Linhas de atraso, envelopes, filtros e convolução mantêm estado entre chamadas. Instâncias de camadas diferentes possuem estados independentes. A preparação carrega os arquivos e aloca os buffers. O processamento executa um grafo ordenado com memória previamente alocada.

Cada nó nomeia suas entradas. A declaração pode apresentar os nós em qualquer ordem. A instalação ordena as dependências, recusa referências ausentes e ciclos e elimina nós sem caminho até a saída. A realimentação pertence aos nós `delay` e `allpass`.

```json
{
  "aureaPlugin": 3,
  "id": "studio.echo",
  "version": "1.0.0",
  "name": "Echo",
  "kind": "audio",
  "params": [
    {"id":"delay_ms","type":"float","label":"Time","default":250,"min":1,"max":2000},
    {"id":"feedback","type":"float","label":"Feedback","default":0.35,"min":0,"max":0.95},
    {"id":"mix","type":"float","label":"Wet mix","default":0.3,"min":0,"max":1}
  ],
  "audio": {
    "nodes": [
      {"id":"dry","type":"input"},
      {"id":"echo","type":"delay","input":"dry","timeMs":{"param":"delay_ms"},"feedback":{"param":"feedback"},"dampingHz":12000,"maxDelayMs":2000},
      {"id":"output","type":"mix","inputs":["dry","echo"],"gains":[1,{"param":"mix"}]}
    ],
    "output":"output"
  }
}
```

## Nós disponíveis

| Tipo | Controles e operação |
|---|---|
| `input` | Entrada PCM do bloco |
| `gain` | Multiplicador linear `gain` |
| `mix` | Soma de até oito `inputs`, com um controle em `gains` por entrada |
| `delay` | `timeMs`, `feedback`, `dampingHz`, `maxDelayMs`. A saída contém o sinal atrasado. O nó `mix` combina seco e molhado |
| `allpass` | Difusor com `timeMs`, `feedback`, `maxDelayMs` |
| `lowpass`, `highpass` | Biquad de segunda ordem, controle `frequency` |
| `eq` | EQ paramétrico com `frequency`, `q`, `gainDb` |
| `compressor` | Detector estéreo vinculado, `thresholdDb`, `ratio`, `attackMs`, `releaseMs`, `kneeDb`, `makeupDb` |
| `limiter` | Limitador de picos por amostra, com teto `thresholdDb` e soltura `releaseMs` |
| `convolution` | FIR por resposta ao impulso, controles `impulse` e `mix` |

Um controle aceita um número ou `{"param":"id","scale":1,"offset":0}`. O motor limita o parâmetro à faixa declarada e aplica `valor × scale + offset`. Os parâmetros DSP aceitam `float`, `int`, `bool`, `enum` e `angle`. O identificador possui até 32 caracteres ASCII, começando por letra ou sublinhado.

O feedback efetivo fica entre −0,999 e 0,999. Atrasos usam interpolação fracionária e pelo menos uma amostra. O tempo máximo corresponde a `maxDelayMs`. Filtros trabalham até 23.000 Hz. O EQ limita Q a 0,1–20 e ganho a ±48 dB. O compressor admite razão de 1–100, joelho de 0–24 dB e ganho de saída de ±24 dB. O limitador mantém o teto por amostra. A verificação de true peak com oversampling exige processamento adicional do autor fora deste conjunto de nós.

## Respostas ao impulso

```json
"audio": {
  "impulses": [{"id":"room","file":"room.wav"}],
  "nodes": [
    {"id":"dry","type":"input"},
    {"id":"wet","type":"convolution","input":"dry","impulse":"room","mix":0.4}
  ],
  "output":"wet"
}
```

O arquivo fica dentro do pacote. Caminhos absolutos, travessia de diretórios e links que escapem da pasta são recusados. O leitor suporta WAV PCM de 16, 24 e 32 bits, float de 32 bits e cabeçalho extensível compatível. Aceita mono ou estéreo de 8–192 kHz. A preparação usa o resampler sinc do mixer para converter a resposta para 48 kHz. Uma resposta mono aplica os mesmos coeficientes aos dois canais. Uma resposta estéreo fornece coeficientes separados para L e R.

A convolução usa uma cabeça direta de 128 amostras e partições FFT de 128, 512 e 2.048 amostras. A cabeça direta permite entregar a saída corrente com latência algorítmica adicional de zero amostras. A divisão dos blocos de entrada mantém o resultado contínuo. A resposta permanece imutável e compartilhada, enquanto cada instância guarda seu histórico.

## Orçamentos

| Recurso | Limite |
|---|---|
| Nós declarados | 64 |
| Entradas por nó | 8 |
| Parâmetros de áudio ou stack | 1.024 |
| Bloco da API DSP | 1–4.096 quadros estéreo a 48 kHz |
| Linha de atraso | 8.000 ms por nó |
| Resposta ao impulso | Um arquivo e um nó de convolução, até 96.000 quadros a 48 kHz |
| Arquivo WAV | 16 MiB |
| Estado e coeficientes por programa | 32 MiB |
| Programas do snapshot ou sessão offline | 128 MiB de admissão acumulada |
| Teste de CPU na instalação | Oito blocos de 4.096 quadros, média de até 50% do tempo real no dispositivo |
| Aquecimento após salto | Até oito segundos, conforme o mixer do host |

O teste de CPU é uma medição de admissão no aparelho durante a instalação. Os limites estruturais continuam valendo durante o uso. A exportação retorna erro se o DSP falhar ou o orçamento da sessão for excedido. O programa e seus coeficientes são retidos pelo snapshot da exportação, inclusive se o plugin for removido do catálogo durante o trabalho.

A continuidade usa todo o histórico desde o início da reprodução. Um seek reconstrói o estado com a janela de aquecimento do host. Redes com feedback muito longo podem conservar energia anterior a essa janela durante a reprodução contínua.

## Colocação e exemplos

Adicione o plugin na pilha de efeitos de uma camada com áudio. A camada pode conter áudio de um vídeo, um arquivo de som ou um gerador de tom. O catálogo oferece esses plugins nas camadas de origem. A pilha conserva a ordem de processamento entre efeitos.

- `audio-delay` implementa eco com feedback e mix.
- `audio-room` combina quatro atrasos amortecidos, pré-delay e difusão allpass.
- `audio-eq` implementa EQ paramétrico.
- `audio-compressor` combina compressão de joelho suave e teto por amostra.
- `audio-convolution` acompanha uma resposta sintética original de sala que pode ser substituída por um WAV do criador.

Os shaders continuam com 252 posições de parâmetros em `shader` e 189 em `shader3d`, conforme o bloco uniforme de 4 KiB. Curvas, degradês e análise de áudio consomem posições adicionais descritas na referência principal.
