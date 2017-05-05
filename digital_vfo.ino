////////////////////////////////////////////////////////////////////////////////
// A digital VFO using the DDS-60 card.
//
// The VFO will generate signals in the range 1.000000MHz to 30.000000MHz
// with a step of 1Hz.
//
// The interface will be a single rotary encoder with a built-in pushbutton.
// The frequency display will have a 'selected' digit which can be moved left
// and right by pressing the encoder knob and twisting left or right.
// Turning the encoder knob will increment or decrement the selected digit by 1
// with overflow or underflow propagating to the left.
////////////////////////////////////////////////////////////////////////////////

#include <LiquidCrystal.h>
#include <EEPROM.h>


// display constants - below is for ubiquitous small HD44780 16x2 display
#define NUM_ROWS        2
#define NUM_COLS        16

// define data pins we connect to the LCD
static byte lcd_RS = 7;
static byte lcd_ENABLE = 8;
static byte lcd_D4 = 9;
static byte lcd_D5 = 10;
static byte lcd_D6 = 11;
static byte lcd_D7 = 12;

// define data pins used by the rotary encoder
static int re_pinA = 2;     // encoder A pin
static int re_pinB = 3;     // encoder B pin
static int re_pinPush = 4;  // encoder pushbutton pin

// max and min frequency showable
#define MAX_FREQ        30000000L
#define MIN_FREQ        1000000L

// size of frequency display in chars (30MHz is maximum frequency)
#define MAX_FREQ_CHARS  8

// address in display CGRAM for definable characters
#define SELECT_CHAR     0   // shows 'underlined' decimal digits (dynamic, 0 to 9)
#define SPACE_CHAR      1   // shows an 'underlined' space character

// define the numeric digits and space with selection underline
byte sel0[8] = {0xe,0x11,0x13,0x15,0x19,0x11,0xe,0x1f};
byte sel1[8] = {0x4,0xc,0x4,0x4,0x4,0x4,0xe,0x1f};
byte sel2[8] = {0xe,0x11,0x1,0x2,0x4,0x8,0x1f,0x1f};
byte sel3[8] = {0x1f,0x2,0x4,0x2,0x1,0x11,0xe,0x1f};
byte sel4[8] = {0x2,0x6,0xa,0x12,0x1f,0x2,0x2,0x1f};
byte sel5[8] = {0x1f,0x10,0x1e,0x1,0x1,0x11,0xe,0x1f};
byte sel6[8] = {0x6,0x8,0x10,0x1e,0x11,0x11,0xe,0x1f};
byte sel7[8] = {0x1f,0x1,0x2,0x4,0x8,0x8,0x8,0x1f};
byte sel8[8] = {0xe,0x11,0x11,0xe,0x11,0x11,0xe,0x1f};
byte sel9[8] = {0xe,0x11,0x11,0xf,0x1,0x2,0xc,0x1f};
byte selspace[8] = {0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x1f};

// array of references to the 11 'selected' characters (0 to 9 plus space)
byte *sel_digits[] = {sel0, sel1, sel2, sel3, sel4, sel5, sel6, sel7, sel8, sel9, selspace};
#define SPACE_INDEX   10

// map select_offset to bump values
unsigned long offset2bump[] = {1,           // offset = 0
                               10,          // 1
                               100,         // 2
                               1000,        // 3
                               10000,       // 4
                               100000,      // 5
                               1000000,     // 6
                               10000000,    // 7
                               100000000};  // 8

// values updated by rotary encoder interrupt routines
volatile byte encoderCount = 0;

// define the display connections
LiquidCrystal lcd(lcd_RS, lcd_ENABLE, lcd_D4, lcd_D5, lcd_D6, lcd_D7);

// old values of 're_delta' and 're_position'
int oldEncOffset = 1;
byte oldEncPos = 180;


////////////////////////////////////////////////////////////////////////////////
// The VFO state variables
////////////////////////////////////////////////////////////////////////////////

unsigned long VfoFrequency;   // VFO frequency (Hz)
int VfoSelectDigit;           // selected digit, indexed from zero at right


