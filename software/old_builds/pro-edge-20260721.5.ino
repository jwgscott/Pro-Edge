// Pro-Edge (a Protogasm fork) build 20260721.5
// (build backup before complete code rework and cleanup)
/* Drives a vibrator and uses changes in pressure of an inflatable buttplug
 * to estimate a user's closeness to orgasm, and turn off the vibrator
 * before that point.
 * A state machine updating at 60Hz creates different modes and option menus
 * that can be identified by the color of the LEDs, especially the RGB LED
 * in the central button/encoder knob.
 * 
 * [Red]    Manual Vibrator Control
 * [Blue]   Automatic vibrator edging, knob adjusts orgasm detection sensitivity
 * [Green]  Setting menu for maximum vibrator speed in automatic mode
 * [White]  Debubbing menu to show data from the pressure sensor ADC
 * [Off]    While still plugged in, holding the button down for >3 seconds turns
 *          the whole device off, until the button is pressed again.
 * 
 * Settings like edging sensitivity, or maximum motor speed are stored in EEPROM,
 * so they are saved through power-cycling.
 * 
 * In the automatic edging mode, the vibrator speed will linearly ramp up to full
 * speed (set in the green menu) over 30 seconds. If a near-orgasm is detected,
 * the vibrator abruptly turns off for 15 seconds, then begins ramping up again.
 * 
 * The motor will beep during power on/off, and if the plug pressure rises above
 * the maximum the board can read - this condition could lead to a missed orgasm 
 * if unchecked. The analog gain for the sensor is adjustable via a trimpot to
 * accomidate different types of plugs that have higher/lower resting pressures.
 * 
 * Motor speed, current pressure, and average pressure are reported via USB serial
 * at 115200 baud. Timestamps can also be enabled, from the main loop.
 * 
 * There is some framework for more features like an adjustable "cool off" time 
 * other than the default 15 seconds, and options for LED brightness and enabling/
 * disabling beeps.
 * 
 * Note - Do not set all the LEDs to white at full brightness at once
 * (RGB 255,255,255) It may overheat the voltage regulator and cause the board 
 * to reset.
 */
//=======Libraries===============================
#include <Encoder.h>
#include <EEPROM.h>
#include "FastLED.h"
#include "RunningAverage.h"

//=======Hardware Setup===============================
//LEDs
#define NUM_LEDS 24
#define LED_PIN 10
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
#define BRIGHTNESS 50  //Subject to change, limits current that the LEDs draw

//Encoder
#define ENC_SW 5      //Pushbutton on the encoder
Encoder myEnc(3, 2);  //Quadrature inputs
#define ENC_SW_UP HIGH
#define ENC_SW_DOWN LOW

//Motor
#define MOTPIN 9

//Pressure Sensor Analog In
#define BUTTPIN A0
// Sampling 4x and not dividing keeps the samples from the Arduino Uno's 10 bit
// ADC in a similar range to the Teensy LC's 12-bit ADC.  This helps ensure the
// feedback algorithm will behave similar to the original.
#define OVERSAMPLE 4
#define ADC_MAX 1023

//=======Software/Timing options=====================
#define FREQUENCY 60          //Update frequency in Hz
#define LONG_PRESS_MS 600     //ms requirements for a long press, to move to option menus
#define V_LONG_PRESS_MS 2500  //ms for a very long press, which turns the device off

//Update/render period
#define period (1000 / FREQUENCY)
#define longBtnCount (LONG_PRESS_MS / period)

//Running pressure average array length and update frequency
#define RA_HIST_SECONDS 25
#define RA_FREQUENCY 6
#define RA_TICK_PERIOD (FREQUENCY / RA_FREQUENCY)
RunningAverage raPressure(RA_FREQUENCY* RA_HIST_SECONDS);
int sensitivity = 0;  //orgasm detection sensitivity, persists through different states

