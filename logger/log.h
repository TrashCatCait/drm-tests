#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>

enum cway_log_types {
    LOGGER_DEBUG,
    LOGGER_INFO,
    LOGGER_WARN, 
    LOGGER_ERROR,
    LOGGER_FATAL
};

struct log_config { 
    bool silenced;    
    uint32_t listen_level; //Anything below this level won't be printed
};

struct log_event {
    uint32_t type; 
    uint64_t line;
    struct tm time;
    const char *fname;
    const char *fmt;
    va_list args; 
};

void logger_log(uint32_t type, uint64_t line, const char *fname, const char *fmt, ...);
void logger_set_silenced(bool silenced);
void logger_set_level(uint32_t type);

//easier ways to call logging function 
#define logger_debug(...) logger_log(LOGGER_DEBUG, __LINE__, __FILE_NAME__, __VA_ARGS__)
#define logger_info(...) logger_log(LOGGER_INFO, __LINE__, __FILE_NAME__, __VA_ARGS__)
#define logger_warn(...) logger_log(LOGGER_WARN, __LINE__, __FILE_NAME__, __VA_ARGS__)
#define logger_error(...) logger_log(LOGGER_ERROR, __LINE__, __FILE_NAME__, __VA_ARGS__)
#define logger_fatal(...) logger_log(LOGGER_FATAL, __LINE__, __FILE_NAME__, __VA_ARGS__)

