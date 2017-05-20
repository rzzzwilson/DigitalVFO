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


// Digital VFO program name & version
const char *ProgramName = "DigitalVFO";
const char *Version = "1.0";
const char *Callsign = "vk4fawr";

// display constants - below is for ubiquitous small HD44780 16x2 display
#define NUM_ROWS        2
#define NUM_COLS        16

// macro to get number of elements in an array
#define ALEN(a)    (sizeof(a)/sizeof(a[0]))

// define one row of blanks
// FIXME: should be dynamic
char *BlankRow = NULL;

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

// define pin controlling brightness and contrast
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

// address in display CGRAM for definable and other characters
#define SELECT_CHAR     0     // shows 'underlined' decimal digits (dynamic, 0 to 9)
#define SPACE_CHAR      1     // shows an 'underlined' space character
#define ALLSET_CHAR     0xff  // the 'all bits set' char in display RAM, used for 'bar' display

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
#define vfo_DClick    7

// the "in use" display character, "→"
#define IN_USE_CHAR   0x7e

// default LCD contrast & brightness
const unsigned int DefaultLcdContrast = 70;
const unsigned int DefaultLcdBrightness = 150;

// VFO modes - online or standby
#define vfo_Standby   0
#define vfo_Online    1

// stuff for the calibrate action
const int MinClockOffset = -32000;
const int MaxClockOffset = +32000;
const int MaxOffsetDigits = 5;


//##############################################################################
// The VFO state variables and typedefs
//##############################################################################

typedef unsigned int Mode;
Mode VfoMode;               // VFO mode

typedef unsigned long Frequency;
Frequency VfoFrequency;     // VFO frequency (Hz)

typedef int SelOffset;
SelOffset VfoSelectDigit;   // selected column index, zero at the right

typedef byte VFOEvent;

int LcdContrast = DefaultLcdContrast;
int LcdBrightness = DefaultLcdBrightness;

// adjustment value for DDS-60 (set in 'Calibrate' menu)
int VfoClockOffset = 0;


//##############################################################################
// Utility routines
//##############################################################################

//----------------------------------------
// Abort the program.
// Tries to tell the world what went wrong, then just loops.
//     msg  address of error string
// Only first NUM_ROWS*NUM_COLS chars of message is displayed on LCD.
//----------------------------------------

void abort(const char *msg)
{
  char buf[NUM_COLS*NUM_ROWS+1];
  char *ptr = buf;
  
  // print error on console (maybe)
  Serial.printf(F("message=%s\nTeensy is paused!\n"), msg);

  // truncate/pad message to NUM_ROWS * NUM_COLS chars
  for (int i = 0; i < NUM_COLS*NUM_ROWS; ++i)
    *ptr++ = ' ';
  *ptr = '\0';
  
  strncpy(buf, msg, NUM_COLS*NUM_ROWS);
  if (strlen(msg) < NUM_COLS*NUM_ROWS)
    strncpy(buf + strlen(msg), "                                ",
            NUM_COLS*NUM_ROWS - strlen(msg));

  // show what we can on the display, forever
  while (1)
  {
    lcd.clear();
    for (int i = 0; i < NUM_ROWS; ++i)
    {
      lcd.setCursor(0, i);
      lcd.print(buf + i*NUM_COLS);
    }
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
// Convert an event number to a display string.   Used only for debug.
//----------------------------------------

const char *event2display(VFOEvent event)
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
    case vfo_DClick:    return "vfo_DClick";
  }
  
  return "UNKNOWN!";
}

//----------------------------------------
// show the credits
//----------------------------------------

void show_credits(void)
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(ProgramName);
  lcd.print(" ");
  lcd.print(Version);
  lcd.setCursor(NUM_COLS-strlen(Callsign), 1);
  lcd.print(Callsign);
}

//----------------------------------------
// display a simple banner on the LCD
//----------------------------------------

void banner(void)
{
  Serial.printf(F("%s %s (%s)\n"), ProgramName, Version, Callsign);

  show_credits();
  delay(900);    // wait a bit

  // do a fade out, clear screen then normal brightness
  for (int i = LcdBrightness; i; --i)
  {
    analogWrite(mc_Brightness, i);
    delay(15);
  }
  lcd.clear();
  delay(400);
  analogWrite(mc_Brightness, LcdBrightness);
}

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
  char *ptr = buf + bufsize - 1;    // rightmost char in 'buf'

  for (int i = 0; i < bufsize; ++i)
  {
    int rem = value % 10;

    value = value / 10;
    *ptr-- = char(rem);
  }
}

