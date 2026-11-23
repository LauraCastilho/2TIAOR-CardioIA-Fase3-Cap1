# Relatório – Parte 1: Pré-processamento e organização das imagens

**Projeto:** CardioIA – Fase 4: Coração em Imagens (Visão Computacional)
**Notebook:** [`notebooks/01_preprocessamento.ipynb`](../../notebooks/01_preprocessamento.ipynb)

## 1. Escolha do dataset e do problema

Usamos o **NIH ChestX-ray14**, o dataset sugerido no enunciado: 112.120 radiografias de tórax frontais de 30.805 pacientes do NIH Clinical Center (EUA), com 14 achados rotulados. Entre esses achados escolhemos a **cardiomegalia** (aumento da área cardíaca), por ser o único com ligação direta ao tema do CardioIA. A cardiomegalia aparece em quadros de insuficiência cardíaca, hipertensão e valvopatias, e é um bom exemplo de "coração em imagens".

O problema ficou **binário**: `cardiomegalia` × `normal` (laudo "No Finding"). As imagens com outros achados e sem cardiomegalia ficaram de fora, para o modelo aprender o contraste entre coração aumentado e tórax sem alterações.

Os arquivos foram baixados do espelho público no Hugging Face (`alkzar90/NIH-Chest-X-ray-dataset`), que tem os mesmos zips e CSV do Kaggle e dispensa credenciais. Assim o notebook roda igual no Google Colab e localmente.

## 2. Coorte

| Classe | Imagens | Pacientes | Critério |
|---|---|---|---|
| cardiomegalia | 2.776 | 1.566 | Todas as imagens com "Cardiomegaly" no laudo, sozinha ou com outros achados |
| normal | 5.552 | 4.479 | Amostra aleatória (semente 42) das 60.361 imagens "No Finding" |
| **Total** | **8.328** | — | Proporção 1 positivo : 2 negativos |

Optamos por um desbalanceamento **moderado (1:2)** em vez de 1:1. A cardiomegalia é minoritária na prática (2,5% do NIH completo), e queríamos métricas mais realistas. No treino, o desbalanceamento é compensado com pesos de classe.

## 3. Etapas do pipeline

| # | Etapa | Implementação | Justificativa |
|---|---|---|---|
| 1 | **Divisão treino/validação/teste** | `StratifiedGroupKFold` com 20 dobras: 14 para treino, 3 para validação e 3 para teste. O grupo é o **paciente** | Um paciente chega a ter dezenas de exames. Dividir por imagem colocaria radiografias quase iguais em treino e teste (vazamento). A estratificação manteve 33,3% de positivos nos três conjuntos |
| 2 | **Download seletivo** | Cada zip (2–4 GB) é baixado com retomada, só as imagens da coorte são lidas e o zip é apagado em seguida | De 45 GB de origem sobram 190 MB processados, em 28 min |
| 3 | **Conversão de formato** | `PIL.Image.convert("L")`: PNG em escala de cinza, 8 bits, 1 canal | 32 imagens vinham em RGBA. Padronizar evita entradas com 4 canais e reduz o tamanho em disco |
| 4 | **Redimensionamento** | 1024×1024 → **224×224**, filtro Lanczos | 224 é a entrada nativa da ResNet50 e deixa o treino viável em CPU. O Lanczos preserva melhor as bordas finas, como o contorno cardíaco |
| 5 | **Organização** | `raiox_processado/{treino,validacao,teste}/{cardiomegalia,normal}/` + `indice.csv` | Estrutura lida direto pelo Keras. O índice guarda paciente, idade, sexo e incidência (PA/AP) para a análise de fairness |
| 6 | **Normalização** | Média (0,499) e desvio (0,250) dos pixels/255, calculados **só no treino** e salvos em `normalizacao.json` | Usados como z-score na CNN do zero. Usar validação ou teste no cálculo seria vazamento. A ResNet50 usa a normalização do ImageNet, aplicada dentro do modelo |

## 4. Resultado

| Conjunto | cardiomegalia | normal | Total | Pacientes |
|---|---|---|---|---|
| Treino | 1.943 | 3.887 | 5.830 (70%) | 3.965 |
| Validação | 417 | 833 | 1.250 (15%) | 868 |
| Teste | 416 | 832 | 1.248 (15%) | 842 |

O notebook confirma com `assert` que **nenhum paciente aparece em mais de um conjunto**. As figuras de verificação estão em `docs/prints/fase4/01_*.png`: antes/depois, histogramas de normalização, distribuição por sexo, idade e incidência, e amostras.

## 5. Decisões deixadas de fora (e por quê)

- **Espelhamento horizontal** não entra como aumento de dados. O coração fica à esquerda, e espelhar a imagem criaria uma anatomia irreal (dextrocardia). Os aumentos usados no treino (rotação ±7°, zoom e translação pequenos, contraste) estão no notebook 02.
- **Equalização de histograma (CLAHE)** não foi aplicada. As imagens do NIH já vêm com contraste ajustado, e a ResNet50 foi pré-treinada sem esse passo.
- **Recorte dos marcadores de texto** ("PORTABLE", "AP") não foi feito nesta versão. O risco de o modelo usar esses marcadores como atalho é discutido no relatório de ética (Ir Além 1).
