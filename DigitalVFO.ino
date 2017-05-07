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


#define DEBUG   // define if debugging


// Digital VFO program name & version
const char *ProgramName = "Digital VFO";
const char *Version = "0.3";
const char *Callsign = "vk4fawr";
const char *Callsign16 = "vk4fawr         ";

// display constants - below is for ubiquitous small HD44780 16x2 display
#define NUM_ROWS        2
#define NUM_COLS        16

// define one row of blanks
const char *BlankRow = "                ";
// check it has the right length
//#if sizeof(BlankRow) <> NUM_COLS
//  #error "BlankRow not defined correctly?"
//#endif

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

// max and min frequency showable
#define MAX_FREQ        30000000L
#define MIN_FREQ        1000000L

// size of frequency display in chars (30MHz is maximum frequency)
#define MAX_FREQ_CHARS  8

// address in display CGRAM for definable characters
#define SELECT_CHAR     0   // shows 'underlined' decimal digits (dynamic, 0 to 9)
#define SPACE_CHAR      1   // shows an 'underlined' space character
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

////////////////////////////////////////////////////////////////////////////////
// The VFO state variables
////////////////////////////////////////////////////////////////////////////////

unsigned long VfoFrequency;   // VFO frequency (Hz)
int VfoSelectDigit;           // selected column index, zero at the right


////////////////////////////////////////////////////////////////////////////////
// Utility routines
////////////////////////////////////////////////////////////////////////////////

//----------------------------------------------------------
// Abort the program.
// Tries to tell the world what went wrong, then just loops.
//
// msg  address of error string
//
// The message may be truncated at display width.
//----------------------------------------------------------

void abort(const char *msg)
{
  Serial.println(msg);
  Serial.println("Teensy is paused!");
  while (1)
  {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.write(msg);
    lcd.setCursor(0, 1);
    lcd.write(msg + NUM_COLS);
    delay(2000);
    
    lcd.clear();
    lcd.write("Teensy is paused");
    lcd.cursor();
    delay(2000);
  }
}

//----------------------------------------------------------
// Convert an event number to a display string
//----------------------------------------------------------

const char * event2display(byte event)
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

// display a simple banner
void banner(void)
{
  Serial.print(ProgramName); Serial.print(" "); Serial.println(Version);
  Serial.println(Callsign);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.write(ProgramName);
  lcd.write(" ");
  lcd.write(Version);
  lcd.setCursor(0, 1);
  lcd.write(Callsign);
  delay(2000);    // wait a bit

  for (int i = 0; i <= 16; ++i)
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


////////////////////////////////////////////////////////////////////////////////
// Code to handle the menus.
////////////////////////////////////////////////////////////////////////////////

// typedef for a menu display handler
// this handler draws the entire current screen
typedef void (*MenuDisplay)(Menu *menu, int slot_num);

// typedef for a menu item action handler
// this handles selecting a particular item
typedef void (*MenuAction)(Menu *menu, int slot_num);

struct Menu
{
  const char *title;
  MenuDisplay display;
  MenuAction action;
  int num_items;
  const char **items;
};

void show_menu(struct Menu *menu)
{
  int item_index = 0;
  
  Serial.print("show_menu("); Serial.print(menu->title); Serial.println(")");
  // update the menu display
  Serial.print("Calling menu->display: menu->title=");
  Serial.print(menu->title);
  Serial.print(", item_index=");
  Serial.println(item_index);
  menu->display(menu, item_index);

  while (true)
  {
    // handle any pending event
    if (queue_len() > 0)
    {
      // get next event and handle it
      byte event = pop_event();
      Serial.print("show_menu: popped event "); Serial.println(event2display(event));

      switch (event)
      {
        case vfo_RLeft:
          Serial.println("Menu: vfo_RLeft");
          --item_index;
          if (item_index < 0)
            item_index = 0;
          break;
        case vfo_RRight:
          Serial.print("Menu: vfo_RRight, menu->num_items="); Serial.println(menu->num_items);
          ++item_index;
          if (item_index >= menu->num_items)
            item_index = menu->num_items - 1;
          Serial.print("Menu: vfo_RRight, new item_index="); Serial.println(item_index);
          break;
        case vfo_DnRLeft:
          Serial.println("Menu: vfo_DnRLeft");
          break;
        case vfo_DnRRight:
          Serial.println("Menu: vfo_DnRRight");
          break;
        case vfo_Click:
          Serial.println("Menu: vfo_Click");
          if (menu->action)
            (*menu->action)(menu, item_index);
          flush_queue();
          return;
        case vfo_HoldClick:
          Serial.println("Menu: vfo_HoldClick, exit menu");
          flush_queue();
          return;
        default:
          Serial.print("Menu: unrecognized event "); Serial.println(event);
          break;
      }
      
      // update the menu display
      Serial.print("Calling menu->display: menu->title=");
      Serial.print(menu->title);
      Serial.print(", item_index=");
      Serial.println(item_index);
      menu->display(menu, item_index);
    }
  }
}


////////////////////////////////////////////////////////////////////////////////
// The system event queue.
// Implemented as a circular buffer.
////////////////////////////////////////////////////////////////////////////////

#define QueueLength 10

byte event_queue[QueueLength];
int queue_fore = 0;   // fore pointer into circular buffer
int queue_aft = 0;    // aft pointer into circular buffer

void push_event(byte event)
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
      dump_queue("ERROR: event queue full:");
      abort("Event queue full");
  }
}

