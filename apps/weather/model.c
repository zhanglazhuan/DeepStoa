#include "model.h"

#include <stdlib.h>

static const char *s_city_codes[] = {
    "101010100", /* Beijing */
    "101020100", /* Shanghai */
    "101280601", /* Shenzhen */
};

bool weather_model_init(WeatherModel **out)
{
    if (!out) return false;
    *out = calloc(1, sizeof(**out));
    return *out != NULL;
}

void weather_model_deinit(WeatherModel *model)
{
    free(model);
}

const char *weather_city_code(unsigned index)
{
    return index < sizeof(s_city_codes) / sizeof(s_city_codes[0])
        ? s_city_codes[index] : NULL;
}

const char *weather_city_options(void)
{
    return "Beijing\nShanghai\nShenzhen";
}
