/*
 * RC Excavator Serial Controller
 * ==============================
 *
 * Target hardware:
 *   - Arduino Mega 2560
 *   - RadioLink R12DS 2.4 GHz receiver (on excavator; PWM outputs emulate its channels)
 *   - HC-05 Bluetooth module (wireless serial commands)
 *
 * The Mega drives PWM outputs that mimic the R12DS receiver signals, allowing
 * automated control from a computer over USB or from a phone/PC over Bluetooth.
 * Each motion runs for a user-specified duration (ms), then returns to neutral.
 *
 * Command format (9600 baud on USB Serial and HC-05 Serial2):
 *   [PART][ACTION][DURATION_MS]
 *
 *   PART     - 2 letters: BU, MI, MN, BR, MO
 *   ACTION   - 2 letters: OP, CL, FW, BW, CW, CC (depends on part)
 *   DURATION - positive integer milliseconds (e.g. 500, 2000)
 *
 * Examples:
 *   BUOP2000   Open bucket for 2 seconds
 *   MNCL1500   Lower main arm (boom) for 1.5 seconds
 *   MOFW3000   Drive forward for 3 seconds
 *   Trial      Run built-in demonstration of all subsystems
 *
 * Mega 2560 pin map (PWM / actuator outputs):
 *   Pin  2  - Bucket channel
 *   Pin  3  - Main arm (boom) channel
 *   Pin  4  - Middle arm (stick) channel
 *   Pin  5  - Body rotation (cab swing) channel
 *   Pin  7  - Hydraulic pump / shared enable channel
 *   Pin  8  - Left track motor channel
 *   Pin  9  - Right track motor channel
 *   Pin 32  - Digital auxiliary output (enable / relay — verify your wiring)
 *   A0      - Analog input (reserved for sensors)
 *
 * HC-05 wiring (Serial2):
 *   Mega TX2 (pin 16) -> HC-05 RX  (use 1 kΩ / 2 kΩ divider if module is 3.3 V)
 *   Mega RX2 (pin 17) <- HC-05 TX
 *   GND common between Mega, HC-05, and excavator control ground
 *
 * USB debug: Serial  (pins 0/1, via USB)
 *
 * Derived from prior project sketches (Serial_First, Serial_First2, BIM-day-show).
 * PWM values were empirically tuned for this specific excavator hardware.
 */

#include <Arduino.h>

// -----------------------------------------------------------------------------
// Hardware pin assignments — Arduino Mega 2560
// -----------------------------------------------------------------------------

const int PIN_BUCKET       = 2;
const int PIN_MAIN_ARM     = 3;
const int PIN_MIDDLE_ARM   = 4;
const int PIN_BODY_ROTATE  = 5;
const int PIN_PUMP         = 7;   // Shared hydraulic pump / valve enable
const int PIN_TRACK_LEFT   = 8;
const int PIN_TRACK_RIGHT  = 9;
const int PIN_DIGITAL_AUX  = 32;  // Valid on Mega 2560; used in original project wiring
const int PIN_ANALOG_IN    = A0;

// HC-05 connected to hardware Serial2 (Mega TX2 = 16, RX2 = 17)
#define BT_SERIAL Serial2

// -----------------------------------------------------------------------------
// PWM setpoints (0–255). Neutral = no motion; other values drive servos/ESCs.
// These emulate the R12DS receiver channel positions. Adjust only if a joint
// or track direction is reversed or too weak on your excavator.
// -----------------------------------------------------------------------------

const int PWM_NEUTRAL_PUMP        = 170;
const int PWM_ACTIVE_PUMP         = 210;

const int PWM_BUCKET_NEUTRAL      = 200;
const int PWM_BUCKET_OPEN         = 130;
const int PWM_BUCKET_CLOSE        = 250;

const int PWM_MAIN_ARM_NEUTRAL    = 180;
const int PWM_MAIN_ARM_UP         = 250;
const int PWM_MAIN_ARM_DOWN       = 120;

const int PWM_MIDDLE_ARM_NEUTRAL  = 185;
const int PWM_MIDDLE_ARM_UP       = 120;
const int PWM_MIDDLE_ARM_DOWN     = 235;

