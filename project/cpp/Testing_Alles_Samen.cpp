#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90632.h>
#include "DFRobot_RGBLCD1602.h"

/* ================= ROTARY ENCODER ================= */
#define outputA 2
#define outputB 3

int counter = 0;
int aState;
int aLastState;

/* ================= TEMPERATUURSENSOR ================= */
Adafruit_MLX90632 mlx = Adafruit_MLX90632();

/* ================= LCD ================= */
DFRobot_RGBLCD1602 lcd(16, 2);

/* ================= SETUP ================= */
void setup()
{
  Serial.begin(115200);

  /* Rotary encoder */
  pinMode(outputA, INPUT_PULLUP);
  pinMode(outputB, INPUT_PULLUP);
  aLastState = digitalRead(outputA);

  /* I2C */
  Wire.begin();
  Wire.setClock(400000);

  /* LCD */
  lcd.init();
  lcd.setRGB(0, 255, 0);
  lcd.setCursor(0, 0);
  lcd.print("Systeem start");

  /* MLX90632 */
  if (!mlx.begin())
  {
    lcd.setCursor(0, 1);
    lcd.print("Sensor fout!");
    while (1);
  }

  mlx.setMode(MLX90632_MODE_CONTINUOUS);
  mlx.setMeasurementSelect(MLX90632_MEAS_MEDICAL);
  mlx.setRefreshRate(MLX90632_REFRESH_2HZ);
  mlx.resetNewData();

  delay(1500);
  lcd.clear();
}

/* ================= LOOP ================= */
void loop()
{
  /* ---------- ROTARY ENCODER ---------- */
  aState = digitalRead(outputA);
  if (aLastState == LOW && aState == HIGH)
  {
    if (digitalRead(outputB) == LOW)
      counter++;
    else
      counter--;
  }
  aLastState = aState;

  /* ---------- TEMPERATUUR ---------- */
  double ambientTemp = NAN;
  double objectTemp = NAN;

  if (mlx.isNewData())
  {
    ambientTemp = mlx.getAmbientTemperature();
    objectTemp = mlx.getObjectTemperature();
    mlx.resetNewData();
  }

  /* ---------- LCD UPDATE ---------- */
  lcd.setCursor(0, 0);
  lcd.print("Pos: ");
  lcd.print(counter);
  lcd.print("     ");

  lcd.setCursor(0, 1);
  if (!isnan(objectTemp))
  {
    lcd.print("Temp: ");
    lcd.print(objectTemp, 1);
    lcd.print((char)223);
    lcd.print("C ");
  }
  else
  {
    lcd.print("Temp: ---.-C");
  }

  /* ---------- SERIAL DEBUG ---------- */
  Serial.print("Positie: ");
  Serial.print(counter);
  Serial.print(" | Object Temp: ");
  Serial.println(objectTemp);

  delay(100);
}
