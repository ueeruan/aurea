// Gerador de texto animado — exemplo de plugin com painel e script do AUREA.
// Mostra: ler os controles do painel (ui.get), criar camadas e keyframes com a
// API no estilo do After Effects, um passo de desfazer por ação e textos do
// plugin.json em todos os idiomas (app.t).

const SUBIDA = 80   // px que o texto sobe no estilo "Subir e aparecer"

function composicao() {
  const comp = app.project.activeItem
  if (!comp) throw new Error(app.t("semProjeto"))
  return comp
}

// Anima uma camada a partir de `inicio` (segundos da composição).
function animar(camada, inicio) {
  const estilo = ui.get("estilo")
  const t0 = inicio
  const t1 = inicio + ui.get("duracao")
  const pos = camada.position.value

  camada.opacity.setValueAtTime(t0, 0)
  camada.opacity.setValueAtTime(t1, 100)
  if (estilo === 0) {
    camada.position.setValueAtTime(t0, [pos[0], pos[1] + SUBIDA])
    camada.position.setValueAtTime(t1, pos)
  } else if (estilo === 1) {
    camada.scale.setValueAtTime(t0, [0, 0])
    camada.scale.setValueAtTime(t1, [100, 100])
  } else if (estilo === 2) {
    camada.position.setValueAtTime(t0, [pos[0] - 300, pos[1]])
    camada.position.setValueAtTime(t1, pos)
  } else {
    camada.rotation.setValueAtTime(t0, -90)
    camada.rotation.setValueAtTime(t1, 0)
  }
  // Entrada suave nas chaves que acabaram de ser criadas.
  for (const prop of [camada.opacity, camada.position, camada.scale, camada.rotation]) {
    for (let k = 1; k <= prop.numKeys; k++) prop.setInterpolationTypeAtKey(k, "ease")
  }
}

// Botão "Gerar".
function gerar() {
  const comp = composicao()
  const texto = ui.get("texto").trim()
  if (!texto) {
    ui.alert(app.t("vazio"))
    return
  }
  const partes = ui.get("porPalavra") ? texto.split(" ").filter(p => p.length > 0) : [texto]
  const tamanho = ui.get("tamanho")
  const atraso = ui.get("atraso")

  app.beginUndoGroup(app.t("acao"))
  const nomes = []
  partes.forEach((parte, i) => {
    const camada = comp.layers.addText(parte)
    camada.name = parte
    camada.fontSize = tamanho
    camada.fillColor = ui.get("cor")
    if (partes.length > 1) {
      // Palavras empilhadas no centro, uma por linha.
      const passo = tamanho * 1.2
      camada.position.setValue([comp.width / 2, comp.height / 2 + (i - (partes.length - 1) / 2) * passo])
    }
    animar(camada, comp.time + i * atraso)
    nomes.push(camada.name)
  })
  app.endUndoGroup()

  storage.set("ultimoTexto", texto)   // o painel lembra na próxima vez
  ui.setText("info", app.t("pronto", nomes.length))
  ui.toast(app.t("pronto", nomes.length))
  return nomes
}

// Botão na barra da camada: anima as camadas escolhidas.
function animarSelecionadas() {
  const comp = composicao()
  const camadas = comp.selectedLayers
  if (camadas.length === 0) {
    ui.alert(app.t("semSelecao"))
    return
  }
  camadas.forEach((camada, i) => animar(camada, comp.time + i * ui.get("atraso")))
  ui.toast(app.t("animadas", camadas.length))
}

function mudouEstilo(valor) {
  ui.setText("info", app.t("estilo" + valor, SUBIDA))
}

// O painel abriu: lembra o último texto usado.
function abriu() {
  const ultimo = storage.get("ultimoTexto")
  if (ultimo) ui.set("texto", ultimo)
}