const int PWM_BODY_NEUTRAL        = 185;
const int PWM_BODY_CW             = 135;
const int PWM_BODY_CCW            = 240;

const int PWM_TRACK_NEUTRAL       = 180;
const int PWM_TRACK_FORWARD       = 250;
const int PWM_TRACK_BACKWARD      = 115;

const int MAIN_ARM_SETTLE_MS      = 1000;  // Pause before/after boom moves
const unsigned long SERIAL_BAUD   = 9600;  // USB and HC-05 (configure HC-05 to match)

// Minimum parsed command length: 2 (part) + 2 (action) + 1 (duration digit)
const int MIN_COMMAND_LENGTH = 5;

// Trial motion durations (ms) — keep short for safe bench verification
const int TRIAL_BUCKET_MS       = 2000;
const int TRIAL_MIDDLE_ARM_MS   = 1500;
const int TRIAL_MAIN_ARM_MS     = 2000;
const int TRIAL_BODY_ROTATE_MS  = 1500;
const int TRIAL_TRACK_MS        = 1500;
const int TRIAL_PAUSE_MS        = 500;

// -----------------------------------------------------------------------------
// Serial command buffer
// -----------------------------------------------------------------------------

String incomingCommand;

// Forward declarations
bool readIncomingCommand(String& commandOut);
void processCommand(const String& command);
void runTrialSequence();
void dispatchCommand(const String& part, const String& action, int durationMs);
void setAllNeutral();
void moveBucket(const String& action, int durationMs);
void moveMiddleArm(const String& action, int durationMs);
void moveMainArm(const String& action, int durationMs);
void rotateBody(const String& action, int durationMs);
void moveTracks(const String& action, int durationMs);

// -----------------------------------------------------------------------------
// Setup
// -----------------------------------------------------------------------------

void setup() {
  Serial.begin(SERIAL_BAUD);    // USB — debug output and wired commands
  BT_SERIAL.begin(SERIAL_BAUD); // HC-05 on Serial2 (pins 16/17)

  pinMode(PIN_BUCKET, OUTPUT);
  pinMode(PIN_MAIN_ARM, OUTPUT);
  pinMode(PIN_MIDDLE_ARM, OUTPUT);
  pinMode(PIN_BODY_ROTATE, OUTPUT);
  pinMode(PIN_PUMP, OUTPUT);
  pinMode(PIN_TRACK_LEFT, OUTPUT);
  pinMode(PIN_TRACK_RIGHT, OUTPUT);
  pinMode(PIN_DIGITAL_AUX, OUTPUT);
  pinMode(PIN_ANALOG_IN, INPUT);

  setAllNeutral();

  Serial.println(F("RC Excavator Controller ready (Arduino Mega 2560)."));
  Serial.println(F("Command via USB Serial or HC-05 Bluetooth (Serial2)."));
  Serial.println(F("Format: [PART][ACTION][DURATION_MS]  e.g. BUOP2000"));
  Serial.println(F("Or send: Trial"));
}

// -----------------------------------------------------------------------------
// Main loop — accept commands from USB or HC-05 Bluetooth
// -----------------------------------------------------------------------------

void loop() {
  if (!readIncomingCommand(incomingCommand)) {
    return;
  }

  processCommand(incomingCommand);
}

// Read one line from USB Serial or HC-05 (whichever has data first)
bool readIncomingCommand(String& commandOut) {
  if (Serial.available() > 0) {
    commandOut = Serial.readStringUntil('\n');
    commandOut.trim();
    return commandOut.length() > 0;
  }

  if (BT_SERIAL.available() > 0) {
    commandOut = BT_SERIAL.readStringUntil('\n');
    commandOut.trim();
    return commandOut.length() > 0;
  }

  return false;
}

void processCommand(const String& command) {
  Serial.print(F("Received: "));
  Serial.println(command);

  if (command.equalsIgnoreCase("Trial")) {
    runTrialSequence();
    return;
  }

  if (command.length() < MIN_COMMAND_LENGTH) {
    Serial.println(F("ERROR: Command too short. Use [PART][ACTION][DURATION_MS]."));
    return;
  }

  const String part    = command.substring(0, 2);
  const String action  = command.substring(2, 4);
  const String timeStr = command.substring(4);
  const int durationMs = timeStr.toInt();

  if (durationMs <= 0) {
    Serial.println(F("ERROR: Duration must be a positive integer (milliseconds)."));
    return;
  }

  Serial.print(F("Part: "));     Serial.println(part);
  Serial.print(F("Action: "));   Serial.println(action);
  Serial.print(F("Duration: ")); Serial.print(durationMs);
  Serial.println(F(" ms"));

  dispatchCommand(part, action, durationMs);
}

