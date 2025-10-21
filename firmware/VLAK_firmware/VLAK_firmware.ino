#include <ArduinoBLE.h>
#include <NRF52_MBED_TimerInterrupt.h>

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

// --- Limity hardwaru ---
#define MOTOR_MIN_FULL_STEPS_PER_SEC 20
#define MOTOR_MAX_FULL_STEPS_PER_SEC 2400
#define MAX_TIMER_FREQ 100000.0f
// Reálný čas (v us), který procesor stráví v ISR. Odvozeno z měření.
#define ISR_OVERHEAD_US 7

// --- UUIDs --- 
#define SERVICE_UUID        "d1f61c9f-6eef-4911-8b49-98e13dd94938"
#define COMMAND_CHAR_UUID    "abd41953-43f6-426c-b9d3-ff151d71b489"
#define STATE_CHAR_UUID        "c3413c66-4001-42e3-a553-064950de6833"

BLEService trainService(SERVICE_UUID);
BLECharacteristic commandChar(COMMAND_CHAR_UUID, BLEWrite, 32);
BLECharacteristic stateChar(STATE_CHAR_UUID, BLERead | BLENotify, 64);

// --- Timer pro generování pulzů ---
NRF52_MBED_Timer ITimer(NRF_TIMER_1);

// --- Stavy systému a motoru ---
enum SystemState { NORMAL, EMERGENCY_STOP };
SystemState systemState = NORMAL;
enum MotorState { STOPPED, FORWARD, BACKWARD };
volatile MotorState currentMotorState = STOPPED;

// --- Proměnné pro řízení pohybu a stavů ---
int currentSpeedDelay = 50000;
int userRequestedMicrosteps = 32;
int activeMicrosteps = 32;
int currentSpeed = 50;         

// Proměnné pro E-STOP
unsigned long emergencyStopTime = 0;
MotorState directionBeforeEmergency = STOPPED;
unsigned long lastBlinkTime = 0;

struct MovementInfo {
  float wheel_rpm;
  float speed_cm_per_second;
};

// --- Deklarace funkcí ---
void onCommandWritten(BLEDevice central, BLECharacteristic characteristic);
void onBleConnected(BLEDevice central, BLECharacteristic characteristic);
void setMicrostepPins(uint8_t microstep);
void updateAndNotifyState();
void loadSettings();
void saveSettings();
void calculateSpeedDelay();
void logMovementInfo();
MovementInfo calculateMovementInfo();

// ISR - Interrupt Service Routine
void TimerHandler() {
  if (currentMotorState != STOPPED) {
    digitalWrite(STEP_PIN, !digitalRead(STEP_PIN));
  }
}

