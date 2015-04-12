/*
  This is a single header ini library using a design similar to the stb
  libraries. See https://github.com/nothings/stb for details.
  
  Do this:
     #define CE_INI_IMPLEMENTATION
  before you include this file in *one* C or C++ file to create the implementation.
  
  // i.e. it should look like this:
  #include ...
  #include ...
  #include ...
  #define CE_INI_IMPLEMENTATION
  #include "ini.h"
*/


/*============================================================================
 * HEADER
 *===========================================================================*/

#ifndef CE_INI_H
#define CE_INI_H

#define CE_INI_MAX_SECTION_LENGTH 32
#define CE_INI_MAX_NAME_LENGTH    32
#define CE_INI_MAX_VALUE_LENGTH   64

#define CE_INI_MAX_WRITE_OPTIONS 256

#define CE_INI_OK    0
#define CE_INI_ERROR 1


/*----------------------------------------------------------------------------
 * API
 *---------------------------------------------------------------------------*/
#ifdef __cplusplus
extern "C" {
#endif

typedef void (*INIReadCallback)(const char *section, const char *name, const char *value, void *userdata);
typedef void (*INIWriteCallback)(int index, char section[CE_INI_MAX_SECTION_LENGTH], char name[CE_INI_MAX_NAME_LENGTH], char value[CE_INI_MAX_VALUE_LENGTH], void *userdata);

int CE_INI_Read(const char *text, INIReadCallback callback, void *userdata);
int CE_INI_Write(char *buffer, int max_length, int option_count, INIWriteCallback callback, void *userdata);

#ifdef __cplusplus /* extern "C" */
}
#endif


#endif /* CE_INI_H */


/*============================================================================
 * IMPLEMENTATION
 *===========================================================================*/
#ifdef CE_INI_IMPLEMENTATION

#include <string.h>
#include <ctype.h>

#ifndef CE_INI_ASSERT
#include <assert.h>
#define CE_INI_ASSERT(x) assert(x)
#endif

#ifndef CE_INI_NO_PRINT
#include <stdio.h>
#endif


/*----------------------------------------------------------------------------
 * Errors
 *---------------------------------------------------------------------------*/

static const char* err(const char *msg)
{
#ifndef CE_INI_NO_PRINT
    printf("%s\n", msg);
#endif
    return NULL;
}

static int err_i(const char *msg, int result)
{
#ifndef CE_INI_NO_PRINT
    printf("%s\n", msg);
#endif
    return result;
}

/*----------------------------------------------------------------------------
 * String Skipping
 *---------------------------------------------------------------------------*/

static const char* skipWhitespace(const char *str)
{
    while(*str && *str != '\n' && (iscntrl(*str) || *str == ' '))
        str++;
    return str;
}

static const char* skipToFirstReadableChar(const char *str)
{
    while(*str && *str && (iscntrl(*str) || *str == ' '))
        str++;
    return str;
}

static const char* nextLine(const char *str)
{
    while(*str && *str != '\n')
        str++;

    while(*str && *str == '\n')
        str++;

    return str;
}

static const char* skipEquality(const char *str)
{
    str = skipWhitespace(str);
    if(*str != '=')
        return err("equality not found");
    return skipWhitespace(++str);
}

/*----------------------------------------------------------------------------
 * Section Parsing
 *---------------------------------------------------------------------------*/

static const char* parseSection(const char *str, char out[CE_INI_MAX_SECTION_LENGTH])
{
    int n = 0;
    memset(out, '\0', CE_INI_MAX_SECTION_LENGTH);

    if(!(*str && *(str++) == '['))
        return err("start of section not found");

    while(*str && *str != ']')
    {
        if(!(isalnum(*str) || *str == '-' || *str == '_' || *str == ' '))
            return err("invalid character in section");

        if(!(n < CE_INI_MAX_SECTION_LENGTH - 1))
            return err("section too long");

        out[n++] = *(str++);
    }

    if(!(*str && *(str++) == ']'))
        return err("end of section not found");

    return str;
}

/*----------------------------------------------------------------------------
 * Name Parsing
 *---------------------------------------------------------------------------*/

static const char* parseName(const char *str, char out[CE_INI_MAX_NAME_LENGTH])
{
    int n = 0;
    memset(out, '\0', CE_INI_MAX_NAME_LENGTH);

    while(*str && *str != ' ' && *str != '=')
    {
        if(!(isalnum(*str) || *str == '.' || *str == '-' || *str == '_'))
            return err("invalid character in name");

        if(!(n < CE_INI_MAX_NAME_LENGTH - 1))
            return err("name too long");
            
        out[n++] = *(str++);
    }

    if(n == 0)
        return err("name too short");

    return str;
}

/*----------------------------------------------------------------------------
 * Value Parsing
 *---------------------------------------------------------------------------*/

static const char* parseUnquotedValue(const char *str, char out[CE_INI_MAX_VALUE_LENGTH])
{
    int n = 0;
    memset(out, '\0', CE_INI_MAX_VALUE_LENGTH);

    while(*str && (*str != '\n' && *str != '\r') && *str != ';')
    {
        if(!(isprint(*str) || *str == '\t'))
            return err("invalid character in value");

        if(!(n < CE_INI_MAX_VALUE_LENGTH - 1))
            return err("value too long or too many trailing spaces");

        out[n++] = *(str++);
    }

    while(n > 0 && out[n - 1] == ' ')
        out[--n] = '\0';

    return str;
}

