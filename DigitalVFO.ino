////////////////////////////////////////////////////////////////////////////////
// A digital VFO using the DDS-60 card.
//
// The VFO will generate signals in the range 1.000000MHz to 60.000000MHz
// with a step ranging down to 1Hz.
//
// The interface will be a single rotary encoder with a built-in pushbutton.
// The frequency display will have a 'selected' digit which can be moved left
// and right by pressing the encoder knob and twisting left or right.
// Turning the encoder knob will increment or decrement the selected digit by 1
// with overflow or underflow propagating to the left.
////////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>

// uncommenting this next line turns on some debug
//#define DEBUG   1

// Digital VFO program name & version
const char *ProgramName = "DigitalVFO";
const char *Version = "1.1";
const char *MinorVersion = ".2";
const char *Callsign = "vk4fawr";

// display constants - below is for ubiquitous small HD44780 16x2 display
const int NumRows = 2;
const int NumCols = 16;

//-----
// Some convenience typedefs
//-----

typedef unsigned long ULONG;
typedef unsigned int UINT;

//-----
// Pins used by microcontroller to control devices.
//-----

// define microcontroller data pins we connect to the LCD
const byte lcd_RS = 7;
const byte lcd_ENABLE = 8;
const byte lcd_D4 = 9;
const byte lcd_D5 = 10;
const byte lcd_D6 = 11;
const byte lcd_D7 = 12;

// define the display connections
LiquidCrystal lcd(lcd_RS, lcd_ENABLE, lcd_D4, lcd_D5, lcd_D6, lcd_D7);

// define microcontroller pins controlling brightness and contrast
const byte mc_Brightness = 5;
const byte mc_Contrast = 6;

// define microcontroller pins connected to the rotary encoder
const int re_pinPush = 3;  // encoder pushbutton pin
const int re_pinA = 2;     // encoder A pin
const int re_pinB = 4;     // encoder B pin

// define microcontroller pins that control the DDS-60
const byte DDS_FQ_UD = 14;    // connected to AD9851 device select pin
const byte DDS_W_CLK = 15;    // connected to AD9851 clock pin
const byte DDS_DATA = 16;     // connected to AD9851 D7 (serial data) pin 

//-----
// Data and values used by the LCD code.
//-----

// max and min frequency showable
const ULONG MaxFreq = 60000000L;
const ULONG MinFreq = 1000000L;

// number of digits in the frequency display
const int NumFreqChars = 8;

// address in display CGRAM for definable and other characters
const int SelectChar = 0;     // shows 'underlined' decimal digits (dynamic, 0 to 9)
const int SpaceChar = 1;      // shows an 'underlined' space character
const int InUseChar = 2;      // shosws a right-facing arrow

const int AllsetChar = 0xff;  // the 'all bits set' char in display RAM, used for 'bar' display

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
const int SpaceIndex = 10;

// define the "in use" character
byte in_use_char[8] = {0x10,0x18,0x1c,0x1e,0x1c,0x18,0x10,0x00};

// map select_offset to bump values
ULONG offset2bump[] = {1,           // offset = 0
                       10,          // 1
                       100,         // 2
                       1000,        // 3
                       10000,       // 4
                       100000,      // 5
                       1000000,     // 6
                       10000000,    // 7
                       100000000};  // 8

// string holding one entire blank row (allocated in setup())
char *BlankRow = NULL;

// default LCD contrast & brightness
const UINT DefaultLcdContrast = 0;
const UINT DefaultLcdBrightness = 150;

//-----
// Events and the event queue.
//-----

// define the VFOevents
enum Event
{
  vfo_None,
  vfo_RLeft,
  vfo_RRight,
  vfo_DnRLeft,
  vfo_DnRRight,
  vfo_Click,
  vfo_HoldClick,
  vfo_DClick,
};

// define the length of the event queue
const int EventQueueLength = 10;

//-----
// VFO modes.
//-----

// VFO modes - online or standby
enum Mode 
{
  vfo_Standby,
  vfo_Online
};

//-----
// Miscellaneous.
//-----

// stuff for the calibrate action
const int MinClockOffset = -32000;
const int MaxClockOffset = +32000;
const int MaxOffsetDigits = 5;

// macro to get number of elements in an array
#define ALEN(a)    (sizeof(a)/sizeof((a)[0]))

