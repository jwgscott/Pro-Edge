// Pro-Edge (a Protogasm fork) build 20260722.3
// (build backup before major rework on modes -> combining different auto modes into one with diff. settings)
/**
 * =======================================================================================
 * PRO-EDGE VIBRATOR CONTROLLER & EDGING SYSTEM
 * =======================================================================================
 * Drives a vibrator and utilizes real-time changes in the internal pressure of an 
 * inflatable plug to estimate a user's closeness to climax, automatically throttling 
 * or cutting power to manage edging sessions or facilitate controlled orgasms.
 * 
 * INTERFACE & NAVIGATION:
 * A 60Hz state machine manages different operating modes and configuration menus. 
 * The system is navigated via a rotary encoder with an integrated push-button.
 * Modes are identified by the color of the LED cursor on the 24-LED ring.
 * 
 *  - Short Press (<600ms): Cycles forward through main modes or sequential menus.
 *  - Long Press (600ms - 2.5s): Jumps from a main mode to its configuration menu, or exits.
 *  - Very Long Press (>2.5s): Suspends device operation (Standby/Off).
 * 
 * MAIN OPERATING MODES:
 * [Red]     Manual Mode: Direct, analog control over the vibrator speed.
 * [Blue]    Auto Abrupt: Edging mode. Motor ramps up over a defined time. If the pressure 
 *           limit is breached, power cuts abruptly to 0 for a cool-down period.
 * [Cyan]    Auto Smooth: Proportional edging mode. As pressure nears the limit, the motor 
 *           smoothly and proportionally throttles down to keep the user "riding the edge".
 * [Magenta] Auto-Release: Target-oriented edging mode. Counts valid edges (crossing the 
 *           pressure limit) using a 15-second hardware debounce timer. Features a deceptive 
 *           LED visualizer (Pink cursor moving toward a Gold target) and a hidden "Roulette" 
 *           variance to randomize the required edges per cycle. Once the hidden target is 
 *           met, triggers a maximum-speed release phase.
 * 
 * CONFIGURATION MENUS (Accessed via Long Press):
 * [Green]   Max Speed: Sets the ceiling speed for all automatic ramping modes.
 * [Orange]  Ramp Speed: Sets how long (5-120s) the motor takes to reach Max Speed.
 * [Magenta] Target Edges: Sets the baseline edge requirement for Auto-Release mode (2-48).
 * [Cyan]    Edge Variance: Sets the +/- roulette variance (0-6) applied to the target edges.
 * [Yellow]  Release Duration: Sets the duration (2-48s) of the final climax vibration.
 * [White]   Debug Mode: Maps raw ADC sensor data to the LEDs for hardware troubleshooting.
 * 
 * All settings are persistently saved to the EEPROM and survive power cycles.
 * =======================================================================================
 */

// ======= Libraries ===============================
#include <Encoder.h>
#include <EEPROM.h>
#include "FastLED.h"
#include "RunningAverage.h"

// ======= Hardware Setup & Constants ===============================

// LEDs
#define NUM_LEDS 24
#define LED_PIN 10
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
#define BRIGHTNESS 50  // Limits total current drawn by the LEDs to prevent overheating

// Encoder
#define ENC_SW 5  // Pushbutton integrated into the rotary encoder
#define ENC_SW_UP HIGH
#define ENC_SW_DOWN LOW
Encoder myEnc(3, 2);  // Quadrature inputs on hardware interrupt pins 2 and 3

// Motor
#define MOTPIN 9
#define MOT_MAX 255  // Absolute maximum PWM value for motor
#define MOT_MIN 20   // Minimum PWM threshold required to prevent the motor from stalling

// Pressure Sensor Analog In
#define BUTTPIN A0
#define OVERSAMPLE 4  // 4x oversampling keeps 10-bit ADC scaling similar to 12-bit Teensy ADC
#define ADC_MAX 1023
#define PRESSURE_RAIL_LIMIT 4030  // Threshold (approx 4095) for sensor railing/clipping alert

// ======= Timing & Software Options ===============================

#define FREQUENCY 60               // Primary state machine update frequency in Hz
#define PERIOD (1000 / FREQUENCY)  // Update/render period in milliseconds (approx 16.6ms)

// Button press thresholds
#define LONG_PRESS_MS 600     // Milliseconds required to trigger option menus
#define V_LONG_PRESS_MS 2500  // Milliseconds required to trigger sleep/power off

