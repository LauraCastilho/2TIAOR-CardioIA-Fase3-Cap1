# FIAP - Faculdade de Informática e Administração Paulista

<p align="center">
<a href="https://www.fiap.com.br/">
  <img src="assets/logo-fiap.png" 
       alt="FIAP - Faculdade de Informática e Administração Paulista" 
       width="40%">
</a>
</p>

<br>

# CardioIA — Fase 1: Batimentos de Dados

## Ecossistema de Cardiologia Inteligente (Graduação ON em Inteligência Artificial)

## 👨‍🎓 Integrantes: 
- <a href="https://www.linkedin.com/in/bernardo-doti/">Bernardo Naves Doti Avelar - RM566867</a>
- <a href="https://www.linkedin.com/in/eduardo-correia-29327631/">David Eduardo da Silva Correia - RM567525</a>
- <a href="https://www.linkedin.com/in/laura-castilho/">Laura de Andrade Castilho - RM568507</a> 
- <a href="https://www.linkedin.com/in/leticiagdornelas/">Leticia Grossi Dornelas - RM568172</a> 
- <a href="https://www.linkedin.com/in/leonardo-borges-alves-da-mota-649703177/">Leonardo Borges Alves da Mota - RM566939</a>

## 👩‍🏫 Professores:
### Tutor(a) 
- <a href="https://www.linkedin.com/in/leonardoorabona/">Leonardo Orabona</a>
### Coordenador(a)
- <a href="https://www.linkedin.com/in/andregodoichiovato/">André Godoi Chiovato</a>


## 📜 Descrição

As doenças cardiovasculares continuam sendo a principal causa de morte no mundo, respondendo por cerca de 17,9 milhões de óbitos por ano. Boa parte desses eventos — infartos, arritmias, insuficiência cardíaca, AVCs — poderia ser evitada ou tratada a tempo com diagnóstico precoce, algo em que a Inteligência Artificial tem potencial real de ajudar, seja antecipando riscos, seja personalizando o acompanhamento do paciente.

É nesse cenário que se encaixa o **CardioIA**, projeto acadêmico do curso de Graduação ON em Inteligência Artificial da FIAP. A proposta é construir, ao longo de sete fases, uma plataforma digital que simule um ecossistema completo de cardiologia moderna: triagem, diagnóstico assistido por IA, monitoramento contínuo via IoT, análise de imagens médicas por Visão Computacional, um assistente virtual para pacientes e, por fim, previsão de eventos cardíacos críticos.

Esta entrega corresponde à **Fase 1 — Batimentos de Dados**, na qual assumimos o papel de cientistas de dados hospitalares. Antes de qualquer modelo de IA existir, é preciso ter dados — e dados de saúde carregam restrições éticas, legais (LGPD) e de privacidade que tornam essa etapa muito mais desafiadora do que parece à primeira vista. Datasets clínicos reais raramente estão disponíveis publicamente em formato pronto para uso, o que nos levou a optar deliberadamente por **dados simulados** em duas das três partes da atividade, documentando essa escolha com transparência em vez de tentar contornar a restrição.

O repositório reúne três tipos de dados que, juntos, alimentarão os módulos das próximas fases do CardioIA:

1. **Dados numéricos** de pacientes cardiológicos (idade, pressão arterial, colesterol, sintomas, histórico, entre outras variáveis clínicas), gerados de forma sintética mas com correlações estatísticas plausíveis entre as variáveis, pensando no que futuramente alimentará os classificadores da Fase 2;
2. **Dados textuais** reais, buscados em fontes públicas (Project Gutenberg e SciELO), relacionados à fisiologia e à saúde cardiovascular, pensando na Fase 5 (NLP e assistente virtual);
3. **Imagens de ECG** sintéticas, geradas a partir de um modelo matemático de batimento cardíaco, pensando na Fase 4 (Visão Computacional).

