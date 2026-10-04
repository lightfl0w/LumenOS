#ifndef ARCH_POWER_H
#define ARCH_POWER_H

#if defined(CONFIG_ARCH_X86_64)
#include "arch/x86_64/irq/acpi.h"
#define arch_poweroff() acpi_shutdown()
#elif defined(CONFIG_ARCH_AARCH64)
#error "aarch64 power management not implemented"
#endif

#endif
