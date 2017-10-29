/*
 * spinner
 * 
 * Drive a bipolar stepper motor synchronously from a teensy (plus amplifier).
 * Generates two sine waves with 90 degrees of phase separation suitable
 * for driving a stepper at the chosen rpm.
 */

#include <Encoder.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Settings
#define GEAR_RATIO            7.342       // ratio of the platter diameter to the pulley diameter
#define STEP_ANGLE            7.5         // step angle of your stepper motor
#define RPM_TOGGLE_LEFT       33.333      // rpm for switch in left position
#define RPM_TOGGLE_RIGHT      45.000      // rpm for switch in right position
#define MIN_SINE_FREQ         10.0        // minimum frequency to allow for driving stepper
#define MAX_SINE_FREQ         75.0       // maximum frequency to allow for driving stepper
#define LED_PIN               LED_BUILTIN // LED pin
#define DAC_PIN_A             A21         // first DAC output pin
#define DAC_PIN_B             A22         // second DAC output pin
#define FREQ_TOGGLE_PIN       A14         // pin connected to frequency toggle switch
#define ENC_SWITCH_PIN        A15         // encoder pin for toggling manual control
#define ENC_DECREASE_PIN      A16         // encoder pin for decreasing freq
#define ENC_INCREASE_PIN      A17         // encoder pin for increasing freq
#define STEPPER_AMP_SCL_PIN   A18         // scl pin for controlling stepper amp gain
#define STEPPER_AMP_SDA_PIN   A19         // sda pin for controlling stepper amp gain
#define STEPPER_AMP_ADDRESS   0x4B        // i2c address for the stepper amp
#define STEPPER_AMP_RES       64          // bit resolution of the stepper amp gain control
#define OLED_DATA             A9          // oled DATA line
#define OLED_CLK              A8          // oled CLK line
#define OLED_DC               A7          // oled D/C line
#define OLED_CS               A6          // oled CS line
#define OLED_RST              A5          // oled RST line
#define DAC_RESOLUTION        12          // resolution of the digital-to-analog converters on your board
#define SAMPLES_PER_CYCLE     600         // number of discreet points in the generated sine waves; higher values are smoother but more processer intensive; should be a multiple of 4

Encoder encoder(ENC_DECREASE_PIN, ENC_INCREASE_PIN);                        // encoder object for reading the encoder value
Adafruit_SSD1306 oled(OLED_DATA, OLED_CLK, OLED_DC, OLED_RST, OLED_CS);     // OLED display object for writing to the screen
IntervalTimer timer;                                                        // timer object for the interrupt
float freqLeft;                                                             // stepper frequency for toggle left
float freqRight;                                                            // stepper frequency for toggle right
int useEncoder = false;                                                     // should encoder control frequency
bool encoderHasBeenPressed = false;                                         // whether the encoder has been toggled in this cycle (debounce)
float sineFrequency;                                                        // derived frequency (hz) of the sine wave to drive stepper at the requested RPMs
float interruptMicroseconds;                                                // derived microseconds between interrupts to achieve the frequency
uint16_t sineTable[SAMPLES_PER_CYCLE];                                      // array of sine wave samples
uint32_t waveIndexA = SAMPLES_PER_CYCLE / 4;                                // index into the sine table for the first wave (90 degrees ahead of the second wave)
uint32_t waveIndexB = 0;                                                    // index into the sine table for the second wave

float getSineFrequency(float stepperFrequency) {
  return ((360.0 / STEP_ANGLE) * (stepperFrequency / 60.0)) / 4.0;
}

float getInterruptMicroseconds(float sineFrequency) {
  return 1000000.0 / (sineFrequency * SAMPLES_PER_CYCLE);
}

void oledDisplayRPM(float sineFrequency) {
  // need to derive the platter rpm from sine frequency
  float platterRPM = 4.0 * 60.0 * STEP_ANGLE * sineFrequency / 360.0 / GEAR_RATIO;
  oled.clearDisplay();
  oled.setCursor(0,0);
  oled.println(String(String(platterRPM, 2) + " rpm"));
  oled.display();
}

