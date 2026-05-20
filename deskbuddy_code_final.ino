#define BLYNK_TEMPLATE_ID "TMPL33-Q8BaVT"
#define BLYNK_TEMPLATE_NAME "DeskBuddy"
#define BLYNK_AUTH_TOKEN "2eS8RPsAHjcpOtxEKi5X1H1ry6jGEykx"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ----- Wi-Fi Credentials -----
char ssid[] = "Ananya";
char pass[] = "12345678";

// ----- Pin Definitions -----
#define TRIG_PIN    5
#define ECHO_PIN    18
#define BUTTON_PIN  13
#define LED_PIN  23

bool     ledBlinkState    = false;
unsigned long lastLedBlink = 0;
const unsigned long LED_BLINK_MS = 350;

// ----- OLED Setup -----
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDRESS  0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ----- Mode -----
int currentMode = 0; // 0=WORK, 1=BREAK, 2=SLEEP

// ----- Timing -----
unsigned long workSessionStart  = 0;
unsigned long lastPresenceTime  = 0;
unsigned long lastBreakReminder = 0;
unsigned long lastWaterReminder = 0;
unsigned long lastEyeReminder   = 0;

const unsigned long BREAK_INTERVAL = 1UL * 60 * 1000;
const unsigned long WATER_INTERVAL = 1UL * 20 * 1000;
const unsigned long EYE_INTERVAL   = 1UL * 30 * 1000;
const unsigned long SLEEP_TIMEOUT  = 3UL  * 30 * 1000;

// ----- Cloud Sync Timer -----
unsigned long lastCloudSync = 0;
const unsigned long CLOUD_INTERVAL = 5000;

// ----- Sensor -----
bool userPresent     = false;
long currentDistance = 0;
const int PRESENCE_DIST = 80; 

// ----- Button -----
bool lastButtonState       = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 50;

// ----- Reminder (Short) -----
bool showingReminder        = false;
unsigned long reminderStart = 0;
const unsigned long REMINDER_DURATION = 5000;
String reminderTitle = "";
String reminderMsg   = "";

// ----- Dashboard Reminder via V4 -----
// When the website fires a custom reminder, it writes the title to V4.
// We store it here and show it on the OLED at the next loop iteration.
bool   dashReminderPending = false;
String dashReminderText    = "";

// ----- Advanced Break Reminder System -----
enum BreakState { BREAK_NONE, BREAK_SOFT, BREAK_HARD };
BreakState currentBreakState = BREAK_NONE;
int snoozeCount = 0;
unsigned long awayStartTime = 0;
bool isAway = false;

// ----- Eye Animation System -----
enum EyeState { EYES_IDLE, EYES_BLINKING, EYES_SMILING, EYES_TIRED };
EyeState currentEyeState = EYES_IDLE;
unsigned long eyeAnimationStart = 0;
unsigned long lastBlinkTime = 0;
const unsigned long BLINK_INTERVAL = 3000;
const unsigned long BLINK_DURATION = 100;

const int EYE_Y = 36;

// ============================================================
// BLYNK HANDLERS
// ============================================================

// V4 — Dashboard sends custom reminder title here
BLYNK_WRITE(V4) {
  String val = param.asStr();
  val.trim();
  if (val.length() > 0) {
    dashReminderText    = val;
    dashReminderPending = true;
    // Immediately clear V4 on the server so it doesn't re-fire on reconnect
    Blynk.virtualWrite(V4, "");
  }
}

// ============================================================
// ANIMATION DRAWING FUNCTIONS
// ============================================================

void drawCenteredText(String text, int y, int size) {
  display.setTextSize(size);
  int charWidth = size * 6;
  int startX = max(0, (128 - (int)text.length() * charWidth) / 2);
  display.setCursor(startX, y);
  display.print(text);
}

void drawBlinking(unsigned long animTime, String headerText) {
  int progress = (animTime > 0) ? (animTime % BLINK_DURATION) * 100 / BLINK_DURATION : 0;
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  if (headerText != "") { drawCenteredText(headerText, 4, 2); }
  
  display.drawCircle(40, EYE_Y, 8, SSD1306_WHITE);
  int closedAmount = (progress > 50) ? (progress - 50) : progress;
  for (int i = 0; i < closedAmount / 10; i++) {
    display.drawLine(32, EYE_Y + i, 48, EYE_Y + i, SSD1306_WHITE);
  }
  
  display.drawCircle(88, EYE_Y, 8, SSD1306_WHITE);
  for (int i = 0; i < closedAmount / 10; i++) {
    display.drawLine(80, EYE_Y + i, 96, EYE_Y + i, SSD1306_WHITE);
  }
  
  if (closedAmount < 80) {
    display.fillCircle(40, EYE_Y, 3, SSD1306_WHITE);
    display.fillCircle(88, EYE_Y, 3, SSD1306_WHITE);
  }
  
  display.display();
}

