/*
 * ===================================================================
 * TELEMETRIA AUTOMOTIVA - VW NIVUS 2021
 * ===================================================================
 * Hardware:
 *   - Arduino Mega 2560
 *   - Módulo MCP2515 (CAN bus)
 *   - Módulo SIM800L (GPRS/MQTT)
 *
 * Conexões MCP2515 -> Mega:
 *   VCC -> 5V    GND -> GND
 *   SCK -> 52    SO  -> 50    SI -> 51    CS -> 53    INT -> 2
 *
 * Conexões SIM800L -> Mega:
 *   VCC -> Fonte externa 4V/2A (NUNCA no 5V do Arduino!)
 *   GND -> GND (comum com Arduino)
 *   TX  -> Pino 19 (RX1 do Mega)
 *   RX  -> Pino 18 (TX1 do Mega) via divisor de tensão (5V->3V)
 * ===================================================================
 */

#include <SPI.h>
#include <mcp_can.h>

// ============ CONFIGURAÇÕES ============
const char* APN          = "claro.com.br";        // ou "vivo.com.br", "tim.br", "gprs.oi.com.br"
const char* APN_USER     = "claro";               // varia por operadora
const char* APN_PASS     = "claro";

const char* MQTT_BROKER  = "broker.hivemq.com";   // broker público gratuito pra testes
const int   MQTT_PORT    = 1883;
const char* MQTT_CLIENT  = "nivus_telemetria_01"; // troque pra algo único!
const char* MQTT_TOPIC   = "nivus/gi/telemetria"; // troque "gi" por algo único pra evitar conflito

// ============ HARDWARE ============
const int SPI_CS_PIN  = 53;
const int CAN_INT_PIN = 2;
MCP_CAN CAN(SPI_CS_PIN);

#define sim800 Serial1  // Mega tem 4 seriais; usamos Serial1 (pinos 18/19)

// ============ OBD-II ============
#define OBD_REQUEST_ID    0x7DF
#define OBD_RESPONSE_MIN  0x7E8
#define OBD_RESPONSE_MAX  0x7EF

byte pids[] = {0x0C, 0x0D, 0x05, 0x11}; // RPM, Velocidade, Temp, Acelerador
const byte numPids = sizeof(pids);
byte pidIndex = 0;

// Buffer de dados (último valor lido de cada PID)
struct TelemetryData {
  int rpm = 0;
  int velocidade = 0;
  int temperatura = 0;
  float acelerador = 0.0;
  unsigned long timestamp = 0;
};
TelemetryData dados;

unsigned long lastRequest = 0;
unsigned long lastPublish = 0;
const unsigned long REQUEST_INTERVAL = 250;   // pede PID a cada 250ms
const unsigned long PUBLISH_INTERVAL = 2000;  // publica MQTT a cada 2s

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  sim800.begin(9600);
  pinMode(CAN_INT_PIN, INPUT);

  Serial.println(F("=== Telemetria Nivus iniciando ==="));

  // --- Inicia CAN ---
  while (CAN_OK != CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ)) {
    Serial.println(F("Falha MCP2515, retry..."));
    delay(500);
  }
  CAN.setMode(MCP_NORMAL);
  Serial.println(F("CAN OK"));

  // --- Inicia SIM800L ---
  Serial.println(F("Inicializando SIM800L..."));
  delay(3000);
  
  sendAT("AT", "OK", 2000);
  sendAT("AT+CPIN?", "READY", 2000);     // SIM destravado?
  sendAT("AT+CREG?", "0,1", 5000);       // Registrado na rede?
  sendAT("AT+CGATT=1", "OK", 5000);      // Anexa ao GPRS
  
  // Configura APN
  sendAT("AT+SAPBR=3,1,\"Contype\",\"GPRS\"", "OK", 2000);
  String aux = "AT+SAPBR=3,1,\"APN\",\"" + String(APN) + "\"";
  sendAT(aux.c_str(), "OK", 2000);
  sendAT("AT+SAPBR=1,1", "OK", 10000);   // Abre conexão GPRS
  sendAT("AT+SAPBR=2,1", "OK", 2000);    // Pega IP (confirma que conectou)

  // Conecta no broker MQTT via TCP
  connectMQTT();
  
  Serial.println(F("=== Sistema pronto ==="));
}

