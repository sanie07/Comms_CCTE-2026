const int stm32InputPin = 4; // Connect the STM32 blink output pin to GPIO 4 on the ESP32-C6

int lastState = -1;

void setup() {
  Serial.begin(115200);
  // Setting the pin as INPUT. Use INPUT_PULLDOWN if the STM32 pin might be floating.
  pinMode(stm32InputPin, INPUT_PULLDOWN); 
  
  Serial.println("ESP32-C6 Initialized.");
  Serial.print("Monitoring STM32 blink output on GPIO ");
  Serial.println(stm32InputPin);
}

void loop() {
  int currentState = digitalRead(stm32InputPin);
  
  // Only print when the state changes to avoid spamming the serial monitor
  if (currentState != lastState) {
    Serial.print("STM32 Pin State: ");
    Serial.println(currentState == HIGH ? "HIGH" : "LOW");
    lastState = currentState;
  }
  
  delay(10); // Small delay to debounce and avoid 100% CPU usage
}