void setGain(float sineFrequency) {
  // determine optimal gain for smoothest stepper operation
  // for now let's just assume our max frequency needs max gain, and vary linearly
  int8_t gain = (int8_t) ((STEPPER_AMP_RES-1) * sineFrequency / MAX_SINE_FREQ);
  Wire.beginTransmission(STEPPER_AMP_ADDRESS);
  Wire.write(gain);
  Wire.endTransmission();
}

// create the table of individual samples for the discreet sine waves
void createSineTable() {
  float dacBitRange = pow(2.0, DAC_RESOLUTION) - 1;
  for(uint32_t i = 0; i < SAMPLES_PER_CYCLE; i++) {
    // sine sample normalised to the correct bit range
    sineTable[i] = (uint16_t)  (((1 + sin(((2.0*PI) / SAMPLES_PER_CYCLE) * i)) * dacBitRange) / 2);
  }
}

void sample() {
  waveIndexA++;
  waveIndexB++;
  if (waveIndexA > SAMPLES_PER_CYCLE) {
    waveIndexA = 0;
  }
  if (waveIndexB > SAMPLES_PER_CYCLE) {
    waveIndexB = 0;
  }
  analogWrite(DAC_PIN_A, sineTable[waveIndexA]);
  analogWrite(DAC_PIN_B, sineTable[waveIndexB]);
}

void setup() {
  
  createSineTable();
  
  pinMode(LED_PIN, OUTPUT);
  pinMode(DAC_PIN_A, OUTPUT);
  pinMode(DAC_PIN_B, OUTPUT);
  pinMode(ENC_SWITCH_PIN, INPUT_PULLUP);
  pinMode(FREQ_TOGGLE_PIN, INPUT);

  Wire.setSDA(STEPPER_AMP_SDA_PIN);
  Wire.setSCL(STEPPER_AMP_SCL_PIN);
  Wire.begin();
  
  analogWriteResolution(DAC_RESOLUTION);

  encoder.write(0); // initialize encoder to 0
  freqLeft = getSineFrequency(RPM_TOGGLE_LEFT*GEAR_RATIO);
  freqRight = getSineFrequency(RPM_TOGGLE_RIGHT*GEAR_RATIO);
  sineFrequency = digitalRead(FREQ_TOGGLE_PIN) == LOW ? freqLeft : freqRight;
  interruptMicroseconds = getInterruptMicroseconds(sineFrequency);

  setGain(sineFrequency);

  oled.begin(SSD1306_SWITCHCAPVCC);
  oled.clearDisplay();
  oled.setTextSize(2);
  oled.setTextColor(WHITE);
  oledDisplayRPM(sineFrequency);
  
  digitalWrite(LED_PIN, HIGH);
  
  interrupts();
  timer.begin(sample, interruptMicroseconds);
}

void loop() {
  float newFreq;

  // if encode is pressed, toggle using encoder and enable debouncing
  if (!encoderHasBeenPressed && digitalRead(ENC_SWITCH_PIN) == LOW) {
    encoderHasBeenPressed = true;
    useEncoder = !useEncoder;
    encoder.write(0);
    return;
  }
  else if (digitalRead(ENC_SWITCH_PIN) == HIGH) {
    encoderHasBeenPressed = false;
  }
  
  // if in manual mode, read in from the encoder and update vars
  if (useEncoder) {
    newFreq = sineFrequency + encoder.read() / 4.0; // combine the encoder reading with the previous frequency and reset
    encoder.write(0); // reset encoder
  }
  // otherwise use the chosen toggle preset
  else {
    newFreq = digitalRead(FREQ_TOGGLE_PIN) == LOW ? freqLeft : freqRight;
    
  }

  // enforce reasonable sine frequency boundaries
  if (newFreq < MIN_SINE_FREQ) {
    newFreq = MIN_SINE_FREQ;
  }
  else if (newFreq > MAX_SINE_FREQ) {
    newFreq = MAX_SINE_FREQ;
  }

  // update the interrupt and display only if necessary
  if (newFreq != sineFrequency) {
    sineFrequency = newFreq;
    interruptMicroseconds = getInterruptMicroseconds(sineFrequency);
    setGain(sineFrequency);
    oledDisplayRPM(sineFrequency);
    timer.end();
    timer.begin(sample, interruptMicroseconds);
  }
}
