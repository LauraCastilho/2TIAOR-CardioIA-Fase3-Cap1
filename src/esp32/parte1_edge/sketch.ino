/*
 * CardioIA - Fase 3 | Parte 1: Armazenamento e processamento local (Edge Computing)
 *
 * Wearable cardíaco simulado no Wokwi com ESP32.
 *
 * Sensores:
 *   - DHT22 (GPIO 15): temperatura e umidade. Consideramos a temperatura como
 *     temperatura corporal medida na pele e a umidade como indicador de sudorese.
 *   - Botão de pressão (GPIO 4): cada aperto representa um batimento cardíaco.
 *     O BPM é calculado pelo intervalo médio entre os últimos apertos.
 *
 * Entradas e saídas auxiliares:
 *   - Chave deslizante (GPIO 18): simula a conectividade Wi-Fi (variável booleana).
 *   - LED vermelho (GPIO 26): acende quando a última leitura é crítica.
 *   - LED verde (GPIO 27): aceso = online; piscando = sincronizando o buffer.
 *
 * Fluxo:
 *   1. A cada INTERVALO_LEITURA_MS lemos o DHT22 e calculamos o BPM.
 *   2. A própria placa avalia os limites clínicos e marca a leitura com flags
 *      (processamento na borda, sem depender da nuvem).
 *   3. Online: a leitura é enviada na hora para a "nuvem" (Serial.println).
 *      Offline: a leitura vai para o buffer circular em RAM e é espelhada no
 *      arquivo /buffer.csv do SPIFFS.
 *   4. Quando a conexão volta, o buffer é enviado em lotes, do mais antigo para
 *      o mais novo, e o arquivo local é apagado ao final.
 *
 * Estratégia de armazenamento limitado:
 *   Leitura a cada 5 s e capacidade de 4.320 amostras = 6 horas sem conexão,
 *   o suficiente para cobrir uma noite de sono do paciente ou um período em
 *   lugar sem sinal. Cada amostra ocupa 14 bytes, cerca de 60 KB de RAM.
 *   Quando o buffer enche, descartamos a leitura NORMAL mais antiga.
 *   Leituras com alerta crítico só são descartadas se o buffer inteiro for de
 *   alertas, porque são elas que o médico precisa ver depois.
 *
 * Observação: no Wokwi o SPIFFS é volátil e some ao parar a simulação. Por isso
 * o Monitor Serial também registra tudo o que é armazenado e sincronizado.
 * Em um ESP32 físico o arquivo sobrevive a um reinício e é recarregado no boot.
 */

#include <Arduino.h>
#include <SPIFFS.h>
#include "DHTesp.h"

// ----------------------------- Pinos ---------------------------------------
const int PINO_DHT         = 15;
const int PINO_BOTAO_PULSO = 4;
const int PINO_CHAVE_WIFI  = 18;
const int PINO_LED_ALERTA  = 26;
const int PINO_LED_WIFI    = 27;

// ----------------------------- Parâmetros ----------------------------------
const char*    ID_PACIENTE          = "0001";
const uint32_t INTERVALO_LEITURA_MS = 5000;   // período de amostragem
const uint16_t CAPACIDADE_DESEJADA  = 4320;   // 6 h com uma leitura a cada 5 s
const uint8_t  LOTE_SINCRONIZACAO   = 20;     // leituras enviadas por volta do loop
const uint32_t INTERVALO_STATUS_MS  = 30000;  // resumo periódico no Serial
const char*    ARQUIVO_BUFFER       = "/buffer.csv";

// Limites clínicos usados na avaliação local
const uint8_t BPM_MAXIMO  = 120;   // acima disso: taquicardia
const uint8_t BPM_MINIMO  = 50;    // abaixo disso: bradicardia
const float   TEMP_MAXIMA = 38.0;  // a partir disso: febre
const float   TEMP_MINIMA = 35.0;  // abaixo disso: hipotermia

// Cálculo do BPM pelo botão
const uint8_t  MAX_PULSOS             = 8;     // quantos apertos guardamos
const uint32_t DEBOUNCE_MS            = 250;   // limita a 240 bpm e filtra repique
const uint32_t INTERVALO_MAX_PULSO_MS = 3000;  // intervalo maior = nova sequência
const uint32_t JANELA_SEM_PULSO_MS    = 6000;  // sem aperto nesse tempo = sem pulso

// ----------------------------- Flags da leitura ----------------------------
const uint8_t F_TAQUICARDIA = 1 << 0;
const uint8_t F_BRADICARDIA = 1 << 1;
const uint8_t F_FEBRE       = 1 << 2;
const uint8_t F_HIPOTERMIA  = 1 << 3;
const uint8_t F_SEM_PULSO   = 1 << 4;
const uint8_t F_ERRO_DHT    = 1 << 5;
const uint8_t MASCARA_CRITICO = F_TAQUICARDIA | F_BRADICARDIA | F_FEBRE | F_HIPOTERMIA;

