#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90632.h>
#include "DFRobot_RGBLCD1602.h"

/* ========== ROTARY ENCODER ========== */
#define outputA 2
#define outputB 3

int counter = 0;
int lastEncoded = 0;
unsigned long lastStepTime = 0;
const unsigned long debounceTime = 5; // ms, pas aan voor gevoeligheid

/* ========== TEMPERATUURSENSOR ========== */
Adafruit_MLX90632 mlx = Adafruit_MLX90632();

/* ========== LCD ========== */
DFRobot_RGBLCD1602 lcd(0x2D, 16, 2); // pas I2C-adres aan

/* ========== INSTELLINGEN ========== */
const double TEMP_THRESHOLD = 37.0; // °C

void setup()
{
  Serial.begin(115200);

  // Rotary encoder pin modes
  pinMode(outputA, INPUT_PULLUP);
  pinMode(outputB, INPUT_PULLUP);

  // I2C
  Wire.begin();
  Wire.setClock(400000);

  // LCD init
  lcd.init();
  lcd.setCursor(0, 0);
  lcd.print("System start");
  lcd.setRGB(0, 255, 0);

  // MLX90632 init
  if (!mlx.begin())
  {
    lcd.setCursor(0, 1);
    lcd.print("Sensor fout!");
    while (1)
      ;
  }

  mlx.setMode(MLX90632_MODE_CONTINUOUS);
  mlx.setMeasurementSelect(MLX90632_MEAS_MEDICAL);
  mlx.setRefreshRate(MLX90632_REFRESH_2HZ);
  mlx.resetNewData();

  delay(1000);
  lcd.clear();
}

void loop()
{
  /* ===== ROTARY ENCODER ===== */
  int MSB = digitalRead(outputA);
  int LSB = digitalRead(outputB);

  int encoded = (MSB << 1) | LSB;
  int sum = (lastEncoded << 2) | encoded;

  unsigned long now = millis();
  if (now - lastStepTime > debounceTime)
  {
    // Detecteer stappen en richting
    if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011)
    {
      counter++;
      lastStepTime = now;
    }
    if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000)
    {
      counter--;
      lastStepTime = now;
    }
  }

  lastEncoded = encoded;

  /* ===== TEMPERATUUR LEZEN ===== */
  double objectTemp = NAN;
  if (mlx.isNewData())
  {
    objectTemp = mlx.getObjectTemperature();
    mlx.resetNewData();
  }

  /* ===== LCD UPDATE ===== */
  lcd.setCursor(0, 0);
  lcd.print("Pos: ");
  lcd.print(counter);
  lcd.print("     "); // wis oude cijfers

  lcd.setCursor(0, 1);
  if (!isnan(objectTemp))
  {
    lcd.print("Temp: ");
    lcd.print(objectTemp, 1);
    lcd.print((char)223); // ° symbool
    lcd.print("C ");

    // Backlight kleur afhankelijk van temperatuur
    if (objectTemp >= TEMP_THRESHOLD)
    {
      lcd.setRGB(255, 0, 0); // rood
    }
    else
    {
      lcd.setRGB(0, 255, 0); // groen
    }
  }
  else
  {
    lcd.print("Temp: ---.-C");
  }

  /* ===== SERIAL DEBUG ===== */
  Serial.print("Pos: ");
  Serial.print(counter);
  Serial.print(" | Temp: ");
  Serial.println(objectTemp);

  delay(50);
}