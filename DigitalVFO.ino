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

//#define RESET   // define if resetting all EEPROM data
#define DEBUG   // define if debugging


// Digital VFO program name & version
const char *ProgramName = "DigitalVFO";
const char *Version = "0.8";
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
const byte FQ_UD = 14;    // connected to AD9851 device select pin
const byte W_CLK = 15;    // connected to AD9851 clock pin
const byte DATA = 16;     // connected to AD9851 D7 (serial data) pin 


// max and min frequency showable
#define MAX_FREQ        30000000L
#define MIN_FREQ        1000000L

// size of frequency display in chars (30MHz is maximum frequency)
#define MAX_FREQ_CHARS  8

// address in display CGRAM for definable characters
#define SELECT_CHAR     0   // shows 'underlined' decimal digits (dynamic, 0 to 9)
#define SPACE_CHAR      1   // shows an 'underlined' space character
#define ALLSET_CHAR     2   // shows an 'all bits set' character
// FIXME use only one CGRAM character for 0 to 9 AND space

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

// define custom char for "all bits set"
byte allset[8] = {0x1f,0x1f,0x1f,0x1f,0x1f,0x1f,0x1f,0x1f};

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
  Serial.println(msg);
  Serial.println("Teensy is paused!");

  // truncate/pad message to 32 chars
  for (int i = 0; i < NUM_COLS*2; ++i)
    *ptr++ = ' ';
  *ptr = '\0';
  Serial.print("buf='"); Serial.print(buf); Serial.println("'");
  
  strncpy(buf, msg, NUM_COLS*2);
  Serial.print("buf='"); Serial.print(buf); Serial.println("'");
  if (strlen(msg) < NUM_COLS*2)
    strncpy(buf + strlen(msg), "                                ", NUM_COLS*2 - strlen(msg));
  Serial.print("buf='"); Serial.print(buf); Serial.println("'");

  // show what we can on the display, forever
  while (1)
  {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.write(buf);
    lcd.setCursor(0, 1);
    lcd.write(buf + NUM_COLS);
    delay(2000);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.write(" ");   // padding to centre name+version
    lcd.write(ProgramName);
    lcd.write(" ");
    lcd.write(Version);
    lcd.setCursor(0, 1);
    lcd.write("   is paused");
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

void banner(void)
{
  Serial.print(ProgramName); Serial.print(" "); Serial.print(Version);
  Serial.print(" ("); Serial.print(Callsign); Serial.println(")");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.write(ProgramName);
  lcd.write(" ");
  lcd.write(Version);
  lcd.setCursor(0, 1);
  lcd.write(Callsign);
  delay(2000);    // wait a bit

  for (int i = 0; i <= NUM_COLS; ++i)
  {
    lcd.clear();
    lcd.setCursor(i, 0);
    lcd.write(ProgramName);
    lcd.write(" ");
    lcd.write(Version);
    lcd.setCursor(0, 1);
    lcd.write(Callsign16 + i);
    delay(200);
  }

  delay(500);
}

//----------------------------------------
// Function to convert a Frequency into an array of decimal digit values.
//     buf      address of buffer for byte results
//     bufsize  size of the 'buf' buffer
//     value    the Frequency value to convert
// The function won't overflow the given buffer, it will truncate at the left.
// Leading zeros are suppressed.
//
// For example, given the value 1234 and a buffer of length 7, will fill the
// buffer with "   1234".  Given 123456789 it will fill with "3456789".
//----------------------------------------

void ltochbuff(char *buf, int bufsize, Frequency value)
{
  char *ptr = buf + bufsize - 1;    // rightmost char in 'buf'

  for (int i = 0; i < bufsize; ++i)
  {
    int rem = value % 10;
    char ch = char(rem + '0');

    value = value / 10;
    if (value == 0L && ch == '0')
      *ptr-- = ' ';
    else
      *ptr-- = ch;
  }
}

//----------------------------------------
// Function to convert a Frequency into an array of byte digit values.
//     buf      address of buffer for byte results
//     bufsize  size of the 'buf' buffer
//     value    the Frequency value to convert
// The function won't overflow the given buffer, it will truncate at the left.
//
// For example, given the value 1234 and a buffer of length 7, will fill the
// buffer with 0001234.  Given 123456789 it will fill with 3456789.
//----------------------------------------

void ltobbuff(char *buf, int bufsize, Frequency value)
{
  char *ptr = buf + bufsize - 1;    // rightmost char in 'buf'

  for (int i = 0; i < bufsize; ++i)
  {
    int rem = value % 10;

    value = value / 10;
    *ptr-- = char(rem);
  }
}


////////////////////////////////////////////////////////////////////////////////
// Code to handle the DigitalVFO menus.
////////////////////////////////////////////////////////////////////////////////

// handler for selection of an item (vfo_Click event)
typedef void (*ItemSelection)(Menu *, int);

// handler for inc/dec of custom item (vfo_RRight, vfo_RLeft events)
typedef void (*ItemIncDec)(int delta);

// structure defining a menu item
struct MenuItem
{
  const char *title;          // menu item display text
  struct Menu *menu;          // menu to use after selection
  ItemSelection action;       // routine to handle select on the item
  ItemIncDec incdec;          // routine to handle inc/dec on the item
};

typedef struct MenuItem *MenuItemPtr;

// A structure defining a menu
struct Menu
{
  const char *title;          // title displayed on menu page
  int num_items;              // number of items in the array below
  struct MenuItem **items;    // array of pointers to MenuItem data
};

typedef struct Menu *MenuPtr;

void dump_menu(const char *msg, struct Menu *menu)
{
  Serial.println("----------------- Menu --------------------");
  Serial.println(msg);
  Serial.print("menu address="); Serial.println((unsigned long) menu, HEX);
  Serial.print("  title="); Serial.println(menu->title);
  Serial.print("  num_items="); Serial.println(menu->num_items);
  Serial.print("  items address="); Serial.println((unsigned long) menu->items, HEX);
  Serial.println("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^");
}

void dump_menuitem(const char *msg, struct MenuItem *menuitem)
{
  Serial.println("--------------- MenuItem ------------------");
  Serial.println(msg);
  Serial.print("menuitem address="); Serial.println((unsigned long) menuitem, HEX);
  Serial.print("  title="); Serial.println(menuitem->title);
  Serial.print("  menu="); Serial.println((unsigned long) menuitem->menu, HEX);
  Serial.print("  action="); Serial.println((unsigned long) menuitem->action, HEX);
  Serial.print("  incdec="); Serial.println((unsigned long) menuitem->incdec, HEX);
  Serial.println("^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^");
}

//----------------------------------------
// Draw the menu on the screen
//     menu      pointer to a Menu structure
//----------------------------------------

void menu_draw(struct Menu *menu)
{
  // clear screen and write menu title on upper row
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.write(menu->title);
}

//----------------------------------------
// Draw a standard menuitem on the screen
//     menu      pointer to a Menu structure
//     item_num  the item number to show
//----------------------------------------
void menuitem_draw_std(struct Menu *menu, int item_num)
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
  lcd.write(BlankRow);
  lcd.setCursor(NUM_COLS - max_len, 1);
  lcd.write(menu->items[item_num]->title);
  
}

