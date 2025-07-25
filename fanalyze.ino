#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <avr/interrupt.h>

// === Pin Tanımları ===
#define DHTPIN 5
#define DHTTYPE DHT11
#define LED_GREEN 7    // Fan açık LED
#define LED_BLUE 8     // Fan kapalı LED
#define LED_YELLOW 9   // Sıcaklık uyarı LED
#define BUTTON_MANUAL 4 // Manuel mod butonu (polling ile)
#define BUTTON_START 2  // Başlat butonu (INT0)
#define BUTTON_STOP 3   // Durdur butonu (INT1)
#define IN1 10
#define IN2 11
#define ENA 12
#define BUZZER_PIN 13   // Buzzer

LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHTPIN, DHTTYPE);

// === Sistem Durumları ===
volatile bool systemActive = false;
volatile bool systemStopped = false;
volatile bool manualMode = false;

volatile bool fanState = true;    // Fan ON/OFF
volatile bool toggleFan = false;  // Fan durumu değişti mi (timer interrupt tetiklediğinde)

const unsigned int onTime = 4000;   // Fan çalışma süresi (ms)
const unsigned int offTime = 5000;  // Fan dinlenme süresi (ms)

bool tempExceeded = false;
bool buzzerActive = false;
unsigned long buzzerStartTime = 0;
const unsigned long buzzerDuration = 3000;

// === Arduino başlatıldığında ===
void setup() {
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  dht.begin();

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENA, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(BUTTON_MANUAL, INPUT_PULLUP);
  pinMode(BUTTON_START, INPUT_PULLUP);
  pinMode(BUTTON_STOP, INPUT_PULLUP);

  // INT0 (pin 2) ve INT1 (pin 3) için buton interruptları
  attachInterrupt(digitalPinToInterrupt(BUTTON_START), startPressed, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_STOP), stopPressed, FALLING);

  // Timer1'i ayarla (1 ms per interrupt)
  cli(); // Interruptları kapat
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  OCR1A = 249;                  // 1ms: (16MHz/64/1000)-1=249
  TCCR1B |= (1 << WGM12);       // CTC mod
  TCCR1B |= (1 << CS11) | (1 << CS10); // 64 prescaler
  TIMSK1 |= (1 << OCIE1A);      // Compare A Match interrupt aç
  sei(); // Interruptları aç
}

// === Ana döngü ===
void loop() {
  // Manuel butonunu polling ile oku (INT yok)
  if (digitalRead(BUTTON_MANUAL) == LOW) {
    manualPressed();
    delay(200); // debounce
  }

  // Sıcaklık & nem oku
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();

  lcd.setCursor(0, 0);
  if (isnan(temp) || isnan(hum)) {
    lcd.print("Sensor Error!     ");
    lcd.setCursor(0, 1);
    lcd.print("                ");
    return;
  }
  lcd.print("Temp: ");
  lcd.print(temp);
  lcd.print((char)223);
  lcd.print("C   ");

  // Buzzer + Sarı LED kontrolü
  if (temp >= 35.0) {
    digitalWrite(LED_YELLOW, HIGH);
    if (!tempExceeded) {
      digitalWrite(BUZZER_PIN, HIGH);
      buzzerStartTime = millis();
      buzzerActive = true;
      tempExceeded = true;
    }
    if (buzzerActive && (millis() - buzzerStartTime >= buzzerDuration)) {
      digitalWrite(BUZZER_PIN, LOW);
      buzzerActive = false;
    }
  } else {
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    buzzerActive = false;
    tempExceeded = false;
  }

  // MOD & FAN kontrol
  if (manualMode) {
    setFan(true);
    lcd.setCursor(0, 1);
    lcd.print("Fan: MANUEL     ");
  } else if (systemStopped) {
    setFan(false);
    lcd.setCursor(0, 1);
    lcd.print("Fan: DURDUR     ");
  } else if (temp >= 29.0) {
    setFan(true);
    lcd.setCursor(0, 1);
    lcd.print("Fan: HOT ALERT  ");
  } else if (systemActive) {
    // FanState timer interrupt ile değiştiyse, setFan ile uygula
    if (toggleFan) {
      setFan(fanState);
      toggleFan = false;
    }
    lcd.setCursor(0, 1);
    lcd.print(fanState ? "Fan: ACIK       " : "Fan: BEKLET     ");
  }

  delay(100); // Ekran titreşim önleyici
}

// === TIMER1 Kesmesi ===
// Her 1ms'de bir çalışır. ON/OFF sürelerini burada tutar!
ISR(TIMER1_COMPA_vect) {
  static unsigned long elapsed = 0;
  if (systemActive && !manualMode && !systemStopped) {
    elapsed++;
    if (fanState && elapsed >= onTime) {
      fanState = false;
      toggleFan = true;
      elapsed = 0;
    } else if (!fanState && elapsed >= offTime) {
      fanState = true;
      toggleFan = true;
      elapsed = 0;
    }
  } else {
    // Mod değişince sayaç sıfırlanır
    elapsed = 0;
  }
}

// === Buton interrupt fonksiyonları ===
void startPressed() {
  systemActive = true;
  manualMode = false;
  systemStopped = false;
}
void stopPressed() {
  systemActive = false;
  manualMode = false;
  systemStopped = true;
}
void manualPressed() {
  manualMode = true;
  systemActive = false;
  systemStopped = false;
}

// === Fan ve LED kontrol ===
void setFan(bool active) {
  if (active) {
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
    digitalWrite(ENA, HIGH);
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_BLUE, LOW);
  } else {
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
    digitalWrite(ENA, LOW);
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_BLUE, HIGH);
  }
}
