/*
  Blink
  Turns on an LED on for one second, then off for one second, repeatedly.
 
  This example code is in the public domain.
 */
 
// Pin 13 has an LED connected on most Arduino boards.
// Pin 11 has the LED on Teensy 2.0
// Pin 6  has the LED on Teensy++ 2.0
// Pin 13 has the LED on Teensy 3.0
// give it a name:
int led = 13;

// the setup routine runs once when you press reset:
void setup() {                
  // initialize the serial console
  Serial.begin(115200);
  Serial.println("Type short commands terminated by ';', eg, \"help;\".");
  
  // initialize the digital pin as an output.
  pinMode(led, OUTPUT);     
  digitalWrite(led, HIGH);    // turn the LED on (HIGH is the voltage level)
}

//----------------------------------------
// Do any commands from the external controller.
//----------------------------------------

// buffer, etc, to gather external command strings
#define MAX_COMMAND_LEN   16
#define COMMAND_END_CHAR    ';'
char CommandBuffer[MAX_COMMAND_LEN+1];
int CommandIndex = 0;

bool do_external_commands(void)
{
  // gather any commands from the external controller
  while (Serial.available()) 
  {
    char ch = Serial.read();

    // ignore a newline
    if (ch == '\n')
      continue;
    
    if (CommandIndex < MAX_COMMAND_LEN)
    { 
      CommandBuffer[CommandIndex++] = tolower(ch);
    }
    
    if (ch == COMMAND_END_CHAR)   // if end of command, execute it
    {
      CommandBuffer[CommandIndex] = '\0';
      CommandIndex = 0;
      
      if (strcmp(CommandBuffer, "quit;") == 0)
        return false;
      if (strcmp(CommandBuffer, "help;") == 0)
      {
        Serial.print("Type in command strings shorter than ");
        Serial.print(MAX_COMMAND_LEN);
        Serial.println(" characters terminated by a ';' character.");
      }
      Serial.print("handled ");
      Serial.println(CommandBuffer);
      return true;
    }
  }

  return false;
}

// delay between heartbeat flashes - milliseconds
#define HEARTBEAT_DELAY   5000

unsigned int next_heartbeat = millis() + 500;

// the loop routine runs over and over again forever:
void loop()
{
  if (do_external_commands())
  {
    digitalWrite(led, LOW);     // turn the LED on by making the voltage LOW
    delay(50);                  // wait a bit
    digitalWrite(led, HIGH);    // turn the LED off (HIGH is the voltage level)
    delay(50);                  // wait a bit
  }

  if (millis() > next_heartbeat)
  {
    next_heartbeat = millis() + HEARTBEAT_DELAY;
    
    digitalWrite(led, LOW);     // turn the LED on by making the voltage LOW
    delay(50);                  // wait a bit
    digitalWrite(led, HIGH);    // turn the LED off (HIGH is the voltage level)
    delay(50);                  // wait a bit
  }
}
