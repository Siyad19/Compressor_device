#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

LiquidCrystal_I2C lcd(0x27, 16, 4);

// Pin definitions
const int pressureSensorPin = PA8;
const int setpointPin = PA0;
const int compressorPin = PA1;
const int alarmPin = PB14;
const int tempFaultPin = PA9;
const int overloadFaultPin = PA10;
const int manualStopPin = PA5;
const int shutdownPin = PA6;
const int resetPin = PA7;
const int togglePin = PA2;
const int nextPin = PA3;
const int selectPin = PA4;

// Screen states
enum ScreenState { MAIN_SCREEN,
                   MENU_SCREEN };
ScreenState currentScreen = MAIN_SCREEN;
bool inOperatorMenu = false;
bool inMachineMenu = false;

// Menu data
const int MENU_COUNT = 3;
const char* menuItems[MENU_COUNT] = {
  "1.Operator",
  "2.Service ",
  "3.Admin   "
};

int menuIndex = 0;

// Operator submenu
const int OP_COUNT = 5;
const char* operatorItems[OP_COUNT] = {
  "1.Machine    ",
  "2.Pressure   ",
  "3.Temperature  ",
  "4.Maintenance  ",
  "5.Fault-Repair  "
};

int operatorIndex = 0;
int opStart = 0;
const int OP_VISIBLE = 3;

// Machine options
struct MachineSettings {
  bool modeLeft = true;
  bool autoStart = true;
  bool pressureBar = true;
  bool tempCelsius = true;
};

MachineSettings machineSettings;
int machineSettingIndex = 0;
const int MACHINE_OPTIONS = 4;

// Status flags
bool compressorStatus = false;
// bool faultDetected = false;
bool manuallyStopped = false;
bool systemShutdown = false;

unsigned long faultTime = 0;
unsigned long lastDebounce = 0;
const unsigned long DEBOUNCE = 50;

bool lastShutdownState = HIGH;
bool lastResetState = HIGH;
bool lastToggleState = HIGH;
bool lastNextState = HIGH;
bool lastSelectState = HIGH;
bool lastManualState = HIGH;

bool warnTemp = false;
bool changeOilFilter = false;
bool changeAirFilter = false;
bool changeOilSeparator = false;
bool regreaseNeeded = false;
bool changeValveKit = false;
bool changeOil = false;

bool emergency = false;
bool revRotation = false;
bool highSumpPres = false;
bool mainMotorOverload = false;
bool fanMotorOverload = false;

bool warmTime = false;
bool star = false;
bool run = false;
bool runload = false;
bool unload = false;
bool stopBusy = false;
bool ready = false;

bool local = false;
bool remote = false;

bool autoStart = false;
bool unloadMode = false;
 

String currentWarning = "";
String currentFault = "";
String currentStatus = "";
String currentUpdate1 = "";
String currentUpdate2 = "";

void setup() {
  Wire.begin();
  lcd.init();
  lcd.backlight();

  pinMode(pressureSensorPin, INPUT);
  pinMode(setpointPin, INPUT);
  pinMode(tempFaultPin, INPUT_PULLUP);
  pinMode(overloadFaultPin, INPUT_PULLUP);
  pinMode(manualStopPin, INPUT_PULLUP);
  pinMode(shutdownPin, INPUT_PULLUP);
  pinMode(resetPin, INPUT_PULLUP);
  pinMode(togglePin, INPUT_PULLUP);
  pinMode(nextPin, INPUT_PULLUP);
  pinMode(selectPin, INPUT_PULLUP);

  pinMode(compressorPin, OUTPUT);
  pinMode(alarmPin, OUTPUT);
  digitalWrite(compressorPin, LOW);
  digitalWrite(alarmPin, LOW);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");
  delay(2000);
  lcd.clear();

  // Load machine settings from EEPROM
  loadMachineSettings();
}

