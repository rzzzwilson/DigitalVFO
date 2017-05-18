////////////////////////////////////////////////////////////////////////////////
// A digital VFO using the DDS-60 card.
//
// The VFO will generate signals in the range 1.000000MHz to 30.000000MHz
// with a step ranging down to 1Hz.
//
// The interface will be a single rotary encoder with a built-in pushbutton.
// The frequency display will have a 'selected' digit which can be moved left
// and right by pressing the encoder knob and twisting left or right.
// Turning the encoder knob will increment or decrement the selected digit by 1
// with overflow or underflow propagating to the left.
////////////////////////////////////////////////////////////////////////////////

#include <LiquidCrystal.h>
#include <EEPROM.h>


// macro to get number of elements in an array
#define ARRAY_LEN(a)    (sizeof(a)/sizeof(a[0]))

//#define VFO_RESET   // define if resetting all EEPROM data
#define VFO_DEBUG   // define if debugging


// Digital VFO program name & version
const char *ProgramName = "DigitalVFO";
const char *Version = "0.9";
const char *Callsign = "vk4fawr";
const char *Callsign16 = "vk4fawr         ";

// display constants - below is for ubiquitous small HD44780 16x2 display
#define NUM_ROWS        2
#define NUM_COLS        16

// define one row of blanks
const char *BlankRow = "                ";

// define data pins we connect to the LCD
const byte lcd_RS = 7;
const byte lcd_ENABLE = 8;
const byte lcd_D4 = 9;
const byte lcd_D5 = 10;
const byte lcd_D6 = 11;
const byte lcd_D7 = 12;

// define data pins used by the rotary encoder
const int re_pinA = 2;     // encoder A pin
const int re_pinB = 3;     // encoder B pin
const int re_pinPush = 4;  // encoder pushbutton pin

// define pin controlling contrast
const byte mc_Brightness = 5;
const byte mc_Contrast = 6;

// define pins that control the DDS-60
const byte DDS_FQ_UD = 14;    // connected to AD9851 device select pin
const byte DDS_W_CLK = 15;    // connected to AD9851 clock pin
const byte DDS_DATA = 16;     // connected to AD9851 D7 (serial data) pin 


// max and min frequency showable
#define MAX_FREQ        30000000L
#define MIN_FREQ        1000000L

// size of frequency display in chars (30MHz is maximum frequency)
#define MAX_FREQ_CHARS  8

// address in display CGRAM for definable characters
#define SELECT_CHAR     0     // shows 'underlined' decimal digits (dynamic, 0 to 9)
#define SPACE_CHAR      1     // shows an 'underlined' space character
#define ALLSET_CHAR     0xff  // shows an 'all bits set' character, used for 'bar' display

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

// define the VFOevents
#define vfo_None      0
#define vfo_RLeft     1
#define vfo_RRight    2
#define vfo_DnRLeft   3
#define vfo_DnRRight  4
#define vfo_Click     5
#define vfo_HoldClick 6

// the "in use" display character
#define IN_USE_CHAR   0x7e

// default LCD contrast & brightness
const int DefaultLcdContrast = 70;
const int DefaultLcdBrightness = 150;


////////////////////////////////////////////////////////////////////////////////
// The VFO state variables and typedefs
////////////////////////////////////////////////////////////////////////////////

typedef unsigned long Frequency;
Frequency VfoFrequency;     // VFO frequency (Hz)

typedef int SelOffset;
SelOffset VfoSelectDigit;   // selected column index, zero at the right

typedef byte VFOEvent;

int LcdContrast;
int LcdBrightness = 255;


////////////////////////////////////////////////////////////////////////////////
// Utility routines
////////////////////////////////////////////////////////////////////////////////

//----------------------------------------
// Abort the program.
// Tries to tell the world what went wrong, then just loops.
//     msg  address of error string
// Only first 32 chars of message is displayed.
//----------------------------------------

void abort(const char *msg)
{
  char buf[NUM_COLS*2+1];
  char *ptr = buf;
  
  // print error on console (maybe)
  Serial.printf(F("message=%s\nTeensy is paused!\n"), msg);

  // truncate/pad message to 32 chars
  for (int i = 0; i < NUM_COLS*2; ++i)
    *ptr++ = ' ';
  *ptr = '\0';
  
  strncpy(buf, msg, NUM_COLS*2);
  if (strlen(msg) < NUM_COLS*2)
    strncpy(buf + strlen(msg), "                                ", NUM_COLS*2 - strlen(msg));

  // show what we can on the display, forever
  while (1)
  {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(buf);
    lcd.setCursor(0, 1);
    lcd.print(buf + NUM_COLS);
    delay(2000);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(" ");   // padding to centre name+version
    lcd.print(ProgramName);
    lcd.print(" ");
    lcd.print(Version);
    lcd.setCursor(0, 1);
    lcd.print("   is paused");
    delay(2000);
  }
}

//----------------------------------------
// Convert an event number to a display string
//----------------------------------------

const char * event2display(VFOEvent event)
{
  switch (event)
  {
    case vfo_None:      return "vfo_None";
    case vfo_RLeft:     return "vfo_RLeft";
    case vfo_RRight:    return "vfo_RRight";
    case vfo_DnRLeft:   return "vfo_DnRLeft";
    case vfo_DnRRight:  return "vfo_DnRRight";
    case vfo_Click:     return "vfo_Click";
    case vfo_HoldClick: return "vfo_HoldClick";
    default:            return "UNKNOWN!";
  }
}

//----------------------------------------
// display a simple banner on the LCD
//----------------------------------------