Cada uma dessas partes é detalhada nas seções abaixo, com a origem dos dados, as variáveis consideradas mais relevantes do ponto de vista clínico e uma justificativa de como cada tipo de dado poderá ser explorado por algoritmos de IA nas fases seguintes — sempre com um olho na governança de dados e nos possíveis vieses embutidos em qualquer processo de simulação.


## 📁 Estrutura de pastas

Dentre os arquivos e pastas presentes na raiz do projeto, definem-se:

- <b>docs</b>: Pasta destinada à documentação textual, incluindo brainstorm, atas e registros de reuniões, desenhos, prints, diagramas, storyboard, estratégia de IA e arquitetura e etc. Nesta fase, contém também `docs/textos` (os dois textos da Parte 2) e `docs/amostras_ecg` (uma amostra das imagens da Parte 3, para visualização rápida sem precisar baixar o pacote completo).

- <b>src</b>: Todo o código fonte desenvolvido, como scripts em Python, R, JS ou HTML, notebooks, códigos para ESP32/Arduino, APIs ou microsserviços, além de modelos, inferências e etc. Nesta fase contém os scripts que geram o dataset numérico e as imagens de ECG sintéticas.

- <b>data</b>: Contém os dados utilizados, como arquivos CSV, Excel, JSON, bases sintéticas e etc. Nesta fase contém o dataset numérico (`pacientes_cardiologicos.csv`) e a pasta `imagens_ecg` com as 150 imagens de ECG geradas (não versionada no Git por volume — ver link de download abaixo).

- <b>README.md</b>: Arquivo que serve como guia e explicação geral sobre o projeto (o mesmo que você está lendo agora).

- <b>Ir Além</b>: pasta onde os arquivos referentes às atividades "Ir Além" devem ser inseridos, evitando misturar com as entregas principais. Não utilizada nesta entrega.


‼️ OBSERVAÇÃO DO TUTOR, favor desconsiderar do seu arquivo final: não há obrigação de usar todas as pastas, use apenas o que fizer SENTIDO para a entrega. ‼️


---

## 🩺 Parte 1 — Dados Numéricos (IoT)

**Arquivo:** [`data/pacientes_cardiologicos.csv`](data/pacientes_cardiologicos.csv) · **Formato:** CSV · **Linhas:** 300 pacientes (mínimo pedido: 100)

**Link para download (OneDrive/Google Drive):** <https://drive.google.com/file/d/1nGFAhySLkrh2F6JwyKHVZk_XLFjmA6td/view?usp=sharing> (pacote também contém as imagens da Parte 3)

### Origem dos dados

Dados **simulados**, não pertencem a nenhum paciente real. Foram gerados pelo script [`src/gerar_dataset_numerico.py`](src/gerar_dataset_numerico.py), que sorteia cada variável a partir de distribuições estatísticas (normal, categórica) e as correlaciona por meio de um "score de risco" latente — por exemplo, pacientes mais velhos, tabagistas, diabéticos ou com histórico familiar tendem a apresentar pressão arterial e colesterol mais altos, e maior probabilidade de diagnóstico positivo para doença cardiovascular. A geração usa uma seed fixa (42), então o dataset é reprodutível.

Optamos por dados simulados nesta parte porque datasets reais de pacientes cardíacos são protegidos por sigilo médico e LGPD, e as bases públicas anonimizadas que existem (ex. Cleveland Heart Disease) são pequenas, antigas e não trazem todas as variáveis pedidas no enunciado (como texto livre de sintomas). Simular nos permitiu montar um dataset do tamanho e formato que as próximas fases vão precisar, sem esbarrar nessas restrições.

### Variáveis e relevância clínica

