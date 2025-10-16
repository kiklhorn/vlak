
#include <ArduinoBLE.h>

// --- UUIDs musí přesně odpovídat Android aplikaci ---
#define SERVICE_UUID        "d1f61c9f-6eef-4911-8b49-98e13dd94938"
#define CHARACTERISTIC_UUID "abd41953-43f6-426c-b9d3-ff151d71b489"
BLEService trainService(SERVICE_UUID);
BLECharacteristic commandChar(CHARACTERISTIC_UUID, BLERead | BLEWrite, 32);

// --- Definice pinů ---
#define DIR_PIN   11
#define STEP_PIN  10
#define SLEEP_PIN 6
#define LED_PIN   LED_BUILTIN
#define M0_PIN    2
#define M1_PIN    3

// --- Nový stavový automat pro motor ---
enum MotorState {
  STOPPED,
  FORWARD,
  BACKWARD
};
MotorState currentMotorState = STOPPED; // Vlak na startu stojí

// --- Proměnné pro řízení pohybu ---
volatile unsigned long lastStepTime = 0; // Čas posledního kroku v mikrosekundách
int currentSpeedDelay = 2000;            // Delay mezi kroky v mikrosekundách (vyšší = pomalejší)

// --- Deklarace callback funkce ---
void onCommandWritten(BLEDevice central, BLECharacteristic characteristic);
void setMicrostepMode(uint8_t microstep);

void setup() {
  Serial.begin(115200);
  Serial.println("BLE Vlak Server - Start (Neblokující verze)");

  // --- Inicializace pinů ---
  pinMode(DIR_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(SLEEP_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(STEP_PIN, LOW);
  digitalWrite(DIR_PIN, LOW);
  digitalWrite(SLEEP_PIN, LOW); // Uspat driver na startu
  digitalWrite(LED_PIN, LOW);
  setMicrostepMode(4);  // Nastavení výchozího mikrokrokování

  // --- Start BLE ---
  if (!BLE.begin()) {
    Serial.println("Chyba: BLE nelze inicializovat!");
    while (1);
  }
  BLE.setLocalName("TrainBLE");
  BLE.setAdvertisedService(trainService);
  trainService.addCharacteristic(commandChar);
  BLE.addService(trainService);
  commandChar.setEventHandler(BLEWritten, onCommandWritten); // Nastavení callbacku
  BLE.advertise();
  Serial.println("Vlak čeká na připojení z aplikace...");
}

// ====================================================================
//  HLAVNÍ SMYČKA
// ====================================================================
void loop() {
  // **** DŮLEŽITÁ OPRAVA: Volání BLE.poll() je nezbytné pro zpracování BLE událostí! ****
  BLE.poll();

  // Tento kód běží neustále a generuje kroky motoru, pokud není zastaven.
  if (currentMotorState != STOPPED) {
    if (micros() - lastStepTime >= currentSpeedDelay) {
      lastStepTime = micros(); // Uložíme čas tohoto kroku
      // Vygenerujeme jeden krátký puls pro jeden krok motoru
      digitalWrite(STEP_PIN, HIGH);
      delayMicroseconds(5); // Puls může být velmi krátký
      digitalWrite(STEP_PIN, LOW);
    }
  }
}

// ====================================================================
//  CALLBACK (ZPRACOVÁNÍ PŘÍKAZŮ)
// ====================================================================
void onCommandWritten(BLEDevice central, BLECharacteristic characteristic) {
  String cmd = "";
  for (int i = 0; i < characteristic.valueLength(); i++) {
    cmd += (char)characteristic.value()[i];
  }
  Serial.print("Příkaz: '");
  Serial.print(cmd);
  Serial.println("'");

  // --- Zpracování příkazů ---
  if (cmd == "FORWARD") {
    if (currentMotorState != FORWARD) {
      digitalWrite(SLEEP_PIN, HIGH);
      delay(2);
      digitalWrite(DIR_PIN, HIGH);
      currentMotorState = FORWARD;
      digitalWrite(LED_PIN, HIGH);
      Serial.println("Akce: Start motoru VPŘED");
    }
  }
  else if (cmd == "BACKWARD") {
    if (currentMotorState != BACKWARD) {
      digitalWrite(SLEEP_PIN, HIGH);
      delay(2);
      digitalWrite(DIR_PIN, LOW);
      currentMotorState = BACKWARD;
      digitalWrite(LED_PIN, HIGH);
      Serial.println("Akce: Start motoru VZAD");
    }
  }
  else if (cmd == "STOP") {
    currentMotorState = STOPPED;
    digitalWrite(SLEEP_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
    Serial.println("Akce: Motor STOP");
  }
  else if (cmd.startsWith("SPEED:")) {
    int speed = cmd.substring(6).toInt();
    currentSpeedDelay = map(speed, 0, 100, 4000, 500);
    Serial.print("Nová rychlost (delay): ");
    Serial.println(currentSpeedDelay);
  }
  else if (cmd.startsWith("MICROSTEPS:")) {
    int microstepsValue = cmd.substring(11).toInt();
    setMicrostepMode(microstepsValue);
    Serial.print("Akce: Nastaveno microsteps na ");
    Serial.println(microstepsValue);
  }
}

// ====================================================================
//  POMOCNÉ FUNKCE
// ====================================================================
void setMicrostepMode(uint8_t microstep) {
  pinMode(M0_PIN, OUTPUT);
  pinMode(M1_PIN, OUTPUT);
  switch (microstep) {
    case 1:   digitalWrite(M0_PIN, LOW);  digitalWrite(M1_PIN, LOW);  break;
    case 2:   digitalWrite(M0_PIN, HIGH); digitalWrite(M1_PIN, LOW);  break;
    case 4:   pinMode(M0_PIN, INPUT);     digitalWrite(M1_PIN, LOW);  break;
    case 8:   digitalWrite(M0_PIN, LOW);  digitalWrite(M1_PIN, HIGH); break;
    case 16:  digitalWrite(M0_PIN, HIGH); digitalWrite(M1_PIN, HIGH); break;
    case 32:  pinMode(M0_PIN, INPUT);     digitalWrite(M1_PIN, HIGH); break;
    default:  pinMode(M0_PIN, INPUT);     digitalWrite(M1_PIN, LOW);  break;
  }
}