// -----------------------------------------------------------------------------
// Command router
// -----------------------------------------------------------------------------

void dispatchCommand(const String& part, const String& action, int durationMs) {
  if      (part == "BU") moveBucket(action, durationMs);
  else if (part == "MI") moveMiddleArm(action, durationMs);
  else if (part == "MN") moveMainArm(action, durationMs);
  else if (part == "BR") rotateBody(action, durationMs);
  else if (part == "MO") moveTracks(action, durationMs);
  else {
    Serial.println(F("ERROR: Unknown part code. Valid: BU, MI, MN, BR, MO."));
  }
}

// -----------------------------------------------------------------------------
// Neutral position — safe resting state for all actuators
// -----------------------------------------------------------------------------

void setAllNeutral() {
  analogWrite(PIN_PUMP, PWM_NEUTRAL_PUMP);
  analogWrite(PIN_BUCKET, PWM_BUCKET_NEUTRAL);
  analogWrite(PIN_MAIN_ARM, PWM_MAIN_ARM_NEUTRAL);
  analogWrite(PIN_MIDDLE_ARM, PWM_MIDDLE_ARM_NEUTRAL);
  analogWrite(PIN_BODY_ROTATE, PWM_BODY_NEUTRAL);
  analogWrite(PIN_TRACK_LEFT, PWM_TRACK_NEUTRAL);
  analogWrite(PIN_TRACK_RIGHT, PWM_TRACK_NEUTRAL);
}

// -----------------------------------------------------------------------------
// Bucket — curl (close) and dump (open)
// Actions: OP = open, CL = close
// -----------------------------------------------------------------------------

void moveBucket(const String& action, int durationMs) {
  if (action == "OP") {
    analogWrite(PIN_PUMP, PWM_ACTIVE_PUMP);
    analogWrite(PIN_BUCKET, PWM_BUCKET_OPEN);
    delay(durationMs);
    setAllNeutral();
  } else if (action == "CL") {
    analogWrite(PIN_PUMP, PWM_ACTIVE_PUMP);
    analogWrite(PIN_BUCKET, PWM_BUCKET_CLOSE);
    delay(durationMs);
    setAllNeutral();
  } else {
    Serial.println(F("ERROR: Bucket actions are OP (open) or CL (close)."));
  }
}

// -----------------------------------------------------------------------------
// Middle arm (stick) — raise and lower
// Actions: OP = up, CL = down
// -----------------------------------------------------------------------------

void moveMiddleArm(const String& action, int durationMs) {
  if (action == "OP") {
    analogWrite(PIN_PUMP, PWM_ACTIVE_PUMP);
    analogWrite(PIN_MIDDLE_ARM, PWM_MIDDLE_ARM_UP);
    delay(durationMs);
    setAllNeutral();
  } else if (action == "CL") {
    analogWrite(PIN_PUMP, PWM_ACTIVE_PUMP);
    analogWrite(PIN_MIDDLE_ARM, PWM_MIDDLE_ARM_DOWN);
    delay(durationMs);
    setAllNeutral();
  } else {
    Serial.println(F("ERROR: Middle arm actions are OP (up) or CL (down)."));
  }
}

// -----------------------------------------------------------------------------
// Main arm (boom) — raise and lower
// Actions: OP = up, CL = down
// Includes settle delay so the pump reaches neutral before boom motion.
// -----------------------------------------------------------------------------

