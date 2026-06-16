#include <Arduino.h>
#include <DHT.h>

// ================= PIN ESP32 =================
#define POT_PIN   34
#define MQ_PIN    35
#define DHT_PIN   4
#define DHT_TYPE  DHT22

// Software SPI pins
#define SPI_SCK   18
#define SPI_MOSI  23
#define SPI_CS    5

DHT dht(DHT_PIN, DHT_TYPE);

// ================= DATA SENSOR =================
volatile uint16_t nilaiPot = 0;
volatile uint16_t nilaiMQ = 0;
volatile int16_t suhuX10 = 0;
volatile uint16_t lembabX10 = 0;
volatile uint8_t nilaiPWM = 0;

SemaphoreHandle_t dataMutex;

// ================= CHECKSUM =================
uint8_t hitungChecksum(uint8_t *data, uint8_t panjang) {
  uint8_t checksum = 0;

  for (uint8_t i = 0; i < panjang; i++) {
    checksum ^= data[i];
  }

  return checksum;
}

// ================= SOFTWARE SPI SEND BYTE =================
void softSPITransferByte(uint8_t data) {
  for (int i = 7; i >= 0; i--) {
    digitalWrite(SPI_MOSI, (data >> i) & 0x01);

    delayMicroseconds(300);
    digitalWrite(SPI_SCK, HIGH);

    delayMicroseconds(300);
    digitalWrite(SPI_SCK, LOW);

    delayMicroseconds(300);
  }
}

void kirimPaketSPI(uint8_t *paket, uint8_t panjang) {
  digitalWrite(SPI_CS, LOW);
  delayMicroseconds(1000);

  for (uint8_t i = 0; i < panjang; i++) {
    softSPITransferByte(paket[i]);
    delayMicroseconds(1000);
  }

  delayMicroseconds(1000);
  digitalWrite(SPI_CS, HIGH);
}

// ================= TASK BACA SENSOR =================
void TaskBacaSensor(void *parameter) {
  while (1) {
    uint16_t pot = analogRead(POT_PIN);
    uint16_t mq = analogRead(MQ_PIN);

    float suhu = dht.readTemperature();
    float lembab = dht.readHumidity();

    if (isnan(suhu)) {
      suhu = 0;
    }

    if (isnan(lembab)) {
      lembab = 0;
    }

    uint8_t pwm = map(pot, 0, 4095, 0, 255);

    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
      nilaiPot = pot;
      nilaiMQ = mq;
      suhuX10 = (int16_t)(suhu * 10.0);
      lembabX10 = (uint16_t)(lembab * 10.0);
      nilaiPWM = pwm;

      xSemaphoreGive(dataMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// ================= TASK KIRIM SPI =================
void TaskKirimSPI(void *parameter) {
  while (1) {
    uint16_t pot = 0;
    uint16_t mq = 0;
    int16_t suhu = 0;
    uint16_t lembab = 0;
    uint8_t pwm = 0;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
      pot = nilaiPot;
      mq = nilaiMQ;
      suhu = suhuX10;
      lembab = lembabX10;
      pwm = nilaiPWM;

      xSemaphoreGive(dataMutex);
    }

    /*
      Format paket 11 byte:
      [0]  Header 0xAA
      [1]  Pot high
      [2]  Pot low
      [3]  MQ high
      [4]  MQ low
      [5]  Suhu high
      [6]  Suhu low
      [7]  Lembab high
      [8]  Lembab low
      [9]  PWM
      [10] Checksum
    */

    uint8_t paket[11];

    paket[0] = 0xAA;
    paket[1] = (pot >> 8) & 0xFF;
    paket[2] = pot & 0xFF;
    paket[3] = (mq >> 8) & 0xFF;
    paket[4] = mq & 0xFF;
    paket[5] = (suhu >> 8) & 0xFF;
    paket[6] = suhu & 0xFF;
    paket[7] = (lembab >> 8) & 0xFF;
    paket[8] = lembab & 0xFF;
    paket[9] = pwm;
    paket[10] = hitungChecksum(paket, 10);

    kirimPaketSPI(paket, 11);

    Serial.println("Data Software SPI dikirim ke STM32");

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ================= TASK SERIAL =================
void TaskSerialMonitor(void *parameter) {
  while (1) {
    uint16_t pot = 0;
    uint16_t mq = 0;
    int16_t suhu = 0;
    uint16_t lembab = 0;
    uint8_t pwm = 0;

    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
      pot = nilaiPot;
      mq = nilaiMQ;
      suhu = suhuX10;
      lembab = lembabX10;
      pwm = nilaiPWM;

      xSemaphoreGive(dataMutex);
    }

    Serial.println("================================");
    Serial.print("POT ADC : ");
    Serial.println(pot);

    Serial.print("MQ ADC  : ");
    Serial.println(mq);

    Serial.print("Suhu    : ");
    Serial.print(suhu / 10.0);
    Serial.println(" C");

    Serial.print("Lembab  : ");
    Serial.print(lembab / 10.0);
    Serial.println(" %");

    Serial.print("PWM LED : ");
    Serial.print(pwm);
    Serial.print(" / 255 | ");
    Serial.print(map(pwm, 0, 255, 0, 100));
    Serial.println(" %");

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("ESP32 MASTER - Software SPI FreeRTOS");

  pinMode(POT_PIN, INPUT);
  pinMode(MQ_PIN, INPUT);

  pinMode(SPI_SCK, OUTPUT);
  pinMode(SPI_MOSI, OUTPUT);
  pinMode(SPI_CS, OUTPUT);

  digitalWrite(SPI_SCK, LOW);
  digitalWrite(SPI_MOSI, LOW);
  digitalWrite(SPI_CS, HIGH);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  dht.begin();

  dataMutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(
    TaskBacaSensor,
    "Task Baca Sensor",
    4096,
    NULL,
    2,
    NULL,
    1
  );

  xTaskCreatePinnedToCore(
    TaskKirimSPI,
    "Task Kirim SPI",
    4096,
    NULL,
    2,
    NULL,
    1
  );

  xTaskCreatePinnedToCore(
    TaskSerialMonitor,
    "Task Serial Monitor",
    4096,
    NULL,
    1,
    NULL,
    0
  );
}

void loop() {
}