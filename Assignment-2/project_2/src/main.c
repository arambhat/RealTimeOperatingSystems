/*
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file Sample app to demonstrate PWM.
 */

#include <zephyr.h>
#include <drivers/gpio.h>
#include <fsl_iomuxc.h>
#include <sys/printk.h>
#include <device.h>
#include <drivers/display.h>
#include <drivers/pwm.h>
#include <zephyr/types.h>
#include <drivers/spi.h>
#include <shell/shell.h>
#include <shell/shell_uart.h>

/* Defining the required macros*/

#define DEBUG 
#define DEC 10
#define HEX 16

/* Defining the required macros using the device tree information*/
#if defined(DEBUG) 
	#define DEBUG(fmt, args...) printk("DEBUG: %s():%d: " fmt, \
   		 __func__, __LINE__, ##args)
#else
 	#define DEBUG(fmt, args...) /* do nothing if not defined*/
#endif

// Setting up Red led
#define PWM_LEDR_NODE	DT_NODELABEL(r_led)

#if DT_NODE_HAS_STATUS(PWM_LEDR_NODE, okay)
#define R_CHANNEL	DT_PWMS_CHANNEL(PWM_LEDR_NODE)
#define PWMR_CTRL   DT_PWMS_CTLR(PWM_LEDR_NODE)
#else
#define R_CHANNEL
#define PWMR_CTRL
#endif

// Setting up green led 
#define PWM_LEDG_NODE	DT_NODELABEL(g_led)

#if DT_NODE_HAS_STATUS(PWM_LEDG_NODE, okay)
#define G_CHANNEL	DT_PWMS_CHANNEL(PWM_LEDG_NODE)
#define PWMG_CTRL   DT_PWMS_CTLR(PWM_LEDG_NODE)
#else
#define G_CHANNEL
#define PWMG_CTRL
#endif

//Setting up blue led
#define PWM_LEDB_NODE	DT_NODELABEL(b_led)

#if DT_NODE_HAS_STATUS(PWM_LEDG_NODE, okay)
#define B_CHANNEL	DT_PWMS_CHANNEL(PWM_LEDB_NODE)
#define PWMB_CTRL   DT_PWMS_CTLR(PWM_LEDB_NODE)	
#else
#define B_CHANNEL
#define PWMB_CTRL
#endif

/* 
 * The handler function for PWM modulation of RGB Led
 */
static int setPwmLed(const struct shell *shell, size_t argc, char **argv)
{
	int x,y,z;
	
	x=atoi(argv[1]);
	y=atoi(argv[2]);
	z=atoi(argv[3]);
	
	int ret1,ret2,ret3;
	const struct device *pwmr, *pwmg, *pwmb;

	// Setting up GPIO pins for PWM modulation.
	IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_11_FLEXPWM1_PWMB03, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_11_FLEXPWM1_PWMB03,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));

	IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_10_FLEXPWM1_PWMA03, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_10_FLEXPWM1_PWMA03,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));

	IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_03_FLEXPWM1_PWMB01, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_03_FLEXPWM1_PWMB01,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));
	
	// Get the pwm device and check if its ready
	char* rLabel=DT_PROP(PWMR_CTRL,label);
	DEBUG("Device node label: %s\n", rLabel);
	pwmr = device_get_binding(rLabel);
	if (!device_is_ready(pwmr)) {
		DEBUG("Error: PWM led %s is not ready\n", pwmr->name);
		return -1;
	}
	
	char* gLabel=DT_PROP(PWMG_CTRL,label);
	DEBUG("Device node label: %s\n", gLabel);
	pwmg = device_get_binding(gLabel);
	if (!device_is_ready(pwmg)) {
		DEBUG("Error: PWM led %s is not ready\n", pwmg->name);
		return -1;
	} 
	
	char* bLabel=DT_PROP(PWMB_CTRL,label);
	DEBUG("Device node label: %s\n", bLabel);
	pwmb = device_get_binding(bLabel);
	if (!device_is_ready(pwmb)) {
		DEBUG("Error: PWM led %s is not ready\n", pwmb->name);
		return -1;
	}
	
	// PWM frquency is set to 50 Hz.
	uint32_t period=20000;
	
	// Setting the duty cycles for each led using the user provided paramenters.
	ret1 = pwm_pin_set_usec(pwmr, R_CHANNEL, period, x*(period/100),0);
	if(ret1){
		DEBUG("Error %d: failed to set pulse width\n", ret1);
		return -1;
	}
	k_usleep(period);
	ret2 = pwm_pin_set_usec(pwmg, G_CHANNEL, period, y*(period/100), 0);
	if(ret2){
		DEBUG("Error %d: failed to set pulse width\n", ret2);
		return -1;
	}
	ret3 = pwm_pin_set_usec(pwmb, B_CHANNEL, period, z*(period/100), 0);
	if(ret3){
		DEBUG("Error %d: failed to set pulse width\n", ret3);
		return -1;
	}
	return 0;
			
}

