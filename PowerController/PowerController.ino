#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <StateMachine.h>

#define SPARE               2     // Currently unused.
#include <neotimer.h>
#define SOUND_BOMB          3     // The external burglar alarm
#define LIGHTING_ENABLE     4     // Emergency lighting.
#define SPARE_PWM           5     // Currently unused.
#define BUZZER_ENABLE       6     // The small buzzer.
#define SOLENOID_ENABLE     7     // The solenoid enable line.
#define INPUT_ACTIVE        8     // The start button
#define LED_ACTIVE          9     // Indicates if the Arduino power is now latched.
#define LED_TIMEOUT         10    // Indicates if the unit is in timeout mode and will eventually shutdown if no interaction occurs.
#define LED_OVERRIDE        11    // Indicates if override is active.
#define INPUT_OVERRIDE      12    // If grounded (via the keyswitch) the startup and under/over voltage checks are ignored.
#define ARDUINO_ENABLE      13    // Keeps the Arduino running.  If this pin goes low the Arduino will power off.

#define INPUT_ALARM_DISABLE A0    // Button to disable the alarm
#define INPUT_BATTERY_SENSE A1    // Measures the voltage of the battery.
#define INPUT_BUS_SENSE     A2    // Measures the voltage of the bus (controlled by the solenoid)
#define INPUT_STOP_BUTTON   A3    // The stop button.

#define BUZZER_SUPPORTED          // If uncommented, the buzzer will sound during events such as timeout.  Disabled during development.
#define DISPLAY_SUPPORTED         // If uncommented, the buzzer will sound during events such as timeout.  Disabled during development.

#define NUM_SAMPLES 3             // number of analog samples to take per reading (voltage measurement)

float noSolenoidVoltageLevel = 6;

float batteryVoltage = 12;
unsigned int batterySum = 0;
unsigned int batterySampleCount = 0;
float batteryVoltageMax = 14.5;
float batteryVoltageMin = 11.0;

float busVoltage = 12;
int busSum = 0;
unsigned int busSampleCount = 0;
float busVoltageMax = 14.5;
float busVoltageMin = 11.0;

Neotimer splashTimer = Neotimer(2000); // 2s
Neotimer startupTimer = Neotimer(30000); // 30s
Neotimer shutdownTimer = Neotimer(300000); // 5 minutes
Neotimer alarmTimer = Neotimer(30000); // 30s
Neotimer alarmAlarmTimer = Neotimer(900000); // 15 minutes

#ifdef DISPLAY_SUPPORTED
  Adafruit_SSD1306 display(128, 32, &Wire, -1);
#endif

StateMachine machine = StateMachine();

State* S0 = machine.addState(&state0);  // Start up
State* S1 = machine.addState(&state1);  // Alarm timeout
State* S2 = machine.addState(&state2);  // Alarm active
State* S3 = machine.addState(&state3);  // Normal timeout
State* S4 = machine.addState(&state4);  // Active
State* S5 = machine.addState(&state5);  // Under voltage
State* S6 = machine.addState(&state6);  // Over voltage
State* S7 = machine.addState(&state7);  // Emergency
State* S8 = machine.addState(&state8);  // Shutdown timeout
State* S9 = machine.addState(&state9);  // Qrt