//=======State Machine Modes=========================
#define MANUAL 1
#define AUTO 2
#define OPT_SPEED 3
#define OPT_RAMPSPD 4
#define OPT_BEEP 5
#define OPT_PRES 6
#define AUTO_SMOOTH 7    // <-- NEW: Smooth edging mode
#define AUTO_RELEASE 8   // <-- NEW: Edge counting & release mode
#define OPT_EDGES 9      // <-- NEW: Menu for target edges
#define OPT_RELDUR 10    // <-- NEW: Menu for release duration
#define OPT_VARIANCE 11  // <-- NEW: Menu for target edge variance

//Button states - no press, short press, long press
#define BTN_NONE 0
#define BTN_SHORT 1
#define BTN_LONG 2
#define BTN_V_LONG 3


uint8_t state = MANUAL;
//=======Global Settings=============================
#define MOT_MAX 255  // Motor PWM maximum
#define MOT_MIN 20   // Motor PWM minimum.  It needs a little more than this to start.

CRGB leds[NUM_LEDS];

int pressure = 0;
int avgPressure = 0;  //Running 25 second average pressure
//int bri =100; //Brightness setting
int rampTimeS = 30;  //Ramp-up time, in seconds
#define DEFAULT_PLIMIT 600
int pLimit = DEFAULT_PLIMIT;  //Limit in change of pressure before the vibrator turns off
int maxSpeed = 255;           //maximum speed the motor will ramp up to in automatic mode
float motSpeed = 0;           //Motor speed, 0-255 (float to maintain smooth ramping to low speeds)
// --- New Variables for Auto Release ---
int targetEdges = 4;   // Number of edges required before release
int edgeVariance = 0;  // NEW: +/- variance for the roulette
int actualTargetEdges = 4;
int currentEdges = 0;                // Current counted edges
int releaseDurationS = 16;           // How long to run at max speed (in seconds)
bool isReleasing = false;            // State flag for the release phase
unsigned long releaseStartTime = 0;  // Timer tracking for the release phase
bool wasOverLimit = false;           // Debouncer to ensure one clench = one edge
unsigned long lastEdgeTime = 0;

//=======EEPROM Addresses============================
//128b available on teensy LC
#define BEEP_ADDR 1
#define MAX_SPEED_ADDR 2
#define SENSITIVITY_ADDR 3
#define TARGET_EDGES_ADDR 4
#define RELEASE_DUR_ADDR 5
#define RAMPSPEED_ADDR 6  //For now, ramp speed adjustments aren't implemented
#define VARIANCE_ADDR 7

//=======Setup=======================================
//Beep out tones over the motor by frequency (1047,1396,2093) may work well
void beep_motor(int f1, int f2, int f3) {
  analogWrite(MOTPIN, 0);
  tone(MOTPIN, f1);
  delay(250);
  tone(MOTPIN, f2);
  delay(250);
  tone(MOTPIN, f3);
  delay(250);
  noTone(MOTPIN);
  analogWrite(MOTPIN, motSpeed);
}

