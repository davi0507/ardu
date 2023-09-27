#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "secrets.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <ESP8266HTTPClient.h>

#define TIME_ZONE -5
// Defina suas credenciais da AWS S3 aqui
const char* awsAccessKey = "YOUR_AWS_ACCESS_KEY";
const char* awsSecretKey = "YOUR_AWS_SECRET_KEY";
const char* awsBucket = "YOUR_AWS_BUCKET";
const char* awsRegion = "YOUR_AWS_REGION";
const char* awsEndpoint = "YOUR_AWS_S3_ENDPOINT"; // Exemplo: s3.amazonaws.com

#define LDR A0
#define LED1 D1
#define LED2 D2
#define IR1 D5
#define IR2 D6

unsigned long lastMillis = 0;
unsigned long previousMillis = 0;
const long period = 5000;
bool light = false;
bool light2 = false;
unsigned long time_turned_on = 0;
unsigned long time_turned_on2 = 0;
int LDRValue = 0;

#define AWS_IOT_PUBLISH_TOPIC   "esp8266/pub"
#define AWS_IOT_SUBSCRIBE_TOPIC "esp8266/sub"

WiFiClientSecure net;

BearSSL::X509List cert(cacert);
BearSSL::X509List client_crt(client_cert);
BearSSL::PrivateKey key(privkey);

PubSubClient client(net);

time_t now;
time_t nowish = 1510592825;

void NTPConnect(void)
{
  Serial.print("Setting time using SNTP");
  configTime(TIME_ZONE * 3600, 0 * 3600, "pool.ntp.org", "time.nist.gov");
  now = time(nullptr);
  while (now < nowish)
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("done!");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));
}

void messageReceived(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Received [");
  Serial.print(topic);
  Serial.print("]: ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void connectAWS(void *pvParameters)
{
  delay(3000);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println(String("Attempting to connect to SSID: ") + String(WIFI_SSID));

  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(1000);
  }

  NTPConnect();

  net.setTrustAnchors(&cert);
  net.setClientRSACert(&client_crt, &key);

  client.setServer(MQTT_HOST, 8883);
  client.setCallback(messageReceived);

  Serial.println("Connecting to AWS IOT");

  while (!client.connect(THINGNAME))
  {
    Serial.print(".");
    delay(1000);
  }

  if (!client.connected()) {
    Serial.println("AWS IoT Timeout!");
    vTaskDelete(NULL);
  }

  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);

  Serial.println("AWS IoT Connected!");
  vTaskDelete(NULL);
}

void publishTask(void *pvParameters)
{
  for (;;)
  {
    StaticJsonDocument<200> doc;
    doc["time"] = millis();
    doc["LDRValue"] = LDRValue;
    char jsonBuffer[512];
    serializeJson(doc, jsonBuffer);

    if (client.connected())
    {
      client.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer);
    }

    vTaskDelay(5000 / portTICK_PERIOD_MS); // Delay for 5 seconds
  }
}

void setup()
{
  Serial.begin(115200);

  xTaskCreate(connectAWS, "AWSConnectTask", 4096, NULL, 1, NULL);
  xTaskCreate(publishTask, "PublishTask", 4096, NULL, 2, NULL);

  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(IR1, INPUT);
  pinMode(IR2, INPUT);
  pinMode(LDR, INPUT);
}

void loop()
{
  LDRValue = analogRead(LDR);

  if (LDRValue > 50) {
      analogWrite(LED1, 0);
      analogWrite(LED2, 0);
      light = false;
      light2 = false;
  } else {
    if (digitalRead(IR1) == LOW && light == false) {
      light = true;
      time_turned_on = millis();
      analogWrite(LED1, 255);
    } else if (light == false) {
      analogWrite(LED1, 127);
    }

    if (light == true && millis() - time_turned_on > period) {
      light = false;
      analogWrite(LED1, 127);
    }

    if (digitalRead(IR2) == LOW && light2 == false) {
      light2 = true;
      time_turned_on2 = millis();
      analogWrite(LED2, 255);
    } else if (light == false) {
      analogWrite(LED2, 127);
    }

    if (light2 == true && millis() - time_turned_on2 > period) {
      light2 = false;
      analogWrite(LED2, 127);
    }
  }
}


void publishTask(void *pvParameters)
{
  for (;;)
  {
    StaticJsonDocument<200> doc;
    doc["time"] = millis();
    doc["LDRValue"] = LDRValue;
    char jsonBuffer[512];
    serializeJson(doc, jsonBuffer);

    if (client.connected())
    {
      client.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer);

      // Enviar o JSON para o S3 após cada publicação MQTT
      HTTPClient http;
      http.begin(String("https://") + awsBucket + "." + awsEndpoint + "/your_filename.json");
      http.setAuthorization(awsAccessKey, awsSecretKey);
      http.addHeader("x-amz-content-sha256", "UNSIGNED-PAYLOAD"); // Para uploads sem assinatura
      http.addHeader("x-amz-storage-class", "STANDARD"); // Padrão, pode ser alterado conforme necessário
      http.addHeader("x-amz-acl", "private"); // Privado, pode ser alterado conforme necessário
      http.addHeader("x-amz-region", awsRegion);
      http.addHeader("Host", awsBucket + "." + awsEndpoint);
      int httpCode = http.POST(jsonBuffer);
      if (httpCode == HTTP_CODE_OK)
      {
        Serial.println("JSON enviado para o S3 com sucesso!");
      }
      else
      {
        Serial.println("Falha ao enviar o JSON para o S3.");
      }
      http.end();
    }

    vTaskDelay(5000 / portTICK_PERIOD_MS); // Delay for 5 seconds
  }
}
