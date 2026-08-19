import os

import numpy as np
import pandas as pd
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

SEED = 7
N_ECG = 150
DURACAO_S = 10
FS = 250  # amostras por segundo

SAIDA_DIR = "../data/imagens_ecg"
AMOSTRAS_DIR = "../docs/amostras_ecg"
N_AMOSTRAS_PREVIEW = 6

rng = np.random.default_rng(SEED)

# posicao (fracao do intervalo RR) e amplitude/largura de cada onda
ONDAS = {
    "P": (0.18, 0.10, 0.010),
    "Q": (0.40, -0.12, 0.006),
    "R": (0.44, 1.00, 0.006),
    "S": (0.48, -0.20, 0.006),
    "T": (0.68, 0.28, 0.020),
}


def batimento(t_rel, amp_extra=None):
    y = np.zeros_like(t_rel)
    for nome, (pos, amp, largura) in ONDAS.items():
        a = amp if amp_extra is None else amp * amp_extra.get(nome, 1.0)
        y += a * np.exp(-((t_rel - pos) ** 2) / (2 * largura ** 2))
    return y


def gerar_tira(hr_bpm, irregular, isquemia, ruido):
    n_amostras = DURACAO_S * FS
    t = np.linspace(0, DURACAO_S, n_amostras)
    sinal = np.zeros(n_amostras)

    rr_medio = 60.0 / hr_bpm
    t_atual = 0.0
    amp_extra = {"T": 0.55} if isquemia else None
    while t_atual < DURACAO_S:
        rr = rr_medio
        if irregular:
            rr *= rng.normal(1.0, 0.18)
            rr = max(rr, 0.32)
        janela = (t >= t_atual) & (t < t_atual + rr)
        t_rel = (t[janela] - t_atual) / rr
        sinal[janela] += batimento(t_rel, amp_extra)
        t_atual += rr

    sinal += rng.normal(0, ruido, n_amostras)
    return t, sinal


def classificar(hr_bpm, irregular, isquemia):
    if irregular:
        return "Irregular / possivel arritmia"
    if isquemia:
        return "Alteracao de repolarizacao (onda T ampliada)"
    if hr_bpm < 60:
        return "Bradicardia"
    if hr_bpm > 100:
        return "Taquicardia"
    return "Ritmo normal"


def salvar_figura(t, sinal, caminho):
    fig, ax = plt.subplots(figsize=(9, 3), dpi=120)
    ax.set_facecolor("#fff5f5")
    ax.plot(t, sinal, color="#8b0000", linewidth=1.1)

    ax.set_xlim(0, DURACAO_S)
    ax.set_xticks(np.arange(0, DURACAO_S + 0.2, 0.2), minor=True)
    ax.set_xticks(np.arange(0, DURACAO_S + 1, 1))
    ax.set_yticks(np.arange(-0.5, 1.6, 0.1), minor=True)
    ax.set_yticks(np.arange(-0.5, 1.6, 0.5))
    ax.grid(which="minor", color="#f3b3b3", linewidth=0.4)
    ax.grid(which="major", color="#e08080", linewidth=0.8)
    ax.set_ylim(-0.5, 1.5)

    ax.set_xlabel("Tempo (s)")
    ax.set_ylabel("Amplitude (mV)")
    fig.tight_layout()
    fig.savefig(caminho)
    plt.close(fig)


def main():
    os.makedirs(SAIDA_DIR, exist_ok=True)
    os.makedirs(AMOSTRAS_DIR, exist_ok=True)

    metadados = []
    for i in range(1, N_ECG + 1):
        irregular = rng.random() < 0.15
        isquemia = (not irregular) and rng.random() < 0.12
        hr_bpm = rng.normal(78, 18)
        hr_bpm = float(np.clip(hr_bpm, 42, 150))
        ruido = rng.uniform(0.01, 0.035)

        t, sinal = gerar_tira(hr_bpm, irregular, isquemia, ruido)
        nome_arquivo = f"ecg_paciente_{i:04d}.png"
        salvar_figura(t, sinal, os.path.join(SAIDA_DIR, nome_arquivo))

        if i <= N_AMOSTRAS_PREVIEW:
            salvar_figura(t, sinal, os.path.join(AMOSTRAS_DIR, nome_arquivo))

        metadados.append({
            "arquivo": nome_arquivo,
            "paciente_id": i,
            "frequencia_cardiaca_estimada_bpm": round(hr_bpm, 1),
            "ritmo": "Irregular" if irregular else "Regular",
            "classificacao_visual": classificar(hr_bpm, irregular, isquemia),
        })

    pd.DataFrame(metadados).to_csv(
        os.path.join(SAIDA_DIR, "_metadados_ecg.csv"), index=False, encoding="utf-8"
    )
    print(f"{N_ECG} imagens de ECG geradas em {SAIDA_DIR}")
    print(f"{N_AMOSTRAS_PREVIEW} amostras de preview copiadas para {AMOSTRAS_DIR}")


if __name__ == "__main__":
    main()