void setup() {
  pinMode(ENC_SW, INPUT);      //Pin to read when encoder is pressed
  digitalWrite(ENC_SW, HIGH);  // Encoder switch pullup

  analogReference(EXTERNAL);

  // Classic AVR based Arduinos have a PWM frequency of about 490Hz which
  // causes the motor to whine.  Change the prescaler to achieve 31372Hz.
  //sbi(TCCR1B, CS10);
  //cbi(TCCR1B, CS11);
  //cbi(TCCR1B, CS12);
  //TCCR1B |= (1 << CS10);
  //TCCR1B |= (1 << CS11);
  //TCCR1B |= (1 << CS12);
  // Löscht zuerst die Bits CS11 und CS12 (setzt sie zwingend auf 0)
  TCCR1B &= ~((1 << CS11) | (1 << CS12));

  // Setzt das Bit CS10 auf 1 (Prescaler = 1 -> ~31.4 kHz PWM Frequenz)
  TCCR1B |= (1 << CS10);


  pinMode(MOTPIN, OUTPUT);  //Enable "analog" out (PWM)

  pinMode(BUTTPIN, INPUT);  //default is 10 bit resolution (1024), 0-3.3

  raPressure.clear();  //Initialize a running pressure average

  digitalWrite(MOTPIN, LOW);  //Make sure the motor is off

  delay(3000);  // 3 second delay for recovery

  Serial.begin(115200);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  // limit power draw to .6A at 5v... Didn't seem to work in my FastLED version though
  //FastLED.setMaxPowerInVoltsAndMilliamps(5,DEFAULT_PLIMIT);
  FastLED.setBrightness(BRIGHTNESS);

  //Recall saved settings from memory
  sensitivity = EEPROM.read(SENSITIVITY_ADDR);
  maxSpeed = min(EEPROM.read(MAX_SPEED_ADDR), MOT_MAX);  //Obey the MOT_MAX the first power  cycle after changing it.
  beep_motor(1047, 1396, 2093);                          //Power on beep

  // Load new settings, providing sensible defaults if the EEPROM is blank (returns 255)
  // Load target edges
  targetEdges = EEPROM.read(TARGET_EDGES_ADDR);
  if (targetEdges == 0 || targetEdges == 255) targetEdges = 4;

  // Load release duration
  releaseDurationS = EEPROM.read(RELEASE_DUR_ADDR);
  if (releaseDurationS == 0 || releaseDurationS == 255) releaseDurationS = 16;

  // Load ramp time
  rampTimeS = EEPROM.read(RAMPSPEED_ADDR);
  if (rampTimeS == 0 || rampTimeS == 255) rampTimeS = 30;  // Default to 30 seconds

  // Seed the random generator using noise from an unconnected analog pin
  randomSeed(analogRead(A1));

  // Initialize the hidden target to match the baseline on startup
  actualTargetEdges = targetEdges;

  // Load variance setting
  edgeVariance = EEPROM.read(VARIANCE_ADDR);
  if (edgeVariance == 255) edgeVariance = 0;  // Default to 0 (no variance)

}

//=======LED Drawing Functions=================

//Draw a "cursor", one pixel representing either a pressure or encoder position value
//C1,C2,C3 are colors for each of 3 revolutions over the 13 LEDs (39 values)
void draw_cursor_3(int pos, CRGB C1, CRGB C2, CRGB C3) {
  pos = constrain(pos, 0, NUM_LEDS * 3 - 1);
  int colorNum = pos / NUM_LEDS;   //revolution number
  int cursorPos = pos % NUM_LEDS;  //place on circle, from 0-12
  switch (colorNum) {
    case 0:
      leds[cursorPos] = C1;
      break;
    case 1:
      leds[cursorPos] = C2;
      break;
    case 2:
      leds[cursorPos] = C3;
      break;
  }
}

//Draw a "cursor", one pixel representing either a pressure or encoder position value
void draw_cursor(int pos, CRGB C1) {
  pos = constrain(pos, 0, NUM_LEDS - 1);
  leds[pos] = C1;
}

//Draw 3 revolutions of bars around the LEDs. From 0-39, 3 colors
void draw_bars_3(int pos, CRGB C1, CRGB C2, CRGB C3) {
  pos = constrain(pos, 0, NUM_LEDS * 3 - 1);
  int colorNum = pos / NUM_LEDS;  //revolution number
  int barPos = pos % NUM_LEDS;    //place on circle, from 0-12
  switch (colorNum) {
    case 0:
      fill_gradient_RGB(leds, 0, C1, barPos, C1);
      //leds[barPos] = C1;
      break;
    case 1:
      fill_gradient_RGB(leds, 0, C1, barPos, C2);
      break;
    case 2:
      fill_gradient_RGB(leds, 0, C2, barPos, C3);
      break;
  }
}

//Provide a limited encoder reading corresponting to tacticle clicks on the knob.
//Each click passes through 4 encoder pulses. This reduces it to 1 pulse per click
int encLimitRead(int minVal, int maxVal) {
  if (myEnc.read() > maxVal * 4) myEnc.write(maxVal * 4);
  else if (myEnc.read() < minVal * 4) myEnc.write(minVal * 4);
  return constrain(myEnc.read() / 4, minVal, maxVal);
}

//=======Program Modes/States==================