void setup()   {
  Serial.begin(115200);
  Serial.println("Starting...");

  pinMode(INPUT_STOP_BUTTON, INPUT);
  pinMode(INPUT_ACTIVE, INPUT_PULLUP);
  pinMode(INPUT_OVERRIDE, INPUT_PULLUP);

  pinMode(SOUND_BOMB, OUTPUT);      digitalWrite(SOUND_BOMB, LOW);
  pinMode(LIGHTING_ENABLE, OUTPUT); digitalWrite(LIGHTING_ENABLE, HIGH);
  pinMode(BUZZER_ENABLE, OUTPUT);   digitalWrite(BUZZER_ENABLE, LOW);
  pinMode(SOLENOID_ENABLE, OUTPUT); digitalWrite(SOLENOID_ENABLE, LOW);
  pinMode(ARDUINO_ENABLE, OUTPUT);  digitalWrite(ARDUINO_ENABLE, HIGH);
  pinMode(SPARE_PWM, OUTPUT);  digitalWrite(SPARE_PWM, LOW);

  pinMode(LED_ACTIVE, OUTPUT);    digitalWrite(LED_ACTIVE, LOW);
  pinMode(LED_TIMEOUT, OUTPUT);   digitalWrite(LED_TIMEOUT, LOW);
  pinMode(LED_OVERRIDE, OUTPUT);  digitalWrite(LED_OVERRIDE, LOW);

  // Setup the state machine transitions
  S0->addTransition(&transitionS0S1, S1);
  S0->addTransition(&transitionS0S4, S4);
  S1->addTransition(&transitionS1S2, S2);
  S1->addTransition(&transitionS1S3, S3);
  S2->addTransition(&transitionS2S9, S9);
  S3->addTransition(&transitionS3S4, S4);
  S3->addTransition(&transitionS3S9, S9);
  S4->addTransition(&transitionToEmergency, S7);
  S4->addTransition(&transitionS4S5, S5);
  S4->addTransition(&transitionS4S6, S6);
  S4->addTransition(&transitionS4S8, S8);
  S5->addTransition(&transitionS5S4, S4);
  S5->addTransition(&transitionToEmergency, S7);
  S5->addTransition(&transitionToQrt, S9);
  S6->addTransition(&transitionS6S4, S4);
  S6->addTransition(&transitionToEmergency, S7);
  S6->addTransition(&transitionToQrt, S9);
  S7->addTransition(&transitionFromEmergency, S4);
  S7->addTransition(&transitionToQrt, S9);
  S8->addTransition(&transitionS8S4, S4);
  S8->addTransition(&transitionS8S9, S9);
  S9->addTransition(&transitionS9S0, S0);

  // Check if the start button was pressed or the override key is enabled and power up the Arduino.
  if (digitalRead(INPUT_ACTIVE) == LOW || digitalRead(INPUT_OVERRIDE) == LOW)
  {
    digitalWrite(ARDUINO_ENABLE, HIGH);
  }

  if (digitalRead(INPUT_OVERRIDE) == LOW)
  {
    digitalWrite(LED_OVERRIDE, HIGH);
  }

  #ifdef DISPLAY_SUPPORTED
    // Initialise the OLED
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
      Serial.println(F("SSD1306 allocation failed"));
      for(;;); // Don't proceed, loop forever
    }
  #endif

}

// Calibrating the voltage measurement
// Connect a stable power supply, such as a 9V battery across the resistor network. Measure the voltage across both resistors together i.e. measure the battery voltage.
// Now measure the voltage across the 100k resistor (TP5) for battery or (TP2) for bus.
// The voltage divider factor is calculated by dividing the first voltage by the second voltage or dividing factor = input voltage ÷ output voltage
// For example, if the first or input voltage measured is 10.02V and the second or output voltage is 0.9V, then the division factor is:
// 10.02 ÷ 0.9 = 11.133
// Now use this value in the Arduino sketch code.

void loop()
{
  machine.run();
  UpdateBatteryVoltage();
  UpdateBusVoltage();
}

//=======================================

void state0() {
  if (machine.executeOnce) {
    //Serial.println("State 0 - Splash");
    #ifdef DISPLAY_SUPPORTED
      display.clearDisplay();
      oledText("V2.0", 16, 2, 4, true);
    #endif

    tone(BUZZER_ENABLE, 1000);
    delay(100);
    tone(BUZZER_ENABLE, 2000);
    delay(100);
    noTone(BUZZER_ENABLE);

    splashTimer.start();
  }
}

// Alarm timeout
void state1() {
  
  if (machine.executeOnce) {
    //Serial.println("State 1 - Alarm timer initiated");
    alarmTimer.start();
    digitalWrite(LED_TIMEOUT, LOW); // Disable the TIMEOUT led.
    #ifdef DISPLAY_SUPPORTED
      display.clearDisplay();
      oledText("ALARM IN", 15, 0, 2, false);
    #endif
  }

  unsigned long remainingSeconds = (alarmTimer.get() - alarmTimer.getEllapsed()) / 1000;
  int runHours = remainingSeconds / 3600;
  int secsRemaining = remainingSeconds % 3600;
  int runMinutes = secsRemaining / 60;
  int runSeconds = secsRemaining % 60;

  char buf[9];
  sprintf(buf, "%02d:%02d:%02d", runHours, runMinutes, runSeconds);
  #ifdef DISPLAY_SUPPORTED
    display.fillRect(0, 18, 128, 32, BLACK);
    oledText(buf, 15, 18, 2, true);
  #endif
  //Serial.print("ALARM: ");
  //Serial.println(buf);

  #ifdef BUZZER_SUPPORTED
    tone(BUZZER_ENABLE, 1000);
    delay(200);
    noTone(BUZZER_ENABLE);
  #endif
  
}

