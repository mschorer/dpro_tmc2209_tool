/*
 * configuring a tmc2209/9 for reasonable parameters
 *
 */

#include <DigisparkOLED.h>
#include <Wire.h>

// Define pins
#define EN_PIN    10 // LOW: Driver enabled. HIGH: Driver disabled
#define DIR_PIN   11 // Set stepping direction
#define STEP_PIN  12 // Step on rising edge

#define R_SENSE 0.11f // Match to your driver
                      // SilentStepStick series use 0.11
                      // UltiMachine Einsy and Archim2 boards use 0.2
                      // Panucatt BSD2660 uses 0.1
                      // Watterott TMC5160 uses 0.075

#include <TMCStepper.h>
#include <TMCStepper_UTILITY.h>

using namespace TMC2209_n;

//#define AUTOMOVE 1

// Create driver that uses SoftwareSerial for communication
TMC2209Stepper* driver;

byte loops = 0;
byte rotIdx = 0;
char label[4] = { 'x', 'X', 'y', 'z'};
char bar[4] = { ' ', '-', '=', '#'};

#ifdef AUTOMOVE
#define RAMP_MIN 0
#define RAMP_MAX 5000
#define RAMP_CYCLE 2
#define RAMP_STEP 25

#define RAMP_LOW  0
#define RAMP_UP   1
#define RAMP_HIGH 2
#define RAMP_DOWN 3

uint16_t ramp_current = RAMP_MIN;
uint16_t ramp_idx = RAMP_CYCLE;
byte ramp_mode = RAMP_LOW;
#endif

bool detect = true;

struct tmcConfig_t {
  char name[4];
  uint32_t gconf;
  uint32_t ihold_irun;
  uint32_t tpowerdown;
  uint32_t tpwmthrs;
  uint32_t chopconf;
  uint32_t pwmconf;
  uint32_t tcoolthrs;
  uint16_t coolconf;
};

typedef tmcConfig_t TmcConfig;

TmcConfig tmcDefault = { 
    "4",
    0x1c0,                                // gconf
    ( 6UL | ( 12UL << 8) | ( 6UL << 16)),  // ihold_irun: hold-run-hdelay
    20,                                   // tpowerdown
    150,                                  // tpwmthrs
    0x169102d4,                           // chopconf, 4msteps
    0xc80d0e24,                           // pwmconf
    0,                                    // tcoolthrs off
    0,                                    // coolconf
  };

TmcConfig tmcDefaultZ = { 
    "2",
    0x1c0,                                // gconf
    ( 6UL | (12UL << 8) | ( 6UL << 16)),  // ihold_irun: hold-run-hdelay
    20,                                   // tpowerdown
    150,                                  // tpwmthrs
    0x179102d4,                           // chopconf, 2msteps
    0xc80d0e24,                           // pwmconf
    0,  /*150, */                         // tcoolthrs on
    0,  /*0x2468, */                      // coolconf
  };

TmcConfig* tmc[4] = { &tmcDefault, &tmcDefault, &tmcDefault, &tmcDefaultZ };

void setup() {

#ifdef AUTOMOVE
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, HIGH);
#endif

  oled.begin();
 
  oled.setCursor(0, 0); //top left
  oled.setFont(FONT8X16);
  oled.print(F("TMC2209")); //wrap strings in F() to save RAM!

  Serial.begin(9600);
  
  driver = new TMC2209Stepper( &Serial, R_SENSE, 0);
  driver->defaults();

  oled.setFont(FONT6X8);
  oled.setCursor(0, 4);
  for( byte i=0; i < 4; i++) {
    oled.print( label[ i]);
    oled.println( tmc[i]->name);
  }

  oled.setCursor(64, 0);
  oled.print(( tmcDefault.ihold_irun >> 8) & 0x1f);
  oled.print(F("/"));
  oled.print( tmcDefault.ihold_irun & 0x1f);
/*
  while( setupTmc2209()) {
    delay( 1000);
  }
*/
  if ( setupTmc2209()) oled.print(F(" err"));
  else oled.print(F(" 03"));

#ifdef AUTOMOVE
  digitalWrite(EN_PIN, LOW);
#endif
}

void loop() { // run over and over
  uint32_t data;
  uint8_t x;

#ifdef AUTOMOVE
  switch( ramp_mode) {
    case RAMP_LOW:
    ramp_current = RAMP_MIN;
      if ( ! --ramp_idx) { 
        ramp_mode = RAMP_UP;
        ramp_idx = RAMP_CYCLE;
      }
    break;
    case RAMP_UP: 
      if ( ramp_current < RAMP_MAX) ramp_current += RAMP_STEP;
      else ramp_mode = RAMP_HIGH;
    break;
    case RAMP_HIGH:
      if ( ! --ramp_idx) {
        ramp_mode = RAMP_DOWN;
        ramp_idx = RAMP_CYCLE;
      }
    break;
    case RAMP_DOWN:
    default:
      if ( ramp_current > RAMP_MIN) ramp_current -= RAMP_STEP;
      else ramp_mode = RAMP_LOW;
  }
#endif

  for( byte i=0; i < 4; i++) {

    driver->devaddr( i);
    
#ifdef AUTOMOVE
    driver->VACTUAL( ramp_current);
#endif

    oled.setCursor( 20, 4+i);
    data = (driver->SG_RESULT() >> 1) & 0xff;
    if ( driver->CRCerror) {
      setupTmc2209();
      oled.print( "x");
    } else {
      byte lvl = 0;
      for ( byte i=0; i < 4; i++) {
        if ( data > 3) oled.print( '#');
        else oled.print( bar[ data]);

        data >>= 2;
      }
    }

    x = 52;
    oled.setCursor( x, 4+i);
    uint32_t flag = driver->DRV_STATUS() & 0xc01f0fff;
    if ( driver->CRCerror) {
      setupTmc2209();
      oled.print( "x");
    } else {
      //oled.print( flag, HEX);
      
      if ( flag & 0x80000000) oled.print( "!");
      else oled.print( "*");
      x += 8;

      oled.setCursor( x, 4+i);
      if ( flag & 0x40000000) {
        // stealthchop
        oled.print( "<");
        
        x += 8;
        oled.setCursor( x, 4+i);
        oled.print( "-");
      } else {
        // spreadchop
        oled.print( ">");

        x += 8;
        oled.setCursor( x, 4+i);
        oled.print( flag >> 17 & 0xf, HEX);
      }
      x += 8;

      oled.setCursor( x, 4+i);
      if ( flag & 0x00000080) oled.print( 'O');
                  else oled.print( "-");
      if ( flag & 0x00000040) oled.print( 'O');
                  else oled.print( "-");
      if ( flag & 0x00000020) oled.print( 'L');
                  else oled.print( "-");
      if ( flag & 0x00000010) oled.print( 'L');
                  else oled.print( "-");
      if ( flag & 0x00000008) oled.print( 'G');
                  else oled.print( "-");
      if ( flag & 0x00000004) oled.print( 'G');
                  else oled.print( "-");
      x += 38;

      oled.setCursor( x, 4+i);
      if ( flag & 0x00000002) oled.print( 'T');
                        else oled.print( "-");
      if ( flag & 0x00000001) oled.print( 't');
                        else oled.print( "-");
    }
  }
  delay( 250);
}

