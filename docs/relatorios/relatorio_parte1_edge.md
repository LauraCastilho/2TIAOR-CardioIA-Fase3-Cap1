# Relatório – Parte 1: Armazenamento e processamento local (Edge Computing)

**Projeto:** CardioIA – Fase 3: Monitoramento Contínuo (IoT na Saúde)
**Código:** [`src/esp32/parte1_edge/sketch.ino`](../../src/esp32/parte1_edge/sketch.ino)
**Circuito:** [`src/esp32/parte1_edge/diagram.json`](../../src/esp32/parte1_edge/diagram.json)

## 1. Objetivo

Simular no Wokwi um wearable cardíaco baseado em ESP32 que continue coletando sinais vitais mesmo sem internet. A ideia é que nenhuma leitura se perca durante uma queda de conexão e que tudo seja enviado assim que o sinal voltar. Nesta parte a "nuvem" é representada pelo Monitor Serial, como pede o enunciado.

## 2. Circuito montado

| Componente | Pino do ESP32 | Papel no projeto |
|---|---|---|
| DHT22 | GPIO 15 | Temperatura (tratada como temperatura de pele) e umidade (indicador de sudorese). |
| Botão de pressão vermelho | GPIO 4 | Cada aperto representa um batimento. É o nosso segundo sensor. |
| Chave deslizante | GPIO 18 | Variável booleana de conectividade Wi-Fi. |
| LED vermelho + resistor 220 Ω | GPIO 26 | Acende quando a última leitura tem alerta crítico. |
| LED verde + resistor 220 Ω | GPIO 27 | Aceso quando online e piscando durante a sincronização. |

O botão e a chave usam o pull-up interno do ESP32, então ficam em nível alto quando soltos e vão para nível baixo quando ligados ao GND. Deixamos o DHT22 configurado no diagrama com 36,6 °C para a simulação já começar com um valor fisiológico. Durante a simulação, a temperatura e a umidade podem ser alteradas clicando no sensor.

## 3. Fluxo de funcionamento

1. **Captura.** A cada 5 segundos o ESP32 lê o DHT22 e calcula o BPM a partir dos apertos do botão.
2. **Processamento local.** A própria placa compara os valores com os limites clínicos e marca a leitura com flags. Os limites são taquicardia acima de 120 bpm, bradicardia abaixo de 50 bpm, febre a partir de 38 °C e hipotermia abaixo de 35 °C. Também marcamos "sem pulso" quando o botão não é apertado por 6 segundos e "erro de sensor" quando o DHT22 não responde.
3. **Decisão de envio.** Se a chave indica que há conexão, a leitura é transformada em JSON e enviada na hora com `Serial.println`, com o prefixo `[NUVEM]`.
4. **Armazenamento offline.** Se não há conexão, a leitura vai para um buffer circular em RAM e também é gravada como uma linha no arquivo `/buffer.csv` do SPIFFS.
5. **Sincronização.** Quando a chave volta para "conectado", o buffer é enviado do mais antigo para o mais novo, em lotes de 20 leituras por volta do `loop()`. Assim a coleta de novas leituras não para durante o envio. Quando o buffer esvazia, o arquivo local é apagado e o Serial mostra quantas leituras foram sincronizadas.

O LED vermelho acompanha a última leitura, e o LED verde mostra o estado da conexão. A cada 30 segundos o Serial imprime um resumo com o estado da conexão, a ocupação do buffer, as leituras descartadas e a memória livre.

### Cálculo do BPM

O botão está ligado a uma interrupção na borda de descida. A interrupção apenas guarda o instante de cada aperto em um vetor de 8 posições, com debounce de 250 ms. Na hora da leitura, o código percorre os apertos do mais novo para o mais antigo e calcula o intervalo médio entre eles. O BPM é 60.000 dividido por esse intervalo em milissegundos. Se aparece uma pausa maior que 3 segundos, consideramos que começou uma nova sequência e paramos a média ali. Com isso, quem está testando pode mudar o ritmo e o BPM acompanha em poucos batimentos.

## 4. Lógica de resiliência

### Estratégia de armazenamento limitado

