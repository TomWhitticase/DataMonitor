#include <Wire.h>
#include <Adafruit_RGBLCDShield.h>
#include <utility/Adafruit_MCP23017.h>
#include <EEPROM.h>
#include <avr/eeprom.h>

Adafruit_RGBLCDShield lcd = Adafruit_RGBLCDShield();

#define EEPROM_KEY 101

#define PURPLE 5
#define WHITE 7
#define RED 1
#define GREEN 2
#define YELLOW 3

#define SIZE 26
#define PREVIOUS_SIZE 12

#define UP_ARROW 0
uint8_t upArrow[] = { B00100, B01110, B11111, B10101, B00100, B00100, B00100, B00100 };
#define DOWN_ARROW 1
uint8_t downArrow[] = { B00100, B00100, B00100, B00100, B10101, B11111, B01110, B00100 };

const char STUDENTID[] = "F132708";

typedef enum state_e {SYNCHRONISATION, AFTERSYNC, AWAITING_INPUT, PROCESSING_INPUT} state_t;

struct channel {
  char description[16];
  char id;
  byte value;
  byte minVal;
  byte maxVal;
  bool active;
  byte average;
  byte previous[PREVIOUS_SIZE];
  byte n;
};

#ifdef __arm__
// should use uinstd.h to define sbrk but Due causes a conflict
extern "C" char* sbrk(int incr);
#else // __ARM__
extern char *__brkval;
#endif // __arm__

int freeMemory() {
  char top;
#ifdef __arm__
  return &top - reinterpret_cast<char*>(sbrk(0));
#elif defined(CORE_TEENSY) || (ARDUINO > 103 && ARDUINO != 151)
  return &top - __brkval;
#else // __arm__
  return __brkval ? &top - __brkval : &top - __malloc_heap_start;
#endif // __arm__
}

//reads eeprom and recovers previous channel data
bool load_EEPROM(struct channel *channels) {
  int addr = 0;
  if (EEPROM.read(addr++) != EEPROM_KEY) {
    Serial.println(F("DEBUG: no previously saved data on EEPROM"));
    return false;
  }
  Serial.println(F("DEBUG: reading saved data from EEPROM"));

  for (int i = 0; i < SIZE; i++) {
    channels[i].id = EEPROM.read(addr++);
    for (int j = 0; j < 15; j++) {
      channels[i].description[j] = EEPROM.read(addr++);
    }
    channels[i].value = 0;
    channels[i].minVal = EEPROM.read(addr++);
    channels[i].maxVal = EEPROM.read(addr++);
    channels[i].active = false;
  }
  return true;
}

//saves channel data to EEPROM
void save_EEPROM(struct channel channels[]) {
  Serial.println(F("DEBUG: saving to EEPROM"));
  clear_EEPROM();
  int addr = 0;
  EEPROM.update(addr++, EEPROM_KEY);
  for (int i = 0; i < SIZE; i++) {
    EEPROM.update(addr++, channels[i].id);
    for (int j = 0; j < 15; j++) {
      EEPROM.update(addr++, channels[i].description[j]);
    }
    EEPROM.update(addr++, channels[i].minVal);
    EEPROM.update(addr++, channels[i].maxVal);
  }
}

void clear_EEPROM() {
  for (int i = 0 ; i < EEPROM.length() ; i++) {
    EEPROM.update(i, 0);
  }
}

//bubble sort channels by their id ascending character
void sort_channels(struct channel *channels) {
  bool noSwaps = false;
  for (int j = 0; j < SIZE - 1; j++) {
    noSwaps = true;
    for (int i = 0; i < num_active_channels(channels, false, false) - 1; i++) {
      if (channels[i].id > channels[i + 1].id) {
        struct channel temp = channels[i];
        channels[i] = channels[i + 1];
        channels[i + 1] = temp;
        noSwaps = false;
      }
    }
    if (noSwaps) break;
  }
  return;
}

//returns the appropriate backlight colour by looking through channels and checking if any value is above its max or below its min
byte get_backlight_colour(struct channel channels[]) {
  bool valBelow = false;
  bool valAbove = false;
  //check if any values are larger than their max or smaller than their min
  for (byte i = 0; i < SIZE; i++) {
    if (!channels[i].active) continue; //skip channels with undefined values
    if (!valBelow and channels[i].value < channels[i].minVal and channels[i].minVal <= channels[i].maxVal)
      valBelow = true;
    if (!valAbove and channels[i].value > channels[i].maxVal)
      valAbove = true;
    if (valAbove and valBelow) break;
  }
  //return the appropriate colour
  if (valBelow and valAbove) return YELLOW;
  if (valBelow) return GREEN;
  if (valAbove) return RED;
  return WHITE;
}