void state2() {
  if (machine.executeOnce) {
    //Serial.println("State 2 - Alarm Activated");
    alarmAlarmTimer.start();
    digitalWrite(SOUND_BOMB, HIGH);
    digitalWrite(LED_TIMEOUT, LOW); // Disable the TIMEOUT led.
    #ifdef DISPLAY_SUPPORTED
      display.clearDisplay();
      oledText("ALARM", 5, 2, 4, true);
    #endif

    // Trigger a pulse on the 3G tracker SOS button. (1 second)
   digitalWrite(SPARE_PWM, HIGH);
   delay(1000);
   digitalWrite(SPARE_PWM, LOW);
  }
}

void state3() {
  
  if (machine.executeOnce) {
  //Serial.println("State 3 - Normal Timeout");
    digitalWrite(SOUND_BOMB, LOW);
    digitalWrite(LED_TIMEOUT, HIGH); // Enable the TIMEOUT led.
    #ifdef DISPLAY_SUPPORTED
      display.clearDisplay();
      oledText("TIMEOUT IN", 0, 0, 2, false);
    #endif
    startupTimer.start();
 }

  unsigned long remainingSeconds = (startupTimer.get() - startupTimer.getEllapsed()) / 1000;
  int runHours = remainingSeconds / 3600;
  int secsRemaining = remainingSeconds % 3600;
  int runMinutes = secsRemaining / 60;
  int runSeconds = secsRemaining % 60;

  char buf[9];
  sprintf(buf, "%02d:%02d:%02d", runHours, runMinutes, runSeconds);
  #ifdef DISPLAY_SUPPORTED
    display.fillRect(0, 18, 128, 32, BLACK);
    oledText(buf, 15, 18, 2, true);
  #endif
  //Serial.print("TIMEOUT: ");
  //Serial.println(buf);

  #ifdef BUZZER_SUPPORTED
    if (secsRemaining <= 10 && secsRemaining % 2 == 0)
    {
      tone(BUZZER_ENABLE, 1500);
      delay(200);
      noTone(BUZZER_ENABLE);
    }
  #endif

}

void state4() {
  if (machine.executeOnce) {
    //Serial.println("State 4 - Active");
    digitalWrite(SOUND_BOMB, LOW); // Disable the alarm.
    digitalWrite(LIGHTING_ENABLE, LOW); // Disable the emergency lighting.
    digitalWrite(LED_TIMEOUT, LOW); // Disable the TIMEOUT led.
    digitalWrite(LED_ACTIVE, HIGH); // Light the ACTIVE led.
    digitalWrite(SOLENOID_ENABLE, HIGH); // Enable the solenoid.

    #ifdef BUZZER_SUPPORTED
      noTone(BUZZER_ENABLE);
    #endif
  }

  #ifdef DISPLAY_SUPPORTED
    display.clearDisplay();
    char buf[9];
    dtostrf(batteryVoltage, 5, 2, buf);
    strncat(buf, "V", 2);
    oledText(buf, 2, 2, 4, true);
  #endif

  //Serial.print("BUS: ");
  //Serial.print(busVoltage);
  //Serial.print("V   - BATTERY: ");
  //Serial.print(batteryVoltage);
  //Serial.println("V");
  
}

void state5() {

  if (machine.executeOnce) {
    //Serial.println("State 5 - Under Voltage");
    #ifdef BUZZER_SUPPORTED
      tone(BUZZER_ENABLE, 1500);
    #endif
    #ifdef DISPLAY_SUPPORTED
      display.clearDisplay();
      oledText("UNDER", 5, 2, 4, true);
    #endif
    //digitalWrite(SOLENOID_ENABLE, LOW); // Disable the solenoid.
  }

  //Serial.print("Under Voltage - ");
  if(busVoltage <= busVoltageMin)
  {
    //Serial.print("BUS - ");
    //Serial.print(busVoltage);
    //Serial.print("V - (<=");
    //Serial.print(busVoltageMin);
    //Serial.println(")");
  } 
  else if (batteryVoltage <= batteryVoltageMin)
  {
    //Serial.print("BATTERY - ");
    //Serial.print(batteryVoltage);
    //Serial.print("V - (<=");
    //Serial.print(batteryVoltageMin);
    //Serial.println(")");
  }

}

