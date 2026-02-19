#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <BH1750.h>
#include <Adafruit_BMP085.h>

#define DHT_PIN 4
#define PH_PIN 34

#define ENC_CLK 33
#define ENC_DT 25
#define ENC_SW 26

#define RELAY_MOTOR 23
#define RELAY_LIGHT 18
#define RELAY_FAN 19

#define RELAY_ON LOW
#define RELAY_OFF HIGH
#define FAN_RELAY_ON HIGH
#define FAN_RELAY_OFF LOW

DHT dht(DHT_PIN, DHT11);
BH1750 lightMeter;
Adafruit_BMP085 bmp;
LiquidCrystal_I2C lcd(0x27, 20, 4);

enum MenuState
{
    WELCOME,
    MAIN_MENU,
    DHT_DISPLAY,
    DS18B20_DISPLAY,
    BH1750_DISPLAY,
    PH_DISPLAY,
    PRESSURE_DISPLAY,
    RELAY_MENU,
    MOTOR_SETTINGS,
    LIGHT_CONTROL,
    FAN_CONTROL
};

MenuState currentState = WELCOME;
MenuState previousState = WELCOME;

int menuIndex = 0;
int relayMenuIndex = 0;
int sensorMenuIndex = 0;
int motorMenuIndex = 0;
int lightMenuIndex = 0;
int fanMenuIndex = 0;

int previousMenuIndex = -1;
int previousRelayMenuIndex = -1;
int previousSensorMenuIndex = -1;

unsigned long motorOnTime = 15 * 60000UL;
unsigned long motorOffTime = 45 * 60000UL;
unsigned long motorLastToggle = 0;
bool motorState = false;
bool motorAutoMode = true;

bool lightState = false;
bool fanAutoMode = true;
bool fanState = false;

unsigned long lastDisplayUpdate = 0;
const unsigned long displayUpdateInterval = 1000;
bool forceDisplayUpdate = true;

float dhtTemp = NAN;
float dhtHumidity = NAN;
float ds18b20Temp = 20.0;
float lux = 0;
float phValue = 0;
float pressure_hPa = 0;

volatile int encoderPos = 0;
int lastEncoderPos = 0;
int lastCLK = HIGH;

unsigned long swPressTime = 0;
bool swWasPressed = false;
const unsigned long LONG_PRESS_MS = 800;

void IRAM_ATTR encoderISR()
{
    int clk = digitalRead(ENC_CLK);
    int dt = digitalRead(ENC_DT);
    if (clk != lastCLK)
    {
        encoderPos += (dt != clk) ? 1 : -1;
        lastCLK = clk;
    }
}

void setMotorRelay(bool state)
{
    motorState = state;
    digitalWrite(RELAY_MOTOR, state ? RELAY_ON : RELAY_OFF);
}

void setLightRelay(bool state)
{
    lightState = state;
    digitalWrite(RELAY_LIGHT, state ? RELAY_ON : RELAY_OFF);
}

void setFanRelay(bool state)
{
    fanState = state;
    digitalWrite(RELAY_FAN, state ? FAN_RELAY_ON : FAN_RELAY_OFF);
}

void displayWelcome()
{
    lcd.clear();
    lcd.setCursor(3, 1);
    lcd.print("WELCOME TO");
    lcd.setCursor(3, 2);
    lcd.print("HYDROPONIC");
    delay(2000);
    currentState = MAIN_MENU;
    forceDisplayUpdate = true;
}

void handleUpButton()
{
    if (currentState == MAIN_MENU)
    {
        if (--menuIndex < 0)
            menuIndex = 5;
    }
    else if (currentState == RELAY_MENU)
    {
        if (--relayMenuIndex < 0)
            relayMenuIndex = 3;
    }
}

void handleDownButton()
{
    if (currentState == MAIN_MENU)
    {
        if (++menuIndex > 5)
            menuIndex = 0;
    }
    else if (currentState == RELAY_MENU)
    {
        if (++relayMenuIndex > 3)
            relayMenuIndex = 0;
    }
}