//----------------------------------------
// Convert a numeric mode to a display string.
//----------------------------------------

const char *mode2display(Mode mode)
{
  switch (mode)
  {
    case vfo_Standby: return "standby";
    case vfo_Online:  return "ONLINE";
  }

  return "UNKNOWN MODE";
}


//##############################################################################
// Code to handle the DigitalVFO menus.
//##############################################################################

// handler for selection of an item (vfo_Click event)
typedef void (*ItemAction)(struct Menu *, int);

// handler for custom item (vfo_RRight, vfo_RLeft events)
typedef int (*ItemIncDec)(int item_num, int delta);

// structure defining a menu item
struct MenuItem
{
  const char *title;          // menu item display text
  struct Menu *menu;          // if not NULL, submenu to pass to show_menu()
  ItemAction action;          // if not NULL, address of action function
};

// A structure defining a menu
struct Menu
{
  const char *title;          // title displayed on menu page
  int num_items;              // number of items in the array below
  struct MenuItem **items;    // array of pointers to MenuItem data
};

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

//----------------------------------------
// Draw the menu on the screen.
//     menu  pointer to a Menu structure
// Only draws the top row.
//----------------------------------------

void menu_draw(struct Menu *menu)
{
  // clear screen and write menu title on upper row
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(menu->title);
}

//----------------------------------------
// Draw a standard menuitem on the screen.
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
// Handle a menu.
//     menu      pointer to a defining Menu structure
//     unused    unused item number (used by action routines)
//               (used to be index to initial item to draw)
// Handle events in the loop here.
// This code doesn't see events handled in any *_action() routine.
//----------------------------------------

void menu_show(struct Menu *menu, int unused)
{
  int item_num = 0;     // index of the menuitem to show

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
          menu_draw(menu);    // redraw the menu header, item redrawn below
          Serial.printf(F("menu_show: end of vfo_Click handling\n"));
          break;
        case vfo_HoldClick:
          Serial.printf(F("menu_show: vfo_HoldClick, exit menu\n"));
          event_flush();
          return;             // back to the parent menu or main screen
        default:
          Serial.printf(F("menu_show: unrecognized event %d\n"), event);
          break;
      }

      // update the item display
      menuitem_draw(menu, item_num);
    }
  }
}

//----------------------------------------
// Show that *something* happened.
// Flash the screen in a possibly eye-catching way.
//----------------------------------------

void display_flash(void)
{
  lcd.noDisplay();
  delay(100);
  lcd.display();
  delay(100);
  lcd.noDisplay();
  delay(100);
  lcd.display();
}


//##############################################################################
// The system event queue.
// Implemented as a circular buffer.
//##############################################################################

#define QueueLength 10

VFOEvent event_queue[QueueLength];
int queue_fore = 0;   // fore pointer into circular buffer
int queue_aft = 0;    // aft pointer into circular buffer

void event_push(VFOEvent event)
{
  // Must protect from RE code fiddling with queue
  noInterrupts();

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

  interrupts();
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
  // Must protect from RE code fiddling with queue
  noInterrupts();

  queue_fore = 0;
  queue_aft = 0;

  interrupts();
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


//##############################################################################
// Utility routines for the display.
//##############################################################################

//----------------------------------------
// Display an unsigned long on the display with selected column underlined.
//     value       the number to display
//     sel_col     the selection offset of digit to underline
//                 (0 is rightmost digit, increasing to the left)
//     num_digits  the number of digits to show on the display
//     col, row    position to display left-most digit at
// If the value is too long it will be truncated at the left.
//----------------------------------------

void display_sel_value(unsigned long value, int sel_col, int num_digits, int col, int row)
{
  char buf [num_digits];
  int index = num_digits - sel_col - 1;
  bool lead_zero = true;

  // convert value to a buffer of decimal values, [0-9]
  ulong2buff(buf, num_digits, value);

  // create special underlined selection character including selected digit
  lcd.createChar(SELECT_CHAR, sel_digits[int(buf[index])]);

  // we set cursor here because we lose cursor position after lcd.createChar()!?
  // would prefer to set cursor position outside this function, but ไม่เป็นไร.
  lcd.setCursor(col, row);

  // write each byte of buffer to display, handling leading spaces and selection
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
      }
      else
      {
        lcd.write(" ");
      }
    }
    else
    {
      if (index == i)
      {
        lcd.write(byte(SELECT_CHAR));
      }
      else
      {
        lcd.write(char_val + '0');
      }
    }
  }
}

