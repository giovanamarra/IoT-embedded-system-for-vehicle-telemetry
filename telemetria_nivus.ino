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
 const char* APN          = "zap.vivo.com.br";        // ou "vivo.com.br", "tim.br", "gprs.oi.com.br"
 const char* APN_USER     = "vivo";               // varia por operadora
 const char* APN_PASS     = "vivo";
 
 const char* MQTT_BROKER  = "linux.etmg.com.br";   // teu servidor
 const int   MQTT_PORT    = 1883;
 const char* MQTT_CLIENT  = "nivus_gi_01";         // ID único no broker (≠ username)
 const char* MQTT_USER    = "giovana";
 const char* MQTT_PASS    = "ibmec2026";
 const char* MQTT_TOPIC   = "ibmec/dados/status";  // mesmo tópico do backend
 
 // >>>> COLOCA TEU NÚMERO AQUI (formato internacional) <<
 const char* MEU_TELEFONE = "+5531971591413";
 
 // Testes de diagnóstico no boot (desliga depois que tudo funcionar)
 const bool TESTE_LIGACAO = true;   // liga pro teu telefone pra provar que tem rede
 
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
 
 bool mqttConectado = false;
 
 unsigned long lastRequest = 0;
 unsigned long lastPublish = 0;
 const unsigned long REQUEST_INTERVAL = 250;   // pede PID a cada 250ms
 const unsigned long PUBLISH_INTERVAL = 2000;  // publica MQTT a cada 2s
 
 // =====================================================
 // SETUP
 // =====================================================
 void setup() {
   Serial.begin(9600);
   sim800.begin(115200);
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
 
   if (!sendAT("AT", "OK", 2000))
     Serial.println(F("[ERRO] SIM800L nao responde! Confere fiacao/alimentacao"));
 
   if (!sendAT("AT+CPIN?", "READY", 2000))
     Serial.println(F("[ERRO] SIM travado ou ausente!"));
 
   // Qualidade do sinal: +CSQ: X,Y -> X de 0 a 31 (>=10 = utilizavel)
   sendAT("AT+CSQ", "OK", 2000);
 
   if (!sendAT("AT+CREG?", ",1", 10000)) {
     // tenta roaming (,5) antes de desistir
     if (!sendAT("AT+CREG?", ",5", 5000))
       Serial.println(F("[ERRO] Nao registrou na rede celular!"));
   }
 
   // ---- TESTE: liga pro telefone pra provar que tem rede ----
   if (TESTE_LIGACAO) {
     testarLigacaoTelefone();
   }
 
   if (!sendAT("AT+CGATT=1", "OK", 10000))
     Serial.println(F("[ERRO] Falha ao anexar GPRS (2G disponivel?)"));
 
   // Configura APN
   sendAT("AT+SAPBR=3,1,\"Contype\",\"GPRS\"", "OK", 2000);
   String aux = "AT+SAPBR=3,1,\"APN\",\"" + String(APN) + "\"";
   sendAT(aux.c_str(), "OK", 2000);
 
   if (!sendAT("AT+SAPBR=1,1", "OK", 15000))
     Serial.println(F("[ERRO] Nao abriu conexao GPRS (confere APN)"));
 
   sendAT("AT+SAPBR=2,1", "OK", 2000);    // mostra o IP obtido
 
   // Conecta no broker MQTT via TCP
   mqttConectado = connectMQTT();
 
   if (mqttConectado)
     Serial.println(F("=== Sistema pronto: MQTT AUTENTICADO ==="));
   else
     Serial.println(F("=== ATENCAO: MQTT NAO CONECTOU ==="));
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
     if (!mqttConectado) {
       Serial.println(F("[MQTT] Desconectado, tentando reconectar..."));
       mqttConectado = connectMQTT();
     }
     if (mqttConectado) {
       publishTelemetry();
     }
     lastPublish = millis();
   }
 }
 
 // =====================================================
 // TESTE: LIGACAO TELEFONICA
 // =====================================================
 void testarLigacaoTelefone() {
   Serial.println(F("\n[TESTE] Ligando pro teu telefone..."));
   Serial.println(F("[TESTE] Se tocar = SIM registrada e com rede!"));
 
   String cmd = "ATD" + String(MEU_TELEFONE) + ";";
   sim800.println(cmd);
 
   // Deixa tocar por 15 segundos (NAO atende, so deixa tocar)
   unsigned long start = millis();
   while (millis() - start < 15000) {
     while (sim800.available()) {
       Serial.write(sim800.read());  // mostra status da chamada
     }
   }
 
   // Desliga a chamada
   sim800.println("ATH");
   delay(1000);
   while (sim800.available()) Serial.write(sim800.read());
   Serial.println(F("[TESTE] Chamada encerrada\n"));
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
 bool connectMQTT() {
   Serial.println(F("Conectando MQTT..."));
 
   // Abre conexão TCP com o broker
   sendAT("AT+CIPSHUT", "SHUT OK", 5000);
   sendAT("AT+CIPMUX=0", "OK", 2000);
 
   String cmd = "AT+CIPSTART=\"TCP\",\"" + String(MQTT_BROKER) + "\"," + String(MQTT_PORT);
   if (!sendAT(cmd.c_str(), "CONNECT OK", 15000)) {
     Serial.println(F("[MQTT] TCP falhou! (DNS? porta 1883 aberta? firewall?)"));
     return false;
   }
   Serial.println(F("[MQTT] TCP OK, enviando CONNECT..."));
 
   byte clientLen = strlen(MQTT_CLIENT);
   byte userLen   = strlen(MQTT_USER);
   byte passLen   = strlen(MQTT_PASS);
 
   // variable header (10) + client(2+n) + user(2+n) + pass(2+n)
   byte remainingLen = 10 + (2 + clientLen) + (2 + userLen) + (2 + passLen);
 
   sim800.print("AT+CIPSEND=");
   sim800.println(2 + remainingLen);   // +2 = fixed header (0x10 + remainingLen)
 
   // Espera o prompt '>' antes de mandar os bytes (delay fixo podia engolir dados)
   if (!esperarPrompt(3000)) {
     Serial.println(F("[MQTT] Sem prompt '>' do CIPSEND"));
     return false;
   }
 
   // ---- Pacote MQTT CONNECT (3.1.1) COM usuário e senha ----
   sim800.write((byte)0x10);                            // CONNECT
   sim800.write(remainingLen);                          // remaining length
 
   // Variable header
   sim800.write((byte)0x00); sim800.write((byte)0x04);  // tamanho "MQTT"
   sim800.print("MQTT");
   sim800.write((byte)0x04);                            // protocol level 3.1.1
   sim800.write((byte)0xC2);                            // flags: USER + PASS + clean session
   sim800.write((byte)0x00); sim800.write((byte)0x3C);  // keepalive 60s
 
   // Payload: client id
   sim800.write((byte)0x00); sim800.write(clientLen);
   sim800.print(MQTT_CLIENT);
   // Payload: username
   sim800.write((byte)0x00); sim800.write(userLen);
   sim800.print(MQTT_USER);
   // Payload: password
   sim800.write((byte)0x00); sim800.write(passLen);
   sim800.print(MQTT_PASS);
 
   // ---- Espera o CONNACK do broker (0x20 0x02 0x?? 0xRC) ----
   return esperarConnack(10000);
 }
 
 // Espera o prompt '>' que o SIM800L manda quando ta pronto pra receber dados
 bool esperarPrompt(unsigned long timeout) {
   unsigned long start = millis();
   while (millis() - start < timeout) {
     if (sim800.available() && sim800.read() == '>') return true;
   }
   return false;
 }
 
 // Le a resposta do broker e procura o pacote CONNACK
 bool esperarConnack(unsigned long timeout) {
   unsigned long start = millis();
   byte prev = 0;
 
   while (millis() - start < timeout) {
     if (sim800.available()) {
       byte b = sim800.read();
 
       // CONNACK = 0x20 0x02 [session] [return code]
       if (prev == 0x20 && b == 0x02) {
         // proximos 2 bytes: session present + return code
         unsigned long t2 = millis();
         byte rc[2];
         byte got = 0;
         while (got < 2 && millis() - t2 < 2000) {
           if (sim800.available()) rc[got++] = sim800.read();
         }
         if (got == 2) {
           switch (rc[1]) {
             case 0x00:
               Serial.println(F("[MQTT] CONNACK OK - autenticado!"));
               return true;
             case 0x04:
               Serial.println(F("[MQTT] RECUSADO: usuario/senha errados"));
               return false;
             case 0x05:
               Serial.println(F("[MQTT] RECUSADO: nao autorizado"));
               return false;
             default:
               Serial.print(F("[MQTT] RECUSADO: codigo "));
               Serial.println(rc[1]);
               return false;
           }
         }
       }
       prev = b;
     }
   }
   Serial.println(F("[MQTT] Timeout esperando CONNACK (broker nao respondeu)"));
   return false;
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
 
   if (!esperarPrompt(3000)) {
     Serial.println(F("[MQTT] CIPSEND falhou - conexao caiu?"));
     mqttConectado = false;
     return;
   }
 
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