// Manual vibrator control mode (red), still shows orgasm closeness in background
void run_manual() {
  //In manual mode, only allow for 13 cursor positions, for adjusting motor speed.
  int knob = encLimitRead(0, NUM_LEDS - 1);
  motSpeed = map(knob, 0, NUM_LEDS - 1, 0., (float)MOT_MAX);
  analogWrite(MOTPIN, motSpeed);

  //gyrGraphDraw(avgPressure, 0, 4 * 3 * NUM_LEDS);
  int presDraw = map(constrain(pressure - avgPressure, 0, pLimit), 0, pLimit, 0, NUM_LEDS * 3);
  draw_bars_3(presDraw, CRGB::Green, CRGB::Yellow, CRGB::Red);
  draw_cursor(knob, CRGB::Red);
}

// Automatic edging mode, knob adjust sensitivity.
void run_auto() {
  static float motIncrement = 0.0;
  motIncrement = ((float)maxSpeed / ((float)FREQUENCY * (float)rampTimeS));

  int knob = encLimitRead(0, (3 * NUM_LEDS) - 1);
  sensitivity = knob * 4;  //Save the setting if we leave and return to this state
  //Reverse "Knob" to map it onto a pressure limit, so that it effectively adjusts sensitivity
  pLimit = map(knob, 0, 3 * (NUM_LEDS - 1), 600, 1);  //set the limit of delta pressure before the vibrator turns off
  //When someone clenches harder than the pressure limit
  if (pressure - avgPressure > pLimit) {
    motSpeed = -.5 * (float)rampTimeS * ((float)FREQUENCY * motIncrement);  //Stay off for a while (half the ramp up time)
  } else if (motSpeed < (float)maxSpeed) {
    motSpeed += motIncrement;
  }
  if (motSpeed > MOT_MIN) {
    analogWrite(MOTPIN, (int)motSpeed);
  } else {
    analogWrite(MOTPIN, 0);
  }

  int presDraw = map(constrain(pressure - avgPressure, 0, pLimit), 0, pLimit, 0, NUM_LEDS * 3);
  draw_bars_3(presDraw, CRGB::Green, CRGB::Yellow, CRGB::Red);
  draw_cursor_3(knob, CRGB(50, 50, 200), CRGB::Blue, CRGB::Purple);
}

// Proportional automatic edging mode. Speed decreases as pressure nears pLimit.
void run_auto_smooth() {
  static float motIncrement = 0.0;
  // Calculate the linear ramp increment based on frequency and user settings
  motIncrement = ((float)maxSpeed / ((float)FREQUENCY * (float)rampTimeS));

  int knob = encLimitRead(0, (3 * NUM_LEDS) - 1);
  sensitivity = knob * 4;  // Save the setting
  pLimit = map(knob, 0, 3 * (NUM_LEDS - 1), 600, 1);

  int delta = pressure - avgPressure;

  // Proportional Throttle Logic
  if (delta > pLimit) {
    // If the limit is fully breached, drop speed to 0 immediately
    motSpeed = 0;
  } else if (delta > 0) {
    // Calculate a throttle percentage (0.0 to 1.0) based on closeness to pLimit
    float throttle = (float)delta / (float)pLimit;
    float targetSpeed = maxSpeed * (1.0 - throttle);

    if (motSpeed > targetSpeed) {
      // Decelerate quickly if current speed is higher than the proportional target
      motSpeed -= (motIncrement * 15);
      if (motSpeed < targetSpeed) motSpeed = targetSpeed;
    } else if (motSpeed < targetSpeed && motSpeed < maxSpeed) {
      // Otherwise, continue ramping up normally
      motSpeed += motIncrement;
    }
  } else {
    // If pressure is below average, ramp up normally
    if (motSpeed < (float)maxSpeed) {
      motSpeed += motIncrement;
    }
  }

  // Enforce minimum motor PWM to prevent stalling
  if (motSpeed > MOT_MIN) {
    analogWrite(MOTPIN, (int)motSpeed);
  } else {
    analogWrite(MOTPIN, 0);
  }

  // LED Rendering
  int presDraw = map(constrain(delta, 0, pLimit), 0, pLimit, 0, NUM_LEDS * 3);
  draw_bars_3(presDraw, CRGB::Green, CRGB::Yellow, CRGB::Red);

  // Use a Cyan/Teal cursor to differentiate this mode from the Blue abrupt auto mode
  draw_cursor_3(knob, CRGB(0, 100, 100), CRGB::Cyan, CRGB::LightBlue);
}

