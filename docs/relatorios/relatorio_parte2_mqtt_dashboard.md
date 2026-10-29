# Relatório – Parte 2: Transmissão para a nuvem e visualização (Fog/Cloud Computing)

**Projeto:** CardioIA – Fase 3: Monitoramento Contínuo (IoT na Saúde)
**Código do ESP32:** [`src/esp32/parte2_mqtt/sketch.ino`](../../src/esp32/parte2_mqtt/sketch.ino)
**Fluxo do Node-RED:** [`src/node-red/flows_cardioia.json`](../../src/node-red/flows_cardioia.json)

## 1. Visão geral da arquitetura

Nesta parte o wearable da Parte 1 passa a falar com a internet de verdade. O circuito é o mesmo: DHT22, botão de batimentos, chave de conectividade e dois LEDs. A diferença é que as leituras agora são publicadas em um broker MQTT e consumidas por um dashboard no Node-RED. O sistema ficou dividido em três camadas:

| Camada | Onde roda | O que faz |
|---|---|---|
| **Edge** | ESP32 (Wokwi) | Lê os sensores, calcula o BPM, avalia limites, guarda tudo em buffer quando está offline e publica via MQTT. |
| **Cloud** | Broker HiveMQ | Recebe as publicações e entrega para quem assinou os tópicos. Guarda a mensagem retida de status e dispara o Last Will. |
| **Fog** | Node-RED | Assina os tópicos, reavalia os limites, detecta silêncio do dispositivo e monta o dashboard. |

O caminho de uma leitura é: **sensor → ESP32 → broker MQTT → Node-RED → dashboard → alerta**.

Chamamos o Node-RED de camada fog porque ele fica entre o dispositivo e o usuário final e toma decisões próximas da origem dos dados. Em um hospital, ele poderia rodar em um servidor local da unidade, recebendo dados de vários pacientes e repassando para a nuvem só o que for necessário.

## 2. Comunicação MQTT

### 2.1 Por que MQTT

O MQTT é um protocolo de publicação e assinatura feito para dispositivos com pouca memória e redes instáveis. O cabeçalho mínimo tem 2 bytes, a conexão fica aberta e não é preciso abrir uma requisição nova a cada envio, como no HTTP. Ele também tem recursos que encaixam bem em monitoramento de saúde, como mensagens retidas e Last Will. O ESP32 publica sem saber quem vai consumir, e podemos adicionar novos consumidores, como um banco de dados ou um serviço de e-mail, sem mexer no firmware.

### 2.2 Broker

Usamos o **broker público da HiveMQ** (`broker.hivemq.com`, porta 1883). Ele não exige cadastro e funciona direto de dentro do Wokwi, o que simplifica os testes. O firmware também já tem a configuração para o **HiveMQ Cloud**, com TLS na porta 8883 e usuário e senha. Para usá-la basta trocar `USAR_HIVEMQ_CLOUD` para `1` e preencher as credenciais do cluster. A seção 5 explica por que essa seria a opção certa em produção.

### 2.3 Tópicos

Todos os tópicos começam com `fiap/2tiaor/cardioia/paciente/0001`. Usamos um prefixo longo porque o broker público é compartilhado com qualquer pessoa, e um nome genérico como `cardio/bpm` poderia receber mensagens de outros projetos.

| Tópico | Quem publica | Retido | Conteúdo |
|---|---|---|---|
| `.../sinais` | ESP32 | Não | Leitura atual, a cada 5 s. |
| `.../historico` | ESP32 | Não | Leituras que estavam no buffer durante a queda de conexão. |
| `.../status` | ESP32 e broker | Sim | `online` ao conectar e `offline` via Last Will. |

Separamos `sinais` e `historico` de propósito. Se as leituras antigas chegassem no mesmo tópico, o gauge e o alerta do dashboard mostrariam por alguns segundos valores de horas atrás como se fossem atuais. Com tópicos separados, o painel em tempo real mostra só o presente, e o histórico pode ir para outro destino.

### 2.4 Formato da mensagem

Cada publicação é um JSON pequeno, com cerca de 200 bytes:

```json
{"paciente":"0001","seq":42,"ts":1793289600,"ts_tipo":"epoch","temp":36.6,"umid":55.0,
 "bpm":78,"flags":0,"status":"NORMAL","origem":"tempo_real"}
```

| Campo | Descrição |
|---|---|
| `paciente` | Identificador pseudonimizado do paciente. Não há nome nem documento. |
| `seq` | Número sequencial da leitura, usado para identificar duplicatas. |
| `ts` / `ts_tipo` | Momento da coleta. É `epoch` quando o ESP32 já sincronizou o relógio via NTP e `uptime` quando ainda não sincronizou. |
| `temp`, `umid` | Valores do DHT22, ou `null` se o sensor falhar. |
| `bpm` | Batimentos por minuto calculados pelo botão. `0` significa sem pulso detectado. |
| `flags` / `status` | Resultado da avaliação feita na borda, em bits e em texto. |
| `origem` | `tempo_real` ou `buffer`. |