void loop() {
  unsigned long now = millis();

  // Shutdown
  bool sdt = digitalRead(shutdownPin);
  if (sdt == LOW && lastShutdownState == HIGH && now - lastDebounce > DEBOUNCE) {
    systemShutdown = !systemShutdown;
    lastDebounce = now;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(systemShutdown ? "System Shutdown" : "System Restarted");
    delay(1000);
    lcd.clear();
  }
  lastShutdownState = sdt;

  // Reset
  bool rst = digitalRead(resetPin);
  if (rst == LOW && lastResetState == HIGH && now - lastDebounce > DEBOUNCE) {
    systemShutdown = false;
    manuallyStopped = false;
    stopCompressor();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("System Reset...");
    delay(1000);
    lcd.clear();
  }
  lastResetState = rst;

  if (systemShutdown) return;

  // Manual stop
  bool m = digitalRead(manualStopPin);
  if (m == LOW && lastManualState == HIGH && now - lastDebounce > DEBOUNCE) {
    manuallyStopped = !manuallyStopped;
    if (manuallyStopped) stopCompressor();
    lastDebounce = now;
  }
  lastManualState = m;

  // Home/Menu toggle
  bool tog = digitalRead(togglePin);
  if (tog == LOW && lastToggleState == HIGH && now - lastDebounce > DEBOUNCE) {
    if (currentScreen == MAIN_SCREEN) {
      currentScreen = MENU_SCREEN;
      inOperatorMenu = false;
      inMachineMenu = false;
    } else {
      currentScreen = MAIN_SCREEN;
    }
    lastDebounce = now;
    lcd.clear();
  }
  lastToggleState = tog;

  // MENU_SCREEN
  if (currentScreen == MENU_SCREEN) {
    // Inside Machine submenu
    if (inMachineMenu) {
      static const int VISIBLE = 3;
      static int startIdx = 0;

      bool sel = digitalRead(selectPin);
      if (sel == LOW && lastSelectState == HIGH && now - lastDebounce > DEBOUNCE) {
        // Toggle the selected option
        switch (machineSettingIndex) {
          case 0: machineSettings.modeLeft = !machineSettings.modeLeft; break;
          case 1: machineSettings.autoStart = !machineSettings.autoStart; break;
          case 2: machineSettings.pressureBar = !machineSettings.pressureBar; break;
          case 3: machineSettings.tempCelsius = !machineSettings.tempCelsius; break;
        }
        // Save updated settings to EEPROM
        saveMachineSettings();
        lastDebounce = now;
      }
      lastSelectState = sel;

      bool nxt = digitalRead(nextPin);
      if (nxt == LOW && lastNextState == HIGH && now - lastDebounce > DEBOUNCE) {
        machineSettingIndex = (machineSettingIndex + 1) % MACHINE_OPTIONS;
        if (machineSettingIndex < startIdx) startIdx = machineSettingIndex;
        if (machineSettingIndex >= startIdx + VISIBLE) startIdx = machineSettingIndex - VISIBLE + 1;
        lastDebounce = now;
      }
      lastNextState = nxt;

      // Clear and print machine settings
      lcd.setCursor(0, 0);
      lcd.print("  MACHINE ");
      for (int i = 0; i < VISIBLE; i++) {
        int idx = startIdx + i;
        lcd.setCursor(-4, i + 1);
        lcd.print(idx == machineSettingIndex ? ">" : " ");
        switch (idx) {
          case 0:
            lcd.print("Mode: ");
            lcd.print(machineSettings.modeLeft ? "L         " : "R         ");
            break;
          case 1:
            lcd.print("Auto-start: ");
            lcd.print(machineSettings.autoStart ? "On " : "Off");
            break;
          case 2:
            lcd.print("Pressure: ");
            lcd.print(machineSettings.pressureBar ? "Bar    " : "PSI    ");
            break;
          case 3:
            lcd.print("Temp: ");
            lcd.print(machineSettings.tempCelsius ? "C          " : "F           ");
            break;
        }
      }
      return;
    }

    // Operator submenu
    if (inOperatorMenu) {
      bool nxt = digitalRead(nextPin);
      if (nxt == LOW && lastNextState == HIGH && now - lastDebounce > DEBOUNCE) {
        operatorIndex = (operatorIndex + 1) % OP_COUNT;
        if (operatorIndex < opStart) opStart = operatorIndex;
        if (operatorIndex >= opStart + OP_VISIBLE) opStart = operatorIndex - OP_VISIBLE + 1;
        lastDebounce = now;
      }
      lastNextState = nxt;

      bool sel = digitalRead(selectPin);
      if (sel == LOW && lastSelectState == HIGH && now - lastDebounce > DEBOUNCE) {
        if (operatorIndex == 0) {
          inMachineMenu = true;
        } else {
          inOperatorMenu = false;
        }
        lastDebounce = now;
        lcd.clear();
      }
      lastSelectState = sel;

      lcd.setCursor(0, 0);
      lcd.print("  OPERATOR  ");
      for (int i = 0; i < OP_VISIBLE; i++) {
        int idx = opStart + i;
        lcd.setCursor(-4, 1 + i);
        lcd.print(idx == operatorIndex ? ">" : " ");
        lcd.print(operatorItems[idx]);
      }
      return;
    }

    // Main menu
    bool nxt = digitalRead(nextPin);
    if (nxt == LOW && lastNextState == HIGH && now - lastDebounce > DEBOUNCE) {
      menuIndex = (menuIndex + 1) % MENU_COUNT;
      lastDebounce = now;
    }
    lastNextState = nxt;

    bool sel = digitalRead(selectPin);
    if (sel == LOW && lastSelectState == HIGH && now - lastDebounce > DEBOUNCE) {
      if (menuIndex == 0) {
        inOperatorMenu = true;
        operatorIndex = opStart = 0;
      } else {
        currentScreen = MAIN_SCREEN;
      }
      lastDebounce = now;
      lcd.clear();
    }
    lastSelectState = sel;

    lcd.setCursor(0, 0);
    lcd.print("     MENU      ");
    for (int i = 0; i < MENU_COUNT; i++) {
      lcd.setCursor(-4, 1 + i);
      lcd.print(i == menuIndex ? ">" : " ");
      lcd.print(menuItems[i]);
    }
    return;
  }

  // MAIN SCREEN logic
  // int pressureValue = analogRead(pressureSensorPin);
  // bool tFault = digitalRead(tempFaultPin) == LOW;
  // bool oFault = digitalRead(overloadFaultPin) == LOW;

  // faultDetected = tFault || oFault;
  // if (faultDetected) {
  //   stopCompressor();
  //   digitalWrite(alarmPin, HIGH);
  //   if (faultTime == 0) faultTime = now;
  //   if (now - faultTime >= 5000) {
  //     faultTime = 0;
  //     restartCompressor();
  //   }
  // } else {
  //   digitalWrite(alarmPin, LOW);
  //   if (!manuallyStopped) {
  //     if (compressorStatus) {
  //       if (pressureValue >= analogRead(setpointPin)) stopCompressor();
  //     } else {
  //       if (pressureValue < analogRead(setpointPin)) restartCompressor();
  //     }
  //   }
  // }

  // Draw Home screen
  lcd.setCursor(0, 0);
  lcd.print("Dis.Prs:");
  lcd.print(machineSettings.pressureBar ? "00.0 Bar" : "00.0 PSI");

  lcd.setCursor(0, 1);
  lcd.print("Dis.Temp:");
  lcd.print(machineSettings.tempCelsius ? "0 C" : "0 F");

  lcd.setCursor(-4, 2);
  updateWarningMessage();
  lcd.print(currentWarning);
  
  // lcd.print(faultDetected ? "Fault" : "No Fault");
  lcd.setCursor(6, 2);
  updateFaultMessage();
  lcd.print(currentFault);

  lcd.setCursor(-4, 3);
  updateStatusMessage();
  lcd.print(currentStatus);

  lcd.setCursor(6, 3);
  updateMessage1();
  lcd.print(currentUpdate1);

  lcd.setCursor(8, 3);
  updateMessage2();
  lcd.print(currentUpdate2);

  delay(300);
}

