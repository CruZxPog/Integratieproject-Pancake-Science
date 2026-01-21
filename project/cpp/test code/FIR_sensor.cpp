#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90632.h>

Adafruit_MLX90632 mlx = Adafruit_MLX90632();

void setup()
{
  Serial.begin(115200);
  while (!Serial)
  {
    delay(10);
  }

  Serial.println("MLX90632 IR Temperatuur Test");

  // I2C starten (Uno: SDA=A4, SCL=A5)
  Wire.begin();
  Wire.setClock(400000);

  if (!mlx.begin())
  {
    Serial.println("❌ Sensor niet gevonden!");
    while (1)
      delay(10);
  }
  Serial.println("✅ Sensor gevonden!");

  // Mode instellen: continuous (continue meten)
  if (!mlx.setMode(MLX90632_MODE_CONTINUOUS))
  {
    Serial.println("❌ Fout bij het instellen van modus");
  }

  // Meetinstellingen (medical = hoge nauwkeurigheid)
  mlx.setMeasurementSelect(MLX90632_MEAS_MEDICAL);

  // Refresh rate
  mlx.setRefreshRate(MLX90632_REFRESH_2HZ);

  // Reset “new data” flag
  mlx.resetNewData();
}

void loop()
{
  // Check of de “new data” flag is gezet
  if (mlx.isNewData())
  {
    // Lees cyclische positie (optioneel)
    Serial.print("Cycle position: ");
    Serial.println(mlx.readCyclePosition());

    // Lees temperaturen
    double ambientTemp = mlx.getAmbientTemperature();
    double objectTemp = mlx.getObjectTemperature();

    Serial.print("Ambient Temp: ");
    Serial.print(ambientTemp, 3);
    Serial.print(" °C | Object Temp: ");

    if (isnan(objectTemp))
    {
      Serial.println("NaN (invalid cycle)");
    }
    else
    {
      Serial.print(objectTemp, 3);
      Serial.println(" °C");
    }

    // Reset de new data flag
    mlx.resetNewData();

    Serial.println();
  }

  delay(10);
}
