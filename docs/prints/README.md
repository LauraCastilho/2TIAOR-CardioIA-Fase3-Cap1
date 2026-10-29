# Prints da Fase 3

Evidências do funcionamento da aplicação, referenciadas no README principal.

| Arquivo | O que mostra |
|---|---|
| `wokwi_parte1_online.png` | Circuito da Parte 1 no Wokwi com o Monitor Serial exibindo envios `[NUVEM]` em tempo real. |
| `wokwi_parte1_offline.png` | Chave desligada e Serial com leituras `[EDGE] Offline. Leitura N armazenada`. |
| `wokwi_parte1_sync.png` | Chave religada, leituras com `"origem":"buffer"` e a mensagem `[SYNC] Concluida`. |
| `wokwi_parte2_mqtt.png` | Parte 2 no Wokwi com o Serial mostrando a conexão ao broker e as publicações MQTT. |
| `nodered_fluxo.png` | Editor do Node-RED com o fluxo `CardioIA - Fase 3`. |
| `dashboard_normal.png` | Dashboard com gráfico de BPM, gauges e LED de alerta verde. |
| `dashboard_alerta.png` | Dashboard em alerta crítico (taquicardia ou febre). |
| `dashboard_offline.png` | Dashboard com "Sem dados do dispositivo" e status offline recebido pelo Last Will. |
| `dashboard_sincronizado.png` | Dashboard depois de religar a conexão, com status online e o contador de leituras recuperadas do buffer. |
