#include <ArduinoBLE.h>

// ====================================================================
//  ABSTRAKCE HARDWARU (VÝBĚR DESKY)
// ====================================================================

#if defined(ARDUINO_ARDUINO_NANO33BLE) || defined(ARDUINO_NANO33BLE_SENSE)
  #include <EEPROM.h>
  #define EEPROM_ADDR_MICROSTEPS 0
  
  #define DIR_PIN   11
  #define STEP_PIN  10
  #define SLEEP_PIN 6
  #define M0_PIN    2
  #define M1_PIN    3

#elif defined(ADAFRUIT_FEATHER_NRF52840_EXPRESS) || defined(ADAFRUIT_FEATHER_NRF52840_SENSE) || \
      defined(ADAFRUIT_CLUE_NRF52840_EXPRESS)  || defined(ADAFRUIT_NRF52840_ITSYBITSY)   || \
      defined(SPARKFUN_PRO_NRF52840_MINI)      || defined(SEEED_XIAO_NRF52840)

  #include <Adafruit_LittleFS.h>
  #include <InternalFileSystem.h>
  #define SETTINGS_FILENAME "/settings.txt"

  // POZOR: Piny jsou zde vymyšlené pro ukázku! Upravte je podle vaší desky a zapojení.
  #define DIR_PIN   12
  #define STEP_PIN  13
  #define SLEEP_PIN 9
  #define M0_PIN    10
  #define M1_PIN    11

#else
  #error "Tato deska není podporována. Zkontrolujte, zda máte v Arduino IDE vybranou správnou desku."
#endif

#define LED_PIN   LED_BUILTIN

// --- Mechanické parametry vlaku ---
#define MOTOR_STEP_ANGLE    18   
#define GEAR_RATIO          21.0  
#define WHEEL_DIAMETER_MM   25.0  


// --- UUIDs --- 
#define SERVICE_UUID        "d1f61c9f-6eef-4911-8b49-98e13dd94938"
#define COMMAND_CHAR_UUID    "abd41953-43f6-426c-b9d3-ff151d71b489"
#define STATE_CHAR_UUID        "c3413c66-4001-42e3-a553-064950de6833"

BLEService trainService(SERVICE_UUID);
BLECharacteristic commandChar(COMMAND_CHAR_UUID, BLEWrite, 32);
BLECharacteristic stateChar(STATE_CHAR_UUID, BLERead | BLENotify, 64);

// --- Stavy systému a motoru ---
enum SystemState { NORMAL, EMERGENCY_STOP };
SystemState systemState = NORMAL;
enum MotorState { STOPPED, FORWARD, BACKWARD };
MotorState currentMotorState = STOPPED;

// --- Proměnné pro řízení pohybu a stavů ---
volatile unsigned long lastStepTime = 0;
int currentSpeedDelay = 2000;  
int currentMicrosteps = 4;
int currentSpeed = 50;         

// Základní prodlevy pro plný krok (1 microstep) - Kalibrováno na 70 RPM
// POZNÁMKA: Tyto hodnoty budou nyní přepočítány v calculateSpeedDelay()
#define BASE_DELAY_MAX 2000 
#define BASE_DELAY_MIN 50

// Proměnné pro E-STOP
unsigned long emergencyStopTime = 0;
MotorState directionBeforeEmergency = STOPPED;
unsigned long lastBlinkTime = 0;

// --- Deklarace funkcí ---
void onCommandWritten(BLEDevice central, BLECharacteristic characteristic);
void onBleConnected(BLEDevice central, BLECharacteristic characteristic);
void setMicrostepMode(uint8_t microstep, bool save);
void updateAndNotifyState();
void loadSettings();
void saveSettings();
void calculateSpeedDelay();
void logMovementInfo();