//----------------------------------------
// Display an integer with sign on the display with selected column underlined.
//     value       the number to display
//     sel_col     the selection offset of digit to underline
//                 (0 is rightmost digit, increasing to the left)
//     num_digits  the number of digits to show on the display
//                 (this does NOT include the +/- initial sign)
//     col, row    position to display left-most digit at
// If the value is too long it will be truncated at the left.
//----------------------------------------

void display_sel_offset(int value, int sel_col, int num_digits, int col, int row)
{
  char buf [num_digits];
  int index = num_digits - sel_col - 1;
  bool lead_zero = true;
  char prefix = '+';

  Serial.printf(F("display_sel_offset: value=%d, sel_col=%d, num_digits=%d, col=%d, row=%d\n"),
                value, sel_col, num_digits, col, row);

  // worry about a negative value
  if (value < 0)
    prefix = '-';
  value = abs(value);

  // convert value to a buffer of decimal values, [0-9]
  ulong2buff(buf, num_digits, value);

  // create special underlined selection character including selected digit
  lcd.createChar(SELECT_CHAR, sel_digits[int(buf[index])]);

  // we set cursor here because we lose cursor position after lcd.createChar()!?
  // would prefer to set cursor position outside this function, but ไม่เป็นไร.
  lcd.setCursor(col, row);

  // write the leading sign character
  lcd.write(prefix);
    
  // write each byte of buffer to display, handling leading spaces and selection
  for (int i = 0; i < num_digits; ++i)
  {
    int char_val = buf[i];

    if (char_val != 0 || i >= num_digits - 1)
        lead_zero = false;

    if (lead_zero)
    {
      if (index == i)
      {
        lcd.write(byte(SPACE_CHAR));
      }
      else
      {
        lcd.write(" ");
      }
    }
    else
    {
      if (index == i)
      {
        lcd.write(byte(SELECT_CHAR));
      }
      else
      {
        lcd.write(char_val + '0');
      }
    }
  }
}


//##############################################################################
// Interrupt driven rotary encoder interface.
// from code by Simon Merrett, based on insight from Oleg Mazurov, Nick Gammon, rt, Steve Spence
//##############################################################################

// time when click becomes a "hold click" (milliseconds)
// the delay is configurable in the UI
#define MinHoldClickTime        100
#define MaxHoldClickTime        1000
#define DefaultHoldClickTime    500

// time when click becomes a "double click" (milliseconds)
// the delay is configurable in the UI
#define MinDClickTime           100
#define MaxDClickTime           1000
#define DefaultDClickTime       300

unsigned int ReHoldClickTime = DefaultHoldClickTime;
unsigned int ReDClickTime = DefaultDClickTime;

// internal variables
bool re_rotation = false;       // true if rotation occurred while knob down
bool re_down = false;           // true while knob is down
unsigned long re_down_time = 0; // milliseconds when knob is pressed down
unsigned long last_click = 0;   // time when last single click was found

// expecting rising edge on pinA - at detent
volatile byte aFlag = 0;

// expecting rising edge on pinA - at detent
volatile byte bFlag = 0;

//----------------------------------------
// Initialize the rotary encoder stuff.
// Return 'true' if button was pressed down during setup.
//----------------------------------------

bool re_setup(void)
{
  // set RE data pins as inputs
  pinMode(re_pinA, INPUT);
  pinMode(re_pinB, INPUT);
  pinMode(re_pinPush, INPUT);

  // attach pins to IST on rising edge only
  attachInterrupt(digitalPinToInterrupt(re_pinA), pinA_isr, RISING);
  attachInterrupt(digitalPinToInterrupt(re_pinB), pinB_isr, RISING);
  attachInterrupt(digitalPinToInterrupt(re_pinPush), pinPush_isr, CHANGE);

  // look at RE button, if DOWN this function returns 'true'
  return ! (PIND & 0x10);
}

//----------------------------------------
// Handler for pusbutton interrupts (UP or DOWN).
//----------------------------------------

void pinPush_isr(void)
{
  cli();

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
      unsigned long last_up_time = millis();
      unsigned int push_time = last_up_time - re_down_time;

      if (push_time < ReHoldClickTime)
      {
        // check to see if we have a single click very recently
        if (last_click != 0)
        {   // did have single click before this
          unsigned long dclick_delta = last_up_time - last_click;

          // if short time since last click, issue double-click event
          if (dclick_delta <= ReDClickTime)
          {
            event_push(vfo_DClick);
            last_click = 0;
          }
          else
          {
            event_push(vfo_Click);
            last_click = last_up_time;    // single-click, prepare for possible double
          }
        }
        else
        {
          event_push(vfo_Click);
          last_click = last_up_time;    // single-click, prepare for possible double
        }
      }
      else
      {
        event_push(vfo_HoldClick);
      }
    }
  }

  sei();
}