// buffer, etc, to gather external command strings
#define MAX_COMMAND_LEN   16
#define COMMAND_END_CHAR    ';'
char CommandBuffer[MAX_COMMAND_LEN+1];
int CommandIndex = 0;

//##############################################################################
// The VFO state variables and typedefs
//
// The data here will be saved in EEPROM at various times.
// Code for the device will contain internal variables that aren't saved.
//##############################################################################

enum Mode VfoMode;          // VFO mode

typedef ULONG Frequency;
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
// Only first NumRows*NumCols chars of message is displayed on LCD.
//----------------------------------------

void abort(const char *msg)
{
  char buf[NumCols*NumRows+1];
  char *ptr = buf;
  
  // print error on console (maybe)
  Serial.printf(F("message=%s\nTeensy is paused!\n"), msg);

  // truncate/pad message to NumRows * NumCols chars
  for (int i = 0; i < NumCols*NumRows; ++i)
    *ptr++ = ' ';
  *ptr = '\0';
  
  strncpy(buf, msg, NumCols*NumRows);
  if (strlen(msg) < NumCols*NumRows)
    strncpy(buf + strlen(msg), "                                ",
            NumCols*NumRows - strlen(msg));

  // show what we can on the display, forever
  while (1)
  {
    lcd.clear();
    for (int i = 0; i < NumRows; ++i)
    {
      lcd.setCursor(0, i);
      lcd.print(buf + i*NumCols);
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
// Convert an event number to a display string.
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
  
  return "UNKNOWN EVENT";
}

//----------------------------------------
// Show the credits on the LCD.
//----------------------------------------

void show_credits(void)
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(ProgramName);
  lcd.print(" ");
  lcd.print(Version);
  lcd.setCursor(NumCols-strlen(Callsign), 1);
  lcd.print(Callsign);
}

//----------------------------------------
// Display a simple banner on the LCD.
//----------------------------------------

void banner(void)
{
#if DEBUG
  Serial.printf(F("\n"));
  Serial.printf(F("*************************************************\n"));
  Serial.printf(F("*           %s %s%s (%s)          *\n"),
                ProgramName, Version, MinorVersion, Callsign);
  Serial.printf(F("*************************************************\n"));
  Serial.printf(F("\n"));
#else
  Serial.printf(F("%s %s%s (%s)\n"), ProgramName, Version, MinorVersion, Callsign);
#endif

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
// The function won't overflow the given buffer, it will truncate at the left.
// For example, given the value 1234 and a buffer of length 7, will fill the
// buffer with 0001234.  Given 123456789 it will fill with 3456789.
//
// Each byte in the buffer is a number in [0, 9], NOT ['0', '9'].
// The resultant buffer does NOT have a terminating '\0'!
//----------------------------------------

void ulong2buff(char *buf, int bufsize, ULONG value)
{
  char *ptr = buf + bufsize - 1;    // rightmost char in 'buf'

  for (int i = 0; i < bufsize; ++i)
  {
    int rem = value % 10;

    value = value / 10;
    *ptr-- = char(rem);     // FIXME: don't require char()?
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

//----------------------------------------
// Convert a string to uppercase in situ.
//----------------------------------------

void str2upper(char *str)
{
  while (*str)
  {
    *str = toupper(*str);
    ++str;
  }
}


//##############################################################################
// Code to handle the DigitalVFO menus.
//##############################################################################

// handler for selection of an item (vfo_Click event)
typedef void (*ItemAction)(struct Menu *, int);

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

#ifdef DEBUG
//----------------------------------------
// dump a MenuItem to the console
// only called from dump_menu()
//----------------------------------------

void dump_menuitem(struct MenuItem *menuitem)
{
  Serial.printf(F("  menuitem address=%08x\n"), menuitem);
  Serial.printf(F("  title=%s\n"), menuitem->title);
  Serial.printf(F("  menu=%08x\n"), menuitem->menu);
  Serial.printf(F("  action=%08x\n"), menuitem->action);
}

//----------------------------------------
// dump a Menu and contained MenuItems to the console
//----------------------------------------

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
// Draw a menu on the screen.
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
// Custome menuitems are drawn by their handler.
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
  lcd.setCursor(NumCols - max_len, 1);
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
#ifdef DEBUG
      Serial.printf(F("menu_show loop: event=%s\n"), event2display(event));
#endif

      switch (event)
      {
        case vfo_RLeft:
#ifdef DEBUG
          Serial.printf(F("menu_show: vfo_RLeft\n"));
#endif
          if (--item_num < 0)
            item_num = 0;
          break;
        case vfo_RRight:
#ifdef DEBUG
          Serial.printf(F("menu_show: vfo_RRight\n"));
#endif
          if (++item_num >= menu->num_items)
            item_num = menu->num_items - 1;
          break;
        case vfo_DnRLeft:
#ifdef DEBUG
          Serial.printf(F("menu_show: vfo_DnRLeft (ignored)\n"));
#endif
          break;
        case vfo_DnRRight:
#ifdef DEBUG
          Serial.printf(F("menu_show: vfo_DnRRight (ignored)\n"));
#endif
          break;
        case vfo_Click:
#ifdef DEBUG
          Serial.printf(F("menu_show: vfo_Click\n"));
#endif
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
#ifdef DEBUG
          Serial.printf(F("menu_show: end of vfo_Click handling\n"));
#endif
          break;
        case vfo_HoldClick:
#ifdef DEBUG
          Serial.printf(F("menu_show: vfo_HoldClick, exit menu\n"));
#endif
          event_flush();
          return;             // back to the parent menu or main screen
        default:
#ifdef DEBUG
          Serial.printf(F("menu_show: unrecognized event %d\n"), event);
#endif
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
// Since the RE code that pushes to the queue is event-driven, we must be
// careful to disable/enable interrupts at the appropriate places.
//##############################################################################

// the queue itself
VFOEvent event_queue[EventQueueLength];

// queue pointers
int queue_fore = 0;   // points at next event to be popped
int queue_aft = 0;    // points at next free slot for a pushed event

//----------------------------------------
// Push an event onto the event queue.
//     event  number of the event to push
// If queue is full, abort()!
//
// This routine is called only from interrupt code, so needs no protection.
//----------------------------------------

void event_push(VFOEvent event)
{
#ifdef DEBUG
  Serial.printf(F("event_push: pushing %s\n"), event2display(event));
#endif

  // put new event into next empty slot
  event_queue[queue_fore] = event;

  // move fore ptr one slot up, wraparound if necessary
  ++queue_fore;
  if (queue_fore >= EventQueueLength)
    queue_fore = 0;

  // if queue full, abort
  if (queue_aft == queue_fore)
  {
      event_dump_queue("ERROR: event queue full!");
      abort("Event queue full");
  }
}

//----------------------------------------
// Pop next event from the queue.
//
// Returns vfo_None if queue is empty.
//----------------------------------------

VFOEvent event_pop(void)
{
  // Must protect from RE code fiddling with queue
  noInterrupts();

  // if queue empty, return None event
  if (queue_fore == queue_aft)
  {
    interrupts();
    return vfo_None;
  }

  // get next event
  VFOEvent event = event_queue[queue_aft];

  // move aft pointer up one slot, wrap if necessary
  ++queue_aft;
  if (queue_aft >= EventQueueLength)
    queue_aft = 0;

  interrupts();

  return event;
}

//----------------------------------------
// Returns the number of events in the queue.
//----------------------------------------

int event_pending(void)
{
  // Must protect from RE code fiddling with queue
  noInterrupts();

  // get distance between fore and aft pointers
  int result = queue_fore - queue_aft;

  // handle case when events wrap around
  if (result < 0)
    result += EventQueueLength;

  interrupts();

  return result;
}

//----------------------------------------
// Clear out any events in the queue.
//----------------------------------------

void event_flush(void)
{
  // Must protect from RE code fiddling with queue
  noInterrupts();

  queue_fore = 0;
  queue_aft = 0;

  interrupts();
}

//----------------------------------------
// Dump the queue contents to the console.
//     msg  address of message to show
// Debug code.
//----------------------------------------

void event_dump_queue(const char *msg)
{
  // Must protect from RE code fiddling with queue
  noInterrupts();

  Serial.printf(F("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"));
  Serial.printf(F("Queue: %s\n"), msg);
  for (int i = 0; i < EventQueueLength; ++i)
  {
    VFOEvent event = event_queue[i];

    Serial.printf(F("  %d -> %s\n"), i, event2display(event));
  }
  if (event_pending() == 0)
    Serial.printf(F("Queue length=0 (or %d)\n"), EventQueueLength);
  else
    Serial.printf(F("Queue length=%d\n"), event_pending());
  Serial.printf(F("queue_aft=%d"), queue_aft);
  Serial.printf(F(", queue_fore=%d\n"), queue_fore);
  Serial.printf(F("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"));

  interrupts();
}


//##############################################################################
// Utility routines for the display.
//##############################################################################

//----------------------------------------
// Display a value on the display with selected column underlined.
//     value       the number to show
//     sel_col     the selection offset of digit to underline
//                 (0 is rightmost digit, increasing to the left)
//     num_digits  the number of digits to show on the display
//     col, row    position to display left-most digit at
// If the value is too long it will be truncated at the left.
//----------------------------------------

void display_sel_value(ULONG value, int sel_col, int num_digits, int col, int row)
{
  char buf [num_digits];
  int index = num_digits - sel_col - 1;
  bool lead_zero = true;

  // convert value to a buffer of decimal values, [0-9]
  ulong2buff(buf, num_digits, value);

  // create special underlined selection character including selected digit
  lcd.createChar(SelectChar, sel_digits[int(buf[index])]);

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
        lcd.write(byte(SpaceChar));
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
        lcd.write(byte(SelectChar));
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

#ifdef DEBUG
  Serial.printf(F("display_sel_offset: value=%d, sel_col=%d, num_digits=%d, col=%d, row=%d\n"),
                value, sel_col, num_digits, col, row);
#endif

  // worry about a negative value
  if (value < 0)
    prefix = '-';
  value = abs(value);

  // convert value to a buffer of decimal values, [0-9]
  ulong2buff(buf, num_digits, value);

  // create special underlined selection character including selected digit
  lcd.createChar(SelectChar, sel_digits[int(buf[index])]);

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
        lcd.write(byte(SpaceChar));
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
        lcd.write(byte(SelectChar));
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
// from code by Simon Merrett, based on insight from Oleg Mazurov, Nick Gammon,
// rt, Steve Spence
//##############################################################################

// time when click becomes a "hold click" (milliseconds)
// the delay is configurable in the UI
const int MinHoldClickTime = 100;     // min configurable time
const int MaxHoldClickTime = 1000;    // max configurable time
const int DefaultHoldClickTime = 500; // default time

// time when click becomes a "double click" (milliseconds)
// the delay is configurable in the UI
const int MinDClickTime = 100;        // min configurable time
const int MaxDClickTime = 1000;       // max configurable time
const int DefaultDClickTime = 300;    // default time

// internal variables
UINT ReHoldClickTime = DefaultHoldClickTime;
UINT ReDClickTime = DefaultDClickTime;

bool re_rotation = false;     // true if rotation occurred while knob down
bool re_down = false;         // true while knob is down
ULONG re_down_time = 0;       // milliseconds when knob is pressed down
ULONG last_click = 0;         // time when last single click was found

// expecting rising edge on pinA - at detent
volatile byte aFlag = 0;

// expecting rising edge on pinA - at detent
volatile byte bFlag = 0;

//----------------------------------------
// Initialize the rotary encoder stuff.
//
// Return 'true' if button was down during setup.
// This is used to reset some VFO display values if necessary.
//
// Note that the three RE inputs are inverted due to 74HC14 debounce.
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

  // look at RE push button, if DOWN this function returns 'true'
  return digitalRead(re_pinPush);
}

//----------------------------------------
// Handler for pusbutton interrupts (UP or DOWN).
// Note: input signal has been inverted.
//----------------------------------------

void pinPush_isr(void)
{
  // sample the pin value
  re_down = digitalRead(re_pinPush);
#ifdef DEBUG
    Serial.printf(F("pinPush_isr: re_down=0x%02X\n"), re_down);
#endif
  
  if (re_down)
  {
#ifdef DEBUG
    Serial.printf(F("pinPush_isr: button DOWN\n"));
#endif
  
    // button pushed down
    re_rotation = false;      // no rotation while down so far
    re_down_time = millis();  // note time we went down
  }
  else
  {
#ifdef DEBUG
    Serial.printf(F("pinPush_isr: button UP\n"));
#endif
  
    // button released, check if rotation, UP event if not
    if (! re_rotation)
    {
      ULONG last_up_time = millis();
      UINT push_time = last_up_time - re_down_time;

      if (push_time < ReHoldClickTime)
      {
        // check to see if we have a single click very recently
        if (last_click != 0)
        {
          // yes, did have single click before this release
          ULONG dclick_delta = last_up_time - last_click;

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
          // no, this is an isolated release
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
}

//----------------------------------------
// Handler for pinA interrupts.
// Note: input signal has been inverted.
//----------------------------------------

void pinA_isr(void)
{
  // sample the pin values
  byte pin_A = digitalRead(re_pinA);
  byte pin_B = digitalRead(re_pinB);
  
#ifdef DEBUG
    Serial.printf(F("pinA_isr: pin_A=0x%02X, pin_B=0x%02X\n"), pin_A, pin_B);
#endif
  
  if (pin_A && pin_B && aFlag)
  { // check that we have both pins at detent (HIGH)
    // and that we are expecting detent on this pin's rising edge
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
  else if (pin_A && !pin_B)
  {
    // show we're expecting pinB to signal the transition to detent from free rotation
    bFlag = 1;
  }
}

//----------------------------------------
// Handler for pinB interrupts.
// Note: input signal has been inverted.
//----------------------------------------

void pinB_isr(void)
{
  // sample the pin values
  byte pin_A = digitalRead(re_pinA);
  byte pin_B = digitalRead(re_pinB);

#ifdef DEBUG
    Serial.printf(F("pinB_isr: pin_A=0x%02X, pin_B=0x%02X\n"), pin_A, pin_B);
#endif
  
  if (pin_A && pin_B && bFlag)
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
  else if (!pin_A && pin_B)
  {
    // show we're expecting pinA to signal the transition to detent from free rotation
    aFlag = 1;
  }
}


//##############################################################################
// Code to save/restore in EEPROM.
//##############################################################################

// Define the address in EEPROM of various things.
// The "NEXT_FREE" value is the address of the next free slot address.
// Ignore "redefine errors" - not very helpful in this case!
// The idea is that we are free to rearrange objects below with minimum fuss.

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

#ifdef DEBUG
void dump_eeprom(void)
{
  Frequency freq;
  SelOffset offset;
  int clkoffset;
  int brightness;
  int contrast;
  UINT hold;
  UINT dclick;

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
#endif

//##############################################################################
// Code to handle the DDS-60
//
// From: http://www.rocketnumbernine.com/2011/10/25/programming-the-ad9851-dds-synthesizer
// Andrew Smallbone <andrew@rocketnumbernine.com>
//
// I've touched this a little bit, so any errors are mine!
//##############################################################################

//----------------------------------------
// Pulse 'pin' high and then low very quickly.
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
// Set the DDS-60 output frequency.
//
// Only set if VFO is online.
//
// Frequency of sinewave (datasheet page 12) will be
// "<sys clock> * <frequency tuning word> / 2^32".
// Rearranging: FTW = (frequency * 2^32) / sys_clock.
// The 'VfoClockOffset' value is subtracted from <sys_clock> below.
//----------------------------------------

void dds_update(Frequency frequency)
{
  // if not online, do nothing
  if (VfoMode != vfo_Online)
    return;
    
  // as in datasheet page 12 - modified to include calibration offset
  ULONG data = (frequency * 4294967296) / (180000000 - VfoClockOffset);

#ifdef DEBUG
  Serial.printf(F("dds_update: frequency=%ld, VfoClockOffset=%d, data=%ld\n"),
                frequency, VfoClockOffset, data);
#endif

  // start programming the DDS-60
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
#ifdef DEBUG
  Serial.printf(F("DDS into standby mode.\n"));
#endif
  dds_update(0L);
}

//----------------------------------------
// Force the DDS into online mode.
//----------------------------------------

void dds_online(void)
{
#ifdef DEBUG
  Serial.printf(F("DDS into online mode, frequency=%dHz.\n"), VfoFrequency);
#endif
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

  // set serial load enable (Datasheet page 15 Fig. 17) 
  dds_pulse_high(DDS_W_CLK);
  dds_pulse_high(DDS_FQ_UD);

  // go into standby
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
#ifdef DEBUG
  Serial.printf(F("DEBUG is defined\n"));
#endif

  // initialize the BlankRow global to a blank string length of a display row
  BlankRow = (char *) malloc(NumCols + 1);
  for (int i = 0; i < NumCols; ++i)
    BlankRow[i] = ' ';
  BlankRow[NumCols] = '\0';     // don't forget the string terminator

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
  lcd.begin(NumCols, NumRows);      // define display size
  lcd.noCursor();
  lcd.clear();

  // create underlined space for frequency display
  lcd.createChar(SpaceChar, sel_digits[SpaceIndex]);

  // create the "in use" char for use in menus
  lcd.createChar(InUseChar, in_use_char);

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
    ReDClickTime = DefaultDClickTime;
    
    Serial.printf(F("Resetting brightness to %d, contrast to %d and hold time to %d\n"),
                  LcdBrightness, LcdContrast, ReHoldClickTime);
    Serial.printf(F("Click the RE button to continue...\n"));

    // show user we were reset
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("DigitalVFO reset");
    lcd.setCursor(0, 1);
    lcd.print("Click to proceed");

    // flush any events in the queue, then wait for vfo_Click
    event_flush();
    while (event_pop() != vfo_Click)
      ;
 
    // clear screen and continue
    lcd.clear();
    delay(500);
  }
  
  // show program name and version number
  banner();

  // dump EEPROM values
#ifdef DEBUG
  dump_eeprom();
#endif

  // eat any events that may have been generated
  event_flush();

  // show the main screen and continue in loop()
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
  display_sel_value(VfoFrequency, VfoSelectDigit, NumFreqChars, NumCols - NumFreqChars - 2, 0);
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
    lcd.write(byte(InUseChar));
  }
  lcd.setCursor(4, 1);
  lcd.write(slot_num + '0');
  lcd.print(":");
  display_sel_value(freq, -1, NumFreqChars, NumCols - NumFreqChars - 2, 1);
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
          Frequency tmp_freq;     // temp VFO frequency
          SelOffset tmp_select;   // temp column index

          //get_slot(slot_num, VfoFrequency, VfoSelectDigit);
          get_slot(slot_num, tmp_freq, tmp_select);
          if (tmp_freq > 0)
          {
            VfoFrequency = tmp_freq;
            VfoSelectDigit = tmp_select;
            display_flash();
          }
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
  VfoFrequency = MinFreq;
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
  // we want to wait until some sort of click
  while (true)
  {
    // handle any pending event, ignore all but hold-click
    if (event_pop() == vfo_HoldClick)
    {
      event_flush();
      return;
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
    lcd.write(AllsetChar);
}

//----------------------------------------
// Set the current contrast and save to EEPROM if 'actioned'.
//   menu      address of 'calling' menu
//   item_num  index of MenuItem we were actioned from
// We show immediately what the changes mean, but only update the
// actual brightness if we do a vfo_Click action.
//----------------------------------------

void brightness_action(struct Menu *menu, int item_num)
{
  // save original brightness in case we don't update
  int old_brightness = LcdBrightness;
  
  // convert brighness value to a display value in [1, 16]
  int index = (LcdBrightness + 16) / 16;
 
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
          old_brightness = LcdBrightness;
          save_to_eeprom();
          display_flash();
          break;
        case vfo_HoldClick:
          LcdBrightness = old_brightness;
          event_flush();
          return;
        default:
          // ignored events we don't handle
          break;
      }

      // adjust display brightness so we can see the results
      LcdBrightness = (index * 16) - 16;
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
// We show immediately what the changes mean, but only update the
// actual contrast if we do a vfo_Click action.
//
// We limit the allowed range for LcdContrast to [0, 128].
//----------------------------------------

void contrast_action(struct Menu *menu, int item_num)
{
  // save old contrast just in case we don't set it here
  int old_contrast = LcdContrast;
  
  // convert contrast value to a display value in [0, 15]
  int index = LcdContrast / 8;

  if (index > 15)   // ensure in range
    index = 15;
  if (index < 0)
    index = 0;
 
  // get rid of any stray events to this point
  event_flush();

  // draw row 0 of menu, get heading from MenuItem
  lcd.clear();
  lcd.print(menu->items[item_num]->title);
  
  // show row slot info on row 1
  // Contrast voltage works opposite to brightness
  draw_row1_bar(16 - index);

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
          if (++index > 15)
            index = 15;
          break;
        case vfo_RRight:
          if (--index < 0)
            index = 0;
          break;
        case vfo_Click:
          old_contrast = LcdContrast;  // for when we exit
          save_to_eeprom();
          display_flash();
          break;
        case vfo_HoldClick:
          // not saving, restore original contrast setting
          LcdContrast = old_contrast;
          analogWrite(mc_Contrast, LcdContrast);
          event_flush();
          return;
        default:
          // ignored events we don't handle
          break;
      }

      // adjust display contrast so we can see the results
      LcdContrast = index * 8;
      analogWrite(mc_Contrast, LcdContrast);

      // show brightness value in row 1
      // Contrast voltage works opposite to brightness
      draw_row1_bar(16 - index);
    }
  }
}

//----------------------------------------
// Draw a click hold time in milliseconds on row 1 of LCD.
//     msec      the time to show
//     def_time  the current system time
//----------------------------------------

void draw_row1_time(UINT msec, UINT def_time)
{
  lcd.setCursor(0, 1);
  lcd.print(BlankRow);
  
  if (msec == def_time)
  {
    lcd.setCursor(0, 1);
    lcd.write(byte(InUseChar));
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
// This works differently from brightness/contrast.
// We show menuitems of the time to use.
//----------------------------------------

void holdclick_action(struct Menu *menu, int item_num)
{
#ifdef DEBUG
  Serial.printf(F("holdclick_action: entered\n"));
#endif

  UINT holdtime = ReHoldClickTime;      // the value we change
  const UINT hold_step = 100;           // step adjustment +/-
  
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
#ifdef DEBUG
          Serial.printf(F("holdclick_action: vfo_RLeft, after holdtime=%d\n"), holdtime);
#endif
          break;
        case vfo_RRight:
          holdtime += hold_step;
          if (holdtime > MaxHoldClickTime)
            holdtime = MaxHoldClickTime;
#ifdef DEBUG
          Serial.printf(F("holdclick_action: vfo_RRight, after holdtime=%d\n"), holdtime);
#endif
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
// This works differently from brightness/contrast.
// We show menuitems of the time to use.
//----------------------------------------

void doubleclick_action(struct Menu *menu, int item_num)
{
#ifdef DEBUG
  Serial.printf(F("doubleclick_action: entered, ReDClickTime=%dmsec\n"), ReDClickTime);
#endif

  int dctime = ReDClickTime;      // the value we adjust
  UINT dclick_step = 100;         // step adjustment +/-
  
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
#ifdef DEBUG
          Serial.printf(F("doubleclick_action: vfo_RLeft, after dctime=%d\n"), dctime);
#endif
          break;
        case vfo_RRight:
          dctime += dclick_step;
          if (dctime > MaxDClickTime)
            dctime = MaxDClickTime;
#ifdef DEBUG
          Serial.printf(F("doubleclick_action: vfo_RRight, after dctime=%d\n"), dctime);
#endif
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
// If VFO is on standby, force it online, tweak, and then offline again.
//
// In line with all other action menuitems we want to observer the effects
// of changing the value, but also only want to make a permanent change on
// a 'click' action only.
//----------------------------------------

void calibrate_action(struct Menu *menu, int item_num)
{
#ifdef DEBUG
  Serial.printf(F("calibrate_action: entered, VfoClockOffset=%dmsec\n"), VfoClockOffset);
#endif

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
#ifdef DEBUG
          Serial.printf(F("calibrate_action: vfo_RLeft, after VfoClockOffset=%d\n"),
                        VfoClockOffset);
#endif
          break;
        case vfo_RRight:
          VfoClockOffset += offset2bump[seldig];
          if (VfoClockOffset > MaxClockOffset)
            VfoClockOffset = MaxClockOffset;
#ifdef DEBUG
          Serial.printf(F("calibrate_action: vfo_RRight, after VfoClockOffset=%d\n"),
                        VfoClockOffset);
#endif
          break;
        case vfo_DnRLeft:
#ifdef DEBUG
          Serial.printf(F("calibrate_action: vfo_DnRLeft\n"));
#endif
          ++seldig;
          if (seldig >= MaxOffsetDigits)
            seldig = MaxOffsetDigits - 1;
#ifdef DEBUG
          Serial.printf(F("calibrate_action: vfo_DnRLeft, after seldig=%d\n"),
                        seldig);
#endif
          break;
        case vfo_DnRRight:
#ifdef DEBUG
          Serial.printf(F("calibrate_action: vfo_DnRLeft\n"));
#endif
          --seldig;
          if (seldig < 0)
            seldig = 0;
#ifdef DEBUG
          Serial.printf(F("calibrate_action: vfo_DnRRight, after seldig=%d\n"),
                        seldig);
#endif
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
//
// Note that DDS-60 only updates if VfoMode is vfo_Online.
// So we must be sure to set mode to vfo_Standby after dds_standby()
// and set mode to vfo_Online before dds_online().
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
    VfoMode = vfo_Online;
    dds_online();
  }

  // clear row 1 and write new mode string
  lcd.setCursor(0, 1);
  lcd.write(BlankRow);
  lcd.setCursor(0, 1);
  lcd.write(mode2display(VfoMode));
// FIXME: if we display something else on bottom row change this code!
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
// Main menu
//----------------------------------------

struct MenuItem mi_saveslot = {"Save slot", NULL, &saveslot_action};
struct MenuItem mi_restoreslot = {"Restore slot", NULL, &restoreslot_action};
struct MenuItem mi_deleteslot = {"Delete slot", NULL, &deleteslot_action};
struct MenuItem *mia_slots[] = {&mi_saveslot, &mi_restoreslot, &mi_deleteslot};
struct Menu slots_menu = {"Slots", ALEN(mia_slots), mia_slots};

//----------------------------------------
// Main menu
//----------------------------------------

struct MenuItem mi_slots = {"Slots", &slots_menu, NULL};
struct MenuItem mi_settings = {"Settings", &settings_menu, NULL};
struct MenuItem mi_reset = {"Reset all", &reset_menu, NULL};
struct MenuItem mi_credits = {"Credits", NULL, &credits_action};
struct MenuItem *mia_main[] = {&mi_slots, &mi_settings, &mi_reset, &mi_credits};
struct Menu menu_main = {"Menu", ALEN(mia_main), mia_main};


//----------------------------------------
// Standard Arduino loop() function.
//----------------------------------------

char * do_external_cmd(char *cmd, int index)
{
  char end_char = cmd[index];
  static char message[128];

  // ensure everything is uppercase
  str2upper(cmd);

  // if command too long it's illegal
  if (end_char != COMMAND_END_CHAR)
  {
    strcpy(message, "Ignored illegal command: '");
    strcat(message, cmd);
    strcat(message, "'");
    return message;
  }

  // process the command
  strcpy(message, "Would execute '");
  strcat(message, cmd);
  strcat(message, "'  command");
  return message;
}

void loop(void)
{
  // gather any commands from controller
  while (Serial.available()) 
  {
    char ch = Serial.read();
    
    if (CommandIndex < MAX_COMMAND_LEN)
    { 
      CommandBuffer[CommandIndex++] = ch;
    }
    
    if (ch == COMMAND_END_CHAR)   // if end of command, execute it
    {
      CommandBuffer[CommandIndex] = '\0';
      Serial.printf(F("%s\n"), do_external_cmd(CommandBuffer, CommandIndex-1));
      CommandIndex = 0;
    }
  }
  
  // handle all events in the queue
  while (event_pending() > 0)
  {
    // get next event and handle it
    VFOEvent event = event_pop();

    switch (event)
    {
      case vfo_RLeft:
        if (VfoFrequency <= MinFreq)
          break;
        VfoFrequency -= offset2bump[VfoSelectDigit];
        if (VfoFrequency < MinFreq)
          VfoFrequency = MinFreq;
        break;
      case vfo_RRight:
        if (VfoFrequency >= MaxFreq)
          break;
        VfoFrequency += offset2bump[VfoSelectDigit];
        if (VfoFrequency > MaxFreq)
          VfoFrequency = MaxFreq;
        break;
      case vfo_DnRLeft:
        VfoSelectDigit += 1;        
        if (VfoSelectDigit >= NumFreqChars)
          VfoSelectDigit = NumFreqChars - 1;
        break;
      case vfo_DnRRight:
        VfoSelectDigit -= 1;
        if (VfoSelectDigit < 0)
          VfoSelectDigit = 0;
        break;
      case vfo_Click:
        break;
      case vfo_HoldClick:
        menu_show(&menu_main, 0);    // redisplay the original menu
        show_main_screen();
        save_to_eeprom();            // save any changes made in menu
        break;
      case vfo_DClick:
        vfo_toggle_mode();
        break;
      default:
        // unrecognized event, ignore
        break;
    }

    // update the display
    display_sel_value(VfoFrequency, VfoSelectDigit, NumFreqChars, NumCols - NumFreqChars - 2, 0);

    // if online, update DDS-60 and write changes to EEPROM
    if (VfoMode == vfo_Online)
    {
      save_to_eeprom();
      dds_update(VfoFrequency);
    }
  }
}
