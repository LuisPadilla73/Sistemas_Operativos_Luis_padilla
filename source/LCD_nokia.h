

#ifndef LCD_NOKIA_H_
#define LCD_NOKIA_H_

#include "stdint.h"

#define SCREENW 84
#define SCREENH 48
#define CHAR_LENGTH 5

#define LCD_X 84
#define LCD_Y 48
#define LCD_DATA 1
#define LCD_CMD 0
#define PORT_DATA_OR_CMD_PIN PORTC
#define GPIO_DATA_OR_CMD_PIN GPIOC
#define DATA_OR_CMD_PIN 5
#define RESET_PIN 7
#define PORT_RESET_PIN PORTC
#define GPIO_RESET_PIN GPIOC
#define CE 6


/*It configures the LCD*/
void LCD_nokia_init(void);
/*It writes a byte in the LCD memory. The place of writting is the last place that was indicated by LCDNokia_gotoXY. In the reset state
 * the initial place is x=0 y=0*/
void LCD_nokia_write_byte(uint8_t data_or_command, uint8_t data);
/*it clears all the figures in the LCD*/
void LCD_nokia_clear(void);
/*It is used to indicate the place for writing a new character in the LCD. The values that x can take are 0 to 84 and y can take values
 * from 0 to 5*/
void LCD_nokia_goto_xy(uint8_t x, uint8_t y);
/*It allows to write a figure represented by constant array*/
void LCD_nokia_bitmap(const uint8_t bitmap []);
/*It write a character in the LCD*/
void LCD_nokia_send_char(uint8_t);
/*It write a string into the LCD*/
void LCD_nokia_send_string(uint8_t string []);
/*It used in the initialization routine*/
void LCD_nokia_delay(void);
/*Writes a set of bytes at any FrameBuffer position*/
void LCD_nokia_write_xy_FB(uint8_t x, uint8_t y, uint8_t *ptr, uint16_t bytes);
/*Writes a String into any FrameBuffer position*/
void LCD_nokia_write_string_xy_FB(uint8_t x, uint8_t y, uint8_t *ptr, uint16_t lenght);
/*Write single character into any frame buffer position*/
void LCD_nokia_write_char_xy_FB(uint8_t x, uint8_t y, uint8_t character);
/*Sets a pixel on any of the 84x48 LCD range*/
void LCD_nokia_set_pixel(uint8_t x, uint8_t y);
/*Clears a pixel on any of the 84x48 LCD range*/
void LCD_nokia_clear_pixel(uint8_t x, uint8_t y);
/*Writes the FrameBuffer to the LCD*/
void LCD_nokia_sent_FrameBuffer();
/*Clear x number of bytes from x,y point*/
void LCD_nokia_clear_range_FrameBuffer(uint8_t x, uint8_t y, uint16_t bytes);

#endif /* LCD_NOKIA_H_ */