//updates the lcd screen
void display_channels(struct channel channels[], int channelPointers[], bool displayId, bool left, bool right) {
  lcd.clear();
  if (displayId) {
    lcd.setBacklight(PURPLE);
    lcd.setCursor(0, 0);
    lcd.print(STUDENTID);
    lcd.setCursor(0, 1);
    lcd.print(F("Free SRAM: "));
    lcd.print(freeMemory());
    return;
  }
  lcd.setBacklight(get_backlight_colour(channels));
  //display nothing if no channels are active
  if (num_active_channels(channels, left, right) == 0) return;
  if (channelPointers[0] == -1) return;
  //line 1
  lcd.setCursor(0, 0);
  if (prev_active_channel(channels, channelPointers[0], left, right) == -1) {
    lcd.print(' ');
  } else {
    lcd.write(UP_ARROW);
  }
  lcd.print(channels[channelPointers[0]].id);
  //add whitespace to right align if needed
  if (channels[channelPointers[0]].value < 100) lcd.print(' ');
  if (channels[channelPointers[0]].value < 10) lcd.print(' ');
  lcd.print(channels[channelPointers[0]].value);
  lcd.print(',');
  //Average
  //add whitespace to right align if needed
  if (channels[channelPointers[0]].average < 100) lcd.print(' ');
  if (channels[channelPointers[0]].average < 10) lcd.print(' ');
  lcd.print(channels[channelPointers[0]].average);
  lcd.print(' ');
  lcd.print(channels[channelPointers[0]].description);
  //skip line 2 if only 1 channel active
  if (channelPointers[1] == SIZE) return;
  //line2
  lcd.setCursor(0, 1);
  if (next_active_channel(channels, channelPointers[1], left, right) == SIZE) {
    lcd.print(' ');
  } else {
    lcd.write(DOWN_ARROW);
  }
  lcd.print(channels[channelPointers[1]].id);
  //add whitespace to right align if needed
  if (channels[channelPointers[1]].value < 100) lcd.print(' ');
  if (channels[channelPointers[1]].value < 10) lcd.print(' ');
  lcd.print(channels[channelPointers[1]].value);
  lcd.print(',');
  //Average
  //add whitespace to right align if needed
  if (channels[channelPointers[1]].average < 100) lcd.print(' ');
  if (channels[channelPointers[1]].average < 10) lcd.print(' ');
  lcd.print(channels[channelPointers[1]].average);
  lcd.print(' ');
  lcd.print(channels[channelPointers[1]].description);
}

//returns the number of channels that are active
//if there is no next channel then return SIZE (26)
byte num_active_channels(struct channel channels[], bool left, bool right) {
  byte count = 0;
  for (byte i = 0; i < SIZE; i++) {
    if (channels[i].active) {
      if (left) {
        if (channels[i].value < channels[i].minVal) count++;
      } else if (right) {
        if (channels[i].value > channels[i].maxVal) count++;
      } else {
        count++;
      }
    }
  }
  return count;
}

//returns index of next active channel
int next_active_channel(struct channel channels[], int i, bool left, bool right) {
  i++;
  if (i > SIZE) return SIZE;

  if (right) {
    while (!channels[i].active or channels[i].value <= channels[i].maxVal) {
      i++;
      if (i > SIZE - 1) {
        return SIZE;
      }
    }
  } else if (left) {
    while (!channels[i].active or channels[i].value >= channels[i].minVal) {
      i++;
      if (i > SIZE - 1) {
        return SIZE;
      }
    }
  } else {
    while (!channels[i].active) {
      i++;
      if (i > SIZE - 1) {
        return SIZE;
      }
    }
  }
  return i;
}

//returns index of previous active channel
//if there is no previous channel then return -1
int prev_active_channel(struct channel channels[], int i, bool left, bool right) {
  i--;
  if (i < -1) return -1;

  if (right) {
    while (!channels[i].active or channels[i].value <= channels[i].maxVal) {
      i--;
      if (i < -1) {
        return -1;
      }
    }
  } else if (left) {
    while (!channels[i].active or channels[i].value >= channels[i].minVal) {
      i--;
      if (i < -1) {
        return -1;
      }
    }
  } else {
    while (!channels[i].active) {
      i--;
      if (i < -1) {
        return -1;
      }
    }
  }
  return i;
}


