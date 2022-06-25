// -----------------------------------------------------------
// VLP-2000 firmware v1.1 03.07.2019
// Tested for STC89C54RD+
// Build size: 2265 bytes
// TODO: Handle values above 3276.7 mW (signed int overflow)
// -----------------------------------------------------------

#include <reg51.h>

// Default configuation: 12MHz, 12T mode
#define FOSC 12000000L
#define T1MS (65536 - FOSC/12/1000) // 1 ms timer in 12T mode

// Calibration values
#define ADC_VREF 2.47f  // LM385
#define ADC_GAIN 2			// Set according to adc_init()
#define OPAMP_GAIN 2.3f // Adjusted by RW2
#define MW_RATIO 9.3f

// Pins configuration
#define LCD_DATA P0

sbit LCD_RS = P2^0;
sbit LCD_EN = P2^7;
sbit LCD_RW = P2^1;

sbit BUTTON = P1^0;
sbit BUZZER = P1^4; // 0 = on, 1 = off

sbit CS     = P1^3; // Not used here
sbit SCK    = P1^5;
sbit MOSI   = P1^6;
sbit MISO   = P1^7;

sbit ADC_DRDY = P3^2; // Not used here, pin is also INT0
sbit LCD_DIS1 = P3^6; // Don't know why this schematic, but either of these must be low for LCD_EN
sbit LCD_DIS2 = P3^7; // Don't know why this schematic, but either of these must be low for LCD_EN

// ------------- Delay functions -------------
void delay_ms(unsigned int ms)
{
	TR0 = 1;					// Start the timer
	do {
		TF0 = 0;		      // Clear timer flag
		TL0 = T1MS;				// Reload timer0 low byte
		TH0 = T1MS >> 8;	// Reload timer0 high byte	
		while (!TF0);			// Wait 1ms
	} while (--ms);
	TR0 = 0;					// Stop the timer
}

void delay_init(void)
{
	// AUXR &= 0x7F;	//Timer clock is 12T mode
	TMOD &= 0xF0; 		// Timer0 in 16-bit mode
	TMOD |= 0x01;
}

// Send MOSI byte, return MISO byte (CPOL = 1, CPHA = 1)
unsigned char spi_byte(unsigned char b)
{
	unsigned char r_miso = 0;
	
	// CS = 0;
	SCK = 0; MOSI = b >> 7 & 0x01; SCK = 1; delay_ms(1); r_miso |= (unsigned char)MISO << 7;
	SCK = 0; MOSI = b >> 6 & 0x01; SCK = 1; delay_ms(1); r_miso |= (unsigned char)MISO << 6;
	SCK = 0; MOSI = b >> 5 & 0x01; SCK = 1; delay_ms(1); r_miso |= (unsigned char)MISO << 5;
	SCK = 0; MOSI = b >> 4 & 0x01; SCK = 1; delay_ms(1); r_miso |= (unsigned char)MISO << 4;
	SCK = 0; MOSI = b >> 3 & 0x01; SCK = 1; delay_ms(1); r_miso |= (unsigned char)MISO << 3;
	SCK = 0; MOSI = b >> 2 & 0x01; SCK = 1; delay_ms(1); r_miso |= (unsigned char)MISO << 2;
	SCK = 0; MOSI = b >> 1 & 0x01; SCK = 1; delay_ms(1); r_miso |= (unsigned char)MISO << 1;
	SCK = 0; MOSI = b >> 0 & 0x01; SCK = 1; delay_ms(1); r_miso |= (unsigned char)MISO << 0;
	// CS = 1;
	
	return r_miso;
}

// ------------- LCD functions -------------
void lcd_cmd(char c)
{
	LCD_DATA = c;
	LCD_RS = 0;	LCD_EN = 1;	delay_ms(1); LCD_EN = 0;
}

void lcd_data(char c)
{
	LCD_DATA = c;
	LCD_RS = 1;	LCD_EN = 1;	delay_ms(1); LCD_EN = 0;
}

