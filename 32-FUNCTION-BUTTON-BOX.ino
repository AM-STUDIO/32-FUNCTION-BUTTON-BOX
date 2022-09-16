//BUTTON BOX 
//USE w ProMicro
//Tested in WIN10 + Assetto Corsa
//AMSTUDIO
//20.11.12

#include <Keypad.h>
#include <Joystick.h>

#define ENABLE_PULLUPS
#define NUMROTARIES 4
#define NUMBUTTONS 24
#define NUMROWS 5
#define NUMCOLS 5

#define ASYNC_UPDATE_MILLIS 20

byte buttons[NUMROWS][NUMCOLS] = {
  {0, 1, 2, 3, 4},
  {5, 6, 7, 8, 9},
  {10,11,12,13,14},
  {15,16,17,18,19},
  {20,21,22,23,/*see note*/15},
};
/* note: the joystick cannot have more than 32 buttons total.
 * - the button matrix can detect 25 buttons
 * - the 4 rotaries are taking button IDs 24-31
 * this means the last button above will be sent as
 * a copy of _some_ button ID, and I chose 15.
 */

struct rotariesdef {
  byte pin1;
  byte pin2;
  byte ccwchar;
  byte cwchar;
  byte halfstep;
  byte pulldown;
  signed char ccw_count;
  signed char cw_count;
  volatile unsigned char state;
};

rotariesdef rotaries[NUMROTARIES] {
  {0,1,24,25,1,0,0, 0},   /* propwash dual axis 0 - halfstep */
  {2,3,26,27,1,0,0, 0},   /* propwash dual axis 1 - halfstep */
  {4,5,28,29,0,0,0, 0},   /* standard encoder */
  {6,7,30,31,0,0,0, 0},   /* standard encoder */
};

#define DIR_CCW 0x10
#define DIR_CW 0x20

/* half-step rotary states
 * if pins are reading 00, one click would transition to pins reading 11
 * if pins are reading 11, one click would transition to pins reading 00
 *
 * one bit changes state before the other for CW and CCW transitions
 * whichever pin changes first determines the direction.
 */
enum {
  Rh_START_LO = 0,
  Rh_CCW_BEGIN,
  Rh_CW_BEGIN,

  Rh_START_HI,
  Rh_CW_BEGIN_HI,
  Rh_CCW_BEGIN_HI,

  Rh_MAX
};

const unsigned char ttable_half[Rh_MAX][4] = {
  // pin bits - transistions from 00 to 00 or 11
  //  00                   01              10             11
  // Rh_START_LO (00) - usually either both on or both off
  {Rh_START_LO,           Rh_CW_BEGIN,    Rh_CCW_BEGIN,   Rh_START_HI},
  // Rh_CCW_BEGIN (was at 10)
  {Rh_START_LO,           Rh_START_LO,    Rh_CCW_BEGIN,   Rh_START_HI | DIR_CCW},
  // Rh_CW_BEGIN  (was at 01)
  {Rh_START_LO,           Rh_CW_BEGIN,    Rh_START_LO,    Rh_START_HI | DIR_CW},

  // pin bits - transistions from 11 to 00 or 11
  //  00                   01              10              11
  // Rh_START_HI (11) - usually either both on or both off
  {Rh_START_LO,           Rh_CCW_BEGIN_HI, Rh_CW_BEGIN_HI, Rh_START_HI},
  // Rh_CW_BEGIN_HI (was at 10)
  {Rh_START_LO | DIR_CW,  Rh_START_HI,     Rh_CW_BEGIN_HI, Rh_START_HI},
  // Rh_CCW_BEGIN_HI (was at 01)
  {Rh_START_LO | DIR_CCW, Rh_CCW_BEGIN_HI, Rh_START_HI,    Rh_START_HI},
};

/* X-step rotaries */
enum {
  R_START = 0x0,
  R_CW_FINAL,
  R_CW_BEGIN,
  R_CW_NEXT,
  R_CCW_BEGIN,
  R_CCW_FINAL,
  R_CCW_NEXT,

  R_MAX
};

const unsigned char ttable[R_MAX][4] = {
  // pin bits - transistions
  //  00       01           10           11
  // R_START
  {R_START,    R_CW_BEGIN,  R_CCW_BEGIN, R_START},
  // R_CW_FINAL
  {R_CW_NEXT,  R_START,     R_CW_FINAL,  R_START | DIR_CW},
  // R_CW_BEGIN
  {R_CW_NEXT,  R_CW_BEGIN,  R_START,     R_START},
  // R_CW_NEXT
  {R_CW_NEXT,  R_CW_BEGIN,  R_CW_FINAL,  R_START},
  // R_CCW_BEGIN
  {R_CCW_NEXT, R_START,     R_CCW_BEGIN, R_START},
  // R_CCW_FINAL
  {R_CCW_NEXT, R_CCW_FINAL, R_START,     R_START | DIR_CCW},
  // R_CCW_NEXT
  {R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START},
};

byte rowPins[NUMROWS] = {21,20,19,18,15}; 
byte colPins[NUMCOLS] = {14,16,10,9,8}; 

