/*
 * Copyright (c) 2020 Kim Bøndergaard <kim@fam-boendergaard.dk>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef MAX7219_DISPLAY_DRIVER_H__
#define MAX7219_DISPLAY_DRIVER_H__

#include <zephyr.h>

#define MAX7219_CMD_DISPLAY_TEST 0x0F01
#define MAX7219_CMD_NORMAL_MODE 0x0F00
#define MAX7219_CMD_SCAN_LIMIT_SET_ALL 0x0BFF
#define MAX7219_CMD_SET_FULL_INTENSITY 0x0AFF
#define MAX7219_CMD_NORMAL_OP_SHUTDOWN_REG 0x0CFF


#endif  /* MAX7219_DISPLAY_DRIVER_H__ */
