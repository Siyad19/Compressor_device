#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

LiquidCrystal_I2C lcd(0x27, 16, 4);

// Pin definitions
const int pressureSensorPin = PA9;
const int potPin = PA0;
const int compressorPin = PA1;
const int alarmPin = PB14;
const int tempSensorPin = PA8;
// const int overloadFaultPin = PA10;
const int shutdownPin = PA6;
const int resetPin = PA7;
const int togglePin = PA2;
const int nextPin = PA3;
const int selectPin = PA4;
const int fanSensorPin = PA11;
const int backPin = PA5;  // previously manualStopPin


// Screen states
enum ScreenState { MAIN_SCREEN,
                   MENU_SCREEN,
};

ScreenState currentScreen = MAIN_SCREEN;

bool inOperatorMenu = false;
bool inServiceMenu = false;
bool inAdminMenu = false;

bool inMachineMenu = false;

float resistanceToTempC(float resistance) {
  return (resistance - 1000.0) / 3.85;
}
bool inTempMenu = false;
const int TEMP_OPTIONS = 3;
int tempSettingIndex = 0;
float fanTemp = 0.0;    // From sensor
float warnTemp = 60.0;  // Set via PA0
float tripTemp = 80.0;  // Set via PA0

bool inMaintenanceMenu = false;
bool warningTemp = false;
bool changeOilFilter = false;
bool changeAirFilter = false;
bool changeOilSeparator = false;
bool regreaseNeeded = false;
bool changeValveKit = false;
bool changeOil = false;

bool inFaultReportMenu = false;
bool emergency = false;
bool revRotation = false;
bool highSumpPres = false;
bool mainMotorOverload = false;
bool fanMotorOverload = false;




bool inPressureMenu = false;
int pressureSettingIndex = 0;
const int PRESSURE_OPTIONS = 3;
float loadPressure = 0.0;
float reloadPressure = 0.0;
float hspPressure = 0.0;


// Menu data
const int MENU_COUNT = 3;
const char* menuItems[MENU_COUNT] = {
  "1.Operator   ",
  "2.Service    ",
  "3.Admin      "
};
int menuIndex = 0;


// Operator submenu
const int OP_COUNT = 5;
const char* operatorItems[OP_COUNT] = {
  "1.Machine      ",
  "2.Pressure     ",
  "3.Temperature  ",
  "4.Maintenance  ",
  "5.Fault-Report  "
};
int operatorIndex = 0;
int opStart = 0;
const int OP_VISIBLE = 3;

// Service submenu
const int SR_COUNT = 4;
const char* serviceItems[SR_COUNT] = {
  "1.Cumulative   ",
  "2.Delay        ",
  "3.Drain value  ",
  "4.Scheduled    ",
};
int serviceIndex = 0;
int srStart = 0;
const int SR_VISIBLE = 3;

bool inServiceItem1 = false;
bool inServiceItem2 = false;
bool inServiceItem3 = false;
bool inServiceItem4 = false;

int adminIndex = 0;
int adStart = 0;
#define AD_VISIBLE 2  // Number of visible lines
#define AD_COUNT 2    // Total admin menu items
const char* adminItems[AD_COUNT] = {
  "Option 1        ",
  "Option 2        "
};

bool inAdminItem1 = false;
bool inAdminItem2 = false;

float rlValue = 0.0;
float ulValue = 0.0;
float lValue = 0.0;

float ulPres = 0.0;
float lPres = 0.0;
int tempTrip = 0;
int tempWarn = 0;


// Machine options
struct MachineSettings {
  bool mode = true;
  bool autoStart = true;
  bool pressureBar = true;
  bool tempCelsius = true;
};

MachineSettings machineSettings;
int machineSettingIndex = 0;
const int MACHINE_OPTIONS = 4;

// Status flags
bool compressorStatus = false;
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

bool warmTime = false;
bool star = false;
bool run = false;
bool runload = false;
bool unload = false;
bool stopBusy = false;
bool ready = false;

bool hasActiveFaults() {
  return emergency || revRotation || highSumpPres || mainMotorOverload || fanMotorOverload;
}


String currentWarning = "";
String currentFault = "";
String currentStatus = "";


// Function declarations (prototypes)
void loadMachineSettings();
void saveMachineSettings();
void stopCompressor();
void updateWarningMessage();
void updateFaultMessage();
void updateStatusMessage();

#define MAX_FAULTS 6  // Adjust this based on the number of fault types

bool faultFlags[MAX_FAULTS] = { false, false, false, false, false, false };

const char* faultMessages[MAX_FAULTS] = {
  "High Temp",
  "Low Pressure",
  "High Pressure",
  "Oil Change Needed",
  "Filter Blocked",
  "Voltage Error"
};