| Variável | Relevância clínica |
|---|---|
| `idade`, `sexo` | Fatores de risco não modificáveis; a incidência de doença coronariana cresce com a idade e varia entre sexos (risco masculino maior antes da menopausa). |
| `pressao_sistolica_mmhg`, `pressao_diastolica_mmhg` | Hipertensão é um dos principais fatores de risco cardiovascular modificáveis e um dos sinais vitais mais monitorados em qualquer triagem. |
| `colesterol_total_mg_dl`, `colesterol_hdl_mg_dl` | Dislipidemia (colesterol alto, HDL baixo) está diretamente ligada à formação de placas ateroscleróticas. |
| `glicemia_jejum_mg_dl`, `diabetico` | Diabetes é comorbidade de altíssimo impacto no risco cardiovascular. |
| `imc` | Obesidade associada a hipertensão, diabetes e sobrecarga cardíaca. |
| `tabagista` | Fator de risco modificável com forte evidência causal para eventos coronarianos. |
| `historico_familiar_dcv` | Componente genético/hereditário do risco cardiovascular. |
| `nivel_atividade_fisica` | Sedentarismo é fator de risco; atividade física é medida de prevenção primária. |
| `frequencia_cardiaca_repouso_bpm` | Sinal vital simples de se monitorar (inclusive via wearable, tema da Fase 3) e indicador de condicionamento e de arritmias. |
| `tipo_dor_peito`, `sintomas_relatados` | Texto/categorias que representam a queixa clínica — insumo direto para os módulos de triagem e para NLP em fases futuras. |
| `diagnostico_dcv` | Variável-alvo (0/1) pensada para servir de rótulo aos classificadores supervisionados da Fase 2. |
| `ecg_imagem_ref` | Chave que liga este dataset às imagens de ECG da Parte 3, para os primeiros 150 pacientes — simula a integração entre prontuário e exame de imagem. |


## 📄 Parte 2 — Dados Textuais (NLP)

**Pasta:** [`docs/textos/`](docs/textos/)

