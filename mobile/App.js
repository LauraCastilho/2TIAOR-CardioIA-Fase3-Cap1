// CardioIA · Fase 4 — app React Native (Expo) do Assistente Cardiológico Virtual.
// Tela 1 (Envio): escolhe uma radiografia (galeria, câmera ou exemplo) e o modelo.
// Tela 2 (Resultado): categoria detectada pela CNN, probabilidades, confiança e Grad-CAM.
// O app conversa com o backend Flask em src/api (POST /api/classificar).
import { useEffect, useState } from "react";
import {
  ActivityIndicator, Image, Platform, Pressable, SafeAreaView, ScrollView,
  StyleSheet, Switch, Text, TextInput, View,
} from "react-native";
import { StatusBar } from "expo-status-bar";
import * as ImagePicker from "expo-image-picker";

const URL_PADRAO =
  process.env.EXPO_PUBLIC_API_URL || (Platform.OS === "web" ? "http://localhost:5000" : "http://192.168.0.10:5000");

const CORES = {
  vermelho: "#c0392b", vermelhoClaro: "#fdecea", verde: "#1e8449", verdeClaro: "#e9f7ef",
  azul: "#2471a3", fundo: "#f5f6f8", cartao: "#ffffff", texto: "#1f2933", suave: "#616e7c", borda: "#e4e7eb",
};
const NOMES_CLASSE = { normal: "Normal", cardiomegalia: "Cardiomegalia" };