void setup() {
  // Load machine settings from EEPROM
  EEPROM.begin();
  loadMachineSettings();


  Wire.begin();
  lcd.init();
  lcd.backlight();

  pinMode(pressureSensorPin, INPUT);
  pinMode(potPin, INPUT);
  pinMode(tempSensorPin, INPUT);
  // pinMode(overloadFaultPin, INPUT_PULLUP);
  // pinMode(manualStopPin, INPUT_PULLUP);
  pinMode(shutdownPin, INPUT_PULLUP);
  pinMode(resetPin, INPUT_PULLUP);
  pinMode(togglePin, INPUT_PULLUP);
  pinMode(nextPin, INPUT_PULLUP);
  pinMode(selectPin, INPUT_PULLUP);
  pinMode(backPin, INPUT_PULLUP);  // Previously manualStopPin


  pinMode(compressorPin, OUTPUT);
  pinMode(alarmPin, OUTPUT);
  digitalWrite(compressorPin, LOW);
  digitalWrite(alarmPin, LOW);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");
  delay(2000);
  lcd.clear();
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

  // Home/Menu toggle
  bool tog = digitalRead(togglePin);
  if (tog == LOW && lastToggleState == HIGH && now - lastDebounce > DEBOUNCE) {
    if (currentScreen == MAIN_SCREEN) {
      currentScreen = MENU_SCREEN;
      inOperatorMenu = false;
      inMachineMenu = false;
      inServiceMenu = false;
      inAdminMenu = false;

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
          case 0: machineSettings.mode = !machineSettings.mode; break;
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

      bool back = digitalRead(backPin);
      if (back == LOW && lastManualState == HIGH && now - lastDebounce > DEBOUNCE) {
        inMachineMenu = false;  // Exit Machine submenu
        inOperatorMenu = true;  // Return to Operator Menu (or MENU_SCREEN)
        lastDebounce = now;
        lcd.clear();
      }
      lastManualState = back;


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
            lcd.print(machineSettings.mode ? "Local     " : "Remote     ");
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

    if (inPressureMenu) {
      static const int VISIBLE = 3;
      static int startIdx = 0;
      static bool adjusting = false;     // Are we adjusting a value?
      static bool valueChanged = false;  // Did the value actually change?

      // Read buttons at the start
      bool sel = digitalRead(selectPin);
      bool nxt = digitalRead(nextPin);

      unsigned long now = millis();  // Ensure you have `now` defined before using

      // Handle Next button press to navigate (only when NOT adjusting)
      if (!adjusting && nxt == LOW && lastNextState == HIGH && now - lastDebounce > DEBOUNCE) {
        pressureSettingIndex = (pressureSettingIndex + 1) % PRESSURE_OPTIONS;
        if (pressureSettingIndex < startIdx) startIdx = pressureSettingIndex;
        if (pressureSettingIndex >= startIdx + VISIBLE) startIdx = pressureSettingIndex - VISIBLE + 1;
        lastDebounce = now;
      }
      lastNextState = nxt;

      // Handle Select button to toggle adjust mode
      if (sel == LOW && lastSelectState == HIGH && now - lastDebounce > DEBOUNCE) {
        adjusting = !adjusting;  // Toggle adjustment mode
        lastDebounce = now;
      }
      lastSelectState = sel;

      bool back = digitalRead(backPin);
      if (back == LOW && lastManualState == HIGH && now - lastDebounce > DEBOUNCE) {
        inMachineMenu = false;  // Exit Machine submenu
        inPressureMenu = false;
        inOperatorMenu = true;  // Return to Operator Menu (or MENU_SCREEN)
        lastDebounce = now;
        lcd.clear();
      }
      lastManualState = back;


      // If adjusting, read potentiometer to update selected pressure setting
      if (adjusting) {
        int raw = analogRead(potPin);
        float newValue = map(raw, 0, 4095, 0, 100) / 10.0;  // Example: Adjusting within 0-10 bar range

        switch (pressureSettingIndex) {
          case 0:
            if (abs(loadPressure - newValue) > 0.05) {
              loadPressure = newValue;
              valueChanged = true;
            }
            break;
          case 1:
            if (abs(reloadPressure - newValue) > 0.05) {
              reloadPressure = newValue;
              valueChanged = true;
            }
            break;
          case 2:
            if (abs(hspPressure - newValue) > 0.05) {
              hspPressure = newValue;
              valueChanged = true;
            }
            break;
        }
      }

      // Save values to EEPROM when Select is released
      if (sel == HIGH && lastSelectState == LOW && adjusting && valueChanged) {
        EEPROM.put(0, loadPressure);
        EEPROM.put(sizeof(float), reloadPressure);
        EEPROM.put(2 * sizeof(float), hspPressure);
        valueChanged = false;
        adjusting = false;
        lastDebounce = now;
      }

      lcd.setCursor(0, 0);
      lcd.print("  PRESSURE  ");
      for (int i = 0; i < VISIBLE; i++) {
        int idx = startIdx + i;
        lcd.setCursor(-4, i + 1);
        lcd.print(idx == pressureSettingIndex ? ">" : " ");
        switch (idx) {
          case 0:
            lcd.print("Load : ");
            lcd.print(loadPressure, 1);
            lcd.print(" Bar  ");
            break;
          case 1:
            lcd.print("Reload: ");
            lcd.print(reloadPressure, 1);
            lcd.print(" Bar   ");
            break;
          case 2:
            lcd.print("HSP   : ");
            lcd.print(hspPressure, 1);
            lcd.print(" Bar   ");
            break;
        }
      }
      return;
    }

    if (inTempMenu) {
      static const int VISIBLE = 3;
      static int startIdx = 0;
      static bool adjusting = false;     // Are we adjusting a value?
      static bool valueChanged = false;  // Did the value actually change?

      // Read buttons at the start
      bool sel = digitalRead(selectPin);
      bool nxt = digitalRead(nextPin);

      unsigned long now = millis();  // Ensure you have `now` defined before using

      // Handle Next button press to navigate (only when NOT adjusting)
      if (!adjusting && nxt == LOW && lastNextState == HIGH && now - lastDebounce > DEBOUNCE) {
        tempSettingIndex = (tempSettingIndex + 1) % TEMP_OPTIONS;
        if (tempSettingIndex < startIdx) startIdx = tempSettingIndex;
        if (tempSettingIndex >= startIdx + VISIBLE) startIdx = tempSettingIndex - VISIBLE + 1;
        lastDebounce = now;
      }
      lastNextState = nxt;

      // Handle Select button to toggle adjust mode
      if (sel == LOW && lastSelectState == HIGH && now - lastDebounce > DEBOUNCE) {
        // Only allow adjustment for index 0 and 1
        if (tempSettingIndex < 2) adjusting = !adjusting;
        lastDebounce = now;
      }
      lastSelectState = sel;

      bool back = digitalRead(backPin);
      if (back == LOW && lastManualState == HIGH && now - lastDebounce > DEBOUNCE) {
        inMachineMenu = false;  // Exit Machine submenu
        inPressureMenu = false;
        inTempMenu = false;
        inMaintenanceMenu = false;
        inFaultReportMenu = false;
        inOperatorMenu = true;  // Return to Operator Menu (or MENU_SCREEN)
        lastDebounce = now;
        lcd.clear();
      }
      lastManualState = back;

      // If adjusting, read potentiometer to update selected temperature setting
      if (adjusting) {
        int raw = analogRead(potPin);
        float newValue = map(raw, 0, 1023, 0, 1000) / 10.0;


        switch (tempSettingIndex) {
          case 0:
            if (abs(warnTemp - newValue) > 0.05) {
              warnTemp = newValue;
              valueChanged = true;
            }
            break;
          case 1:
            if (abs(tripTemp - newValue) > 0.05) {
              tripTemp = newValue;
              valueChanged = true;
            }
            break;
        }
      }

      // Save values to EEPROM when Select is released
      if (sel == HIGH && lastSelectState == LOW && adjusting && valueChanged) {
        EEPROM.put(0, warnTemp);
        EEPROM.put(sizeof(float), tripTemp);
        valueChanged = false;
        adjusting = false;
        lastDebounce = now;
      }

      // Always read fan sensor
      int rawFan = analogRead(fanSensorPin);
      fanTemp = map(rawFan, 0, 4095, 0, 100) / 10.0;

      lcd.setCursor(0, 0);
      lcd.print(" TEMPERATURE ");
      for (int i = 0; i < VISIBLE; i++) {
        int idx = startIdx + i;
        lcd.setCursor(-4, i + 1);
        lcd.print(idx == tempSettingIndex ? ">" : " ");
        switch (idx) {
          case 0:
            lcd.print("WarnTemp:");
            lcd.print(warnTemp, 1);
            lcd.print(" C ");
            break;
          case 1:
            lcd.print("TripTemp:");
            lcd.print(tripTemp, 1);
            lcd.print(" C ");
            break;
          case 2:
            lcd.print("FanTemp:");
            lcd.print(fanTemp, 1);
            lcd.print(" C ");
            break;
        }
      }
      return;
    }

    if (inMaintenanceMenu) {
      static const int VISIBLE = 3;
      static int startIdx = 0;
      static int maintenanceIndex = 0;
      const int MAINTENANCE_OPTIONS = 6;

      bool sel = digitalRead(selectPin);
      bool nxt = digitalRead(nextPin);
      unsigned long now = millis();

      // Navigate through the list
      if (nxt == LOW && lastNextState == HIGH && now - lastDebounce > DEBOUNCE) {
        maintenanceIndex = (maintenanceIndex + 1) % MAINTENANCE_OPTIONS;
        if (maintenanceIndex < startIdx) startIdx = maintenanceIndex;
        if (maintenanceIndex >= startIdx + VISIBLE) startIdx = maintenanceIndex - VISIBLE + 1;
        lastDebounce = now;
      }
      lastNextState = nxt;

      bool back = digitalRead(backPin);
      if (back == LOW && lastManualState == HIGH && now - lastDebounce > DEBOUNCE) {
        inMachineMenu = false;  // Exit Machine submenu
        inPressureMenu = false;
        inTempMenu = false;
        inMaintenanceMenu = false;
        inFaultReportMenu = false;
        inOperatorMenu = true;  // Return to Operator Menu (or MENU_SCREEN)
        lastDebounce = now;
        lcd.clear();
      }
      lastManualState = back;


      // Display header
      lcd.setCursor(0, 0);
      lcd.print(" MAINTENANCE ");

      // Display maintenance items
      for (int i = 0; i < VISIBLE; i++) {
        int idx = startIdx + i;
        lcd.setCursor(-4, i + 1);
        lcd.print(idx == maintenanceIndex ? ">" : " ");

        switch (idx) {
          case 0:
            lcd.print("Oil Filt : ");
            lcd.print(changeOilFilter ? "Yes" : "No ");
            break;
          case 1:
            lcd.print("Oil Chng : ");
            lcd.print(changeOil ? "Yes" : "No ");
            break;
          case 2:
            lcd.print("Air Filt : ");
            lcd.print(changeAirFilter ? "Yes" : "No ");
            break;
          case 3:
            lcd.print("Regrease : ");
            lcd.print(regreaseNeeded ? "Yes" : "No ");
            break;
          case 4:
            lcd.print("Oil Sep  : ");
            lcd.print(changeOilSeparator ? "Yes" : "No ");
            break;
          case 5:
            lcd.print("Valve Kit: ");
            lcd.print(changeValveKit ? "Yes" : "No ");
            break;
        }
      }

      return;
    }


    if (inFaultReportMenu) {
      static const int VISIBLE = 2;
      static int startIdx = 0;
      static int faultIndex = 0;

      static bool lastNextState = HIGH;
      static bool lastBackState = HIGH;
      static bool needsRedraw = true;

      // Temporary arrays for active faults
      const char* activeFaults[10];  // enough to hold all possible faults
      int activeCount = 0;

      // Build active fault list manually
      if (emergency) activeFaults[activeCount++] = "Emergency";
      if (revRotation) activeFaults[activeCount++] = "Rev Rotation";
      if (highSumpPres) activeFaults[activeCount++] = "High sump pres";
      if (mainMotorOverload) activeFaults[activeCount++] = "Main motor overload";
      if (fanMotorOverload) activeFaults[activeCount++] = "Fan motor overload";

      unsigned long now = millis();

      // Handle back button
      bool back = digitalRead(backPin);
      if (back == LOW && lastBackState == HIGH && now - lastDebounce > DEBOUNCE) {
        inFaultReportMenu = false;
        inOperatorMenu = true;
        lastDebounce = now;
        needsRedraw = true;
        lcd.clear();
        lastBackState = back;
        return;
      }
      lastBackState = back;

      // Handle next button
      bool nxt = digitalRead(nextPin);
      if (nxt == LOW && lastNextState == HIGH && now - lastDebounce > DEBOUNCE) {
        if (activeCount > 0) {
          faultIndex = (faultIndex + 1) % activeCount;
          if (faultIndex < startIdx) startIdx = faultIndex;
          if (faultIndex >= startIdx + VISIBLE) startIdx = faultIndex - VISIBLE + 1;
        }
        needsRedraw = true;
        lastDebounce = now;
      }
      lastNextState = nxt;

      // Redraw screen
      if (needsRedraw) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(" FAULT REPORT");

        if (activeCount == 0) {
          lcd.setCursor(0, 1);
          lcd.print("No Faults Found");
        } else {
          for (int i = 0; i < VISIBLE; i++) {
            int idx = startIdx + i;
            if (idx >= activeCount) break;
            lcd.setCursor(0, i + 1);
            lcd.print(idx == faultIndex ? ">" : " ");
            lcd.print(activeFaults[idx]);
          }
        }
        needsRedraw = false;
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
        } else if (operatorIndex == 1) {
          inPressureMenu = true;
        } else if (operatorIndex == 2) {
          inTempMenu = true;
        } else if (operatorIndex == 3) {
          inMaintenanceMenu = true;
        } else if (operatorIndex == 4) {
          inFaultReportMenu = true;
        } else {
          inOperatorMenu = false;
        }
        lastDebounce = now;
        lcd.clear();
      }
      lastSelectState = sel;

      bool back = digitalRead(backPin);
      if (back == LOW && lastManualState == HIGH && (now - lastDebounce > DEBOUNCE)) {
        inMachineMenu = false;
        inPressureMenu = false;
        inTempMenu = false;
        inMaintenanceMenu = false;
        inFaultReportMenu = false;
        inOperatorMenu = false;
        currentScreen == MENU_SCREEN;
        lcd.clear();
        lastDebounce = now;
      }
      lastManualState = back;


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

    if (inServiceItem1) {
      static const int VISIBLE = 4;
      static int serviceIndex = 0;
      static int serviceStart = 0;

      bool sel = digitalRead(selectPin);
      bool nxt = digitalRead(nextPin);
      bool back = digitalRead(backPin);
      unsigned long now = millis();

      // Next button to scroll
      if (nxt == LOW && lastNextState == HIGH && now - lastDebounce > DEBOUNCE) {
        serviceIndex = (serviceIndex + 1) % 4;
        if (serviceIndex < serviceStart) serviceStart = serviceIndex;
        if (serviceIndex >= serviceStart + VISIBLE) serviceStart = serviceIndex - VISIBLE + 1;
        lastDebounce = now;
      }
      lastNextState = nxt;

      // Back to Service Menu
      if (back == LOW && lastManualState == HIGH && now - lastDebounce > DEBOUNCE) {
        inServiceItem1 = false;
        inServiceMenu = true;
        lcd.clear();
        lastDebounce = now;
      }
      lastManualState = back;

      // Simulated values (replace with real sensor/EEPROM values)
      int stSpHrs = 0;
      int runTime = 0;
      int loadTime = 0;
      int unloadTime = 0;

      char buffer[6];  // for 0000 + h + null terminator

      for (int i = 0; i < VISIBLE; i++) {
        int idx = serviceStart + i;
        lcd.setCursor(-4, i + 0);
        lcd.print(idx == serviceIndex ? ">" : " ");
        switch (idx) {
          case 0:
            lcd.print("St/Sp Hrs: ");
            sprintf(buffer, "%04d", stSpHrs);
            lcd.print(buffer);
            break;
          case 1:
            lcd.print("Runtime  : ");
            sprintf(buffer, "%04d", runTime);
            lcd.print(buffer);
            break;
          case 2:
            lcd.print("Load     : ");
            sprintf(buffer, "%04d", loadTime);
            lcd.print(buffer);
            break;
          case 3:
            lcd.print("Unload   : ");
            sprintf(buffer, "%04d", unloadTime);
            lcd.print(buffer);
            break;
        }
      }

      return;
    }


    if (inServiceItem2) {
      static const int VISIBLE = 4;
      static int serviceIndex = 0;
      static int serviceStart = 0;
      static bool valueChanged = false;
      bool adjusting = false;
      int startIdx = 0;

      // Adjustable values
      static int srDelay = 0;
      static int rlDelay = 0;
      static int warnDelay = 0;
      static int stopDelay = 0;

      const int SERVICE_OPTIONS = 4;

      // Read buttons
      bool sel = digitalRead(selectPin);
      bool nxt = digitalRead(nextPin);
      unsigned long now = millis();

      // Next button: navigate
      if (nxt == LOW && lastNextState == HIGH && now - lastDebounce > DEBOUNCE) {
        serviceIndex = (serviceIndex + 1) % 4;
        if (serviceIndex < serviceStart) serviceStart = serviceIndex;
        if (serviceIndex >= serviceStart + VISIBLE) serviceStart = serviceIndex - VISIBLE + 1;
        lastDebounce = now;
      }
      lastNextState = nxt;

      // Select button: toggle adjust mode
      if (sel == LOW && lastSelectState == HIGH && now - lastDebounce > DEBOUNCE) {
        adjusting = !adjusting;
        lastDebounce = now;
      }
      lastSelectState = sel;

      // Back button: exit submenu
      bool back = digitalRead(backPin);
      if (back == LOW && lastManualState == HIGH && now - lastDebounce > DEBOUNCE) {
        inServiceItem2 = false;
        currentScreen = MENU_SCREEN;
        lcd.clear();
        lastDebounce = now;
      }
      lastManualState = back;

      // If adjusting, read pot and update selected value
      if (adjusting) {
        int raw = analogRead(potPin);
        int newValue = map(raw, 0, 1023, 0, 999);  // value range: 0–999 seconds

        switch (serviceIndex) {
          case 0:
            if (srDelay != newValue) {
              srDelay = newValue;
              valueChanged = true;
            }
            break;
          case 1:
            if (rlDelay != newValue) {
              rlDelay = newValue;
              valueChanged = true;
            }
            break;
          case 2:
            if (warnDelay != newValue) {
              warnDelay = newValue;
              valueChanged = true;
            }
            break;
          case 3:
            if (stopDelay != newValue) {
              stopDelay = newValue;
              valueChanged = true;
            }
            break;
        }
      }

      // Save to EEPROM if value changed
      if (sel == HIGH && lastSelectState == LOW && adjusting && valueChanged) {
        EEPROM.put(100, srDelay);
        EEPROM.put(104, rlDelay);
        EEPROM.put(108, warnDelay);
        EEPROM.put(112, stopDelay);
        adjusting = false;
        valueChanged = false;
        lastDebounce = now;
      }

      // Display on LCD
      for (int i = 0; i < VISIBLE; i++) {
        int idx = startIdx + i;
        lcd.setCursor(-4, i + 0);
        lcd.print(idx == serviceIndex ? ">" : " ");
        switch (idx) {
          case 0:
            lcd.print("SR delay(s):");
            lcd.print(srDelay);
            break;
          case 1:
            lcd.print("RL delay(s):");
            lcd.print(rlDelay);
            break;
          case 2:
            lcd.print("Warn(s)    :");
            lcd.print(warnDelay);
            break;
          case 3:
            lcd.print("Stop(s)    :");
            lcd.print(stopDelay);
            break;
        }
      }

      return;
    }

    if (inServiceItem4) {
      static int serviceIndex = 0;
      static int serviceStart = 0;
      static const int VISIBLE = 4;
      const int SERVICE_COUNT = 6;

      unsigned long now = millis();
      unsigned long runTimeMillis = now;  // since boot
      float runTimeHours = runTimeMillis / 3600000.0;

      // Read navigation buttons
      bool sel = digitalRead(selectPin);
      bool nxt = digitalRead(nextPin);

      // Navigation with next button
      if (nxt == LOW && lastNextState == HIGH && now - lastDebounce > DEBOUNCE) {
        serviceIndex = (serviceIndex + 1) % SERVICE_COUNT;
        if (serviceIndex < serviceStart) serviceStart = serviceIndex;
        if (serviceIndex >= serviceStart + VISIBLE) serviceStart = serviceIndex - VISIBLE + 1;
        lastDebounce = now;
      }
      lastNextState = nxt;

      // Back button to return to menu
      bool back = digitalRead(backPin);
      if (back == LOW && lastManualState == HIGH && now - lastDebounce > DEBOUNCE) {
        inServiceItem4 = false;
        currentScreen = MENU_SCREEN;
        lcd.clear();
        lastDebounce = now;
      }
      lastManualState = back;

      // Only refresh display every 500ms to reduce flicker
      static unsigned long lastDisplayUpdate = 0;
      if (now - lastDisplayUpdate > 500) {
        lastDisplayUpdate = now;

        for (int i = 0; i < VISIBLE; i++) {
          int idx = serviceStart + i;
          if (idx >= SERVICE_COUNT) break;

          lcd.setCursor(-4, i);
          lcd.print(idx == serviceIndex ? ">" : " ");

          switch (idx) {
            case 0: lcd.print("Oil Filt :"); break;
            case 1: lcd.print("Air Filt :"); break;
            case 2: lcd.print("Oil Septr:"); break;
            case 3: lcd.print("Oil Chnge:"); break;
            case 4: lcd.print("Regrease :"); break;
            case 5: lcd.print("Valve Kit:"); break;
          }

          // Format and print hours in 0000h format
          char buffer[6];  // 4 digits + 'h' + null terminator
          int hours = (int)runTimeHours;
          sprintf(buffer, "%04d", hours);
          lcd.print(buffer);
          lcd.print("   ");  // to clear any leftover chars
        }
      }

      return;
    }


    // Service submenu
    if (inServiceMenu) {
      bool nxt = digitalRead(nextPin);
      if (nxt == LOW && lastNextState == HIGH && now - lastDebounce > DEBOUNCE) {
        serviceIndex = (serviceIndex + 1) % SR_COUNT;
        if (serviceIndex < srStart) srStart = serviceIndex;
        if (serviceIndex >= srStart + SR_VISIBLE) srStart = serviceIndex - SR_VISIBLE + 1;
        lastDebounce = now;
      }
      lastNextState = nxt;

      bool sel = digitalRead(selectPin);
      if (sel == LOW && lastSelectState == HIGH && now - lastDebounce > DEBOUNCE) {
        if (serviceIndex == 0) {
          inServiceItem1 = true;
        } else if (serviceIndex == 1) {
          inServiceItem2 = true;
        } else if (serviceIndex == 2) {
          inServiceItem3 = true;
        } else if (serviceIndex == 3) {
          inServiceItem4 = true;
        } else {
          inServiceMenu = false;
        }
        lastDebounce = now;
        lcd.clear();
      }
      lastSelectState = sel;

      // Back to Service Menu
      bool back = digitalRead(backPin);
      if (back == LOW && lastManualState == HIGH && (now - lastDebounce > DEBOUNCE)) {
        inServiceItem1 = false;
        inServiceItem2 = false;
        inServiceItem3 = false;
        inServiceItem4 = false;
        inServiceMenu = false;
        currentScreen == MENU_SCREEN;
        lcd.clear();
        lastDebounce = now;
      }
      lastManualState = back;

      lcd.setCursor(0, 0);
      lcd.print("  SERVICE     ");
      for (int i = 0; i < SR_VISIBLE; i++) {
        int idx = srStart + i;
        lcd.setCursor(-4, 1 + i);
        lcd.print(idx == serviceIndex ? ">" : " ");
        lcd.print(serviceItems[idx]);
      }
      return;
    }

    if (inAdminItem1) {
      const int OPTION_COUNT = 4;
      const int VISIBLE_LINES = 4;

      static int optionIndex = 0;
      static int optionStart = 0;
      static bool adjusting = false;
      static bool valueChanged = false;

      bool nxt = digitalRead(nextPin);
      unsigned long now = millis();

      // Handle NEXT button for scrolling and selection
      if (nxt == LOW && lastNextState == HIGH && now - lastDebounce > DEBOUNCE) {
        optionIndex = (optionIndex + 1) % OPTION_COUNT;

        // Scroll if needed
        if (optionIndex >= optionStart + VISIBLE_LINES)
          optionStart = optionIndex - VISIBLE_LINES + 1;
        else if (optionIndex < optionStart)
          optionStart = optionIndex;

        lastDebounce = now;
      }
      lastNextState = nxt;

      // Handle SELECT button
      bool sel = digitalRead(selectPin);
      if (sel == LOW && lastSelectState == HIGH && now - lastDebounce > DEBOUNCE) {
        adjusting = !adjusting;
        lastDebounce = now;
      }
      lastSelectState = sel;

      // Handle BACK button
      bool back = digitalRead(backPin);
      if (back == LOW && lastManualState == HIGH && now - lastDebounce > DEBOUNCE) {
        inAdminItem1 = false;
        currentScreen = MENU_SCREEN;
        lcd.clear();
        lastDebounce = now;
      }
      lastManualState = back;

      // Adjust values using potentiometer
      if (adjusting) {
        int raw = analogRead(potPin);
        switch (optionIndex) {
          case 0:
            {
              float newVal = map(raw, 0, 1023, 0, 2000) / 10.0;
              if (ulPres != newVal) {
                ulPres = newVal;
                valueChanged = true;
              }
              break;
            }
          case 1:
            {
              float newVal = map(raw, 0, 1023, 0, 2000) / 10.0;
              if (lPres != newVal) {
                lPres = newVal;
                valueChanged = true;
              }
              break;
            }
          case 2:
            {
              int newVal = map(raw, 0, 1023, 0, 150);
              if (tempTrip != newVal) {
                tempTrip = newVal;
                valueChanged = true;
              }
              break;
            }
          case 3:
            {
              int newVal = map(raw, 0, 1023, 0, 150);
              if (tempWarn != newVal) {
                tempWarn = newVal;
                valueChanged = true;
              }
              break;
            }
        }
      }

      // Display options (4 lines)
      for (int i = 0; i < VISIBLE_LINES; i++) {
        int idx = optionStart + i;
        lcd.setCursor(-4, i);
        lcd.print(idx == optionIndex ? ">" : " ");
        switch (idx) {
          case 0:
            lcd.print("UL pres: ");
            lcd.print(ulPres, 1);
            lcd.print(" B");
            break;
          case 1:
            lcd.print("L pres : ");
            lcd.print(lPres, 1);
            lcd.print(" B");
            break;
          case 2:
            lcd.print("Trip   : ");
            lcd.print(tempTrip);
            lcd.print(" C");
            break;
          case 3:
            lcd.print("Warn   : ");
            lcd.print(tempWarn);
            lcd.print(" C");
            break;
        }
      }

      // Save to EEPROM when adjusting stops
      if (sel == HIGH && lastSelectState == LOW && adjusting && valueChanged) {
        EEPROM.put(200, ulPres);
        EEPROM.put(204, lPres);
        EEPROM.put(208, tempTrip);
        EEPROM.put(212, tempWarn);
        adjusting = false;
        valueChanged = false;
        lastDebounce = now;
      }

      return;
    }

    if (inAdminItem2) {
      const int OPTION_COUNT = 3;
      const int VISIBLE_LINES = 3;

      static int optionIndex = 0;
      static int optionStart = 0;
      static bool adjusting = false;
      static bool valueChanged = false;

      bool nxt = digitalRead(nextPin);
      unsigned long now = millis();

      // Handle NEXT button
      if (nxt == LOW && lastNextState == HIGH && now - lastDebounce > DEBOUNCE) {
        optionIndex = (optionIndex + 1) % OPTION_COUNT;

        if (optionIndex >= optionStart + VISIBLE_LINES)
          optionStart = optionIndex - VISIBLE_LINES + 1;
        else if (optionIndex < optionStart)
          optionStart = optionIndex;

        lastDebounce = now;
      }
      lastNextState = nxt;

      // Handle SELECT button
      bool sel = digitalRead(selectPin);
      if (sel == LOW && lastSelectState == HIGH && now - lastDebounce > DEBOUNCE) {
        adjusting = !adjusting;
        lastDebounce = now;
      }
      lastSelectState = sel;

      // Handle BACK button
      bool back = digitalRead(backPin);
      if (back == LOW && lastManualState == HIGH && now - lastDebounce > DEBOUNCE) {
        inAdminItem2 = false;
        currentScreen = MENU_SCREEN;
        lcd.clear();
        lastDebounce = now;
      }
      lastManualState = back;

      // Adjust values with potentiometer
      if (adjusting) {
        int raw = analogRead(potPin);
        float newVal = map(raw, 0, 1023, 0, 10000) / 10.0;  // 0000.0 to 1000.0

        switch (optionIndex) {
          case 0:
            if (rlValue != newVal) {
              rlValue = newVal;
              valueChanged = true;
            }
            break;
          case 1:
            if (ulValue != newVal) {
              ulValue = newVal;
              valueChanged = true;
            }
            break;
          case 2:
            if (lValue != newVal) {
              lValue = newVal;
              valueChanged = true;
            }
            break;
        }
      }

      // Display 3 options
      for (int i = 0; i < VISIBLE_LINES; i++) {
        int idx = optionStart + i;
        lcd.setCursor(-4, i);
        lcd.print(idx == optionIndex ? ">" : " ");
        switch (idx) {
          case 0:
            lcd.print("RL : ");
            lcd.print(rlValue, 1);
            break;
          case 1:
            lcd.print("UL : ");
            lcd.print(ulValue, 1);
            break;
          case 2:
            lcd.print("L  : ");
            lcd.print(lValue, 1);
            break;
        }
      }

      // Save to EEPROM
      if (sel == HIGH && lastSelectState == LOW && adjusting && valueChanged) {
        EEPROM.put(220, rlValue);
        EEPROM.put(224, ulValue);
        EEPROM.put(228, lValue);
        adjusting = false;
        valueChanged = false;
        lastDebounce = now;
      }

      return;
    }




    // Admin submenu
    if (inAdminMenu) {
      bool nxt = digitalRead(nextPin);
      if (nxt == LOW && lastNextState == HIGH && now - lastDebounce > DEBOUNCE) {
        adminIndex = (adminIndex + 1) % AD_COUNT;
        if (adminIndex < adStart) adStart = adminIndex;
        if (adminIndex >= adStart + AD_VISIBLE) adStart = adminIndex - AD_VISIBLE + 1;
        lastDebounce = now;
      }
      lastNextState = nxt;

      bool sel = digitalRead(selectPin);
      if (sel == LOW && lastSelectState == HIGH && now - lastDebounce > DEBOUNCE) {
        if (adminIndex == 0) {
          inAdminItem1 = true;  // Add your action here
        } else if (adminIndex == 1) {
          inAdminItem2 = true;  // Add your action here
        } else {
          inAdminMenu = false;
        }
        lastDebounce = now;
        lcd.clear();
      }
      lastSelectState = sel;

      bool back = digitalRead(backPin);
      if (back == LOW && lastManualState == HIGH && (now - lastDebounce > DEBOUNCE)) {
        inAdminItem1 = false;
        inAdminItem2 = false;
        inAdminMenu = false;
        currentScreen == MENU_SCREEN;
        lcd.clear();
        lastDebounce = now;
      }
      lastManualState = back;

      lcd.setCursor(0, 0);
      lcd.print("    ADMIN      ");
      for (int i = 0; i < AD_VISIBLE; i++) {
        int idx = adStart + i;
        lcd.setCursor(-4, 1 + i);
        lcd.print(idx == adminIndex ? ">" : " ");
        lcd.print(adminItems[idx]);
      }
      lcd.setCursor(-4, 3);
      lcd.print("                 ");
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
      } else if (menuIndex == 1) {
        inServiceMenu = true;
        serviceIndex = srStart = 0;
      } else if (menuIndex == 2) {
        inAdminMenu = true;
        adminIndex = adStart = 0;
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


  // Draw Home screen
  lcd.setCursor(0, 0);
  lcd.print("Dis.Prs:");

  int rawValue = analogRead(pressureSensorPin);                    //  Read analog value from the pressure sensor pin
  float voltage = (rawValue / 1023.0) * 5.0;                       // Convert raw analog value to voltage (assuming 10-bit ADC and 5V reference)
  float current_mA = (voltage / 5.0) * 20.0;                       // convert voltage to current (mA)
  current_mA = constrain(current_mA, 4.0, 20.0);                   // clamp current between 4 and 20 mA
  float pressureBar = ((current_mA - 4.0) * 10.0) / (20.0 - 4.0);  // Calculate pressure in Bar (example: 0 Bar = 4 mA, 10 Bar = 20 mA)
  float pressurePSI = pressureBar * 14.5038;                       // Convert pressure to PSI if needed (1 Bar = 14.5038 PSI)
  // Display pressure value with one decimal place and units
  if (machineSettings.pressureBar) {
    lcd.print(pressureBar, 1);  // show 1 decimal place
    lcd.print(" Bar ");
  } else {
    lcd.print(pressurePSI, 1);
    lcd.print(" PSI ");
  }

  int rawValueTemp = analogRead(tempSensorPin);
  float voltageTemp = (rawValueTemp / 4095.0) * 3.3;
  float R_REF = 1000.0;
  float Vsupply = 3.3;
  if (voltageTemp >= Vsupply) voltageTemp = Vsupply - 0.01;
  float R_sensor = R_REF * (voltageTemp / (Vsupply - voltageTemp));
  float tempC = resistanceToTempC(R_sensor);
  float tempF = tempC * 9.0 / 5.0 + 32.0;
  // Clamp to zero as lowest range
  if (tempC < 0) tempC = 0;
  if (tempF < 32) tempF = 32;

  lcd.setCursor(0, 1);
  lcd.print("Dis.Temp:");
  if (machineSettings.tempCelsius) {
    lcd.print(tempC, 1);
    lcd.print(" C");
  } else {
    lcd.print(tempF, 1);
    lcd.print(" F");
  }

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
  // updateMessage1();
  lcd.print(machineSettings.mode ? "L" : "R");

  lcd.setCursor(8, 3);
  // updateMessage2();
  lcd.print(machineSettings.autoStart ? "A" : "-");

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
  if (warningTemp) {
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
    currentWarning = "-";  // No warning
  }
}

void updateFaultMessage() {
  if (warningTemp) {
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
    currentFault = "-";  // No warning
  }
}

void updateStatusMessage() {
  if (inFaultReportMenu && hasActiveFaults()) {
    currentStatus = "Fault";
  } else if (inFaultReportMenu && !hasActiveFaults()) {
    currentStatus = "Ready";
  } else if (warningTemp) {
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
  } else {
    currentStatus = "Ready";
  }
}
// void updateMessage1() {
//   if (local) {
//     currentUpdate1 = "L";
//   } else if (remote) {
//     currentUpdate1 = "R";
//   } else {
//     currentUpdate1 = "-";
//   }
// }

// void updateMessage2() {
//   if (autoStart) {
//     currentUpdate2 = "A";
//   } else if (unloadMode) {
//     currentUpdate2 = "UL";
//   }  else {
//     currentUpdate2 = "-";
//   }
// }

void saveMachineSettings() {
  EEPROM.write(0, machineSettings.mode ? 1 : 0);
  EEPROM.write(1, machineSettings.autoStart ? 1 : 0);
  EEPROM.write(2, machineSettings.pressureBar ? 1 : 0);
  EEPROM.write(3, machineSettings.tempCelsius ? 1 : 0);
  // NO EEPROM.commit() here
}

void loadMachineSettings() {
  machineSettings.mode = EEPROM.read(0) == 1;
  machineSettings.autoStart = EEPROM.read(1) == 1;
  machineSettings.pressureBar = EEPROM.read(2) == 1;
  machineSettings.tempCelsius = EEPROM.read(3) == 1;
}

void savePressureSettings() {
  EEPROM.put(0x30, loadPressure);
  EEPROM.put(0x34, reloadPressure);
  EEPROM.put(0x38, hspPressure);
}