//----------------------------------------
// Handler for pinA interrupts.
//----------------------------------------

void pinA_isr(void)
{
  byte reading;

  cli();

  reading = PIND & 0xC;

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

  sei();
}

//----------------------------------------
// Handler for pinB interrupts.
//----------------------------------------

void pinB_isr(void)
{
  byte reading;

  cli();

  reading = PIND & 0xC;

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

  sei();
}


//##############################################################################
// Code to save/restore in EEPROM.
//##############################################################################

// start storing at address 0
#define NEXT_FREE   (0)

// address for Frequency 'frequency'
const int AddressFreq = NEXT_FREE;
#define NEXT_FREE   (AddressFreq + sizeof(Frequency))

// address for int 'selected digit'
const int AddressSelDigit = NEXT_FREE;
#define NEXT_FREE   (AddressSelDigit + sizeof(SelOffset))

// address for 'VfoClockOffset' calibration
const int AddressVfoClockOffset = NEXT_FREE;
#define NEXT_FREE   (AddressVfoClockOffset + sizeof(VfoClockOffset))

// address for byte 'contrast'
const int AddressContrast = NEXT_FREE;
#define NEXT_FREE   (AddressContrast + sizeof(LcdContrast))

// address for byte 'brightness'
const int AddressBrightness = NEXT_FREE;
#define NEXT_FREE   (AddressBrightness + sizeof(LcdBrightness))

// address for int 'hold click time'
const int AddressHoldClickTime = NEXT_FREE;
#define NEXT_FREE   (AddressHoldClickTime + sizeof(ReHoldClickTime))

// address for int 'double click time'
const int AddressDClickTime = NEXT_FREE;
#define NEXT_FREE   (AddressDClickTime + sizeof(ReDClickTime))

// number of frequency save slots in EEPROM
const int NumSaveSlots = 10;

const int SaveFreqBase = NEXT_FREE;
#define NEXT_FREE   (SaveFreqBase + NumSaveSlots * sizeof(Frequency))

//also save the offset for each frequency
const int SaveOffsetBase = NEXT_FREE;
#define NEXT_FREE   (SaveOffsetBase + NumSaveSlots * sizeof(SelOffset);

// additional EEPROM saved items go here

//----------------------------------------
// Save VFO state to EEPROM.
// Everything except slot data.
//----------------------------------------

void save_to_eeprom(void)
{
  EEPROM.put(AddressFreq, VfoFrequency);
  EEPROM.put(AddressSelDigit, VfoSelectDigit);
  EEPROM.put(AddressVfoClockOffset, VfoClockOffset);
  EEPROM.put(AddressBrightness, LcdBrightness);
  EEPROM.put(AddressContrast, LcdContrast);
  EEPROM.put(AddressHoldClickTime, ReHoldClickTime);
  EEPROM.put(AddressDClickTime, ReDClickTime);
}

//----------------------------------------
// Restore VFO state from EEPROM.
// Everything except slot data.
//----------------------------------------

void restore_from_eeprom(void)
{
  EEPROM.get(AddressFreq, VfoFrequency);
  EEPROM.get(AddressSelDigit, VfoSelectDigit);
  EEPROM.get(AddressVfoClockOffset, VfoClockOffset);
  EEPROM.get(AddressBrightness, LcdBrightness);
  EEPROM.get(AddressContrast, LcdContrast);
  EEPROM.get(AddressHoldClickTime, ReHoldClickTime);
  EEPROM.get(AddressDClickTime, ReDClickTime);
}

//----------------------------------------
// Given slot number, return freq/offset.
//     freq    pointer to Frequency item to be updated
//     offset  pointer to selection offset item to be updated
//----------------------------------------

void get_slot(int slot_num, Frequency &freq, SelOffset &offset)
{
  int freq_address = SaveFreqBase + slot_num * sizeof(Frequency);
  int offset_address = SaveOffsetBase + slot_num * sizeof(SelOffset);

  EEPROM.get(freq_address, freq);
  EEPROM.get(offset_address, offset);
}

//----------------------------------------
// Put frequency/offset into given slot number.
//     freq    pointer to Frequency item to be saved in slot
//     offset  pointer to selection offset item to be saved in slot
//----------------------------------------

void put_slot(int slot_num, Frequency freq, SelOffset offset)
{
  int freq_address = SaveFreqBase + slot_num * sizeof(Frequency);
  int offset_address = SaveOffsetBase + slot_num * sizeof(SelOffset);

  EEPROM.put(freq_address, freq);
  EEPROM.put(offset_address, offset);
}