/*-----------------------------------------
* TMC settings
*/

bool setupTmc2209() {
  bool err = false;
  
  for( byte i=0; i < 4; i++) {
    driver->devaddr( i);
    err |= setTmc2209( tmc[ i]);
  }

  return err;
}

bool setTmc2209( TmcConfig* tmc) {
  driver->GCONF( tmc->gconf); 
  driver->IHOLD_IRUN( tmc->ihold_irun);
  driver->TPOWERDOWN( tmc->tpowerdown);
  driver->TPWMTHRS( tmc->tpwmthrs);
  driver->CHOPCONF( tmc->chopconf);
  driver->PWMCONF( tmc->pwmconf);
  driver->TCOOLTHRS( tmc->tcoolthrs);
  driver->COOLCONF( tmc->coolconf);

  return driver->CRCerror;
}
  
bool setupTmc2209ex() {
  // GCONF 0x00
  //  v testmode      v mstep_reg_sel      v index_optw    v spreadCycle      
  //         v multistep_flt        v index_step    v shaft       v internal_rsense
  //                         v pdn_disable                               v i_scale_ana
  // (0<<9)|(1<<8) | (1<<7)|(1<<6)|(0<<5)|(0<<4) | (0<<3)|(1<<2)|(0<<1)|(0)
  driver->GCONF( 0x1c0); 
  /*
  driver->I_scale_analog(false); // Use internal voltage reference
  driver->en_spreadCycle(true);
  driver->shaft( false);           // normal/inverse dir pin
  driver->pdn_disable(true);       // PDN controlled internally
  driver->mstep_reg_select( true); // MSTEP from register
  driver->multistep_filt( true);  // filtering off
  */
  
  // IHOLD_IRUN 
  //                  v ihold           v hold_delay
  //                        v irun
  driver->IHOLD_IRUN( 4 | (16 << 8) | ( 2UL << 16));
  /*
  driver->irun( curr_irun);               // 0-31: (irun to ihold ) * 1/64s
  driver->ihold( curr_ihold);               // 0-31: hold current to half
  driver->iholddelay(2);           // 0-15: (irun to ihold ) * 1/64s
  */
  // TPOWERDOWN
  driver->TPOWERDOWN( 20);

  // TPWMTHRS
  driver->TPWMTHRS( 80);
  
  // CHOPCONF 0x6c
  //   v dedge/dissX (3)  v mres (4)          v tbl (2) v hend (4)        v toff (4)
  //              v intpol          v vsense (1)                 v hstrt (3)   
  // ( 0<<30)|( 1<<28) | (6<<24) | (1<<17) | (2<<15) | (5<<7) | (2<<4) | (4);
  // 0x10000053
  driver->CHOPCONF( 0x169102a4);
  /*
  driver->intpol( true);           // hw interpolation to 256 microsteps
  driver->microsteps(4);           

  driver->tbl(2);                  // blank select

  driver->hend( 5);
  driver->hstrt(5);

  driver->toff(4);                 // slow decay pahse duration
  */
  // PWMCONF 0x70
  // 0xC10D0024;
  //  v pwm_lim (4)     v freewheel (2)   v autoscale        v pwm_grad(8)
  //          v pwm_reg (4)       v autograd      v pwm_freq (2)      v pwm_ofs (8)
  //(12<<28)|(8<<24) | (0<<20) | (1<<19)|(1<<18)|(1<<16) | (14<<8) | (36)
  driver->PWMCONF( 0xc80d0e24);
  /*
  driver->pwm_lim(12);
  driver->pwm_reg(8);
  
  driver->pwm_autograd( true);
  driver->pwm_autoscale(true);     // Needed for stealthChop

  driver->pwm_grad(15);
  driver->pwm_ofs(36);
  */
  // TCOOLTHRS 0x14
  // 0x00000000
  driver->TCOOLTHRS( 180);
  
  // COOLCONF 0x42
  // 0x0000;
  //  v .25 curr (1)    v semax (4)       v semin (4)
  //          v stp dwn (2)      v stp up (2)
  // (0<<15)|(1<<13) | (4<<8) | (3<<5) | (8<<0)
  driver->COOLCONF( 0x2468);
  //driver->push();
  
  return false;
}