#ifndef VFO_DEBUG
void banner(void)
{
  Serial.printf(F("%s %s (%s)\n"), ProgramName, Version, Callsign);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(ProgramName);
  lcd.print(" ");
  lcd.print(Version);
  lcd.setCursor(0, 1);
  lcd.print(Callsign);
  delay(2000);    // wait a bit

  for (int i = 0; i <= NUM_COLS; ++i)
  {
    lcd.clear();
    lcd.setCursor(i, 0);
    lcd.print(ProgramName);
    lcd.print(" ");
    lcd.print(Version);
    lcd.setCursor(0, 1);
    lcd.print(Callsign16 + i);
    delay(200);
  }

  delay(500);
}
#endif

//----------------------------------------
// Function to convert an unsigned long into an array of byte digit values.
//     buf      address of buffer for byte results
//     bufsize  size of the 'buf' buffer
//     value    the Frequency value to convert
//
// The function won't overflow the given buffer, it will truncate at the left.
// For example, given the value 1234 and a buffer of length 7, will fill the
// buffer with 0001234.  Given 123456789 it will fill with 3456789.
//
// Each byte in the buffer is a number in [0, 9], NOT ['0', '9'].
// The resultant buffer does NOT have a terminating '\0'!
//----------------------------------------

void ulong2buff(char *buf, int bufsize, unsigned long value)
{
  Serial.printf(F("ulong2buff: bufsize=%d, value=%ld\n"), bufsize, value);
  
  char *ptr = buf + bufsize - 1;    // rightmost char in 'buf'

  for (int i = 0; i < bufsize; ++i)
  {
    int rem = value % 10;

    value = value / 10;
    *ptr-- = char(rem);

    Serial.printf(F("long2buff: stored byte %d, i=%d\n"), rem, i);
  }
}


////////////////////////////////////////////////////////////////////////////////
// Code to handle the DigitalVFO menus.
////////////////////////////////////////////////////////////////////////////////

// handler for selection of an item (vfo_Click event)
typedef void (*ItemAction)(struct Menu *, int);

// handler for inc/dec of custom item (vfo_RRight, vfo_RLeft events)
typedef int (*ItemIncDec)(int item_num, int delta);

// structure defining a menu item
struct MenuItem
{
  const char *title;          // menu item display text
  struct Menu *menu;          // if not NULL, menu to pass to show_menu()
  ItemAction action;          // if not NULL, address of action function
};

typedef struct MenuItem *MenuItemPtr;

// A structure defining a menu
struct Menu
{
  const char *title;          // title displayed on menu page
  int num_items;              // number of items in the array below
  struct MenuItem **items;    // array of pointers to MenuItem data
};

#ifdef VFO_DEBUG
// dump a MenuItem to the console
// only called from dump_menu()
void dump_menuitem(struct MenuItem *menuitem)
{
  Serial.printf(F("  menuitem address=%08x\n"), menuitem);
  Serial.printf(F("  title=%s\n"), menuitem->title);
  Serial.printf(F("  menu=%08x\n"), menuitem->menu);
  Serial.printf(F("  action=%08x\n"), menuitem->action);
}

// dump a Menu and contained MenuItems to the console
void dump_menu(const char *msg, struct Menu *menu)
{
  Serial.printf(F("----------------- Menu --------------------\n"));
  Serial.printf(F("%s\n"), msg);
  Serial.printf(F("menu address=%08x\n"), menu);
  Serial.printf(F("  title=%s\n"), menu->title);
  Serial.printf(F("  num_items=%d\n"), menu->num_items);
  Serial.printf(F("  items address=%08x\n"), menu->items);
  
  for (int i = 0; i < menu->num_items; ++i)
    dump_menuitem(menu->items[i]);
    
  Serial.printf(F("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n"));
}
#endif

//----------------------------------------
// Draw the menu on the screen
//     menu      pointer to a Menu structure
//----------------------------------------

void menu_draw(struct Menu *menu)
{
  // clear screen and write menu title on upper row
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(menu->title);
}

//----------------------------------------
// Draw a standard or custom menuitem on the screen.
//     menu      pointer to a Menu structure
//     item_num  the item number to show
//----------------------------------------

void menuitem_draw(struct Menu *menu, int item_num)
{
  // figure out max length of item strings
  int max_len = 0;

  for (int i = 0; i < menu->num_items; ++ i)
  {
    int new_len = strlen(menu->items[i]->title);

    if (new_len > max_len)
        max_len = new_len;
  }

  // write indexed item on lower row, right-justified
  lcd.setCursor(0, 1);
  lcd.print(BlankRow);
  lcd.setCursor(NUM_COLS - max_len, 1);
  lcd.print(menu->items[item_num]->title);
}

//----------------------------------------
// Draw a menu page from the passed "menu" structure.
//     menu      pointer to a defining Menu structure
//     unused    unused item number (used by action routines)
// Handle events in the loop here.
// This code doesn't see events handled in any *_action() routine.
//----------------------------------------

void menu_show(struct Menu *menu, int unused)
{
  int item_num = 0;     // index of the menuitem to show

#ifdef VFO_DEBUG
  dump_menu("menu_show, menu:", menu);
#endif

  // draw the menu page
  menu_draw(menu);
  menuitem_draw(menu, item_num);

  // get rid of any stray events to this point
  event_flush();

  while (true)
  {
    // handle any pending event
    if (event_pending() > 0)
    {
      // get next event and handle it
      byte event = event_pop();
      Serial.printf(F("menu_show loop: event=%s\n"), event2display(event));

      switch (event)
      {
        case vfo_RLeft:
          Serial.printf(F("menu_show: vfo_RLeft\n"));
          if (--item_num < 0)
            item_num = 0;
          break;
        case vfo_RRight:
          Serial.printf(F("menu_show: vfo_RRight\n"));
          if (++item_num >= menu->num_items)
            item_num = menu->num_items - 1;
          break;
        case vfo_DnRLeft:
          Serial.printf(F("menu_show: vfo_DnRLeft (ignored)\n"));
          break;
        case vfo_DnRRight:
          Serial.printf(F("menu_show: vfo_DnRRight (ignored)\n"));
          break;
        case vfo_Click:
          Serial.printf(F("menu_show: vfo_Click\n"));
          if (menu->items[item_num]->action != NULL)
          {
            // if there's a handler, call it
            // this will probably destroy the curent menu page
            (*menu->items[item_num]->action)(menu, item_num);
          }
          else
          {
            // recurse down to sub-menu
            menu_show(menu->items[item_num]->menu, 0);
          }
          menu_draw(menu);    // redraw the menu header
          Serial.printf(F("menu_show: end of vfo_Click handling\n"));
          break;
        case vfo_HoldClick:
          Serial.printf(F("menu_show: vfo_HoldClick, exit menu\n"));
          event_flush();
          return;
        default:
          Serial.printf(F("menu_show: unrecognized event %d\n"), event);
          break;
      }

      // update the menu display
      menuitem_draw(menu, item_num);
    }
  }
}

