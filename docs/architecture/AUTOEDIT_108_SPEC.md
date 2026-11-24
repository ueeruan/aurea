# AutoEdit 1.0.8 — especificação de comportamento

Fonte: leitura (sem executar) do JS/JSX legível da extensão do próprio dono,
Essential Captions 2.36.2 (`%APPDATA%\Adobe\CEP\extensions\com.essential.captions.ae`),
fluxo "Preparar meu vídeo". É **especificação de comportamento**: nada de código,
modelo ou asset dessa extensão entra no Aurea. O Editor Premium (terceiros) não
foi aberto. Números marcados [obs] foram lidos no código; [inf] são inferidos.
Nada foi medido em execução.

## 0. Fluxo e padrões
- Ordem das etapas [obs, `ec-edicao/preparar.js`]: analisar → cortes → broll →
  cópia → legendas → zoom → transições → cor → rastreio → palavra atrás → gancho →
  cena → voz → música → sons.
- Padrões: intensidade de corte "média" (força 50; leve 25, forte 75), gravação
  "retomadas" (alternativa "contínua"/entrevista, e "aula"/tela), ritmo "direto",
  J/L automático 3–5 quadros, cor ligada, voz ligada, música −15 dB com redução 8 dB.
- A edição sai numa **cópia**; a original fica intacta [obs].

## 1. Análise de áudio [obs, `ec-edicao/audio.js`]
- Fala em WAV mono 16 kHz/16 bits, só as faixas de fala.
- Envelope em quadros de 10 ms: energia dB = 10·log10(média x² + 1e-18), "hf" = dB
  da diferença entre amostras vizinhas, zcr normalizado a 16 kHz.
- Detecção de fala: suavização 3 quadros; ruído = p10, nível = p95 (quadros > −120 dB);
  limiar = ruído + max(6, 0,3·(nível − ruído)) (faixa < 6 dB: ruído − 1 se nível > −50,
  senão nível + 1); histerese ±2 dB; segura 120 ms; junta buracos < 120 ms;
  ilha < 60 ms = estalo.
- Borda pelo ruído do vão: p20 do vão; sensibilidade 1–7 → 3/4,5/6/8/10/12,5/15 dB
  acima do ruído (padrão 8 dB); fricativa colada (zcr16k > 0,3) estende a borda.
- Respiração: 0,12–1,0 s, média < nível − 10 dB, zcr > 0,15, ≤ 20 % de quadros vozeados
  (vozeado = zcr16k < 0,1 e dB > max(ruído + 12, nível − 40)).

## 2. Transcrição [obs, `ec-transcricao/whisper.js`]
- Whisper local (modelo small padrão), idioma pt padrão, prompt "ao pé da letra"
  (mantém gaguejos e hesitações), tempo por palavra por DTW.
- Laço detectado (8 repetições ou 12 inícios a < 20 ms) → refaz com busca gulosa.
- Alinhamento ao som: limiar p10 + 0,3·(p95 − p10) se faixa ≥ 10 dB; início procura
  ataque em −0,25..+0,15 s e tira 20 ms; fim segue até a voz cair por 80 ms + 30 ms;
  duração mínima 80 ms; duração máxima por palavra min(2 s, max(0,6 s, 0,12 s × letras)).
- Palavra sem nenhum quadro acima de min(p10 + 0,3·faixa, p95 − 6) = fantasma, sai.

## 3. Cortes (motor "v5") [obs, `ec-edicao/fluxo.js`, `regras-corte.js`]
### 3.1 Pausas
| Intensidade | Vão que sai (> s) | Margem antes | Margem depois |
|---|---|---|---|
| leve | 0,45 | 0,10 | 0,16 |
| média | 0,30 | 0,07 | 0,12 |
| forte | 0,22 | 0,05 | 0,08 |
- Borda do trecho pela energia: início recua até 0,15 s, fim avança até 0,25 s
  enquanto nível ≥ limiar; depois soma as margens. Trechos a ≤ 20 ms se fundem;
  trecho < 0,2 s sai; corte só se > 40 ms.
- Respiro entre palavras: vão sem vogal ≥ 0,3 s com ≥ 0,18 s de respiro → corta
  deixando 60 ms de cada lado (corte ≥ 80 ms).
### 3.2 Vícios
- Hesitações (saem sempre): ah, ahn, an, aa, ã, ãh, eh, ee, er, erm, hm, hmm, hum,
  uh, uhm, mm, mmm (e variações).
- Claquete: "take/cena/tomada + número", "mais uma", "de novo", "valendo", "gravando",
  "rodando" isolados por ≥ 0,3 s antes e 0,4 s depois.
- Muletas pt: TAGS fechando frase (entendeu, né, beleza) a partir da 2ª palavra;
  TAGQ com "?" (tá, ok, certo, tranquilo, viu) que aparecem ≥ 3 vezes; vocativos
  (galera, pessoal, rapaziada) só se ≥ 5 vezes, no início da frase, a > 15 s das
  pontas. Guarda do "não": muleta a ≤ 0,12 s de "não/nem" volta.
