/**
 * Firmware pro hardwarový BLE ovladač vlaku.
 * 
 * Platforma: Arduino MBED Core (Arduino Nano 33 BLE)
 * Knihovna: ArduinoBLE
 */

// =============================================================================
//  NASTAVENÍ LADĚNÍ
// =============================================================================
#define DEBUG_MODE

#ifdef DEBUG_MODE
  #define DEBUG_PRINT(...) Serial.print(__VA_ARGS__)
  #define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
  #define DEBUG_PRINT(...)
  #define DEBUG_PRINTLN(...)
#endif

// =============================================================================
//  KNIHOVNY
// =============================================================================
#include <ArduinoBLE.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <vector>

// =============================================================================
//  KONFIGURACE PINŮ (ARDUINO NANO 33 BLE)
// =============================================================================
#define PIN_SCL         P0_2   // I2C SCL
#define PIN_SDA         P0_31  // I2C SDA
#define PIN_OLED_POWER  P1_9   // Pin pro napájení displeje a "power" LED
#define ENCODER_SW_PIN  P0_30  // Tlačítko enkodéru
#define HORN_BUTTON_PIN P0_29  // Tlačítko houkačky
#define ENCODER_CLK_PIN P0_5   // Enkodér CLK (A) (TRA)
#define ENCODER_DT_PIN  P0_4   // Enkodér DT (B) (TRB)

// =============================================================================
//  GLOBÁLNÍ PROMĚNNÉ A KONSTANTY
// =============================================================================

// --- Displej ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- BLE UUIDs ---
const char* serviceUuid     = "d1f61c9f-6eef-4911-8b49-98e13dd94938";
const char* commandCharUuid = "abd41953-43f6-426c-b9d3-ff151d71b489";
const char* stateCharUuid   = "c3413c66-4001-42e3-a553-064950de6833";

// --- Enkodér ---
volatile long encoderPos = 0;
volatile int selectedDeviceIndexVolatile = 0;
volatile bool encoderMoved = false;

// --- Stavové proměnné ---
enum ControllerState { STATE_SCANNING, STATE_DEVICE_SELECTION, STATE_CONNECTING, STATE_CONTROLLING, STATE_SLEEPING };
ControllerState currentState;
bool needsDisplayUpdate = false;
unsigned long scanStartTime = 0;
const unsigned long scanDuration = 3000;
bool isSynced = false; 

// --- BLE Objekty ---
std::vector<BLEDevice> discoveredDevices;
int selectedDeviceIndex = 0;
BLEDevice trainPeripheral;
BLECharacteristic commandChar;
BLECharacteristic stateChar;

// --- Stav vlaku ---
float train_rpm = 0.0;
float train_speed_cms = 0.0;
int train_speed_perc = 0;
String train_state = "S";
int train_microsteps = 0;

// --- Ovládání ---
int currentSpeed = 0;
bool hornActive = false;
unsigned long encoderButtonPressTime = 0;
const long longPressDuration = 1500;
bool isPaused = false;
int speedBeforePause = 0;
String directionBeforePause = "F";


// =============================================================================
//  DEKLARACE FUNKCÍ
// =============================================================================
void sendCommand(const String& cmd);
void updateControllingDisplay();
void handleInputs();
void parseState(const String& stateString);
void goToSleep();
void startScanning();
void updateScanningDisplay();
void onDisconnected(BLEDevice central);
void processReceivedData();
void encoderISR();
void wakeupISR();

// =============================================================================
//  HLAVNÍ PROGRAM (SETUP & LOOP)
// =============================================================================

void setup() {
  pinMode(PIN_OLED_POWER, OUTPUT);
  digitalWrite(PIN_OLED_POWER, HIGH);
  delay(10); 

  Serial.begin(115200);
  while (!Serial && millis() < 2000);
  DEBUG_PRINTLN("Booting up controller (Arduino Core)...");

  Wire.begin();
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    DEBUG_PRINTLN("Chyba: SSD1306 - displej nenalezen!"); for(;;);
  }
  DEBUG_PRINTLN("Displej OK.");
  
  pinMode(ENCODER_SW_PIN, INPUT_PULLUP);
  pinMode(HORN_BUTTON_PIN, INPUT_PULLUP);
  pinMode(ENCODER_CLK_PIN, INPUT_PULLUP);
  pinMode(ENCODER_DT_PIN, INPUT_PULLUP);
  
  attachInterrupt(digitalPinToInterrupt(ENCODER_CLK_PIN), encoderISR, RISING);
  DEBUG_PRINTLN("Enkoder ISR inicializovan.");

  display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0); display.println("Ovladač vlaku");
  display.display();
  delay(1000);
  
  DEBUG_PRINTLN("Startuji BLE...");
  if (!BLE.begin()) {
    DEBUG_PRINTLN("Chyba: BLE nelze inicializovat!"); while(1);
  }

  BLE.setEventHandler(BLEDisconnected, onDisconnected);
  
  startScanning();
}