void state6() {

  if (machine.executeOnce) {
    //Serial.println("State 6 - Over Voltage");
    #ifdef BUZZER_SUPPORTED
      tone(BUZZER_ENABLE, 1500);
    #endif
    #ifdef DISPLAY_SUPPORTED
      display.clearDisplay();
      oledText("OVER", 18, 2, 4, true);
    #endif
    //digitalWrite(SOLENOID_ENABLE, LOW); // Disable the solenoid.
  }

  //Serial.print("Over Voltage - ");
  if(busVoltage >= busVoltageMax)
  {
    //Serial.print("BUS - ");
    //Serial.print(busVoltage);
    //Serial.print("V - (>=");
    //Serial.print(busVoltageMax);
    //Serial.println(")");
  } 
  else if (batteryVoltage >= batteryVoltageMax)
  {
    //Serial.print("BATTERY - ");
    //Serial.print(batteryVoltage);
    //Serial.print("V - (>=");
    //Serial.print(batteryVoltageMax);
    //Serial.println(")");
  }

}

void state7() {

  if (machine.executeOnce) {
    //Serial.println("State 7 - Emergency");

    #ifdef BUZZER_SUPPORTED
      tone(BUZZER_ENABLE, 1500);
    #endif

    #ifdef DISPLAY_SUPPORTED
      display.clearDisplay();
      oledText("NO BUS!", 25, 0, 2, false);
      oledText("Emergency?", 6, 16, 2, true);
    #endif

    digitalWrite(LIGHTING_ENABLE, HIGH); // Enable the emergency lighting.
  }
}

void state8() {
  
  if (machine.executeOnce) {
    //Serial.println("State 8 - Shutdown timer initiated");
    digitalWrite(LED_TIMEOUT, HIGH);     // Enable the TIMEOUT led.
    digitalWrite(LIGHTING_ENABLE, HIGH); // Enable the emergency lighting.
    digitalWrite(SOLENOID_ENABLE, LOW); // Disable the solenoid.
    shutdownTimer.start();
    #ifdef DISPLAY_SUPPORTED
      display.clearDisplay();
      oledText("SHUTDOWN", 15, 0, 2, false);
    #endif
  }

  unsigned long remainingSeconds = (shutdownTimer.get() - shutdownTimer.getEllapsed()) / 1000;
  int runHours = remainingSeconds / 3600;
  int secsRemaining = remainingSeconds % 3600;
  int runMinutes = secsRemaining / 60;
  int runSeconds = secsRemaining % 60;

  char buf[9];
  sprintf(buf, "%02d:%02d:%02d", runHours, runMinutes, runSeconds);
  #ifdef DISPLAY_SUPPORTED
    display.fillRect(0, 18, 128, 32, BLACK);
    oledText(buf, 15, 18, 2, true);
  #endif
  //Serial.print("SHUTDOWN: ");
  //Serial.println(buf);

  #ifdef BUZZER_SUPPORTED
    if (secsRemaining <= 10 && secsRemaining % 2 == 0)
    {
      tone(BUZZER_ENABLE, 1500);
      delay(200);
      noTone(BUZZER_ENABLE);
    }
  #endif
  
}

void state9() {
    if (machine.executeOnce) {
      //Serial.println("State 9 - QRT");
      #ifdef DISPLAY_SUPPORTED
        display.clearDisplay();
        oledText("QRT!", 22, 2, 4, true);
      #endif
      digitalWrite(ARDUINO_ENABLE, LOW);
      digitalWrite(LIGHTING_ENABLE, LOW);
      digitalWrite(BUZZER_ENABLE, LOW);
      digitalWrite(SOLENOID_ENABLE, LOW);
      digitalWrite(SOUND_BOMB, LOW);
      digitalWrite(SPARE_PWM, LOW);
      digitalWrite(LED_ACTIVE, LOW);
      digitalWrite(LED_TIMEOUT, LOW);
      digitalWrite(LED_OVERRIDE, LOW);
    }
}