//----------------------------------------
// Print all EEPROM saved data to console.
//----------------------------------------

void dump_eeprom(void)
{
  Frequency freq;
  SelOffset offset;
  int clkoffset;
  int brightness;
  int contrast;
  unsigned int hold;
  unsigned int dclick;

  EEPROM.get(AddressFreq, freq);
  EEPROM.get(AddressSelDigit, offset);
  EEPROM.get(AddressVfoClockOffset, clkoffset);
  EEPROM.get(AddressBrightness, brightness);
  EEPROM.get(AddressContrast, contrast);
  EEPROM.get(AddressHoldClickTime, hold);
  EEPROM.get(AddressDClickTime, dclick);
  
  Serial.printf(F("=================================================\n"));
  Serial.printf(F("dump_eeprom: VfoFrequency=%ld\n"), freq);
  Serial.printf(F("             AddressSelDigit=%d\n"), offset);
  Serial.printf(F("             VfoClockOffset=%d\n"), clkoffset);
  Serial.printf(F("             LcdBrightness=%d\n"), brightness);
  Serial.printf(F("             LcdContrast=%d\n"), contrast);
  Serial.printf(F("             ReHoldClickTime=%dmsec\n"), hold);
  Serial.printf(F("             ReDClickTime=%dmsec\n"), dclick);

  for (int i = 0; i < NumSaveSlots; ++i)
  {
    get_slot(i, freq, offset);
    Serial.printf(F("Slot %d: freq=%ld, seldig=%d\n"), i, freq, offset);
  }

  Serial.printf(F("=================================================\n"));
}

//##############################################################################
// Code to handle the DDS-60
//
// From: http://www.rocketnumbernine.com/2011/10/25/programming-the-ad9851-dds-synthesizer
// Andrew Smallbone <andrew@rocketnumbernine.com>
//
// I've touched this a little bit, so any errors are mine!
//##############################################################################

//----------------------------------------
// Pulse 'pin' high and then low.
//----------------------------------------

void dds_pulse_high(byte pin)
{
  digitalWrite(pin, HIGH);
  digitalWrite(pin, LOW);
}

//----------------------------------------
// Transfer a 'data' byte a bit at a time, LSB first, to DDS_DATA pin.
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
// frequency of sinewave (datasheet page 12) will be <sys clock> * <frequency tuning word> / 2^32
//
// 'VfoClockOffset' is the value the 'Calibrate' menu tweaks, initially 0.
//----------------------------------------

void dds_update(Frequency frequency)
{
//  int32_t data = frequency * 4294967296.0 / 180.0e6;
  unsigned long data = frequency * 4294967296 / (6 * (30000000 - VfoClockOffset));

  Serial.printf(F("dds_update: frequency=%ld, VfoClockOffset=%d, data=%ld\n"),
                frequency, VfoClockOffset, data);
  
  for (int b = 0; b < 4; ++b, data >>= 8)
  {
    dds_tfr_byte(data & 0xFF);
  }
  
  dds_tfr_byte(0x001);
  dds_pulse_high(DDS_FQ_UD);
}

//----------------------------------------
// Force the DDS into standby mode.
// There is a standby bit in the control word, but setting frequency to zero works.
//----------------------------------------

void dds_standby(void)
{
  Serial.printf(F("DDS into standby mode.\n"));
  dds_update(0L);
}

//----------------------------------------
// Force the DDS into online mode.
//----------------------------------------

void dds_online(void)
{
  Serial.printf(F("DDS into online mode.\n"));
  dds_update(VfoFrequency);
}

//----------------------------------------
// Initialize the DDS hardware.
// The VFO 'wakes up' in standby mode.
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

  // wait a bit, then go into standby
  delay(100);
  dds_standby();
}


//##############################################################################
// Main VFO code
//##############################################################################

//----------------------------------------
// The standard Arduino setup() function.
// Called once on powerup.
//----------------------------------------