//----------------------------------------
// Draw a standard or custom menuitem on the screen.
//     menu      pointer to a Menu structure
//     item_num  the item number to show
// Decides if this is a standard or custom menuitem.
//----------------------------------------

void menuitem_draw(struct Menu *menu, int item_num)
{
  if (menu->num_items == 1)
    (menu->items[item_num]->incdec)(0);  // draw without modifying
  else
    menuitem_draw_std(menu, item_num);
}

//----------------------------------------
// Draw a menu page from the passed "menu" structure.
//     menu      pointer to a defining Menu structure
//     item_num  number of item to initially display
// Handle events in the loop here.
//----------------------------------------

void menu_show(struct Menu *menu, int item_num)
{
  //Serial.print("menu_show: menu="); Serial.print((unsigned long) menu, HEX);
  dump_menu("menu_show, menu:", menu);
  Serial.print("item_num="); Serial.println(item_num);
  
  // get rid of any stray events to this point
  flush_events();

  // draw the menu page
  menu_draw(menu);

  dump_menuitem("menu_show, menuitem:", menu->items[item_num]);
  
  // decide if drawing menuitem or a dynamic display
  if (menu->num_items == 1)
    (menu->items[item_num]->incdec)(0);  // draw without modifying
  else
    menuitem_draw(menu, item_num);

  Serial.println("Start of menu_show() loop");
  
  while (true)
  {
    // handle any pending event
    if (events_pending() > 0)
    {
      // get next event and handle it
      byte event = pop_event();
      Serial.print("menu_show() loop: event="); Serial.println(event2display(event));

      switch (event)
      {
        case vfo_RLeft:
          Serial.println("Menu: vfo_RLeft");
          if (menu->num_items == 1)
          {
            // special dynamic menuitem
            menu->items[item_num]->incdec(-1);
          }
          else
          {
            // normal menuitem
            if (--item_num < 0)
              item_num = 0;
          }
          break;
        case vfo_RRight:
          Serial.println("Menu: vfo_RRight");
          if (menu->num_items == 1)
          {
            // special dynamic menuitem
            menu->items[item_num]->incdec(+1);
          }
          else
          {
            // normal menuitem
            if (++item_num >= menu->num_items)
              item_num = menu->num_items - 1;
          }
          break;
        case vfo_DnRLeft:
          Serial.println("Menu: vfo_DnRLeft");
          break;
        case vfo_DnRRight:
          Serial.println("Menu: vfo_DnRRight");
          break;
        case vfo_Click:
          Serial.print("Menu: vfo_Click: menu->title=");
          Serial.print(menu->title);
          Serial.print(", item_num=");
          Serial.print(item_num); Serial.print(", menu->items[item_num]->action=");
          Serial.println((unsigned long) menu->items[item_num]->action, HEX);
          
          // if there's a handler, call it
          // this will probably destroy the curent menu page
          if (menu->items[item_num]->action != NULL)
          {
            Serial.println("Calling vfo_Click handler");
            (*menu->items[item_num]->action)(menu->items[item_num]->menu, 0);
          }

          menu_draw(menu);    // redraw the menu header
          Serial.println("Menu: end of vfo_Click handling");
          break;
        case vfo_HoldClick:
          Serial.println("Menu: vfo_HoldClick, exit menu");
          flush_events();
          return;
        default:
          Serial.print("Menu: unrecognized event "); Serial.println(event);
          break;
      }

      // update the menu display
      if (menu->num_items == 1)
        (menu->items[item_num]->incdec)(0);  // draw without modifying
      else
        menuitem_draw(menu, item_num);

      dump_menuitem("menu_show: new menuitem=", menu->items[item_num]);
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

void push_event(VFOEvent event)
{
  Serial.print("push_event: new event="); Serial.print(event);
  Serial.print(" = "); Serial.println(event2display(event));

  // put new event into next empty slot
  event_queue[queue_fore] = event;

  // move fore ptr one slot up, wraparound if necessary
  ++queue_fore;
  if (queue_fore >= QueueLength)
    queue_fore = 0;

  // if queue full, abort
  if (queue_aft == queue_fore)
  {
      dump_queue("ERROR: event queue full:");
      abort("Event queue full");
  }
}

VFOEvent pop_event(void)
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

int events_pending(void)
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

void flush_events(void)
{
  queue_fore = 0;
  queue_aft = 0;
}

void dump_queue(const char *msg)
{
  // Must protect from RE code fiddling with queue
  noInterrupts();

  Serial.println("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
  Serial.print("Queue: "); Serial.println(msg);
  for (int i = 0; i < QueueLength; ++i)
  {
    VFOEvent event = event_queue[i];
    
    Serial.print("  ");
    Serial.print(event);
    Serial.print(" -> ");
    Serial.println(event2display(event));
  }
  Serial.print("Queue length="); Serial.println(events_pending());
  Serial.print("queue_aft="); Serial.print(queue_aft);
  Serial.print(", queue_fore="); Serial.println(queue_fore);
  Serial.println("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");

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
// A final "Hz" is written.
//----------------------------------------

void print_freq(Frequency freq, int sel_col, int row)
{
  char buf [MAX_FREQ_CHARS];
  int index = MAX_FREQ_CHARS - sel_col - 1;
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
// from code by Simon Merrett, based on insight from Oleg Mazurov, Nick Gammon, rt, Steve Spence
////////////////////////////////////////////////////////////////////////////////

// time when click becomes a "hold click" (milliseconds)
#define HoldClickTime 500

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
      unsigned long push_time = millis() - re_down_time;

      if (push_time < HoldClickTime)
      {
        push_event(vfo_Click);
      }
      else
      {
        push_event(vfo_HoldClick);
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
      push_event(vfo_DnRLeft);
      re_rotation = true;
    }
    else
    {
      push_event(vfo_RLeft);
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
      push_event(vfo_DnRRight);
      re_rotation = true;
    }
    else
    {
      push_event(vfo_RRight);
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

// address for Frequency 'frequency'
const int AddressFreq = 0;
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

// number of frequency save slots in EEPROM
const int NumSaveSlots = 10;

const int SaveFreqSize = sizeof(long);
const int SaveFreqBase = AddressBrightness + AddressBrightnessSize;
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
  Serial.println("save_to_eeprom() called");
  
  EEPROM.put(AddressFreq, VfoFrequency);
  EEPROM.put(AddressSelDigit, VfoSelectDigit);
  EEPROM.put(AddressBrightness, LcdBrightness);
  EEPROM.put(AddressContrast, LcdContrast);
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
  Serial.print("put_slot: slot_num="); Serial.print(slot_num);
  Serial.print(", freq="); Serial.print(freq);
  Serial.print(", offset="); Serial.println(offset);
  
  int freq_address = SaveFreqBase + slot_num * SaveFreqSize;
  int offset_address = SaveOffsetBase + slot_num * SaveOffsetSize;

  Serial.print("Saving freq to slot at "); Serial.println(freq_address);
  EEPROM.put(freq_address, freq);
  Serial.print("Saving offset to slot at "); Serial.println(offset_address);
  EEPROM.put(offset_address, offset);
}

//----------------------------------------
// print all EEPROM saved data to console
//----------------------------------------

void dump_eeprom(void)
{
  Frequency ulong;
  SelOffset offset;
  int brightness;
  int contrast;

  EEPROM.get(AddressFreq, ulong);
  EEPROM.get(AddressSelDigit, offset);
  EEPROM.get(AddressBrightness, brightness);
  EEPROM.get(AddressContrast, contrast);
  Serial.println("=================================================");
  Serial.print("dump_eeprom: VfoFrequency="); Serial.println(ulong);
  Serial.print("             AddressSelDigit="); Serial.println(offset);
  Serial.print("             LcdBrightness="); Serial.println(brightness);
  Serial.print("             LcdContrast="); Serial.println(contrast);

  for (int i = 0; i < NumSaveSlots; ++i)
  {
    get_slot(i, ulong, offset);
    Serial.print("Slot "); Serial.print(i); Serial.print(": ");
    Serial.print(ulong); Serial.print(", ");
    Serial.println(offset);
  }

  Serial.println("=================================================");
}

//----------------------------------------
//----------------------------------------

void get_vfo_freq_offset(Frequency &freq, SelOffset &offset)
{
  EEPROM.get(AddressFreq, freq);
  EEPROM.get(AddressSelDigit, offset);
}

////////////////////////////////////////////////////////////////////////////////
// Code to handle the DDS-60
//
// From: http://www.rocketnumbernine.com/2011/10/25/programming-the-ad9851-dds-synthesizer
// Andrew Smallbone <andrew@rocketnumbernine.com>
////////////////////////////////////////////////////////////////////////////////

//----------------------------------------
//----------------------------------------

void dds_pulse_high(byte pin)
{
  digitalWrite(pin, HIGH);
  digitalWrite(pin, LOW);
}

//----------------------------------------
// transfer a byte a bit at a time LSB first to DATA
//----------------------------------------

void dds_tfr_byte(byte data)
{
  for (int i = 0; i < 8; ++i, data >>= 1)
  {
    digitalWrite(DATA, data & 0x01);
    dds_pulse_high(W_CLK);
  }
}

//----------------------------------------
// frequency of signwave (datasheet page 12) will be <sys clock> * <frequency tuning word> / 2^32
//----------------------------------------

void update_dds60(Frequency frequency)
{
  int32_t freq = frequency * 4294967296.0 / 180.0e6;

  Serial.print("update_dds60: frequency="); Serial.println(frequency);
  Serial.print("                   fred="); Serial.println(freq);
  
  for (int b = 0; b < 4; ++b, freq >>= 8)
  {
    dds_tfr_byte(freq & 0xFF);
  }
  
  dds_tfr_byte(0x001);
  dds_pulse_high(FQ_UD);
}

//----------------------------------------
//----------------------------------------

void dds_setup()
{
  // all pins to outputs
  pinMode(FQ_UD, OUTPUT);
  pinMode(W_CLK, OUTPUT);
  pinMode(DATA, OUTPUT);

  // if your board needs it, connect RESET pin and pulse it to reset AD9851
  // dds_pulse_high(RESET)

  // set serial load enable (Datasheet page 15 Fig. 17) 
  dds_pulse_high(W_CLK);
  dds_pulse_high(FQ_UD);
}


////////////////////////////////////////////////////////////////////////////////
// Main VFO code
////////////////////////////////////////////////////////////////////////////////

//----------------------------------------
// The standard Arduino setup() function.
//----------------------------------------

void zero_slots(void)
{
  Frequency zero_freq = 0L;
  SelOffset zero_offset = 0;

  // zero the frequency+selected values
  VfoFrequency = MIN_FREQ;
  VfoSelectDigit = 0;
  LcdBrightness = DefaultLcdBrightness;
  LcdContrast = DefaultLcdContrast;
  
  save_to_eeprom();

  for (int i = 0; i < NumSaveSlots; ++i)
  {
    put_slot(i, zero_freq, zero_offset);
  }
}

void setup(void)
{
  Serial.begin(115200);

#ifdef RESET
  zero_slots();
  Serial.println("All EEPROM data reset");
#endif

  // initialize the display
  lcd.begin(NUM_COLS, NUM_COLS);      // define display size
  lcd.clear();
  lcd.noCursor();
  lcd.createChar(SPACE_CHAR, sel_digits[SPACE_INDEX]);
  lcd.createChar(ALLSET_CHAR, allset);

  pinMode(mc_Brightness, OUTPUT);
  pinMode(mc_Contrast, OUTPUT);

  // get state back from EEPROM
  restore_from_eeprom();
  analogWrite(mc_Brightness, LcdBrightness);
  analogWrite(mc_Contrast, LcdContrast);

  // set up the rotary encoder
  re_setup(VfoSelectDigit);

  // show program name and version number
#ifndef DEBUG
  banner();
  if (pop_event() == vfo_HoldClick)
  {
    LcdBrightness = DefaultLcdBrightness;
    analogWrite(mc_Brightness, LcdBrightness);
    LcdContrast = DefaultLcdContrast;
    analogWrite(mc_Contrast, LcdContrast);

    Serial.print("Reset (not saved) brightness to "); Serial.print(LcdBrightness);
    Serial.print(" and contrast to "); Serial.println(LcdContrast);
  }
#endif

  // we sometimes see random events on powerup, flush them here
  flush_events();
  
#ifdef DEBUG
  // dump EEPROM values
  dump_eeprom();
#endif

  // get going
  show_main_screen();

  // we sometimes see random events on powerup, flush them here
  flush_events();
}

//----------------------------------------
//----------------------------------------

void show_main_screen(void)
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.write("Freq:");
  print_freq(VfoFrequency, VfoSelectDigit, 0);
}

//----------------------------------------
// Define the menu handler routines.
// These actually do the work, everything else is just navigation.
//----------------------------------------

// show a save slot, draw in row 1
void saveslot_show(Menu *menu, int slot_num)
{
  Serial.print("saveslot_show() entered, slot_num="); Serial.println(slot_num);

  Frequency freq;
  SelOffset offset;

  // max length of item strings is "9: xxxxxxxxHz" - 13 chars
  const int max_len = 13;
  char buf[MAX_FREQ_CHARS + 1];

  get_slot(slot_num, freq, offset);

  ltochbuff(buf, MAX_FREQ_CHARS, freq);
  buf[MAX_FREQ_CHARS] = 0;

//  // write menu title on first row
//  lcd.clear();
//  lcd.setCursor(0, 0);
//  lcd.write(menu->title);

  // write indexed item on lower row, right-justified
  lcd.setCursor(0, 1);
  lcd.write(BlankRow);
  // show '*' if slot frequency equal to current
  if (freq == VfoFrequency)
  {
    lcd.setCursor(0, 1);
    lcd.write("*");
  }
  lcd.setCursor(NUM_COLS - max_len, 1);
  lcd.write(slot_num + '0');
  lcd.write(": ");
  lcd.write(buf);
  lcd.write("Hz");
}

//----------------------------------------
// save the current frequency to a slot
//----------------------------------------

void saveslot_action(Menu *menu, int item_num)
{
  Serial.println("saveslot_action() entered");

  // display an 'action' flash
  menu_flash();

  put_slot(item_num, VfoFrequency, VfoSelectDigit);

  savresdel_show(menu, item_num);
}

//----------------------------------------
// cycle through the save slots
//----------------------------------------

void saveslot_incdec(int delta)
{
  
}

//----------------------------------------
// restore the current frequency from a slot
//----------------------------------------

void restoreslot_action(Menu *menu, int item_num)
{
  Serial.println("restoreslot_show() entered");

  // display an 'action' flash
  menu_flash();

  get_slot(item_num, VfoFrequency, VfoSelectDigit);

  savresdel_show(menu, item_num);  
}

//----------------------------------------
// cycle through the slots
//----------------------------------------

void restoreslot_incdec(int delta)
{
  
}

//----------------------------------------
// delete the frequency in a slot
//----------------------------------------

void deleteslot_action(Menu *menu, int item_num)
{
  Serial.println("deleteslot_show() entered");

  // display an 'action' flash
  menu_flash();

  put_slot(item_num, 0L, 0);

  savresdel_show(menu, item_num);    
}

//----------------------------------------
// cycle through the slots
//----------------------------------------

void deleteslot_incdec(int delta)
{
  
}

//----------------------------------------
// save the current brightness to EEPROM
//----------------------------------------

void brightness_action(Menu *menu, int item_num)
{
  Serial.println("brightness_action() entered");

  // display an 'action' flash, save brightness to EEPROM
  menu_flash();
  save_to_eeprom();
}

//----------------------------------------
// adjust the LCD brightness up or down and update menu display
//----------------------------------------

void brightness_incdec(int delta)
{
  Serial.print("brightness_incdec: delta="); Serial.println(delta);
  
  // convert brighness value to a display value in [1, 16]
  int index = (LcdBrightness + 1) / 16;
    
  if (delta != 0)
  {
    Serial.print("Old LcdBrightness="); Serial.print(LcdBrightness);
    Serial.print(", index="); Serial.println(index);
    
    // first, bump the internal index, limit to range
    index += delta;
    if (index < 1)
      index = 1;
    else
      if (index > 16)
        index = 16;
  
    Serial.print("New LcdBrightness="); Serial.print(LcdBrightness);
    Serial.print(", index="); Serial.println(index);
  
    // put brightness back into global variable and adjust display brightness
    LcdBrightness = (index * 16) - 1;
    analogWrite(mc_Brightness, LcdBrightness);
  }
  
  // draw the row 1 representation of the value
  lcd.setCursor(0, 1);
  lcd.write(BlankRow);
  lcd.setCursor(0, 1);
  for (int i = 0; i < index; ++i)
  {
    lcd.write(ALLSET_CHAR);
  }
}

//----------------------------------------
// save the current contrast to EEPROM
//----------------------------------------

void contrast_action(Menu *menu, int item_num)
{
  Serial.println("contrast_action() entered");

  // display an 'action' flash, save contrast to EEPROM
  save_to_eeprom();
  menu_flash();
}

//----------------------------------------
// adjust the LCD contrast up or down and update menu display
//----------------------------------------

void contrast_incdec(int delta)
{
  Serial.print("contrast_incdec: delta="); Serial.println(delta);
  
  // convert brighness value to a display value in [1, 16]
  int index = 16 - LcdContrast / 8;
    
  if (delta != 0)
  {
    Serial.print("Old LcdContrast="); Serial.print(LcdContrast);
    Serial.print(", index="); Serial.println(index);
    
    // first, bump the internal index, limit to range
    index += delta;
    if (index < 1)
      index = 1;
    else
      if (index > 16)
        index = 16;
  
    Serial.print("New LcdContrast="); Serial.print(LcdContrast);
    Serial.print(", index="); Serial.println(index);
  
    // put brightness back into global variable and adjust display brightness
    LcdContrast = (16 - index) * 8;
    analogWrite(mc_Contrast, LcdContrast);
  }
  
  // draw the row 1 representation of the value
  lcd.setCursor(0, 1);
  lcd.write(BlankRow);
  lcd.setCursor(0, 1);
  for (int i = 0; i < index; ++i)
  {
    lcd.write(ALLSET_CHAR);
  }
}

//----------------------------------------
// Brightness & Contrast menu with custom menuitems
//----------------------------------------

struct MenuItem mic_brightness = {NULL, NULL, brightness_action, brightness_incdec};
struct MenuItem *miac_brightness[] = {&mic_brightness};
struct Menu brightness_menu = {"Brightness", ARRAY_LEN(miac_brightness), miac_brightness};

struct MenuItem mic_contrast = {NULL, NULL, contrast_action, contrast_incdec};
struct MenuItem *miac_contrast[] = {&mic_contrast};
struct Menu contrast_menu = {"Contrast", ARRAY_LEN(miac_contrast), miac_contrast};

//----------------------------------------
// Settings menu
//----------------------------------------

struct MenuItem mi_brightness = {"Brightness", &brightness_menu, &menu_show, NULL};
struct MenuItem mi_contrast = {"Contrast", &contrast_menu, &menu_show, NULL};
struct MenuItem *mia_settings[] = {&mi_brightness, &mi_contrast};
struct Menu settings_menu = {"Settings", ARRAY_LEN(mia_settings), mia_settings};

//----------------------------------------
// "save slot" menu
//----------------------------------------

struct MenuItem mi_saveslot = {"Save slot", NULL, saveslot_action, saveslot_incdec};
struct MenuItem *mia_saveslot[] = {&mi_saveslot};
struct Menu saveslot_menu = {"Save slot", ARRAY_LEN(mia_saveslot), mia_saveslot};

//----------------------------------------
// "restore slot" menu
//----------------------------------------

struct MenuItem mi_restoreslot = {"Restore slot", NULL, restoreslot_action, restoreslot_incdec};
struct MenuItem *mia_restoreslot[] = {&mi_restoreslot};
struct Menu restoreslot_menu = {"Restore slot", ARRAY_LEN(mia_restoreslot), mia_restoreslot};

//----------------------------------------
// "delete slot" menu
//----------------------------------------

struct MenuItem mi_deleteslot = {"Delete slot", NULL, deleteslot_action, deleteslot_incdec};
struct MenuItem *mia_deleteslot[] = {&mi_deleteslot};
struct Menu deleteslot_menu = {"Delete slot", ARRAY_LEN(mia_deleteslot), mia_deleteslot};

//----------------------------------------
// main menu
//----------------------------------------

struct MenuItem mi_save = {"Save slot", &saveslot_menu, &menu_show, NULL};
struct MenuItem mi_restore = {"Restore slot", &restoreslot_menu, &menu_show, NULL};
struct MenuItem mi_del = {"Delete slot", &deleteslot_menu, &menu_show, NULL};
struct MenuItem mi_settings = {"Settings", &settings_menu, &menu_show, NULL};
struct MenuItem *mia_main[] = {&mi_save, &mi_restore, &mi_del, &mi_settings};
struct Menu menu_main = {"Menu", ARRAY_LEN(mia_main), mia_main};


//----------------------------------------
// draw the Save, Restore or Delete screen
//----------------------------------------

void savresdel_show(struct Menu *menu, int slot_num)
{
  Frequency freq;
  SelOffset offset;

  // max length of item strings is "9: xxxxxxxxHz" - 13 chars
  const int max_len = 13;
  char buf[MAX_FREQ_CHARS + 1];

  get_slot(slot_num, freq, offset);

  ltochbuff(buf, MAX_FREQ_CHARS, freq);
  buf[MAX_FREQ_CHARS] = 0;

  // write menu title on first row
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.write(menu->title);

  // write indexed item on lower row, right-justified
  lcd.setCursor(0, 1);
  lcd.write(BlankRow);
  // show '*' if slot frequency equal to current
  if (freq == VfoFrequency)
  {
    lcd.setCursor(0, 1);
    lcd.write("*");
  }
  lcd.setCursor(NUM_COLS - max_len, 1);
  lcd.write(slot_num + '0');
  lcd.write(": ");
  lcd.write(buf);
  lcd.write("Hz");
}

//----------------------------------------
//----------------------------------------

void save_select(struct Menu *menu, int slot_num)
{
  savresdel_show(menu, slot_num);

  while (true)
  {
    
  }
  // display an 'action flash'
  menu_flash();
  
  put_slot(slot_num, VfoFrequency, VfoSelectDigit);

  savresdel_show(menu, slot_num);
}

//----------------------------------------
//----------------------------------------

void restore_select(struct Menu *menu, int slot_num)
{
  // display an 'action flash'
  menu_flash();
  
  get_slot(slot_num, VfoFrequency, VfoSelectDigit);

//  savresdel_show(menu, slot_num);  NOT NEEDED?
}

//----------------------------------------
//----------------------------------------

void delete_select(struct Menu *menu, int slot_num)
{
  // display an 'action flash'
  menu_flash();
  
  put_slot(slot_num, 0L, 0);

//  savresdel_show(menu, slot_num);  NOT NEEDED?
}

//----------------------------------------
//----------------------------------------

void contrast_select(struct Menu *menu, int slot_num)
{
  Serial.print("contrast_select(): slot_num="); Serial.println(slot_num);

  // adjust contrast
  // +/- opposite to that for brightness because increasing
  // voltage has the opposite effect with contrast
  if (slot_num == 0)        // "increase" item selected 
    LcdContrast -= 10;
  else if (slot_num == 1)   // "decrease" item selected
    LcdContrast += 10;
  else
    abort("Bad slot number");

  if (LcdContrast < 0)
    LcdContrast = 0;
  if (LcdContrast > 125)
    LcdContrast = 125;
  analogWrite(mc_Contrast, LcdContrast);
  Serial.print("Set contrast to "); Serial.println(LcdContrast);
  menu_flash();
}

//----------------------------------------
//----------------------------------------

void brightness_select(struct Menu *menu, int slot_num)
{
  Serial.print("brightness_select(): slot_num="); Serial.println(slot_num);
  
  if (slot_num == 0)        // "increase" item selected 
    LcdBrightness += 10;
  else if (slot_num == 1)   // "increase" item selected 
    LcdBrightness -= 10;
  else
    abort("Bad slot numvber");

  if (LcdBrightness < 0)
    LcdBrightness = 0;
  if (LcdBrightness > 255)
    LcdBrightness = 255;

  analogWrite(mc_Brightness, LcdBrightness);
  Serial.print("Set brightness to "); Serial.println(LcdBrightness);
  menu_flash();  
}

//----------------------------------------
// Standard Arduino loop() function.
//----------------------------------------

void loop(void)
{
  // remember old values, update screen if changed
  Frequency old_freq = VfoFrequency;
  int old_position = VfoSelectDigit;

  // handle all events in the queue
  while (events_pending() > 0)
  {
    // get next event and handle it
    VFOEvent event = pop_event();

    switch (event)
    {
      case vfo_RLeft:
        Serial.println("vfo_RLeft");
        VfoFrequency -= offset2bump[VfoSelectDigit];
        if (VfoFrequency < MIN_FREQ)
          VfoFrequency = MIN_FREQ;
        if (VfoFrequency > MAX_FREQ)
          VfoFrequency = MAX_FREQ;
       break;
      case vfo_RRight:
        Serial.println("vfo_RRight");
        VfoFrequency += offset2bump[VfoSelectDigit];
        if (VfoFrequency < MIN_FREQ)
          VfoFrequency = MIN_FREQ;
        if (VfoFrequency > MAX_FREQ)
          VfoFrequency = MAX_FREQ;
        break;
      case vfo_DnRLeft:
        Serial.println("vfo_DnRLeft");
        VfoSelectDigit += 1;        
        if (VfoSelectDigit >= MAX_FREQ_CHARS)
          VfoSelectDigit = MAX_FREQ_CHARS - 1;
        break;
      case vfo_DnRRight:
        Serial.println("vfo_DnRRight");
        VfoSelectDigit -= 1;
        if (VfoSelectDigit < 0)
          VfoSelectDigit = 0;
        break;
      case vfo_Click:
        Serial.println("vfo_Click event ignored");
        break;
      case vfo_HoldClick:
        Serial.println("Got vfo_HoldClick: calling menu_show()");

  Serial.print("brightness_menu="); Serial.println((unsigned long) &brightness_menu, HEX);
  Serial.print("mic_brightness="); Serial.println((unsigned long) &mic_brightness, HEX);

        menu_show(&menu_main, 0);
        save_to_eeprom();  // save any changes made
        show_main_screen();
        break;
      default:
        Serial.print("Unrecognized event: "); Serial.println(event);
        break;
    }

    // display frequency if changed, update DDS-60
    if (old_freq != VfoFrequency || old_position != VfoSelectDigit)
    {
      print_freq(VfoFrequency, VfoSelectDigit, 0);
      old_freq = VfoFrequency;
      old_position = VfoSelectDigit;

      save_to_eeprom();

      update_dds60(VfoFrequency);
    }
  }
}
