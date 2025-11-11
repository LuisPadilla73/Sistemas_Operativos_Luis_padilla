/*
 * nokia_draw.c
 *
 *  Created on: Mar 19, 2025
 *      Author: nxg04072
 */

#include "nokia_draw.h"
#include "LCD_nokia.h"


uint8_t drawline(float x0, float y0, float x1, float y1,uint8_t mindots){
    float m;
    float b;
    float xstep;
    float xout;
    float yout;
    error_code return_code = pass_code;
    uint32_t dotscount;

    x0>PIXEL_X_MAX_LIMIT ? x0=PIXEL_X_MAX_LIMIT, return_code=out_of_bounds_error : x0;
    x0<PIXEL_X_MIN_LIMIT ? x0=PIXEL_X_MIN_LIMIT, return_code=out_of_bounds_error : x0;
    y0>PIXEL_Y_MAX_LIMIT ? y0=PIXEL_Y_MAX_LIMIT, return_code=out_of_bounds_error : y0;
    y0<PIXEL_Y_MIN_LIMIT ? y0=PIXEL_Y_MIN_LIMIT, return_code=out_of_bounds_error : y0;
    x1>PIXEL_X_MAX_LIMIT ? x1=PIXEL_X_MAX_LIMIT, return_code=out_of_bounds_error : x1;
    x1<PIXEL_X_MIN_LIMIT ? x1=PIXEL_X_MIN_LIMIT, return_code=out_of_bounds_error : x1;
    y1>PIXEL_Y_MAX_LIMIT ? y1=PIXEL_Y_MAX_LIMIT, return_code=out_of_bounds_error : y1;
    y1<PIXEL_Y_MIN_LIMIT ? y1=PIXEL_Y_MIN_LIMIT, return_code=out_of_bounds_error : y1;

    if((x1-x0)==0){
        return zero_division_error;
    }

    xout = x0;
    xstep = (x1-x0)/mindots;
    m = (y1-y0)/(x1-x0);
    b = y1 - (m*x1);

    for(dotscount=0;dotscount<mindots;dotscount++){
        if(xout<=x1){
            yout = ((m * xout) + b);
            xout += xstep;
            LCD_nokia_set_pixel((uint8_t)xout,(uint8_t)yout);
        }
    }
    return return_code;
}