void drawSmiling(String headerText, String footerText) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  if (headerText != "") { drawCenteredText(headerText, 4, 2); }
  
  display.drawCircle(40, EYE_Y, 8, SSD1306_WHITE);
  display.fillCircle(40, EYE_Y, 3, SSD1306_WHITE);
  display.drawLine(35, EYE_Y + 6, 45, EYE_Y + 6, SSD1306_WHITE);
  display.drawCircle(88, EYE_Y, 8, SSD1306_WHITE);
  display.fillCircle(88, EYE_Y, 3, SSD1306_WHITE);
  display.drawLine(83, EYE_Y + 6, 93, EYE_Y + 6, SSD1306_WHITE);
  
  display.drawLine(35, EYE_Y + 10, 55, EYE_Y + 14, SSD1306_WHITE);
  display.drawLine(55, EYE_Y + 14, 75, EYE_Y + 14, SSD1306_WHITE);
  display.drawLine(75, EYE_Y + 14, 95, EYE_Y + 10, SSD1306_WHITE);
  if (footerText != "") { drawCenteredText(footerText, 56, 1); }
  
  display.display();
}

void drawTired() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.drawCircle(40, EYE_Y, 8, SSD1306_WHITE);
  display.drawLine(35, EYE_Y - 4, 45, EYE_Y + 4, SSD1306_WHITE);
  display.drawLine(32, EYE_Y + 8, 48, EYE_Y + 8, SSD1306_WHITE);
  
  display.drawCircle(88, EYE_Y, 8, SSD1306_WHITE);
  display.drawLine(83, EYE_Y - 4, 93, EYE_Y + 4, SSD1306_WHITE);
  display.drawLine(80, EYE_Y + 8, 96, EYE_Y + 8, SSD1306_WHITE);
  drawCenteredText("Zzz... (Sleep)", 56, 1);
  
  display.display();
}

// ============================================================
// CORE FUNCTIONS
// ============================================================
long measureDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long d = pulseIn(ECHO_PIN, HIGH, 30000);
  if (d == 0) return 999;
  return d * 0.034 / 2;
}

void showStatusScreen() {
  String modeName = (currentMode==0)?"WORK":(currentMode==1)?"BREAK":"SLEEP";
  unsigned long sessionMinutes = (millis() - workSessionStart) / 60000;
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("DeskBuddy ["); display.print(modeName); display.print("]");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  display.setCursor(0, 14); display.print("Session: "); display.print(sessionMinutes); display.print(" min");
  display.setCursor(0, 26);
  display.print("User: "); display.print(userPresent ? "Present" : "Away   ");
  display.setCursor(0, 38); display.print("Dist: ");
  if (currentDistance < 999) { display.print(currentDistance); display.print(" cm"); }
  else display.print("Out of range");
  display.setCursor(0, 52); display.print("WiFi: "); display.print(WiFi.isConnected() ? "OK" : "OFF");
  display.display();
}

void showReminder(String title, String message) {
  display.clearDisplay();
  display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(5, 8);
  display.println(title);
  display.setTextSize(1);
  display.setCursor(5, 36);
  display.println(message);
  display.display();
}

// ============================================================
void setup() {
  Serial.begin(115200);
  
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED FAILED"); while (true);
  }
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 5);
  display.println("DeskBuddy");
  display.setTextSize(1);
  display.setCursor(10, 35);
  display.println("Connecting WiFi...");
  display.display();
  
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("WiFi Connected!");
  display.setCursor(0, 15);
  display.println(WiFi.localIP().toString());
  display.setCursor(0, 30);
  display.println("Blynk: OK");
  display.display();
  delay(2500);
  
  unsigned long now = millis();
  workSessionStart = lastPresenceTime = lastBreakReminder = lastWaterReminder = lastEyeReminder = lastBlinkTime = now;
  Serial.println("DeskBuddy ready.");
}