//----------------------------------------
// show that *something* happened
// flashes the menu page in a possibly eye-catching way
//----------------------------------------

void menu_flash(void)
{
  lcd.noDisplay();
  delay(100);
  lcd.display();
  delay(100);
  lcd.noDisplay();
  delay(100);
  lcd.display();
}


////////////////////////////////////////////////////////////////////////////////
// The system event queue.
// Implemented as a circular buffer.
////////////////////////////////////////////////////////////////////////////////

#define QueueLength 10

VFOEvent event_queue[QueueLength];
int queue_fore = 0;   // fore pointer into circular buffer
int queue_aft = 0;    // aft pointer into circular buffer

void event_push(VFOEvent event)
{
  // put new event into next empty slot
  event_queue[queue_fore] = event;

  // move fore ptr one slot up, wraparound if necessary
  ++queue_fore;
  if (queue_fore >= QueueLength)
    queue_fore = 0;

  // if queue full, abort
  if (queue_aft == queue_fore)
  {
      event_dump_queue("ERROR: event queue full:");
      abort("Event queue full");
  }
}

VFOEvent event_pop(void)
{
  // Must protect from RE code fiddling with queue
  noInterrupts();

  // if queue empty, return None event
  if (queue_fore == queue_aft)
    return vfo_None;

  // get next event
  VFOEvent event = event_queue[queue_aft];

  // move aft pointer up one slot, wrap if necessary
  ++queue_aft;
  if (queue_aft  >= QueueLength)
    queue_aft = 0;

  interrupts();

  return event;
}

int event_pending(void)
{
  // Must protect from RE code fiddling with queue
  noInterrupts();

  // get distance between fore and aft pointers
  int result = queue_fore - queue_aft;

  // handle case when events wrap around
  if (result < 0)
    result += QueueLength;

  interrupts();

  return result;
}

void event_flush(void)
{
  queue_fore = 0;
  queue_aft = 0;
}

void event_dump_queue(const char *msg)
{
  // Must protect from RE code fiddling with queue
  noInterrupts();

  Serial.printf(F("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"));
  Serial.printf(F("Queue: %s\n"), msg);
  for (int i = 0; i < QueueLength; ++i)
  {
    VFOEvent event = event_queue[i];

    Serial.printf(F("  %d -> %s\n"), event, event2display(event));
  }
  Serial.printf(F("Queue length=%d\n"), event_pending());
  Serial.printf(F("queue_aft=%d\n"), queue_aft);
  Serial.printf(F(", queue_fore=%d\n"), queue_fore);
  Serial.printf(F("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"));

  interrupts();
}


////////////////////////////////////////////////////////////////////////////////
// Utility routines for the display.
////////////////////////////////////////////////////////////////////////////////

//----------------------------------------
// Print the frequency on the display with selected colum underlined.
//     freq     the frequency to display
//     sel_col  the selection offset of digit to underline
//              (0 is rightmost digit, increasing to the left)
//     row      the row to use, 0 is at top
// The row and columns used to show frequency digits are defined elsewhere.
//----------------------------------------

void print_freq(Frequency freq, int sel_col, int row)
{
  char buf [MAX_FREQ_CHARS];
  int index = MAX_FREQ_CHARS - sel_col - 1;
  bool lead_zero = true;

  Serial.printf(F("print_freq: freq=%ld, row=%d\n"), freq, row);
  
  ulong2buff(buf, MAX_FREQ_CHARS, freq);

  lcd.createChar(SELECT_CHAR, sel_digits[int(buf[index])]);
  lcd.setCursor(NUM_COLS - MAX_FREQ_CHARS - 2, row);
  for (int i = 0; i < MAX_FREQ_CHARS; ++i)
  {
    int char_val = buf[i];

    if (char_val != 0)
        lead_zero = false;

    if (lead_zero)
    {
      if (index == i)
      {
        lcd.write(byte(SPACE_CHAR));
        Serial.printf(F("print_freq: wrote selected space\n"));
      }
      else
      {
        lcd.write(" ");
        Serial.printf(F("print_freq: wrote space\n"));
      }
    }
    else
    {
      if (index == i)
      {
        lcd.write(byte(SELECT_CHAR));
        Serial.printf(F("print_freq: wrote selected char\n"));
      }
      else
      {
        lcd.write(char_val + '0');
        Serial.printf("print_freq: wrote char %d\n", char_val);
      }
    }
  }
}

//----------------------------------------
// Display an unsigned long on the display with selected colum underlined.
//     value       the number to display
//     sel_col     the selection offset of digit to underline
//                 (0 is rightmost digit, increasing to the left)
//     num_digits  the number of digits to show on the display
//     col, row    position to display left-most digit at
// The LCD cursor position is assumed set before this function is called.
// If the value is too long it will be truncated at the left.
//----------------------------------------

