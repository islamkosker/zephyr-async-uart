#ifndef LOGGER_H
#define LOGGER_H

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>


#ifndef APP_LOG_MODULE
#define APP_LOG_MODULE app
#endif

#if IS_ENABLED(CONFIG_APP_DEV_BUILD)
#define APP_LOG_LEVEL LOG_LEVEL_DBG
#else
#define APP_LOG_LEVEL LOG_LEVEL_INF
#endif

#ifndef __FILE_NAME__
#define __FILE_NAME__ __FILE__
#endif

#if IS_ENABLED(CONFIG_APP_LOG_WITH_FILELINE)
#define _LOGF_FMT(fmt) "[%s:%d] " fmt
#define _LOGF_ARGS() , __FILE_NAME__, __LINE__
#else
#define _LOGF_FMT(fmt) fmt
#define _LOGF_ARGS() /* empty */
#endif

#define LOG_INFO(fmt, ...) LOG_INF(_LOGF_FMT(fmt) _LOGF_ARGS(), ##__VA_ARGS__)
#define LOG_WARNING(fmt, ...) LOG_WRN(_LOGF_FMT(fmt) _LOGF_ARGS(), ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG_ERR(_LOGF_FMT(fmt) _LOGF_ARGS(), ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) LOG_DBG(_LOGF_FMT(fmt) _LOGF_ARGS(), ##__VA_ARGS__)

#if IS_ENABLED(CONFIG_THREAD_STACK_INFO) && IS_ENABLED(CONFIG_INIT_STACKS)
#define LOG_STACK_INFO()                                              \
    do                                                                \
    {                                                                 \
        size_t _unused = 0;                                           \
        if (k_thread_stack_space_get(k_current_get(), &_unused) == 0) \
        {                                                             \
            LOG_WRN(_LOGF_FMT("STACK unused: %u bytes")               \
                        _LOGF_ARGS(),                                 \
                    (unsigned)_unused);                               \
        }                                                             \
    } while (0)
#else
#define LOG_STACK_INFO() \
    do                   \
    {                    \
    } while (0)
#endif

#endif /* LOGGER_H */