void moveMainArm(const String& action, int durationMs) {
  analogWrite(PIN_PUMP, PWM_NEUTRAL_PUMP);
  analogWrite(PIN_MAIN_ARM, PWM_MAIN_ARM_NEUTRAL);
  delay(MAIN_ARM_SETTLE_MS);

  if (action == "OP") {
    analogWrite(PIN_PUMP, PWM_ACTIVE_PUMP);
    analogWrite(PIN_MAIN_ARM, PWM_MAIN_ARM_UP);
    delay(durationMs);
  } else if (action == "CL") {
    analogWrite(PIN_PUMP, PWM_ACTIVE_PUMP);
    analogWrite(PIN_MAIN_ARM, PWM_MAIN_ARM_DOWN);
    delay(durationMs);
  } else {
    Serial.println(F("ERROR: Main arm actions are OP (up) or CL (down)."));
    return;
  }

  setAllNeutral();
  delay(MAIN_ARM_SETTLE_MS);
}

// -----------------------------------------------------------------------------
// Body rotation (cab swing)
// Actions: CW = clockwise, CC = counter-clockwise
// -----------------------------------------------------------------------------

void rotateBody(const String& action, int durationMs) {
  if (action == "CW") {
    analogWrite(PIN_BODY_ROTATE, PWM_BODY_CW);
    delay(durationMs);
    analogWrite(PIN_BODY_ROTATE, PWM_BODY_NEUTRAL);
  } else if (action == "CC") {
    analogWrite(PIN_BODY_ROTATE, PWM_BODY_CCW);
    delay(durationMs);
    analogWrite(PIN_BODY_ROTATE, PWM_BODY_NEUTRAL);
  } else {
    Serial.println(F("ERROR: Body rotation actions are CW or CC."));
  }
}

// -----------------------------------------------------------------------------
// Track drive — forward, backward, and pivot turns
// Actions: FW, BW, CW (pivot right), CC (pivot left)
// -----------------------------------------------------------------------------

void moveTracks(const String& action, int durationMs) {
  if (action == "FW") {
    analogWrite(PIN_TRACK_LEFT, PWM_TRACK_FORWARD);
    analogWrite(PIN_TRACK_RIGHT, PWM_TRACK_FORWARD);
    delay(durationMs);
  } else if (action == "BW") {
    analogWrite(PIN_TRACK_LEFT, PWM_TRACK_BACKWARD);
    analogWrite(PIN_TRACK_RIGHT, PWM_TRACK_BACKWARD);
    delay(durationMs);
  } else if (action == "CW") {
    analogWrite(PIN_TRACK_LEFT, PWM_TRACK_FORWARD);
    analogWrite(PIN_TRACK_RIGHT, PWM_TRACK_BACKWARD);
    delay(durationMs);
  } else if (action == "CC") {
    analogWrite(PIN_TRACK_LEFT, PWM_TRACK_BACKWARD);
    analogWrite(PIN_TRACK_RIGHT, PWM_TRACK_FORWARD);
    delay(durationMs);
  } else {
    Serial.println(F("ERROR: Track actions are FW, BW, CW, or CC."));
    return;
  }

  analogWrite(PIN_TRACK_LEFT, PWM_TRACK_NEUTRAL);
  analogWrite(PIN_TRACK_RIGHT, PWM_TRACK_NEUTRAL);
}

// -----------------------------------------------------------------------------
// Demonstration sequence — exercises every subsystem once
// Send the serial keyword: Trial
// -----------------------------------------------------------------------------

void runTrialSequence() {
  Serial.println(F("Running Trial sequence (all subsystems)..."));

  moveBucket("OP", TRIAL_BUCKET_MS);
  delay(TRIAL_PAUSE_MS);
  moveBucket("CL", TRIAL_BUCKET_MS);
  delay(TRIAL_PAUSE_MS);

  moveMiddleArm("OP", TRIAL_MIDDLE_ARM_MS);
  delay(TRIAL_PAUSE_MS);
  moveMiddleArm("CL", TRIAL_MIDDLE_ARM_MS);
  delay(TRIAL_PAUSE_MS);

  moveMainArm("OP", TRIAL_MAIN_ARM_MS);
  delay(TRIAL_PAUSE_MS);
  moveMainArm("CL", TRIAL_MAIN_ARM_MS);
  delay(TRIAL_PAUSE_MS);

  rotateBody("CW", TRIAL_BODY_ROTATE_MS);
  delay(TRIAL_PAUSE_MS);

  moveTracks("FW", TRIAL_TRACK_MS);
  delay(TRIAL_PAUSE_MS);

  Serial.println(F("Trial complete."));
}