//scrolls channel pointers up
bool scroll_up(struct channel channels[], int *channelPointers, bool left, bool right) {
  //if there is no previous channel to scroll up to then do nothing
  if (prev_active_channel(channels, channelPointers[0], left, right) == -1) return false;
  //decrease pointers to previous channels
  channelPointers[0] = prev_active_channel(channels, channelPointers[0], left, right);
  channelPointers[1] = next_active_channel(channels, channelPointers[0], left, right);
  return true;
}

//scrolls channel pointers down
bool scroll_down(struct channel channels[], int *channelPointers, bool left, bool right) {
  //if there is no next channel to scroll down to then do nothing
  if (next_active_channel(channels, channelPointers[1], left, right) == SIZE) return false;
  //increase pointers to next channels
  channelPointers[1] = next_active_channel(channels, channelPointers[1], left, right);
  channelPointers[0] = prev_active_channel(channels, channelPointers[1], left, right);
  return true;
}

//checks if a channel has been created (NOT checking if it is active)
bool channel_created(struct channel channels[], char id) {
  for (byte i = 0; i < SIZE; i++) {
    if (channels[i].id == id) {
      return true;
    }
  }
  return false;
}

//returns the index of the next empty channel
int next_free_channel(struct channel channels[]) {
  for (int i = 0; i < SIZE; i++) {
    if (channels[i].id == '?') return i;
  }
  return -1;
}

//creates new channel with given id and description, if channel already exists then just change description
//returns false if unable to create channel
bool new_channel(struct channel channels[], char id, char description[16], int *channelPointers) {
  int i = get_channel_index(channels, id);
  if (i == -1) { //channel does not exists yet
    i = next_free_channel(channels);
    if (i == -1) { //no more free channels
      Serial.println(F("DEBUG: Maximum number of channels have already been created"));
      return false;
    }
    channels[i].id = id;
    strcpy(channels[i].description, description);
  } else { //channel exists so change its description
    strcpy(channels[i].description, description);
  }
  return true;
}

//check if given string can be parsed to a 8 bit number
bool is_byte_number(char description[16]) {
  if (0 < atoi(description) and atoi(description) < 256) return true;
  if (description[0] == '0' and description[1] == '\0') return true;
  return false;
}

//sets max value for channel with given id
bool set_max_value(struct channel channels[], char id, char desc[16]) {
  int val = atoi(desc);
  ///check if changing value for given channel is allowed, return false if it is not
  if (!is_byte_number(desc)) { //check if string entered is is a valid number
    Serial.println("DEBUG: value entered is not a valid int in range 0-255");
    return false;
  }
  //check if channel has been created
  if (!channel_created(channels, id)) {
    Serial.println(F("DEBUG: channel does not exist"));
    return false;
  }
  //change max value of given channel
  channels[get_channel_index(channels, id)].maxVal = val;
  return true;
}

//sets the min value for a channel with given id
bool set_min_value(struct channel channels[], char id, char desc[16]) {
  int val = atoi(desc);
  //check if changing value for given channel is allowed, return false if it is not
  if (!is_byte_number(desc)) { //check if string entered is is a valid number
    Serial.println("DEBUG: value entered is not a valid int in range 0-255");
    return false;
  }
  //check if channel has been created
  if (!channel_created(channels, id)) {
    Serial.println(F("DEBUG: channel does not exist"));
    return false;
  }
  //change min value of given channel
  channels[get_channel_index(channels, id)].minVal = val;
  return true;
}

bool change_value(struct channel channels[], char id, char desc[16]) {
  int val = atoi(desc);
  //check if changing value for given channel is allowed, return false if it is not
  if (!is_byte_number(desc)) { //check if string entered is is a valid number
    Serial.println("DEBUG: value entered is not a valid int in range 0-255");
    return false;
  }
  //check if channel has been created
  if (!channel_created(channels, id)) {
    Serial.println(F("DEBUG: channel does not exist"));
    return false;
  }
  int i = get_channel_index(channels, id);

  //rotate previous values array
  for (int k = PREVIOUS_SIZE - 1; k > 0; k--) {
    channels[i].previous[k] = channels[i].previous[k - 1];
  }
  channels[i].previous[0] = channels[i].value;
  if (channels[i].n < PREVIOUS_SIZE)
    channels[i].n++;

  //change value of given channel
  channels[i].value = val;
  channels[i].active = true;
  
  //update previous values and average;
  int sum = val;
  for (int j = 0; j < channels[i].n; j++) {
    sum += channels[i].previous[j];
  }
  channels[i].average = sum / (channels[i].n);
  return true;
}

int get_channel_index(struct channel channels[], char id) {
  for (int i = 0; i < SIZE; i++) {
    if (channels[i].id == id) {
      return i;
    }
  }
  return -1;
}

