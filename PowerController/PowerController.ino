#include <Wire.h>
#include <Adafruit_SSD1306.h>

#define OLED_RESET 4

#define LIGHTING_ENABLE     5
#define BUZZER_ENABLE       6
#define SOLENOID_ENABLE     7
#define ARDUINO_ENABLE      13    // Keeps the Arduino running.  If this pin goes low the Arduino will power off.

#define LED_ACTIVE          9
#define LED_TIMEOUT         10
#define LED_OVERRIDE        11

#define INPUT_STOP_BUTTON   3
#define INPUT_ACTIVE        8
#define INPUT_OVERRIDE      12

#define INPUT_BATTERY_SENSE 1     //(analog)
#define INPUT_BUS_SENSE     2     //(analog)

// number of analog samples to take per reading
#define NUM_SAMPLES 10

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

Adafruit_SSD1306 display(128, 32, &Wire, OLED_RESET, 400000, 100000);


bool isShutdownMode = false;
bool isTimeoutMode = true;
unsigned long splashDurationMillis = 2000;
unsigned long timeoutSeconds = 90;
unsigned long millisAtTimeoutStart = 0;
unsigned long millisAtTimeoutEnd = timeoutSeconds * 1000;

// Size 2 = 10 chars.

// this is the Width and Height of Display which is 128 xy 32
#define LOGO16_GLCD_HEIGHT 32
#define LOGO16_GLCD_WIDTH  128


#if (SSD1306_LCDHEIGHT != 32)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

void setup()   {
  Serial.begin(115200);

  pinMode(INPUT_STOP_BUTTON, INPUT_PULLUP);
  pinMode(INPUT_ACTIVE, INPUT_PULLUP);
  pinMode(INPUT_OVERRIDE, INPUT_PULLUP);

  pinMode(LIGHTING_ENABLE, OUTPUT); digitalWrite(LIGHTING_ENABLE, HIGH);
  pinMode(BUZZER_ENABLE, OUTPUT);   digitalWrite(BUZZER_ENABLE, LOW);
  pinMode(SOLENOID_ENABLE, OUTPUT); digitalWrite(SOLENOID_ENABLE, LOW);
  pinMode(ARDUINO_ENABLE, OUTPUT);  digitalWrite(ARDUINO_ENABLE, HIGH);

  pinMode(LED_ACTIVE, OUTPUT);    digitalWrite(LED_ACTIVE, HIGH);
  pinMode(LED_TIMEOUT, OUTPUT);   digitalWrite(LED_TIMEOUT, HIGH);
  pinMode(LED_OVERRIDE, OUTPUT);  digitalWrite(LED_OVERRIDE, LOW);

  if (digitalRead(INPUT_ACTIVE) == LOW || IsOverride())
  {
    digitalWrite(ARDUINO_ENABLE, HIGH);
    digitalWrite(LED_ACTIVE, HIGH);
  }

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 128x32)

  // Clear the buffer.
  display.clearDisplay();

  oledText("V1.0", 6, 4, 4, true);
  tone(BUZZER_ENABLE, 1000); // Send 1KHz sound signal...
  delay(100);
  tone(BUZZER_ENABLE, 1500); // Send 1.5KHz sound signal...
  delay(100);
  noTone(BUZZER_ENABLE);
 
}

// Calibrating the voltage measurement
// Connect a stable power supply, such as a 9V battery across the resistor network. Measure the voltage across both resistors together i.e. measure the battery voltage.
// Now measure the voltage across the 100k resistor (TP5) for battery or (TP2) for bus.
// The voltage divider factor is calculated by dividing the first voltage by the second voltage or dividing factor = input voltage ÷ output voltage
// For example, if the first or input voltage measured is 10.02V and the second or output voltage is 0.9V, then the division factor is:
// 10.02 ÷ 0.9 = 11.133
// Now use this value in the Arduino sketch code.


