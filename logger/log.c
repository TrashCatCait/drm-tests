#include "log.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

static struct log_config config = {
    .silenced = 0,
    .listen_level = LOGGER_DEBUG,
};

static inline const char *logger_get_type_str(uint32_t type) {
    static const char *type_strs[] = { "DEBUG", "INFO", "WARN", "ERROR", "FATAL" };
    return type > LOGGER_FATAL ? "INVALID" : type_strs[type];
}

static inline const char *logger_get_type_colour(uint32_t type) {
    static const char *type_colours[] = { "\e[1;36m", "\e[1;35m", "\e[1;32m", "\e[1;33m", "\e[1;91m" };
    return type > LOGGER_FATAL ? "\e[1;91m" : type_colours[type];
}

static struct tm get_time() {
    time_t time_posix = time(NULL);
    return *localtime(&time_posix);
}

static void logger_print_stderr(struct log_event ev) {
    fprintf(stderr, "[%02d:%02d:%02d] - %s%s:\e[0m \e[1;90m%s %lu -\e[0m ", 
	    ev.time.tm_hour, ev.time.tm_min, ev.time.tm_sec,
	    logger_get_type_colour(ev.type), logger_get_type_str(ev.type),
	    ev.fname, ev.line);
    
    vfprintf(stderr, ev.fmt, ev.args);
    
    fprintf(stderr, "\n");
}

void logger_set_silenced(bool silenced) {
    config.silenced = silenced;
}

void logger_set_level(uint32_t type) {
    logger_debug("%d", type);
    type = type > LOGGER_FATAL ? LOGGER_FATAL : type;
    logger_debug("%d", type);
    config.listen_level = type;
}


void logger_log(uint32_t type, uint64_t line, const char *file_name, const char *fmt, ...) {
    struct log_event event = { 
	.fmt = fmt,
	.fname = file_name,
	.line = line,
	.time = get_time(),
	.type = type,
    };

    if(!config.silenced && type >= config.listen_level) {
	va_start(event.args, fmt);
	logger_print_stderr(event);	
	va_end(event.args);
    }

}