Keypad buttbx = Keypad( makeKeymap(buttons), rowPins, colPins, NUMROWS, NUMCOLS); 

Joystick_ Joystick(JOYSTICK_DEFAULT_REPORT_ID, 
  JOYSTICK_TYPE_JOYSTICK, 32, 0,
  false, false, false, false, false, false,
  false, false, false, false, false);

unsigned long LastSendTime;

void setup() {
#ifdef ASYNC_UPDATE_MILLIS
  Joystick.begin(false);
  buttbx.setDebounceTime(2*ASYNC_UPDATE_MILLIS);
#else
  Joystick.begin(true);
#endif
  rotary_init();
  LastSendTime = millis();
}

int CheckAllEncoders(void);
int CheckAllButtons(void);

void loop() { 
  int           changes = 0;
  unsigned long now;

  CheckAllEncoders();

  now = millis();

#ifdef ASYNC_UPDATE_MILLIS
  if ( (signed long)(now - LastSendTime) > ASYNC_UPDATE_MILLIS ) {
    /* do the clicks */
    for (int i=0;i<NUMROTARIES;i++) {
      if ( rotaries[i].cw_count > 0 ) {        /* clockwise clicks */
        if ( !(rotaries[i].cw_count & 1) ) {   /* EVEN: start click */
          Joystick.setButton(rotaries[i].cwchar, 1);
        } else {
          Joystick.setButton(rotaries[i].cwchar, 0);
        }
        rotaries[i].cw_count--;
        changes++;
      }
      if ( rotaries[i].ccw_count > 0 ) { /* counter-clockwise clicks */
        if ( !(rotaries[i].ccw_count & 1) ) {   /* EVEN: start click */
          Joystick.setButton(rotaries[i].ccwchar, 1);
        } else {
          Joystick.setButton(rotaries[i].ccwchar, 0);
        }
        rotaries[i].ccw_count--;
        changes++;
      }
    }
  }
  changes += CheckAllButtons();

  if ( changes ) {
    Joystick.sendState();
    LastSendTime = now /* millis() */;
  } else {
    //delay(1);
  }
#else
  changes += CheckAllButtons();
#endif
}

int CheckAllButtons(void) {
  int changes = 0;
  if (buttbx.getKeys())
    {
       for (int i=0; i<LIST_MAX; i++)   
        {
           if ( buttbx.key[i].stateChanged )   
            {
              switch (buttbx.key[i].kstate) {  
                    case PRESSED:
                    case HOLD:
                              Joystick.setButton(buttbx.key[i].kchar, 1);
                              changes++;
                              break;
                    case RELEASED:
                    case IDLE:
                              Joystick.setButton(buttbx.key[i].kchar, 0);
                              changes++;
                              break;
            }
           }   
         }
     }
  return changes;
}


void rotary_init() {
  for (int i=0;i<NUMROTARIES;i++) {
    pinMode(rotaries[i].pin1, INPUT);
    pinMode(rotaries[i].pin2, INPUT);
    if ( rotaries[i].pulldown ) {
      digitalWrite(rotaries[i].pin1, LOW);
      digitalWrite(rotaries[i].pin2, LOW);
    } else {
      digitalWrite(rotaries[i].pin1, HIGH);
      digitalWrite(rotaries[i].pin2, HIGH);
    }
  }
}


unsigned char rotary_process(int _i) {
   unsigned char pinstate = (digitalRead(rotaries[_i].pin2) << 1) | digitalRead(rotaries[_i].pin1);
   if ( rotaries[_i].halfstep ) {
      rotaries[_i].state = ttable_half[rotaries[_i].state & 0xf][pinstate];
   } else {
      rotaries[_i].state = ttable[rotaries[_i].state & 0xf][pinstate];
   }
  return (rotaries[_i].state & 0x30);
}

int CheckAllEncoders(void) {
  int changes = 0;
  for (int i=0;i<NUMROTARIES;i++) {
    unsigned char result = rotary_process(i);
    if (result == DIR_CCW) {
      changes++;
      #ifdef ASYNC_UPDATE_MILLIS
        rotaries[i].ccw_count += 2;
        rotaries[i].state &= 0xf; /* clear the CW/CCW state as we've added to click count */
        /* and cancel the opposite rotation, note we get rid
         * of everything except the LSB so it may "wind down" to 0
         */
        rotaries[i].cw_count &= 0x1;
      #else
        Joystick.setButton(rotaries[i].ccwchar, 1); delay(50); Joystick.setButton(rotaries[i].ccwchar, 0);
      #endif
    };
    if (result == DIR_CW) {
      changes++;
      #ifdef ASYNC_UPDATE_MILLIS
        rotaries[i].cw_count += 2;
        rotaries[i].state &= 0xf; /* clear the CW/CCW state as we've added to click count */
        /* and cancel the opposite rotation, note we get rid
         * of everything except the LSB so it may "wind down" to 0
         */
        rotaries[i].ccw_count &= 0x1;
      #else
        Joystick.setButton(rotaries[i].cwchar, 1); delay(50); Joystick.setButton(rotaries[i].cwchar, 0);
      #endif
    };
  }
  return changes;
}
