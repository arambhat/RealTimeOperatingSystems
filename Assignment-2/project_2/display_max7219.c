#define DT_DRV_COMPAT maxim_max7219

#include <device.h>
#include <drivers/spi.h>
#include <drivers/gpio.h>
#include <pm/device.h>
#include <sys/byteorder.h>
#include <drivers/display.h>
#include <drivers/gpio.h>
#include <fsl_iomuxc.h>

#include <logging/log.h>
#include "display_max7219.h"

/* Defining the required data structures */
struct max7219_config {
	struct spi_dt_spec spi_user_dt_spec;
	int height;
	int width;
};

/* Blanking handler functions */
static int max7219_blanking_on(const struct device* dev){
	return -1; 
}

static int max7219_blanking_off(const struct device* dev){
	return -1; 
}

/* Display write handler */
static int max7219_write(const struct device *dev,
			 const uint16_t x,
			 const uint16_t y,
			 const struct display_buffer_descriptor *desc,
			 const void *buf){
	//fetching the spi bus controller
	struct max7219_config* config = dev->config;
	struct spi_dt_spec spi_config = config->spi_user_dt_spec;
	int len = sizeof(x);
	//setting up the buffers to send out data
	struct spi_buf tx_buf = { .buf = &x, .len = len };
	struct spi_buf_set tx_bufs = { .buffers = &tx_buf, .count = 1 };
	// transfering data through SPI
	spi_write(spi_config.bus, &spi_config.config, &tx_bufs);
	return 0;
}

void max7219_lcd_init(struct device* spi_bus, struct spi_config config){

	uint16_t buff;

	struct spi_buf tx_buf = { .buf = &buff, .len = sizeof(buff) };
	struct spi_buf_set tx_bufs = { .buffers = &tx_buf, .count = 1 };

	//setting up the buffers to send out data
	buff = MAX7219_CMD_DISPLAY_TEST;
	int ret = spi_write(spi_bus, &config, &tx_bufs);
	// To check the display functionality, we need a second to observe.
	k_msleep(1000);

	buff = MAX7219_CMD_NORMAL_MODE;
	ret = spi_write(spi_bus, &config, &tx_bufs);
	// Giving a second to get back to normal mode.
	k_msleep(1000);

	// Initialising the other registers such as intensity, shutdown, etc.
	buff = MAX7219_CMD_SCAN_LIMIT_SET_ALL;
	ret = spi_write(spi_bus, &config, &tx_bufs);

	buff = MAX7219_CMD_SET_FULL_INTENSITY;
	ret = spi_write(spi_bus, &config, &tx_bufs);

	buff = MAX7219_CMD_NORMAL_OP_SHUTDOWN_REG;
	ret = spi_write(spi_bus, &config, &tx_bufs);

	// Switch off all LEDs.
	for (uint8_t led_num = 0; led_num<9; led_num++) {
		uint8_t led_off = 0;

		buff = (led_num << 8) + led_off;
		ret = spi_write(spi_bus, &config, &tx_bufs);
	}
}

// Driver initialisation function
void max7219_init(struct device* dev){
	// Configuring the CS, CLK and DIN GPIOs for SPI transfer.
	IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_01_LPSPI1_PCS0, 0);
	IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_01_LPSPI1_PCS0,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));

	IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_02_LPSPI1_SDO, 0);
	IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_02_LPSPI1_SDO,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));

	IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_00_LPSPI1_SCK, 0);
	IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_00_LPSPI1_SCK,
			    IOMUXC_SW_PAD_CTL_PAD_PUE(1) |
			    IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
			    IOMUXC_SW_PAD_CTL_PAD_SPEED(2) |
			    IOMUXC_SW_PAD_CTL_PAD_DSE(6));
	
	// Fetching the SPI controller
	struct device* spi_bus = ((struct spi_dt_spec*)(dev->config))->bus;
	struct spi_config config = ((struct spi_dt_spec*)(dev->config))->config;
	max7219_lcd_init(spi_bus, config);
}

// Declaring the display driver API
static const struct display_driver_api max7219_api = {
	.blanking_on = max7219_blanking_on,
	.blanking_off = max7219_blanking_off,
	.write = max7219_write
};

// Intialising the config and data fields 
#define MAX7219_INIT(inst)							\
										\
	const static struct max7219_config max7219_config_ ## inst = {		\
		.spi_user_dt_spec = SPI_DT_SPEC_INST_GET(					\
			inst, SPI_OP_MODE_MASTER | SPI_WORD_SET(16) | SPI_TRANSFER_MSB, 0),		\
		.width = DT_INST_PROP(inst, width),				\
		.height = DT_INST_PROP(inst, height),				\
	};									\
										\
	DEVICE_DT_INST_DEFINE(inst, max7219_init, (void*)NULL,	\
			      (void*)NULL, &max7219_config_ ## inst,	\
			      POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY,	\
			      &max7219_api);

DT_INST_FOREACH_STATUS_OKAY(MAX7219_INIT)