void display_sel_value(unsigned long value, int sel_col, int num_digits, int col, int row)
{
  char buf [num_digits];
  int index = num_digits - sel_col - 1;
  bool lead_zero = true;

  Serial.printf(F("display_sel_value: value=%ld, num_digits=%d\n"), value, num_digits);
  
  ulong2buff(buf, num_digits, value);

  lcd.createChar(SELECT_CHAR, sel_digits[int(buf[index])]);
  lcd.setCursor(col, row);  // because we lose cursor position after lcd.createChar()!?
  for (int i = 0; i < num_digits; ++i)
  {
    int char_val = buf[i];

    if (char_val != 0)
        lead_zero = false;

    if (lead_zero)
    {
      if (index == i)
      {
        lcd.write(byte(SPACE_CHAR));
        Serial.printf(F("display_sel_value: wrote selected space\n"));
      }
      else
      {
        lcd.write(" ");
        Serial.printf(F("display_sel_value: wrote space\n"));
      }
    }
    else
    {
      if (index == i)
      {
        lcd.write(byte(SELECT_CHAR));
        Serial.printf(F("display_sel_value: wrote selected char\n"));
      }
      else
      {
        lcd.write(char_val + '0');
        Serial.printf("display_sel_value: wrote char %d\n", char_val);
      }
    }
  }
}


////////////////////////////////////////////////////////////////////////////////
// Interrupt driven rotary encoder interface.
// from code by Simon Merrett, based on insight from Oleg Mazurov, Nick Gammon, rt, Steve Spence
////////////////////////////////////////////////////////////////////////////////

// time when click becomes a "hold click" (milliseconds)
// the delay is configurable in the UI
#define MinHoldClickTime        100
#define MaxHoldClickTime        1000
#define DefaultHoldClickTime    500

int ReHoldClickTime = DefaultHoldClickTime;

// internal variables
bool re_rotation = false;       // true if rotation occurred while knob down
bool re_down = false;           // true while knob is down
Frequency re_down_time = 0; // milliseconds when knob is pressed down

// expecting rising edge on pinA - at detent
volatile byte aFlag = 0;

// expecting rising edge on pinA - at detent
volatile byte bFlag = 0;

//----------------------------------------
// Setup the encoder stuff, pins, etc.
//----------------------------------------

void re_setup(int position)
{
  // set RE data pins as pullup inputs
  pinMode(re_pinA, INPUT_PULLUP);
  pinMode(re_pinB, INPUT_PULLUP);
  pinMode(re_pinPush, INPUT_PULLUP);

  // attach pins to IST on rising edge only
  attachInterrupt(digitalPinToInterrupt(re_pinA), pinA_isr, RISING);
  attachInterrupt(digitalPinToInterrupt(re_pinB), pinB_isr, RISING);
  attachInterrupt(digitalPinToInterrupt(re_pinPush), pinPush_isr, CHANGE);
}

void pinPush_isr(void)
{
  re_down = ! (PIND & 0x10);
  if (re_down)
  {
    // button pushed down
    re_rotation = false;      // no rotation while down so far
    re_down_time = millis();  // note time we went down
  }
  else
  {
    // button released, check if rotation, UP event if not
    if (! re_rotation)
    {
      int push_time = millis() - re_down_time;

      if (push_time < ReHoldClickTime)
      {
        event_push(vfo_Click);
      }
      else
      {
        event_push(vfo_HoldClick);
      }
    }
  }
}

void pinA_isr(void)
{
  byte reading = PIND & 0xC;

  if (reading == B00001100 && aFlag)
  { // check that we have both pins at detent (HIGH) and that we are expecting detent on
    // this pin's rising edge
    if (re_down)
    {
      event_push(vfo_DnRLeft);
      re_rotation = true;
    }
    else
    {
      event_push(vfo_RLeft);
    }
    bFlag = 0; //reset flags for the next turn
    aFlag = 0; //reset flags for the next turn
  }
  else if (reading == B00000100)
  {
    // show we're expecting pinB to signal the transition to detent from free rotation
    bFlag = 1;
  }
}

void pinB_isr(void)
{
  byte reading = PIND & 0xC;

  if (reading == B00001100 && bFlag)
  { // check that we have both pins at detent (HIGH) and that we are expecting detent on
    // this pin's rising edge
    if (re_down)
    {
      event_push(vfo_DnRRight);
      re_rotation = true;
    }
    else
    {
      event_push(vfo_RRight);
    }
    bFlag = 0; //reset flags for the next turn
    aFlag = 0; //reset flags for the next turn
  }
  else if (reading == B00001000)
  {
    // show we're expecting pinA to signal the transition to detent from free rotation
    aFlag = 1;
  }
}


////////////////////////////////////////////////////////////////////////////////
// Code to save/restore in EEPROM.
////////////////////////////////////////////////////////////////////////////////

// start storing at address 0
const int EEPROMBase = 0;

// address for Frequency 'frequency'
const int AddressFreq = EEPROMBase;
const int AddressFreqSize = sizeof(AddressFreq);

// address for int 'selected digit'
const int AddressSelDigit = AddressFreq + AddressFreqSize;
const int AddressSelDigitSize = sizeof(int);

// address for byte 'contrast'
const int AddressContrast = AddressSelDigit + AddressSelDigitSize;
const int AddressContrastSize = sizeof(LcdContrast);

// address for byte 'brightness'
const int AddressBrightness = AddressContrast + AddressContrastSize;
const int AddressBrightnessSize = sizeof(LcdBrightness);

// address for int 'hold click time'
const int AddressHoldClickTime = AddressBrightness + AddressBrightnessSize;
const int AddressHoldClickTimeSize = sizeof(ReHoldClickTime);

// number of frequency save slots in EEPROM
const int NumSaveSlots = 10;