// Running pressure average
#define RA_HIST_SECONDS 25
#define RA_FREQUENCY 6
#define RA_TICK_PERIOD (FREQUENCY / RA_FREQUENCY)
RunningAverage raPressure(RA_FREQUENCY* RA_HIST_SECONDS);

// Auto-Release timing constants
// #define EDGE_COOLDOWN_MS 15000UL  // Hardware debounce: 15 seconds required between valid counted edges
// Old fixed timeout: 15 seconds; now random between 8-20 seconds.

// ======= State Machine Modes ===============================

#define MANUAL 1
#define AUTO 2
#define OPT_SPEED 3
#define OPT_RAMPSPD 4
#define OPT_BEEP 5
#define OPT_PRES 6
#define AUTO_SMOOTH 7
#define AUTO_RELEASE 8
#define OPT_EDGES 9
#define OPT_RELDUR 10
#define OPT_VARIANCE 11

// Button states
#define BTN_NONE 0
#define BTN_SHORT 1
#define BTN_LONG 2
#define BTN_V_LONG 3

// ======= EEPROM Addresses ===============================
// Allocates memory slots for persistent setting retention

#define BEEP_ADDR 1
#define MAX_SPEED_ADDR 2
#define SENSITIVITY_ADDR 3
#define TARGET_EDGES_ADDR 4
#define RELEASE_DUR_ADDR 5
#define RAMPSPEED_ADDR 6
#define VARIANCE_ADDR 7

// ======= Global State & Settings ===============================

CRGB leds[NUM_LEDS];
uint8_t currentState = MANUAL;
uint8_t previousMode = MANUAL;  // Memory tracker for menu returns

// Pressure & Speed State
int pressure = 0;
int avgPressure = 0;  // Running 25-second average to establish resting baseline
int pLimit = 600;     // Dynamic pressure delta limit before vibrator cuts off
int sensitivity = 0;  // Orgasm detection sensitivity (pulse count, persists across states)
int maxSpeed = 255;   // Max automatic ramp-up speed limit
float motSpeed = 0;   // Current motor speed (uses float for smooth  ramping)
int rampTimeS = 30;   // Ramp-up time in seconds

// Auto-Release State Variables
int targetEdges = 4;        // Baseline configured number of edges required
int actualTargetEdges = 4;  // The actual, hidden target with the variance applied
int currentEdges = 0;       // Number of edges successfully accumulated in the current session
int edgeVariance = 0;       // +/- variance parameter for target randomization
int releaseDurationS = 16;  // How long the climax phase runs at max speed
bool isReleasing = false;   // Boolean flag locking the state machine into the release phase
unsigned long releaseStartTime = 0;
unsigned long lastEdgeTime = 0;             // Timestamp tracker for the 15-second debounce window
bool wasOverLimit = false;                  // Clench debouncer (prevents single clench from counting rapidly)
unsigned long currentCooldownMS = 15000UL;  // Dynamic cooldown tracker

// ======= Functions ===============================

/**
 * @brief Plays a 3-tone sequence on the motor using PWM frequency modulation.
 * @param f1 First frequency in Hz
 * @param f2 Second frequency in Hz
 * @param f3 Third frequency in Hz
 */
void beep_motor(int f1, int f2, int f3) {
  analogWrite(MOTPIN, 0);  // Halt active vibration temporarily
  tone(MOTPIN, f1);
  delay(250);
  tone(MOTPIN, f2);
  delay(250);
  tone(MOTPIN, f3);
  delay(250);
  noTone(MOTPIN);
  analogWrite(MOTPIN, (int)motSpeed);  // Resume previous vibration state
}

/**
 * @brief Initial hardware setup, EEPROM loading, Timer1 configuration, and state preparation.
 */