////////////////////////////////////////////////////////////////////////////////
// Utility routines for the display.
////////////////////////////////////////////////////////////////////////////////

//------------------------------------------------------------------------------
// Function to convert an unsigned long into an array of byte digit values.
//     buf      address of buffer for byte results
//     bufsize  size of the 'buf' buffer
//     value    the unsigned long value to convert
// The function won't overflow the given buffer, it will truncate at the left.
// 
// For example, given the value 1234 and a buffer of length 7, will fill the
// buffer with 0001234.  Given 123456789 it will fill with 3456789.
//------------------------------------------------------------------------------
void ltobbuff(char *buf, int bufsize, unsigned long value)
{
  char *ptr = buf + bufsize - 1;

  for (int i = 0; i < bufsize; ++i)
  {
    int rem = value % 10;
    
    value = value / 10;
    *ptr-- = char(rem);
  }
}

//------------------------------------------------------------------------------
// Print the frequency on the display with selected colum underlined.
//     freq  the frequency to display
//     col   the selection offset of digit to underline
//           (0 is rightmost digit, increasing to the left)
// The row and columns used to show frequency digits are defined elsewhere.
// A final "Hz" is written. 
//------------------------------------------------------------------------------
void print_freq(unsigned long freq, int col)
{
  char buf [MAX_FREQ_CHARS];
  int index = MAX_FREQ_CHARS - col - 1;
  bool lead_zero = true;

  ltobbuff(buf, MAX_FREQ_CHARS, freq);

  lcd.createChar(SELECT_CHAR, sel_digits[int(buf[index])]);
  lcd.setCursor(NUM_COLS - MAX_FREQ_CHARS - 2, 0);
  for (int i = 0; i < MAX_FREQ_CHARS; ++i)
  {
    int char_val = buf[i];

    if (char_val != 0)
        lead_zero = false;

    if (lead_zero)
    {
      if (index == i)
        lcd.write(byte(SPACE_CHAR));
      else
        lcd.write(" ");
    }
    else
    {
      if (index == i)
        lcd.write(byte(SELECT_CHAR));
      else
        lcd.write(char_val + '0');
    }
  }
  
  lcd.write("Hz");
}

////////////////////////////////////////////////////////////////////////////////
// Interrupt driven rotary encoder interface.
////////////////////////////////////////////////////////////////////////////////

// states for the encode machine
static int re_stateUp = 0;
static int re_stateDown = 1;
volatile int re_state = 0;

////////////////////////////
// internal state variables
////////////////////////////

// expecting rising edge on pinA - at detent
volatile byte aFlag = 0;

// expecting rising edge on pinA - at detent
volatile byte bFlag = 0;

// this variable stores our current value of encoder position.
// Change to int or uin16_t instead of byte if you want to record a larger range than 0-255
volatile long re_delta = 0;

// stores latest offset value
volatile int re_position = 0;

//----------------------------------------------------------
// Setup the encoder stuff, pins, etc.
//----------------------------------------------------------
void re_setup(int position)
{
  // set pinA as an input, pulled HIGH to the logic voltage (5V or 3.3V for most cases)
  pinMode(re_pinA, INPUT_PULLUP);
  
  // set pinB as an input, pulled HIGH to the logic voltage (5V or 3.3V for most cases)
  pinMode(re_pinB, INPUT_PULLUP);
  
  // set pinPush as an input, pulled HIGH to the logic voltage (5V or 3.3V for most cases)
  pinMode(re_pinPush, INPUT_PULLUP);

  // attach pins to IST on rising edge only
  attachInterrupt(digitalPinToInterrupt(re_pinA), pinA_isr, RISING);
  attachInterrupt(digitalPinToInterrupt(re_pinB), pinB_isr, RISING);
  attachInterrupt(digitalPinToInterrupt(re_pinPush), pinPush_isr, CHANGE);

  // start at given select digit position
  re_position = position;
}

void bump_encoder_posn(long bump)
{
  if (re_state == re_stateUp)
  {
    re_delta += bump * offset2bump[re_position];
  }
  else if (re_state == re_stateDown)
  {
    re_position -= bump;
    if (re_position < 0)
      re_position = 0;
    if (re_position > 7)
      re_position = 7;
  }
}

