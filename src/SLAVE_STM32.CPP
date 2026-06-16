#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ================= LCD =================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ================= PIN STM32 =================
#define SOFT_SPI_CS    PA4
#define SOFT_SPI_SCK   PA5
#define SOFT_SPI_MOSI  PA7
#define LED_PWM_PIN    PA8

// ================= DATA DARI ESP32 =================
volatile uint16_t adcPot = 0;
volatile uint16_t adcMQ = 0;
volatile int16_t suhuX10 = 0;
volatile uint16_t lembabX10 = 0;
volatile uint8_t pwmLED = 0;

volatile uint32_t jumlahPaketMasuk = 0;
volatile uint32_t jumlahByteMasuk = 0;

// ================= BUFFER SPI =================
volatile uint8_t bitCounter = 0;
volatile uint8_t currentByte = 0;
volatile uint8_t bufferSPI[11];
volatile uint8_t byteIndex = 0;

// ================= TIMER =================
unsigned long lastLCD = 0;
unsigned long lastPWM = 0;
uint8_t halaman = 0;

// ================= CHECKSUM =================
uint8_t hitungChecksum(volatile uint8_t *data, uint8_t panjang) {
  uint8_t checksum = 0;

  for (uint8_t i = 0; i < panjang; i++) {
    checksum ^= data[i];
  }

  return checksum;
}

// ================= RESET BUFFER =================
void resetSPIBuffer() {
  bitCounter = 0;
  currentByte = 0;
  byteIndex = 0;
}

// ================= INTERRUPT CS =================
void ISR_CS_Change() {
  if (digitalRead(SOFT_SPI_CS) == LOW) {
    resetSPIBuffer();
  }
}

// ================= INTERRUPT SCK =================
void ISR_SCK_Rising() {
  if (digitalRead(SOFT_SPI_CS) == HIGH) {
    return;
  }

  uint8_t bitData = digitalRead(SOFT_SPI_MOSI);

  currentByte <<= 1;

  if (bitData) {
    currentByte |= 0x01;
  }

  bitCounter++;

  if (bitCounter >= 8) {
    uint8_t data = currentByte;

    bitCounter = 0;
    currentByte = 0;

    jumlahByteMasuk++;

    // cari header 0xAA
    if (byteIndex == 0) {
      if (data == 0xAA) {
        bufferSPI[byteIndex] = data;
        byteIndex++;
      }
    } else {
      bufferSPI[byteIndex] = data;
      byteIndex++;

      if (byteIndex >= 11) {
        uint8_t checksumTerima = bufferSPI[10];
        uint8_t checksumHitung = hitungChecksum(bufferSPI, 10);

        if (checksumTerima == checksumHitung) {
          adcPot = ((uint16_t)bufferSPI[1] << 8) | bufferSPI[2];
          adcMQ = ((uint16_t)bufferSPI[3] << 8) | bufferSPI[4];
          suhuX10 = ((int16_t)bufferSPI[5] << 8) | bufferSPI[6];
          lembabX10 = ((uint16_t)bufferSPI[7] << 8) | bufferSPI[8];
          pwmLED = bufferSPI[9];

          jumlahPaketMasuk++;
        }

        byteIndex = 0;
      }
    }
  }
}

// ================= UPDATE PWM =================
void updatePWM() {
  if (millis() - lastPWM >= 20) {
    lastPWM = millis();
    analogWrite(LED_PWM_PIN, pwmLED);
  }
}

// ================= UPDATE LCD =================
void updateLCD() {
  if (millis() - lastLCD < 1000) {
    return;
  }

  lastLCD = millis();

  uint16_t pot = adcPot;
  uint16_t mq = adcMQ;
  int16_t suhu = suhuX10;
  uint16_t lembab = lembabX10;
  uint8_t pwm = pwmLED;

  uint32_t paket = jumlahPaketMasuk;
  uint32_t byteMasuk = jumlahByteMasuk;

  lcd.clear();

  if (paket == 0) {
    lcd.setCursor(0, 0);
    lcd.print("Waiting data");

    lcd.setCursor(0, 1);
    lcd.print("Byte:");
    lcd.print(byteMasuk);
  } 
  
  else {
    uint8_t persenLED = map(pwm, 0, 255, 0, 100);

    if (halaman == 0) {
      lcd.setCursor(0, 0);
      lcd.print("POT:");
      lcd.print(pot);

      lcd.setCursor(0, 1);
      lcd.print("LED:");
      lcd.print(persenLED);
      lcd.print("% PWM:");
      lcd.print(pwm);
    } 
    
    else if (halaman == 1) {
      lcd.setCursor(0, 0);
      lcd.print("MQ:");
      lcd.print(mq);

      lcd.setCursor(0, 1);

      if (mq < 1500) {
        lcd.print("Status: AMAN");
      } else if (mq < 3000) {
        lcd.print("Status: WASPADA");
      } else {
        lcd.print("Status: BAHAYA");
      }
    } 
    
    else if (halaman == 2) {
      lcd.setCursor(0, 0);
      lcd.print("Temp:");
      lcd.print(suhu / 10.0);
      lcd.print((char)223);
      lcd.print("C");

      lcd.setCursor(0, 1);
      lcd.print("Hum:");
      lcd.print(lembab / 10.0);
      lcd.print("%");
    }

    halaman++;

    if (halaman > 2) {
      halaman = 0;
    }
  }
}

// ================= SETUP =================
void setup() {
  delay(1000);

  // LCD I2C
  Wire.setSDA(PB7);
  Wire.setSCL(PB6);
  Wire.begin();

  lcd.init();
  lcd.backlight();
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("STM32 Start");
  lcd.setCursor(0, 1);
  lcd.print("No RTOS Mode");
  delay(1500);

  // Software SPI
  pinMode(SOFT_SPI_CS, INPUT_PULLUP);
  pinMode(SOFT_SPI_SCK, INPUT);
  pinMode(SOFT_SPI_MOSI, INPUT);

  attachInterrupt(digitalPinToInterrupt(SOFT_SPI_CS), ISR_CS_Change, CHANGE);
  attachInterrupt(digitalPinToInterrupt(SOFT_SPI_SCK), ISR_SCK_Rising, RISING);

  // PWM LED
  pinMode(LED_PWM_PIN, OUTPUT);
  analogWriteResolution(8);
  analogWrite(LED_PWM_PIN, 0);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ready");
  lcd.setCursor(0, 1);
  lcd.print("Waiting SPI");
  delay(1000);
}

// ================= LOOP =================
void loop() {
  updatePWM();
  updateLCD();
}