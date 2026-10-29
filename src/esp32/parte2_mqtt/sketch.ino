/*
 * CardioIA - Fase 3 | Parte 2: Transmissão para a nuvem via MQTT (Fog/Cloud)
 *
 * Mesmo wearable da Parte 1, agora publicando de verdade em um broker MQTT.
 * O dashboard no Node-RED assina os tópicos abaixo e mostra os dados em
 * tempo real, com gráfico, gauges e indicador de alerta.
 *
 * Sensores:
 *   - DHT22 (GPIO 15): temperatura (tratada como temperatura de pele) e umidade.
 *   - Botão de pressão (GPIO 4): cada aperto é um batimento. O BPM sai do
 *     intervalo médio entre os últimos apertos.
 *
 * Entradas e saídas auxiliares:
 *   - Chave deslizante (GPIO 18): simula a perda do link com a nuvem. Ao
 *     desligar, a conexão TCP é derrubada sem aviso, como aconteceria numa
 *     queda real de sinal, e o broker publica o Last Will "offline".
 *   - LED vermelho (GPIO 26): última leitura com alerta crítico.
 *   - LED verde (GPIO 27): aceso = publicando; piscando = sincronizando buffer.
 *
 * Tópicos MQTT (prefixo TOPICO_BASE):
 *   .../sinais     leitura atual, publicada a cada 5 s
 *   .../historico  leituras que ficaram no buffer durante a queda
 *   .../status     "online" / "offline" (retido; "offline" vem do Last Will)
 *
 * A lógica de resiliência é a mesma da Parte 1: buffer circular de 4.320
 * amostras (6 h) em RAM, espelhado em /buffer.csv no SPIFFS, descartando
 * primeiro as leituras normais mais antigas quando enche.
 *
 * Broker: por padrão usamos o broker público da HiveMQ (porta 1883, sem TLS),
 * que funciona direto no Wokwi. Para usar o HiveMQ Cloud com TLS e usuário,
 * troque USAR_HIVEMQ_CLOUD para 1 e preencha host, usuário e senha.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <SPIFFS.h>
#include <time.h>
#include "DHTesp.h"

#define USAR_HIVEMQ_CLOUD 0

#if USAR_HIVEMQ_CLOUD
#include <WiFiClientSecure.h>
#endif

// ----------------------------- Rede ----------------------------------------
const char* WIFI_SSID  = "Wokwi-GUEST";   // rede aberta do simulador
const char* WIFI_SENHA = "";

#if USAR_HIVEMQ_CLOUD
const char*    MQTT_HOST    = "SEU-CLUSTER.s1.eu.hivemq.cloud";
const uint16_t MQTT_PORTA   = 8883;
const char*    MQTT_USUARIO = "SEU_USUARIO";
const char*    MQTT_SENHA   = "SUA_SENHA";
WiFiClientSecure clienteRede;
#else
const char*    MQTT_HOST    = "broker.hivemq.com";
const uint16_t MQTT_PORTA   = 1883;
const char*    MQTT_USUARIO = nullptr;
const char*    MQTT_SENHA   = nullptr;
WiFiClient clienteRede;
#endif

PubSubClient mqtt(clienteRede);

const char* TOPICO_BASE = "fiap/2tiaor/cardioia/paciente/0001";
char topicoSinais[80];
char topicoHistorico[80];
char topicoStatus[80];

// ----------------------------- Pinos ---------------------------------------
const int PINO_DHT         = 15;
const int PINO_BOTAO_PULSO = 4;
const int PINO_CHAVE_WIFI  = 18;
const int PINO_LED_ALERTA  = 26;
const int PINO_LED_WIFI    = 27;

// ----------------------------- Parâmetros ----------------------------------
const char*    ID_PACIENTE          = "0001";
const uint32_t INTERVALO_LEITURA_MS = 5000;
const uint16_t CAPACIDADE_DESEJADA  = 4320;   // 6 h com uma leitura a cada 5 s
const uint8_t  LOTE_SINCRONIZACAO   = 10;     // publicações do buffer por volta do loop
const uint32_t INTERVALO_STATUS_MS  = 30000;
const uint32_t RETENTATIVA_WIFI_MS  = 15000;
const uint32_t RETENTATIVA_MQTT_MS  = 5000;
const char*    ARQUIVO_BUFFER       = "/buffer.csv";

// Limites clínicos avaliados na borda (o Node-RED usa os mesmos)
const uint8_t BPM_MAXIMO  = 120;
const uint8_t BPM_MINIMO  = 50;
const float   TEMP_MAXIMA = 38.0;
const float   TEMP_MINIMA = 35.0;

// Cálculo do BPM pelo botão
const uint8_t  MAX_PULSOS             = 8;
const uint32_t DEBOUNCE_MS            = 250;
const uint32_t INTERVALO_MAX_PULSO_MS = 3000;
const uint32_t JANELA_SEM_PULSO_MS    = 6000;

// ----------------------------- Flags da leitura ----------------------------
const uint8_t F_TAQUICARDIA = 1 << 0;
const uint8_t F_BRADICARDIA = 1 << 1;
const uint8_t F_FEBRE       = 1 << 2;
const uint8_t F_HIPOTERMIA  = 1 << 3;
const uint8_t F_SEM_PULSO   = 1 << 4;
const uint8_t F_ERRO_DHT    = 1 << 5;
const uint8_t F_TS_RELATIVO = 1 << 6;   // ts em segundos desde o boot (sem NTP)
const uint8_t MASCARA_CRITICO = F_TAQUICARDIA | F_BRADICARDIA | F_FEBRE | F_HIPOTERMIA;

struct __attribute__((packed)) Leitura {
  uint32_t seq;
  uint32_t ts;       // epoch UTC quando o NTP já sincronizou; senão, uptime
  int16_t  temp10;
  uint16_t umid10;
  uint8_t  bpm;
  uint8_t  flags;
};

// ----------------------------- Estado global -------------------------------
DHTesp dht;

Leitura* buffer      = nullptr;
uint16_t capacidade  = 0;
uint16_t inicio      = 0;
uint16_t quantidade  = 0;
uint32_t descartadas = 0;

bool     spiffsOk      = false;
uint32_t linhasArquivo = 0;

bool     chaveOnline      = false;  // estado da chave (link simulado)
bool     estavaOnline     = false;  // para detectar a volta da conexão
bool     sincronizando    = false;
bool     ntpConfigurado   = false;
uint32_t enviadasNaSync   = 0;
uint32_t ultimaTentativaWifi = 0;
uint32_t ultimaTentativaMqtt = 0;

uint32_t proximoSeq    = 1;
uint32_t ultimaLeitura = 0;
uint32_t ultimoStatus  = 0;
uint8_t  ultimasFlags  = 0;

portMUX_TYPE      muxPulsos = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t temposPulso[MAX_PULSOS];
volatile uint8_t  idxPulso      = 0;
volatile uint8_t  qtdPulsos     = 0;
volatile uint32_t ultimoPulsoMs = 0;

// ============================================================================
// Batimentos
// ============================================================================

void IRAM_ATTR aoDetectarPulso() {
  uint32_t agora = millis();
  portENTER_CRITICAL_ISR(&muxPulsos);
  if (agora - ultimoPulsoMs >= DEBOUNCE_MS) {
    ultimoPulsoMs = agora;
    temposPulso[idxPulso] = agora;
    idxPulso = (idxPulso + 1) % MAX_PULSOS;
    if (qtdPulsos < MAX_PULSOS) qtdPulsos++;
  }
  portEXIT_CRITICAL_ISR(&muxPulsos);
}

uint8_t calcularBpm() {
  uint32_t tempos[MAX_PULSOS];
  uint8_t n, idx;

  portENTER_CRITICAL(&muxPulsos);
  for (uint8_t i = 0; i < MAX_PULSOS; i++) tempos[i] = temposPulso[i];
  n = qtdPulsos;
  idx = idxPulso;
  portEXIT_CRITICAL(&muxPulsos);

  if (n < 2) return 0;

  uint32_t maisRecente = tempos[(idx + MAX_PULSOS - 1) % MAX_PULSOS];
  if (millis() - maisRecente > JANELA_SEM_PULSO_MS) return 0;

  uint32_t soma = 0;
  uint8_t intervalos = 0;
  for (uint8_t k = 1; k < n; k++) {
    uint32_t atual    = tempos[(idx + MAX_PULSOS - k) % MAX_PULSOS];
    uint32_t anterior = tempos[(idx + MAX_PULSOS - k - 1) % MAX_PULSOS];
    uint32_t dt = atual - anterior;
    if (dt > INTERVALO_MAX_PULSO_MS) break;
    soma += dt;
    intervalos++;
  }
  if (intervalos == 0 || soma == 0) return 0;

  uint32_t bpm = (60000UL * intervalos + soma / 2) / soma;
  return bpm > 250 ? 250 : (uint8_t)bpm;
}

// ============================================================================
// Buffer circular em RAM
// ============================================================================

uint16_t posicao(uint16_t i) {
  return (uint16_t)((inicio + (uint32_t)i) % capacidade);
}

void alocarBuffer() {
  uint16_t tentativa = CAPACIDADE_DESEJADA;
  while (tentativa >= 100) {
    buffer = (Leitura*)malloc(sizeof(Leitura) * tentativa);
    if (buffer != nullptr) {
      capacidade = tentativa;
      return;
    }
    tentativa /= 2;
  }
  Serial.println("[ERRO] Sem memoria para o buffer offline");
}

// Buffer cheio: sai a leitura normal mais antiga; alertas ficam.
void bufferInserir(const Leitura& nova) {
  if (capacidade == 0) return;

  if (quantidade == capacidade) {
    uint16_t alvo = 0;
    for (uint16_t i = 0; i < quantidade; i++) {
      if ((buffer[posicao(i)].flags & MASCARA_CRITICO) == 0) {
        alvo = i;
        break;
      }
    }
    for (uint16_t j = alvo; j > 0; j--) {
      buffer[posicao(j)] = buffer[posicao(j - 1)];
    }
    inicio = posicao(1);
    quantidade--;
    descartadas++;
  }

  buffer[posicao(quantidade)] = nova;
  quantidade++;
}

void bufferRemoverMaisAntiga() {
  if (quantidade == 0) return;
  inicio = posicao(1);
  quantidade--;
}

// ============================================================================
// Espelho do buffer no SPIFFS
// ============================================================================

void arquivoEscreverLinha(File& f, const Leitura& l) {
  f.printf("%lu,%lu,%d,%u,%u,%u\n",
           (unsigned long)l.seq, (unsigned long)l.ts, (int)l.temp10,
           (unsigned)l.umid10, (unsigned)l.bpm, (unsigned)l.flags);
}

void arquivoCompactar() {
  File f = SPIFFS.open(ARQUIVO_BUFFER, FILE_WRITE);
  if (!f) return;
  for (uint16_t i = 0; i < quantidade; i++) arquivoEscreverLinha(f, buffer[posicao(i)]);
  f.close();
  linhasArquivo = quantidade;
}

void arquivoAnexar(const Leitura& l) {
  if (!spiffsOk) return;
  File f = SPIFFS.open(ARQUIVO_BUFFER, FILE_APPEND);
  if (!f) return;
  arquivoEscreverLinha(f, l);
  f.close();
  linhasArquivo++;
  if (linhasArquivo > (uint32_t)capacidade + capacidade / 4) arquivoCompactar();
}

void arquivoApagar() {
  if (spiffsOk && SPIFFS.exists(ARQUIVO_BUFFER)) SPIFFS.remove(ARQUIVO_BUFFER);
  linhasArquivo = 0;
}

void arquivoRecuperar() {
  if (!spiffsOk || !SPIFFS.exists(ARQUIVO_BUFFER)) return;
  File f = SPIFFS.open(ARQUIVO_BUFFER, FILE_READ);
  if (!f) return;

  uint32_t recuperadas = 0;
  while (f.available()) {
    String linha = f.readStringUntil('\n');
    unsigned long seq, ts;
    int temp10;
    unsigned umid10, bpm, flags;
    if (sscanf(linha.c_str(), "%lu,%lu,%d,%u,%u,%u",
               &seq, &ts, &temp10, &umid10, &bpm, &flags) == 6) {
      Leitura l = {(uint32_t)seq, (uint32_t)ts, (int16_t)temp10,
                   (uint16_t)umid10, (uint8_t)bpm, (uint8_t)flags};
      bufferInserir(l);
      if (l.seq >= proximoSeq) proximoSeq = l.seq + 1;
      recuperadas++;
    }
  }
  f.close();
  linhasArquivo = recuperadas;
  Serial.printf("[EDGE] %lu leituras pendentes recuperadas do SPIFFS\n",
                (unsigned long)recuperadas);
}

// ============================================================================
// Coleta e avaliação local
// ============================================================================

const char* descreverStatus(uint8_t flags) {
  if (flags & F_TAQUICARDIA) return "TAQUICARDIA";
  if (flags & F_BRADICARDIA) return "BRADICARDIA";
  if (flags & F_FEBRE)       return "FEBRE";
  if (flags & F_HIPOTERMIA)  return "HIPOTERMIA";
  if (flags & F_ERRO_DHT)    return "ERRO_SENSOR";
  if (flags & F_SEM_PULSO)   return "SEM_PULSO";
  return "NORMAL";
}

Leitura coletarLeitura() {
  Leitura l;
  l.seq = proximoSeq++;
  l.bpm = calcularBpm();
  l.flags = 0;

  // Com NTP sincronizado usamos o horário real; antes disso, o uptime.
  time_t agora = time(nullptr);
  if (agora > 1700000000) {
    l.ts = (uint32_t)agora;
  } else {
    l.ts = millis() / 1000;
    l.flags |= F_TS_RELATIVO;
  }

  TempAndHumidity th = dht.getTempAndHumidity();
  bool dhtOk = dht.getStatus() == DHTesp::ERROR_NONE &&
               !isnan(th.temperature) && !isnan(th.humidity);

  if (dhtOk) {
    l.temp10 = (int16_t)lroundf(th.temperature * 10);
    l.umid10 = (uint16_t)lroundf(th.humidity * 10);
    if (th.temperature >= TEMP_MAXIMA)     l.flags |= F_FEBRE;
    else if (th.temperature < TEMP_MINIMA) l.flags |= F_HIPOTERMIA;
  } else {
    l.temp10 = 0;
    l.umid10 = 0;
    l.flags |= F_ERRO_DHT;
  }

  if (l.bpm == 0)              l.flags |= F_SEM_PULSO;
  else if (l.bpm > BPM_MAXIMO) l.flags |= F_TAQUICARDIA;
  else if (l.bpm < BPM_MINIMO) l.flags |= F_BRADICARDIA;

  return l;
}

void montarJson(const Leitura& l, const char* origem, char* saida, size_t tamanho) {
  char temp[12], umid[12];
  if (l.flags & F_ERRO_DHT) {
    strcpy(temp, "null");
    strcpy(umid, "null");
  } else {
    snprintf(temp, sizeof(temp), "%.1f", l.temp10 / 10.0f);
    snprintf(umid, sizeof(umid), "%.1f", l.umid10 / 10.0f);
  }
  snprintf(saida, tamanho,
           "{\"paciente\":\"%s\",\"seq\":%lu,\"ts\":%lu,\"ts_tipo\":\"%s\","
           "\"temp\":%s,\"umid\":%s,\"bpm\":%u,\"flags\":%u,"
           "\"status\":\"%s\",\"origem\":\"%s\"}",
           ID_PACIENTE, (unsigned long)l.seq, (unsigned long)l.ts,
           (l.flags & F_TS_RELATIVO) ? "uptime" : "epoch",
           temp, umid, (unsigned)l.bpm, (unsigned)l.flags,
           descreverStatus(l.flags), origem);
}

// ============================================================================
// Conexões: Wi-Fi, NTP e MQTT
// ============================================================================

bool estaOnline() {
  return chaveOnline && WiFi.status() == WL_CONNECTED && mqtt.connected();
}

void manterWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!ntpConfigurado) {
      configTime(0, 0, "pool.ntp.org", "time.google.com");
      ntpConfigurado = true;
      Serial.print("[WIFI] Conectado. IP: ");
      Serial.println(WiFi.localIP());
    }
    return;
  }
  if (millis() - ultimaTentativaWifi < RETENTATIVA_WIFI_MS) return;
  ultimaTentativaWifi = millis();
  Serial.println("[WIFI] Tentando conectar...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_SENHA, 6);  // canal 6 acelera a conexão no Wokwi
}

void manterMqtt() {
  if (!chaveOnline || WiFi.status() != WL_CONNECTED) return;

  if (mqtt.connected()) {
    mqtt.loop();
    return;
  }
  if (millis() - ultimaTentativaMqtt < RETENTATIVA_MQTT_MS) return;
  ultimaTentativaMqtt = millis();

  char idCliente[40];
  snprintf(idCliente, sizeof(idCliente), "cardioia-%04X%08X",
           (uint16_t)(ESP.getEfuseMac() >> 32), (uint32_t)ESP.getEfuseMac());

  Serial.printf("[MQTT] Conectando a %s:%u ...\n", MQTT_HOST, MQTT_PORTA);
  // Last Will: se a conexão cair sem aviso, o broker publica "offline".
  bool ok = mqtt.connect(idCliente, MQTT_USUARIO, MQTT_SENHA,
                         topicoStatus, 1, true, "offline");
  if (ok) {
    mqtt.publish(topicoStatus, "online", true);
    Serial.println("[MQTT] Conectado ao broker");
  } else {
    Serial.printf("[MQTT] Falhou (estado %d). Nova tentativa em %lu s\n",
                  mqtt.state(), (unsigned long)(RETENTATIVA_MQTT_MS / 1000));
  }
}

// A chave simula a perda do link. Ao desligar, fechamos o socket sem mandar
// DISCONNECT, para o broker perceber a queda pelo keep-alive e disparar o
// Last Will, como numa queda real.
void lerChave() {
  bool estado = digitalRead(PINO_CHAVE_WIFI) == LOW;
  if (estado == chaveOnline) return;
  chaveOnline = estado;
  if (chaveOnline) {
    Serial.println("[LINK] Link liberado pela chave. Reconectando ao broker...");
    ultimaTentativaMqtt = 0;
  } else {
    Serial.println("[LINK] Queda de conexao simulada. A coleta continua no buffer local");
    clienteRede.stop();
  }
}

// ============================================================================
// Publicação e sincronização
// ============================================================================

bool publicar(const Leitura& l, const char* origem) {
  char json[260];
  montarJson(l, origem, json, sizeof(json));
  const char* topico = strcmp(origem, "buffer") == 0 ? topicoHistorico : topicoSinais;
  bool ok = mqtt.publish(topico, json);
  if (ok) {
    Serial.print("[MQTT] -> ");
    Serial.print(topico);
    Serial.print(" ");
    Serial.println(json);
  }
  return ok;
}

void armazenarLocal(const Leitura& l) {
  bufferInserir(l);
  arquivoAnexar(l);
  Serial.printf("[EDGE] Offline. Leitura %lu armazenada (%u/%u) | bpm=%u temp=%.1f status=%s\n",
                (unsigned long)l.seq, quantidade, capacidade, (unsigned)l.bpm,
                l.temp10 / 10.0f, descreverStatus(l.flags));
}

void registrarLeitura(const Leitura& l) {
  if (estaOnline() && publicar(l, "tempo_real")) return;
  armazenarLocal(l);  // sem link ou publicação falhou: nada se perde
}

void sincronizarLote() {
  for (uint8_t k = 0; k < LOTE_SINCRONIZACAO && quantidade > 0; k++) {
    if (!publicar(buffer[inicio], "buffer")) return;  // tenta de novo depois
    bufferRemoverMaisAntiga();
    enviadasNaSync++;
    mqtt.loop();
  }
  if (quantidade == 0) {
    arquivoApagar();
    sincronizando = false;
    Serial.printf("[SYNC] Concluida: %lu leituras enviadas. Arquivo local apagado\n",
                  (unsigned long)enviadasNaSync);
  }
}

// Avisa no Serial quando a conexão volta e decide se há buffer para enviar.
// A sincronização roda sempre que houver pendências com o link ativo,
// inclusive quando uma publicação isolada falhou sem a conexão cair.
void verificarRetornoDaConexao() {
  bool agora = estaOnline();
  if (agora && !estavaOnline && quantidade > 0) {
    enviadasNaSync = 0;
    Serial.printf("[SYNC] Conexao restabelecida. %u leituras pendentes\n", quantidade);
  }
  sincronizando = agora && quantidade > 0;
  estavaOnline = agora;
}

// ============================================================================

void atualizarLeds() {
  digitalWrite(PINO_LED_ALERTA, (ultimasFlags & MASCARA_CRITICO) ? HIGH : LOW);
  if (sincronizando) {
    digitalWrite(PINO_LED_WIFI, (millis() / 150) % 2);
  } else {
    digitalWrite(PINO_LED_WIFI, estaOnline() ? HIGH : LOW);
  }
}

void imprimirResumo() {
  Serial.printf("[STATUS] chave=%s wifi=%s mqtt=%s | buffer=%u/%u | descartadas=%lu | heap livre=%u\n",
                chaveOnline ? "on" : "off",
                WiFi.status() == WL_CONNECTED ? "ok" : "--",
                mqtt.connected() ? "ok" : "--",
                quantidade, capacidade, (unsigned long)descartadas,
                (unsigned)ESP.getFreeHeap());
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== CardioIA | Fase 3 | Parte 2 - MQTT ===");

  pinMode(PINO_BOTAO_PULSO, INPUT_PULLUP);
  pinMode(PINO_CHAVE_WIFI, INPUT_PULLUP);
  pinMode(PINO_LED_ALERTA, OUTPUT);
  pinMode(PINO_LED_WIFI, OUTPUT);
  attachInterrupt(digitalPinToInterrupt(PINO_BOTAO_PULSO), aoDetectarPulso, FALLING);

  dht.setup(PINO_DHT, DHTesp::DHT22);

  snprintf(topicoSinais, sizeof(topicoSinais), "%s/sinais", TOPICO_BASE);
  snprintf(topicoHistorico, sizeof(topicoHistorico), "%s/historico", TOPICO_BASE);
  snprintf(topicoStatus, sizeof(topicoStatus), "%s/status", TOPICO_BASE);

  alocarBuffer();
  Serial.printf("[EDGE] Buffer offline: %u amostras de %u bytes\n",
                capacidade, (unsigned)sizeof(Leitura));
  spiffsOk = SPIFFS.begin(true);
  arquivoRecuperar();

#if USAR_HIVEMQ_CLOUD
  clienteRede.setInsecure();  // simplificação para o protótipo (ver relatório)
#endif
  mqtt.setServer(MQTT_HOST, MQTT_PORTA);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(15);
  mqtt.setSocketTimeout(5);

  chaveOnline = digitalRead(PINO_CHAVE_WIFI) == LOW;
  Serial.printf("[LINK] Estado inicial da chave: %s\n", chaveOnline ? "conectado" : "desconectado");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_SENHA, 6);
  ultimaTentativaWifi = millis();

  Serial.println("Aperte o botao no ritmo dos batimentos para gerar o BPM.");
}

void loop() {
  lerChave();
  manterWiFi();
  manterMqtt();
  verificarRetornoDaConexao();

  uint32_t agora = millis();
  if (agora - ultimaLeitura >= INTERVALO_LEITURA_MS) {
    ultimaLeitura = agora;
    Leitura l = coletarLeitura();
    ultimasFlags = l.flags;
    registrarLeitura(l);
  }

  if (sincronizando) sincronizarLote();

  if (agora - ultimoStatus >= INTERVALO_STATUS_MS) {
    ultimoStatus = agora;
    imprimirResumo();
  }

  atualizarLeds();
  delay(10);
}
