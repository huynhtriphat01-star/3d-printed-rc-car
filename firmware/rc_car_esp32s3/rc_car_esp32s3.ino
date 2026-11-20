/************************************************************
 * XE RC ESP32-S3 + TAY CAM PS2 + GYRO RX-7D
 *
 * DIEU KHIEN:
 * - Joystick trai, truc Y (LY):
 *      Day len  -> tien / tang ga
 *      Keo xuong-> lui
 *
 * - Joystick phai, truc X (RX):
 *      Sang trai -> banh lai trai
 *      Sang phai -> banh lai phai
 *
 * KET NOI PS2 RECEIVER -> ESP32-S3:
 *   DATA     -> GPIO12
 *   CMD      -> GPIO11
 *   CLK      -> GPIO10
 *   ATT/SEL  -> GPIO13
 *   VCC      -> 3V3
 *   GND      -> GND
 *
 * KET NOI ESC:
 *   ESC Signal (trang/vang/cam) -> GPIO4
 *   ESC GND (den/nau)           -> GND ESP32
 *   ESC day do BEC              -> KHONG NOI, boc cach dien
 *
 * KET NOI GYRO RX-7D:
 *   GPIO6 ESP32                 -> Signal dau RC/IN gyro
 *   XL4015/Mini560 +5V/+6V      -> Day do dau RC/IN gyro
 *   XL4015/Mini560 GND          -> Day nau/den dau RC/IN gyro
 *   GND nguon gyro              -> GND ESP32
 *
 *   Dau duc SERVO/OUT cua gyro  -> cam truc tiep vao MG996R
 *
 * NGUON:
 *   XL4015 6V -> gyro + MG996R
 *   Mini560 5V -> ESP32-S3 + receiver PS2
 *   Tat ca GND phai noi chung.
 ************************************************************/

#include <PS2X_lib.h>
#include <ESP32Servo.h>

// ==================== PIN CONFIG ====================

// ESC signal
#define ESC_PIN 4

// GPIO6 -> gyro RC/IN signal
#define STEER_PIN 6

// PS2 receiver
#define PS2_CLK 10
#define PS2_CMD 11
#define PS2_ATT 13
#define PS2_DAT 12

// ==================== OBJECTS ====================

PS2X ps2x;
Servo esc;
Servo steering;

// ==================== ESC CONFIG ====================

// Kiem tra ESC cua ban co dung neutral 1500 us hay khong.
// Phan lon ESC xe RC dung 1500 us = neutral.
const int ESC_MIN_US     = 1000;
const int ESC_NEUTRAL_US = 1500;
const int ESC_MAX_US     = 2000;

// Gioi han ga de test an toan.
// 45 = toi da 45% ga tien va lui.
const int MAX_THROTTLE_PERCENT = 40;

// ==================== NEW SAFETY CONFIG ====================

// PWM brake (thử 1400 trước, nếu ESC không brake sẽ chỉnh sau)
const int ESC_BRAKE_US = 1400;




// Steering rate limit
const int STEER_RATE = 3;

// Ramping: so nho hon = tang ga cham hon, xe em hon.
const int RAMP_STEP_NORMAL  = 1;
const int RAMP_STEP_REVERSE = 1;

// ==================== STEERING CONFIG ====================

// Day la goc PWM GUI VAO GYRO.
// Chinh sau khi test co khi thuc te.
const int STEER_LEFT   = 65;
const int STEER_CENTER = 90;
const int STEER_RIGHT  = 115;

// Vung chet joystick
const int THROTTLE_DEADZONE = 10;
const int STEER_DEADZONE    = 8;

// ==================== STATE ====================

int ps2Error = 1;

int throttleTarget = 0;
int throttleCurrent = 0;

// ==================== NEW VARIABLES ====================



// steering smoothing
int currentSteeringAngle = STEER_CENTER;

// joystick filter
int filteredRX = 128;
int filteredLY = 128;


unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 2000;

// ==================== FUNCTIONS ====================

int applyDeadzone(int value, int deadzone) {
  if (value > -deadzone && value < deadzone) {
    return 0;
  }
  return value;
}

bool connectPS2() {
  ps2Error = ps2x.config_gamepad(
    PS2_CLK,
    PS2_CMD,
    PS2_ATT,
    PS2_DAT,
    false,  // rumble
    false   // pressure buttons
  );

  Serial.print("PS2 init error = ");
  Serial.println(ps2Error);

  if (ps2Error == 0) {
    Serial.println("PS2 connected.");
    return true;
  }

  if (ps2Error == 1) {
    Serial.println("PS2 error 1: Kiem tra DATA/CMD/CLK/ATT, VCC va GND.");
  } else if (ps2Error == 2) {
    Serial.println("PS2 error 2: Receiver co nguon nhung khong nhan lenh.");
  } else if (ps2Error == 3) {
    Serial.println("PS2 error 3: Nhan MODE tren tay cam de vao Analog mode.");
  }

  return false;
}

void stopVehicle() {
  throttleTarget = 0;
  throttleCurrent = 0;
  esc.writeMicroseconds(ESC_NEUTRAL_US);
}

void centerSteering() {
  steering.write(STEER_CENTER);
}
 void brakeVehicle()
{
    esc.writeMicroseconds(ESC_BRAKE_US);

    throttleCurrent = 0;
    throttleTarget = 0;
}

