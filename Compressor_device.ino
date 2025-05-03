#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 4); // 16×4 LCD

// Pin definitions
const int pressureSensorPin = PA8;
const int setpointPin       = PA0;
const int compressorPin     = PA1;
const int alarmPin          = PB14;
const int tempFaultPin      = PA9;
const int overloadFaultPin  = PA10;
const int manualStopPin     = PA5;
const int shutdownPin       = PA6;
const int resetPin          = PA7;
const int togglePin         = PA2;  // Home/Menu toggle
const int nextPin           = PA3;  // Next in menu
const int selectPin         = PA4;  // Select in menu

// Screen states
enum ScreenState { MAIN_SCREEN, MENU_SCREEN };
ScreenState currentScreen = MAIN_SCREEN;
bool inOperatorMenu = false;  // track nested operator submenu

// Top-level menu data
const int MENU_COUNT = 3;
const char* menuItems[MENU_COUNT] = {
  "1.Operator",
  "2.Service ",
  "3.Admin   "
};
int menuIndex = 0;

// Operator submenu data
const int OP_COUNT = 5;
const char* operatorItems[OP_COUNT] = {
  "1.Machine",
  "2.Pressure",
  "3.Temp    ",
  "4.Maintaince",
  "5.FaultRepair"
};
int operatorIndex = 0;
int opStart = 0;
const int OP_VISIBLE = 3;

// Status flags
bool compressorStatus = false;
bool faultDetected    = false;
bool manuallyStopped  = false;
bool systemShutdown   = false;

unsigned long faultTime      = 0;
unsigned long lastDebounce   = 0;
const unsigned long DEBOUNCE = 50;

// Last button states
bool lastShutdownState = HIGH;
bool lastResetState    = HIGH;
bool lastToggleState   = HIGH;
bool lastNextState     = HIGH;
bool lastSelectState   = HIGH;
bool lastManualState   = HIGH;

void setup() {
  Wire.begin();
  lcd.init();
  lcd.backlight();

  pinMode(pressureSensorPin, INPUT);
  pinMode(setpointPin,       INPUT);
  pinMode(tempFaultPin,      INPUT_PULLUP);
  pinMode(overloadFaultPin,  INPUT_PULLUP);
  pinMode(manualStopPin,     INPUT_PULLUP);
  pinMode(shutdownPin,       INPUT_PULLUP);
  pinMode(resetPin,          INPUT_PULLUP);
  pinMode(togglePin,         INPUT_PULLUP);
  pinMode(nextPin,           INPUT_PULLUP);
  pinMode(selectPin,         INPUT_PULLUP);

  pinMode(compressorPin, OUTPUT);
  pinMode(alarmPin,      OUTPUT);
  digitalWrite(compressorPin, LOW);
  digitalWrite(alarmPin,      LOW);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Initializing...");
  delay(2000);
  lcd.clear();
}