void run_auto_release() {
  static float motIncrement = 0.0;
  motIncrement = ((float)maxSpeed / ((float)FREQUENCY * (float)rampTimeS));

  int knob = encLimitRead(0, (3 * NUM_LEDS) - 1);
  sensitivity = knob * 4;
  pLimit = map(knob, 0, 3 * (NUM_LEDS - 1), 600, 1);

  // --- Release Phase Logic ---
  if (isReleasing) {
    analogWrite(MOTPIN, maxSpeed);  // Force maximum configured speed

    // Check if the defined release duration has passed
    if (millis() - releaseStartTime > ((unsigned long)releaseDurationS * 1000UL)) {
      isReleasing = false;
      currentEdges = 0;
      motSpeed = 0;
    }

    // Flash LEDs white to indicate release phase
    fill_solid(leds, NUM_LEDS, CRGB::White);
    fadeToBlackBy(leds, NUM_LEDS, (millis() % 100 > 50) ? 100 : 0);
    return;
  }

  // --- Smooth Edge Counting and Ramping Logic ---
  int delta = pressure - avgPressure;

  if (delta > pLimit) {
    if (!wasOverLimit) {
      wasOverLimit = true;

      // NEW: Only count the edge if 15 seconds (15000 ms) have passed,
      // or if it is the very first edge of the session (currentEdges == 0).
      if (millis() - lastEdgeTime > 15000UL || currentEdges == 0) {
        currentEdges++;
        lastEdgeTime = millis();  // Record the exact time this edge was counted

        // Evaluate against the HIDDEN randomized target
        if (currentEdges >= actualTargetEdges) {
          isReleasing = true;
          releaseStartTime = millis();

          // Calculate the next hidden target for the following cycle
          actualTargetEdges = targetEdges + random(-edgeVariance, edgeVariance + 1);

          // CLAMPING: Prevent the target from ever dropping below 1 edge
          if (actualTargetEdges < 1) actualTargetEdges = 1;

          return;
        }
      }
    }
    // Drop speed to 0 immediately when limit is breached, regardless of if the edge was counted
    motSpeed = 0;
  } else if (delta > 0) {
    wasOverLimit = false;

    // Proportional Throttle Logic
    float throttle = (float)delta / (float)pLimit;
    float targetSpeed = maxSpeed * (1.0 - throttle);

    if (motSpeed > targetSpeed) {
      motSpeed -= (motIncrement * 15);
      if (motSpeed < targetSpeed) motSpeed = targetSpeed;
    } else if (motSpeed < targetSpeed && motSpeed < maxSpeed) {
      motSpeed += motIncrement;
    }
  } else {
    wasOverLimit = false;
    if (motSpeed < (float)maxSpeed) {
      motSpeed += motIncrement;
    }
  }

  // Motor output
  if (motSpeed > MOT_MIN) analogWrite(MOTPIN, (int)motSpeed);
  else analogWrite(MOTPIN, 0);

  // --- LED Rendering ---
  // 1. Draw the pressure background
  int presDraw = map(constrain(delta, 0, pLimit), 0, pLimit, 0, NUM_LEDS * 3);
  draw_bars_3(presDraw, CRGB::Green, CRGB::Yellow, CRGB::Red);

  // 2. Draw the Live Edge Counter Visualization (Cursor & Target)
  int visualProgress = map(currentEdges, 0, targetEdges, 0, NUM_LEDS - 1);
  visualProgress = constrain(visualProgress, 0, NUM_LEDS - 1);

  // Draw the fixed target at the very end of the LED scale
  leds[NUM_LEDS - 1] = CRGB::Gold;

  // Draw the moving progress cursor
  leds[visualProgress] = CRGB::Pink;

  // 3. Draw the mode cursor
  draw_cursor_3(knob, CRGB::Magenta, CRGB::DeepPink, CRGB::HotPink);
}