void loop() {
  switch (currentState) {
    case STATE_SCANNING: {
      BLE.poll();
      if (needsDisplayUpdate) {
        display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 20); display.println("Skenuji vlaky...");
        display.display();
        needsDisplayUpdate = false;
      }

      BLEDevice peripheral = BLE.available();
      if (peripheral) {
          String localName = peripheral.localName();
          if (localName == "VlakBezHW" || localName == "TrainBLE") {
              bool found = false;
              for(const auto& dev : discoveredDevices) {
                  if (dev.address() == peripheral.address()) {
                      found = true;
                      break;
                  }
              }
              if (!found) {
                  DEBUG_PRINT("Nalezen vlak: "); DEBUG_PRINTLN(localName);
                  discoveredDevices.push_back(peripheral);
              }
          }
      }

      if (millis() - scanStartTime > scanDuration) {
        BLE.stopScan();
        DEBUG_PRINTLN("Skenovani dokonceno.");
        DEBUG_PRINT("Nalezeno celkem vlaku: "); DEBUG_PRINTLN(discoveredDevices.size());
        currentState = STATE_DEVICE_SELECTION;
        encoderPos = 0;
        needsDisplayUpdate = true;
      }
      break;
    }
    case STATE_DEVICE_SELECTION: {
      if (needsDisplayUpdate) {
        updateScanningDisplay();
        needsDisplayUpdate = false;
      }

      if (encoderMoved) {
        noInterrupts();
        encoderMoved = false;
        if (discoveredDevices.size() > 0) {
            selectedDeviceIndex = selectedDeviceIndexVolatile % discoveredDevices.size();
            if (selectedDeviceIndex < 0) selectedDeviceIndex += discoveredDevices.size();
        }
        interrupts();
        DEBUG_PRINT("Vyber zarizeni: "); DEBUG_PRINTLN(selectedDeviceIndex);
        needsDisplayUpdate = true;
      }

      if (digitalRead(ENCODER_SW_PIN) == LOW) {
        delay(50);
        if (digitalRead(ENCODER_SW_PIN) == LOW) {
           if (discoveredDevices.size() > 0) {
              currentState = STATE_CONNECTING;
              trainPeripheral = discoveredDevices[selectedDeviceIndex];
              DEBUG_PRINT("Pokus o pripojeni k: "); DEBUG_PRINTLN(trainPeripheral.address());
              display.clearDisplay(); display.setCursor(0,0); display.println("Pripojuji..."); display.display();
              
              if (trainPeripheral.connect()) {
                DEBUG_PRINTLN("Pripojeno. Zjistuji sluzby...");
                if (trainPeripheral.discoverAttributes()) {
                  commandChar = trainPeripheral.characteristic(commandCharUuid);
                  stateChar = trainPeripheral.characteristic(stateCharUuid);
                  if (commandChar && stateChar && stateChar.canSubscribe()) {
                    DEBUG_PRINTLN("Sluzby a charakteristiky OK. Prihlasuji k odberu...");
                    if (stateChar.subscribe()) {
                      DEBUG_PRINTLN("Prihlaseni k odberu uspesne. Cekam na prvni stav...");
                      currentState = STATE_CONTROLLING;
                    } else {
                      DEBUG_PRINTLN("CHYBA: Prihlaseni k odberu selhalo.");
                      trainPeripheral.disconnect();
                    }
                  } else {
                    DEBUG_PRINTLN("CHYBA: Klicove charakteristiky nenalezeny.");
                    trainPeripheral.disconnect();
                  }
                } else {
                  DEBUG_PRINTLN("CHYBA: Nepodarilo se objevit sluzby.");
                  trainPeripheral.disconnect();
                }
              } else {
                DEBUG_PRINTLN("CHYBA: Pripojeni selhalo.");
                startScanning();
              }
           } else {
              DEBUG_PRINTLN("Seznam je prazny, nove skenovani.");
              startScanning();
           }
        }
      }
      break;
    }
    case STATE_CONTROLLING:
      BLE.poll();
      if (stateChar.valueUpdated()) {
        processReceivedData();
      }
      if (isSynced) {
        handleInputs();
      }
      updateControllingDisplay();
      break;
    case STATE_SLEEPING:
      // Neděláme nic, čekáme na probuzení přes interrupt
      delay(1000);
      break;
    default:
      delay(10);
      break;
  }
}

// =============================================================================
//  DEFINICE FUNKCÍ
// =============================================================================