export default function App() {
  const [apiUrl, setApiUrl] = useState(URL_PADRAO);
  const [status, setStatus] = useState("verificando");
  const [modelos, setModelos] = useState([]);
  const [modelo, setModelo] = useState(null);
  const [amostras, setAmostras] = useState([]);
  const [imagem, setImagem] = useState(null);       // { uri, nome, tipo, file?, amostra? }
  const [carregando, setCarregando] = useState(false);
  const [resultado, setResultado] = useState(null);
  const [erro, setErro] = useState(null);

  async function conectar() {
    setStatus("verificando");
    setErro(null);
    try {
      const base = apiUrl.replace(/\/$/, "");
      const m = await fetch(`${base}/api/modelos`).then((r) => r.json());
      setModelos(m.modelos);
      setModelo((atual) => atual || (m.modelos.find((x) => x.padrao) || m.modelos[0]).id);
      setAmostras(await fetch(`${base}/api/amostras`).then((r) => r.json()));
      setStatus("online");
    } catch {
      setStatus("offline");
    }
  }
  useEffect(() => { conectar(); }, []);

  async function escolher(origem) {
    setErro(null);
    if (origem === "camera") {
      const perm = await ImagePicker.requestCameraPermissionsAsync();
      if (!perm.granted) { setErro("Permissão da câmera negada."); return; }
    }
    const opcoes = { mediaTypes: ["images"], quality: 1, allowsEditing: false };
    const r = origem === "camera"
      ? await ImagePicker.launchCameraAsync(opcoes)
      : await ImagePicker.launchImageLibraryAsync(opcoes);
    if (r.canceled || !r.assets?.length) return;
    const a = r.assets[0];
    const nome = a.fileName || `radiografia.${(a.mimeType || "image/jpeg").includes("png") ? "png" : "jpg"}`;
    setImagem({ uri: a.uri, nome, tipo: a.mimeType || "image/jpeg", file: a.file });
  }

  async function analisar(img = imagem, mid = modelo) {
    if (!img) return;
    setCarregando(true);
    setErro(null);
    const fd = new FormData();
    if (img.amostra) {
      fd.append("amostra", img.amostra);
    } else if (Platform.OS === "web") {
      fd.append("imagem", img.file || (await (await fetch(img.uri)).blob()), img.nome);
    } else {
      fd.append("imagem", { uri: img.uri, name: img.nome, type: img.tipo });
    }
    fd.append("modelo", mid);
    try {
      const resp = await fetch(`${apiUrl.replace(/\/$/, "")}/api/classificar`, { method: "POST", body: fd });
      const r = await resp.json();
      if (!resp.ok) throw new Error(r.erro || "Falha na análise.");
      setResultado(r);
    } catch (e) {
      setErro(e.message.includes("Network") || e.message.includes("fetch")
        ? "Não foi possível falar com o servidor. Confira o endereço da API." : e.message);
    } finally {
      setCarregando(false);
    }
  }

  if (resultado) {
    return (
      <TelaResultado
        resultado={resultado} imagem={imagem} modelos={modelos}
        trocarModelo={(mid) => { setModelo(mid); analisar(imagem, mid); }}
        carregando={carregando}
        voltar={() => setResultado(null)}
      />
    );
  }

  return (
    <SafeAreaView style={s.tela}>
      <StatusBar style="light" />
      <Cabecalho />
      <ScrollView contentContainerStyle={s.conteudo}>
        <View style={s.cartao}>
          <Text style={s.titulo}>Servidor</Text>
          <View style={s.linha}>
            <TextInput style={s.input} value={apiUrl} onChangeText={setApiUrl} autoCapitalize="none"
              autoCorrect={false} keyboardType="url" placeholder="http://IP-do-computador:5000" />
            <Pressable style={s.botaoPequeno} onPress={conectar}><Text style={s.botaoPequenoTxt}>Conectar</Text></Pressable>
          </View>
          <Text style={[s.status, { color: status === "online" ? CORES.verde : status === "offline" ? CORES.vermelho : CORES.suave }]}>
            {status === "online" ? "● conectado" : status === "offline" ? "● sem conexão com a API" : "● verificando…"}
          </Text>
        </View>

        <View style={s.cartao}>
          <Text style={s.titulo}>1. Radiografia de tórax</Text>
          {imagem ? (
            <Image source={{ uri: imagem.uri }} style={s.previa} resizeMode="contain" />
          ) : (
            <View style={s.vazio}><Text style={s.suave}>Nenhuma imagem selecionada</Text></View>
          )}
          <View style={s.linha}>
            <Botao texto="Galeria" onPress={() => escolher("galeria")} secundario />
            {Platform.OS !== "web" && <Botao texto="Câmera" onPress={() => escolher("camera")} secundario />}
          </View>

          {amostras.length > 0 && (
            <>
              <Text style={s.subtitulo}>ou use um exame de exemplo</Text>
              <ScrollView horizontal showsHorizontalScrollIndicator={false}>
                {amostras.map((a) => (
                  <Pressable key={a.arquivo} style={[s.amostra, imagem?.amostra === a.arquivo && s.amostraSel]}
                    onPress={() => setImagem({ uri: a.url, nome: a.arquivo, tipo: "image/png", amostra: a.arquivo })}>
                    <Image source={{ uri: a.url }} style={s.amostraImg} />
                    <Text style={s.amostraTxt}>real: {a.rotulo}</Text>
                  </Pressable>
                ))}
              </ScrollView>
            </>
          )}
        </View>

        <View style={s.cartao}>
          <Text style={s.titulo}>2. Modelo</Text>
          <SeletorModelo modelos={modelos} atual={modelo} onChange={setModelo} />
        </View>

        {erro && <Text style={s.erro}>{erro}</Text>}
        <Botao texto={carregando ? "Analisando…" : "Analisar radiografia"} onPress={() => analisar()}
          desabilitado={!imagem || !modelo || carregando || status !== "online"} />
        {carregando && <ActivityIndicator style={{ marginTop: 12 }} color={CORES.vermelho} />}
        <Text style={s.rodape}>Protótipo acadêmico (FIAP · CardioIA). Não substitui avaliação médica.</Text>
      </ScrollView>
    </SafeAreaView>
  );
}