void setup() {
  pinMode(ENC_SW, INPUT);
  digitalWrite(ENC_SW, HIGH);  // Enable internal pullup resistor for the button

  analogReference(EXTERNAL);

  // CONFIGURE TIMER1 FOR HIGH-FREQUENCY PWM
  // Classic AVR Arduinos use 490Hz PWM which causes audible motor whine.
  // Clearing CS11 & CS12 and setting CS10 changes the prescaler to 1, achieving ~31.4 kHz.
  TCCR1B &= ~((1 << CS11) | (1 << CS12));
  TCCR1B |= (1 << CS10);

  pinMode(MOTPIN, OUTPUT);
  pinMode(BUTTPIN, INPUT);

  raPressure.clear();
  digitalWrite(MOTPIN, LOW);  // Ensure motor is strictly off during boot sequence

  delay(3000);  // 3-second safety recovery delay
  Serial.begin(115200);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);

  // LOAD SETTINGS FROM EEPROM (With fallback defaults if memory is blank/255)
  // Note: Sensitivity is stored divided by 4 to fit within the 8-bit (max 255) EEPROM limit,
  // as the maximum possible encoder pulse value is 284. It is multiplied back upon reading.
  sensitivity = EEPROM.read(SENSITIVITY_ADDR) * 4;
  maxSpeed = min((int)EEPROM.read(MAX_SPEED_ADDR), MOT_MAX);

  targetEdges = EEPROM.read(TARGET_EDGES_ADDR);
  if (targetEdges == 0 || targetEdges == 255) targetEdges = 4;
  actualTargetEdges = targetEdges;  // Initialize hidden target to match baseline on startup

  releaseDurationS = EEPROM.read(RELEASE_DUR_ADDR);
  if (releaseDurationS == 0 || releaseDurationS == 255) releaseDurationS = 16;

  rampTimeS = EEPROM.read(RAMPSPEED_ADDR);
  if (rampTimeS < 1 || rampTimeS > 24) rampTimeS = 10;

  edgeVariance = EEPROM.read(VARIANCE_ADDR);
  if (edgeVariance == 255) edgeVariance = 0;

  randomSeed(analogRead(A1));               // Seed random generator using background noise from unconnected pin
  currentCooldownMS = random(8000, 20001);  // Roll the initial randomized edge cooldown (auto_release) between 10-20 seconds
  beep_motor(1047, 1396, 2093);             // Play rising power-on beep
}

/**
 * @brief Draws a single cursor pixel for visual feedback on the LED ring.
 */
void draw_cursor(int pos, CRGB C1) {
  pos = constrain(pos, 0, NUM_LEDS - 1);
  leds[pos] = C1;
}

/**
 * @brief Draws a cursor wrapped over 3 virtual revolutions of the LED ring (For high-resolution tracking).
 */
void draw_cursor_3(int pos, CRGB C1, CRGB C2, CRGB C3) {
  pos = constrain(pos, 0, NUM_LEDS * 3 - 1);
  int colorNum = pos / NUM_LEDS;
  int cursorPos = pos % NUM_LEDS;

  if (colorNum == 0) leds[cursorPos] = C1;
  else if (colorNum == 1) leds[cursorPos] = C2;
  else if (colorNum == 2) leds[cursorPos] = C3;
}

/**
 * @brief Draws filled gradient bars wrapped over 3 virtual revolutions (Used for pressure display).
 */
void draw_bars_3(int pos, CRGB C1, CRGB C2, CRGB C3) {
  pos = constrain(pos, 0, NUM_LEDS * 3 - 1);
  int colorNum = pos / NUM_LEDS;
  int barPos = pos % NUM_LEDS;

  if (colorNum == 0) fill_gradient_RGB(leds, 0, C1, barPos, C1);
  else if (colorNum == 1) fill_gradient_RGB(leds, 0, C1, barPos, C2);
  else if (colorNum == 2) fill_gradient_RGB(leds, 0, C2, barPos, C3);
}

/**
 * @brief Reads encoder and limits output based on tactile physical clicks.
 * Because standard quadrature encoders emit 4 pulses per physical detent (click), 
 * this normalizes the math so 1 click = 1 logical step.
 */
int encLimitRead(int minVal, int maxVal) {
  long rawRead = myEnc.read();
  // Force encoder position back within physical bounds if rotated too far
  if (rawRead > maxVal * 4) myEnc.write(maxVal * 4);
  else if (rawRead < minVal * 4) myEnc.write(minVal * 4);

  return constrain(myEnc.read() / 4, minVal, maxVal);
}

// ======= Mode Implementations ===============================

/**
 * @brief [RED] Manual Control Mode. Maps knob directly to motor PWM output.
 */