// ============================================================
void loop() {
  Blynk.run(); 
  
  unsigned long now = millis();

  // --- Button ---
  bool buttonReading = digitalRead(BUTTON_PIN);
  if (buttonReading != lastButtonState) lastDebounceTime = now;
  if ((now - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (lastButtonState == HIGH && buttonReading == LOW) {
      currentMode = (currentMode + 1) % 3;
      if (currentMode == 0) {
        workSessionStart = lastBreakReminder = lastWaterReminder = lastEyeReminder = now;
        currentBreakState = BREAK_NONE;
      }
    }
  }
  lastButtonState = buttonReading;

  // --- Distance + Presence + Gestures ---
  currentDistance = measureDistance();
  userPresent = (currentDistance > 5 && currentDistance < PRESENCE_DIST); 
  bool userAway = (currentDistance >= PRESENCE_DIST || currentDistance == 999);
  bool handWaving = (currentDistance > 0 && currentDistance <= 5);

  if (userPresent) lastPresenceTime = now;

  // --- Auto Sleep ---
  if ((now - lastPresenceTime) > SLEEP_TIMEOUT && currentMode != 2) {
    currentMode = 2;
  }
  if (currentMode == 2 && userPresent) {
    currentMode = 0;
    workSessionStart = lastBreakReminder = lastWaterReminder = lastEyeReminder = now;
  }

  // --- REMINDER LOGIC ---

  // 0. Dashboard reminder (V4) — inject as a short reminder if nothing active
  if (dashReminderPending && !showingReminder && currentBreakState == BREAK_NONE) {
    reminderTitle    = dashReminderText;
    reminderMsg      = "from dashboard";
    showingReminder  = true;
    reminderStart    = now;
    dashReminderPending = false;
    Serial.println("Dashboard reminder: " + reminderTitle);
  }

  // 1. Triggering the Break State
  if (currentMode == 0 && currentBreakState == BREAK_NONE) {
    if ((now - lastBreakReminder) >= BREAK_INTERVAL) {
      if (snoozeCount < 2) {
        currentBreakState = BREAK_SOFT;
      } else {
        currentBreakState = BREAK_HARD;
      }
    }
  }

  // 2. Handling Active Break States
  if (currentBreakState != BREAK_NONE) {
      if (userAway) {
          if (!isAway) {
              isAway = true;
              awayStartTime = now;
          } else if ((now - awayStartTime) >= 60000) {
              currentBreakState = BREAK_NONE;
              snoozeCount = 0;
              lastBreakReminder = now;
              isAway = false;
          }
      } else if (userPresent) {
          isAway = false;
      }

      if (currentBreakState == BREAK_SOFT && handWaving) {
          snoozeCount++;
          currentBreakState = BREAK_NONE;
          lastBreakReminder = now;
          isAway = false;
          delay(1500);
      }
  }

  // 3. Normal Short Reminders (Water, Eye) - Blocked if a break is active
  if (currentMode == 0 && userPresent && currentBreakState == BREAK_NONE && !showingReminder) {
    if ((now - lastWaterReminder) >= WATER_INTERVAL)  { 
        reminderTitle = "HYDRATE"; reminderMsg = "Drink some water!";  
        showingReminder=true; reminderStart=now; lastWaterReminder=now; 
    }
    else if ((now - lastEyeReminder) >= EYE_INTERVAL) { 
        reminderTitle = "EYE CARE"; reminderMsg = "Look 20ft, 20 sec"; 
        showingReminder=true; reminderStart=now; lastEyeReminder=now; 
    }
  }

  // --- Display with Animations ---
  
  if (currentBreakState == BREAK_HARD) {
      display.clearDisplay();
      display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
      drawCenteredText("BREAK", 5, 3);
      drawCenteredText("TIME!", 35, 3);
      display.display();
  }
  else if (currentBreakState == BREAK_SOFT) {
      showReminder("BREAK!", "Stand up, stretch\nWave <5cm to snooze");
  }
  else if (showingReminder) {
    if (reminderTitle == "EYE CARE") {
      unsigned long cycle = (now - reminderStart) % 1500;
      if (cycle < BLINK_DURATION) { drawBlinking(cycle, "EYE CARE"); } 
      else { drawBlinking(0, "EYE CARE"); }
    } 
    else if (reminderTitle == "HYDRATE") {
      drawSmiling("HYDRATE", "");
    } 
    else {
      // Dashboard custom reminder — title on line 1, "from dashboard" on line 2
      showReminder(reminderTitle, reminderMsg);
    }
    
    if ((now - reminderStart) >= REMINDER_DURATION) showingReminder = false;
  }
  else if (currentMode == 1 || currentMode == 2) {
    if ((now - lastBlinkTime) >= BLINK_INTERVAL) {
      currentEyeState = EYES_BLINKING;
      eyeAnimationStart = now;
      lastBlinkTime = now;
    }

    if (currentEyeState == EYES_BLINKING && (now - eyeAnimationStart) < BLINK_DURATION) {
      drawBlinking(now - eyeAnimationStart, "");
    } else {
      if (currentMode == 1) { drawSmiling("", "Enjoy your break!"); } 
      else { drawTired(); }
    }
  }
  else {
    showStatusScreen();
  }

  // --- Cloud Sync ---
  if ((now - lastCloudSync) >= CLOUD_INTERVAL) {
    unsigned long sessionMinutes = (now - workSessionStart) / 60000;
    Blynk.virtualWrite(V0, sessionMinutes);         
    Blynk.virtualWrite(V1, userPresent ? 1 : 0);   
    Blynk.virtualWrite(V2, currentDistance < 999 ? currentDistance : 0); 
    Blynk.virtualWrite(V3, currentMode);
    lastCloudSync = now;
  }
  // --- LED Control ---
if (currentBreakState == BREAK_HARD) {
  // Solid ON — hard break, no snoozes left
  digitalWrite(LED_PIN, HIGH);

} else if (currentBreakState == BREAK_SOFT || showingReminder) {
  // Blinking — soft break, hydrate, eye care, or custom reminder
  if ((now - lastLedBlink) >= LED_BLINK_MS) {
    ledBlinkState = !ledBlinkState;
    digitalWrite(LED_PIN, ledBlinkState ? HIGH : LOW);
    lastLedBlink = now;
  }

} else {
  // Off — normal operation
  digitalWrite(LED_PIN, LOW);
  ledBlinkState = false;
}
  delay(200);
}
