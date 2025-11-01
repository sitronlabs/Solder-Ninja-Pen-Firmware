#ifndef COM_H
#define COM_H

/* Config */
#include "../cfg/config.h"
#ifndef CONFIG_COMMAND_LENGTH_LIMIT
#define CONFIG_COMMAND_LENGTH_LIMIT 256  //!< Maximum length of a command (in bytes).
#endif
#ifndef CONFIG_COMMAND_JSON_DOCUMENT_SIZE
#define CONFIG_COMMAND_JSON_DOCUMENT_SIZE (4 * CONFIG_COMMAND_LENGTH_LIMIT)  //!< Size of JSON document buffer for command parsing (accounts for JSON overhead).
#endif

/* C/C++ headers */
#include <stddef.h>

/* Prototypes */
int com_setup(void);
int com_task(void);

#endif