void run_manual() {
  int knob = encLimitRead(0, NUM_LEDS - 1);

  // Uses manual float casting to ensure perfectly accurate scaling, bypassing Arduino's integer-only map()
  motSpeed = ((float)knob / (float)(NUM_LEDS - 1)) * (float)MOT_MAX;
  analogWrite(MOTPIN, motSpeed);

  // Background visualizer still shows real-time pressure spikes
  int presDraw = map(constrain(pressure - avgPressure, 0, pLimit), 0, pLimit, 0, NUM_LEDS * 3);
  draw_bars_3(presDraw, CRGB::DarkGreen, CRGB::DarkOrange, CRGB::Red);
  draw_cursor(knob, CRGB::Red);
}

/**
 * @brief [AMBER] Abrupt Auto Mode. Linear ramp-up with instant shutoff upon limit breach.
 */
void run_auto() {
  static float motIncrement = 0.0;
  // Calculate exact float increment required to reach maxSpeed over rampTimeS at 60Hz
  motIncrement = ((float)maxSpeed / ((float)FREQUENCY * (float)rampTimeS));

  int knob = encLimitRead(0, (3 * NUM_LEDS) - 1);
  sensitivity = knob * 4;
  pLimit = map(knob, 0, 3 * (NUM_LEDS - 1), 600, 1);

  if (pressure - avgPressure > pLimit) {
    // Cutoff: Apply a negative mathematical delay equal to half the ramp up time
    motSpeed = -0.5 * (float)rampTimeS * ((float)FREQUENCY * motIncrement);
  } else if (motSpeed < (float)maxSpeed) {
    motSpeed += motIncrement;
  }

  // Ternary operator prevents motor stall by enforcing MOT_MIN while active
  analogWrite(MOTPIN, motSpeed > MOT_MIN ? (int)motSpeed : 0);

  int presDraw = map(constrain(pressure - avgPressure, 0, pLimit), 0, pLimit, 0, NUM_LEDS * 3);
  draw_bars_3(presDraw, CRGB::DarkGreen, CRGB::DarkOrange, CRGB::Red);
  draw_cursor_3(knob, CRGB::OrangeRed, CRGB::DarkOrange, CRGB::Yellow);
}

/**
 * @brief [ICY BLUE] Smooth Auto Mode. Speed proportionally throttles down as pressure approaches the limit.
 */
void run_auto_smooth() {
  static float motIncrement = 0.0;
  motIncrement = ((float)maxSpeed / ((float)FREQUENCY * (float)rampTimeS));

  int knob = encLimitRead(0, (3 * NUM_LEDS) - 1);
  sensitivity = knob * 4;
  pLimit = map(knob, 0, 3 * (NUM_LEDS - 1), 600, 1);

  int delta = pressure - avgPressure;

  if (delta > pLimit) {
    motSpeed = 0;  // Total breach: drop speed immediately
  } else if (delta > 0) {
    // Proportional Deceleration Logic
    // Calculates how close (in %) the pressure is to the absolute limit
    float throttle = (float)delta / (float)pLimit;
    float targetSpeed = maxSpeed * (1.0 - throttle);

    if (motSpeed > targetSpeed) {
      // Actively brake/reduce speed 15x faster than ramp-up if currently over the proportional target
      motSpeed -= (motIncrement * 15);
      if (motSpeed < targetSpeed) motSpeed = targetSpeed;
    } else if (motSpeed < targetSpeed && motSpeed < maxSpeed) {
      motSpeed += motIncrement;
    }
  } else {
    // No significant pressure: continue normal ramp up
    if (motSpeed < (float)maxSpeed) motSpeed += motIncrement;
  }

  analogWrite(MOTPIN, motSpeed > MOT_MIN ? (int)motSpeed : 0);

  int presDraw = map(constrain(delta, 0, pLimit), 0, pLimit, 0, NUM_LEDS * 3);
  draw_bars_3(presDraw, CRGB::DarkGreen, CRGB::DarkOrange, CRGB::Red);
  draw_cursor_3(knob, CRGB::Aquamarine, CRGB::Cyan, CRGB::Blue);
}

/**
 * @brief [MAGENTA] Auto-Release Mode. Counts valid proportional edges and executes a timed climax phase.
 */