static const char* parseQuotedValue(const char *str, char out[CE_INI_MAX_VALUE_LENGTH])
{
    int n = 0;
    memset(out, '\0', CE_INI_MAX_VALUE_LENGTH);

    if(*str && *(str++) != '"')
        return err("starting quote not found");

    while(*str && (*str != '\n' && *str != '\r') && *str != ';' && *str != '"')
    {
        char c = '\0';

        if(*str == '\\')
        {
            str++;

            switch(*(str++))
            {
                case '"':  c = '"'; c = '"';  break;
                case '\\': c = '"'; c = '\\'; break;
                case 't':  c = '"'; c = '\t'; break;
                case 'n':  c = '"'; c = '\n'; break;
                default: return err("invalid escape sequence"); break;
            };
        }
        else
        {
            if(!(isprint(*str) || *str == '\t'))
                return err("invalid character in value");
            c = *(str++);
        }

        if(!(n < CE_INI_MAX_VALUE_LENGTH - 1))
            return err("value too long or too many trailing spaces");

        out[n++] = c;
    }

    if(*str && *(str++) != '"')
        return err("ending quote not found");

    return str;
}

static const char* parseValue(const char *str, char out[CE_INI_MAX_VALUE_LENGTH])
{
    return (*str == '\"') ? parseQuotedValue(str, out) : parseUnquotedValue(str, out);
}

/*----------------------------------------------------------------------------
 * INI Parsing
 *---------------------------------------------------------------------------*/

int CE_INI_Read(const char *text, INIReadCallback callback, void *userdata)
{
    CE_INI_ASSERT(callback != NULL);

    char section[CE_INI_MAX_SECTION_LENGTH];
    char name[CE_INI_MAX_NAME_LENGTH];
    char value[CE_INI_MAX_VALUE_LENGTH];
    const char *str = text;

    while(str != NULL && *str)
    {
        if(!(str = skipToFirstReadableChar(str)))
            return CE_INI_ERROR;

        if(*str == '[')
        {
            if(!(str = parseSection(str, section)))
                return CE_INI_ERROR;
        }
        else if(*str == ';')
        {
            if(!(str = nextLine(str)))
                return CE_INI_ERROR;
        }
        else if(*str)
        {
            if(!(str = parseName(str, name)))
                return CE_INI_ERROR;

            if(!(str = skipEquality(str)))
                return CE_INI_ERROR;

            if(!(str = parseValue(str, value)))
                return CE_INI_ERROR;

            (*callback)(section, name, value, userdata);
        }
    }

    return CE_INI_OK;
}

/*----------------------------------------------------------------------------
 * INI Writing
 *---------------------------------------------------------------------------*/

static int  getWritten(int index, char *written) { return (written[index / 8] >> (index % 8)) & 0x1;   }
static void setWritten(int index, char *written) {         written[index / 8] |= (0x1 << (index % 8)); }

static int bufferPrint(char *buffer, int length, int *bytes_written, const char *str)
{
    int index = *bytes_written;

    while(*str)
    {
        if(index < length - 1)
            buffer[index++] = *(str++);
        else
            return err_i("write buffer full\n", CE_INI_ERROR);
    }

    buffer[index + 1] = '\0';
    *bytes_written = index;

    return CE_INI_OK;
}

int CE_INI_Write(char *buffer, int max_length, int option_count, INIWriteCallback callback, void *userdata)
{
    char write_section[CE_INI_MAX_SECTION_LENGTH];
    char section[CE_INI_MAX_SECTION_LENGTH];
    char name[CE_INI_MAX_NAME_LENGTH];
    char value[CE_INI_MAX_VALUE_LENGTH];
    char written[CE_INI_MAX_WRITE_OPTIONS / 8] = {0};
    int  bytes_written = 0;
    int  num_written = 0;

    CE_INI_ASSERT(callback != NULL);

    if(option_count > CE_INI_MAX_WRITE_OPTIONS)
        return err_i("too many write options", CE_INI_ERROR);

    do
    {
        num_written = 0;
        int section_found = 0;

        for(int i = 0; i < option_count && !section_found; i++)
        {
            if(getWritten(i, written) == 0)
            {
                (*callback)(i, write_section, name, value, userdata);
                section_found = 1;
            }
        }

        if(section_found)
        {
            if(bufferPrint(buffer, max_length, &bytes_written, "[")           == CE_INI_ERROR ||
               bufferPrint(buffer, max_length, &bytes_written, write_section) == CE_INI_ERROR ||
               bufferPrint(buffer, max_length, &bytes_written, "]\n")         == CE_INI_ERROR)
            {
                return err_i("failed to write section\n", CE_INI_ERROR);
            }
        }

        for(int i = 0; i < option_count; i++)
        {
            if(getWritten(i, written) == 1)
                continue;

            (*callback)(i, section, name, value, userdata);

            if(strcmp(section, write_section) != 0)
                continue;

            if(bufferPrint(buffer, max_length, &bytes_written, name)    == CE_INI_ERROR ||
               bufferPrint(buffer, max_length, &bytes_written, "=")     == CE_INI_ERROR ||
               bufferPrint(buffer, max_length, &bytes_written, value)   == CE_INI_ERROR ||
               bufferPrint(buffer, max_length, &bytes_written, "\n")    == CE_INI_ERROR)
            {
                return err_i("failed to write name value pair\n", CE_INI_ERROR);
            }

            setWritten(i, written);
            num_written++;
        }

        if(bufferPrint(buffer, max_length, &bytes_written, "\n") == CE_INI_ERROR)
            return err_i("failed to write\n", CE_INI_ERROR);
    }
    while(num_written > 0);

    return CE_INI_OK;
}

#endif /* CE_INI_IMPLEMENTATION */