const int SaveFreqSize = sizeof(long);
const int SaveFreqBase = AddressHoldClickTime + AddressHoldClickTimeSize;
const int SaveFreqBaseSize = NumSaveSlots * SaveFreqSize;

//also save the offset for each frequency
const int SaveOffsetSize = sizeof(SelOffset);
const int SaveOffsetBase = SaveFreqBase + SaveFreqBaseSize;
const int SaveOffsetBaseSize = NumSaveSlots * SaveOffsetSize;

// free slot address
const int NextFreeAddress = SaveOffsetBase + SaveOffsetBaseSize;

//----------------------------------------
// save VFO state to EEPROM
//----------------------------------------

void save_to_eeprom(void)
{
  EEPROM.put(AddressFreq, VfoFrequency);
  EEPROM.put(AddressSelDigit, VfoSelectDigit);
  EEPROM.put(AddressBrightness, LcdBrightness);
  EEPROM.put(AddressContrast, LcdContrast);
  EEPROM.put(AddressHoldClickTime, ReHoldClickTime);
}

//----------------------------------------
// restore VFO state from EEPROM
//----------------------------------------

void restore_from_eeprom(void)
{
  EEPROM.get(AddressFreq, VfoFrequency);
  EEPROM.get(AddressSelDigit, VfoSelectDigit);
  EEPROM.get(AddressBrightness, LcdBrightness);
  EEPROM.get(AddressContrast, LcdContrast);
  EEPROM.get(AddressHoldClickTime, ReHoldClickTime);
}

//----------------------------------------
// given slot number, return freq/offset
//----------------------------------------

void get_slot(int slot_num, Frequency &freq, SelOffset &offset)
{
  int freq_address = SaveFreqBase + slot_num * SaveFreqSize;
  int offset_address = SaveOffsetBase + slot_num * SaveOffsetSize;

  EEPROM.get(freq_address, freq);
  EEPROM.get(offset_address, offset);
}

//----------------------------------------
// put frequency/offset into given slot number
//----------------------------------------

void put_slot(int slot_num, Frequency freq, SelOffset offset)
{
  int freq_address = SaveFreqBase + slot_num * SaveFreqSize;
  int offset_address = SaveOffsetBase + slot_num * SaveOffsetSize;

  EEPROM.put(freq_address, freq);
  EEPROM.put(offset_address, offset);
}

//----------------------------------------
// print all EEPROM saved data to console
//----------------------------------------

#ifdef VFO_DEBUG
void dump_eeprom(void)
{
  Frequency ulong;
  SelOffset offset;
  int brightness;
  int contrast;
  int hold;

  EEPROM.get(AddressFreq, ulong);
  EEPROM.get(AddressSelDigit, offset);
  EEPROM.get(AddressBrightness, brightness);
  EEPROM.get(AddressContrast, contrast);
  EEPROM.get(AddressHoldClickTime, hold);
  Serial.printf(F("=================================================\n"));
  Serial.printf(F("dump_eeprom: VfoFrequency=%ld\n"), ulong);
  Serial.printf(F("             AddressSelDigit=%d\n"), offset);
  Serial.printf(F("             LcdBrightness=%d\n"), brightness);
  Serial.printf(F("             LcdContrast=%d\n"), contrast);
  Serial.printf(F("             ReHoldClickTime=%d\n"), hold);

  for (int i = 0; i < NumSaveSlots; ++i)
  {
    get_slot(i, ulong, offset);
    Serial.printf(F("Slot %d: freq=%ld, seldig=%d\n"), i, ulong, offset);
  }

  Serial.printf(F("=================================================\n"));
}
#endif

////////////////////////////////////////////////////////////////////////////////
// Code to handle the DDS-60
//
// From: http://www.rocketnumbernine.com/2011/10/25/programming-the-ad9851-dds-synthesizer
// Andrew Smallbone <andrew@rocketnumbernine.com>
////////////////////////////////////////////////////////////////////////////////

//----------------------------------------
// pulse the 'pin' high and then low
//----------------------------------------

void dds_pulse_high(byte pin)
{
  digitalWrite(pin, HIGH);
  digitalWrite(pin, LOW);
}

//----------------------------------------
// transfer a byte a bit at a time, LSB first, to DDS_DATA pin
//----------------------------------------

void dds_tfr_byte(byte data)
{
  for (int i = 0; i < 8; ++i, data >>= 1)
  {
    digitalWrite(DDS_DATA, data & 0x01);
    dds_pulse_high(DDS_W_CLK);
  }
}

//----------------------------------------
// frequency of signwave (datasheet page 12) will be <sys clock> * <frequency tuning word> / 2^32
//----------------------------------------

void dds_update(Frequency frequency)
{
  int32_t data = frequency * 4294967296.0 / 180.0e6;

  Serial.printf(F("dds_update: frequency=%ld, data=%ld\n"), frequency, data);
  
  for (int b = 0; b < 4; ++b, data >>= 8)
  {
    dds_tfr_byte(data & 0xFF);
  }
  
  dds_tfr_byte(0x001);
  dds_pulse_high(DDS_FQ_UD);
}

//----------------------------------------
// initialize the DDS hardware
//----------------------------------------

void dds_setup(void)
{
  // all pins to outputs
  pinMode(DDS_FQ_UD, OUTPUT);
  pinMode(DDS_W_CLK, OUTPUT);
  pinMode(DDS_DATA, OUTPUT);

  // if your board needs it, connect RESET pin and pulse it to reset AD9851
  // dds_pulse_high(RESET)

  // set serial load enable (Datasheet page 15 Fig. 17) 
  dds_pulse_high(DDS_W_CLK);
  dds_pulse_high(DDS_FQ_UD);
}


////////////////////////////////////////////////////////////////////////////////
// Main VFO code
////////////////////////////////////////////////////////////////////////////////

//----------------------------------------
// The standard Arduino setup() function.
//----------------------------------------