void run_opt_edges() {
  // Map 24 LED positions to 2-48 edges in increments of 2
  int knob = encLimitRead(0, NUM_LEDS - 1);
  targetEdges = (knob * 2) + 2;
  analogWrite(MOTPIN, 0);  // Motor off while setting

  draw_bars_3(knob, CRGB::Magenta, CRGB::Magenta, CRGB::Magenta);
  draw_cursor(knob, CRGB::White);
}

void run_opt_variance() {
  // Limit the encoder to exactly 0 to 6 increments
  int knob = encLimitRead(0, 6);
  edgeVariance = knob;
  analogWrite(MOTPIN, 0);  // Motor off while setting

  // Map the 0-6 variance directly onto the 0-23 LED positions
  // This satisfies the requirement of dividing the 24 LEDs by 6 (4 LEDs per step)
  int displayPos = map(edgeVariance, 0, 6, 0, NUM_LEDS - 1);

  draw_bars_3(displayPos, CRGB::Cyan, CRGB::Cyan, CRGB::Cyan);

  // Draw a white cursor at the leading edge
  draw_cursor(displayPos, CRGB::White);
}

void run_opt_reldur() {
  // Map 24 LED positions to 2-48 seconds in increments of 2
  int knob = encLimitRead(0, NUM_LEDS - 1);
  releaseDurationS = (knob * 2) + 2;
  analogWrite(MOTPIN, 0);  // Motor off while setting

  draw_bars_3(knob, CRGB::Yellow, CRGB::Yellow, CRGB::Yellow);
  draw_cursor(knob, CRGB::White);
}

//Setting menu for adjusting the maximum vibrator speed automatic mode will ramp up to
void run_opt_speed() {
  Serial.println("speed settings");
  int knob = encLimitRead(0, NUM_LEDS - 1);
  motSpeed = map(knob, 0, NUM_LEDS - 1, 0., (float)MOT_MAX);
  analogWrite(MOTPIN, motSpeed);
  maxSpeed = motSpeed;  //Set the maximum ramp-up speed in automatic mode
  //Little animation to show ramping up on the LEDs
  static int visRamp = 0;
  if (visRamp <= FREQUENCY * NUM_LEDS - 1) visRamp += 16;
  else visRamp = 0;
  draw_bars_3(map(visRamp, 0, (NUM_LEDS - 1) * FREQUENCY, 0, knob), CRGB::Green, CRGB::Green, CRGB::Green);
}

//Not yet added, but adjusts how quickly the vibrator turns back on after being triggered off
void run_opt_rampspd() {
  // Map 24 LED positions to 5-120 seconds in increments of 5
  int knob = encLimitRead(0, NUM_LEDS - 1);
  rampTimeS = (knob * 5) + 5;
  analogWrite(MOTPIN, 0);  // Ensure motor remains off while setting

  // Display solid orange bars to indicate the Ramp Speed menu
  draw_bars_3(knob, CRGB::Orange, CRGB::Orange, CRGB::Orange);
  draw_cursor(knob, CRGB::White);
}

//Also not completed, option for enabling/disabling beeps
void run_opt_beep() {
  Serial.println("Brightness Settings");
}

//Simply display the pressure analog voltage. Useful for debugging sensitivity issues.
void run_opt_pres() {
  int p = map(analogRead(BUTTPIN), 0, ADC_MAX, 0, NUM_LEDS - 1);
  draw_cursor(p, CRGB::White);
}

//Poll the knob click button, and check for long/very long presses as well
uint8_t check_button() {
  static bool lastBtn = ENC_SW_DOWN;
  static unsigned long keyDownTime = 0;
  uint8_t btnState = BTN_NONE;
  bool thisBtn = digitalRead(ENC_SW);

  //Detect single presses, no repeating, on keyup
  if (thisBtn == ENC_SW_DOWN && lastBtn == ENC_SW_UP) {
    keyDownTime = millis();
  }

  if (thisBtn == ENC_SW_UP && lastBtn == ENC_SW_DOWN) {  //there was a keyup
    if ((millis() - keyDownTime) >= V_LONG_PRESS_MS) {
      btnState = BTN_V_LONG;
    } else if ((millis() - keyDownTime) >= LONG_PRESS_MS) {
      btnState = BTN_LONG;
    } else {
      btnState = BTN_SHORT;
    }
  }

  lastBtn = thisBtn;
  return btnState;
}