//Prints channel values. used for debugging
void print_channels(struct channel channels[]) {
  Serial.println(F("printing channels:"));
  for (int i = 0; i < SIZE; i++) {
    Serial.print(F("id:"));
    Serial.print(channels[i].id);
    Serial.print(F(", Desc:"));
    Serial.print(channels[i].description);
    Serial.print(F(", Value:"));
    Serial.print(channels[i].value);
    Serial.print(F(", Min Value:"));
    Serial.print(channels[i].minVal);
    Serial.print(F(", MaxValue:"));
    Serial.print(channels[i].maxVal);
    Serial.print(F(", n: "));
    Serial.print(channels[i].n);
    Serial.print(F(", Avg: "));
    Serial.print(channels[i].average);
    Serial.print(" [");
    for(int j = 0; j < PREVIOUS_SIZE; j++){
      Serial.print(channels[i].previous[j]);
      Serial.print(',');
    }
    Serial.println(F("] "));    
  }
}

bool process_input(String input, struct channel channels[], int *channelPointers, bool left, bool right) {
  char id = input[1];
  char desc[16] = "               ";
  for (byte i = 0; i < strlen(desc); i++) {
    desc[i] = input[i + 2];
    if (desc[i] == '\0') break;
  }

  if (desc[0] == '\0') {
    return false;
  }

  //check that id is an upper case letter A-Z
  if(!isalpha(id)) return false;
  if(id != toupper(id)) return false;

  switch (input[0]) {
    case '*':
      clear_EEPROM();
      create_channel_array(channels);
 //     print_channels(channels);
      break;
    case 'C':
      if (new_channel(channels, id, desc, channelPointers)) {
        save_EEPROM(channels);
        return true;
      }
      return false;
      break;
    case 'V':
      if (change_value(channels, id, desc)) {
        //update channel pointers
        channelPointers[0] = next_active_channel(channels, -1, left, right);
        if (num_active_channels(channels, left, right) > 1)
          channelPointers[1] = next_active_channel(channels, channelPointers[0], left, right);
        sort_channels(channels);
        save_EEPROM(channels);
        return true;
      }
      return false;
      break;
    case 'X':
      if (set_max_value(channels, id, desc)) {
        save_EEPROM(channels);
        return true;
      }
      return false;
      break;
    case 'N':
      if (set_min_value(channels, id, desc)) {
        save_EEPROM(channels);
        return true;
      }
      return false;
      break;
    default:
      return false;
      break;
  }
}

//sets all the values of every channel to starting default values
void create_channel_array(struct channel *channels) {
  if (load_EEPROM(channels)) {
    return;
  }

  for (int i = 0; i < SIZE; i++) {
    channels[i].id = '?';
    channels[i].description[0] = '\0';
    channels[i].minVal = 0;
    channels[i].maxVal = 255;
    channels[i].value = 0;
    channels[i].average = 0;
    channels[i].n = 0;
    for (int j = 0; j < PREVIOUS_SIZE; j++) {
      channels[i].previous[j] = 0;
    }
  }
}

