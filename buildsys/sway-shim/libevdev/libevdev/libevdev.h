#ifndef LUMEN_LIBEVDEV_SHIM_H
#define LUMEN_LIBEVDEV_SHIM_H

#include <linux/input-event-codes.h>

const char *libevdev_event_code_get_name(unsigned int type,
	unsigned int code);
int libevdev_event_code_from_name(unsigned int type, const char *name);

#endif
