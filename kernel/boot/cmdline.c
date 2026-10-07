#include "kernel/boot/cmdline.h"

#include "lib/string/str.h"

#define CMDLINE_TOK_MAX 32

struct cmdline_tok {
    char name[CMDLINE_MAX];
    char value[CMDLINE_MAX];
    int has_value;
};

static struct cmdline_tok g_toks[CMDLINE_TOK_MAX];
static size_t g_ntoks;

static void add_tok(const char *start, size_t len) {
    if (g_ntoks >= CMDLINE_TOK_MAX) {
        return;
    }
    size_t eq = (size_t)-1;
    for (size_t i = 0; i < len; ++i) {
        if (start[i] == '=') {
            eq = i;
            break;
        }
    }
    struct cmdline_tok *t = &g_toks[g_ntoks];
    if (eq == (size_t)-1) {
        if (len >= CMDLINE_MAX) {
            len = CMDLINE_MAX - 1;
        }
        for (size_t i = 0; i < len; ++i) {
            t->name[i] = start[i];
        }
        t->name[len] = '\0';
        t->value[0] = '\0';
        t->has_value = 0;
    } else {
        size_t nlen = eq;
        size_t vlen = len - eq - 1;
        if (nlen >= CMDLINE_MAX) {
            nlen = CMDLINE_MAX - 1;
        }
        for (size_t i = 0; i < nlen; ++i) {
            t->name[i] = start[i];
        }
        t->name[nlen] = '\0';
        if (vlen >= CMDLINE_MAX) {
            vlen = CMDLINE_MAX - 1;
        }
        for (size_t i = 0; i < vlen; ++i) {
            t->value[i] = start[eq + 1 + i];
        }
        t->value[vlen] = '\0';
        t->has_value = 1;
    }
    ++g_ntoks;
}

void cmdline_init(const char *raw) {
    g_ntoks = 0;
    if (raw == NULL) {
        return;
    }

    size_t len = 0;
    while (raw[len] != '\0') {
        ++len;
    }

    size_t tok_start = (size_t)-1;
    for (size_t p = 0; p <= len; ++p) {
        char c = (p < len) ? raw[p] : ' ';
        int is_ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
        if (is_ws) {
            if (tok_start != (size_t)-1) {
                add_tok(raw + tok_start, p - tok_start);
                tok_start = (size_t)-1;
            }
        } else if (tok_start == (size_t)-1) {
            tok_start = p;
        }
    }
}

int cmdline_has_flag(const char *name) {
    if (name == NULL) {
        return 0;
    }
    for (size_t i = 0; i < g_ntoks; ++i) {
        if (g_toks[i].has_value) {
            continue;
        }
        if (strcmp(g_toks[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

int cmdline_get_value(const char *name, char *out, size_t out_size) {
    if (name == NULL || out == NULL || out_size == 0) {
        return 0;
    }
    out[0] = '\0';
    for (size_t i = 0; i < g_ntoks; ++i) {
        if (!g_toks[i].has_value) {
            continue;
        }
        if (strcmp(g_toks[i].name, name) == 0) {
            size_t j = 0;
            while (g_toks[i].value[j] != '\0' && j + 1 < out_size) {
                out[j] = g_toks[i].value[j];
                ++j;
            }
            out[j] = '\0';
            return 1;
        }
    }
    return 0;
}

size_t cmdline_count(void) {
    return g_ntoks;
}
