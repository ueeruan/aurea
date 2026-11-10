// Roda a cada quadro, sem acesso ao projeto: recebe o tempo e devolve parâmetros.
function onFrame(f) {
  return { amount: 0.5 + 0.5 * Math.sin(f.time * 3) };
}