void wakeupISR() {
  // Provedeme softwarový restart, což je nejčistší způsob probuzení
  NVIC_SystemReset();
}

void encoderISR() {
  static unsigned long lastInterruptTime = 0;
  unsigned long interruptTime = millis();
  
  if (interruptTime - lastInterruptTime > 5) { // Debounce
    if (digitalRead(ENCODER_DT_PIN) != digitalRead(ENCODER_CLK_PIN)) {
      encoderPos++;
    } else {
      encoderPos--;
    }
    
    if (currentState == STATE_DEVICE_SELECTION) {
        selectedDeviceIndexVolatile = encoderPos;
    }
    encoderMoved = true;
    lastInterruptTime = interruptTime;
  }
}

void sendCommand(const String& cmd) {
  if (trainPeripheral && trainPeripheral.connected() && commandChar) {
    DEBUG_PRINT("Odesilam prikaz: "); DEBUG_PRINTLN(cmd);
    commandChar.writeValue((const uint8_t*)cmd.c_str(), cmd.length(), true);
  } else {
    DEBUG_PRINT("Nelze odeslat prikaz (nepripojeno): "); DEBUG_PRINTLN(cmd);
  }
}

void startScanning() {
  DEBUG_PRINTLN("Zmena stavu na STATE_SCANNING");
  discoveredDevices.clear();
  selectedDeviceIndex = 0;
  isSynced = false;
  currentState = STATE_SCANNING;
  needsDisplayUpdate = true;
  scanStartTime = millis();
  BLE.scan(false);
}

void onDisconnected(BLEDevice central) {
  DEBUG_PRINT("Odpojeno od: "); DEBUG_PRINTLN(central.address());
  startScanning();
}

void processReceivedData() {
  String stateString = "";
  const uint8_t* value = stateChar.value();
  for(int i=0; i < stateChar.valueLength(); i++) {
    stateString += (char)value[i];
  }
  parseState(stateString);
}

void updateScanningDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Vyber vlak:");
  display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);

  if (discoveredDevices.empty()) {
    display.setCursor(0, 20);
    display.println("Zadny vlak nenalezen.");
    display.setCursor(0, 30);
    display.println("Stiskni pro opakovani.");
  } else {
    const int MAX_ITEMS_ON_SCREEN = 6;
    const int ITEM_HEIGHT = 8;
    const int LIST_START_Y = 12;
    int total_devices = discoveredDevices.size();

    int view_start_index = 0;
    if (total_devices > MAX_ITEMS_ON_SCREEN) {
      view_start_index = selectedDeviceIndex - (MAX_ITEMS_ON_SCREEN / 2);
      if (view_start_index < 0) view_start_index = 0;
      if (view_start_index > total_devices - MAX_ITEMS_ON_SCREEN) {
        view_start_index = total_devices - MAX_ITEMS_ON_SCREEN;
      }
    }

    for (int i = view_start_index; i < view_start_index + MAX_ITEMS_ON_SCREEN && i < total_devices; i++) {
      int display_row = i - view_start_index;
      int y_pos = LIST_START_Y + (display_row * ITEM_HEIGHT);

      if (i == selectedDeviceIndex) {
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
      } else {
        display.setTextColor(SSD1306_WHITE);
      }
      display.setCursor(0, y_pos);
      
      String deviceName = discoveredDevices[i].localName();
      if (deviceName.length() > 0) {
        display.println(deviceName);
      } else {
        display.println(discoveredDevices[i].address());
      }
    }
    display.setTextColor(SSD1306_WHITE);
  }
  display.display();
}

void handleInputs() {
  long newPos;
  noInterrupts();
  newPos = encoderPos;
  interrupts();

  if (newPos != currentSpeed) {
    if (isPaused) {
      isPaused = false; 
    }
    currentSpeed = newPos;
    if (abs(currentSpeed) > 100) {
      currentSpeed = (currentSpeed > 0) ? 100 : -100;
      noInterrupts();
      encoderPos = currentSpeed;
      interrupts();
    }
    
    if (currentSpeed == 0) sendCommand("STOP");
    else if (currentSpeed > 0) sendCommand("FORWARD");
    else sendCommand("BACKWARD");
    sendCommand("SPEED:" + String(abs(currentSpeed)));
  }

  if (digitalRead(ENCODER_SW_PIN) == LOW) {
    if (encoderButtonPressTime == 0) {
      encoderButtonPressTime = millis();
    }
    if (millis() - encoderButtonPressTime > longPressDuration) {
      sendCommand("STOP");
      if(trainPeripheral && trainPeripheral.connected()) trainPeripheral.disconnect();
      goToSleep();
    }
  } else {
    if (encoderButtonPressTime > 0 && (millis() - encoderButtonPressTime < longPressDuration)) {
      if (isPaused) {
        isPaused = false;
        if (directionBeforePause == "F") sendCommand("FORWARD");
        else sendCommand("BACKWARD");
        sendCommand("SPEED:" + String(speedBeforePause));
        
        currentSpeed = (directionBeforePause == "F") ? speedBeforePause : -speedBeforePause;
        noInterrupts();
        encoderPos = currentSpeed;
        interrupts();

      } else {
        if (train_speed_perc > 0) {
            isPaused = true;
            speedBeforePause = train_speed_perc;
            directionBeforePause = train_state;
        }
        currentSpeed = 0;
        noInterrupts();
        encoderPos = 0;
        interrupts();
        sendCommand("STOP");
      }
    }
    encoderButtonPressTime = 0;
  }

  bool hornBtnState = (digitalRead(HORN_BUTTON_PIN) == LOW);
  if (hornBtnState != hornActive) {
    hornActive = hornBtnState;
    sendCommand(hornActive ? "HORN_ON" : "HORN_OFF");
  }
}