O horário da coleta vai dentro da mensagem porque uma leitura sincronizada depois de uma queda chega ao broker horas depois de ter sido medida. Sem esse campo, não daria para reconstruir a linha do tempo do paciente.

### 2.5 Ciclo de conexão no ESP32

O firmware cuida de três conexões, todas sem travar a coleta:

1. **Wi-Fi.** Conecta na rede `Wokwi-GUEST`. Se cair, tenta de novo a cada 15 s.
2. **NTP.** Assim que o Wi-Fi conecta, o relógio é sincronizado com `pool.ntp.org`. Isso permite enviar timestamps reais.
3. **MQTT.** Se o Wi-Fi está ativo e a chave está ligada, o ESP32 tenta conectar ao broker a cada 5 s. O tempo limite de cada tentativa é de 5 s, para a coleta não ficar parada muito tempo.

Na conexão MQTT registramos um **Last Will** no tópico de status, com a mensagem `offline`, QoS 1 e retida. Logo depois de conectar, o ESP32 publica `online` retido no mesmo tópico. O keep-alive é de 15 s.

A chave deslizante simula a perda do link. Quando ela é desligada, o firmware fecha o socket TCP sem enviar o pacote DISCONNECT do MQTT, como aconteceria em uma queda real de sinal. O broker percebe a conexão encerrada e publica o Last Will, e o dashboard mostra o dispositivo como offline. Enquanto isso, as leituras vão para o buffer local, exatamente como na Parte 1. Quando a chave volta, o ESP32 reconecta, publica `online` e envia o buffer no tópico `historico`, em lotes de 10 publicações por volta do `loop()`. Se uma publicação falha no meio do envio, a leitura continua no buffer e o envio recomeça na próxima volta.

### 2.6 Qualidade de serviço

A biblioteca PubSubClient só publica com QoS 0, ou seja, sem confirmação de entrega pelo broker. Sabemos disso e compensamos na borda. A leitura só sai do buffer depois que `publish()` retorna sucesso, e qualquer falha de conexão manda as leituras para o buffer. O `seq` permite detectar duplicatas do lado do consumidor. O Node-RED assina com QoS 1, e o Last Will usa QoS 1, que a biblioteca suporta. Em uma versão de produção trocaríamos para uma biblioteca com QoS 1 na publicação, como a `esp-mqtt` do ESP-IDF.

## 3. Configuração do Node-RED

### 3.1 Instalação

Usamos o Node-RED com o pacote `node-red-dashboard`, que fornece os nós de gráfico, gauge, texto e template. O fluxo completo está exportado em `src/node-red/flows_cardioia.json` e pode ser importado pelo menu **Importar** do Node-RED. O passo a passo está no README.

### 3.2 Nós do fluxo

| Nó | Tipo | Função |
|---|---|---|
| Sinais em tempo real | `mqtt in` | Assina `.../sinais` e já converte o payload para objeto JSON. |
| Avaliar limites (fog) | `function` | Reavalia os limites clínicos e divide a mensagem em 5 saídas: BPM, temperatura, umidade, status do alerta e evento de novo alerta crítico. |
| Watchdog 20 s | `trigger` | Cada mensagem reinicia um contador. Se o ESP32 ficar 20 s sem publicar, ele dispara um aviso de "sem dados". |
| Histórico sincronizado | `mqtt in` + `function` | Conta as leituras que chegam do buffer e mostra o total no painel, ignorando duplicatas consecutivas pelo `seq`. |
| Status do dispositivo | `mqtt in` + `function` | Lê o tópico retido de status e mostra se o wearable está online ou offline. |
| Novo alerta crítico | `debug` | Registra no painel de depuração cada vez que o nível passa para crítico. É o ponto de integração para notificações externas. |

### 3.3 Por que reavaliar os limites no Node-RED

O ESP32 já marca as leituras com flags, então reavaliar parece repetição. Fizemos isso por dois motivos. Primeiro, os limites podem mudar por paciente: um atleta pode ter 45 bpm em repouso sem nada de errado. Ajustar um número em um nó do Node-RED é muito mais simples do que regravar o firmware de cada wearable. Segundo, a camada fog pode cruzar informações que o dispositivo não tem, como o histórico do paciente. Com isso o alerta da borda continua funcionando sem internet, e o alerta da fog pode ser refinado.

Os limites usados nas duas camadas são:

| Condição | Limite | Nível |
|---|---|---|
| Taquicardia | BPM > 120 | Crítico |
| Bradicardia | BPM < 50 (com pulso detectado) | Crítico |
| Febre | Temperatura ≥ 38,0 °C | Crítico |
| Hipotermia | Temperatura < 35,0 °C | Crítico |
| Sem pulso | BPM = 0 | Atenção |
| Falha no DHT22 | Leitura nula | Atenção |
| Sem dados do dispositivo | 20 s sem mensagens | Sem dados |

O evento de "novo alerta crítico" só sai quando o nível muda de não crítico para crítico. Se o paciente ficar 10 minutos em taquicardia, a equipe recebe um aviso, e não 120 avisos seguidos. Isso evita a fadiga de alarmes, um problema conhecido em UTIs.

