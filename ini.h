/*
  This is a single header ini library using a design similar to the stb
  libraries. See https://github.com/nothings/stb for details.
  
  Do this:
     #define INI_IMPLEMENTATION
  before you include this file in *one* C or C++ file to create the implementation.
  
  // i.e. it should look like this:
  #include ...
  #include ...
  #include ...
  #define INI_IMPLEMENTATION
  #include "ini.h"
*/


/*============================================================================
 * HEADER
 *===========================================================================*/

#ifndef INI_H
#define INI_H

#define INI_MAX_SECTION_LENGTH 32
#define INI_MAX_KEY_LENGTH     32
#define INI_MAX_VALUE_LENGTH   64

#define INI_OK    0
#define INI_ERROR 1


/*----------------------------------------------------------------------------
 * API
 *---------------------------------------------------------------------------*/
#ifdef __cplusplus
extern "C" {
#endif

typedef void (*INIParseCallback)(const char *section, const char *key, const char *value, void *data);

int INI_Parse(const char *text, INIParseCallback callback, void *callback_data);

#ifdef __cplusplus /* extern "C" */
}
#endif


#endif /* INI_H */


/*============================================================================
 * IMPLEMENTATION
 *===========================================================================*/
#ifdef INI_IMPLEMENTATION

#include <string.h>
#include <ctype.h>

#ifndef INI_ASSERT
#include <assert.h>
#define INI_ASSERT(x) assert(x)
#endif

#ifndef INI_NO_PRINT
#include <stdio.h>
#endif

static const char* err(const char *msg)
{
#ifndef INI_NO_PRINT
    printf("%s\n", msg);
#endif
    return NULL;
}

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

static const char* parseSection(const char *str, char out[INI_MAX_SECTION_LENGTH])
{
    int n = 0;
    memset(out, '\0', INI_MAX_SECTION_LENGTH);

    if(!(*str && *(str++) == '['))
        return err("start of section not found");

    while(*str && *str != ']')
    {
        if(!(isalnum(*str) || *str == '-' || *str == '_' || *str == ' '))
            return err("invalid character in section");

        if(!(n < INI_MAX_SECTION_LENGTH - 1))
            return err("section too long");

        out[n++] = *(str++);
    }

    if(!(*str && *(str++) == ']'))
        return err("end of section not found");

    return str;
}

/*----------------------------------------------------------------------------
 * Key Parsing
 *---------------------------------------------------------------------------*/

static const char* parseKey(const char *str, char out[INI_MAX_KEY_LENGTH])
{
    int n = 0;
    memset(out, '\0', INI_MAX_KEY_LENGTH);

    while(*str && *str != ' ' && *str != '=')
    {
        if(!(isalnum(*str) || *str == '.' || *str == '-' || *str == '_'))
            return err("invalid character in key");

        if(!(n < INI_MAX_KEY_LENGTH - 1))
            return err("key too long");
            
        out[n++] = *(str++);
    }

    if(n == 0)
        return err("key too short");

    return str;
}

/*----------------------------------------------------------------------------
 * Value Parsing
 *---------------------------------------------------------------------------*/

static const char* parseUnquotedValue(const char *str, char out[INI_MAX_VALUE_LENGTH])
{
    int n = 0;
    memset(out, '\0', INI_MAX_VALUE_LENGTH);

    while(*str && (*str != '\n' && *str != '\r') && *str != ';')
    {
        if(!(isprint(*str) || *str == '\t'))
            return err("invalid character in value");

        if(!(n < INI_MAX_VALUE_LENGTH - 1))
            return err("value too long or too many trailing spaces");

        out[n++] = *(str++);
    }

    while(n > 0 && out[n - 1] == ' ')
        out[--n] = '\0';

    return str;
}

static const char* parseQuotedValue(const char *str, char out[INI_MAX_VALUE_LENGTH])
{
    int n = 0;
    memset(out, '\0', INI_MAX_VALUE_LENGTH);

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

        if(!(n < INI_MAX_VALUE_LENGTH - 1))
            return err("value too long or too many trailing spaces");

        out[n++] = c;
    }

    if(*str && *(str++) != '"')
        return err("ending quote not found");

    return str;
}

static const char* parseValue(const char *str, char out[INI_MAX_VALUE_LENGTH])
{
    return (*str == '\"') ? parseQuotedValue(str, out) : parseUnquotedValue(str, out);
}

/*----------------------------------------------------------------------------
 * INI Parsing
 *---------------------------------------------------------------------------*/

int INI_Parse(const char *text, INIParseCallback callback, void *callback_data)
{
    INI_ASSERT(callback != NULL);

    char section[INI_MAX_SECTION_LENGTH];
    char key[INI_MAX_KEY_LENGTH];
    char value[INI_MAX_VALUE_LENGTH];
    const char *str = text;

    while(str != NULL && *str)
    {
        if(!(str = skipToFirstReadableChar(str)))
            return INI_ERROR;

        if(*str == '[')
        {
            if(!(str = parseSection(str, section)))
                return INI_ERROR;
        }
        else if(*str == ';')
        {
            if(!(str = nextLine(str)))
                return INI_ERROR;
        }
        else if(*str)
        {
            if(!(str = parseKey(str, key)))
                return INI_ERROR;

            if(!(str = skipEquality(str)))
                return INI_ERROR;

            if(!(str = parseValue(str, value)))
                return INI_ERROR;

            (*callback)(section, key, value, callback_data);
        }
    }

    return INI_OK;
}

#endif /* INI_IMPLEMENTATION */