#ifdef VFO_RESET
void zero_eeprom(void)
{
  Frequency zero_freq = 0L;
  SelOffset zero_offset = 0;
  
  // zero the frequency+selected values
  VfoFrequency = MIN_FREQ;
  VfoSelectDigit = 0;
  LcdBrightness = DefaultLcdBrightness;
  LcdContrast = DefaultLcdContrast;
  ReHoldClickTime = DefaultHoldClickTime;
  
  save_to_eeprom();

  // zero the save slots
  for (int i = 0; i < NumSaveSlots; ++i)
  {
    put_slot(i, zero_freq, zero_offset);
  }
}
#endif

void setup(void)
{
  Serial.begin(115200);

#ifdef VFO_RESET
  zero_eeprom();
  Serial.printf(F("All EEPROM data reset\n"));
#endif

  // initialize the display
  lcd.begin(NUM_COLS, NUM_COLS);      // define display size
  lcd.clear();
  lcd.noCursor();
  lcd.createChar(SPACE_CHAR, sel_digits[SPACE_INDEX]);

  pinMode(mc_Brightness, OUTPUT);
  pinMode(mc_Contrast, OUTPUT);

  // get state back from EEPROM, set display brightness/contrast
  restore_from_eeprom();
  analogWrite(mc_Brightness, LcdBrightness);
  analogWrite(mc_Contrast, LcdContrast);

  // set up the DDS device
  dds_setup();
  
  // set up the rotary encoder
  re_setup(VfoSelectDigit);

  // show program name and version number
#ifndef VFO_DEBUG
  banner();

  // we might have got a 'reset' HoldClick during banner presentation
  if (event_pop() == vfo_HoldClick)
  {
    // if holdClick during banner, reset all "Settings" values
    LcdBrightness = DefaultLcdBrightness;
    analogWrite(mc_Brightness, LcdBrightness);
    LcdContrast = DefaultLcdContrast;
    analogWrite(mc_Contrast, LcdContrast);
    ReHoldClickTime = DefaultHoldClickTime;

    // tell what happened on LCD
    lcd.clear();
    lcd.print("Reset all values");
    lcd.setCursor(0, 1);
    lcd.print("in Settings menu");

    // and Serial console
    Serial.printf(F("Reset brightness to %d, contrast to %d and hold time to %d\n"),
                  LcdBrightness, LcdContrast, ReHoldClickTime);
    Serial.printf(F("Select or rotate the rotary encoder to continue\n"));

    // wait here until there is some input action
    event_flush();
    while (! event_pending())
      delay(100);
    event_flush();
    delay(500);
  }
#endif

  // we sometimes see random events on powerup, flush them here
  event_flush();
  
#ifdef VFO_DEBUG
  // dump EEPROM values
  dump_eeprom();
#endif

  // get going
  show_main_screen();

  // we sometimes see random events on powerup, flush them here
  event_flush();
}

//----------------------------------------
//----------------------------------------

void show_main_screen(void)
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Freq:");
  display_sel_value(VfoFrequency, VfoSelectDigit, MAX_FREQ_CHARS, NUM_COLS - MAX_FREQ_CHARS - 2, 0);
//  print_freq(VfoFrequency, VfoSelectDigit, 0);
  lcd.print("Hz");
}

//----------------------------------------
// Define the menu handler routines.
//----------------------------------------

void show_slot_frequency(int slot_num)
{
  Frequency freq;         // put data from slot X here
  SelOffset offset;       // put data from slot X here

  get_slot(slot_num, freq, offset);
  lcd.setCursor(0, 1);
  lcd.print(BlankRow);
  if (VfoFrequency == freq)
  {
    lcd.setCursor(0, 1);
    lcd.write(IN_USE_CHAR);
  }
  lcd.setCursor(4, 1);
  lcd.write(slot_num + '0');
  lcd.print(":");
  print_freq(freq, -1, 1);
  lcd.print("Hz");
}

//----------------------------------------
// save the current frequency to a slot.
//   menu      address of 'calling' menu
//   item_num  index of MenuItem we were actioned from
//----------------------------------------

void saveslot_action(struct Menu *menu, int item_num)
{
  int slot_num = 0;       // the slot we are viewing/saving

  // get rid of any stray events to this point
  event_flush();

  // draw row 0 of menu
  lcd.clear();
  lcd.print(menu->items[item_num]->title);
  
  // show row slot info on row 1
  show_slot_frequency(slot_num);

  // handle events in our own little event loop
  while (true)
  {
    // handle any pending event
    if (event_pending() > 0)
    {
      byte event = event_pop(); // get next event and handle it

      switch (event)
      {
        case vfo_RLeft:
          if (--slot_num < 0)
            slot_num = 0;
          break;
        case vfo_RRight:
          if (++slot_num >= NumSaveSlots)
            slot_num = NumSaveSlots - 1;
          break;
        case vfo_Click:
          put_slot(slot_num, VfoFrequency, VfoSelectDigit);
          show_slot_frequency(slot_num);
          menu_flash();
          break;
        case vfo_HoldClick:
          event_flush();
          return;
        default:
          // ignore any events we don't handle
          break;
      }

      // show row slot info
      show_slot_frequency(slot_num);
    }
  }
}

//----------------------------------------
// restore the current frequency from a slot
//   menu      address of 'calling' menu
//   item_num  index of MenuItem we were actioned from
//----------------------------------------

