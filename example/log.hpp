#include <iostream>


#define LOG_LEVEL_QUIET 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_DEBUG 2

#define LOG_QUIET(fmt, ...) \
    do { \
        if(LOG_LEVEL == LOG_LEVEL_QUIET); \
    } while(0)


#define LOG_INFO(fmt, ...) \
    do { \
        if(LOG_LEVEL == LOG_LEVEL_INFO || LOG_LEVEL == LOG_LEVEL_DEBUG) \
        fprintf(stderr, "[INFO] " fmt "\n", ##__VA_ARGS__); \
    } while(0)



#define LOG_DEBUG(fmt, ...) \
    do { \
        if(LOG_LEVEL == LOG_LEVEL_DEBUG) \
        fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__); \
    } while(0)
