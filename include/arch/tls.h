#ifndef ARCH_TLS_H
#define ARCH_TLS_H

#include <stdint.h>

#if defined(CONFIG_ARCH_X86_64)
#include "arch/x86_64/cpu/gdt.h"
#define arch_tls_desc_set_base(base) tls_desc_set_base(base)
#define ARCH_TLS_SELECTOR SELECTOR_TLS
#elif defined(CONFIG_ARCH_AARCH64)
#error "aarch64 TLS not implemented"
#endif

#endif