//run the important/unique parts of each state. Also, set button LED color.
void run_state_machine(uint8_t state) {
  switch (state) {
    case MANUAL:
      run_manual();
      break;
    case AUTO:
      run_auto();
      break;
    case AUTO_SMOOTH:
      run_auto_smooth();
      break;
    case AUTO_RELEASE:
      run_auto_release();
      break;
    case OPT_EDGES:
      run_opt_edges();
      break;
    case OPT_VARIANCE:
      run_opt_variance();
      break;
    case OPT_RELDUR:
      run_opt_reldur();
      break;
    case OPT_SPEED:
      run_opt_speed();
      break;
    case OPT_RAMPSPD:
      run_opt_rampspd();
      break;
    case OPT_BEEP:
      run_opt_beep();
      break;
    case OPT_PRES:
      run_opt_pres();
      break;
    default:
      run_manual();
      break;
  }
}

//Switch between state machine states, and reset the encoder position as necessary
//Returns the next state to run. Very long presses will turn the system off (sort of)
uint8_t set_state(uint8_t btnState, uint8_t state) {
  if (btnState == BTN_NONE) {
    return state;
  }
  if (btnState == BTN_V_LONG) {
    //Turn the device off until woken up by the button
    Serial.println("power off");
    fill_gradient_RGB(leds, 0, CRGB::Black, NUM_LEDS - 1, CRGB::Black);  //Turn off LEDS
    FastLED.show();
    analogWrite(MOTPIN, 0);
    beep_motor(2093, 1396, 1047);
    analogWrite(MOTPIN, 0);  //Turn Motor off
    // 1. Wait infinitely while the button is UNPRESSED (Sleeping)
    while (digitalRead(ENC_SW) == ENC_SW_UP) delay(1);
    // 2. Wait while the button is PRESSED (Waking up)
    // This prevents the device from immediately registering a short press upon waking
    while (digitalRead(ENC_SW) == ENC_SW_DOWN) delay(1);
    beep_motor(1047, 1396, 2093);
    return MANUAL;
  } else if (btnState == BTN_SHORT) {
    switch (state) {
      case MANUAL:
        myEnc.write(sensitivity);  //Whenever going into auto mode, keep the last sensitivity
        motSpeed = 0;              //Also reset the motor speed to 0
        return AUTO;
      case AUTO:
        myEnc.write(sensitivity);
        motSpeed = 0;
        EEPROM.update(SENSITIVITY_ADDR, sensitivity);
        return AUTO_SMOOTH;
      case AUTO_SMOOTH:
        myEnc.write(sensitivity);
        motSpeed = 0;
        EEPROM.update(SENSITIVITY_ADDR, sensitivity);
        return AUTO_RELEASE;
      case AUTO_RELEASE:
        myEnc.write(0);
        motSpeed = 0;
        currentEdges = 0;     // Reset tracking if mode is manually exited
        isReleasing = false;  // Cancel active release if manually exited
        EEPROM.update(SENSITIVITY_ADDR, sensitivity);
        return MANUAL;  // Return to start
      case OPT_SPEED:
        EEPROM.update(MAX_SPEED_ADDR, maxSpeed);
        // Pre-set encoder knob to the currently saved ramp time
        // Formula: ((Value - Min) / Increment) * 4 pulses
        myEnc.write(((rampTimeS - 5) / 5) * 4);
        return OPT_RAMPSPD;  //Skip beep and rampspeed settings for now
      case OPT_EDGES:
        EEPROM.update(TARGET_EDGES_ADDR, targetEdges);
        // Pre-set encoder to the current saved variance (multiply by 4 for encoder pulses)
        myEnc.write(edgeVariance * 4);
        return OPT_VARIANCE;
      case OPT_VARIANCE:
        EEPROM.update(VARIANCE_ADDR, edgeVariance);
        // Pre-set encoder for the Release Duration menu
        myEnc.write((releaseDurationS - 2) * 2);
        return OPT_RELDUR;
      case OPT_RELDUR:
        EEPROM.update(RELEASE_DUR_ADDR, releaseDurationS);
        // Chain into the original speed settings menu
        myEnc.write(map(maxSpeed, 0, 255, 0, 4 * (NUM_LEDS)));
        return OPT_SPEED;
      case OPT_RAMPSPD:
        EEPROM.update(RAMPSPEED_ADDR, rampTimeS);
        motSpeed = 0;
        analogWrite(MOTPIN, motSpeed);  // Turn the motor off for the white pressure monitoring mode
        myEnc.write(0);
        return OPT_PRES;
      case OPT_BEEP:
        myEnc.write(0);
        return OPT_PRES;
      case OPT_PRES:
        myEnc.write(map(maxSpeed, 0, 255, 0, 4 * (NUM_LEDS)));  //start at saved value
        return OPT_SPEED;
    }
  } else if (btnState == BTN_LONG) {
    switch (state) {
      case MANUAL:
        myEnc.write(map(maxSpeed, 0, 255, 0, 4 * (NUM_LEDS)));  //start at saved value
        return OPT_SPEED;
      case AUTO:
        myEnc.write(map(maxSpeed, 0, 255, 0, 4 * (NUM_LEDS)));  //start at saved value
        return OPT_SPEED;
      case AUTO_SMOOTH:
        myEnc.write(map(maxSpeed, 0, 255, 0, 4 * (NUM_LEDS)));
        return OPT_SPEED;
      case OPT_SPEED:
        myEnc.write(0);
        return MANUAL;
      case OPT_RAMPSPD:
        EEPROM.update(RAMPSPEED_ADDR, rampTimeS);  // Save setting before exiting
        myEnc.write(0);
        return MANUAL;
      case OPT_BEEP:
        return MANUAL;
      case OPT_PRES:
        myEnc.write(0);
        return MANUAL;
      case AUTO_RELEASE:
        // Jump to the Edges configuration menu
        myEnc.write((targetEdges - 2) * 2);
        return OPT_EDGES;
      case OPT_EDGES:
        myEnc.write(0);
        return MANUAL;
      case OPT_VARIANCE:
        myEnc.write(0);
        return MANUAL;
      case OPT_RELDUR:
        myEnc.write(0);
        return MANUAL;
    }
  } else return MANUAL;
}

