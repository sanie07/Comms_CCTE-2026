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
  uint8_t rxData[64] = {0};
  uint8_t txData[64] = {0}; // Dummy bytes to clock the SPI
  
  // Start transaction: 1 MHz, MSB First, SPI Mode 0 (Matches STM32 CPOL=Low, CPHA=1Edge)
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0)); 
  
  // Pull CS LOW to select the STM32 slave
  digitalWrite(SPI_CS, LOW);
  
  // Send 64 bytes of dummy data to clock in the 64 bytes from STM32
  SPI.transferBytes(txData, rxData, 64);
  
  // Pull CS HIGH to deselect
  digitalWrite(SPI_CS, HIGH);
  
  // End transaction
  SPI.endTransaction();

  // Print received data
  // The rxData array will contain the null-terminated string sent by STM32
  Serial.print("Received from STM32: ");
  Serial.println((char*)rxData);

  // Wait 1 second before polling again
  delay(1000); 
}