void setup(void)
{
  // initialize the BlankRow global to a blank string length of a display row
  BlankRow = (char *) malloc(NUM_COLS + 1);     // size of one display row
  for (int i = 0; i < NUM_COLS; ++i)
    BlankRow[i] = ' ';
  BlankRow[NUM_COLS] = '\0';

  // initialize the serial console
  Serial.begin(115200);

  // VFO wakes up in standby mode
  VfoMode = vfo_Standby;

  // set brightness/contrast pin state
  pinMode(mc_Brightness, OUTPUT);
  pinMode(mc_Contrast, OUTPUT);

  // get state back from EEPROM, set display brightness/contrast
  restore_from_eeprom();
  analogWrite(mc_Brightness, LcdBrightness);
  analogWrite(mc_Contrast, LcdContrast);

  // initialize the display
  lcd.begin(NUM_COLS, NUM_ROWS);      // define display size
  lcd.clear();
  lcd.noCursor();

  // create underlined space for frequency display
  lcd.createChar(SPACE_CHAR, sel_digits[SPACE_INDEX]);

  // set up the DDS device
  dds_setup();

  // set up the rotary encoder
  // reset a few things if RE button down during powerup
  // the aim is to get the user out of an "unusable" VFO state
  if (re_setup())
  {
    LcdBrightness = DefaultLcdBrightness;
    analogWrite(mc_Brightness, LcdBrightness);
    LcdContrast = DefaultLcdContrast;
    analogWrite(mc_Contrast, LcdContrast);
    ReHoldClickTime = DefaultHoldClickTime;    

    Serial.printf(F("Resetting brightness to %d, contrast to %d and hold time to %d\n"),
                  LcdBrightness, LcdContrast, ReHoldClickTime);

    // show user we were reset
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("DigitalVFO reset");
    lcd.setCursor(0, 1);
    lcd.print(" nothing saved");
    delay(2000);
    lcd.clear();
    delay(1000);
  }
  
  // show program name and version number
  banner();

  // dump EEPROM values
  dump_eeprom();

  // we sometimes see random events on powerup, flush them here
//  event_flush();

  // get going
  show_main_screen();
}

//----------------------------------------
// Show the main screen.
// Room for 'mode', etc, display on second row
//----------------------------------------

void show_main_screen(void)
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Vfo:");
  display_sel_value(VfoFrequency, VfoSelectDigit, MAX_FREQ_CHARS, NUM_COLS - MAX_FREQ_CHARS - 2, 0);
  lcd.print("Hz");

  lcd.setCursor(0, 1);
  lcd.write(BlankRow);
  lcd.setCursor(0, 1);
  lcd.write(mode2display(VfoMode));
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
  display_sel_value(freq, -1, MAX_FREQ_CHARS, NUM_COLS - MAX_FREQ_CHARS - 2, 1);
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
          display_flash();
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
          display_flash();
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
          display_flash();
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
// Reset everything in the VFO - Dangerous!
//   menu      address of 'calling' menu
//   item_num  index of MenuItem we were actioned from
//----------------------------------------

void reset_action(struct Menu *menu, int item_num)
{
  Frequency zero_freq = 0L;
  SelOffset zero_offset = 0;
  
  // zero the frequency+selected values
  VfoFrequency = MIN_FREQ;
  VfoSelectDigit = 0;
  VfoClockOffset = 0;
  LcdBrightness = DefaultLcdBrightness;
  LcdContrast = DefaultLcdContrast;
  ReHoldClickTime = DefaultHoldClickTime;
  ReDClickTime = DefaultDClickTime;
  
  save_to_eeprom();

  // zero the save slots
  for (int i = 0; i < NumSaveSlots; ++i)
  {
    put_slot(i, zero_freq, zero_offset);
  }

  display_flash();
}

void reset_no_action(struct Menu *menu, int item_num)
{
}

//----------------------------------------
// Show the credits.
//   menu      address of 'calling' menu
//   item_num  index of MenuItem we were actioned from
// Wait here until any type of RE click.
//----------------------------------------

void credits_action(struct Menu *menu, int item_num)
{
  // get rid of any stray events to this point
  event_flush();

  // show the credits
  show_credits();
  
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
          break;
        case vfo_RRight:
          break;
        case vfo_Click:
          return;
        case vfo_HoldClick:
          return;
        default:
          // ignored events we don't handle
          break;
      }
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
          display_flash();
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
          display_flash();
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
//     msec      the time to show
//     def_time  the current system time
//----------------------------------------