void run_auto_release() {
  static float motIncrement = 0.0;
  motIncrement = ((float)maxSpeed / ((float)FREQUENCY * (float)rampTimeS));

  int knob = encLimitRead(0, (3 * NUM_LEDS) - 1);
  sensitivity = knob * 4;
  pLimit = map(knob, 0, 3 * (NUM_LEDS - 1), 600, 1);

  // 1. ACTIVE RELEASE PHASE LOGIC
  if (isReleasing) {
    analogWrite(MOTPIN, maxSpeed);  // Force absolute max speed

    // Conclude release phase if duration timer has expired (resumes the edging session, next release after set target edges)
    if (millis() - releaseStartTime > ((unsigned long)releaseDurationS * 1000UL)) {
      isReleasing = false;
      currentEdges = 0;
      motSpeed = 0;
    }

    // Strobe LEDs white to indicate release phase
    fill_solid(leds, NUM_LEDS, CRGB::White);
    fadeToBlackBy(leds, NUM_LEDS, (millis() % 100 > 50) ? 100 : 0);
    return;  // Bypass the rest of the edge logic
  }

  // 2. SMOOTH EDGE COUNTING & DEBOUNCE LOGIC
  int delta = pressure - avgPressure;

  if (delta > pLimit) {
    if (!wasOverLimit) {
      wasOverLimit = true;

      // Check against the newly randomized dynamic cooldown
      if (millis() - lastEdgeTime > currentCooldownMS || currentEdges == 0) {

        // --- VALID EDGE ACHIEVED ---
        currentEdges++;
        lastEdgeTime = millis();

        // Roll a new random cooldown for the next edge (8 to 20 seconds)
        currentCooldownMS = random(8000, 20001);

        if (currentEdges >= actualTargetEdges) {
          isReleasing = true;
          releaseStartTime = millis();

          actualTargetEdges = targetEdges + random(-edgeVariance, edgeVariance + 1);
          if (actualTargetEdges < 1) actualTargetEdges = 1;
          return;
        }
      } else {
        // --- STRICT PENALTY LOGIC & ANIMATION ---
        lastEdgeTime = millis();

        // Re-roll the cooldown to keep the user guessing after a failure
        currentCooldownMS = random(8000, 20001);

        // Force motor off instantly
        motSpeed = 0;
        analogWrite(MOTPIN, 0);

        // Execute Blocking Penalty Animation (~900ms visual timeout)
        for (int b = 0; b < 3; b++) {
          fill_solid(leds, NUM_LEDS, CRGB::Black);

          // Draw 2 pairs of 3 LEDs equally spaced (Positions 0,1,2 and 12,13,14)
          for (int i = 0; i < 3; i++) {
            leds[i] = CRGB::Red;
            leds[i + 12] = CRGB::Red;
          }
          FastLED.show();
          delay(150);

          fill_solid(leds, NUM_LEDS, CRGB::Black);
          FastLED.show();
          delay(150);
        }
      }
    }
    motSpeed = 0;  // Ensure power stays cut while limit is breached
  } else if (delta > 0) {

    // --- ANTI-CHEAT RE-ARM LOGIC ---
    // The user must relax the pressure to less than 25% of the limit to re-arm the edge detector.
    // Small muscle tremors near the limit will no longer bypass the system.
    if (delta < (pLimit / 4)) {
      wasOverLimit = false;
    }

    float throttle = (float)delta / (float)pLimit;
    float targetSpeed = maxSpeed * (1.0 - throttle);

    if (motSpeed > targetSpeed) {
      motSpeed -= (motIncrement * 15);
      if (motSpeed < targetSpeed) motSpeed = targetSpeed;
    } else if (motSpeed < targetSpeed && motSpeed < maxSpeed) {
      motSpeed += motIncrement;
    }
  } else {
    // delta <= 0 (User is fully relaxed at or below resting average)
    wasOverLimit = false;
    if (motSpeed < (float)maxSpeed) motSpeed += motIncrement;
  }

  analogWrite(MOTPIN, motSpeed > MOT_MIN ? (int)motSpeed : 0);

  // 3. PROGRESS VISUALIZER RENDERING

  // Background: Real-time pressure
  int presDraw = map(constrain(delta, 0, pLimit), 0, pLimit, 0, NUM_LEDS * 3);
  draw_bars_3(presDraw, CRGB::DarkGreen, CRGB::DarkOrange, CRGB::Red);

  // Progress: Map current edges against the BASELINE target to hide the active variance.
  // Constrain ensures the cursor stops at the Gold LED if the user is forced into "Overtime"
  int visualProgress = constrain(map(currentEdges, 0, targetEdges, 0, NUM_LEDS - 1), 0, NUM_LEDS - 1);
  leds[NUM_LEDS - 1] = CRGB::Gold;     // Fixed baseline target at the end of the ring
  leds[visualProgress] = CRGB::White;  // Advancing progress cursor

  // Foreground: Sensitivity knob position
  draw_cursor_3(knob, CRGB::Purple, CRGB::MediumVioletRed, CRGB::HotPink);
}

