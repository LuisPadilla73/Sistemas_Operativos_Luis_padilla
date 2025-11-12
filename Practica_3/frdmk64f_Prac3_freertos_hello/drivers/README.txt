1. Copiar los siguientes archivos en el folder "source" de tu proyecto:
	- LCD_nokia_images.c
	- LCD_nokia_images.h
	- LCD_nokia_draw.c
	- LCD_nokia_draw.h
	- LCD_nokia.c
	- LCD_nokia.h
	- SPI.c
	- SPI.h
   
2. Copiar los drivers de SPI del SDK en el folder "drivers" de tu proyecto:
	- fsl_dspi.c 
	- fsl_dspi.h
   
3. Incluir los siguientes headers en tu proyecto:
	- #include "LCD_nokia.h"
	- #include "SPI.h"
	- #include "nokia_draw.h"
   
4. El driver define los siguientes pines y conexiones:
	- PORTD0 -> SPI_CS se conecta a CE de la pantalla
	- PORTD1 -> SPI_SCLK se conecta a CLK de la pantalla 
	- PORTD2 -> SPI_SDOUT se conecta a DIN de la pantalla
	- PORTD3 -> SPI_SDIN no se conecta (la pantalla no envía datos).
	- PORTC5 -> "Data/Cmd" se conecta a DC de la pantalla
	- PORTC7 -> "Reset" se conecta a RST de la pantalla
	- VCC en la pantalla se conecta a 3.3v
	- LIGHT en la pantalla se conecta a GND si se quiere encender el backlight al máximo.
   
5. APIs:
	- Inicialización. Se deben de llamar en ésta secuencia para inicializar la pantalla
		- SPI_config()
		- LCD_nokia_init()

	- Escritura en la pantalla:
		- LCD_nokia_clear() -> Limpia la pantalla completa
		- LCD_nokia_write_string_xy_FB(x,y,ptr_to_string,lenght in bytes) -> Imprime una cadena de caracteres a partir de la posición dada.
		                                                                     Rango de X = 0 a 84 (cada caracter utiliza 5 pixeles
															                 Rango de Y = 0 a 5 (La pantalla tiene 6 bancos en el eje vertical)

		- LCD_nokia_write_char_xy_FB(x,y,char) -> Imprime un caracter a partir de la posición indicada
		                                          Rango de X = 0 a 84 (cada caracter utiliza 5 pixeles
												  Rango de Y = 0 a 5 (La pantalla tiene 6 bancos en el eje vertical)
												  
		- LCD_nokia_bitmap(ptr_to_image_table) -> Imprime una tabla(imagen) de 48x84 pixeles
		
		- LCD_nokia_clear_range_FrameBuffer(x,y,bytes) -> Limpia una cantidad específica de bytes a partir de una posición inicial en la pantalla
		                                                  Rango de X = 0 a 84 (cada caracter utiliza 5 pixeles
												          Rango de Y = 0 a 5 (La pantalla tiene 6 bancos en el eje vertical)
							  
		- drawline(x0,y0,x1,y1,minidots) -> Dibuja una linea en el rango de la pantalla desde (x0,y0) hasta (x1,y1)
		                                    - Rango de x0 y x1 = 0 a 84
											- Rango de y0 y y1 = 0 a 48
											- minidots representa la cantidad de puntos intermedios a dibujar, mientras más puntos
											  se especifiquen, la línea se dibujará mejor pero tomará más tiempo en imprimirse
											  
		- LCD_nokia_sent_FrameBuffer() -> Envía el valor actual del FrameBuffer para ser impreso en la pantalla.
                                          TODAS LAS APIS DESCRITAS ANTERIORMENTE (a excepción de LCD_nokia_bitmap) DEBEN LLAMAR
                                          ESTA FUNCION PARA ESCRIBIR EN PANTALLA										  
		                                                  