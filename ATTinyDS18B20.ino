//Simple standalone Temp display
//R.J.Tidey 31 July 2021
//Note text co-ordinates are by pixel in x direction and by 8 pixels in Y direction
#include <Arduino.h>
#include <OneWire.h>
#include <avr/sleep.h>``
#include <avr/power.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>
#include <avr/boot.h>
#include <ssd1306BB.h>

// adjust  to calibrate ds18b20
//added to reading in units of 1/16 degree
#define TEMP_ADJUST -24
// default PIN configuration is as below
// VCC ---- vcc
// GND ---- gnd
// SCL ---- pb2
// SDA ---- pb1
// ONEWIRE  pb0
// LDR ---- pb5 

#define ADC_CHAN 0		//PB5
#define ADCSRA_INIT 0x04  // DISABLED | 16 prescale 
#define ADCSRA_STARTSINGLE 0xc4 //16 prescale 
#define ADCSRA_CLEAR ADCSRA_INIT | (1 << ADIF) //16 prescale 
#define ADCSRB_INIT 0x00
#define ADMUX_INIT 0x20 | ADC_CHAN; // ADLAR Vcc Reference
#define LIGHT_THRESHOLD 20

//watchdog set
//#define WDTCR_ENABLE 1<<WDIE | 1<<WDE | 1<<WDP1 | 1<<WDP0 //128mS
#define WDTCR_ENABLE 1<<WDIE | 1<<WDE | 1<<WDP2 //256mS

#define SSD1306_SCL		PB2
#define SSD1306_SDA		PB1
#define SSDI2C_DELAY	4		// sets i2c speed
#define SSD1306_SA		0X3C	// Slave address
#define BLANK "                "
#define CELSIUS "c"
#define ONEWIRE_BUSS PB0

OneWire TemperatureSensor(ONEWIRE_BUSS);

int16_t temp;
uint8_t saveADCSRA;

ISR(WDT_vect) {
	MCUSR = 0x00; // Clear WDRF in MCUSR/
	WDTCR |= (1<<WDCE) | (1<<WDE); // Write logical one to WDCE and WDE/
	WDTCR = 0x00; // Turn off WDT
}

uint8_t getAnalog() {
	//start conversion
	ADCSRA = ADCSRA_STARTSINGLE;
	//wait till complete
	while((ADCSRA & (1 << ADIF)) == 0) {
	}
	//reset complete flag
	ADCSRA = ADCSRA_CLEAR;
	return ADCH;
}

void sleepTillLight() {
	//put display sleep on
	if(getAnalog() > LIGHT_THRESHOLD) {
		SSD1306.ssd1306_sleep(1);
		while(getAnalog() > LIGHT_THRESHOLD) {
			saveADCSRA = ADCSRA;
			ADCSRA = 0;
			power_all_disable();
			noInterrupts();
			sleep_bod_disable();
			WDTCR = WDTCR_ENABLE;
			set_sleep_mode(SLEEP_MODE_PWR_DOWN);
			sleep_enable();
			interrupts();
			sleep_mode();
			//resume here after watchdog wakes up from sleep
			sleep_disable();
			power_all_enable();
			ADCSRA = saveADCSRA;
		}
		//wake up display
		SSD1306.ssd1306_sleep(0);
	}
}

void displayInit() {
	delay(1000);
	SSD1306.ssd1306_init(SSD1306_SDA, SSD1306_SCL, SSD1306_SA, SSDI2C_DELAY);
	SSD1306.ssd1306_setscale(1);
	delay(500);
	SSD1306.ssd1306_fillscreen(0);
	SSD1306.ssd1306_string(0,6,(char*)BLANK);
	SSD1306.ssd1306_string(96,0,(char*)CELSIUS);
	SSD1306.ssd1306_setscale(2);
}

void checkResolution() {
	//check resolution of ds18b20 is 9 bits
	// if not then set it as default EEPROM config
	uint8_t scratchpad[5];
	uint8_t i;
	
	TemperatureSensor.reset(); // reset one wire buss
	TemperatureSensor.skip(); // select only device
	TemperatureSensor.write(0xBE); // read scratchpad
	delay(10);
	for(i=0;i<5;i++) {
		scratchpad[i] = TemperatureSensor.read();
	}
	TemperatureSensor.reset(); // reset one wire buss
	if(scratchpad[4] != 0x1f) {
		SSD1306.ssd1306_setscale(1);
		SSD1306.ssd1306_string(0,6,(char*)"Set Res 9");
		//not 9 bits so set it
		TemperatureSensor.skip(); // select only device
		scratchpad[0] = 0x7f; //TH
		scratchpad[1] = 0x80; //TL
		scratchpad[2] = 0x1f; //CONFIG
		TemperatureSensor.write(0x4E); // write scratchpad
		for(i=0;i<3;i++) {
			TemperatureSensor.write(scratchpad[i]);
		}
		delay(10);
		TemperatureSensor.reset(); // reset one wire buss
		TemperatureSensor.skip(); // select only device
		TemperatureSensor.write(0x48); // copy scratchpad to EEPROM
		delay(10);
		TemperatureSensor.reset(); // reset one wire bus
		delay(1000);
		SSD1306.ssd1306_string(0,6,(char*)BLANK);
		SSD1306.ssd1306_setscale(2);
	}
}

static int16_t readTemp(void) {
	int16_t t = 800; // 50C;
	uint8_t retry = 0;

	while((t > 799 || t < -480) && retry < 5) {
		TemperatureSensor.reset(); // reset one wire buss
		TemperatureSensor.skip(); // select only device
		TemperatureSensor.write(0x44); // start conversion
		delay(200); // wait for the conversion
		TemperatureSensor.reset();
		TemperatureSensor.skip();
		TemperatureSensor.write(0xBE); // Read Scratchpad
		t = TemperatureSensor.read();
		t |= TemperatureSensor.read() << 8;
		TemperatureSensor.reset();
		retry++;
	}
	return t;
}

void displayValue() {
	int16_t v = temp + TEMP_ADJUST;
	char cTemp[4] = {32,32,32,0};
	char *p = cTemp;
	if(v & 0x8008) {
		v -= 16;
	} else if(v & 0x0008) {
		v += 16;
	}
	//nearest integer value ensuring sign bits maintained
	v >>= 4;
  if(v & 0x800) {
    v |= 0xf000;
  }

	if(v >= 10) {
		itoa(v, p + 1, 10);
	} else if(v <= -10) {
		itoa(v, p, 10);
	} else if(v >= 0) {
		itoa(v, p + 2, 10);
	} else {
		itoa(v, p + 1, 10);
	}
	SSD1306.ssd1306_string(0,0,cTemp);
}

void setup() {
	// get clock set up from low fuse bits
	uint8_t clkSel = boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS) & 0x0f;
	cli(); // Disable interrupts
	if(clkSel == 1) {
		//pll operation 16MHz so scale down to 8MHz
		CLKPR = (1<<CLKPCE); // Prescaler enable
		CLKPR = 1; // Clock division factor 2 8MHz
	}
	TCCR1 = (TCCR1 & 0xF0) | 6; // timer1 prescale 32 for 4uS ticks
	sei(); // Enable interrupts	int i;
	//initialise for adc reading
	ADCSRA = ADCSRA_INIT;
	ADCSRB = ADCSRB_INIT;
	ADMUX = ADMUX_INIT;
	displayInit();
	checkResolution();
}

void loop() {
	temp = readTemp();
	displayValue();
	sleepTillLight();
	delay(1000);
}