byte pop_event(void)
{
  // Must protect from RE code fiddling with queue
  noInterrupts();

  // if queue empty, return None event
  if (queue_fore == queue_aft)
    return vfo_None;

  // get next event
  byte event = event_queue[queue_aft];

  // mkove aft pointer up one slot, wrap if necessary
  ++queue_aft;
  if (queue_aft  >= QueueLength)
    queue_aft = 0;

  interrupts();

  return event;
}

int queue_len(void)
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

void flush_queue(void)
{
  queue_fore = 0;
  queue_aft = 0;
}

void dump_queue(const char *msg)
{
  // Must protect from RE code fiddling with queue
  noInterrupts();

  Serial.print("Queue: "); Serial.println(msg);
  for (int i = 0; i < QueueLength; ++i)
  {
    Serial.print(" "); Serial.print(event_queue[i]);
  }
  Serial.println("");
  Serial.print("Queue length="); Serial.println(queue_len());
  Serial.print("queue_aft="); Serial.print(queue_aft);
  Serial.print(", queue_fore="); Serial.println(queue_fore);

  interrupts();
}


////////////////////////////////////////////////////////////////////////////////
// Utility routines for the display.
////////////////////////////////////////////////////////////////////////////////

//----------------------------------------------------------
// Function to convert an unsigned long into an array of byte digit values.
//     buf      address of buffer for byte results
//     bufsize  size of the 'buf' buffer
//     value    the unsigned long value to convert
// The function won't overflow the given buffer, it will truncate at the left.
//
// For example, given the value 1234 and a buffer of length 7, will fill the
// buffer with 0001234.  Given 123456789 it will fill with 3456789.
//----------------------------------------------------------

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

//----------------------------------------------------------
// Print the frequency on the display with selected colum underlined.
//     freq     the frequency to display
//     sel_col  the selection offset of digit to underline
//              (0 is rightmost digit, increasing to the left)
//     row      the row to use, 0 is at top
// The row and columns used to show frequency digits are defined elsewhere.
// A final "Hz" is written.
//----------------------------------------------------------

void print_freq(unsigned long freq, int sel_col, int row)
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
////////////////////////////////////////////////////////////////////////////////

// time when click becomes a "hold click" (milliseconds)
#define HoldClickTime 700

// internal variables
bool re_rotation = false;       // true if rotation occurred while knob down
bool re_down = false;           // true while knob is down
unsigned long re_down_time = 0; // milliseconds when knob is pressed down

// expecting rising edge on pinA - at detent
volatile byte aFlag = 0;

// expecting rising edge on pinA - at detent
volatile byte bFlag = 0;

