#ifndef KERNEL_INPUT_EVDEV_H
#define KERNEL_INPUT_EVDEV_H

#include "uapi/evdev.h"

struct evdev_dev;

struct evdev_dev *evdev_create(const char *name, uint16_t bustype, uint16_t vendor,
                               uint16_t product, const char *devnode);
void evdev_enable_key(struct evdev_dev *dev, uint16_t code);
void evdev_enable_rel(struct evdev_dev *dev, uint16_t code);
void evdev_report(struct evdev_dev *dev, uint16_t type, uint16_t code, int32_t value);

#endif