void loop() {

  // Check if the stop button is pressed.  If so, switch off.
  CheckForShutdownButton();

  while (digitalRead(ARDUINO_ENABLE) == LOW && digitalRead(INPUT_ACTIVE) == HIGH)
  {
    display.clearDisplay();
  }

  if (digitalRead(INPUT_ACTIVE) == LOW)
  {
    digitalWrite(ARDUINO_ENABLE, HIGH);
    digitalWrite(LED_ACTIVE, HIGH);
  }

  digitalWrite(LED_OVERRIDE, IsOverride());

  UpdateBatteryVoltage();
  UpdateBusVoltage();

  while (IsBatteryVoltageOutOfRange() || (IsBusVoltageOutOfRange() && !isTimeoutMode) )
  {

    // Check if the stop button is pressed.  If so, switch off.
    CheckForShutdownButton();

    tone(BUZZER_ENABLE, 1000);
    display.clearDisplay();

    if (busVoltage < noSolenoidVoltageLevel)
    {
      oledText("NO DC BUS", 5, 0, 2, false);
      oledText("EMERGENCY?", 0, 18, 2, false);
      digitalWrite(LIGHTING_ENABLE, HIGH);  // Enable the emergency lights.
    }
    else if (IsBusVoltageOutOfRange())
    {
      digitalWrite(SOLENOID_ENABLE, IsSolenoidAllowedToBeOn());
      oledText("BUS:" + getVoltageString(busVoltage, 5, 1) + "V", 0, 1, 2, false);
      oledText("PLS CHECK!", 0, 18, 2, false);
    }
    else if (IsBatteryVoltageOutOfRange())
    {
      digitalWrite(SOLENOID_ENABLE, IsSolenoidAllowedToBeOn());
      oledText("BAT:" + getVoltageString(busVoltage, 5, 1) + "V", 0, 1, 2, false);
      oledText("PLS CHECK!", 0, 18, 2, false);
    }

    display.display();
    UpdateBatteryVoltage();
    UpdateBusVoltage();
    CheckTimeout();
  }

  noTone(BUZZER_ENABLE);

  isTimeoutMode = isTimeoutMode && digitalRead(INPUT_ACTIVE) == HIGH && !IsOverride();
  digitalWrite(LED_TIMEOUT, isTimeoutMode);
  digitalWrite(LIGHTING_ENABLE, isTimeoutMode);
  if(!isTimeoutMode)
  {
      DisableTimeout();
  }

  if (millis() > splashDurationMillis)
  {
    display.clearDisplay();

    if(!isShutdownMode)
    {
      oledText("BATTERY:          ", 4, 3, 1, false);
      oledText(getVoltageString(batteryVoltage, 7, 3) + " V", 72, 3, 1, false);
  
      oledText("DC BUS :           ", 4, 12, 1, false);
      oledText(getVoltageString(busVoltage, 7, 3) + " V", 72, 12, 1, false);
    }
  
    if (isTimeoutMode)
    {

      CheckTimeout();

      unsigned long totalSecondsUntilShutdown = (millisAtTimeoutEnd - millis()) / 1000;
      int runHours = totalSecondsUntilShutdown / 3600;
      int secsRemaining = totalSecondsUntilShutdown % 3600;
      int runMinutes = secsRemaining / 60;
      int runSeconds = secsRemaining % 60;

      char buf[21];

      if(isShutdownMode)
      {
        oledText(" SHUTDOWN", 4, 3, 2, false);
        sprintf(buf, "      %02d:%02d:%02d      ", runHours, runMinutes, runSeconds);
      }
       else
      {
        sprintf(buf, "TIMEOUT IN: %02d:%02d:%02d", runHours, runMinutes, runSeconds);
      }
      
      oledText(String(buf), 4, 21, 1, false);

      if (secsRemaining <= 10 && secsRemaining % 2 == 0)
      {
        tone(BUZZER_ENABLE, 1500);
        delay(100);
        noTone(BUZZER_ENABLE);
      }

    }
    else
    {

      digitalWrite(SOLENOID_ENABLE, IsSolenoidAllowedToBeOn());

      if (IsOverride())
      {
        oledText("STATUS :    OVERRIDE", 4, 21, 1, false);
      }
      else
      {
        oledText("STATUS :      NORMAL", 4, 21, 1, false);
      }

    }

    display.drawRect(0, 0, 128, 32, WHITE);
    display.display();

  }

}