// =====================================================
// LOOP
// =====================================================
void loop() {
  // 1) Pede um PID em rotação
  if (millis() - lastRequest > REQUEST_INTERVAL) {
    requestPID(pids[pidIndex]);
    pidIndex = (pidIndex + 1) % numPids;
    lastRequest = millis();
  }

  // 2) Lê resposta CAN se houver
  if (!digitalRead(CAN_INT_PIN)) {
    long unsigned int rxId;
    unsigned char len = 0;
    unsigned char buf[8];
    CAN.readMsgBuf(&rxId, &len, buf);

    unsigned long actualId = rxId & 0x1FFFFFFF;
    bool isExtended = (rxId & 0x80000000);

    if (!isExtended && actualId >= OBD_RESPONSE_MIN && actualId <= OBD_RESPONSE_MAX) {
      decodeAndStore(buf[2], buf);
    }
  }

  // 3) Publica no MQTT periodicamente
  if (millis() - lastPublish > PUBLISH_INTERVAL) {
    publishTelemetry();
    lastPublish = millis();
  }
}

// =====================================================
// OBD-II
// =====================================================
void requestPID(byte pid) {
  byte data[8] = {0x02, 0x01, pid, 0, 0, 0, 0, 0};
  CAN.sendMsgBuf(OBD_REQUEST_ID, 0, 8, data);
}

void decodeAndStore(byte pid, unsigned char* buf) {
  dados.timestamp = millis();
  
  switch (pid) {
    case 0x0C:
      dados.rpm = ((buf[3] * 256) + buf[4]) / 4;
      break;
    case 0x0D:
      dados.velocidade = buf[3];
      break;
    case 0x05:
      dados.temperatura = buf[3] - 40;
      break;
    case 0x11:
      dados.acelerador = (buf[3] * 100.0) / 255.0;
      break;
  }
}

// =====================================================
// MQTT VIA SIM800L
// =====================================================
void connectMQTT() {
  Serial.println(F("Conectando MQTT..."));
  
  // Abre conexão TCP com broker
  sendAT("AT+CIPSHUT", "SHUT OK", 5000);
  sendAT("AT+CIPMUX=0", "OK", 2000);
  
  String cmd = "AT+CIPSTART=\"TCP\",\"" + String(MQTT_BROKER) + "\"," + String(MQTT_PORT);
  sendAT(cmd.c_str(), "CONNECT OK", 15000);

  // Envia pacote MQTT CONNECT (montado byte a byte conforme MQTT 3.1.1)
  byte clientLen = strlen(MQTT_CLIENT);
  byte remainingLen = 12 + 2 + clientLen; // header + client id

  sim800.print("AT+CIPSEND=");
  sim800.println(14 + clientLen);
  delay(500);

  // Pacote MQTT CONNECT
  sim800.write((byte)0x10);                      // CONNECT
  sim800.write(remainingLen);                    // tamanho restante
  sim800.write((byte)0x00); sim800.write((byte)0x04); // protocol name length
  sim800.print("MQTT");
  sim800.write((byte)0x04);                      // protocol level (3.1.1)
  sim800.write((byte)0x02);                      // flags (clean session)
  sim800.write((byte)0x00); sim800.write((byte)0x3C); // keepalive 60s
  sim800.write((byte)0x00); sim800.write(clientLen);  // client id length
  sim800.print(MQTT_CLIENT);
  
  delay(2000);
  Serial.println(F("MQTT CONNECT enviado"));
}

void publishTelemetry() {
  // Monta JSON
  String payload = "{";
  payload += "\"rpm\":" + String(dados.rpm) + ",";
  payload += "\"vel\":" + String(dados.velocidade) + ",";
  payload += "\"temp\":" + String(dados.temperatura) + ",";
  payload += "\"throttle\":" + String(dados.acelerador, 1) + ",";
  payload += "\"ts\":" + String(dados.timestamp);
  payload += "}";

  Serial.print(F("Publicando: "));
  Serial.println(payload);

  byte topicLen = strlen(MQTT_TOPIC);
  byte payloadLen = payload.length();
  byte remainingLen = 2 + topicLen + payloadLen;

  sim800.print("AT+CIPSEND=");
  sim800.println(2 + remainingLen);
  delay(300);

  // Pacote MQTT PUBLISH (QoS 0)
  sim800.write((byte)0x30);
  sim800.write(remainingLen);
  sim800.write((byte)0x00); sim800.write(topicLen);
  sim800.print(MQTT_TOPIC);
  sim800.print(payload);
  
  delay(500);
}

// =====================================================
// HELPER AT
// =====================================================
bool sendAT(const char* cmd, const char* expected, unsigned long timeout) {
  sim800.println(cmd);
  Serial.print(F(">> ")); Serial.println(cmd);
  
  unsigned long start = millis();
  String response = "";
  
  while (millis() - start < timeout) {
    while (sim800.available()) {
      char c = sim800.read();
      response += c;
      Serial.write(c);
    }
    if (response.indexOf(expected) != -1) {
      Serial.println();
      return true;
    }
  }
  Serial.println(F("\n[TIMEOUT]"));
  return false;
}