void stopCompressor() {
  digitalWrite(compressorPin, LOW);
  compressorStatus = false;
}

void restartCompressor() {
  digitalWrite(compressorPin, HIGH);
  compressorStatus = true;
}

void updateWarningMessage() {
  if (warnTemp) {
    currentWarning = "High Temp";
  } else if (changeOilFilter) {
    currentWarning = "Change Oil Filter";
  } else if (changeAirFilter) {
    currentWarning = "Air Filter";
  } else if (changeOilSeparator) {
    currentWarning = "Change Oil Sep.";
  } else if (regreaseNeeded) {
    currentWarning = "Regrease Required";
  } else if (changeValveKit) {
    currentWarning = "Change-Valve-Kit";
  } else if (changeOil) {
    currentWarning = "Change Oil";
  } else {
    currentWarning = "Warning";  // No warning
  }
}

void updateFaultMessage() {
  if (warnTemp) {
    currentFault = "Emergency";
  } else if (emergency) {
    currentFault = "Rev Rotation";
  } else if (revRotation) {
    currentFault = "High sump pres";
  } else if (highSumpPres) {
    currentFault = "Main motor overload";
  } else if (mainMotorOverload) {
    currentFault = "Fan motor overload";
  } else if (fanMotorOverload) {
    currentFault = "Trip";
  } else {
    currentFault = "Fault";  // No warning
  }
}

void updateStatusMessage() {
  if (warnTemp) {
    currentStatus = "Warm-time";
  } else if (warmTime) {
    currentStatus = "Star";
  } else if (star) {
    currentStatus = "Run";
  } else if (run) {
    currentStatus = "Runload";
  } else if (runload) {
    currentStatus = "Unload";
  } else if (unload) {
    currentStatus = "Stop busy";
  } else if (stopBusy) {
    currentStatus = "Ready";
  } else if (ready) {
    currentStatus = "Start Ack";
  }  else {
    currentStatus = "Status";  // No warning
  }
}

void updateMessage1() {
  if (local) {
    currentUpdate1 = "L";
  } else if (remote) {
    currentUpdate1 = "R"; 
  } else {
    currentUpdate1 = "-";
  }
}

void updateMessage2() {
  if (autoStart) {
    currentUpdate2 = "A";
  } else if (unloadMode) {
    currentUpdate2 = "UL";  
  }  else {
    currentUpdate2 = "-";
  }
}

void saveMachineSettings() {
  EEPROM.write(0, machineSettings.modeLeft ? 1 : 0);
  EEPROM.write(1, machineSettings.autoStart ? 1 : 0);
  EEPROM.write(2, machineSettings.pressureBar ? 1 : 0);
  EEPROM.write(3, machineSettings.tempCelsius ? 1 : 0);
}

void loadMachineSettings() {
  machineSettings.modeLeft = EEPROM.read(0) == 1;
  machineSettings.autoStart = EEPROM.read(1) == 1;
  machineSettings.pressureBar = EEPROM.read(2) == 1;
  machineSettings.tempCelsius = EEPROM.read(3) == 1;
}
