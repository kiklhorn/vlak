// =============================================================================
//  MINIMALISTICKÝ TEST NAPÁJECÍHO PINU
// =============================================================================
//  Cíl: Ověřit, zda je možné programově nastavit pin P0.13 na HIGH.
//  Očekávané chování: Po nahrání tohoto kódu se musí rozsvítit oranžová
//  LED na desce (která je spojena s pinem P0.13) a na pinu musí být 
//  měřitelné napětí ~3.3V.
// =============================================================================

// Používáme číslování pro Adafruit nRF52 Core.
// Pin P0.13 odpovídá číslu 13.
#define PIN_OLED_POWER 13

void setup() {
  // Nastaví pin jako výstup
  pinMode(PIN_OLED_POWER, OUTPUT);
  
  // Zapne napájení
  digitalWrite(PIN_OLED_POWER, LOW);
}

void loop() {
  // Loop je úmyslně prázdný.
}
