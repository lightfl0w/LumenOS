#ifndef ARCH_IRQCHIP_H
#define ARCH_IRQCHIP_H

#if defined(CONFIG_ARCH_X86_64)
#include "arch/x86_64/irq/acpi.h"
#include "arch/x86_64/irq/apic.h"
#include "arch/x86_64/irq/pic.h"
#elif defined(CONFIG_ARCH_AARCH64)
#error "aarch64 irqchip not implemented"
#endif

#endif