### 3.4 Dashboard

O dashboard fica em `http://localhost:1880/ui`, na aba **CardioIA**, dividido em três grupos:

- **Paciente 0001 · Sinais vitais.** Tem o gráfico de linha da frequência cardíaca com os últimos 10 minutos e eixo de 30 a 200 bpm. Ao lado ficam o gauge de temperatura, de 30 a 42 °C, e o gauge de umidade da pele. O gauge de temperatura usa faixas de cor que coincidem com os limites: azul abaixo de 35 °C, verde até 38 °C e vermelho acima disso.
- **Alertas.** É um LED virtual feito com o nó `ui_template`. Ele fica verde quando está normal, laranja em atenção, vermelho piscando em alerta crítico e cinza quando não há dados. Ao lado aparecem o título, a descrição do problema e o horário da última atualização.
- **Dispositivo.** Mostra o estado online ou offline vindo do Last Will e o contador de leituras recuperadas do buffer, que prova a sincronização após uma queda.

No gráfico só entram leituras com BPM maior que zero. Os períodos sem pulso aparecem como lacuna, e não como uma queda falsa para 0 bpm, que poderia ser confundida com parada cardíaca.

## 4. Roteiro de demonstração

1. Importar o fluxo no Node-RED, fazer o deploy e abrir `/ui`.
2. Iniciar a simulação da Parte 2 no Wokwi com a chave ligada. No Serial aparecem `[WIFI] Conectado`, `[MQTT] Conectado ao broker` e as publicações. No dashboard, o status passa para online.
3. Apertar o botão em ritmo normal, perto de 1 aperto por segundo. O gráfico mostra cerca de 60 bpm e o LED virtual fica verde.
4. Apertar o botão rapidamente, mais de 2 vezes por segundo, para passar de 120 bpm. O LED virtual fica vermelho piscando, o LED físico do circuito acende e o debug registra o novo alerta.
5. Clicar no DHT22 e subir a temperatura para 38,5 °C. O gauge vai para a faixa vermelha e o alerta mostra febre.
6. Desligar a chave. Em cerca de 20 s o dashboard mostra "Sem dados do dispositivo", e o status muda para offline pelo Last Will. No Serial, as leituras passam a ser armazenadas no buffer.
7. Religar a chave. O ESP32 reconecta, o contador de leituras recuperadas aumenta e o Serial mostra `[SYNC] Concluida`.

## 5. Segurança e boas práticas em IoT médico

O protótipo usa o broker público por praticidade, mas temos claro que isso **não é aceitável com dados reais**. Qualquer pessoa que assine `fiap/2tiaor/#` consegue ler os sinais vitais publicados. Para um produto real, adotaríamos:

- **Criptografia em trânsito.** Usaríamos TLS na porta 8883, já preparado no firmware para o HiveMQ Cloud. No protótipo usamos `setInsecure()`, que não valida o certificado do servidor. Em produção o certificado da autoridade certificadora seria gravado no dispositivo.
- **Autenticação e autorização.** Cada wearable teria credenciais próprias e regras de acesso no broker, para que ele só possa publicar nos próprios tópicos e não leia dados de outros pacientes.
- **Minimização de dados (LGPD).** O payload já leva só um identificador pseudonimizado. A ligação entre `0001` e a pessoa real ficaria apenas no prontuário do hospital, fora do fluxo IoT. Dados de saúde são dados pessoais sensíveis pela LGPD, então esse cuidado não é opcional.
- **Integridade e auditoria.** O `seq` e o `ts` permitem detectar lacunas e duplicatas. Em produção, as mensagens seriam gravadas em um banco com registro de acesso.
- **O alerta não substitui o profissional.** Os limites são genéricos e servem para triagem. A decisão clínica continua com a equipe de saúde.

## 6. Eficiência

- Cada leitura tem cerca de 200 bytes. A 1 leitura a cada 5 s, isso dá cerca de 3,5 MB por paciente por dia, um volume pequeno até para redes móveis.
- A conexão MQTT permanece aberta, sem o custo de abrir uma conexão HTTP por leitura.
- A sincronização em lotes não bloqueia a coleta, e a avaliação na borda mantém os alertas locais funcionando sem rede.
- Em um wearable de bateria, o próximo passo seria colocar o ESP32 em light sleep entre as leituras e agrupar várias leituras por mensagem.

## 7. Limitações e próximos passos

- O botão é uma aproximação de um sensor de pulso. Um sensor óptico real, como o MAX30102, traria ruído e artefatos de movimento que precisariam de filtragem.
- O DHT22 mede o ambiente, não o corpo. Em um dispositivo real usaríamos um sensor de temperatura de contato, como o MAX30205.
- As leituras do histórico são contadas, mas não são persistidas. A próxima evolução é gravá-las em um banco de séries temporais, como InfluxDB, e visualizar no Grafana. Isso também alimentaria a Fase 6 do CardioIA, de previsão de crises com séries temporais.
- O evento de "novo alerta crítico" já está isolado em uma saída própria, pronto para ser ligado a um envio de e-mail ou mensagem.
