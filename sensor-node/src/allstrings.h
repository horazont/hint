#ifndef ALL_STRINGS_H
#define ALL_STRINGS_H

static const __flash char all_strings[] = {
    
};


#define STR_lcd_write(strname) lcd_write_textbuf_from_flash(\
    STR_ ## strname ## _FLASHBUF, \
    STR_ ## strname ## _LEN)

#endif

