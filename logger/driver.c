#include "./log.h"

int main() {
    logger_info("%s", "Info Example");
    logger_debug("%s", "Debug Example");
    logger_error("%s", "Error Example");
    logger_warn("%s", "Warning Example");
    logger_fatal("%s", "Fatal Example");
    return 0;
}