void draw_row1_time(unsigned int msec, unsigned int def_time)
{
  lcd.setCursor(0, 1);
  lcd.print(BlankRow);
  
  if (msec == def_time)
  {
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

  unsigned int holdtime = ReHoldClickTime;      // the value we change
  const unsigned int hold_step = 100;           // step adjustment +/-
  
  // get rid of any stray events to this point
  event_flush();

  // draw row 0 of menu, get heading from MenuItem
  lcd.clear();
  lcd.print(menu->items[item_num]->title);
  
  // show row slot info on row 1
  draw_row1_time(holdtime, ReHoldClickTime);

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
          holdtime -= hold_step;
          if (holdtime < MinHoldClickTime)
            holdtime = MinHoldClickTime;
          Serial.printf(F("holdclick_action: vfo_RLeft, after holdtime=%d\n"), holdtime);
          break;
        case vfo_RRight:
          holdtime += hold_step;
          if (holdtime > MaxHoldClickTime)
            holdtime = MaxHoldClickTime;
          Serial.printf(F("holdclick_action: vfo_RRight, after holdtime=%d\n"), holdtime);
          break;
        case vfo_Click:
          ReHoldClickTime = holdtime;
          save_to_eeprom();         // save change to EEPROM
          display_flash();
          break;
        case vfo_HoldClick:
          event_flush();
          return;
        default:
          // ignored events we don't handle
          break;
      }

      // show hold time value in row 1
      draw_row1_time(holdtime, ReHoldClickTime);
    }
  }
}

//----------------------------------------
// Set the current 'doubleclick' time and save to EEPROM if 'actioned'.
//   menu      address of 'calling' menu
//   item_num  index of MenuItem we were actioned from
// This works differently from brightness/contrast.  We show menuitems
// of the time to use.
//----------------------------------------

void doubleclick_action(struct Menu *menu, int item_num)
{
  Serial.printf(F("doubleclick_action: entered, ReDClickTime=%dmsec\n"), ReDClickTime);

  int dctime = ReDClickTime;        // the value we adjust
  unsigned int dclick_step = 100;   // step adjustment +/-
  
  // get rid of any stray events to this point
  event_flush();

  // draw row 0 of menu, get heading from MenuItem
  lcd.clear();
  lcd.print(menu->items[item_num]->title);
  
  // show row slot info on row 1
  draw_row1_time(dctime, ReDClickTime);

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
          dctime -= dclick_step;
          if (dctime < MinDClickTime)
            dctime = MinDClickTime;
          Serial.printf(F("doubleclick_action: vfo_RLeft, after dctime=%d\n"), dctime);
          break;
        case vfo_RRight:
          dctime += dclick_step;
          if (dctime > MaxDClickTime)
            dctime = MaxDClickTime;
          Serial.printf(F("doubleclick_action: vfo_RRight, after dctime=%d\n"), dctime);
          break;
        case vfo_Click:
          ReDClickTime = dctime;
          save_to_eeprom();         // save changes to EEPROM
          display_flash();
          break;
        case vfo_HoldClick:
          event_flush();
          return;
        default:
          // ignored events we don't handle
          break;
      }

      // show hold time value in row 1
      draw_row1_time(dctime, ReDClickTime);
    }
  }
}

//----------------------------------------
// Adjust the DDS-60 clock tweak to allow frequency calibration.
//   menu      address of 'calling' menu
//   item_num  index of MenuItem we were actioned from
// Only works if the VFO is online.
//
// In line with all other action menuitems we want to observer the effects
// of changing the value, but also only want to make a permanent change on
// a 'click' action.
//----------------------------------------

void calibrate_action(struct Menu *menu, int item_num)
{
  Serial.printf(F("calibrate_action: entered, VfoClockOffset=%dmsec\n"), VfoClockOffset);

  int save_offset = VfoClockOffset; // save the existing offset
  int seldig = 0;                   // the selected digit in the display
  bool was_standby = false;         // true if we have to set standby when finished

  // if VFO in standby mode, remember that and put into online mode
  if (VfoMode == vfo_Standby)
  {
    was_standby = true;
    vfo_toggle_mode();
  }
  
  // get rid of any stray events to this point
  event_flush();

  // draw row 0 of menu, get heading from MenuItem
  lcd.clear();
  lcd.print(menu->items[item_num]->title);

  // show offset info on row 1
  display_sel_offset(VfoClockOffset, seldig, 5, 10, 1);

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
          VfoClockOffset -= offset2bump[seldig];
          if (VfoClockOffset < MinClockOffset)
            VfoClockOffset = MinClockOffset;
          Serial.printf(F("calibrate_action: vfo_RLeft, after VfoClockOffset=%d\n"),
                        VfoClockOffset);
          break;
        case vfo_RRight:
          VfoClockOffset += offset2bump[seldig];
          if (VfoClockOffset > MaxClockOffset)
            VfoClockOffset = MaxClockOffset;
          Serial.printf(F("calibrate_action: vfo_RRight, after VfoClockOffset=%d\n"),
                        VfoClockOffset);
          break;
        case vfo_DnRLeft:
          Serial.printf(F("calibrate_action: vfo_DnRLeft\n"));
          ++seldig;
          if (seldig >= MaxOffsetDigits)
            seldig = MaxOffsetDigits - 1;
          Serial.printf(F("calibrate_action: vfo_DnRLeft, after seldig=%d\n"),
                        seldig);
          break;
        case vfo_DnRRight:
          Serial.printf(F("calibrate_action: vfo_DnRLeft\n"));
          --seldig;
          if (seldig < 0)
            seldig = 0;
          Serial.printf(F("calibrate_action: vfo_DnRRight, after seldig=%d\n"),
                        seldig);
          break;
        case vfo_Click:
          save_offset = VfoClockOffset; // for when we exit menuitem
          display_flash();
          break;
        case vfo_HoldClick:
          // put changed VfoClockOffset into EEPROM
          VfoClockOffset = save_offset;
          save_to_eeprom();             // save changes to EEPROM
          event_flush();
          if (was_standby)
            vfo_toggle_mode();
          return;
        default:
          // ignored events we don't handle
          break;
      }

      // show hold time value in row 1
      display_sel_offset(VfoClockOffset, seldig, 5, 10, 1);

      // update the VFO frequency
      dds_update(VfoFrequency);
    }
  }
}