//----------------------------------------------------------
// Setup the encoder stuff, pins, etc.
//----------------------------------------------------------

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
        Serial.println("pinPush_isr pushed vfo_Click to queue");
      }
      else
      {
        push_event(vfo_HoldClick);
        Serial.println("pinPush_isr pushed vfo_HoldClick to queue");
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
      Serial.println("pinA_isr pushed vfo_RLeft to queue");
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
      Serial.println("pinB_isr pushed vfo_DnRRight to queue");
      re_rotation = true;
    }
    else
    {
      push_event(vfo_RRight);
      Serial.println("pinB_isr pushed vfo_RRight to queue");
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

// address for unsigned long 'frequency'
const int AddressFreq = 0;

// address for int 'selected digit'
const int AddressSelDigit = AddressFreq + sizeof(AddressFreq);

// number of frequency save slots in EEPROM
const int NumSaveSlots = 10;
const int SaveSlotBase = AddressSelDigit + sizeof(AddressSelDigit);


// save VFO state to EEPROM
void save_to_eeprom(void)
{
  EEPROM.put(AddressFreq, VfoFrequency);
  EEPROM.put(AddressSelDigit, VfoSelectDigit);
}

// restore VFO state from EEPROM
void restore_from_eeprom(void)
{
  EEPROM.get(AddressFreq, VfoFrequency);
  EEPROM.get(AddressSelDigit, VfoSelectDigit);
}


////////////////////////////////////////////////////////////////////////////////
// Code to handle the DDS-60
////////////////////////////////////////////////////////////////////////////////

void update_dds60(unsigned long freq)
{

}


////////////////////////////////////////////////////////////////////////////////
// Main VFO code
////////////////////////////////////////////////////////////////////////////////

//----------------------------------------------------------
// The standard Arduino setup() function.
//----------------------------------------------------------

void zero_save_slots(void)
{
  for (int i = 0; i < NumSaveSlots; ++i)
  {
    int address = SaveSlotBase + i * sizeof(unsigned long);
    EEPROM.put(address, 0L);
    Serial.print("Zeroing address "); Serial.println(address);
  }
  Serial.println("End of zero_save_slots()");
}

void setup(void)
{
  Serial.begin(115200);

  // initialize the display
  lcd.begin(NUM_COLS, NUM_COLS);      // define display size
  lcd.clear();
  lcd.noCursor();
  lcd.createChar(SPACE_CHAR, sel_digits[SPACE_INDEX]);

  restore_from_eeprom();
//  zero_save_slots();
//  Serial.println("After zero_save_slots()");

  // set up the rotary encoder
  re_setup(VfoSelectDigit);

  // show program name and version number
#ifndef DEBUG
  banner();
#endif
  
  // get going
  show_main_screen();
}

void show_main_screen(void)
{
  lcd.clear();
  print_freq(VfoFrequency, VfoSelectDigit, 0);
}

//-----
// Define the menus to be used, plus handler code
//-----

const char *menu_items[2] = {"Save", "Restore"};
#define NumMainMenuItems (sizeof(menu_items)/sizeof(const char *))

Menu main_menu = {"Menu", menu_display, menu_select,
                  NumMainMenuItems, menu_items};

Menu save_menu = {"Save", save_restore_display, save_select,
                  NumSaveSlots, NULL};

Menu restore_menu = {"Restore", save_restore_display, restore_select,
                     NumSaveSlots, NULL};


void menu_display(Menu *menu, int slot_num)
{
  Serial.println("menu_display() called");
  
  // figure out max length of item strings
  int max_len = 0;

  Serial.print("menu->num_items="); Serial.println(menu->num_items);
  for (int i = 0; i < menu->num_items; ++ i)
  {
    int new_len = strlen(menu->items[i]);

    if (new_len > max_len)
        max_len = new_len;

    Serial.print("i="); Serial.print(i);
    Serial.print(", new_len="); Serial.print(new_len);
    Serial.print(", max_len="); Serial.println(max_len);
  }
  Serial.print("Max item len="); Serial.println(max_len);

  // write menu title on upper row
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.write(menu->title);

  // write indexed item on lower row, right-justified
  lcd.setCursor(0, 1);
  lcd.setCursor(NUM_COLS - max_len, 1);
  lcd.write(menu->items[slot_num]);
  Serial.print("Setting cursor to column "); Serial.println(NUM_COLS - max_len);
  Serial.print("item text="); Serial.println(menu->items[slot_num]);
}

void menu_select(Menu *menu, int slot_num)
{
  Serial.print("menu_select: slot_num="); Serial.println(slot_num);
  const char *action_text = menu->items[slot_num];

  if (strcmp(action_text, "Save") == 0)
  {
      show_menu(&save_menu);
  }
  else if (strcmp(action_text, "Restore") == 0)
  {
      show_menu(&restore_menu);
  }
}

// draw the Save or Restore screen
void save_restore_display(Menu *menu, int slot_num)
{
  Serial.print("save_restore_display: menu="); Serial.println(menu->title);
  Serial.print("save_restore_display: slot_num="); Serial.println(slot_num);

  // max length of item strings is "0: xxxxxxxxHz"
  const int max_len = 13;
  char buf[MAX_FREQ_CHARS + 1];
  
  // get saved frequency, convert to a display buffer
  int address = SaveSlotBase + slot_num * sizeof(unsigned long);
  unsigned long freq;

  EEPROM.get(address, freq);
  ltobbuff(buf, MAX_FREQ_CHARS, freq);
  buf[MAX_FREQ_CHARS] = 0;
  Serial.print("save_restore_display: freq="); Serial.println(freq);
  Serial.print("save_restore_display: buf="); Serial.write(buf, MAX_FREQ_CHARS);
  Serial.println("");

  // write menu title on first row
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.write(menu->title);
  
  // write indexed item on lower row, right-justified
  lcd.setCursor(0, 1);
  lcd.write(BlankRow);
  lcd.setCursor(NUM_COLS - max_len, 1);
  lcd.write(slot_num + '0');
  lcd.write(": ");
  lcd.write(buf);
  lcd.write("Hz");
}

void save_select(Menu *menu, int slot_num)
{
  int address = SaveSlotBase + slot_num * sizeof(unsigned long);

  Serial.print("save_select: saving Frequency "); Serial.print(VfoFrequency);
  Serial.print(" to slot "); Serial.print(slot_num);
  Serial.print("="); Serial.println(address, HEX);
  EEPROM.put(address, VfoFrequency);
}

void restore_select(Menu *menu, int slot_num)
{
  Serial.print("restore_select: slot_num="); Serial.println(slot_num);
  int address = SaveSlotBase + slot_num * sizeof(unsigned long);

  EEPROM.get(address, VfoFrequency);
  Serial.print("restore_select: restoring frequency "); Serial.print(VfoFrequency);
  Serial.print(" from slot "); Serial.print(slot_num);
  Serial.print("="); Serial.println(address, HEX);
}

//----------------------------------------------------------
// Standard Arduino loop() function.
//----------------------------------------------------------

void loop(void)
{
  // remember old values, update screen if changed
  unsigned long old_freq = VfoFrequency;
  int old_position = VfoSelectDigit;

  // handle all events in the queue
  while (queue_len() > 0)
  {
    // get next event and handle it
    byte event = pop_event();
    Serial.print("loop: popped event "); Serial.println(event2display(event));

    switch (event)
    {
      case vfo_RLeft:
        Serial.println("vfo_RLeft");
        VfoFrequency -= offset2bump[VfoSelectDigit];
        if (VfoFrequency < MIN_FREQ)
          VfoFrequency = MIN_FREQ;
        break;
      case vfo_RRight:
        Serial.println("vfo_RRight");
        VfoFrequency += offset2bump[VfoSelectDigit];
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
        Serial.println("Got vfo_HoldClick: calling show_menu()");
        show_menu(&main_menu);
        Serial.println("After show_menu()");
        dump_queue("After show_menu()");
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