void updateThrottleRamp()
{
    bool changingDirection =
        (throttleCurrent > 0 && throttleTarget < 0) ||
        (throttleCurrent < 0 && throttleTarget > 0);

    int step;

    if (changingDirection)
    {
        step = 1;
    }
    else
    {
        if (abs(throttleCurrent) < 10)
            step = 1;

        else if (abs(throttleCurrent) < 20)
            step = 1;

        else if (abs(throttleCurrent) < 30)
            step = 2;

        else
            step = 2;
    }

    if (throttleCurrent < throttleTarget)
        throttleCurrent = min(throttleCurrent + step, throttleTarget);

    else if (throttleCurrent > throttleTarget)
        throttleCurrent = max(throttleCurrent - step, throttleTarget);

    int pwmUs = map(
        throttleCurrent,
        -100,
        100,
        ESC_MIN_US,
        ESC_MAX_US);

    esc.writeMicroseconds(pwmUs);
}

// ===== NEW =====
// Steering quay mượt
void updateSteeringSmooth(int targetAngle)
{
    if(currentSteeringAngle < targetAngle)
    {
        currentSteeringAngle =
            min(currentSteeringAngle + STEER_RATE, targetAngle);
    }
    else if(currentSteeringAngle > targetAngle)
    {
        currentSteeringAngle =
            max(currentSteeringAngle - STEER_RATE, targetAngle);
    }

    steering.write(currentSteeringAngle);
}

void applySteering(int steeringPercent) {
  steeringPercent = constrain(steeringPercent, -100, 100);
  steeringPercent = applyDeadzone(steeringPercent, STEER_DEADZONE);

  /*
   * Quy uoc:
   * RX = 0   -> joystick phai sang trai -> banh lai trai
   * RX = 255 -> joystick phai sang phai -> banh lai phai
   *
   * Neu sau khi qua gyro banh lai bi nguoc:
   * - Truoc tien doi cong tac REV tren gyro.
   * - Neu can, dao STEER_LEFT va STEER_RIGHT trong dong map ben duoi.
   */
  int angle = map(
    steeringPercent,
    -100, 100,
    STEER_RIGHT, STEER_LEFT
  );

 // steering.write(angle);
 updateSteeringSmooth(angle);
}

// ==================== SETUP ====================

void setup() {
  Serial.begin(115200);
  delay(800);

  Serial.println();
  Serial.println("======================================");
  Serial.println("RC CAR: PS2 + ESP32-S3 + GYRO RX-7D");
  Serial.println("ESC signal: GPIO4");
  Serial.println("Gyro RC signal: GPIO6");
  Serial.println("======================================");

  esc.setPeriodHertz(50);
  steering.setPeriodHertz(50);

  esc.attach(ESC_PIN, ESC_MIN_US, ESC_MAX_US);
  steering.attach(STEER_PIN, 500, 2400);

  // Trang thai an toan luc khoi dong
  stopVehicle();
  centerSteering();
  // ===== NEW =====moithem

currentSteeringAngle = STEER_CENTER;

  Serial.println("ESC neutral. Waiting 3 seconds...");
  delay(3000);

  connectPS2();

  Serial.println("System ready.");
}

// ==================== LOOP ====================

void loop() {
  // Mat ket noi PS2 -> dung xe va dua lai ve giua
  if (ps2Error != 0) {
    stopVehicle();
    centerSteering();

    if (millis() - lastReconnectAttempt >= RECONNECT_INTERVAL) {
      lastReconnectAttempt = millis();
      Serial.println("Trying to reconnect PS2...");
      connectPS2();
    }

    delay(50);
    return;
  }

  ps2x.read_gamepad(false, 0);

  // SELECT + START = dung khan cap
  if (ps2x.Button(PSB_SELECT) && ps2x.Button(PSB_START)) {
    stopVehicle();
    centerSteering();

    Serial.println("EMERGENCY STOP");
    delay(50);
    return;
  }

  // ===== DOC JOYSTICK =====

  // ===== NEW =====
// Filter joystick

filteredLY =
(filteredLY * 3 + ps2x.Analog(PSS_LY)) / 4;

filteredRX =
(filteredRX * 3 + ps2x.Analog(PSS_RX)) / 4;

int rawY = filteredLY;
int rawX = filteredRX;

  // ===== XU LY GA =====

  int rawThrottle = map(rawY, 0, 255, 100, -100);
  rawThrottle = applyDeadzone(rawThrottle, THROTTLE_DEADZONE);

  // Gioi han ga toi da
  throttleTarget = map(
    rawThrottle,
    -100, 100,
    -MAX_THROTTLE_PERCENT,
    MAX_THROTTLE_PERCENT
  );
  
  // ===== XU LY LAI =====

  int steeringPercent = map(rawX, 0, 255, -100, 100);
  // ===== NEW =====
// Brake bằng L2

if(ps2x.Button(PSB_L2))
{
    brakeVehicle();

    applySteering(steeringPercent);

    //delay(10);

    return;
}

  // Gui PWM ra ESC va gyro
  updateThrottleRamp();
  applySteering(steeringPercent);

  // Debug Serial Monitor
  Serial.print("THR=");
  Serial.print(throttleCurrent);
  Serial.print("%  STEER=");
  Serial.println(steeringPercent);

  delay(10);
}