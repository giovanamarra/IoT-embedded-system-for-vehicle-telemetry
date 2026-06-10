"""
Backend Telemetria Nivus
========================
- Inscreve no broker MQTT e recebe dados do Arduino
- Grava cada leitura no SQLite
- Serve dashboard web (Flask)
- Empurra dados ao vivo pro frontend via WebSocket (Socket.IO)
- Expõe API REST pra consultar histórico

Como rodar:
    pip install -r requirements.txt
    python app.py

Acessa: http://localhost:5003
"""

import json
import sqlite3
import threading
from datetime import datetime, timedelta
from pathlib import Path

import paho.mqtt.client as mqtt
from flask import Flask, jsonify, render_template, request
from flask_socketio import SocketIO

# =====================================================
# CONFIGURAÇÕES (devem bater com o Arduino!)
# =====================================================
MQTT_BROKER = "linux.etmg.com.br"
MQTT_PORT   = 1883
MQTT_USER   = "giovana"
MQTT_PASS   = "ibmec2026"
MQTT_TOPIC  = "ibmec/dados/status"   # << MESMO TÓPICO DO ARDUINO

DB_PATH = Path(__file__).parent / "telemetria.db"

# =====================================================
# BANCO DE DADOS
# =====================================================
def init_db():
    """Cria tabela se não existir."""
    conn = sqlite3.connect(DB_PATH)
    conn.execute("""
        CREATE TABLE IF NOT EXISTS leituras (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            rpm         INTEGER,
            velocidade  INTEGER,
            temperatura INTEGER,
            acelerador  REAL,
            ts_arduino  INTEGER,
            ts_servidor DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    """)
    # Índice por timestamp pra consultas históricas serem rápidas
    conn.execute("CREATE INDEX IF NOT EXISTS idx_ts ON leituras(ts_servidor)")
    conn.commit()
    conn.close()
    print(f"[DB] SQLite pronto em {DB_PATH}")


def salvar_leitura(dados: dict):
    """Insere uma leitura no banco."""
    conn = sqlite3.connect(DB_PATH)
    conn.execute("""
        INSERT INTO leituras (rpm, velocidade, temperatura, acelerador, ts_arduino)
        VALUES (?, ?, ?, ?, ?)
    """, (
        dados.get("rpm", 0),
        dados.get("vel", 0),
        dados.get("temp", 0),
        dados.get("throttle", 0.0),
        dados.get("ts", 0),
    ))
    conn.commit()
    conn.close()


# =====================================================
# FLASK + SOCKETIO
# =====================================================
app = Flask(__name__)
app.config["SECRET_KEY"] = "nivus-telemetria-secret"
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")


@app.route("/")
def index():
    return render_template("index.html")


@app.route("/api/historico")
def historico():
    """
    Retorna leituras filtradas por intervalo.
    Query params:
        - minutos: int (últimos N minutos, padrão 30)
        - limit:   int (máx de registros, padrão 500)
    """
    minutos = int(request.args.get("minutos", 30))
    limit   = int(request.args.get("limit", 500))

    desde = datetime.now() - timedelta(minutes=minutos)

    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    rows = conn.execute("""
        SELECT id, rpm, velocidade, temperatura, acelerador, ts_servidor
        FROM leituras
        WHERE ts_servidor >= ?
        ORDER BY ts_servidor DESC
        LIMIT ?
    """, (desde, limit)).fetchall()
    conn.close()

    return jsonify([dict(r) for r in rows])


@app.route("/api/estatisticas")
def estatisticas():
    """Resumão da viagem atual (últimos 30 min)."""
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    desde = datetime.now() - timedelta(minutes=30)

    row = conn.execute("""
        SELECT
            COUNT(*)             AS total_leituras,
            MAX(rpm)             AS rpm_max,
            AVG(rpm)             AS rpm_medio,
            MAX(velocidade)      AS vel_max,
            AVG(velocidade)      AS vel_media,
            MAX(temperatura)     AS temp_max,
            AVG(temperatura)     AS temp_media
        FROM leituras
        WHERE ts_servidor >= ?
    """, (desde,)).fetchone()
    conn.close()

    return jsonify(dict(row) if row else {})


# =====================================================
# MQTT CLIENT
# =====================================================
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"[MQTT] Conectado ao broker {MQTT_BROKER}")
        client.subscribe(MQTT_TOPIC)
        print(f"[MQTT] Inscrito em '{MQTT_TOPIC}'")
    elif rc == 4:
        print("[MQTT] Usuário/senha incorretos!")
    elif rc == 5:
        print("[MQTT] Não autorizado — confere as credenciais no broker")
    else:
        print(f"[MQTT] Falha na conexão, código {rc}")


def on_message(client, userdata, msg):
    """Cada mensagem recebida do Arduino passa por aqui."""
    try:
        payload = msg.payload.decode("utf-8")
        dados = json.loads(payload)
        print(f"[MQTT] RX: {dados}")

        # 1) Grava no banco
        salvar_leitura(dados)

        # 2) Empurra pro frontend em tempo real
        dados["recebido_em"] = datetime.now().isoformat()
        socketio.emit("telemetria", dados)

    except json.JSONDecodeError as e:
        print(f"[MQTT] Payload inválido: {msg.payload} - {e}")
    except Exception as e:
        print(f"[MQTT] Erro processando mensagem: {e}")


def iniciar_mqtt():
    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION1,
        client_id="backend_nivus",
    )
    client.username_pw_set(MQTT_USER, MQTT_PASS)
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    client.loop_forever()  # bloqueia neste thread


# =====================================================
# MAIN
# =====================================================
if __name__ == "__main__":
    init_db()

    # MQTT roda em thread separado pra não travar o Flask
    mqtt_thread = threading.Thread(target=iniciar_mqtt, daemon=True)
    mqtt_thread.start()

    print("[WEB] Servidor em http://localhost:5003")
    socketio.run(app, host="0.0.0.0", port=5003, debug=False, allow_unsafe_werkzeug=True)