void setup() {
  Serial.begin(115200);
  unsigned long start_time = millis();
  while (!Serial && (millis() - start_time < 2000));
  
  Serial.println("BLE Vlak Server - Start (v25-full-data)");

  loadSettings();

  pinMode(DIR_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(SLEEP_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(STEP_PIN, LOW);
  digitalWrite(DIR_PIN, LOW);
  digitalWrite(SLEEP_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  
  setMicrostepPins(userRequestedMicrosteps);
  calculateSpeedDelay();
  
  if (ITimer.attachInterruptInterval(currentSpeedDelay, TimerHandler)) {
    Serial.println("Časovač pro motor úspěšně nastaven.");
  } else {
    Serial.println("Chyba: Nepodařilo se nastavit časovač pro motor!");
  }
  ITimer.disableTimer();

  if (!BLE.begin()) {
    Serial.println("Chyba: BLE nelze inicializovat!");
    while (1);
  }
  BLE.setLocalName("VlakBezHW");
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
    if (millis() - emergencyStopTime >= 5000 && digitalRead(SLEEP_PIN) == HIGH) {
        Serial.println("E-Stop: 5s timeout. Uvolňuji motor.");
        digitalWrite(SLEEP_PIN, LOW);
    }
    if (millis() - lastBlinkTime > 250) { 
      lastBlinkTime = millis();
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
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
      ITimer.disableTimer();
      currentMotorState = STOPPED;
      systemState = EMERGENCY_STOP;
      emergencyStopTime = millis();
      digitalWrite(SLEEP_PIN, HIGH);
      updateAndNotifyState();
    }
    return;
  }

  if (systemState == EMERGENCY_STOP) {
    Serial.println("Nouzový stav zrušen.");
    systemState = NORMAL;
    digitalWrite(LED_PIN, LOW);
    updateAndNotifyState();
  }

  bool stateChanged = false;
  bool movementCommand = false;

  if (cmd == "FORWARD") {
    if (currentMotorState != FORWARD) {
      digitalWrite(SLEEP_PIN, HIGH); delay(2);
      digitalWrite(DIR_PIN, HIGH);
      currentMotorState = FORWARD;
      ITimer.enableTimer();
      stateChanged = true;
      movementCommand = true;
    }
  } else if (cmd == "BACKWARD") {
    if (currentMotorState != BACKWARD) {
      digitalWrite(SLEEP_PIN, HIGH); delay(2);
      digitalWrite(DIR_PIN, LOW);
      currentMotorState = BACKWARD;
      ITimer.enableTimer();
      stateChanged = true;
      movementCommand = true;
    }
  } else if (cmd == "STOP") {
    if (currentMotorState != STOPPED) {
        ITimer.disableTimer();
        currentMotorState = STOPPED;
        digitalWrite(STEP_PIN, LOW);
        digitalWrite(SLEEP_PIN, LOW);
        stateChanged = true;
        movementCommand = true;
    }
  } else if (cmd.startsWith("SPEED:")) {
    currentSpeed = cmd.substring(6).toInt();
    calculateSpeedDelay();
    updateAndNotifyState(); 
    movementCommand = true;
  } else if (cmd.startsWith("MICROSTEPS:")) {
    userRequestedMicrosteps = cmd.substring(11).toInt();
    saveSettings();
    calculateSpeedDelay();
    stateChanged = true;
    movementCommand = true; // Přidáno pro logování
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
  if (currentSpeed == 0) {
    ITimer.setInterval(1000000, TimerHandler);
    return;
  }

  long target_full_steps_freq = map(currentSpeed, 1, 100, MOTOR_MIN_FULL_STEPS_PER_SEC, MOTOR_MAX_FULL_STEPS_PER_SEC);

  int newMicrosteps = userRequestedMicrosteps;
  while ((target_full_steps_freq * newMicrosteps * 2) > MAX_TIMER_FREQ && newMicrosteps > 1) {
    newMicrosteps /= 2;
  }

  if (newMicrosteps != activeMicrosteps) {
    activeMicrosteps = newMicrosteps;
    Serial.print("Dynamicky měním microsteps na: "); Serial.println(activeMicrosteps);
    setMicrostepPins(activeMicrosteps);
  }

  long timer_freq = (long)target_full_steps_freq * activeMicrosteps * 2;
  currentSpeedDelay = 1000000L / timer_freq;
  
  ITimer.setInterval(currentSpeedDelay, TimerHandler);
  
  Serial.print("Nová prodleva časovače (us): "); Serial.println(currentSpeedDelay);
}

void setMicrostepPins(uint8_t microstep) {
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

MovementInfo calculateMovementInfo() {
  MovementInfo info = {0.0, 0.0};
  if (currentMotorState == STOPPED || currentSpeed == 0) return info;

  // Zohledníme reálný čas vykonání ISR
  long real_delay = max(currentSpeedDelay, ISR_OVERHEAD_US);

  float pulse_freq = (1000000.0 / real_delay) / 2.0;
  float steps_per_motor_rev = 360.0 / MOTOR_STEP_ANGLE;
  float microsteps_per_motor_rev = steps_per_motor_rev * activeMicrosteps;
  
  info.wheel_rpm = (pulse_freq / microsteps_per_motor_rev) / GEAR_RATIO * 60.0;
  
  float wheel_circumference_mm = WHEEL_DIAMETER_MM * PI;
  info.speed_cm_per_second = (info.wheel_rpm * wheel_circumference_mm) / 60.0 / 10.0;
  
  return info;
}

void updateAndNotifyState() {
  String stateString = "S:";
  if (currentMotorState == FORWARD) stateString += "F";
  else if (currentMotorState == BACKWARD) stateString += "B";
  else stateString += "S";
  
  stateString += ";SP:";
  stateString += currentSpeed;
  stateString += ";MS:";
  stateString += activeMicrosteps;

  MovementInfo info = calculateMovementInfo();
  stateString += ";SC:";
  stateString += String(info.speed_cm_per_second, 2);
  stateString += ";RPM:";
  stateString += String(info.wheel_rpm, 2);

  Serial.print("Odesílám stav: "); Serial.println(stateString);
  stateChar.writeValue(stateString.c_str());
}

void logMovementInfo() {
  if (currentMotorState == STOPPED || currentSpeed == 0) {
    Serial.println("-> Vlak stojí.");
    return;
  }
  MovementInfo info = calculateMovementInfo();
  Serial.println(F("--- Info o pohybu (Realtime) ---"));
  Serial.print(F("  Otáčky kola (RPM): ")); Serial.println(info.wheel_rpm, 2);
  Serial.print(F("  Rychlost (cm/s): ")); Serial.println(info.speed_cm_per_second, 2);
  Serial.println(F("--------------------------------"));
}

#if defined(ARDUINO_ARDUINO_NANO33BLE) || defined(ARDUINO_NANO33BLE_SENSE)
void loadSettings() { 
  int storedValue;
  EEPROM.get(EEPROM_ADDR_MICROSTEPS, storedValue);
  if (storedValue >= 1 && storedValue <= 32) {
    userRequestedMicrosteps = storedValue;
    activeMicrosteps = userRequestedMicrosteps;
    Serial.println("Načteno z EEPROM: " + String(userRequestedMicrosteps));
  } else {
    Serial.println("Nenalezeno platné nastavení v EEPROM, výchozí: 32");
    userRequestedMicrosteps = 32;
    activeMicrosteps = 32;
  }
}
void saveSettings() {
  Serial.println("Ukládám do EEPROM: " + String(userRequestedMicrosteps));
  EEPROM.put(EEPROM_ADDR_MICROSTEPS, userRequestedMicrosteps);
}
#elif defined(ADAFRUIT_FEATHER_NRF52840_EXPRESS) || defined(ADAFRUIT_FEATHER_NRF52840_SENSE) || \
      defined(ADAFRUIT_CLUE_NRF52840_EXPRESS)  || defined(ADAFRUIT_NRF52840_ITSYBITSY)   || \
      defined(SPARKFUN_PRO_NRF52840_MINI)      || defined(SEEED_XIAO_NRF52840)
void loadSettings() {
  InternalFS.begin();
  if (InternalFS.exists(SETTINGS_FILENAME)) {
    File settingsFile = InternalFS.open(SETTINGS_FILENAME, FILE_READ);
    if (settingsFile && settingsFile.available()) {
      userRequestedMicrosteps = settingsFile.parseInt();
      activeMicrosteps = userRequestedMicrosteps;
      Serial.println("Načteno z LittleFS: " + String(userRequestedMicrosteps));
    }
    settingsFile.close();
  } else {
     Serial.println("Soubor s nastavením neexistuje, výchozí: 32");
    userRequestedMicrosteps = 32;
    activeMicrosteps = 32;
  }
}
void saveSettings() {
  File settingsFile = InternalFS.open(SETTINGS_FILENAME, FILE_WRITE);
  if (!settingsFile) {
    Serial.println("Chyba: Nepodařilo se otevřít soubor pro uložení!");
    return;
  }
  settingsFile.print(userRequestedMicrosteps);
  settingsFile.close();
  Serial.println("Uloženo do LittleFS: " + String(userRequestedMicrosteps));
}
#endif