// ======= Option Menu Rendering Functions =======
// These modes halt the motor and map the encoder to specific setting ranges

void run_opt_edges() {
  int knob = encLimitRead(0, NUM_LEDS - 1);
  targetEdges = (knob * 2) + 2;  // Maps 0-23 to 2-48 edges in increments of 2

  // Synchronize the hidden target with the menu setting to prevent the old value persisting in the current session.
  actualTargetEdges = targetEdges;

  analogWrite(MOTPIN, 0);
  draw_bars_3(knob, CRGB::Magenta, CRGB::Magenta, CRGB::Magenta);
  draw_cursor(knob, CRGB::White);
}

void run_opt_variance() {
  int knob = encLimitRead(0, 6);  // Hard limit to max variance of 6
  edgeVariance = knob;
  analogWrite(MOTPIN, 0);
  // Expand the 0-6 variance evenly across the 24 LEDs for visual clarity
  int displayPos = map(edgeVariance, 0, 6, 0, NUM_LEDS - 1);
  draw_bars_3(displayPos, CRGB::DeepSkyBlue, CRGB::DeepSkyBlue, CRGB::DeepSkyBlue);
  draw_cursor(displayPos, CRGB::White);
}

void run_opt_reldur() {
  int knob = encLimitRead(0, NUM_LEDS - 1);
  releaseDurationS = (knob * 2) + 2;  // Maps 0-23 to 2-48 seconds in increments of 2
  analogWrite(MOTPIN, 0);
  draw_bars_3(knob, CRGB::Gold, CRGB::Gold, CRGB::Gold);
  draw_cursor(knob, CRGB::White);
}

void run_opt_speed() {
  int knob = encLimitRead(0, NUM_LEDS - 1);

  // Float interpolation for smooth accuracy
  motSpeed = ((float)knob / (float)(NUM_LEDS - 1)) * (float)MOT_MAX;
  analogWrite(MOTPIN, motSpeed);
  maxSpeed = motSpeed;

  // Static display: Lime Green bars up to the White cursor
  draw_bars_3(knob, CRGB::Lime, CRGB::Lime, CRGB::Lime);
  draw_cursor(knob, CRGB::White);
}

void run_opt_rampspd() {
  int knob = encLimitRead(0, NUM_LEDS - 1);
  rampTimeS = knob + 1;  // Maps 0-23 to 1-24 seconds

  // --- 1. TIME CYCLE CALCULATION ---
  // Calculate the total duration of one full ramp in milliseconds
  unsigned long cycleDurationMS = (unsigned long)rampTimeS * 1000UL;

  // Use modulo to find the exact millisecond position within the current repeating cycle
  unsigned long currentCyclePosition = millis() % cycleDurationMS;

  // --- 2. PHYSICAL MOTOR DEMONSTRATION ---
  // Calculate the motor speed proportionally based on the time elapsed in the cycle
  motSpeed = ((float)currentCyclePosition / (float)cycleDurationMS) * (float)maxSpeed;

  // Output to motor, enforcing the minimum stall threshold
  analogWrite(MOTPIN, motSpeed > MOT_MIN ? (int)motSpeed : 0);

  // --- 3. SYNCHRONIZED VISUAL DEMONSTRATION ---
  // Map the exact same cycle time directly to the LED ring to match the physical vibration
  int rampPos = map(currentCyclePosition, 0, cycleDurationMS, 0, knob);

  draw_bars_3(rampPos, CRGB::Lime, CRGB::Lime, CRGB::Lime);
  draw_cursor(knob, CRGB::White);
}

void run_opt_beep() {
  // Beep/Brightness implementation pending architecture
}

void run_opt_pres() {
  // Diagnostic Mode: Maps raw sensor data straight to LEDs
  int p = map(analogRead(BUTTPIN), 0, ADC_MAX, 0, NUM_LEDS - 1);
  draw_bars_3(p, CRGB::DimGray, CRGB::DimGray, CRGB::DimGray);
  draw_cursor(p, CRGB::White);
}

