#include <Arduino.h>

#define outputA 2
#define outputB 3

int counter = 0;
int aState;
int aLastState;

void setup() {
  pinMode(outputA, INPUT_PULLUP);
  pinMode(outputB, INPUT_PULLUP);
  Serial.begin(9600);
  aLastState = digitalRead(outputA);
}

void loop() {
  aState = digitalRead(outputA);

  if (aLastState == LOW && aState == HIGH) {
    if (digitalRead(outputB) == LOW)
      counter++;
    else
      counter--;
      
    Serial.print("Position: ");
    Serial.println(counter);
  }

  aLastState = aState;
}
