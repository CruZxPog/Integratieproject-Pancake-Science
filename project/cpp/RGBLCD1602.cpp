#include <Arduino.h>
#include <Wire.h>
#include "DFRobot_RGBLCD1602.h"

// Maak een 16x2 LCD object
DFRobot_RGBLCD1602 lcd(16, 2);

void setup()
{
  Serial.begin(115200);

  // Initialiseer het LCD
  lcd.init();          // <-- correct, niet lcd.begin()
  lcd.setCursor(0, 0); // Begin bij linkerbovenhoek
  lcd.print("Hallo, Wereld!");

  // Achtergrondkleur instellen
  lcd.setRGB(0, 255, 0); // Groen
}

void loop()
{
  lcd.setRGB(255, 0, 0); // Rood
  delay(1000);

  lcd.setRGB(0, 0, 255); // Blauw
  delay(1000);

  lcd.setRGB(0, 255, 0); // Groen
  delay(1000);
}