/**
 * @brief Polls the encoder button to register short, long, or very long presses on key-up.
 * @return Button state macro (BTN_NONE, BTN_SHORT, BTN_LONG, BTN_V_LONG).
 */
uint8_t check_button() {
  static bool lastBtn = ENC_SW_DOWN;
  static unsigned long keyDownTime = 0;
  uint8_t btnState = BTN_NONE;
  bool thisBtn = digitalRead(ENC_SW);

  // Trigger timer on key-down
  if (thisBtn == ENC_SW_DOWN && lastBtn == ENC_SW_UP) {
    keyDownTime = millis();
  }
  // Evaluate duration on key-up
  else if (thisBtn == ENC_SW_UP && lastBtn == ENC_SW_DOWN) {
    unsigned long holdTime = millis() - keyDownTime;
    if (holdTime >= V_LONG_PRESS_MS) btnState = BTN_V_LONG;
    else if (holdTime >= LONG_PRESS_MS) btnState = BTN_LONG;
    else btnState = BTN_SHORT;
  }

  lastBtn = thisBtn;
  return btnState;
}

/**
 * @brief Routes main loop execution to the currently active mode function.
 */
void run_state_machine(uint8_t state) {
  switch (state) {
    case MANUAL: run_manual(); break;
    case AUTO: run_auto(); break;
    case AUTO_SMOOTH: run_auto_smooth(); break;
    case AUTO_RELEASE: run_auto_release(); break;
    case OPT_EDGES: run_opt_edges(); break;
    case OPT_VARIANCE: run_opt_variance(); break;
    case OPT_RELDUR: run_opt_reldur(); break;
    case OPT_SPEED: run_opt_speed(); break;
    case OPT_RAMPSPD: run_opt_rampspd(); break;
    case OPT_BEEP: run_opt_beep(); break;
    case OPT_PRES: run_opt_pres(); break;
    default: run_manual(); break;
  }
}

/**
 * @brief Determines the next state based on button presses, saves current data to EEPROM, 
 * and mathematically pre-sets the encoder knob position for the incoming mode.
 */
