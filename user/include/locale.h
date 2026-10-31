#pragma once
#define LC_ALL 0
#define LC_COLLATE 1
#define LC_CTYPE 2
#define LC_MONETARY 3
#define LC_NUMERIC 4
#define LC_TIME 5
struct lconv {
    char *decimal_point, *thousands_sep, *grouping;
    char *int_curr_symbol, *currency_symbol, *mon_decimal_point, *mon_thousands_sep, *mon_grouping;
    char *positive_sign, *negative_sign;
};
char *setlocale(int category, const char *locale);
struct lconv *localeconv(void);