void pinPush_isr()
{
  // stop interrupts, read Push pin, restore interrupts
  byte reading;

  cli();
    reading = PIND & 0x10;
    if (reading)
      re_state = re_stateUp;
    else
      re_state = re_stateDown;
  sei();
}

void pinA_isr()
{
  // stop interrupts, read A & B pins, restore interrupts
  byte reading;

  cli();
    reading = PIND & 0xC;

    if (reading == B00001100 && aFlag)
    { // check that we have both pins at detent (HIGH) and that we are expecting detent on
      // this pin's rising edge
      bump_encoder_posn(-1);
      bFlag = 0; //reset flags for the next turn
      aFlag = 0; //reset flags for the next turn
    }
    else if (reading == B00000100)
      // show we're expecting pinB to signal the transition to detent from free rotation
      bFlag = 1;
  sei();
}

void pinB_isr()
{
  // stop interrupts, read A & B pins, restore interrupts
  byte reading;

  cli();
  reading = PIND & 0xC;
  
  if (reading == B00001100 && bFlag)
  { // check that we have both pins at detent (HIGH) and that we are expecting detent on
    // this pin's rising edge
    bump_encoder_posn(+1);
    bFlag = 0; //reset flags for the next turn
    aFlag = 0; //reset flags for the next turn
  }
  else if (reading == B00001000)
    // show we're expecting pinA to signal the transition to detent from free rotation
    aFlag = 1;
  sei();
}

////////////////////////////////////////////////////////////////////////////////
// Code to save/restore in EEPROM.
////////////////////////////////////////////////////////////////////////////////

// address for unsigned long 'frequency'
static int AddressFreq = 0;

// address for int 'selected digit'
static int AddressSelDigit = AddressFreq + sizeof(AddressFreq);


// save VFO state to EEPROM
void save_to_eeprom()
{
  EEPROM.put(AddressFreq, VfoFrequency);
  EEPROM.put(AddressSelDigit, VfoSelectDigit);
}

// restore VFO state from EEPROM
void restore_from_eeprom()
{
  EEPROM.get(AddressFreq, VfoFrequency);
  EEPROM.get(AddressSelDigit, VfoSelectDigit);
}

////////////////////////////////////////////////////////////////////////////////
// Code to handle the DDS-60.
////////////////////////////////////////////////////////////////////////////////

void update_dds60(unsigned long freq)
{
  
}

//------------------------------------------------------------------------------
// The standard Arduino setup() function.
//------------------------------------------------------------------------------

void setup()
{
  Serial.begin(115200);

  // initialize the display
  lcd.begin(NUM_COLS, NUM_COLS);      // define display size
  lcd.clear();
  lcd.createChar(SPACE_CHAR, sel_digits[SPACE_INDEX]);

  restore_from_eeprom();
  
  // set up the rotary encoder
  re_setup(VfoSelectDigit);
}

//------------------------------------------------------------------------------
// Standard Arduino loop() function.
//------------------------------------------------------------------------------
void loop()
{
  static unsigned long old_freq = 0L;
  static int old_position = -1;
  
  long copy_re_delta;
  
  // do an atomic copy/reset of the volatile RE state variables
  cli();
    copy_re_delta = re_delta;
    VfoSelectDigit = re_position;
    re_delta = 0L;
  sei();

  // calculate new frequency, limit if required
  VfoFrequency += copy_re_delta;
  if (VfoFrequency > MAX_FREQ)
    VfoFrequency = MAX_FREQ;
  if (VfoFrequency < MIN_FREQ)
    VfoFrequency = MIN_FREQ;

  // display frequency if changed, update DDS-60
  if (old_freq != VfoFrequency || old_position != VfoSelectDigit)
  {
    print_freq(VfoFrequency, VfoSelectDigit);
    old_freq = VfoFrequency;
    old_position = VfoSelectDigit;

    save_to_eeprom();
    
    update_dds60(VfoFrequency);
  }
}
