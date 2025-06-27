//***************************************************
//* PS/2 to USB mouse converter       Main file     *
//* Copyright (C) 2025 Esteban Looser-Rojas.        *
//* This program acts as an interface between a PS/2*
//* mouse and a USB host. Designed for CH552 USB    *
//* microcontrollers.                               *
//***************************************************
#include "CH552.H"
#include "CH552_RCC.h"
#include "CH552_GPIO.h"
#include "CH552_TIMER.h"
#include "CH552_PS2H_MOUSE.h"
#include "usb_hid_mouse.h"

//Pins:
// LED0 = P11
// KBDAT = P16
// LED2 = P17
// KBCLK = P32
// LED1 = P33
// UDP = P36
// UDM = P37

int main()
{
	UINT8 ms_buttons_prev;
	UINT8 ms_status_prev;
	UINT8 scrolling_requested;

	rcc_set_clk_freq(RCC_CLK_FREQ_24M);
	gpio_set_mode(GPIO_MODE_PP, GPIO_PORT_1, GPIO_PIN_1 | GPIO_PIN_7);	//LED0, LED2
	gpio_set_mode(GPIO_MODE_OD, GPIO_PORT_1, GPIO_PIN_6);	//MSDAT
	gpio_set_mode(GPIO_MODE_PP, GPIO_PORT_3, GPIO_PIN_3);	//LED1
	gpio_set_mode(GPIO_MODE_OD, GPIO_PORT_3, GPIO_PIN_2);	//MSCLK
	
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
	
	ps2h_ms_init(TIMER_2);	//HINT: beyond this point the GPIO library cannot be used (some pins directly controlled by the PS/2 host library).
	ms_buttons_prev = 0x00;
	ms_status_prev = PS2H_MS_STAT_NO_ERROR;
	scrolling_requested = 0;
	
	hid_init();
	
	while(TRUE)
	{
		ps2h_ms_update();
		
		if(ps2h_ms_status & ~ms_status_prev & PS2H_MS_STAT_DEVICE_ID)
		{
			if(scrolling_requested)
			{
				ps2h_ms_set_sample_rate(PS2H_MS_SAMPLE_RATE_200);
				while(!(ps2h_ms_status & PS2H_MS_STAT_READY));
				ps2h_ms_set_resolution(PS2H_MS_RESOLUTION_8);
				while(!(ps2h_ms_status & PS2H_MS_STAT_READY));
				ps2h_ms_enable_reporting();
			}
			else
			{
				ps2h_ms_enable_scrolling();
				scrolling_requested = 1;
			}		
		}
		
		if(ps2h_ms_status & (PS2H_MS_STAT_DATA_ERROR | PS2H_MS_STAT_PROT_ERROR | PS2H_MS_STAT_TIM_ERROR))
		{
			hid_mouse_release(HID_MOUSE_BTN_LEFT | HID_MOUSE_BTN_RIGHT | HID_MOUSE_BTN_WHEEL);
			ms_buttons_prev = 0x00;
			scrolling_requested = 0;
			ps2h_ms_reset();	//this clears all status bits
		}
		
		ms_status_prev = ps2h_ms_status;
		
		hid_mouse_press(ps2h_ms_buttons & 0x07);
		hid_mouse_release(~ps2h_ms_buttons & 0x07);
		hid_mouse_move(ps2h_ms_x_rel, -ps2h_ms_y_rel);
		hid_mouse_scroll(ps2h_ms_z_rel);
		ps2h_ms_x_rel = 0;
		ps2h_ms_y_rel = 0;
		ps2h_ms_z_rel = 0;
		
		//Drive LED pins directly
		T2EX = !(ps2h_ms_buttons & PS2H_MS_BTNS_BTN_LEFT);
		INT1 = !(ps2h_ms_buttons & PS2H_MS_BTNS_BTN_CENTER);
		SCK = !(ps2h_ms_buttons & PS2H_MS_BTNS_BTN_RIGHT);
	}
}