void loop() {
  unsigned long now = millis();

  // Shutdown toggle
  bool sdt = digitalRead(shutdownPin);
  if (sdt==LOW && lastShutdownState==HIGH && now-lastDebounce>DEBOUNCE) {
    systemShutdown = !systemShutdown;
    lastDebounce = now;
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print(systemShutdown?"System Shutdown":"System Restarted");
    delay(1000);
    lcd.clear();
  }
  lastShutdownState = sdt;

  // Reset
  bool rst = digitalRead(resetPin);
  if (rst==LOW && lastResetState==HIGH && now-lastDebounce>DEBOUNCE) {
    systemShutdown = false;
    manuallyStopped = false;
    stopCompressor();
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("System Reset...");
    delay(1000);
    lcd.clear();
  }
  lastResetState = rst;

  if (systemShutdown) return;

  // Manual STOP toggle
  bool m = digitalRead(manualStopPin);
  if (m==LOW && lastManualState==HIGH && now-lastDebounce>DEBOUNCE) {
    manuallyStopped = !manuallyStopped;
    if (manuallyStopped) stopCompressor();
    lastDebounce = now;
  }
  lastManualState = m;

  // Home/Menu toggle
  bool tog = digitalRead(togglePin);
  if (tog==LOW && lastToggleState==HIGH && now-lastDebounce>DEBOUNCE) {
    if (currentScreen==MAIN_SCREEN) {
      currentScreen = MENU_SCREEN;
      inOperatorMenu = false;
    } else {
      currentScreen = MAIN_SCREEN;
    }
    lastDebounce = now;
    lcd.clear();
  }
  lastToggleState = tog;

  // MENU_SCREEN
  if (currentScreen==MENU_SCREEN) {
    // If inside operator submenu
    if (inOperatorMenu) {
      bool nxt = digitalRead(nextPin);
      if (nxt==LOW && lastNextState==HIGH && now-lastDebounce>DEBOUNCE) {
        operatorIndex = (operatorIndex+1)%OP_COUNT;
        if (operatorIndex < opStart) opStart = operatorIndex;
        if (operatorIndex >= opStart+OP_VISIBLE) opStart = operatorIndex-OP_VISIBLE+1;
        lastDebounce = now;
      }
      lastNextState = nxt;

      bool sel = digitalRead(selectPin);
      if (sel==LOW && lastSelectState==HIGH && now-lastDebounce>DEBOUNCE) {
        inOperatorMenu = false;
        lastDebounce = now;
        lcd.clear();
      }
      lastSelectState = sel;

      lcd.setCursor(0,0);  lcd.print("  OPERATOR  ");
      for (int i=0; i<OP_VISIBLE; i++) {
        int idx = opStart + i;
        lcd.setCursor(0,1+i);
        lcd.print(idx==operatorIndex?">":" ");
        lcd.print(operatorItems[idx]);
      }
      return;
    }

    // Top-level menu navigation
    bool nxt = digitalRead(nextPin);
    if (nxt==LOW && lastNextState==HIGH && now-lastDebounce>DEBOUNCE) {
      menuIndex = (menuIndex+1)%MENU_COUNT;
      lastDebounce = now;
    }
    lastNextState = nxt;

    bool sel = digitalRead(selectPin);
    if (sel==LOW && lastSelectState==HIGH && now-lastDebounce>DEBOUNCE) {
      if (menuIndex==0) {
        inOperatorMenu = true;
        operatorIndex = opStart = 0;
      } else {
        currentScreen = MAIN_SCREEN;
      }
      lastDebounce = now;
      lcd.clear();
    }
    lastSelectState = sel;

    lcd.setCursor(0,0);  lcd.print("     MENU       ");
    for (int i=0; i<MENU_COUNT; i++) {
      lcd.setCursor(0,1+i);
      lcd.print(i==menuIndex?">":" ");
      lcd.print(menuItems[i]);
    }
    return;
  }

  // MAIN_SCREEN logic
  int pressureValue = analogRead(pressureSensorPin);
  bool tFault = digitalRead(tempFaultPin)==LOW;
  bool oFault = digitalRead(overloadFaultPin)==LOW;

  faultDetected = tFault||oFault;
  if (faultDetected) {
    stopCompressor();
    digitalWrite(alarmPin, HIGH);
    if (faultTime==0) faultTime=now;
    if (now-faultTime>=5000) {
      faultTime=0;
      restartCompressor();
    }
  } else {
    digitalWrite(alarmPin, LOW);
    if (!manuallyStopped) {
      if (compressorStatus) {
        if (pressureValue>=analogRead(setpointPin)) stopCompressor();
      } else {
        if (pressureValue<analogRead(setpointPin)) restartCompressor();
      }
    }
  }

  // Draw Home screen
  lcd.setCursor(0,0);
  lcd.print("Dis.Pres : 00.0 Bar");

  lcd.setCursor(0,1);
  lcd.print("Dis Temp : 0 C   ");

  lcd.setCursor(0,2);
  lcd.print("Warning  : ");
  lcd.print(faultDetected?"Fault   ":"No Fault");

  lcd.setCursor(0,3);
  lcd.print("Status   : LA   ");

  delay(300);
}

void stopCompressor(){ digitalWrite(compressorPin,LOW); compressorStatus=false; }
void restartCompressor(){ digitalWrite(compressorPin,HIGH); compressorStatus=true; }
