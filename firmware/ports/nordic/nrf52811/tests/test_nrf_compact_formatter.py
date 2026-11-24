"""Preserve all compact formatter conversions, bounded writes and return values."""
import os
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
ORACLE=r'''
static void prior_format_char(char* buffer, size_t size, size_t* length, char value) {
    if (*length + 1 < size) buffer[(*length)++] = value;
}

static void prior_format_string(char* buffer, size_t size, size_t* length, const char* value) {
    if (value == NULL) value = "(null)";
    while (*value != '\0') prior_format_char(buffer, size, length, *value++);
}

static void prior_format_unsigned(char* buffer, size_t size, size_t* length, uint32_t value, uint8_t base,
                            bool upper, uint8_t width, char padding, bool negative) {
    char digits[10];
    uint8_t count = 0;
    do {
        uint8_t digit = (uint8_t)(value % base);
        digits[count++] = (char)(digit < 10 ? '0' + digit : (upper ? 'A' : 'a') + digit - 10);
        value /= base;
    } while (value != 0 && count < sizeof(digits));

    uint8_t printed = (uint8_t)(count + (negative ? 1 : 0));
    if (padding == ' ') {
        while (printed < width) {
            prior_format_char(buffer, size, length, ' ');
            printed++;
        }
    }
    if (negative) prior_format_char(buffer, size, length, '-');
    if (padding == '0') {
        while (printed < width) {
            prior_format_char(buffer, size, length, '0');
            printed++;
        }
    }
    while (count > 0) prior_format_char(buffer, size, length, digits[--count]);
}

static size_t prior_GFX_vformat(char* buffer, size_t size, const char* format, va_list args) {
    size_t length = 0;
    if (buffer == NULL || size == 0 || format == NULL) return 0;

    while (*format != '\0' && length + 1 < size) {
        if (*format != '%') {
            prior_format_char(buffer, size, &length, *format++);
            continue;
        }
        format++;
        if (*format == '%') {
            prior_format_char(buffer, size, &length, *format++);
            continue;
        }

        char padding = ' ';
        uint8_t width = 0;
        if (*format == '0') {
            padding = '0';
            format++;
        }
        while (*format >= '0' && *format <= '9') {
            uint16_t next = (uint16_t)width * 10u + (uint8_t)(*format++ - '0');
            width = next > UINT8_MAX ? UINT8_MAX : (uint8_t)next;
        }
        if (*format == 'l') format++;

        switch (*format) {
            case 's':
                prior_format_string(buffer, size, &length, va_arg(args, const char*));
                break;
            case 'c':
                prior_format_char(buffer, size, &length, (char)va_arg(args, int));
                break;
            case 'd':
            case 'i': {
                int32_t value = va_arg(args, int32_t);
                bool negative = value < 0;
                uint32_t magnitude = negative ? 0u - (uint32_t)value : (uint32_t)value;
                prior_format_unsigned(buffer, size, &length, magnitude, 10, false, width, padding, negative);
                break;
            }
            case 'u':
                prior_format_unsigned(buffer, size, &length, va_arg(args, uint32_t), 10, false, width, padding, false);
                break;
            case 'x':
            case 'X': {
                bool upper = *format == 'X';
                prior_format_unsigned(buffer, size, &length, va_arg(args, uint32_t), 16, upper, width, padding, false);
                break;
            }
            case '\0':
                prior_format_char(buffer, size, &length, '%');
                continue;
            default:
                prior_format_char(buffer, size, &length, '%');
                prior_format_char(buffer, size, &length, *format);
                break;
        }
        format++;
    }
    buffer[length] = '\0';
    return length;
}

size_t prior_GFX_format(char* buffer, size_t size, const char* format, ...) {
    va_list args;
    va_start(args, format);
    size_t length = prior_GFX_vformat(buffer, size, format, args);
    va_end(args);
    return length;
}

'''
source=(ROOT/'compat/common/gui/Adafruit_GFX.c').read_text(encoding='utf-8')
a=source.index('static void format_char(');b=source.index('\nsize_t GFX_printf(',a)
DRIVER=r'''
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
'''+ORACLE+'\n'+source[a:b]+r'''
static char a[300],b[300];
#define COMPARE(...) do { memset(a,0xa5,sizeof(a));memset(b,0xa5,sizeof(b)); size_t al=GFX_format(a,size,__VA_ARGS__),bl=prior_GFX_format(b,size,__VA_ARGS__); assert(al==bl && !memcmp(a,b,sizeof(a))); } while(0)
static uint32_t random_state=0xf07aab61u;
static uint32_t random_value(void) { random_state=random_state*1664525u+1013904223u;return random_state; }
int main(void) {
 const char *formats[]={"%d","%i","%u","%x","%X","%02d","%04u","%08x","%12X","%0256u","%999999999u","%ld","%lu","%lX","%%%u%%","prefix %u suffix","%0u","%1u","%1d","%010d"};
 const uint32_t values[]={0,1,9,10,255,256,65535,0x7fffffff,0x80000000,0xffffffff};
 unsigned long count=0;
 for(size_t size=0;size<=sizeof(a);size++) {
  for(unsigned f=0;f<sizeof(formats)/sizeof(formats[0]);f++)
   for(unsigned v=0;v<sizeof(values)/sizeof(values[0])+20;v++) {
    uint32_t value=v<10?values[v]:random_value();
    if(strchr(formats[f],'d') || strchr(formats[f],'i')) { COMPARE(formats[f],(int32_t)value); }
    else { COMPARE(formats[f],value); }
    count++;
   }
  COMPARE("%s %c %u %d %x %X %%", "中文标签",'A',(uint32_t)456,(int32_t)-789,(uint32_t)0xabcd,(uint32_t)0xabcd);
  COMPARE("%s / %s",NULL,"text");COMPARE("%","unused");COMPARE("%l");COMPARE("%000");COMPARE("%q %s","kept");COMPARE("plain 中文");COMPARE("");COMPARE(NULL);
 }
 assert(GFX_format(NULL,0,"%u",(uint32_t)1)==0);
 assert(GFX_format(NULL,3,"%u",(uint32_t)1)==0);
 printf("PASS %lu formatter numeric cases plus null/string/char/unknown/truncated formats and all output capacities; bytes/return/canaries match cfff3e4\n",count);
}
'''
with tempfile.TemporaryDirectory() as temp:
 out=Path(temp);(out/'driver.c').write_text(DRIVER,encoding='utf-8')
 flags=['-O2'] if not os.environ.get('INKTAG_TEST_SANITIZERS') else ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-sanitize-recover=all','-no-pie']
 subprocess.run([os.environ.get('CC','cc'),'-std=c99',*flags,'-Wall','-Werror',str(out/'driver.c'),'-o',str(out/'test')],check=True)
 subprocess.run([str(out/'test')],check=True)
