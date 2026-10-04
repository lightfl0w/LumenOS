#include "kernel/assert.h"

#include "drivers/char/serial/console/io.h"
#include "kernel/asm_func.h"

void assert_fail(const char *expr, const char *file, int line) {
    set_text_color(12);
    kprintf("\n*** ASSERT FAILED ***\n");
    kprintf("  expr: %s\n", expr);
    kprintf("  file: %s\n", file);
    kprintf("  line: %d\n", line);

    asm_cli();
    for (;;) {
        asm_hlt();
    }
}