void updateControllingDisplay() {
  display.clearDisplay(); display.setCursor(0, 0);
  display.print("Vlak OK | Bat: --%");
  
  display.setTextSize(2); display.setCursor(15, 16);

  if(isPaused) {
    if(directionBeforePause == "F") display.print("|> ");
    else display.print("<| ");
    display.print(speedBeforePause); 
  } else {
    if (train_state == "F") display.print(">> ");
    else if (train_state == "B") display.print("<< ");
    else display.print("|| ");
    display.print(train_speed_perc);
  }
  display.print("%");
  display.setTextSize(1);

  display.setCursor(0, 40); display.print("RPM: "); display.print(train_rpm, 1);
  display.setCursor(0, 50); display.print("cm/s: "); display.print(train_speed_cms, 1);

  int barWidth = map(abs(currentSpeed), 0, 100, 0, SCREEN_WIDTH);
  if (currentSpeed >= 0) {
    display.fillRect(0, SCREEN_HEIGHT - 5, barWidth, 5, SSD1306_WHITE);
  } else {
    display.fillRect(SCREEN_WIDTH - barWidth, SCREEN_HEIGHT - 5, barWidth, 5, SSD1306_WHITE);
  }
  
  display.display();
}

void parseState(const String& stateString) {
    DEBUG_PRINT("Prijata data o stavu: "); DEBUG_PRINTLN(stateString);
    int s_idx = stateString.indexOf("S:");
    int sp_idx = stateString.indexOf("SP:");
    int sc_idx = stateString.indexOf("SC:");
    int rpm_idx = stateString.indexOf("RPM:");
    int ms_idx = stateString.indexOf("MS:");

    if(s_idx != -1) train_state = stateString.substring(s_idx+2, s_idx+3);
    if(sp_idx != -1) train_speed_perc = stateString.substring(sp_idx+3, stateString.indexOf(";", sp_idx)).toInt();
    if(sc_idx != -1) train_speed_cms = stateString.substring(sc_idx+3, stateString.indexOf(";", sc_idx)).toFloat();
    if(rpm_idx != -1) train_rpm = stateString.substring(rpm_idx+4, stateString.indexOf(";", rpm_idx)).toFloat();
    if(ms_idx != -1) train_microsteps = stateString.substring(ms_idx+3, stateString.indexOf(";", ms_idx)).toInt();
    
    if (!isSynced) {
      DEBUG_PRINTLN("Prvni synchronizace se stavem vlaku.");
      if (train_state == "S") {
        DEBUG_PRINTLN("Vlak stoji, synchronizuji na rychlost 0.");
        currentSpeed = 0;
        train_speed_perc = 0; 
        sendCommand("SPEED:0"); 
      } else if (train_state == "F") {
        currentSpeed = train_speed_perc;
      } else if (train_state == "B") {
        currentSpeed = -train_speed_perc;
      }
      noInterrupts();
      encoderPos = currentSpeed;
      interrupts();
      isSynced = true;
      isPaused = false;
    }
}

void goToSleep(){
  DEBUG_PRINTLN("Uspavam zarizeni...");

  // Uspíme displej přes I2C
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  
  // Zhasneme "power" LED přepnutím pinu na INPUT
  pinMode(PIN_OLED_POWER, INPUT); 
  
  // Připravíme pin houkačky pro probuzení a čekáme
  currentState = STATE_SLEEPING;
  DEBUG_PRINTLN("Probudit stiskem tlacitka houkacky.");
  attachInterrupt(digitalPinToInterrupt(HORN_BUTTON_PIN), wakeupISR, LOW);
  
  // V tomto bodě se loop() zasekne ve stavu STATE_SLEEPING a čeká
}
