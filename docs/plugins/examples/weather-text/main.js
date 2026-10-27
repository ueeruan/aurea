// Clima na tela — exemplo de plugin que traz dados de uma API pública.
// Mostra: fetch só para o domínio declarado em "permissions.network"
// (open-meteo.com e os subdomínios dele), lista preenchida pelo script,
// storage e uma camada de texto criada com o resultado.
//
// Sem a permissão de rede concedida pela pessoa, fetch lança PermissionError
// e o painel mostra o erro: nenhum byte sai do aparelho.

let lugares = []      // resultados da última busca
let leitura = ""      // texto pronto para virar camada

function idioma() {
  return app.language.split("-")[0]
}

function descricao(codigo) {
  // Códigos WMO que a Open-Meteo devolve, agrupados.
  if (codigo === 0) return app.t("c0")
  if (codigo <= 3) return app.t("c1")
  if (codigo <= 48) return app.t("c45")
  if (codigo <= 57) return app.t("c51")
  if (codigo <= 67) return app.t("c61")
  if (codigo <= 77) return app.t("c71")
  if (codigo <= 82) return app.t("c80")
  return app.t("c95")
}

// Botão "Buscar": procura a cidade e mostra os resultados na lista.
function buscar() {
  const nome = ui.get("cidade").trim()
  if (!nome) return
  ui.setText("resultado", app.t("buscando"))
  let resposta
  try {
    resposta = fetch("https://geocoding-api.open-meteo.com/v1/search?count=5&format=json&name=" +
                     encodeURIComponent(nome) + "&language=" + idioma())
  } catch (e) {
    if (e.name === "NetworkError") { ui.setText("resultado", app.t("semRede")); return }
    throw e   // PermissionError e outros: o painel mostra
  }
  const dados = resposta.json()
  lugares = dados.results || []
  ui.setItems("lugares", lugares.map((l, i) => ({
    id: String(i),
    label: l.name,
    detail: [l.admin1, l.country_code].filter(x => x).join(", ")
  })))
  if (lugares.length === 0) {
    ui.setText("resultado", app.t("nada"))
    return
  }
  storage.set("cidade", nome)
  ui.set("lugares", "0")
  escolheu("0")
}

// Item da lista escolhido: busca o tempo agora.
function escolheu(id) {
  const lugar = lugares[Number(id)]
  if (!lugar) return
  const f = ui.get("unidade") === 1
  let resposta
  try {
    resposta = fetch("https://api.open-meteo.com/v1/forecast?current=temperature_2m,weather_code" +
                     "&latitude=" + lugar.latitude + "&longitude=" + lugar.longitude +
                     (f ? "&temperature_unit=fahrenheit" : ""))
  } catch (e) {
    if (e.name === "NetworkError") { ui.setText("resultado", app.t("semRede")); return }
    throw e
  }
  if (!resposta.ok) {
    ui.setText("resultado", app.t("semRede") + " (" + resposta.status + ")")
    return
  }
  const agora = resposta.json().current
  const temperatura = Math.round(agora.temperature_2m) + (f ? " °F" : " °C")
  leitura = app.t("agora", lugar.name, temperatura, descricao(agora.weather_code))
  ui.setText("resultado", leitura)
}

// Botão "Criar texto": a leitura vira uma camada de texto (um desfazer).
function criar() {
  if (!leitura) {
    ui.alert(app.t("escolha"))
    return
  }
  const comp = app.project.activeItem
  if (!comp) throw new Error(app.t("semProjeto"))
  app.beginUndoGroup(app.t("acao"))
  const camada = comp.layers.addText(leitura)
  camada.name = leitura
  camada.fontSize = Math.round(Math.min(comp.width, comp.height) * 0.06)
  camada.opacity.setValueAtTime(comp.time, 0)
  camada.opacity.setValueAtTime(comp.time + 0.5, 100)
  app.endUndoGroup()
  ui.toast(app.t("criado"))
}

function abriu() {
  const cidade = storage.get("cidade")
  if (cidade) ui.set("cidade", cidade)
}