function TelaResultado({ resultado: r, imagem, modelos, trocarModelo, carregando, voltar }) {
  const [calor, setCalor] = useState(true);
  const positivo = r.classe === "cardiomegalia";
  return (
    <SafeAreaView style={s.tela}>
      <StatusBar style="light" />
      <Cabecalho />
      <ScrollView contentContainerStyle={s.conteudo}>
        <View style={[s.faixa, { backgroundColor: positivo ? CORES.vermelhoClaro : CORES.verdeClaro,
                                 borderLeftColor: positivo ? CORES.vermelho : CORES.verde }]}>
          <Text style={s.suave}>Categoria detectada</Text>
          <Text style={[s.classe, { color: positivo ? CORES.vermelho : CORES.verde }]}>{NOMES_CLASSE[r.classe]}</Text>
          <Text style={s.suave}>{r.descricao}</Text>
        </View>

        <View style={s.cartao}>
          <Image
            source={{ uri: calor ? `data:image/png;base64,${r.gradcam_png_base64}` : imagem?.uri }}
            style={s.resultadoImg} resizeMode="contain" />
          <View style={[s.linha, { justifyContent: "space-between" }]}>
            <Text style={s.texto}>Mapa de calor (Grad-CAM)</Text>
            <Switch value={calor} onValueChange={setCalor} trackColor={{ true: CORES.vermelho }} />
          </View>
          <Text style={s.legenda}>Vermelho/amarelo = regiões que mais pesaram na decisão do modelo.</Text>
        </View>

        <View style={s.cartao}>
          <Text style={s.titulo}>Probabilidade por categoria</Text>
          {Object.entries(r.probabilidades).map(([c, p]) => (
            <View key={c} style={{ marginBottom: 12 }}>
              <View style={[s.linha, { justifyContent: "space-between", marginBottom: 4 }]}>
                <Text style={s.texto}>{NOMES_CLASSE[c]}</Text>
                <Text style={[s.texto, { fontWeight: "700" }]}>{(p * 100).toFixed(1)}%</Text>
              </View>
              <View style={s.trilho}>
                <View style={[s.barra, { width: `${p * 100}%`, backgroundColor: c === "cardiomegalia" ? CORES.vermelho : CORES.verde }]} />
                {c === "cardiomegalia" && <View style={[s.limiar, { left: `${r.limiar * 100}%` }]} />}
              </View>
            </View>
          ))}
          <Info rotulo="Confiança" valor={r.confianca} />
          <Info rotulo="Limiar de decisão" valor={String(r.limiar)} />
          <Info rotulo="Modelo" valor={r.modelo.nome} />
          <Info rotulo="Tempo de resposta" valor={`${r.tempo_ms} ms`} />
        </View>

        <View style={s.cartao}>
          <Text style={s.titulo}>Comparar com outro modelo</Text>
          <SeletorModelo modelos={modelos} atual={r.modelo.id} onChange={trocarModelo} />
          {carregando && <ActivityIndicator style={{ marginTop: 10 }} color={CORES.vermelho} />}
        </View>

        <Text style={s.aviso}>⚠️ {r.aviso}</Text>
        <Botao texto="Nova análise" onPress={voltar} />
      </ScrollView>
    </SafeAreaView>
  );
}

function Cabecalho() {
  return (
    <View style={s.cabecalho}>
      <Text style={s.marca}>♥ CardioIA</Text>
      <Text style={s.marcaSub}>Assistente Cardiológico Virtual · raio-X de tórax</Text>
    </View>
  );
}

function SeletorModelo({ modelos, atual, onChange }) {
  return (
    <View style={s.segmentado}>
      {modelos.map((m) => (
        <Pressable key={m.id} onPress={() => onChange(m.id)} style={[s.segmento, atual === m.id && s.segmentoSel]}>
          <Text style={[s.segmentoTxt, atual === m.id && { color: "#fff" }]}>{m.nome}</Text>
          <Text style={[s.segmentoSub, atual === m.id && { color: "#dbe9f5" }]}>AUC {m.metricas_teste.auc_roc.toFixed(2)}</Text>
        </Pressable>
      ))}
    </View>
  );
}

function Botao({ texto, onPress, desabilitado, secundario }) {
  return (
    <Pressable onPress={onPress} disabled={desabilitado}
      style={[secundario ? s.botaoSec : s.botao, desabilitado && { opacity: 0.45 }]}>
      <Text style={secundario ? s.botaoSecTxt : s.botaoTxt}>{texto}</Text>
    </Pressable>
  );
}

function Info({ rotulo, valor }) {
  return (
    <View style={[s.linha, { justifyContent: "space-between", marginTop: 6 }]}>
      <Text style={s.suave}>{rotulo}</Text>
      <Text style={[s.texto, { fontWeight: "600" }]}>{valor}</Text>
    </View>
  );
}

