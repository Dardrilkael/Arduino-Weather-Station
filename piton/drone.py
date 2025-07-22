import sys
import json
from PyQt6.QtWidgets import (
    QApplication, QWidget, QVBoxLayout, QTextEdit, QLineEdit, QPushButton, QLabel, QHBoxLayout
)
from PyQt6.QtCore import pyqtSignal, QObject
import paho.mqtt.client as mqtt

# Configurações MQTT
MQTT_BROKER = "broker.gpicm-ufrj.tec.br"
MQTT_PORT = 1883
MQTT_USERNAME = "telemetria"
MQTT_PASSWORD = "kancvx8thz9FCN5jyq"
LOG_FILE = "received_messages.txt"


class Communicator(QObject):
    message_received = pyqtSignal(str)


class MQTTClient:
    def __init__(self, communicator):
        self.client = mqtt.Client()
        self.communicator = communicator
        self.topic = None
        self.connected = False

        self.client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message

    def connect_and_subscribe(self, topic):
        self.topic = topic
        self.client.connect(MQTT_BROKER, MQTT_PORT, 60)
        self.client.loop_start()
        self.connected = True

    def on_connect(self, client, userdata, flags, rc):
        print(f"Conectado com código: {rc}")
        if self.topic:
            client.subscribe(self.topic)
            print(f"Inscrito no tópico: {self.topic}")

    def on_message(self, client, userdata, msg):
        message = msg.payload.decode("utf-8")
        full_message = f"{message}"
        print(full_message)
        self.communicator.message_received.emit(full_message)
        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write(full_message + "\n")

    def publish(self, message):
        if self.connected and self.topic:
            self.client.publish(self.topic, message)

    def disconnect(self):
        if self.connected:
            self.client.loop_stop()
            self.client.disconnect()
            self.connected = False
            print("Desconectado do broker.")


class MQTTApp(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("MQTT Station Tool")
        self.setGeometry(100, 100, 600, 500)

        self.communicator = Communicator()
        self.communicator.message_received.connect(self.display_message)

        self.mqtt_client = MQTTClient(self.communicator)
        self.topic = None

        self.init_ui()

    def init_ui(self):
        layout = QVBoxLayout()

        # Estação
        station_layout = QHBoxLayout()
        self.station_input = QLineEdit()
        self.station_input.setPlaceholderText("Digite a estação (ex: est300)")
        self.connect_button = QPushButton("Conectar")
        self.connect_button.clicked.connect(self.connect_to_station)
        self.disconnect_button = QPushButton("Desconectar")
        self.disconnect_button.clicked.connect(self.disconnect_from_broker)
        self.disconnect_button.setEnabled(False)
        station_layout.addWidget(self.station_input)
        station_layout.addWidget(self.connect_button)
        station_layout.addWidget(self.disconnect_button)

        # Mensagens recebidas
        self.output_label = QLabel("Mensagens recebidas:")
        self.text_display = QTextEdit()
        self.text_display.setReadOnly(True)

        # Envio
        self.input_label = QLabel("Enviar mensagem:")
        self.input_box = QLineEdit()
        self.send_button = QPushButton("Enviar")
        self.send_button.clicked.connect(self.send_message)

        layout.addLayout(station_layout)
        layout.addWidget(self.output_label)
        layout.addWidget(self.text_display)
        layout.addWidget(self.input_label)
        layout.addWidget(self.input_box)
        layout.addWidget(self.send_button)

        self.setLayout(layout)

    def connect_to_station(self):
        estname = self.station_input.text().strip()
        if estname:
            self.topic = f"/prefeituras/macae/estacoes/{estname}"
            self.mqtt_client.connect_and_subscribe(self.topic)
            self.text_display.append(f"✅ Conectado à estação: {estname}")
            self.connect_button.setEnabled(False)
            self.station_input.setEnabled(False)
            self.disconnect_button.setEnabled(True)
        else:
            self.text_display.append("⚠️ Nome da estação não pode estar vazio.")

    def disconnect_from_broker(self):
        self.mqtt_client.disconnect()
        self.text_display.append("⛔ Desconectado do broker.")
        self.connect_button.setEnabled(True)
        self.station_input.setEnabled(True)
        self.disconnect_button.setEnabled(False)

    def display_message(self, message):
        self.text_display.append(message)

    def send_message(self):
        message = self.input_box.text().strip()
        if message:
            self.mqtt_client.publish(message)
            self.input_box.clear()
        else:
            self.text_display.append("⚠️ Mensagem vazia não enviada.")


if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MQTTApp()
    window.show()
    sys.exit(app.exec())
