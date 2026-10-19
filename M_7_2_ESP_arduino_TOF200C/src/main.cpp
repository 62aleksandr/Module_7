#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_VL53L0X.h>

#define SDA_PIN 16
#define SCL_PIN 15

Adafruit_VL53L0X lox;
VL53L0X_RangingMeasurementData_t measure;

void setup()
{
  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN);

  Serial.println("TOF200C Distance Sensor");

  if (!lox.begin())
  {
    Serial.println("Sensor initialization failed!");
    while (1)
    {
      delay(10);
    }
  }

  Serial.println("TOF200C ready");
}

void loop()
{
  // Старт вимірювання
  unsigned long startTime = micros();

  lox.rangingTest(&measure, false);

  // Стоп имірювання
  unsigned long duration = micros() - startTime;

  if (measure.RangeStatus != 4)
  {
    Serial.print("Distance: ");
    Serial.print(measure.RangeMilliMeter);
    Serial.println(" mm");
  }
  else
  {
    Serial.println("Out of range");
  }

  // Serial.print(" | Measurement time: ");
  // Serial.print(duration);
  // Serial.print(" us (~");
  // Serial.print(duration / 1000.0, 2);
  // Serial.println(" ms)");

  delay(200);
}