#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <time.h>

const char* SSID = "FIAP-IOT";
const char* PASSWORD = "COLOQUE_A_SENHA_WIFI_AQUI";

const char* BROKER_MQTT = "3.135.234.101";
const int BROKER_PORT = 1883;
const char* MQTT_USER = "";
const char* MQTT_PASS = "";

const char* TOPIC_PREFIX = "/TEF";
const char* DEVICE_ID = "vinheria-mgrg";

#define PIN_DHT 4
#define DHTTYPE DHT11
#define PIN_LDR 34
#define PIN_LED 2

const unsigned long INTERVALO_MS = 5000;

DHT dht(PIN_DHT, DHTTYPE);
WiFiClient espClient;
PubSubClient mqtt(espClient);

String topicAttrs;
String topicCmd;
String topicCmdExe;

unsigned long ultimoEnvio = 0;
bool ledLigado = false;

String timestampISO() {
  struct tm t;
  if (!getLocalTime(&t, 2000)) {
    return "1970-01-01T00:00:00Z";
  }
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
  return String(buf);
}

void conectaWiFi() {
  Serial.printf("Conectando ao Wi-Fi %s", SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("Wi-Fi conectado!");
  Serial.print("IP local: ");
  Serial.println(WiFi.localIP());
}

void conectaMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Conectando ao broker MQTT... ");
    String clientId = String(DEVICE_ID) + "-" +
                      String((uint32_t)ESP.getEfuseMac(), HEX);
    bool conectado;
    if (strlen(MQTT_USER) > 0) {
      conectado = mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS);
    } else {
      conectado = mqtt.connect(clientId.c_str());
    }
    if (conectado) {
      Serial.println("MQTT conectado!");
      mqtt.subscribe(topicCmd.c_str());
      Serial.print("Inscrito no topico: ");
      Serial.println(topicCmd);
    } else {
      Serial.print("Falha MQTT. Codigo: ");
      Serial.println(mqtt.state());
      delay(3000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String mensagem = "";
  for (unsigned int i = 0; i < length; i++) {
    mensagem += (char)payload[i];
  }
  mensagem.trim();
  Serial.print("Comando recebido: ");
  Serial.println(mensagem);

  String comandoLigar = String(DEVICE_ID) + "@on|";
  String comandoDesligar = String(DEVICE_ID) + "@off|";

  if (mensagem == comandoLigar) {
    ledLigado = true;
    digitalWrite(PIN_LED, HIGH);
    String resposta = String(DEVICE_ID) + "@on|LED ligado";
    mqtt.publish(topicCmdExe.c_str(), resposta.c_str());
    Serial.println("LED LIGADO");
  } else if (mensagem == comandoDesligar) {
    ledLigado = false;
    digitalWrite(PIN_LED, LOW);
    String resposta = String(DEVICE_ID) + "@off|LED desligado";
    mqtt.publish(topicCmdExe.c_str(), resposta.c_str());
    Serial.println("LED DESLIGADO");
  } else {
    Serial.println("Comando nao reconhecido");
  }
}

void publicaLeituras() {
  float temperatura = dht.readTemperature();
  float umidade = dht.readHumidity();
  int ldrRaw = analogRead(PIN_LDR);
  int luminosidade = map(ldrRaw, 0, 4095, 0, 100);

  if (isnan(temperatura) || isnan(umidade)) {
    Serial.println("Erro ao ler DHT11!");
    return;
  }

  String timestamp = timestampISO();
  String payload =
      "t|" + String(temperatura, 1) +
      "|h|" + String(umidade, 1) +
      "|l|" + String(luminosidade) +
      "|ts|" + timestamp;

  mqtt.publish(topicAttrs.c_str(), payload.c_str());
  mqtt.publish((topicAttrs + "/t").c_str(), String(temperatura, 1).c_str());
  mqtt.publish((topicAttrs + "/h").c_str(), String(umidade, 1).c_str());
  mqtt.publish((topicAttrs + "/l").c_str(), String(luminosidade).c_str());

  Serial.println("--------------------------");
  Serial.print("Temperatura: ");
  Serial.print(temperatura);
  Serial.println(" C");
  Serial.print("Umidade: ");
  Serial.print(umidade);
  Serial.println(" %");
  Serial.print("Luminosidade: ");
  Serial.print(luminosidade);
  Serial.println(" %");
  Serial.print("LDR bruto: ");
  Serial.println(ldrRaw);
  Serial.print("Data/Hora: ");
  Serial.println(timestamp);
  Serial.println("--------------------------");
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  pinMode(PIN_LDR, INPUT);
  dht.begin();

  topicAttrs = String(TOPIC_PREFIX) + "/" + DEVICE_ID + "/attrs";
  topicCmd = String(TOPIC_PREFIX) + "/" + DEVICE_ID + "/cmd";
  topicCmdExe = String(TOPIC_PREFIX) + "/" + DEVICE_ID + "/cmdexe";

  conectaWiFi();
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  for (int i = 0; i < 50 && time(nullptr) < 100000; i++) {
    delay(200);
  }
  mqtt.setServer(BROKER_MQTT, BROKER_PORT);
  mqtt.setCallback(callback);
  Serial.println("Sistema Vinheria Agnello iniciado!");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    conectaWiFi();
  }
  if (!mqtt.connected()) {
    conectaMQTT();
  }
  mqtt.loop();

  if (millis() - ultimoEnvio >= INTERVALO_MS) {
    ultimoEnvio = millis();
    publicaLeituras();
  }
}
