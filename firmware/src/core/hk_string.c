#include "hk_string.h"

char ascii_lower(char c)
{
    if(c >= 'A' && c <= 'Z')
        return (char)(c + ('a' - 'A'));
    return c;
}

uint8_t str_eq_ci(const char *a, const char *b)
{
    while(*a && *b)
    {
        if(ascii_lower(*a++) != ascii_lower(*b++))
            return 0;
    }
    return *a == '\0' && *b == '\0';
}
