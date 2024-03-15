/*
  Rui Santos
  Complete project details at https://RandomNerdTutorials.com/esp-now-esp32-arduino-ide/

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files.

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
*/

#include <Arduino.h>

void setup()
{
  // put your setup code here, to run once:
  pinMode(26, OUTPUT);
}
void loop()
{
  // put your main code here, to run repeatedly:
  digitalWrite(26, HIGH);
  digitalWrite(26, LOW);
}