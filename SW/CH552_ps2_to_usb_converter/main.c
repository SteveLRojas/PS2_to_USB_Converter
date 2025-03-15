//***************************************************
//* PS/2 to USB keyboard converter    Main file     *
//* Copyright (C) 2025 Esteban Looser-Rojas.        *
//* This program acts as an interface between a PS/2*
//* keyboard and a USB host. Designed for CH552 USB *
//* microcontrollers.                               *
//***************************************************
#include "CH552.H"
#include "CH552_RCC.h"
#include "CH552_GPIO.h"
#include "CH552_TIMER.h"
#include "CH552_PS2H_KB.h"
#include "usb_hid_keyboard.h"

//Pins:
// LED0 = P11
// KBDAT = P16
// LED2 = P17
// RXD = P30
// TXD = P31
// KBCLK = P32
// LED1 = P33
// UDP = P36
// UDM = P37

UINT8 code kb_scan_tab_base[0x84] = 
{
	0x03, 0x42, 0x03, 0x3E, 0x3C, 0x3A, 0x3B, 0x45, 0x03, 0x43, 0x41, 0x3F, 0x3D, 0x2B, 0x35, 0x03,
	0x03, 0xE2, 0xE1, 0x03, 0xE0, 0x14, 0x1E, 0x03, 0x03, 0x03, 0x1D, 0x16, 0x04, 0x1A, 0x1F, 0x03,
	0x03, 0x06, 0x1B, 0x07, 0x08, 0x21, 0x20, 0x03, 0x03, 0x2C, 0x19, 0x09, 0x17, 0x15, 0x22, 0x03,
	0x03, 0x11, 0x05, 0x0B, 0x0A, 0x1C, 0x23, 0x03, 0x03, 0x03, 0x10, 0x0D, 0x18, 0x24, 0x25, 0x03,
	0x03, 0x36, 0x0E, 0x0C, 0x12, 0x27, 0x26, 0x03, 0x03, 0x37, 0x38, 0x0F, 0x33, 0x13, 0x2D, 0x03,
	0x03, 0x03, 0x34, 0x03, 0x2F, 0x2E, 0x03, 0x03, 0x39, 0xE5, 0x28, 0x30, 0x03, 0x31, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x2A, 0x03, 0x03, 0x59, 0x03, 0x5C, 0x5F, 0x03, 0x03, 0x03,
	0x62, 0x63, 0x5A, 0x5D, 0x5E, 0x60, 0x29, 0x53, 0x44, 0x57, 0x5B, 0x56, 0x55, 0x61, 0x47, 0x03,
	0x03, 0x03, 0x03, 0x40
};

UINT8 code kb_scan_tab_ext1[31] = 
{
	0xE6, 0x00, 0x03, 0xE4, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0xE3, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0xE7, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x65
};

UINT8 code kb_scan_tab_ext2[21] = 
{
	0x4D, 0x03, 0x50, 0x4A, 0x03, 0x03, 0x03, 0x49, 0x4C, 0x51, 0x03, 0x4F, 0x52, 0x03, 0x03, 0x03,
	0x03, 0x4E, 0x03, 0x46, 0x4B
};

//HINT: Modifier keys are translated into codes 0xE0 to 0xE7, which are handled as special cases in the main function
UINT8 translate_scan_code(UINT16 scan_code)
{
	if(scan_code < 0x84)
		return kb_scan_tab_base[(UINT8)scan_code];
	else if((scan_code >> 8) == 0xE0)
	{
		if(((UINT8)scan_code >= 0x11) && ((UINT8)scan_code <= 0x2F))
			return kb_scan_tab_ext1[(UINT8)scan_code - 0x11];
		else if(((UINT8)scan_code >= 0x69) && ((UINT8)scan_code <= 0x7D))
			return kb_scan_tab_ext2[(UINT8)scan_code - 0x69];
		else if((UINT8)scan_code == 0x5A)
			return 0x58;
		else if((UINT8)scan_code == 0x4A)
			return 0x54;
		else
			return HID_KB_KEY_ERROR_UNDEFINED;
	}
	else
		return HID_KB_KEY_ERROR_UNDEFINED;
}