//----------------------------------------
// Toggle the VFO mode: vfo_Online or vfo_Standby.
//----------------------------------------
void vfo_toggle_mode(void)
{
  if (VfoMode == vfo_Online)
  {
    // DDS goes into standby, change mode
    dds_standby();
    VfoMode = vfo_Standby;
  }
  else
  {
    // DDS goes online, change mode
    dds_online();
    VfoMode = vfo_Online;
  }

  // update display
  lcd.setCursor(0, 1);
  lcd.write(BlankRow);
  lcd.setCursor(0, 1);
  lcd.write(mode2display(VfoMode));
}

//----------------------------------------
// Reset menu
//----------------------------------------

struct MenuItem mi_reset_no = {"No", NULL, &reset_no_action};
struct MenuItem mi_reset_yes = {"Yes", NULL, &reset_action};
struct MenuItem *mia_reset[] = {&mi_reset_no, &mi_reset_yes};
struct Menu reset_menu = {"Reset all", ALEN(mia_reset), mia_reset};

//----------------------------------------
// Settings menu
//----------------------------------------

struct MenuItem mi_brightness = {"Brightness", NULL, &brightness_action};
struct MenuItem mi_contrast = {"Contrast", NULL, &contrast_action};
struct MenuItem mi_holdclick = {"Hold click", NULL, &holdclick_action};
struct MenuItem mi_doubleclick = {"Double click", NULL, &doubleclick_action};
struct MenuItem mi_calibrate = {"Calibrate", NULL, &calibrate_action};
struct MenuItem *mia_settings[] = {&mi_brightness, &mi_contrast, &mi_holdclick,
                                   &mi_doubleclick, &mi_calibrate};
struct Menu settings_menu = {"Settings", ALEN(mia_settings), mia_settings};

//----------------------------------------
// main menu
//----------------------------------------

struct MenuItem mi_save = {"Save slot", NULL, &saveslot_action};
struct MenuItem mi_restore = {"Restore slot", NULL, &restoreslot_action};
struct MenuItem mi_del = {"Delete slot", NULL, &deleteslot_action};
struct MenuItem mi_settings = {"Settings", &settings_menu, NULL};
struct MenuItem mi_reset = {"Reset all", &reset_menu, NULL};
struct MenuItem mi_credits = {"Credits", NULL, &credits_action};
struct MenuItem *mia_main[] = {&mi_save, &mi_restore, &mi_del, &mi_settings, &mi_reset, &mi_credits};
struct Menu menu_main = {"Menu", ALEN(mia_main), mia_main};


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
        save_to_eeprom();            // save any changes made in menu
        break;
      case vfo_DClick:
        Serial.printf(F("loop: Got vfo_DClick\n"));
        vfo_toggle_mode();
        break;
      default:
        Serial.printf(F("loop: Unrecognized event: %d\n"), event);
        break;
    }

    // display frequency if changed, update DDS-60 if online
    if (old_freq != VfoFrequency || old_position != VfoSelectDigit)
    {
      display_sel_value(VfoFrequency, VfoSelectDigit, MAX_FREQ_CHARS, NUM_COLS - MAX_FREQ_CHARS - 2, 0);
      old_freq = VfoFrequency;
      old_position = VfoSelectDigit;

      save_to_eeprom();         // worry about frequent writes?

      if (VfoMode == vfo_Online)
        dds_update(VfoFrequency);
    }
  }
}
