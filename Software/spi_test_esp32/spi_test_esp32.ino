#include <SPI.h>

// Define ESP32 VSPI pins (standard)
#define SPI_SCK  6
#define SPI_MISO 4
#define SPI_MOSI 5
#define SPI_CS   7

void setup() {
  Serial.begin(115200);
  
  // Initialize the global SPI bus. Pass -1 for CS to manage it manually via GPIO
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, -1);
  
  // Setup CS pin as output and set it HIGH (inactive)
  pinMode(SPI_CS, OUTPUT);
  digitalWrite(SPI_CS, HIGH);

  Serial.println("ESP32 SPI Master Started.");
  Serial.println("Waiting for STM32 slave...");
}

void loop() {
  uint8_t txBuffer[12] = {0}; // 12 bytes dummy data to clock out the STM32
  uint8_t rxBuffer[12] = {0}; 
  
  // Start transaction
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0)); 
  
  // Pull CS LOW
  digitalWrite(SPI_CS, LOW);
  
  // Send 12 bytes of dummy data
  for (int i = 0; i < 12; i++) {
    rxBuffer[i] = SPI.transfer(txBuffer[i]);
  }
  
  // Pull CS HIGH
  digitalWrite(SPI_CS, HIGH);
  SPI.endTransaction();

  // Print received data
  Serial.print("Received from STM32: ");
  for (int i = 0; i < 12; i++) {
    Serial.print((char)rxBuffer[i]);
  }
  Serial.println();

  delay(1000); // Wait 1 second before polling again
}