//=======Main Loop=============================
void loop() {
  static uint8_t state = MANUAL;
  static int sampleTick = 0;
  //Run this section at the update frequency (default 60 Hz)
  if (millis() % period == 0) {
    delay(1);

    sampleTick++;  //Add pressure samples to the running average slower than 60Hz
    if (sampleTick % RA_TICK_PERIOD == 0) {
      raPressure.addValue(pressure);
      avgPressure = raPressure.getAverage();
    }

    pressure = 0;
    for (uint8_t i = OVERSAMPLE; i; --i) {
      pressure += analogRead(BUTTPIN);
      if (i) {     // Don't delay after the last sample
        delay(1);  // Spread samples out a little
      }
    }
    fadeToBlackBy(leds, NUM_LEDS, 20);  //Create a fading light effect. LED buffer is not otherwise cleared
    uint8_t btnState = check_button();
    state = set_state(btnState, state);  //Set the next state based on this state and button presses
    run_state_machine(state);
    FastLED.show();  //Update the physical LEDs to match the buffer in software

    //Alert that the Pressure voltage amplifier is railing, and the trim pot needs to be adjusted
    if (pressure > 4030) beep_motor(2093, 2093, 2093);  //Three high beeps

    //Report pressure and motor data over USB for analysis / other uses. timestamps disabled by default
    //Serial.print(millis()); //Timestamp (ms)
    //Serial.print(",");
    Serial.print(motSpeed);  //Motor speed (0-255)
    Serial.print(",");
    Serial.print(pressure);  //(Original ADC value - 12 bits, 0-4095)
    Serial.print(",");
    Serial.println(avgPressure);  //Running average of (default last 25 seconds) pressure
  }
}