// Uma amostra compactada (14 bytes). Temperatura e umidade ficam x10
// como inteiros para economizar memória.
struct __attribute__((packed)) Leitura {
  uint32_t seq;      // número sequencial, ajuda a nuvem a detectar duplicatas
  uint32_t ts;       // segundos desde o boot
  int16_t  temp10;   // temperatura em décimos de °C
  uint16_t umid10;   // umidade em décimos de %
  uint8_t  bpm;      // 0 = sem pulso detectado
  uint8_t  flags;    // combinação das flags acima
};

// ----------------------------- Estado global -------------------------------
DHTesp dht;

// Buffer circular em RAM
Leitura* buffer      = nullptr;
uint16_t capacidade  = 0;
uint16_t inicio      = 0;   // posição da leitura mais antiga
uint16_t quantidade  = 0;
uint32_t descartadas = 0;

// Espelho no SPIFFS
bool     spiffsOk      = false;
uint32_t linhasArquivo = 0;

// Conectividade simulada
bool     online         = false;
bool     sincronizando  = false;
uint32_t enviadasNaSync = 0;

// Controle de tempo
uint32_t proximoSeq    = 1;
uint32_t ultimaLeitura = 0;
uint32_t ultimoStatus  = 0;
uint8_t  ultimasFlags  = 0;

// Pulsos do botão (escritos pela interrupção)
portMUX_TYPE      muxPulsos = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t temposPulso[MAX_PULSOS];
volatile uint8_t  idxPulso      = 0;
volatile uint8_t  qtdPulsos     = 0;
volatile uint32_t ultimoPulsoMs = 0;

// ============================================================================
// Batimentos: interrupção do botão e cálculo do BPM
// ============================================================================

// Chamada a cada aperto do botão. Só registra o instante do batimento.
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

// BPM = 60.000 / intervalo médio entre os apertos mais recentes.
// Retorna 0 quando não há apertos recentes (sensor solto ou sem pulso).
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

  // Percorre do aperto mais novo para o mais antigo e para quando encontra
  // uma pausa longa, que indica o começo de outra sequência de batimentos.
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

// Reserva o buffer. Se faltar memória, tenta com metade do tamanho.
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