- Bastidor: frase curta de comando ("corta", "de novo", contagem regressiva,
  palavrão isolado ≤ 7 palavras) sai.
- Listas equivalentes em en/es.
### 3.3 Gaguejo e falso começo
- Mesma palavra dobrada com vão ≤ 0,5 s: sai a primeira (exceto ênfase e números).
- Palavra cortada (termina em "-"/"…" ou toco ≤ 4 letras, ≤ 0,3 s, prefixo de ≤ 70 %
  da seguinte) sai. Par dobrado (a b a b) com vãos ≤ 0,4 s: sai o primeiro par.
### 3.4 Tomadas repetidas (retakes)
- Frases: ponto final ou vão > 0,7 s. Tokens sem acento, números por extenso → dígito,
  igualdade "quase igual" = distância de edição ≤ 1 para palavras de 4+ letras.
- Compara cada frase (≥ 3 palavras) com as seguintes até 60 s, por LCS de tokens:
  repete = LCS/maior ≥ 0,45 se as 3 primeiras palavras batem, senão ≥ 0,55;
  recomeça = LCS/menor ≥ 0,80 com tamanhos diferentes.
- Veto por contraste (frases ≤ 10 palavras): negação diferente, números diferentes,
  1–2 termos trocados, mesmas palavras em outra ordem → não é retake.
- Escolha: sai a ANTERIOR (fica a última tentativa), exceto se a posterior termina
  sem ponto e a anterior está completa e é maior. Até 3 passadas.
- Sequência de 5 palavras que volta em ≤ 20 s no início de frase: sai da 1ª até a 2ª.
### 3.5 Proteções e faxina
- Trava: aborta se o corte deixar < 8 % do vídeo. Trecho mantido ≤ 0,12 s sai;
  ≤ 0,25 s só com palavras fracas sai. Cortes com fiapo < 0,15 s entre eles viram um.
- Corte de palavra: de max(anterior + 0,02, início − 0,05) até min(próxima − 0,03,
  fim + 0,05); mínimo 40 ms. Bordas no quadro de vídeo (sem zero-crossing).
- Emendas de áudio: crossfade potência constante de 2 quadros.
- J/L automático: sem J/L se corte < 0,15 s ou trecho vizinho < 1,0 s; fim de frase
  usa L, resto J; 3–5 quadros; teto 60 % da pausa limpa; mínimo 2 quadros.
- Mapa do tempo: trechos mantidos concatenados; palavra coberta ≥ 70 % por corte
  sai da legenda; legendas e todos os elementos seguintes usam o tempo da cópia.

## 4. Zoom ("zoom Reels") [obs, `ec-reels/zoomplan.js`]
- Pulado em gravação de tela. Pedaços = clipes da pessoa na cópia, divididos nos
  inícios de legenda (marca a > 1,5 s das pontas; sem marca, a cada 2,5 s).
- Forças: sutil 106/110 %, **média 112/118 %**, forte 118/126 % (punch/push).
  Parte do vídeo com zoom ≤ share (0,4 / **0,6** / 0,75). Taxa 0,9. Semente 7
  (mulberry32) → determinístico.
- Regras: pedaço 0 nunca é zoom (é a intro); depois de um zoom pelo menos um
  pedaço sem zoom; não repete o tipo anterior; evita zonas da "palavra atrás".
- Tipos por duração d: hold (punch fixo) d ≥ 1,2 s peso 1; inout (100→push→100)
  d ≥ 1,6 s peso 1; push (100→push até o acento + 0,35 s) d ≥ 2,6 s peso 0,8;
  track (escala média fixa, posição segue o rosto) d ≥ 2,0 s peso 0,45.
- Intro "out": o pedaço 0 começa em push % e vai a 100 % em min(1,2, d/2).
- Âncora: maior rosto → fx = centro x, fy = topo + 0,45 × altura (linha dos olhos);
  padrão (0,5, 0,42). Posição limitada para nunca mostrar borda.
- Curva Easy Ease 65/65 = cubic-bezier(0,65, 0, 0,35, 1). Rampas: inout
  min(0,9, d/2,5) cada lado; hold dentro do clipe min(0,7, d/2,5); push volta a
  100 % no fim se sobrar > 0,15 s.

## 5. SFX [obs, `ec-reels/reelsfx.js`, `sons.js`]
| Papel | Pico-alvo | Quando |
|---|---|---|
| woosh | −21 dBFS | zoom inout (pico aos 0,75 da rampa), intro, push (−2 dB) |
| shutter | −24 | início de zoom hold; contador |
| riser | −27 | antes da palavra atrás (≥ 8 s entre risers) |
| sub (drop) | −22, corta 0,9 s, fade 0,35 | no acento da palavra atrás; gancho |
| flash | −24 | corte com flash branco |
| cutflick | −23 | corte seco (prob. 0,45, ≥ 2 s entre eles) |
| flick (palavras) | −26 | palavras das legendas, em blocos (ligado 1–2 legendas, desligado 2–3) |
- Ganho = clamp(alvo − pico do arquivo, −40, +12). Sons a < 0,5 s mantêm o de maior
  prioridade (sub e riser isentos). Sorteio determinístico (sementes 11 e 13).
