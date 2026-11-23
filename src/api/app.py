"""CardioIA · Fase 4 — API Flask do Assistente Cardiológico Virtual.

Serve duas coisas:
  * Interface web simples (GET /) para enviar uma radiografia e ver o resultado.
  * API REST (JSON) consumida pela interface web e pelo app React Native (pasta mobile/).

Rotas:
  GET  /                     interface web
  GET  /api/saude            status do serviço e modelos carregados
  GET  /api/modelos          modelos disponíveis, limiares e métricas de teste
  GET  /api/amostras         radiografias de exemplo (conjunto de teste)
  POST /api/classificar      multipart/form-data: imagem=<arquivo> (ou amostra=<nome do exemplo>),
                             modelo=<resnet50_tl|cnn_do_zero> (opcional)

Execução:  python src/api/app.py   (porta 5000, aceita conexões da rede local para o app mobile)
"""
import json
import os
import sys
from pathlib import Path

from flask import Flask, jsonify, render_template, request, send_from_directory
from flask_cors import CORS

sys.path.insert(0, str(Path(__file__).resolve().parent))
from inferencia import RAIZ, Classificador, ImagemInvalida  # noqa: E402

AMOSTRAS_DIR = RAIZ / "docs" / "amostras_raiox"
EXTENSOES = {".png", ".jpg", ".jpeg"}

app = Flask(__name__)
app.config["MAX_CONTENT_LENGTH"] = 10 * 1024 * 1024   # 10 MB por upload
app.json.ensure_ascii = False
CORS(app, resources={r"/api/*": {"origins": "*"}})
classificador = Classificador()


def erro(mensagem, status):
    return jsonify({"erro": mensagem}), status


@app.get("/")
def index():
    return render_template("index.html")


@app.get("/api/saude")
def saude():
    return jsonify({"status": "ok", "modelos": classificador.modelos_disponiveis(),
                    "modelo_padrao": classificador.config["modelo_padrao"]})


@app.get("/api/modelos")
def modelos():
    return jsonify({"classes": classificador.classes, "modelos": classificador.info_modelos(),
                    "dataset": classificador.config.get("dataset")})


@app.get("/api/amostras")
def amostras():
    meta_path = AMOSTRAS_DIR / "amostras.json"
    meta = json.loads(meta_path.read_text(encoding="utf-8")) if meta_path.exists() else []
    base = request.host_url.rstrip("/")
    return jsonify([{**m, "url": f"{base}/amostras/{m['arquivo']}"} for m in meta])


@app.get("/amostras/<path:nome>")
def amostra_arquivo(nome):
    return send_from_directory(AMOSTRAS_DIR, nome)


@app.post("/api/classificar")
def classificar():
    modelo_id = request.form.get("modelo") or None
    amostra = request.form.get("amostra")
    if amostra:
        # Atalho para o app/web: classificar uma das radiografias de exemplo pelo nome
        caminho = (AMOSTRAS_DIR / Path(amostra).name)
        if caminho.suffix.lower() not in EXTENSOES or not caminho.exists():
            return erro("Amostra não encontrada.", 404)
        dados = caminho.read_bytes()
    else:
        arquivo = request.files.get("imagem")
        if arquivo is None or arquivo.filename == "":
            return erro("Envie a radiografia no campo 'imagem' (multipart/form-data).", 400)
        if Path(arquivo.filename).suffix.lower() not in EXTENSOES:
            return erro("Formato não suportado. Use PNG ou JPG.", 415)
        dados = arquivo.read()
    try:
        resultado = classificador.classificar(dados, modelo_id)
    except ImagemInvalida as e:
        return erro(str(e), 422)
    except KeyError as e:
        return erro(str(e.args[0]), 400)
    return jsonify(resultado)


@app.errorhandler(413)
def muito_grande(_):
    return erro("Arquivo maior que 10 MB.", 413)


if __name__ == "__main__":
    print("Carregando modelos...")
    classificador.aquecer()
    porta = int(os.environ.get("PORT", 5000))
    print(f"CardioIA pronto em http://localhost:{porta}  (no celular use o IP desta máquina na rede local)")
    app.run(host="0.0.0.0", port=porta, debug=False)
