#include <SPI.h>

#define SPI_MISO 4
#define SPI_MOSI 5
#define SPI_SCK  6
#define SPI_CS   7

void setup() {
  Serial.begin(115200);
  
  // Initialize SPI with CS pin handled manually (-1)
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, -1);
  pinMode(SPI_CS, OUTPUT);
  digitalWrite(SPI_CS, HIGH);

  Serial.println("Waiting for STM32 valid handshake...");
  
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0)); 
  
  uint8_t status = 0x00;
  
  // Poll until we get '1' (OK) or '2' (ERR). 
  // Ignore 0x00 and 0xFF as they are common SPI bus floating/boot states.
  while(status != 1 && status != 2) {
     digitalWrite(SPI_CS, LOW);
     delayMicroseconds(10); // Give STM32's EXTI/hardware a brief moment to react
     
     status = SPI.transfer(0x00); // Send dummy byte to generate clock
     
     digitalWrite(SPI_CS, HIGH); // Raise CS to complete frame (Triggers STM32 EXTI)
     
     if (status != 1 && status != 2) {
       delay(50); // Don't spam the bus too fast while waiting
     }
  }
  
  SPI.endTransaction();
  
  Serial.print("Handshake result received: ");
  Serial.println(status == 1 ? "SUCCESS" : "FAILED");
}

void loop() {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0)); 
  
  digitalWrite(SPI_CS, LOW);
  delayMicroseconds(50); // Increased slightly to give STM32 plenty of time
  
  uint8_t rxData = SPI.transfer(0x00);
  
  digitalWrite(SPI_CS, HIGH);
  SPI.endTransaction();
  
  Serial.print("Received from STM32: ");
  if (rxData == 1) {
      Serial.println("HANDSHAKE OK (1)");
  } 
  else if (rxData == 2) {
      Serial.println("HANDSHAKE FAILED (2)");
  } 
  else {
      Serial.print("INVALID DATA (Raw HEX: 0x");
      if (rxData < 0x10) Serial.print("0");
      Serial.print(rxData, HEX);
      Serial.println(")");
  }
  
  delay(1000); 
}