Pensamos no caso de uso de um wearable de acompanhamento domiciliar. O cenário mais comum de perda de conexão é o paciente dormir longe do roteador ou passar algumas horas em um local sem sinal. Por isso dimensionamos o armazenamento para **6 horas offline**:

| Item | Valor |
|---|---|
| Intervalo entre leituras | 5 s |
| Leituras por hora | 720 |
| Capacidade do buffer | **4.320 amostras** (6 h) |
| Tamanho de cada amostra na RAM | 14 bytes (struct compactada) |
| Memória usada na RAM | cerca de 60 KB |
| Tamanho de cada linha no SPIFFS | cerca de 30 bytes |
| Espaço usado no SPIFFS | cerca de 130 KB, dentro da partição padrão de 1,5 MB |

Para caber esse volume na RAM, cada leitura é guardada em uma struct compactada. A temperatura e a umidade são multiplicadas por 10 e salvas como inteiros de 16 bits, em vez de `float`. Se o `malloc` não conseguir os 60 KB, o código tenta automaticamente com metade da capacidade e informa no Serial.

### Política de descarte quando o buffer enche

Se a conexão ficar fora por mais de 6 horas, algo precisa ser descartado. Escolhemos **descartar a leitura normal mais antiga** e manter as leituras com alerta crítico. Uma leitura de taquicardia às 3 da manhã é muito mais importante para o médico do que mais uma leitura normal. Só quando o buffer inteiro é de alertas o código passa a descartar o alerta mais antigo. O total de descartes aparece no resumo do Serial, para a equipe saber que houve perda.

### Espelho no SPIFFS

O buffer em RAM é rápido, mas se perde se a bateria acabar. Por isso cada leitura armazenada offline também é anexada ao `/buffer.csv`. No `setup()`, se o arquivo existir, as leituras são recarregadas para o buffer antes de qualquer coisa. Assim um reinício do dispositivo não apaga o que ainda não foi enviado. Como a RAM descarta leituras quando enche e o arquivo só recebe novas linhas, o arquivo é regravado com o conteúdo atual do buffer quando passa de 125% da capacidade. Depois de uma sincronização completa, o arquivo é apagado, como pede o enunciado.

Cada leitura tem um número de sequência (`seq`). Se a energia cair no meio de uma sincronização, algumas leituras podem ser enviadas duas vezes depois do reinício. O `seq` permite que a nuvem descarte essas duplicatas.

### Limitação do simulador

No Wokwi o SPIFFS funciona, mas é volátil: tudo some quando a simulação é encerrada. Por isso o Monitor Serial também registra cada leitura armazenada (`[EDGE]`), cada envio (`[NUVEM]`) e o fim de cada sincronização (`[SYNC]`). Ele é a nossa evidência de resiliência dentro do simulador, conforme a alternativa sugerida no enunciado. Em um ESP32 físico, o mesmo código manteria o arquivo entre reinícios.

## 5. Roteiro de teste

1. Iniciar a simulação com a chave na posição conectada e apertar o botão em ritmo constante. O Serial mostra as linhas `[NUVEM]` com `"origem":"tempo_real"`.
2. Mover a chave para desconectado. O Serial passa a mostrar `[EDGE] Offline. Leitura N armazenada (x/4320)`.
3. Durante o período offline, apertar o botão bem rápido para gerar taquicardia ou subir a temperatura do DHT22 acima de 38 °C. O LED vermelho acende mesmo sem conexão, porque a avaliação é feita na borda.
4. Voltar a chave para conectado. O LED verde pisca, as leituras pendentes saem com `"origem":"buffer"` e aparece a mensagem `[SYNC] Concluida`, indicando que o arquivo local foi apagado.

## 6. Conclusão

A Parte 1 mostra o papel do Edge Computing em uma aplicação de saúde. O dispositivo não depende da nuvem para decidir se uma leitura é perigosa, não perde dados durante quedas de conexão e prioriza o que tem valor clínico quando a memória acaba. Essa mesma lógica foi reaproveitada na Parte 2, onde o envio pelo Serial foi trocado por publicações MQTT reais.