void bufferInserir(const Leitura& nova) {
  if (capacidade == 0) return;

  if (quantidade == capacidade) {
    // Buffer cheio: procura a leitura normal mais antiga para descartar.
    uint16_t alvo = 0;
    for (uint16_t i = 0; i < quantidade; i++) {
      if ((buffer[posicao(i)].flags & MASCARA_CRITICO) == 0) {
        alvo = i;
        break;
      }
    }
    // Empurra as leituras anteriores ao alvo uma posição para frente,
    // sobrescrevendo o alvo, e libera a primeira posição.
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
// Espelho do buffer no SPIFFS (/buffer.csv)
// ============================================================================

void arquivoEscreverLinha(File& f, const Leitura& l) {
  f.printf("%lu,%lu,%d,%u,%u,%u\n",
           (unsigned long)l.seq, (unsigned long)l.ts, (int)l.temp10,
           (unsigned)l.umid10, (unsigned)l.bpm, (unsigned)l.flags);
}

// Regrava o arquivo só com o que está no buffer. Usado quando o arquivo
// cresce demais por causa dos descartes feitos na RAM.
void arquivoCompactar() {
  File f = SPIFFS.open(ARQUIVO_BUFFER, FILE_WRITE);
  if (!f) return;
  for (uint16_t i = 0; i < quantidade; i++) arquivoEscreverLinha(f, buffer[posicao(i)]);
  f.close();
  linhasArquivo = quantidade;
  Serial.printf("[EDGE] Arquivo local compactado para %u linhas\n", quantidade);
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

// No boot, recarrega leituras que ficaram pendentes antes de um desligamento.
// Só faz diferença no ESP32 físico; no Wokwi o arquivo começa vazio.
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
  l.ts = millis() / 1000;
  l.bpm = calcularBpm();
  l.flags = 0;

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

// Monta o JSON que seria enviado para a nuvem.
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
           "{\"paciente\":\"%s\",\"seq\":%lu,\"ts\":%lu,\"temp\":%s,\"umid\":%s,"
           "\"bpm\":%u,\"flags\":%u,\"status\":\"%s\",\"origem\":\"%s\"}",
           ID_PACIENTE, (unsigned long)l.seq, (unsigned long)l.ts, temp, umid,
           (unsigned)l.bpm, (unsigned)l.flags, descreverStatus(l.flags), origem);
}

// Nesta parte a "nuvem" é o Monitor Serial.
void enviarParaNuvem(const Leitura& l, const char* origem) {
  char json[220];
  montarJson(l, origem, json, sizeof(json));
  Serial.print("[NUVEM] ");
  Serial.println(json);
}

// ============================================================================
// Conectividade simulada e sincronização
// ============================================================================

void atualizarConectividade() {
  bool agoraOnline = digitalRead(PINO_CHAVE_WIFI) == LOW;
  if (agoraOnline == online) return;

  online = agoraOnline;
  if (online) {
    Serial.printf("[WIFI] Conectado. %u leituras pendentes para sincronizar\n", quantidade);
    if (quantidade > 0) {
      sincronizando = true;
      enviadasNaSync = 0;
    }
  } else {
    Serial.println("[WIFI] Conexao perdida. A coleta continua e as leituras vao para o buffer local");
    sincronizando = false;
  }
}

// Envia um lote do buffer por volta do loop para não travar a coleta.
void sincronizarLote() {
  for (uint8_t k = 0; k < LOTE_SINCRONIZACAO && quantidade > 0; k++) {
    enviarParaNuvem(buffer[inicio], "buffer");
    bufferRemoverMaisAntiga();
    enviadasNaSync++;
  }
  if (quantidade == 0) {
    arquivoApagar();
    sincronizando = false;
    Serial.printf("[SYNC] Concluida: %lu leituras enviadas. Arquivo local apagado\n",
                  (unsigned long)enviadasNaSync);
  }
}

void registrarLeitura(const Leitura& l) {
  if (online) {
    // Online: a leitura atual vai direto. Se ainda houver pendências,
    // elas continuam saindo em paralelo pelo sincronizarLote().
    enviarParaNuvem(l, "tempo_real");
  } else {
    bufferInserir(l);
    arquivoAnexar(l);
    Serial.printf("[EDGE] Offline. Leitura %lu armazenada (%u/%u) | bpm=%u temp=%.1f status=%s\n",
                  (unsigned long)l.seq, quantidade, capacidade, (unsigned)l.bpm,
                  l.temp10 / 10.0f, descreverStatus(l.flags));
  }
}

void atualizarLeds() {
  digitalWrite(PINO_LED_ALERTA, (ultimasFlags & MASCARA_CRITICO) ? HIGH : LOW);
  if (sincronizando) {
    digitalWrite(PINO_LED_WIFI, (millis() / 150) % 2);  // pisca rápido
  } else {
    digitalWrite(PINO_LED_WIFI, online ? HIGH : LOW);
  }
}

void imprimirResumo() {
  Serial.printf("[STATUS] online=%s | buffer=%u/%u | descartadas=%lu | arquivo=%lu linhas | heap livre=%u\n",
                online ? "sim" : "nao", quantidade, capacidade,
                (unsigned long)descartadas, (unsigned long)linhasArquivo,
                (unsigned)ESP.getFreeHeap());
}

// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== CardioIA | Fase 3 | Parte 1 - Edge Computing ===");

  pinMode(PINO_BOTAO_PULSO, INPUT_PULLUP);
  pinMode(PINO_CHAVE_WIFI, INPUT_PULLUP);
  pinMode(PINO_LED_ALERTA, OUTPUT);
  pinMode(PINO_LED_WIFI, OUTPUT);
  attachInterrupt(digitalPinToInterrupt(PINO_BOTAO_PULSO), aoDetectarPulso, FALLING);

  dht.setup(PINO_DHT, DHTesp::DHT22);

  alocarBuffer();
  Serial.printf("[EDGE] Buffer offline: %u amostras de %u bytes (~%.1f h com uma leitura a cada %lu s)\n",
                capacidade, (unsigned)sizeof(Leitura),
                capacidade * (INTERVALO_LEITURA_MS / 1000.0f) / 3600.0f,
                (unsigned long)(INTERVALO_LEITURA_MS / 1000));

  spiffsOk = SPIFFS.begin(true);
  Serial.println(spiffsOk ? "[EDGE] SPIFFS montado" : "[EDGE] SPIFFS indisponivel, usando apenas a RAM");
  arquivoRecuperar();

  // Lê o estado inicial da chave sem disparar a mensagem de troca.
  online = digitalRead(PINO_CHAVE_WIFI) == LOW;
  sincronizando = online && quantidade > 0;
  Serial.printf("[WIFI] Estado inicial: %s\n", online ? "conectado" : "desconectado");
  Serial.println("Aperte o botao no ritmo dos batimentos para gerar o BPM.");
}

void loop() {
  atualizarConectividade();

  uint32_t agora = millis();
  if (agora - ultimaLeitura >= INTERVALO_LEITURA_MS) {
    ultimaLeitura = agora;
    Leitura l = coletarLeitura();
    ultimasFlags = l.flags;
    registrarLeitura(l);
  }

  if (online && quantidade > 0) sincronizarLote();

  if (agora - ultimoStatus >= INTERVALO_STATUS_MS) {
    ultimoStatus = agora;
    imprimirResumo();
  }

  atualizarLeds();
  delay(10);
}
