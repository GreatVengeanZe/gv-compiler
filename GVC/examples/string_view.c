#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    const char* data;
    unsigned long count;
} String_View;

String_View sv (const char* cstr)
{
    return (String_View) {
        .data = cstr,
        .count = strlen(cstr)
    };
}

void sv_chop_left(String_View* sv, unsigned long n)
{
    if (n > sv->count) n = sv->count;
    sv->count -= n;
    sv->data  += n;
}

void sv_chop_right(String_View* sv, unsigned long n)
{
    if (n > sv->count) n = sv->count;
    sv->count -= n;
}

void sv_trim_left(String_View* sv)
{
    while (sv->count > 0 && isspace(sv->data[0]))
    {
        sv_chop_left(sv, 1);
    }
}

void sv_trim_right(String_View* sv)
{
    while (sv->count > 0 && isspace(sv->data[sv->count - 1]))
    {
        sv_chop_right(sv, 1);
    }
}

void sv_trim(String_View* sv)
{
    sv_trim_left(sv);
    sv_trim_right(sv);
}

String_View sv_chop_by_delim(String_View* sv, char delim)
{
    unsigned long i = 0;
    while (i < sv->count && sv->data[i] != delim)
    {
        i++;
    }

    if (i < sv->count)
    {
        String_View result = {
            .data = sv->data,
            .count = i
        };
        sv_chop_left(sv, i + 1);
        return result;
    }

    String_View result = *sv;
    sv_chop_left(sv, sv->count);
    return result;
}

#define SV_Fmt "%.*s"
#define SV_Arg(s) (s).count, (s).data

int main()
{
    String_View s = sv("    Hello, World   ");
    sv_trim(&s);
    String_View hello = sv_chop_by_delim(&s, ',');
    printf("|"SV_Fmt"|\n", SV_Arg(s));
    printf("|"SV_Fmt"|\n", SV_Arg(hello));
    return 0;
}