void setup() {
  Serial.begin(115200);
  Serial.println("BLE Vlak Server - Start (v11, oprava pulzů)");

  loadSettings();

  pinMode(DIR_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(SLEEP_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(STEP_PIN, LOW);
  digitalWrite(DIR_PIN, LOW);
  digitalWrite(SLEEP_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  setMicrostepMode(currentMicrosteps, false);
  calculateSpeedDelay();

  if (!BLE.begin()) {
    Serial.println("Chyba: BLE nelze inicializovat!");
    while (1);
  }
  BLE.setLocalName("TrainBLE");
  BLE.setAdvertisedService(trainService);
  trainService.addCharacteristic(commandChar);
  trainService.addCharacteristic(stateChar);
  BLE.addService(trainService);
  
  commandChar.setEventHandler(BLEWritten, onCommandWritten);
  stateChar.setEventHandler(BLESubscribed, onBleConnected);

  BLE.advertise();
  Serial.println("Vlak čeká na připojení z aplikace...");
}

void loop() {
  BLE.poll();

  if (systemState == EMERGENCY_STOP) {
    unsigned long timeSinceEmergency = millis() - emergencyStopTime;

    if (directionBeforeEmergency != STOPPED && timeSinceEmergency < 1000) {
      // Fáze protipohybu - používáme stejnou prodlevu jako pro normální pohyb
      if (micros() - lastStepTime >= currentSpeedDelay) { 
        lastStepTime = micros();
        // Generujeme jeden krok (změna stavu pinů)
        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(5);
        digitalWrite(STEP_PIN, LOW);
      }
    } else {
      // Fáze držení motoru a blikání
      if (timeSinceEmergency >= 5000 && digitalRead(SLEEP_PIN) == HIGH) {
          Serial.println("E-Stop: 5s timeout. Uvolňuji motor.");
          digitalWrite(SLEEP_PIN, LOW);
      }
      if (millis() - lastBlinkTime > 250) { 
        lastBlinkTime = millis();
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      }
    }

  } else { // systemState == NORMAL
    if (currentMotorState != STOPPED) {
      // --- SPRÁVNÁ LOGIKA GENEROVÁNÍ PULZU ---
      if (micros() - lastStepTime >= currentSpeedDelay) {
        lastStepTime = micros(); // Aktualizujeme čas posledního kroku
        // Generujeme jeden krok: PŘEKLOOPENÍ STAVU PINU STEP
        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(3);
        digitalWrite(STEP_PIN, LOW);
      }
    }
  }
}

void onBleConnected(BLEDevice central, BLECharacteristic characteristic) {
  Serial.println("Klient se přihlásil k odběru. Odesílám aktuální stav.");
  updateAndNotifyState();
}

void onCommandWritten(BLEDevice central, BLECharacteristic characteristic) {
  String cmd = "";
  for (int i = 0; i < characteristic.valueLength(); i++) {
    cmd += (char)characteristic.value()[i];
  }
  Serial.print("Příkaz: '"); Serial.print(cmd); Serial.println("'");

  if (cmd == "E_STOP") {
    if (systemState != EMERGENCY_STOP) {
      Serial.println("NOUZOVÁ BRZDA AKTIVOVÁNA!");
      directionBeforeEmergency = currentMotorState;
      bool stateChanged = (currentMotorState != STOPPED);
      currentMotorState = STOPPED;
      systemState = EMERGENCY_STOP;
      emergencyStopTime = millis();
      digitalWrite(SLEEP_PIN, HIGH);
      delay(2);
      if (directionBeforeEmergency == FORWARD) { digitalWrite(DIR_PIN, LOW); }
      else if (directionBeforeEmergency == BACKWARD) { digitalWrite(DIR_PIN, HIGH); }
      if (stateChanged) { updateAndNotifyState(); }
    }
    return;
  }

  if (systemState == EMERGENCY_STOP) {
    Serial.println("Nouzový stav zrušen.");
    systemState = NORMAL;
    digitalWrite(LED_PIN, LOW);
  }

  bool stateChanged = false;
  bool movementCommand = false;

  if (cmd == "FORWARD") {
    if (currentMotorState != FORWARD) {
      digitalWrite(SLEEP_PIN, HIGH); delay(2);
      digitalWrite(DIR_PIN, HIGH);
      currentMotorState = FORWARD;
      stateChanged = true;
      movementCommand = true;
    }
  } else if (cmd == "BACKWARD") {
    if (currentMotorState != BACKWARD) {
      digitalWrite(SLEEP_PIN, HIGH); delay(2);
      digitalWrite(DIR_PIN, LOW);
      currentMotorState = BACKWARD;
      stateChanged = true;
      movementCommand = true;
    }
  } else if (cmd == "STOP") {
    stateChanged = (currentMotorState != STOPPED);
    currentMotorState = STOPPED;
    digitalWrite(SLEEP_PIN, LOW);
    movementCommand = true;
  } else if (cmd.startsWith("SPEED:")) {
    currentSpeed = cmd.substring(6).toInt();
    calculateSpeedDelay();
    movementCommand = true;
  } else if (cmd.startsWith("MICROSTEPS:")) {
    int microstepsValue = cmd.substring(11).toInt();
    if (microstepsValue != currentMicrosteps) {
        setMicrostepMode(microstepsValue, true);
        stateChanged = true;
    }
  } else if (cmd == "HORN_ON") {
    digitalWrite(LED_PIN, HIGH);
  } else if (cmd == "HORN_OFF") {
    digitalWrite(LED_PIN, LOW);
  }

  if (stateChanged) {
    updateAndNotifyState();
  }
  if (movementCommand) {
    logMovementInfo();
  }
}

void calculateSpeedDelay() {
  int baseDelay = map(currentSpeed, 0, 100, BASE_DELAY_MAX, BASE_DELAY_MIN);
  currentSpeedDelay = baseDelay / currentMicrosteps;
  
  if (currentSpeedDelay < 3) { 
    currentSpeedDelay = 3;
  }
  Serial.print("Nová finální prodleva: "); Serial.println(currentSpeedDelay);
}

void setMicrostepMode(uint8_t microstep, bool save) {
  currentMicrosteps = microstep;
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

  calculateSpeedDelay(); 
  logMovementInfo();

  if (save) {
    saveSettings();
  }
}

void updateAndNotifyState() {
  String stateString = "";
  stateString += "STATE:";
  if (currentMotorState == FORWARD) stateString += "FORWARD";
  else if (currentMotorState == BACKWARD) stateString += "BACKWARD";
  else stateString += "STOPPED";
  
  stateString += ";";
  stateString += "SPEED:";
  stateString += currentSpeed;
  stateString += ";";
  stateString += "MICROSTEPS:";
  stateString += currentMicrosteps;

  Serial.print("Odesílám stav: ");
  Serial.println(stateString);
  
  stateChar.writeValue(stateString.c_str());
}

void logMovementInfo() {
  if (currentMotorState == STOPPED) {
    Serial.println("-> Vlak stojí.");
    return;
  }

  float steps_per_motor_rev = 360.0 / MOTOR_STEP_ANGLE;
  float microsteps_per_wheel_rev = steps_per_motor_rev * GEAR_RATIO * currentMicrosteps;
  float steps_per_second = 1000000.0 / currentSpeedDelay;
  float wheel_rpm = (steps_per_second / microsteps_per_wheel_rev) * 60.0 / GEAR_RATIO;
  float wheel_circumference_mm = WHEEL_DIAMETER_MM * PI;
  float speed_cm_per_second = wheel_rpm * wheel_circumference_mm / 60.0 / 10.0;

  Serial.println(F("--- Info o pohybu ---"));
  Serial.print(F("  Otáčky kola (RPM): ")); Serial.println(wheel_rpm);
  Serial.print(F("  Rychlost (cm/s): ")); Serial.println(speed_cm_per_second);
  Serial.println(F("---------------------"));
}

// --- Funkce pro ukládání / načítání ---
#if defined(ARDUINO_ARDUINO_NANO33BLE) || defined(ARDUINO_NANO33BLE_SENSE)
void loadSettings() { 
  int storedValue;
  EEPROM.get(EEPROM_ADDR_MICROSTEPS, storedValue);
  if (storedValue == 1 || storedValue == 2 || storedValue == 4 || storedValue == 8 || storedValue == 16 || storedValue == 32) {
    currentMicrosteps = storedValue;
    Serial.println("Načteno z EEPROM: " + String(currentMicrosteps));
  } else {
    Serial.println("Nenalezeno platné nastavení v EEPROM, výchozí: 4");
    currentMicrosteps = 4;
  }
}
void saveSettings() {
  Serial.println("Ukládám do EEPROM: " + String(currentMicrosteps));
  EEPROM.put(EEPROM_ADDR_MICROSTEPS, currentMicrosteps);
}

#elif defined(ADAFRUIT_FEATHER_NRF52840_EXPRESS) || defined(ADAFRUIT_FEATHER_NRF52840_SENSE) || \
      defined(ADAFRUIT_CLUE_NRF52840_EXPRESS)  || defined(ADAFRUIT_NRF52840_ITSYBITSY)   || \
      defined(SPARKFUN_PRO_NRF52840_MINI)      || defined(SEEED_XIAO_NRF52840)
void loadSettings() {
  InternalFS.begin();
  if (InternalFS.exists(SETTINGS_FILENAME)) {
    File settingsFile = InternalFS.open(SETTINGS_FILENAME, FILE_READ);
    if (settingsFile && settingsFile.available()) {
      currentMicrosteps = settingsFile.parseInt();
      Serial.println("Načteno z LittleFS: " + String(currentMicrosteps));
    }
    settingsFile.close();
  } else {
     Serial.println("Soubor s nastavením neexistuje, výchozí: 4");
    currentMicrosteps = 4;
  }
}
void saveSettings() {
  File settingsFile = InternalFS.open(SETTINGS_FILENAME, FILE_WRITE);
  if (!settingsFile) {
    Serial.println("Chyba: Nepodařilo se otevřít soubor pro uložení!");
    return;
  }
  settingsFile.print(currentMicrosteps);
  settingsFile.close();
  Serial.println("Uloženo do LittleFS: " + String(currentMicrosteps));
}

#endif
