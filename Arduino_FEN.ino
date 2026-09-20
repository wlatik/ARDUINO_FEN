#include <max6675.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>

// --- ПИНЫ ---
const int thermoSO  = 12;
const int thermoCS  = 10; // D10 под вашу физическую разводку
const int thermoCLK = 13;
MAX6675 thermocouple(thermoCLK, thermoCS, thermoSO);

const int HEATER_PIN = 6; // D6 управляет MOC3083

const int BTN_TOGGLE = 2; // Кнопка D2 "Нагрев Вкл/Выкл"
const int BTN_DOWN   = 4; // Кнопка D4 "Минус"
const int BTN_UP     = 5; // Кнопка D5 "Плюс"

Adafruit_SSD1306 display(128, 64, &Wire, -1);

// --- ПЕРЕМЕННЫЕ СОСТОЯНИЯ ---
bool heaterEnabled = false;
int targetTemp = 200;
const int MIN_TEMP = 50;
const int MAX_TEMP = 450;
const int EEPROM_ADDR = 0;

// --- ПИД-РЕГУЛЯТОР ---
float Kp = 5.0, Ki = 0.05, Kd = 12.0;
const unsigned long WINDOW_SIZE = 1000; // Окно 1 сек
unsigned long windowStartTime;
float integral = 0, prevError = 0;
float pidOutput = 0;

// --- ТАЙМЕРЫ ---
unsigned long lastBtnCheck = 0;
unsigned long eepromSaveTimer = 0;
bool eepromNeedSave = false;
unsigned long lastTempRead = 0;

void setup() {
  pinMode(HEATER_PIN, OUTPUT);
  digitalWrite(HEATER_PIN, LOW);

  pinMode(BTN_TOGGLE, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(WHITE);

  int savedTemp = 0;
  EEPROM.get(EEPROM_ADDR, savedTemp);
  if (savedTemp >= MIN_TEMP && savedTemp <= MAX_TEMP) {
    targetTemp = savedTemp;
  }

  windowStartTime = millis();
}

void loop() {
  unsigned long now = millis();

  // 1. ОПРОС КНОПОК
  if (now - lastBtnCheck >= 150) {
    lastBtnCheck = now;

    if (digitalRead(BTN_TOGGLE) == LOW) {
      heaterEnabled = !heaterEnabled;
      if (!heaterEnabled) {
        digitalWrite(HEATER_PIN, LOW);
        integral = 0;
      }
    }

    if (digitalRead(BTN_DOWN) == LOW) {
      targetTemp -= 5;
      if (targetTemp < MIN_TEMP) targetTemp = MIN_TEMP;
      eepromNeedSave = true;
      eepromSaveTimer = now;
    }

    if (digitalRead(BTN_UP) == LOW) {
      targetTemp += 5;
      if (targetTemp > MAX_TEMP) targetTemp = MAX_TEMP;
      eepromNeedSave = true;
      eepromSaveTimer = now;
    }
  }

  // Сохранение уставки в EEPROM
  if (eepromNeedSave && (now - eepromSaveTimer > 3000)) {
    EEPROM.put(EEPROM_ADDR, targetTemp);
    eepromNeedSave = false;
  }

  // 2. ЧТЕНИЕ ТЕРМОПАРЫ (Строго раз в 300 мс)
  static float currentTemp = 30.0;
  if (now - lastTempRead >= 300) {
    lastTempRead = now;
    float readVal = thermocouple.readCelsius();
    if (!isnan(readVal) && readVal > 0) {
      currentTemp = readVal;
    }
  }

  // Защита от аварийного перегрева
  if (currentTemp > 500.0) {
    heaterEnabled = false;
    digitalWrite(HEATER_PIN, LOW);
    
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(5, 20);
    display.print("OVERHEAT");
    display.display();
    return;
  }

  // 3. ПИД-РЕГУЛЯТОР И СИМИСТОР (D6)
  if (heaterEnabled) {
    if (now - windowStartTime >= WINDOW_SIZE) {
      windowStartTime = now;

      float dt = 1.0;
      float error = targetTemp - currentTemp;
      float pTerm = Kp * error;

      if (abs(error) < 50.0) {
        integral += error * dt;
        integral = constrain(integral, -1200.0, 1200.0);
      } else {
        integral = 0;
      }

      float iTerm = Ki * integral;
      float dTerm = Kd * (error - prevError);
      prevError = error;

      pidOutput = constrain(pTerm + iTerm + dTerm, 0.0, (float)WINDOW_SIZE);
    }

    // Подача логической 1 на D6
    if (pidOutput > 20 && (now - windowStartTime) < pidOutput) {
      digitalWrite(HEATER_PIN, HIGH);
    } else {
      digitalWrite(HEATER_PIN, LOW);
    }
  } else {
    digitalWrite(HEATER_PIN, LOW);
  }

  // 4. ДИСПЛЕЙ
  display.clearDisplay();

  display.setTextSize(3);
  display.setCursor(0, 0);
  display.print((int)currentTemp);
  display.print((char)247);

  display.setTextSize(2);
  display.setCursor(80, 5);
  if (heaterEnabled) {
    display.print("ON");
  } else {
    display.print("OFF");
  }

  display.setCursor(0, 42);
  display.print("SET: ");
  display.print(targetTemp);
  display.print((char)247);

  display.display();
}