// ==== Transitions ===========================================
bool transitionS0S1() {
  return (splashTimer.done()) ? true : false;
}

bool transitionS0S4() {
  return digitalRead(INPUT_ACTIVE) == LOW || digitalRead(INPUT_OVERRIDE) == LOW;
}

bool transitionS1S2() {
  return (alarmTimer.done()) ? true : false;
}

bool transitionS1S3() {
    // Check if the alarm disable button is pressed.
  return digitalRead(INPUT_ALARM_DISABLE) == LOW;
}

bool transitionS2S9() {
  // Check if the alarm disable button is pressed.
  if(digitalRead(INPUT_ALARM_DISABLE) == LOW)
    return true;

  return (alarmAlarmTimer.done()) ? true : false;
}

bool transitionS3S4() {
  return digitalRead(INPUT_ACTIVE) == LOW;
}

bool transitionS3S9() {
  if(digitalRead(INPUT_STOP_BUTTON) == LOW)
    return true;

  return (startupTimer.done()) ? true : false;
}

bool transitionS4S5() {

  if(digitalRead(INPUT_OVERRIDE) == LOW)
    return false;

  if(busVoltage < noSolenoidVoltageLevel)
    return false;

  if(batteryVoltage < noSolenoidVoltageLevel)
    return false;

  return busVoltage <= busVoltageMin || batteryVoltage <= batteryVoltageMin;
}
bool transitionS4S6() {
  if(digitalRead(INPUT_OVERRIDE) == LOW)
    return false;

  return busVoltage >= busVoltageMax || batteryVoltage >= batteryVoltageMax;
}
bool transitionToEmergency() {
  return busVoltage <= noSolenoidVoltageLevel;
}

bool transitionS4S8() {
  return digitalRead(INPUT_STOP_BUTTON) == LOW;
}

bool transitionS5S4() {
  return busVoltage > busVoltageMin && batteryVoltage >= batteryVoltageMin;
}

bool transitionS6S4() {
  return busVoltage < busVoltageMax && batteryVoltage < batteryVoltageMax;
}
bool transitionFromEmergency() {
  return busVoltage >= noSolenoidVoltageLevel;
}
bool transitionToQrt() {
  return digitalRead(INPUT_STOP_BUTTON) == LOW;
}
bool transitionS8S4() {
  return digitalRead(INPUT_ACTIVE) == LOW;
}

bool transitionS8S9() {
  // We debounce the stop button by ignoring it for the first 2 seconds.
  if(shutdownTimer.getEllapsed() > 2000 && digitalRead(INPUT_STOP_BUTTON) == LOW)
    return true;

  return (shutdownTimer.done()) ? true : false;
}

bool transitionS9S0() {
  return digitalRead(INPUT_ACTIVE) == LOW;
}


#ifdef DISPLAY_SUPPORTED
/*
   oledText(String text, int x, int y,int size, boolean d)
   text is the text string to be printed
   x is the integer x position of text
   y is the integer y position of text
   z is the text size, 1, 2, 3 etc
   updateDisplay is either "true" or "false".
*/
void* oledText(char* text, int x, int y, int size, boolean updateDisplay) {
  display.setTextSize(size);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(x, y);
  display.println(text);
  if (updateDisplay) {
    display.display();
  }
}
#endif

char* getVoltageString(double value, int len, int precision)
{
  char outstr[len];
  dtostrf(value, len, precision, outstr);
  return outstr;
}

void UpdateBatteryVoltage()
{
  // Update the battery voltage average.
  while (batterySampleCount < NUM_SAMPLES) {
    batterySum += analogRead(INPUT_BATTERY_SENSE);
    batterySampleCount ++;
  }
  batteryVoltage = ((float)batterySum / (float)NUM_SAMPLES * 5.1) / 1024.0;
  batteryVoltage = batteryVoltage * 11.365;  // Calibration fix.
  batterySum = 0;
  batterySampleCount = 0;
}

void UpdateBusVoltage()
{
  // Update the bus voltage average.
  while (busSampleCount < NUM_SAMPLES) {
    busSum += analogRead(INPUT_BUS_SENSE);
    busSampleCount ++;
  }
  busVoltage = ((float)busSum / (float)NUM_SAMPLES * 5.1) / 1024.0;
  busVoltage = busVoltage * 11.42;  // Calibration fix.
  busSum = 0;
  busSampleCount = 0;
}
