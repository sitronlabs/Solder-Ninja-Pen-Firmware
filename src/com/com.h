#ifndef COM_H
#define COM_H

/* C/C++ libraries */
#include <stddef.h>
#include <stdint.h>

/* Prototypes */
int com_setup(void);
int com_command_process(const char* const str, const size_t len);
int com_task(void);

#endif