/* 
 * The handler function for Maxim7219 dot matrix blinking function
 */
static int setLedMatrixBlink(const struct shell *shell, size_t argc, char **argv) {
  // Blinking code here, basically whatever is on will blink
	// uint16_t buffer;
	// int buff_len = sizeof(buffer);

	// struct spi_buf tx_buf_1 = { .buf = &buffer, .len = buff_len };
	// struct spi_buf_set tx_bufs_1 = { .buffers = &tx_buf_1, .count = 1 };
	if (atoi(argv[1]) == 1) {
		// Blinking logic.
	}
	return 0;
}

/* 
 * The handler function for Maxim7219 dot matrix row function
 */
static int setLedMatrixRow(const struct shell *shell, size_t argc, char **argv)
{	
	int cachedDisplayState[8][8];
	memset(cachedDisplayState, 0, sizeof(cachedDisplayState));
	struct device* dev = DEVICE_DT_GET_ANY(maxim_max7219);
	struct display_driver_api* driverApi = (struct display_driverApi*)(dev->api);
	int beginRowNum;

	// Setting the row patterns by manipulting respective bits
	for (size_t row = 1; row < argc; row++) {
		if (row == 1){
			beginRowNum = strtol(argv[1], NULL, DEC);
		} else {
			int row_val = strtol((argv[row])+2, NULL, HEX);
			for (int i=7; i>=0; i--){
				cachedDisplayState[beginRowNum+row-2][i] = row_val%2;
				row_val = row_val / 2;
			}
		}
	}
	// Setting the column patterns by manipulting respective bits
	for (int col_no=0; col_no<8; col_no++){
		uint8_t col_value = 0;
		for (int row_no=0; row_no<8; row_no++){
			col_value += (cachedDisplayState[row_no][col_no] << row_no);
		}
		uint16_t write_value = ((col_no+1) << 8) + col_value;
		driverApi->write(dev, write_value, (void*)NULL, (void*)NULL, (void*)NULL);
	}
	return 0;	
}

/*
 * Creating subcommands directory and registering the root command.
 */
SHELL_STATIC_SUBCMD_SET_CREATE(cmd_parts,
	SHELL_CMD(rgb, NULL, "Sets RGB Led PWM modulation: p2 rgb <duty_cycle1> <duty_cycle2> <duty_cycle3>", setPwmLed),
	SHELL_CMD(ledm, NULL, "Sets the led matrix with row number and data: Eg: p2 ledm 4 0x00 0xff", setLedMatrixRow),
	SHELL_CMD(ledb, NULL, "Sets the led matrix blinking: Eg: p2 ledb 1", setLedMatrixBlink),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(p2, &cmd_parts, "p2 root command", NULL);

/* Main Function */
void main(void)
{
	DEBUG("AK Main thread");
}