void restoreslot_action(struct Menu *menu, int item_num)
{
  int slot_num = 0;       // the slot we are viewing/restoring

  // get rid of any stray events to this point
  event_flush();

  // draw row 0 of menu
  lcd.clear();
  lcd.print(menu->items[item_num]->title);
  
  // show row slot info on row 1
  show_slot_frequency(slot_num);

  // handle events in our own little event loop
  while (true)
  {
    // handle any pending event
    if (event_pending() > 0)
    {
      byte event = event_pop(); // get next event and handle it

      switch (event)
      {
        case vfo_RLeft:
          if (--slot_num < 0)
            slot_num = 0;
          break;
        case vfo_RRight:
          if (++slot_num >= NumSaveSlots)
            slot_num = NumSaveSlots - 1;
          break;
        case vfo_Click:
          get_slot(slot_num, VfoFrequency, VfoSelectDigit);
          menu_flash();
          break;
        case vfo_HoldClick:
          event_flush();
          return;
        default:
          // ignore any events we don't handle
          break;
      }

      // show row slot info
      show_slot_frequency(slot_num);
    }
  }
}

//----------------------------------------
// delete the frequency in a slot
//   menu      address of 'calling' menu
//   item_num  index of MenuItem we were actioned from
//----------------------------------------

void deleteslot_action(struct Menu *menu, int item_num)
{
  Frequency zero_freq = 0;        // zero value to be saved
  SelOffset zero_offset = 0;      // zero value to be saved
  int slot_num = 0;               // the slot we are viewing/restoring

  // get rid of any stray events to this point
  event_flush();

  // draw row 0 of menu
  lcd.clear();
  lcd.print(menu->items[item_num]->title);
  
  // show row slot info on row 1
  show_slot_frequency(slot_num);

  // handle events in our own little event loop
  while (true)
  {
    // handle any pending event
    if (event_pending() > 0)
    {
      byte event = event_pop(); // get next event and handle it

      switch (event)
      {
        case vfo_RLeft:
          if (--slot_num < 0)
            slot_num = 0;
          break;
        case vfo_RRight:
          if (++slot_num >= NumSaveSlots)
            slot_num = NumSaveSlots - 1;
          break;
        case vfo_Click:
          put_slot(slot_num, zero_freq, zero_offset);
          menu_flash();
          break;
        case vfo_HoldClick:
          event_flush();
          return;
        default:
          // ignored events we don't handle
          break;
      }

      // show row slot info
      show_slot_frequency(slot_num);
    }
  }
}

//----------------------------------------
// Draw a bar of a given length on row 1 of LCD.
//   length  length of the bar [1, 16]
//----------------------------------------

void draw_row1_bar(int length)
{
  lcd.setCursor(0, 1);
  lcd.print(BlankRow);
  lcd.setCursor(0, 1);
  for (int i = 0; i < length; ++i)
    lcd.write(ALLSET_CHAR);
}

//----------------------------------------
// Set the current contrast and save to EEPROM if 'actioned'.
//   menu      address of 'calling' menu
//   item_num  index of MenuItem we were actioned from
//----------------------------------------

void brightness_action(struct Menu *menu, int item_num)
{
  // convert brighness value to a display value in [1, 16]
  int index = (LcdBrightness + 1) / 16;
 
  // get rid of any stray events to this point
  event_flush();

  // draw row 0 of menu, get heading from MenuItem
  lcd.clear();
  lcd.print(menu->items[item_num]->title);
  
  // show row slot info on row 1
  draw_row1_bar(index);

  // handle events in our own little event loop
  while (true)
  {
    // handle any pending event
    if (event_pending() > 0)
    {
      byte event = event_pop(); // get next event and handle it

      switch (event)
      {
        case vfo_RLeft:
          if (--index < 1)
            index = 1;
          break;
        case vfo_RRight:
          if (++index > 16)
            index = 16;
          break;
        case vfo_Click:
          save_to_eeprom();
          menu_flash();
          break;
        case vfo_HoldClick:
          event_flush();
          return;
        default:
          // ignored events we don't handle
          break;
      }

      // adjust display brightness so we can see the results
      LcdBrightness = (index * 16) - 1;
      analogWrite(mc_Brightness, LcdBrightness);

      // show brightness value in row 1
      draw_row1_bar(index);
    }
  }
}

//----------------------------------------
// Set the current contrast and save to EEPROM if 'actioned'.
//   menu      address of 'calling' menu
//   item_num  index of MenuItem we were actioned from
//----------------------------------------

void contrast_action(struct Menu *menu, int item_num)
{
  // convert contrast value to a display value in [1, 16]
  int index = 16 - LcdContrast / 8;
 
  // get rid of any stray events to this point
  event_flush();

  // draw row 0 of menu, get heading from MenuItem
  lcd.clear();
  lcd.print(menu->items[item_num]->title);
  
  // show row slot info on row 1
  draw_row1_bar(index);

  // handle events in our own little event loop
  while (true)
  {
    // handle any pending event
    if (event_pending() > 0)
    {
      byte event = event_pop(); // get next event and handle it

      switch (event)
      {
        case vfo_RLeft:
          if (--index < 1)
            index = 1;
          break;
        case vfo_RRight:
          if (++index > 16)
            index = 16;
          break;
        case vfo_Click:
          save_to_eeprom();
          menu_flash();
          break;
        case vfo_HoldClick:
          event_flush();
          return;
        default:
          // ignored events we don't handle
          break;
      }

      // adjust display contrast so we can see the results
      LcdContrast = (16 - index) * 8;
      analogWrite(mc_Contrast, LcdContrast);

      // show brightness value in row 1
      draw_row1_bar(index);
    }
  }
}

//----------------------------------------
// Draw a click hold time in milliseconds on row 1 of LCD.
//----------------------------------------

void draw_row1_time(int msec)
{
  Serial.printf(F("draw_row1_time: msec=%d, ReHoldClickTime=%d\n"), msec, ReHoldClickTime);
  
  lcd.setCursor(0, 1);
  lcd.print(BlankRow);
  
  if (msec == ReHoldClickTime)
  {
    Serial.printf(F("draw_row1_time: drawing IN_USE_CHAR\n"));
    lcd.setCursor(0, 1);
    lcd.write(IN_USE_CHAR);
  }
  
  lcd.setCursor(8, 1);
  if (msec < 1000)
    lcd.setCursor(9, 1);
  lcd.print(msec);
  lcd.print("msec");
}

