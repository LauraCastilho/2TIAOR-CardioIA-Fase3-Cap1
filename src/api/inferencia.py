"""Carregamento dos modelos treinados no notebook 02, inferência e Grad-CAM.

O pré-processamento repete exatamente o pipeline do notebook 01:
conversão para escala de cinza 8 bits (1 canal) e redimensionamento para 224x224 com Lanczos.
A normalização fica dentro do próprio modelo salvo.
"""
import base64
import io
import json
import os
import threading
import time
from pathlib import Path

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import numpy as np
from PIL import Image, UnidentifiedImageError
from matplotlib import colormaps

import keras
import tensorflow as tf

RAIZ = Path(__file__).resolve().parents[2]
MODEL_DIR = Path(os.environ.get("CARDIOIA_MODEL_DIR", RAIZ / "modelos"))

AVISO = ("Resultado gerado por um protótipo acadêmico de IA, treinado com o dataset público NIH ChestX-ray14. "
         "É apoio à decisão e não substitui a avaliação de um médico.")
DESCRICOES = {
    "normal": "Sem sinais de aumento da área cardíaca",
    "cardiomegalia": "Sinais de aumento da área cardíaca (cardiomegalia)",
}


class ImagemInvalida(ValueError):
    pass


class Classificador:
    def __init__(self, model_dir=MODEL_DIR):
        self.model_dir = Path(model_dir)
        self.config = json.loads((self.model_dir / "config_modelos.json").read_text(encoding="utf-8"))
        self.tamanho = self.config["tamanho_entrada"]
        self.classes = self.config["classes"]
        self._modelos, self._gradcam = {}, {}
        self._lock = threading.Lock()

    # ---------- modelos ----------
    def modelos_disponiveis(self):
        return [mid for mid, m in self.config["modelos"].items() if (self.model_dir / m["arquivo"]).exists()]

    def info_modelos(self):
        saida = []
        for mid in self.modelos_disponiveis():
            m = self.config["modelos"][mid]
            met = m["metricas_teste"]
            saida.append({
                "id": mid, "nome": m["nome"], "limiar": round(m["limiar"], 3),
                "padrao": mid == self.config["modelo_padrao"],
                "metricas_teste": {k: round(met[k], 3) for k in
                                   ("acuracia", "precisao", "recall_sensibilidade", "especificidade", "f1", "auc_roc")},
            })
        return saida

    def _carregar(self, mid):
        if mid not in self._modelos:
            cfg = self.config["modelos"][mid]
            modelo = keras.saving.load_model(self.model_dir / cfg["arquivo"], compile=False)
            self._modelos[mid] = modelo
            self._gradcam[mid] = keras.Model(modelo.inputs, [modelo.get_layer(cfg["camada_gradcam"]).output, modelo.output])
        return self._modelos[mid], self._gradcam[mid]

    def aquecer(self):
        for mid in self.modelos_disponiveis():
            self._carregar(mid)
            self.classificar(Image.new("L", (self.tamanho, self.tamanho), 128), mid)

    # ---------- pré-processamento ----------
    def preprocessar(self, imagem):
        if not isinstance(imagem, Image.Image):
            try:
                imagem = Image.open(io.BytesIO(imagem))
                imagem.load()
            except (UnidentifiedImageError, OSError) as e:
                raise ImagemInvalida("Arquivo não é uma imagem válida (use PNG ou JPG).") from e
        img = imagem.convert("L").resize((self.tamanho, self.tamanho), Image.Resampling.LANCZOS)
        return np.asarray(img, dtype=np.float32)[..., None]

    # ---------- inferência + Grad-CAM ----------
    def classificar(self, imagem, modelo_id=None):
        mid = modelo_id or self.config["modelo_padrao"]
        if mid not in self.modelos_disponiveis():
            raise KeyError(f"Modelo '{mid}' indisponível. Opções: {self.modelos_disponiveis()}")
        t0 = time.perf_counter()
        x = self.preprocessar(imagem)
        with self._lock:
            _, grad_model = self._carregar(mid)
            entrada = tf.convert_to_tensor(x[None])
            with tf.GradientTape() as tape:
                mapas, pred = grad_model(entrada, training=False)
                alvo = pred[:, 0]
            grads = tape.gradient(alvo, mapas)
        p = float(pred[0, 0])
        pesos = tf.reduce_mean(grads, axis=(1, 2))
        cam = tf.nn.relu(tf.reduce_sum(mapas * pesos[:, None, None, :], axis=-1))[0]
        cam = (cam / (tf.reduce_max(cam) + 1e-9)).numpy()

        limiar = self.config["modelos"][mid]["limiar"]
        classe = "cardiomegalia" if p >= limiar else "normal"
        distancia = abs(p - limiar)
        confianca = "alta" if distancia > 0.3 else "moderada" if distancia > 0.12 else "baixa"
        return {
            "classe": classe,
            "descricao": DESCRICOES[classe],
            "probabilidades": {"normal": round(1 - p, 4), "cardiomegalia": round(p, 4)},
            "limiar": round(limiar, 3),
            "confianca": confianca,
            "modelo": {"id": mid, "nome": self.config["modelos"][mid]["nome"]},
            "gradcam_png_base64": self._sobrepor(x[..., 0], cam),
            "tempo_ms": round((time.perf_counter() - t0) * 1000),
            "aviso": AVISO,
        }

    def _sobrepor(self, img, cam, alpha=0.38):
        cam_img = Image.fromarray(np.uint8(cam * 255)).resize((self.tamanho, self.tamanho), Image.Resampling.BILINEAR)
        calor = colormaps["jet"](np.asarray(cam_img) / 255.0)[..., :3]
        base = np.repeat(img[..., None] / 255.0, 3, axis=-1)
        mistura = np.uint8(np.clip((1 - alpha) * base + alpha * calor, 0, 1) * 255)
        buf = io.BytesIO()
        Image.fromarray(mistura).save(buf, format="PNG")
        return base64.b64encode(buf.getvalue()).decode("ascii")
