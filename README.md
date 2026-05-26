# Telemetria Nivus 🏎️

Sistema de telemetria automotiva: lê dados OBD-II do Nivus 2021, envia via 2G/GPRS por MQTT e exibe num dashboard web ao vivo.

## Arquitetura

```
Carro → CAN bus → MCP2515 → Arduino Mega → SIM800L → 2G/GPRS
                                                          ↓
                                                   Broker MQTT (HiveMQ)
                                                          ↓
                                                   Backend Python
                                                    ↓         ↓
                                                  SQLite   WebSocket
                                                              ↓
                                                         Dashboard
```

## Setup

### 1. Arduino

**Bibliotecas necessárias** (Library Manager):
- `mcp_can` (coryjfowler)

**Antes de gravar, edite no `.ino`:**
- `APN`, `APN_USER`, `APN_PASS` → conforme sua operadora do chip:
  - Claro:  `claro.com.br` / `claro` / `claro`
  - Vivo:   `zap.vivo.com.br` / `vivo` / `vivo`
  - TIM:    `tim.br` / `tim` / `tim`
  - Oi:     `gprs.oi.com.br` / `oi` / `oi`
- `MQTT_TOPIC` → mude `nivus/gi/telemetria` pra algo único (o broker é público, qualquer um pode escutar!)
- `MQTT_CLIENT` → use um ID único também

### 2. Backend Python

```bash
cd backend
pip install -r requirements.txt
python app.py
```

**Importante:** edite `MQTT_TOPIC` no `app.py` pra ser o **mesmo** do Arduino.

Acessa: http://localhost:5000

### 3. Testando sem o carro

Se quer testar o dashboard antes de ir pro carro, instala o `mosquitto-clients` e publica manualmente:

```bash
# Em outro terminal:
mosquitto_pub -h broker.hivemq.com -t "nivus/gi/telemetria" \
  -m '{"rpm":2500,"vel":60,"temp":85,"throttle":35.5,"ts":12345}'
```

O dashboard deve mostrar o valor na hora.

## Cuidados importantes

### SIM800L

- **Alimentação:** precisa de fonte externa 3.7-4.2V capaz de fornecer **picos de 2A**. NUNCA alimente pelo Arduino — vai resetar.
- **Antena:** sem antena = sem sinal. Use a helicoidal que vem no kit.
- **Divisor de tensão no RX:** o pino RX do SIM800L é 3V, e o TX do Mega é 5V. Use divisor resistivo (10kΩ + 20kΩ) ou queima.

### Cobertura 2G

O SIM800L só funciona em 2G. Antes de testar em movimento, confirma que tem 2G na sua região — em algumas cidades brasileiras já foi desligado. Faz teste estático primeiro.

### Broker público

O `broker.hivemq.com` é gratuito e sem autenticação — qualquer um que souber o tópico pode ler seus dados. Pra produção, considera HiveMQ Cloud (free tier com auth) ou Mosquitto self-hosted.

## Próximos passos

- [ ] Adicionar mais PIDs (MAF, pressão coletor, consumo instantâneo)
- [ ] GPS no SIM800L (ele tem!) pra registrar rota
- [ ] Autenticação no MQTT
- [ ] Exportar viagens em CSV/GPX
- [ ] OTA pra atualizar firmware sem cabo