int main()
{
	UINT8 count;
	UINT16 pressed_keys_prev[PS2H_KB_MAX_NUM_PRESSED];
	UINT8 kb_indicators_prev;
	UINT8 kb_rollover_pressed = 0;
	UINT8 kb_pause_pressed = 0;
	UINT8 translated_key;
	
	rcc_set_clk_freq(RCC_CLK_FREQ_24M);
	gpio_set_mode(GPIO_MODE_PP, GPIO_PORT_1, GPIO_PIN_1 | GPIO_PIN_7);	//LED0, LED2
	gpio_set_mode(GPIO_MODE_OD, GPIO_PORT_1, GPIO_PIN_6);				//KBDAT
	gpio_set_mode(GPIO_MODE_PP, GPIO_PORT_3, GPIO_PIN_1 | GPIO_PIN_3);	//TXD, LED1
	gpio_set_mode(GPIO_MODE_INPUT, GPIO_PORT_3, GPIO_PIN_0);			//RXD
	gpio_set_mode(GPIO_MODE_OD, GPIO_PORT_3, GPIO_PIN_2);				//KBCLK
	
	timer_init(TIMER_0, NULL);
	timer_set_period(TIMER_0, FREQ_SYS / 1000ul);	//period is 1ms
	EA = 1;	//enable interupts
	E_DIS = 0;
	
	//Blink LED once
	gpio_clear_pin(GPIO_PORT_1, GPIO_PIN_1 | GPIO_PIN_7);
	gpio_clear_pin(GPIO_PORT_3, GPIO_PIN_3);
	timer_long_delay(TIMER_0, 250);
	gpio_set_pin(GPIO_PORT_1, GPIO_PIN_1 | GPIO_PIN_7);
	gpio_set_pin(GPIO_PORT_3, GPIO_PIN_3);
	timer_long_delay(TIMER_0, 250);
	
	ps2h_kb_init(TIMER_2);	//HINT: beyond this point the GPIO library cannot be used (some pins directly controlled by the PS/2 host library).
	for(count = 0; count < PS2H_KB_MAX_NUM_PRESSED; ++count)
	{
		pressed_keys_prev[count] = 0;
	}
	
	hid_kb_init();
	kb_indicators_prev = hid_kb_indicators;
	
	while(TRUE)
	{
		ps2h_kb_update_keys();
		
		if(ps2h_kb_status & PS2H_KB_STAT_BAT_PASSED)
		{
			ps2h_kb_set_led(0x07 & ((hid_kb_indicators << 1) | ((hid_kb_indicators >> 2) & 0x01)));
			kb_indicators_prev = hid_kb_indicators;
			ps2h_kb_status &= ~PS2H_KB_STAT_BAT_PASSED;
		}
		
		if(ps2h_kb_status & (PS2H_KB_STAT_DATA_ERROR | PS2H_KB_STAT_PROT_ERROR | PS2H_KB_STAT_TIM_ERROR))
		{
			hid_kb_release_all_keys();
			for(count = 0; count < PS2H_KB_MAX_NUM_PRESSED; ++count)
			{
				pressed_keys_prev[count] = 0;
			}
			ps2h_kb_reset();	//this clears all status bits
		}
		
		if(ps2h_kb_status & PS2H_KB_STAT_ROLLOVER)
		{
			hid_kb_release_all_keys();
			hid_kb_press_key(HID_KB_KEY_ROLL_OVER);
			kb_rollover_pressed = 1;
			ps2h_kb_status &= ~PS2H_KB_STAT_ROLLOVER;
		}
		
		if(kb_rollover_pressed && !hid_kb_report_pending)
		{
			hid_kb_release_key(HID_KB_KEY_ROLL_OVER);
			kb_rollover_pressed = 0;
		}
		
		if(ps2h_kb_status & PS2H_KB_STAT_PAUSE_KEY)
		{
			hid_kb_press_key(0x48);
			kb_pause_pressed = 1;
			ps2h_kb_status &= ~PS2H_KB_STAT_PAUSE_KEY;
		}
		
		if(kb_pause_pressed && !hid_kb_report_pending)
		{
			hid_kb_release_key(0x48);
			kb_pause_pressed = 0;
		}
		
		for(count = 0; count < PS2H_KB_MAX_NUM_PRESSED; ++count)
		{
			if(pressed_keys_prev[count] && (pressed_keys_prev[count] != ps2h_kb_pressed_keys[count]))
			{
				translated_key = translate_scan_code(pressed_keys_prev[count]);
				if((translated_key >> 4) == 0x0E)
					hid_kb_release_modifier(HID_KB_MOD_LEFT_CONTROL << (translated_key & 0x07));
				else
					hid_kb_release_key(translated_key);
			}
			
			if(ps2h_kb_pressed_keys[count] && (ps2h_kb_pressed_keys[count] != pressed_keys_prev[count]))
			{
				translated_key = translate_scan_code(ps2h_kb_pressed_keys[count]);
				if((translated_key >> 4) == 0x0E)
					hid_kb_press_modifier(HID_KB_MOD_LEFT_CONTROL << (translated_key & 0x07));
				else
					hid_kb_press_key(translated_key);
			}
			
			pressed_keys_prev[count] = ps2h_kb_pressed_keys[count];
		}
		
		if(kb_indicators_prev != hid_kb_indicators)
		{
			ps2h_kb_set_led(0x07 & ((hid_kb_indicators << 1) | ((hid_kb_indicators >> 2) & 0x01)));
			kb_indicators_prev = hid_kb_indicators;
		}
		
		//Drive LED pins directly
		T2EX = !(hid_kb_indicators & HID_KB_LED_NUM_LOCK);
		INT1 = !(hid_kb_indicators & HID_KB_LED_CAPS_LOCK);
		SCK = !(hid_kb_indicators & HID_KB_LED_SCROLL_LOCK);
	}
}