void loop() {
  static state_t state = SYNCHRONISATION;

  //create channel array
  static struct channel channels[SIZE];
  static bool channelsCreated = false;
  if (!channelsCreated)
    create_channel_array(channels);
  channelsCreated = true;

  //variables used to display student id when select is being held
  static bool displayId = false;
  static long timestamp = millis();
  static bool holdingSelect = false;

  //variables used for HCI
  static bool left = false;
  static bool right = false;

  //pointers used for scrolling through channels
  static int channelPointers[2] = { -1, SIZE};

  //pointers used for scrolling description
  static byte descScrollPointers[2] = {0, 0};

  //used to for timing
  static long lastMillis = millis();

  static uint8_t prevButtonsState = 0;

  switch (state) {
    case SYNCHRONISATION:
      lcd.setBacklight(PURPLE);//set backlight purple
      if (millis() - lastMillis > 1000) {
        lastMillis = millis();
        Serial.print('Q');
        if (Serial.available() > 0) {
          // read the incoming byte:
          String msg = Serial.readString();
          if (msg == "X") {
            state = AFTERSYNC;
            break;
          }
        }
      }
      break;
    case AFTERSYNC:
      lcd.setBacklight(WHITE); //white
      Serial.println("UDCHARS,FREERAM,HCI,EEPROM,RECENT,NAMES,SCROLL\n");
      state = AWAITING_INPUT;
      break;
    case AWAITING_INPUT:
      if (true) {
        if (Serial.available() > 0) {
          // read the serial monitor input
          state = PROCESSING_INPUT;
          break;
        }

        if (lastMillis + 500 < millis()) {
          lastMillis = millis();
          //update desc scroll pointers
          for (int i = 0; i < 2; i++) {
            descScrollPointers[i]++;
            if (descScrollPointers[i] > 9) descScrollPointers[i] = 0;
          }

          //scroll text
          if (!displayId) {
            //top scroll
            if (channelPointers[0] != -1) {//if there is a channel to display on lcd first line
              if (strlen(channels[channelPointers[0]].description) > 6) {//only scroll if the description is longer than the display (6 chars)
                lcd.setCursor(10, 0);
                for (int i = 0; i < 7; i++) {
                  //set the scroll pointer back to begginning if pointing to end of description
                  if (descScrollPointers[0] + i > strlen(channels[channelPointers[0]].description) - 1) {
                    descScrollPointers[0] = -1;
                  } else {
                    lcd.print(channels[channelPointers[0]].description[descScrollPointers[0] + i]);
                  }
                }
              }
            }
            //bottom scroll
            if (channelPointers[1] != SIZE) {// if there is a channel to display on lcd second line
              if (strlen(channels[channelPointers[1]].description) > 6) {//only scroll if the description is longer than the display (6 chars)
                lcd.setCursor(10, 1);
                for (int i = 0; i < 7; i++) {
                  //set the scroll pointer back to begginning if pointing to end of description
                  if (descScrollPointers[1] + i > strlen(channels[channelPointers[1]].description) - 1) {
                    descScrollPointers[1] = -1;
                  } else {
                    lcd.print(channels[channelPointers[1]].description[descScrollPointers[1] + i]);
                  }
                }
              }
            }
          }
        }

        //button stuff
        uint8_t buttonsState = lcd.readButtons();
        //check if the buttons state is different to last previous buttons state
        if (buttonsState != prevButtonsState) {
          buttonsState = lcd.readButtons();
          if (buttonsState & BUTTON_UP) {
            if (scroll_up(channels, channelPointers, left, right))
              display_channels(channels, channelPointers, displayId, left, right);
          } else if (buttonsState & BUTTON_DOWN) {
            if (scroll_down(channels, channelPointers, left, right))
              display_channels(channels, channelPointers, displayId, left, right);
          }
          if (buttonsState & BUTTON_LEFT) {
            left = !left;
            right = false;
            //reset channel pointers
            channelPointers[0] = -1;
            channelPointers[1] = SIZE;
            if (num_active_channels(channels, left, right) > 0) channelPointers[0] = next_active_channel(channels, -1, left, right);
            if (num_active_channels(channels, left, right) > 1) channelPointers[1] = next_active_channel(channels, channelPointers[0], left, right);
            display_channels(channels, channelPointers, displayId, left, right);
          } else if (buttonsState & BUTTON_RIGHT) {
            right = !right;
            left = false;
            //reset channel pointers
            channelPointers[0] = -1;
            channelPointers[1] = SIZE;
            if (num_active_channels(channels, left, right) > 0) channelPointers[0] = next_active_channel(channels, -1, left, right);
            if (num_active_channels(channels, left, right) > 1) channelPointers[1] = next_active_channel(channels, channelPointers[0], left, right);
            display_channels(channels, channelPointers, displayId, left, right);
          }
          if (holdingSelect & !(buttonsState & BUTTON_SELECT)) {
            holdingSelect = false;
            if (displayId)
              display_channels(channels, channelPointers, false, left, right);
            displayId = false;
          }
        }

        //displayId
        if (buttonsState & BUTTON_SELECT) {
          if (!holdingSelect) {
            holdingSelect = true;
            timestamp = millis();
          } else if (millis() - timestamp > 1000 & !displayId) {
            displayId = true;
            display_channels(channels, channelPointers, displayId, left, right);
          }
        }
        //save button state value for next loop
        prevButtonsState = buttonsState;

      }
      break;
    case PROCESSING_INPUT:
      String input = Serial.readString();
      if (process_input(input, channels, channelPointers, left, right)) {
        display_channels(channels, channelPointers, displayId, left, right);
//        print_channels(channels);
      } else {
        Serial.print(F("ERROR: "));
        Serial.println(input);
      }
      state = AWAITING_INPUT;
      break;
  }
}



void setup() {
  Serial.begin(9600);
  lcd.begin(16, 2);

  //create custom up and down arrow characters
  lcd.createChar(UP_ARROW, upArrow);
  lcd.createChar(DOWN_ARROW, downArrow);
}