//----------------------------------------
// Set the current 'hold click' time and save to EEPROM if 'actioned'.
//   menu      address of 'calling' menu
//   item_num  index of MenuItem we were actioned from
// This works differently from brightness/contrast.  We show menuitems
// of the time to use.
//----------------------------------------

void holdclick_action(struct Menu *menu, int item_num)
{
  Serial.printf(F("holdclick_action: entered\n"));

  int holdtime = ReHoldClickTime;
  
// the step time for hold click
#define HOLD_STEP   100

  // get rid of any stray events to this point
  event_flush();

  // draw row 0 of menu, get heading from MenuItem
  lcd.clear();
  lcd.print(menu->items[item_num]->title);
  
  // show row slot info on row 1
  draw_row1_time(holdtime);

  // handle events in our own little event loop
  while (true)
  {
    // handle any pending event
    if (event_pending() > 0)
    {
      byte event = event_pop(); // get next event and handle it

      switch (event)
      {
        case vfo_RLeft:
          holdtime -= HOLD_STEP;
          if (holdtime < MinHoldClickTime)
            holdtime = MinHoldClickTime;
          Serial.printf(F("holdclick_action: vfo_RLeft, after holdtime=%d\n"), holdtime);
          break;
        case vfo_RRight:
          holdtime += HOLD_STEP;
          if (holdtime > MaxHoldClickTime)
            holdtime = MaxHoldClickTime;
          Serial.printf(F("holdclick_action: vfo_RRight, after holdtime=%d\n"), holdtime);
          break;
        case vfo_Click:
          ReHoldClickTime = holdtime;
          save_to_eeprom();
          menu_flash();
          break;
        case vfo_HoldClick:
          event_flush();
          return;
        default:
          // ignored events we don't handle
          break;
      }

      // show hold time value in row 1
      draw_row1_time(holdtime);
    }
  }
}


//----------------------------------------
// Settings menu
//----------------------------------------

struct MenuItem mi_brightness = {"Brightness", NULL, &brightness_action};
struct MenuItem mi_contrast = {"Contrast", NULL, &contrast_action};
struct MenuItem mi_holdclick = {"Hold click", NULL, &holdclick_action};
struct MenuItem *mia_settings[] = {&mi_brightness, &mi_contrast, &mi_holdclick};
struct Menu settings_menu = {"Settings", ARRAY_LEN(mia_settings), mia_settings};

//----------------------------------------
// main menu
//----------------------------------------

struct MenuItem mi_save = {"Save slot", NULL, &saveslot_action};
struct MenuItem mi_restore = {"Restore slot", NULL, &restoreslot_action};
struct MenuItem mi_del = {"Delete slot", NULL, &deleteslot_action};
struct MenuItem mi_settings = {"Settings", &settings_menu, NULL};
struct MenuItem *mia_main[] = {&mi_save, &mi_restore, &mi_del, &mi_settings};
struct Menu menu_main = {"Menu", ARRAY_LEN(mia_main), mia_main};


//----------------------------------------
// Standard Arduino loop() function.
//----------------------------------------

void loop(void)
{
  // remember old values, update screen if changed
  Frequency old_freq = VfoFrequency;
  int old_position = VfoSelectDigit;

  // handle all events in the queue
  while (event_pending() > 0)
  {
    // get next event and handle it
    VFOEvent event = event_pop();

    switch (event)
    {
      case vfo_RLeft:
        Serial.printf(F("loop: vfo_RLeft\n"));
        VfoFrequency -= offset2bump[VfoSelectDigit];
        if (VfoFrequency < MIN_FREQ)
          VfoFrequency = MIN_FREQ;
        if (VfoFrequency > MAX_FREQ)
          VfoFrequency = MAX_FREQ;
       break;
      case vfo_RRight:
        Serial.printf(F("loop: vfo_RRight\n"));
        VfoFrequency += offset2bump[VfoSelectDigit];
        if (VfoFrequency < MIN_FREQ)
          VfoFrequency = MIN_FREQ;
        if (VfoFrequency > MAX_FREQ)
          VfoFrequency = MAX_FREQ;
        break;
      case vfo_DnRLeft:
        Serial.printf(F("loop: vfo_DnRLeft\n"));
        VfoSelectDigit += 1;        
        if (VfoSelectDigit >= MAX_FREQ_CHARS)
          VfoSelectDigit = MAX_FREQ_CHARS - 1;
        break;
      case vfo_DnRRight:
        Serial.printf(F("loop: vfo_DnRRight\n"));
        VfoSelectDigit -= 1;
        if (VfoSelectDigit < 0)
          VfoSelectDigit = 0;
        break;
      case vfo_Click:
        Serial.printf(F("loop: vfo_Click event ignored\n"));
        break;
      case vfo_HoldClick:
        Serial.printf(F("loop: Got vfo_HoldClick: calling menu_show()\n"));
        menu_show(&menu_main, 0);    // redisplay the original menu
        show_main_screen();
        save_to_eeprom();   // save any changes made
        break;
      default:
        Serial.printf(F("loop: Unrecognized event: %d\n"), event);
        break;
    }

    // display frequency if changed, update DDS-60
    if (old_freq != VfoFrequency || old_position != VfoSelectDigit)
    {
      display_sel_value(VfoFrequency, VfoSelectDigit, MAX_FREQ_CHARS, NUM_COLS - MAX_FREQ_CHARS - 2, 0);
//      print_freq(VfoFrequency, VfoSelectDigit, 0);
      old_freq = VfoFrequency;
      old_position = VfoSelectDigit;

      save_to_eeprom();

      dds_update(VfoFrequency);
    }
  }
}