uint8_t set_state(uint8_t btnState, uint8_t state) {
  if (btnState == BTN_NONE) return state;

  if (btnState == BTN_V_LONG) {
    // POWER-OFF SLEEP SEQUENCE
    fill_gradient_RGB(leds, 0, CRGB::Black, NUM_LEDS - 1, CRGB::Black);
    FastLED.show();
    beep_motor(2093, 1396, 1047);  // Descending sleep tone
    analogWrite(MOTPIN, 0);

    // Halt microcontroller execution indefinitely until button is clicked
    while (digitalRead(ENC_SW) == ENC_SW_UP) delay(1);    // Wait for wake press
    while (digitalRead(ENC_SW) == ENC_SW_DOWN) delay(1);  // Wait for button release (Debounce)

    beep_motor(1047, 1396, 2093);  // Ascending wake tone

    // Forces motor speed and encoder value to 0 to prevent unwanted activation of the vibrator after leaving standby.
    motSpeed = 0;
    myEnc.write(0);

    return MANUAL;
  }

  if (btnState == BTN_SHORT) {
    switch (state) {
      // --- MAIN MODE CYCLING ---
      case MANUAL:
        myEnc.write(sensitivity);
        motSpeed = 0;
        return AUTO;
      case AUTO:
        myEnc.write(sensitivity);
        motSpeed = 0;
        EEPROM.update(SENSITIVITY_ADDR, sensitivity / 4);
        return AUTO_SMOOTH;
      case AUTO_SMOOTH:
        myEnc.write(sensitivity);
        motSpeed = 0;
        EEPROM.update(SENSITIVITY_ADDR, sensitivity / 4);
        return AUTO_RELEASE;
      case AUTO_RELEASE:
        myEnc.write(0);
        motSpeed = 0;
        currentEdges = 0;
        isReleasing = false;  // Safely cancel active release if manually exited
        EEPROM.update(SENSITIVITY_ADDR, sensitivity / 4);
        return MANUAL;

      // --- MENU CYCLING (SPEED SETTINGS) ---
      case OPT_SPEED:
        EEPROM.update(MAX_SPEED_ADDR, maxSpeed);
        // Pre-set encoder knob. Formula reverses the mapping calculation: ((Value - Min) / Increment) * Pulses
        myEnc.write((rampTimeS - 1) * 4);
        return OPT_RAMPSPD;
      case OPT_RAMPSPD:
        EEPROM.update(RAMPSPEED_ADDR, rampTimeS);
        motSpeed = 0;
        analogWrite(MOTPIN, motSpeed);
        myEnc.write(0);
        return OPT_PRES;
      case OPT_BEEP:
        myEnc.write(0);
        return OPT_PRES;
      case OPT_PRES:
        myEnc.write(map(maxSpeed, 0, 255, 0, 4 * (NUM_LEDS - 1)));
        return OPT_SPEED;

      // --- MENU CYCLING (AUTO-RELEASE SETTINGS) ---
      case OPT_EDGES:
        EEPROM.update(TARGET_EDGES_ADDR, targetEdges);
        myEnc.write(edgeVariance * 4);  // Re-map 0-6 variance directly to encoder pulses
        return OPT_VARIANCE;
      case OPT_VARIANCE:
        EEPROM.update(VARIANCE_ADDR, edgeVariance);
        myEnc.write((releaseDurationS - 2) * 2);
        return OPT_RELDUR;
      case OPT_RELDUR:
        EEPROM.update(RELEASE_DUR_ADDR, releaseDurationS);
        // Chain back into standard speed settings
        myEnc.write(map(maxSpeed, 0, 255, 0, 4 * (NUM_LEDS - 1)));
        return OPT_SPEED;
    }
  }

  if (btnState == BTN_LONG) {
    switch (state) {
      // ENTERING MENUS FROM MAIN MODES
      case MANUAL:
      case AUTO:
      case AUTO_SMOOTH:
        previousMode = state;  // Save the active mode to memory
        myEnc.write(map(maxSpeed, 0, 255, 0, 4 * (NUM_LEDS - 1)));
        return OPT_SPEED;
      case AUTO_RELEASE:
        previousMode = state;  // Save the active mode to memory
        myEnc.write((targetEdges - 2) * 2);
        return OPT_EDGES;

      // EXITING MENUS TO MAIN MODE
      case OPT_SPEED:
      case OPT_BEEP:
      case OPT_PRES:
      case OPT_EDGES:
      case OPT_VARIANCE:
      case OPT_RELDUR:
        // Restore correct encoder position based on return mode
        if (previousMode == MANUAL) {
          myEnc.write(0);
        } else {
          myEnc.write(sensitivity);
        }
        motSpeed = 0;         // Ensure motor drops safely when returning
        return previousMode;  // Return to saved mode

      case OPT_RAMPSPD:
        EEPROM.update(RAMPSPEED_ADDR, rampTimeS);
        if (previousMode == MANUAL) {
          myEnc.write(0);
        } else {
          myEnc.write(sensitivity);
        }
        motSpeed = 0;
        analogWrite(MOTPIN, 0);  // Safety cutoff
        return previousMode;     // Return to saved mode
    }
  }
  return MANUAL;
}

// ======= Main Loop ===============================

/**
 * @brief Main execution loop handling sensor sampling, hardware updating, and USB telemetry.
 */
void loop() {
  static int sampleTick = 0;

  // Throttle core loop execution to desired FREQUENCY (60Hz)
  if (millis() % PERIOD == 0) {
    delay(1);

    // Update running average slower than full execution loop to create a stable baseline
    if (++sampleTick % RA_TICK_PERIOD == 0) {
      raPressure.addValue(pressure);
      avgPressure = raPressure.getAverage();
    }

    // Acquire and accumulate pressure samples
    pressure = 0;
    for (uint8_t i = OVERSAMPLE; i; --i) {
      pressure += analogRead(BUTTPIN);
      if (i > 1) delay(1);  // Spread samples out slightly for cleaner ADC reads
    }

    fadeToBlackBy(leds, NUM_LEDS, 20);  // Clear buffer slightly to create light trail effect

    // Process state machine
    uint8_t btnState = check_button();
    currentState = set_state(btnState, currentState);
    run_state_machine(currentState);

    FastLED.show();

    // Safety Alert: Pressure voltage amplifier is railing/clipping
    if (pressure > PRESSURE_RAIL_LIMIT) beep_motor(2093, 2093, 2093);

    // USB Serial Telemetry for diagnostic tracking
    Serial.print(motSpeed);
    Serial.print(",");
    Serial.print(pressure);
    Serial.print(",");
    Serial.println(avgPressure);
  }
}