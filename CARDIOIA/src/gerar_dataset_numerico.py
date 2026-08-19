import numpy as np
import pandas as pd

SEED = 42
N_PACIENTES = 300
N_COM_ECG = 150  # subconjunto de pacientes que tambem tem ECG na Parte 3

rng = np.random.default_rng(SEED)


def sortear_categoria(rng, categorias, pesos):
    return rng.choice(categorias, p=pesos)


def gerar_dataset(n=N_PACIENTES):
    idade = rng.normal(55, 14, n).clip(18, 92).round().astype(int)
    sexo = rng.choice(["M", "F"], size=n, p=[0.52, 0.48])

    tabagista = rng.choice(["Sim", "Nao"], size=n, p=[0.22, 0.78])
    diabetico = rng.choice(["Sim", "Nao"], size=n, p=[0.18, 0.82])
    historico_familiar = rng.choice(["Sim", "Nao"], size=n, p=[0.30, 0.70])
    nivel_atividade = rng.choice(
        ["Sedentario", "Moderado", "Intenso"], size=n, p=[0.45, 0.40, 0.15]
    )

    # score de risco latente usado para correlacionar as demais variaveis
    risco = (
        (idade - 55) / 20
        + np.where(sexo == "M", 0.25, 0.0)
        + np.where(tabagista == "Sim", 0.9, 0.0)
        + np.where(diabetico == "Sim", 0.8, 0.0)
        + np.where(historico_familiar == "Sim", 0.6, 0.0)
        + np.where(nivel_atividade == "Sedentario", 0.4, 0.0)
        - np.where(nivel_atividade == "Intenso", 0.4, 0.0)
        + rng.normal(0, 0.6, n)
    )

    imc = (26 + risco * 1.3 + rng.normal(0, 3.2, n)).clip(16, 45).round(1)

    pressao_sistolica = (118 + risco * 9 + rng.normal(0, 8, n)).clip(90, 220).round().astype(int)
    pressao_diastolica = (pressao_sistolica * 0.62 + rng.normal(0, 5, n)).clip(55, 140).round().astype(int)

    colesterol_total = (185 + risco * 22 + rng.normal(0, 22, n)).clip(120, 400).round().astype(int)
    colesterol_hdl = (52 - risco * 5 + rng.normal(0, 8, n)).clip(20, 100).round().astype(int)

    glicemia_jejum = np.where(
        diabetico == "Sim",
        rng.normal(155, 35, n),
        rng.normal(92, 12, n),
    ).clip(65, 400).round().astype(int)

    frequencia_cardiaca = (72 + risco * 6 + rng.normal(0, 9, n)).clip(45, 160).round().astype(int)

    prob_diagnostico = 1 / (1 + np.exp(-(risco - 0.9)))
    diagnostico_dcv = (rng.random(n) < prob_diagnostico).astype(int)

    dores = np.array(["Assintomatico", "Angina tipica", "Angina atipica", "Dor nao anginosa"])
    tipo_dor_peito = []
    for d in diagnostico_dcv:
        if d == 1:
            tipo_dor_peito.append(
                sortear_categoria(rng, dores, [0.10, 0.50, 0.25, 0.15])
            )
        else:
            tipo_dor_peito.append(
                sortear_categoria(rng, dores, [0.55, 0.05, 0.15, 0.25])
            )

    pool_sintomas = [
        "Dor no peito", "Falta de ar", "Palpitacoes", "Tontura",
        "Fadiga", "Suor frio", "Inchaco nos tornozelos", "Nenhum sintoma relatado",
    ]
    sintomas_relatados = []
    for d in diagnostico_dcv:
        k = rng.integers(1, 4) if d == 1 else rng.integers(1, 3)
        if d == 0 and rng.random() < 0.45:
            sintomas_relatados.append("Nenhum sintoma relatado")
            continue
        escolhidos = rng.choice(
            [s for s in pool_sintomas if s != "Nenhum sintoma relatado"],
            size=k, replace=False,
        )
        sintomas_relatados.append(", ".join(escolhidos))

    ecg_imagem_ref = [
        f"ecg_paciente_{i+1:04d}.png" if i < N_COM_ECG else ""
        for i in range(n)
    ]

    df = pd.DataFrame({
        "paciente_id": np.arange(1, n + 1),
        "idade": idade,
        "sexo": sexo,
        "pressao_sistolica_mmhg": pressao_sistolica,
        "pressao_diastolica_mmhg": pressao_diastolica,
        "colesterol_total_mg_dl": colesterol_total,
        "colesterol_hdl_mg_dl": colesterol_hdl,
        "glicemia_jejum_mg_dl": glicemia_jejum,
        "imc": imc,
        "tabagista": tabagista,
        "diabetico": diabetico,
        "historico_familiar_dcv": historico_familiar,
        "nivel_atividade_fisica": nivel_atividade,
        "frequencia_cardiaca_repouso_bpm": frequencia_cardiaca,
        "tipo_dor_peito": tipo_dor_peito,
        "sintomas_relatados": sintomas_relatados,
        "diagnostico_dcv": diagnostico_dcv,
        "ecg_imagem_ref": ecg_imagem_ref,
    })
    return df


if __name__ == "__main__":
    df = gerar_dataset()
    saida = "../data/pacientes_cardiologicos.csv"
    df.to_csv(saida, index=False, encoding="utf-8")
    print(f"Dataset gerado com {len(df)} linhas em {saida}")
    print(df["diagnostico_dcv"].value_counts(normalize=True).rename("proporcao"))