const s = StyleSheet.create({
  tela: { flex: 1, backgroundColor: CORES.fundo },
  cabecalho: { backgroundColor: CORES.vermelho, paddingTop: Platform.OS === "android" ? 36 : 14, paddingBottom: 14, paddingHorizontal: 18 },
  marca: { color: "#fff", fontSize: 22, fontWeight: "700" },
  marcaSub: { color: "#fff", opacity: 0.9, fontSize: 13 },
  conteudo: { padding: 16, paddingBottom: 40, gap: 14, maxWidth: 640, width: "100%", alignSelf: "center" },
  cartao: { backgroundColor: CORES.cartao, borderRadius: 12, padding: 16, borderWidth: 1, borderColor: CORES.borda },
  titulo: { fontSize: 16, fontWeight: "700", color: CORES.texto, marginBottom: 10 },
  subtitulo: { fontSize: 13, color: CORES.suave, marginTop: 14, marginBottom: 8 },
  texto: { color: CORES.texto, fontSize: 14 },
  suave: { color: CORES.suave, fontSize: 13 },
  linha: { flexDirection: "row", alignItems: "center", gap: 8 },
  input: { flex: 1, borderWidth: 1, borderColor: CORES.borda, borderRadius: 8, paddingHorizontal: 10, paddingVertical: 8, color: CORES.texto },
  status: { marginTop: 8, fontSize: 13, fontWeight: "600" },
  previa: { width: "100%", height: 240, borderRadius: 8, backgroundColor: "#000", marginBottom: 12 },
  vazio: { height: 160, borderRadius: 8, borderWidth: 2, borderStyle: "dashed", borderColor: CORES.borda, alignItems: "center", justifyContent: "center", marginBottom: 12 },
  amostra: { width: 92, marginRight: 8, borderWidth: 1, borderColor: CORES.borda, borderRadius: 8, padding: 4 },
  amostraSel: { borderColor: CORES.vermelho, borderWidth: 2 },
  amostraImg: { width: "100%", height: 80, borderRadius: 4 },
  amostraTxt: { fontSize: 10, color: CORES.suave, marginTop: 2, textAlign: "center" },
  segmentado: { flexDirection: "row", borderWidth: 1, borderColor: CORES.borda, borderRadius: 8, overflow: "hidden" },
  segmento: { flex: 1, paddingVertical: 10, alignItems: "center" },
  segmentoSel: { backgroundColor: CORES.azul },
  segmentoTxt: { color: CORES.texto, fontWeight: "600" },
  segmentoSub: { color: CORES.suave, fontSize: 11 },
  botao: { backgroundColor: CORES.vermelho, borderRadius: 10, paddingVertical: 14, alignItems: "center" },
  botaoTxt: { color: "#fff", fontWeight: "700", fontSize: 16 },
  botaoSec: { flex: 1, borderWidth: 1, borderColor: CORES.vermelho, borderRadius: 10, paddingVertical: 10, alignItems: "center" },
  botaoSecTxt: { color: CORES.vermelho, fontWeight: "600" },
  botaoPequeno: { backgroundColor: CORES.azul, borderRadius: 8, paddingHorizontal: 12, paddingVertical: 9 },
  botaoPequenoTxt: { color: "#fff", fontWeight: "600" },
  faixa: { borderRadius: 12, padding: 16, borderLeftWidth: 6 },
  classe: { fontSize: 26, fontWeight: "800", marginVertical: 2 },
  resultadoImg: { width: "100%", aspectRatio: 1, borderRadius: 8, backgroundColor: "#000", marginBottom: 10 },
  legenda: { fontSize: 12, color: CORES.suave, marginTop: 4 },
  trilho: { height: 14, backgroundColor: CORES.borda, borderRadius: 7, overflow: "hidden", position: "relative" },
  barra: { height: "100%", borderRadius: 7 },
  limiar: { position: "absolute", top: 0, bottom: 0, width: 2, backgroundColor: CORES.texto },
  erro: { color: CORES.vermelho, backgroundColor: CORES.vermelhoClaro, padding: 10, borderRadius: 8 },
  aviso: { fontSize: 12, color: CORES.suave, lineHeight: 18 },
  rodape: { fontSize: 11, color: CORES.suave, textAlign: "center", marginTop: 16 },
});
