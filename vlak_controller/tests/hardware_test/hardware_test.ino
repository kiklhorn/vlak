/**
 * Firmware pro nízkoúrovňovou diagnostiku stavu pinu P1_9 po startu.
 * Platforma: Arduino MBED Core (Arduino Nano 33 BLE)
 */

#include <Arduino.h>

// Definice pinu pro přehlednost
#define LED_PIN_NUM 9

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000); 
  
  Serial.println("\n\n--- Nizkourovnova diagnostika pinu P1.09 ---");
  Serial.println("Ctu stav registru IHNED po startu, pred pinMode().");
  Serial.println("-------------------------------------------------");

  // Přečteme celý 32-bitový konfigurační registr pro pin P1.09
  uint32_t pin_cnf = NRF_P1->PIN_CNF[LED_PIN_NUM];

  // Přečteme celý 32-bitový registr výstupních hodnot pro Port 1
  uint32_t out_reg = NRF_P1->OUT;

  // Extrahujeme jednotlivé bity pomocí bitových operací
  uint8_t dir   = (pin_cnf >> 0) & 1;   // Bit 0: Směr (0=Input, 1=Output)
  uint8_t input = (pin_cnf >> 1) & 1;   // Bit 1: Připojení vstupního bufferu
  uint8_t pull  = (pin_cnf >> 2) & 3;   // Bity 2-3: Pull rezistor (0=Nic, 1=Down, 3=Up)
  uint8_t drive = (pin_cnf >> 8) & 7;   // Bity 8-10: Drive strength

  // Extrahujeme stav výstupního bitu pro náš pin
  uint8_t out_val = (out_reg >> LED_PIN_NUM) & 1; // Je pin nastaven na HIGH nebo LOW?

  Serial.print("PIN_CNF[9] (raw): 0x"); Serial.println(pin_cnf, HEX);
  Serial.println();
  Serial.print("DIR (Smer):         "); Serial.print(dir);
  Serial.println(dir ? " (Output)" : " (Input)");
  
  Serial.print("INPUT (Buffer):     "); Serial.print(input);
  Serial.println(input ? " (Disconnected)" : " (Connected)");

  Serial.print("PULL (Rezistor):    "); Serial.print(pull);
  if (pull == 0) Serial.println(" (Disabled)");
  else if (pull == 1) Serial.println(" (Pull-down)");
  else if (pull == 3) Serial.println(" (Pull-up)");
  else Serial.println(" (Unknown)");

  Serial.print("DRIVE (Sila):       "); Serial.print(drive, BIN);
  Serial.println(" (Standard 'S0S1')");
  
  Serial.println();
  Serial.print("OUT Register bit 9: "); Serial.print(out_val);
  Serial.println(out_val ? " (HIGH)" : " (LOW)");
  Serial.println("-------------------------------------------------");

  if (dir == 1 && out_val == 1) {
    Serial.println("ZAVER: Pin je po startu nakonfigurovan jako OUTPUT HIGH.");
    Serial.println("To vysvetluje, proc LED sviti.");
  } else {
    Serial.println("ZAVER: Chovani je jine, nez se ocekavalo.");
  }

  // Zastavíme program, aby nedošlo k dalším změnám
  while(1);
}

void loop() {
  // Tento kód se nikdy nespustí
}