void handleOkButton()
{
    if (currentState == MAIN_MENU)
    {
        if (menuIndex == 0)
            currentState = DHT_DISPLAY;
        else if (menuIndex == 1)
            currentState = DS18B20_DISPLAY;
        else if (menuIndex == 2)
            currentState = BH1750_DISPLAY;
        else if (menuIndex == 3)
            currentState = PH_DISPLAY;
        else if (menuIndex == 4)
            currentState = PRESSURE_DISPLAY;
        else if (menuIndex == 5)
            currentState = RELAY_MENU;
    }
    else if (currentState == RELAY_MENU)
    {
        if (relayMenuIndex == 0)
            currentState = MOTOR_SETTINGS;
        else if (relayMenuIndex == 1)
            currentState = LIGHT_CONTROL;
        else if (relayMenuIndex == 2)
            currentState = FAN_CONTROL;
        else
            currentState = MAIN_MENU;
    }
    else
    {
        currentState = MAIN_MENU;
    }
}

void handleBackButton()
{
    if (currentState != MAIN_MENU)
        currentState = MAIN_MENU;
}

void handleEncoder()
{
    int pos = encoderPos;
    int delta = pos - lastEncoderPos;
    if (delta >= 2)
    {
        lastEncoderPos = pos;
        handleDownButton();
        forceDisplayUpdate = true;
    }
    else if (delta <= -2)
    {
        lastEncoderPos = pos;
        handleUpButton();
        forceDisplayUpdate = true;
    }

    bool swPressed = digitalRead(ENC_SW) == LOW;
    if (swPressed && !swWasPressed)
    {
        swPressTime = millis();
        swWasPressed = true;
    }
    if (!swPressed && swWasPressed)
    {
        swWasPressed = false;
        if (millis() - swPressTime >= LONG_PRESS_MS)
            handleBackButton();
        else
            handleOkButton();
        forceDisplayUpdate = true;
    }
}

void updateSensors()
{
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate < 2000)
        return;
    lastUpdate = millis();

    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t))
        dhtTemp = t;
    if (!isnan(h))
        dhtHumidity = h;

    ds18b20Temp += random(-5, 6) / 100.0;
    if (ds18b20Temp < 19.7)
        ds18b20Temp = 19.7;
    if (ds18b20Temp > 21.2)
        ds18b20Temp = 21.2;

    lux = lightMeter.readLightLevel();

    int raw = analogRead(PH_PIN);
    phValue = map(raw, 0, 4095, 0, 1400) / 100.0;

    pressure_hPa = bmp.readPressure() / 100.0;
}

void updateDisplay()
{
    if (currentState == previousState && !forceDisplayUpdate &&
        millis() - lastDisplayUpdate < displayUpdateInterval)
        return;

    lcd.clear();
    lastDisplayUpdate = millis();
    previousState = currentState;
    forceDisplayUpdate = false;

    if (currentState == MAIN_MENU)
    {
        const char *items[] = {"DHT11", "DS18B20", "BH1750", "pH Sensor", "Pressure", "Relay"};
        lcd.setCursor(0, 0);
        lcd.print("==== MAIN MENU ====");
        for (int i = 0; i < 3; i++)
        {
            int idx = (menuIndex + i - 1 + 6) % 6;
            lcd.setCursor(0, i + 1);
            lcd.print(i == 1 ? ">" : " ");
            lcd.print(items[idx]);
        }
    }
    else if (currentState == PRESSURE_DISPLAY)
    {
        lcd.setCursor(0, 0);
        lcd.print("=== PRESSURE ===");
        lcd.setCursor(0, 2);
        lcd.print(pressure_hPa, 1);
        lcd.print(" hPa");
        lcd.setCursor(0, 3);
        lcd.print(">Back");
    }
}

void setup()
{
    Wire.begin(21, 22);
    lcd.init();
    lcd.backlight();

    dht.begin();
    lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
    bmp.begin();

    pinMode(ENC_CLK, INPUT_PULLUP);
    pinMode(ENC_DT, INPUT_PULLUP);
    pinMode(ENC_SW, INPUT_PULLUP);
    lastCLK = digitalRead(ENC_CLK);
    attachInterrupt(digitalPinToInterrupt(ENC_CLK), encoderISR, CHANGE);

    pinMode(RELAY_MOTOR, OUTPUT);
    pinMode(RELAY_LIGHT, OUTPUT);
    pinMode(RELAY_FAN, OUTPUT);

    digitalWrite(RELAY_MOTOR, RELAY_OFF);
    digitalWrite(RELAY_LIGHT, RELAY_OFF);
    digitalWrite(RELAY_FAN, FAN_RELAY_OFF);

    randomSeed(analogRead(0));

    displayWelcome();
}

void loop()
{
    handleEncoder();
    updateSensors();
    updateDisplay();
}