/*
   oledText(String text, int x, int y,int size, boolean d)
   text is the text string to be printed
   x is the integer x position of text
   y is the integer y position of text
   z is the text size, 1, 2, 3 etc
   updateDisplay is either "true" or "false".
*/
void oledText(String text, int x, int y, int size, boolean updateDisplay) {

  display.setTextSize(size);
  display.setTextColor(WHITE);
  display.setCursor(x, y);
  display.println(text);
  if (updateDisplay) {
    display.display();
  }
}

String getVoltageString(double value, int len, int precision)
{
  char outstr[len];
  dtostrf(value, len, precision, outstr);
  return String(outstr);
}


void ShutdownArduino()
{
  Serial.println("SHUTDOWN");
  isTimeoutMode = true;
  isShutdownMode = true;
  digitalWrite(LIGHTING_ENABLE, HIGH);
  digitalWrite(LED_TIMEOUT, HIGH);
  digitalWrite(BUZZER_ENABLE, LOW);
  digitalWrite(SOLENOID_ENABLE, LOW);
  digitalWrite(LED_ACTIVE, LOW);
  digitalWrite(LED_OVERRIDE, LOW);
}


void PowerOffArduino()
{
  Serial.println("QRT");
  display.clearDisplay();
  oledText("QRT!", 22, 4, 4, true);
  digitalWrite(ARDUINO_ENABLE, LOW);
  digitalWrite(LIGHTING_ENABLE, LOW);
  digitalWrite(BUZZER_ENABLE, LOW);
  digitalWrite(SOLENOID_ENABLE, LOW);
  digitalWrite(LED_ACTIVE, LOW);
  digitalWrite(LED_TIMEOUT, LOW);
  digitalWrite(LED_OVERRIDE, LOW);
  while (1 == 1)
  {
    // Should never get here.
  }
}

void DisableTimeout()
{
  isShutdownMode = false;
  isTimeoutMode = false;
  millisAtTimeoutEnd = millis() + (timeoutSeconds * 1000);
}

void UpdateBatteryVoltage()
{
  // Update the battery voltage average.
  while (batterySampleCount < NUM_SAMPLES) {
    batterySum += analogRead(INPUT_BATTERY_SENSE);
    batterySampleCount ++;
  }
  batteryVoltage = ((float)batterySum / (float)NUM_SAMPLES * 5.1) / 1024.0;
  batteryVoltage = batteryVoltage * 10.83;  // Calibration fix.
  batterySum = 0;
  batterySampleCount = 0;
}

bool IsBatteryVoltageOutOfRange()
{
  return batteryVoltage <= batteryVoltageMin || batteryVoltage  >= batteryVoltageMax;
}

void UpdateBusVoltage()
{
  // Update the bus voltage average.
  while (busSampleCount < NUM_SAMPLES) {
    busSum += analogRead(INPUT_BUS_SENSE);
    busSampleCount ++;
  }
  busVoltage = ((float)busSum / (float)NUM_SAMPLES * 5.1) / 1024.0;
  busVoltage = busVoltage * 10.83;  // Calibration fix.
  busSum = 0;
  busSampleCount = 0;
}

bool IsBusVoltageOutOfRange()
{
  return busVoltage <= busVoltageMin || busVoltage  >= busVoltageMax;
}

bool IsBusConnected()
{
  return busVoltage > 1;
}

bool IsSolenoidAllowedToBeOn()
{
  if (isTimeoutMode)
    return false;
  if (IsBusVoltageOutOfRange() && busVoltage > noSolenoidVoltageLevel)
    return false;
  if (IsBatteryVoltageOutOfRange())
    return false;

  return true;
}

bool IsOverride()
{
  return digitalRead(INPUT_OVERRIDE) == LOW;
}

void CheckTimeout()
{
  unsigned long totalSecondsUntilShutdown = (millisAtTimeoutEnd - millis()) / 1000;
  if (isTimeoutMode && totalSecondsUntilShutdown == 0)
  {
    PowerOffArduino();
  }
}

void CheckForShutdownButton()
{
  if (digitalRead(INPUT_STOP_BUTTON) == LOW && !IsOverride())
  {
    if (isTimeoutMode)
    {
      PowerOffArduino();
    }
    else
    {
      ShutdownArduino();
    }
    
    // Halt the app whilst the button is pressed.
    while(digitalRead(INPUT_STOP_BUTTON) == LOW)
    {
    }
    
  }
}