| Arquivo | Fonte | Tipo | Licença |
|---|---|---|---|
| `harvey_1628_motion_of_the_heart.txt` | [Project Gutenberg — eBook #67065](https://www.gutenberg.org/ebooks/67065): *An Anatomical Disquisition on the Motion of the Heart & Blood in Animals*, William Harvey (1628), tradução de Robert Willis (1906) | Literatura clássica / obra fundadora da cardiologia moderna | Domínio público |
| `scielo_reabilitacao_cardiaca_2021.txt` (+ PDF original) | [SciELO Brasil — Arquivos Brasileiros de Cardiologia](https://www.scielo.br/j/abc/a/5L54CbkWFrQkd9pHMg6R7Nj/?lang=pt): "Qual é o Cenário Atual da Reabilitação Cardíaca no Brasil e em Portugal?", Stein, Milani & Abreu (2021) | Editorial científico, acesso aberto | CC BY 4.0 |

O texto de Harvey foi escolhido de propósito por ser o documento que, em 1628, descreveu pela primeira vez a circulação sanguínea como hoje a entendemos — é literalmente o ponto de partida histórico do que o CardioIA tenta modernizar com IA. Já o editorial da SciELO traz um recorte atual e nacional do sistema de saúde, com estatísticas, comparação Brasil/Portugal e uma leitura crítica sobre a subutilização da reabilitação cardíaca.

### Como esses textos podem ser explorados por NLP

- **Extração de entidades e termos clínicos**: identificar nomes de estruturas anatômicas, sintomas, doenças e medicamentos citados nos dois textos (NER), útil para popular um vocabulário/dicionário médico do futuro chatbot da Fase 5.
- **Sumarização automática**: o editorial da SciELO é um bom candidato para testar resumo automático de artigo científico, uma funcionalidade que pode apoiar profissionais de saúde a acompanhar literatura rapidamente.
- **Análise de sentimento/tom**: o editorial usa termos como "cenário alarmante" e "extremamente subutilizada" — dá para testar classificadores de urgência/tom em texto clínico-institucional, relevante para triagem de mensagens de pacientes.
- **Classificação de tópicos**: separar trechos por tema (fatores de risco, barreiras de acesso, recomendações de política pública) é um exercício direto de topic modeling.
- **Comparação diacrônica de linguagem médica**: colocar o texto de 1628 (traduzido) ao lado do texto de 2021 permite observar a evolução do vocabulário médico sobre o mesmo órgão, um exercício interessante de linguística computacional.

Esse tipo de análise é relevante para um projeto de IA em saúde porque boa parte da informação clínica ainda existe em texto livre — prontuários, artigos, relatos de pacientes — e a capacidade de extrair estrutura desse texto é o que viabiliza desde chatbots de triagem até sistemas de apoio à decisão clínica.


## 🫀 Parte 3 — Dados Visuais (Visão Computacional)

**Pasta local:** `data/imagens_ecg/` (150 imagens, não versionada no Git — ver link abaixo) · **Amostra versionada:** [`docs/amostras_ecg/`](docs/amostras_ecg/) (6 imagens)

**Link para download do pacote completo (OneDrive/Google Drive):** <https://drive.google.com/file/d/1nGFAhySLkrh2F6JwyKHVZk_XLFjmA6td/view?usp=sharing>

### Origem dos dados

Optamos por **ECGs sintéticos** em vez de imagens de exames reais. Bancos de ECG reais existem (ex. MIT-BIH, PTB-XL), mas em sua maioria distribuem sinais brutos, não imagens prontas, e usar exames de pacientes reais — mesmo que anonimizados — levanta questões de consentimento e reidentificação que preferimos não assumir num projeto de sala de aula. Optamos por gerar as imagens com o script [`src/gerar_ecgs_sinteticos.py`](src/gerar_ecgs_sinteticos.py), que constrói cada batimento somando cinco pulsos gaussianos (ondas P, Q, R, S, T — abordagem clássica de simulação de ECG, inspirada no modelo de McSharry/ECGSYN) e encadeia os batimentos ao longo de uma tira de 10 segundos.

Para dar variedade real ao conjunto (e não gerar 150 imagens praticamente idênticas), o script varia a frequência cardíaca sorteada por imagem, e uma fração das imagens simula ritmo irregular (variabilidade de intervalo RR, sugerindo arritmia) ou alteração de onda T (sugerindo isquemia). O arquivo `data/imagens_ecg/_metadados_ecg.csv` registra, por imagem, a frequência cardíaca estimada, o ritmo (regular/irregular) e uma classificação visual — útil como rótulo de referência para a Fase 4.

### Como essas imagens podem ser exploradas por Visão Computacional

- **Detecção de picos (peak detection)**: localizar os complexos QRS na imagem/sinal é o primeiro passo de qualquer pipeline automatizado de ECG, permitindo calcular frequência cardíaca e intervalos RR.
- **Detecção de bordas e segmentação do traçado**: separar o traçado (linha vermelha escura) do fundo quadriculado é um exercício direto de pré-processamento de imagem (thresholding, detecção de bordas) antes de qualquer classificação.
- **Reconhecimento de padrões/anomalias**: treinar um classificador (CNN) para diferenciar ritmo regular de irregular, ou identificar alteração de onda T, é exatamente o tipo de tarefa prevista para a Fase 4 do CardioIA.
- **Ligação com dados tabulares**: como cada imagem está referenciada no dataset numérico da Parte 1 (`ecg_imagem_ref`), esse conjunto também serve para testar, futuramente, modelos multimodais que combinam sinais tabulares e imagem.

Automatizar a leitura de ECG é particularmente valioso em contextos de triagem com poucos cardiologistas disponíveis (o que o próprio texto da Parte 2 sobre reabilitação cardíaca no Brasil já evidencia como um problema real de acesso), daí a relevância desse tipo de análise para um projeto de IA aplicado à saúde.


## 🔒 Governança de Dados e Vieses

Como as Partes 1 e 3 usam dados **simulados**, o maior risco de governança aqui não é vazamento de dados sensíveis (não existe paciente real), e sim o risco de **viés embutido no próprio gerador** ser confundido, mais adiante, com um padrão epidemiológico real. Por isso:

- Todo arquivo simulado está claramente identificado como tal neste README e nos próprios scripts que o geram — nenhuma fase futura deve tratar `pacientes_cardiologicos.csv` ou as imagens de ECG como dado real sem essa ressalva.
- As correlações usadas no gerador (ex. peso maior para tabagismo e histórico familiar) são simplificações baseadas em conhecimento geral de fatores de risco, não em estudos epidemiológicos brasileiros — um modelo treinado sobre elas aprende a lógica do gerador, não a realidade clínica do país, e isso precisa ser dito explicitamente antes de qualquer conclusão ser tirada dele.
- A distribuição de sexo, idade e das demais variáveis foi escolhida de forma aproximadamente balanceada de propósito, mas isso não reflete necessariamente a distribuição real da população de pacientes cardíacos atendidos no SUS ou na rede privada — algo a revisitar se o grupo decidir buscar dados reais nas próximas fases.
- Os dois textos da Parte 2 são reais e públicos, mas representam pontos de vista específicos (um tratado do século XVII e um editorial de 2021 com recorte Brasil/Portugal) — não devem ser tomados como cobertura representativa de toda a literatura médica sobre o tema.
- Mesmo sendo dados fictícios, tratamos o dataset como se fosse sensível (sem nomes, sem identificadores reais, `paciente_id` sequencial e artificial) como exercício de boa prática de anonimização.


## 📎 Links e Observações

- <b>Repositório GitHub</b>: <https://github.com/LauraCastilho/2TIAOR-CardioIA-Fase1-Cap1>
- <b>Dataset numérico (Parte 1) e pacote de imagens de ECG (Parte 3)</b>: <https://drive.google.com/file/d/1nGFAhySLkrh2F6JwyKHVZk_XLFjmA6td/view?usp=sharing> (mesmo pacote .zip contém os dois)
- <b>Explicação de decisões técnicas</b>: ver seções "Origem dos dados" de cada parte, acima.
- <b>Observações Gerais</b>: atividade não está vinculada a nenhuma competição externa.


## 🔧 Como executar o código

Pré-requisitos: Python 3.12+ com `numpy`, `pandas` e `matplotlib`.

```bash
pip install numpy pandas matplotlib

cd src
python gerar_dataset_numerico.py   # gera ../data/pacientes_cardiologicos.csv
python gerar_ecgs_sinteticos.py    # gera ../data/imagens_ecg/*.png e ../docs/amostras_ecg/*.png
```

Ambos os scripts usam uma seed fixa, então rodar novamente reproduz exatamente os mesmos arquivos.


## 🗃 Histórico de lançamentos

* 0.1.0 - 18/08/2026
    * Entrega da Fase 1 — Batimentos de Dados: dataset numérico simulado (300 pacientes), dois textos reais para NLP (Gutenberg + SciELO) e 150 imagens de ECG sintéticas para Visão Computacional.

---


## 📋 Licença

<img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/cc.svg?ref=chooser-v1"><img style="height:22px!important;margin-left:3px;vertical-align:text-bottom;" src="https://mirrors.creativecommons.org/presskit/icons/by.svg?ref=chooser-v1"><p xmlns:cc="http://creativecommons.org/ns#" xmlns:dct="http://purl.org/dc/terms/"><a property="dct:title" rel="cc:attributionURL" href="https://github.com/SabrinaOtoni/TEMPLATE-FIAP-GRAD-ON-IA">MODELO GIT FIAP</a> por <a rel="cc:attributionURL dct:creator" property="cc:attributionName" href="https://fiap.com.br">FIAP</a> está licenciado sobre <a href="http://creativecommons.org/licenses/by/4.0/?ref=chooser-v1" target="_blank" rel="license noopener noreferrer" style="display:inline-block;">Attribution 4.0 International</a>.</p>