void lcd_line(char *p, char l)
{
	switch (l)
	{
		case 1: lcd_cmd(0x80); break;
		case 2: lcd_cmd(0xc0); break;
		case 3: lcd_cmd(0x90); break;
		case 4: lcd_cmd(0xd0); break;
		default: break;
	}
	while(*p) lcd_data(*p++);
}

void lcd_init(void)
{
	LCD_DIS1 = 0; LCD_DIS2 = 1; LCD_RW = 0;
	lcd_cmd(0x38); // Function set (8-bit interface, 2 lines, 5x7 Pixels)
	lcd_cmd(0x0c); // Make cursor invisible 
	lcd_cmd(0x01); // Clear screen 
}

// ------------- General functions -------------

// Use first 4 digits of a number as integer part,
// and last one as fraction to evade true float conversion
void int_to_str(int n, char *str)
{
	int m;
	char d;
	
	if (n < 0) { *str++ = '-'; n = -n; }
	else { *str++ = ' '; } // Alignment
	
	if (n < 10) *str++ = '0';

	m = n;	

	if (n >= 10000)	{ d = m / 10000;	m -= d * 10000;	*str++ = d + '0'; }
	if (n >= 1000)	{ d = m / 1000;		m -= d * 1000;	*str++ = d + '0'; }
	if (n >= 100)		{ d = m / 100;		m -= d * 100;		*str++ = d + '0'; }
	if (n >= 10)		{ d = m / 10;			m -= d * 10;		*str++ = d + '0'; }
	*str++ = '.'; *str++ = '0' + m; // Note: no trailing \0
}

void adc_init(void)
{
	CS = 1; SCK = 1;	// CPOL = 1, CPHA = 1
	
	spi_byte(0xff); // Reset
	spi_byte(0xff);
	spi_byte(0xff);
	spi_byte(0xff);
	
	spi_byte(0x20); // Active channel is Ain1+/Ain1-, next operation: write to the clock register
	//spi_byte(0x0c); // Master clock enabled, 4.9152 MHz divided by 2, set output rate to 50Hz
	spi_byte(0x04); // Master clock enabled, 2.4576 MHz not divided, set output rate to 50Hz
	spi_byte(0x10); // Active channel is Ain1+/Ain1-, next operation: write to the setup register
	// spi_byte(0x54); // Do self-calibration, gain = 4, unipolar mode, buffer off, start processing (FSYNC)
	spi_byte(0x4c); // Do self-calibration, gain = 2, unipolar mode, buffer off, start processing (FSYNC)
}

unsigned int adc_read(void)
{
	unsigned int adc_result;	
	spi_byte(0x38);
	adc_result  = spi_byte(0xff) << 8;
	adc_result |= spi_byte(0xff);
	return adc_result;
}

void main(void)
{
	char str[16], i;
	float adc_voltage, mv_display, mw_display, mw_unbiased, mw_bias = 0;

	delay_init(); // Must be first, as other _init functions use delay
	adc_init();
	lcd_init();
	BUTTON = 1; 	// Set pin as input
	
	if (!BUTTON)
	{
		lcd_line("    VLP-2000    ", 1);
		lcd_line("      v1.1      ", 2);
		while (!BUTTON); while (BUTTON);
		delay_ms(500);
	}
	
	lcd_line("Power   Voltage", 1);

	while (1)
	{
		adc_voltage = adc_read() * ADC_VREF / ADC_GAIN / 65.536f;
		mv_display	= adc_voltage / OPAMP_GAIN;
		mw_unbiased	=	mv_display * MW_RATIO;
		mw_display	= mw_unbiased - mw_bias;

		for (i = 0; i < 15; i++) str[i] = ' '; str[15] = 0;
		int_to_str((int)(mw_display * 10), &str);
		int_to_str((int)(mv_display * 10), &(str + 8));
		
		lcd_line(str, 2);
		
		if (!BUTTON)
		{
			BUZZER = 0;
			mw_bias = mw_unbiased;
			delay_ms(100);
			BUZZER = 1;
			while (!BUTTON);
		}
	}
}