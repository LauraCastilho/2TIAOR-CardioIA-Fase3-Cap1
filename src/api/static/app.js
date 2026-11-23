const $ = (id) => document.getElementById(id);
let arquivoAtual = null;
let modeloAtual = null;
let ultimoResultado = null;
let previaUrl = null;

const pct = (v) => `${(v * 100).toFixed(1)}%`;

async function carregarModelos() {
  const r = await fetch("/api/modelos").then((r) => r.json());
  const seg = $("modelos");
  seg.innerHTML = "";
  r.modelos.forEach((m) => {
    const b = document.createElement("button");
    b.textContent = m.nome;
    b.onclick = () => selecionarModelo(m.id);
    b.dataset.id = m.id;
    seg.appendChild(b);
    if (m.padrao) modeloAtual = m.id;
  });
  selecionarModelo(modeloAtual || r.modelos[0].id);

  const nomes = { acuracia: "Acurácia", precisao: "Precisão", recall_sensibilidade: "Recall", especificidade: "Especificidade", f1: "F1-score", auc_roc: "AUC-ROC" };
  const tab = $("tabela-metricas");
  tab.innerHTML = `<tr><th>Modelo</th>${Object.values(nomes).map((n) => `<th>${n}</th>`).join("")}<th>Limiar</th></tr>` +
    r.modelos.map((m) => `<tr><td>${m.nome}</td>${Object.keys(nomes).map((k) => `<td>${m.metricas_teste[k].toFixed(3)}</td>`).join("")}<td>${m.limiar}</td></tr>`).join("");
}

function selecionarModelo(id) {
  modeloAtual = id;
  document.querySelectorAll("#modelos button").forEach((b) => b.classList.toggle("sel", b.dataset.id === id));
  if (arquivoAtual && ultimoResultado) analisar();
}

async function carregarAmostras() {
  const lista = await fetch("/api/amostras").then((r) => r.json());
  $("amostras").innerHTML = "";
  lista.forEach((a) => {
    const b = document.createElement("button");
    b.innerHTML = `<img src="${a.url}" alt=""><span>real: ${a.rotulo}</span>`;
    b.onclick = async () => {
      const blob = await fetch(a.url).then((r) => r.blob());
      definirArquivo(new File([blob], a.arquivo, { type: "image/png" }));
      analisar();
    };
    $("amostras").appendChild(b);
  });
}

function definirArquivo(f) {
  arquivoAtual = f;
  ultimoResultado = null;
  if (previaUrl) URL.revokeObjectURL(previaUrl);
  previaUrl = URL.createObjectURL(f);
  $("previa").src = previaUrl;
  $("previa").hidden = false;
  $("zona-texto").hidden = true;
  $("analisar").disabled = false;
}

async function analisar() {
  if (!arquivoAtual) return;
  $("resultado").hidden = false;
  $("carregando").hidden = false;
  $("conteudo").hidden = true;
  $("erro").hidden = true;
  $("resultado").scrollIntoView({ behavior: "smooth", block: "start" });
  const fd = new FormData();
  fd.append("imagem", arquivoAtual);
  fd.append("modelo", modeloAtual);
  try {
    const resp = await fetch("/api/classificar", { method: "POST", body: fd });
    const r = await resp.json();
    if (!resp.ok) throw new Error(r.erro || "Falha na análise");
    ultimoResultado = r;
    mostrar(r);
  } catch (e) {
    $("erro").textContent = e.message;
    $("erro").hidden = false;
  } finally {
    $("carregando").hidden = true;
  }
}

function mostrar(r) {
  $("faixa").className = `faixa ${r.classe}`;
  $("classe").textContent = r.classe;
  $("descricao").textContent = r.descricao;
  atualizarImagem();
  const cores = { normal: "var(--verde)", cardiomegalia: "var(--vermelho)" };
  $("barras").innerHTML = Object.entries(r.probabilidades).map(([c, p]) => `
    <div class="barra">
      <div class="topo"><span>${c}</span><strong>${pct(p)}</strong></div>
      <div class="trilho"><div class="valor" style="width:${p * 100}%;background:${cores[c]}"></div>
      ${c === "cardiomegalia" ? `<div class="marca-limiar" style="left:${r.limiar * 100}%" title="limiar ${r.limiar}"></div>` : ""}</div>
    </div>`).join("");
  $("confianca").textContent = r.confianca;
  $("limiar").textContent = `${r.limiar} (probabilidade de cardiomegalia ≥ limiar ⇒ positivo)`;
  $("modelo-usado").textContent = r.modelo.nome;
  $("tempo").textContent = `${r.tempo_ms} ms`;
  $("aviso").textContent = `⚠️ ${r.aviso}`;
  $("conteudo").hidden = false;
}

function atualizarImagem() {
  if (!ultimoResultado) return;
  $("img-gradcam").src = $("mostrar-calor").checked ? `data:image/png;base64,${ultimoResultado.gradcam_png_base64}` : previaUrl;
}

$("arquivo").addEventListener("change", (e) => e.target.files[0] && definirArquivo(e.target.files[0]));
$("analisar").addEventListener("click", analisar);
$("mostrar-calor").addEventListener("change", atualizarImagem);
const zona = $("zona");
["dragenter", "dragover"].forEach((ev) => zona.addEventListener(ev, (e) => { e.preventDefault(); zona.classList.add("ativa"); }));
["dragleave", "drop"].forEach((ev) => zona.addEventListener(ev, (e) => { e.preventDefault(); zona.classList.remove("ativa"); }));
zona.addEventListener("drop", (e) => e.dataTransfer.files[0] && definirArquivo(e.dataTransfer.files[0]));

carregarModelos();
carregarAmostras();