- Flash branco nos cortes: até 3, ≥ 6 s entre si, o primeiro no gancho (0,6–4 s);
  0,43 s, opacidade 0 → 100 % (0,13 s) → 100 % (0,17 s) → 0.
- Light leak: até 6 por vídeo, corte ≥ 3 s, ≥ 8 s entre luzes, 1 s centrado no
  corte, mesclagem Tela. Estilo Reels: sem transições de dissolver.
- O Aurea usa a **sua** biblioteca de SFX (whoosh, riser, impacto, câmera, clique)
  com picos medidos; os WAV da extensão não entram.

## 6. Música e ducking [obs, `musica-medida.js`, `calc.js`, `controle-audio.js`]
- Escolha: BPM alvo = clamp(85 + (palavras/s − 2)·15, 80, 120); pontos =
  −|bpm − alvo|/8, +3 se cobre o vídeo + 2 s, −2 se usada nas últimas 5, −1,5 se
  energia < −30 dB. Começa na "entrada" medida (1 s seguido ≥ energia − 6 dB).
- Volume base −15 dB, fade 1 s entrada / 3 s saída, sem laço (acaba antes se curta).
- Ducking: redução 8 dB, ataque 0,25 s, soltura 0,6 s; falas a < 1,15 s viram uma
  região; rampas lineares em dB.
- Segurança de pico: mede a mistura (ebur128); pico > −1,5 dBTP → redução +=
  ceil(pico + 2,5) e reaplica (até 3 vezes).
- **Para o Aurea:** só músicas da biblioteca própria do Aurea (a pasta da extensão
  tem faixas comerciais de artistas, que não podem ir no app).

## 7. Voz [obs, `painel-edicao.js:tratarVoz`, `host/ppro-audio.jsx`]
- Ganho até o alvo: LUFS integrados da fala; ganho = clamp(−12,5 − LUFS, −6, +15)
  em passos de 0,5 dB (≈ −14 LUFS no export).
- Passa-alta 70 Hz; compressor 2,5:1, limiar = p95 da fala + ganho − 8 dB
  (−26 dB sem medida), make-up automático; limitador true-peak −3 dBTP.
- Nível por frase: alvo = mediana; ganho clamp(alvo − nível, −6, +6) se |ganho| ≥ 1 dB.
- Respirações (piso + 8 < energia < fala − 10, ≥ 120 ms): −15 dB com rampas de 30 ms.

## 8. Cor [obs, `preparar.js:cor`, `controle.js:imagemJaTemCor`]
- Pula se o vídeo já tem cor: 3 quadros (20/50/80 %), p2 ≤ 0,08 e (saturação ≥ 0,42
  ou p98 − p2 ≥ 0,6).
- Normal: exposição 0, contraste +8, saturação 106 %. Log (por metadado): exposição
  +0,35, contraste +55, saturação 150 % (ou LUT técnica da câmera, quando houver).

## 9. Gancho, letterings, palavra atrás [obs, `ec-reels/*.js`]
- Destaque da legenda (`viral.js`): base 10 + 2·min(tamanho, 11); número +30;
  ! ou ? +8; ≤ 3 letras −6; última palavra +6; tudo maiúsculo +6.
- Gancho: frase das legendas até 3,5 s (≤ 26 caracteres), na tela até o primeiro
  corte (1,2–2,6 s); título ao lado oposto do rosto quando o rosto está fora do centro.
- Contador: uma vez, primeira legenda com número a partir de 4 s, ≥ 1,6 s.
- Palavra atrás da pessoa: legenda 0,9–2,8 s, destaque 3–14 letras, ≥ 8 s entre si,
  nunca nos 3 primeiros s, máx. 6; pessoa recortada por máscara; texto atrás da
  cabeça (y = topo − 0,55 × altura das letras); desiste se a cabeça cobrir > 22 %
  do texto; acompanha o rosto (suavização gaussiana σ 0,25 s).

## 10. B-roll [obs, `ec-edicao/broll.js`]
- Só das pastas do próprio usuário, casando palavras da fala com nome/tags/descrição
  dos arquivos (sem visão). Média: 8 por minuto, ≥ 3 s entre si, nada nos 3 primeiros s,
  mesmo clipe não volta antes de 60 s, 1,5–5 s cada, começa 2 quadros antes da
  palavra-âncora; modos tela inteira, dividida e dividida suave. Sem áudio do B-roll.

## 11. Visão [obs, `bin/visao`]
- Rosto: caixas de rosto (Windows FaceDetector / Apple Vision).
- Pessoa: MediaPipe Selfie Segmentation (Apache 2.0), máscara alfa.
- **Para o Aurea:** usar o que o app já tem — BackgroundRemoval do motor, ML Kit
  (Android) e Vision (iOS) para rosto — idêntico nas duas plataformas.

## 12. Para o Aurea (decisões)
- Legendas: as AutoCaptions do Aurea, com tempo por palavra da transcrição já no
  tempo da cópia cortada, posição que evita rostos e fica na área segura do Reels.
- Tudo determinístico (mesmo vídeo → mesma edição), uma etapa de desfazer, a
  original preservada.
