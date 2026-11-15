#!/usr/bin/env python3

from pathlib import Path
import sys


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    if new in text:
        return
    if old not in text:
        raise SystemExit(f"patch_sway_min: pattern not found in {path}")
    path.write_text(text.replace(old, new, 1))


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_sway_min.py <sway-src>", file=sys.stderr)
        return 2
    root = Path(sys.argv[1]).resolve()

    shim = Path(__file__).resolve().parent.parent / "buildsys" / "sway-shim"
    for rel in ("cairo/cairo.h", "pango/pango.h", "pango/pangocairo.h"):
        source = shim / rel
        if not source.exists():
            raise SystemExit(f"patch_sway_min: missing shim header {source}")
    (root / "include" / "config.h").write_text(
        "#ifndef LUMEN_SWAY_CONFIG_H\n"
        "#define LUMEN_SWAY_CONFIG_H\n"
        "#include <cairo.h>\n"
        "#include <pango/pangocairo.h>\n"
        "#define HAVE_XWAYLAND 0\n"
        "#define HAVE_GDK_PIXBUF 0\n"
        "#define HAVE_LIBSYSTEMD 0\n"
        "#define HAVE_LIBELOGIND 0\n"
        "#define HAVE_BASU 0\n"
        "#define HAVE_TRAY 0\n"
        "#define HAVE_LIBINPUT_CONFIG_ACCEL_PROFILE_CUSTOM 1\n"
        "#endif\n")

    container = root / "sway" / "tree" / "container.c"
    text = container.read_text()
    start = text.index("static void render_titlebar_text_texture(")
    end = text.index("void container_update_title_textures(", start)
    text = text[:start] + (
        "static void render_titlebar_text_texture(struct sway_output *output,\n"
        "\t\tstruct sway_container *con, struct wlr_texture **texture,\n"
        "\t\tstruct border_colors *class, bool pango_markup, char *text) {\n"
        "\t(void)output;\n\t(void)con;\n\t(void)pango_markup;\n\t(void)text;\n"
        "\tif (*texture) {\n\t\twlr_texture_destroy(*texture);\n"
        "\t\t*texture = NULL;\n\t}\n"
        "\t(void)class;\n}\n\n"
        "static void update_title_texture(struct sway_container *con,\n"
        "\t\tstruct wlr_texture **texture, struct border_colors *class) {\n"
        "\t(void)con; (void)class;\n"
        "\tif (*texture) { wlr_texture_destroy(*texture); *texture = NULL; }\n}\n\n"
    ) + text[end:]
    container.write_text(text)

    config = root / "sway" / "config.c"
    text = config.read_text()
    if "struct config_path {\n\t\tconst char *prefix;\n\t\tconst char *config_folder;\n\t};\n\n\tstruct config_path config_paths[] = {" in text:
        text = text.replace(
            "struct config_path {\n\t\tconst char *prefix;\n\t\tconst char *config_folder;\n\t};\n\n\tstruct config_path config_paths[] = {",
            "struct {\n\t\tconst char *prefix;\n\t\tconst char *config_folder;\n\t} config_paths[] = {", 1)
        config.write_text(text)
    if "size_t num_config_paths = sizeof(config_paths)/sizeof(config_paths[0]);" in text:
        text = text.replace(
            "size_t num_config_paths = sizeof(config_paths)/sizeof(config_paths[0]);",
            "const size_t num_config_paths = 6;", 1)
        config.write_text(text)
    replace_once(
        config,
        "\tconfig->font_description = pango_font_description_from_string(config->font);\n",
        "\tconfig->font_description = NULL;\n")
    replace_once(
        config,
        "\tget_text_metrics(config->font_description, &config->font_height, &config->font_baseline);\n",
        "\tconfig->font_height = 10;\n\tconfig->font_baseline = 8;\n")

    font = root / "sway" / "commands" / "font.c"
    text = font.read_text()
    if "// Parse the font early" in text:
        start = text.index("\t// Parse the font early")
        end = text.index("\tconfig_update_font_height();", start)
        text = text[:start] + (
            "\tconfig_update_font_height();\n"
            "\n\treturn cmd_results_new(CMD_SUCCESS, NULL);\n"
        ) + text[end + len("\tconfig_update_font_height();\n"):]
        font.write_text(text)



    tc = root / "sway" / "main.c"
    text = tc.read_text()
    marker = "\tif (!getenv(\"XDG_RUNTIME_DIR\") && optind == argc) {"
    if marker in text and "XKB_CONFIG_ROOT" not in text:
        defaults = (
            "\t/* LUMEN_OS: default runtime dir */\n"
            "\tif (!getenv(\"XDG_RUNTIME_DIR\")) setenv(\"XDG_RUNTIME_DIR\", \"/tmp\", 1);\n"
            "\tif (!getenv(\"XKB_CONFIG_ROOT\")) setenv(\"XKB_CONFIG_ROOT\", \"/share/xkb\", 1);\n\tif (!getenv(\"FONTCONFIG_FILE\")) setenv(\"FONTCONFIG_FILE\", \"/etc/fonts/fonts.conf\", 1);\n\tif (!getenv(\"FONTCONFIG_PATH\")) setenv(\"FONTCONFIG_PATH\", \"/etc/fonts\", 1);\n"
            "\tif (!getenv(\"SEATD_VTBOUND\")) setenv(\"SEATD_VTBOUND\", \"0\", 1);\n"
            "\tif (!getenv(\"WLR_RENDERER\")) setenv(\"WLR_RENDERER\", \"pixman\", 1);\n"
            "\tif (!getenv(\"WLR_BACKENDS\")) setenv(\"WLR_BACKENDS\", \"drm\", 1);\n"
            "\tif (!getenv(\"WLR_DRM_DEVICES\")) setenv(\"WLR_DRM_DEVICES\", \"/dev/dri/card0\", 1);\n")
        text = text.replace(marker, defaults + marker, 1)
        tc.write_text(text)

    main_c = root / "sway" / "main.c"
    replace_once(main_c, "#include <pango/pangocairo.h>\n", "")
    replace_once(main_c, "\tpango_cairo_font_map_set_default(NULL);\n\n", "")

    ipc_h = root / "include" / "sway" / "ipc-server.h"
    text = ipc_h.read_text()
    marker = "#include \"sway/config.h\""
    text = text.replace("void ipc_init(struct sway_server *server);\n", "", 1)
    if "#define ipc_init" not in text:
        text = text.replace(marker, marker + "\n#define ipc_init(server) ((void)(server))", 1)
    for name in ("ipc_event_workspace", "ipc_event_window", "ipc_event_barconfig_update",
                 "ipc_event_bar_state_update", "ipc_event_mode", "ipc_event_shutdown",
                 "ipc_event_binding", "ipc_event_input", "ipc_event_output"):
        macro = f"#define {name}(...)"
        if macro not in text:
            text += f"\n{macro} ((void)0)\n"
    ipc_h.write_text(text)

    main_cfg = root / "sway" / "main.c"
    text = main_cfg.read_text()
    helper = '''\n/* LUMEN_OS evdev keyboard bridge: no libinput backend in this build. */\n#include <fcntl.h>\n#include <linux/input.h>\n#include <unistd.h>\n#include <wlr/interfaces/wlr_keyboard.h>\n#include "sway/input/input-manager.h"\n#include "sway/input/seat.h"\n\nstatic int lumen_evdev_key_event(int fd, uint32_t mask, void *data) {\n\tstruct wlr_keyboard *keyboard = data;\n\tstruct input_event ev;\n\twhile (read(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {\n\t\tif (ev.type != EV_KEY) {\n\t\t\tcontinue;\n\t\t}\n\t\tstruct wlr_keyboard_key_event wlr_event = {\n\t\t\t.time_msec = (uint32_t)(ev.time.tv_sec * 1000 + ev.time.tv_usec / 1000),\n\t\t\t.keycode = ev.code,\n\t\t\t.update_state = true,\n\t\t\t.state = ev.value ? WL_KEYBOARD_KEY_STATE_PRESSED\n\t\t\t\t: WL_KEYBOARD_KEY_STATE_RELEASED,\n\t\t};\n\t\twlr_keyboard_notify_key(keyboard, &wlr_event);\n\t}\n\t(void)mask;\n\treturn 0;\n}\n\nstatic void lumen_evdev_keyboard_init(struct sway_server *server) {\n\tint fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);\n\tif (fd < 0) {\n\t\tsway_log(SWAY_ERROR, "LUMEN_OS: cannot open /dev/input/event0");\n\t\treturn;\n\t}\n\tstruct wlr_keyboard *keyboard = calloc(1, sizeof(*keyboard));\n\tstruct sway_input_device *device = calloc(1, sizeof(*device));\n\tif (!keyboard || !device) {\n\t\tclose(fd);\n\t\tfree(keyboard);\n\t\tfree(device);\n\t\treturn;\n\t}\n\twlr_keyboard_init(keyboard, NULL, "lumen-atkbd");\n\tdevice->identifier = strdup("lumen-atkbd");\n\tdevice->wlr_device = &keyboard->base;\n\twl_list_init(&device->link);\n\twl_list_insert(&server->input->devices, &device->link);\n\tseat_add_device(input_manager_get_default_seat(), device);\n\tif (wl_event_loop_add_fd(server->wl_event_loop, fd, WL_EVENT_READABLE,\n\t\t\tlumen_evdev_key_event, keyboard) == NULL) {\n\t\tsway_log(SWAY_ERROR, "LUMEN_OS: cannot add evdev keyboard source");\n\t\tclose(fd);\n\t}\n}\n'''
    if "LUMEN_OS evdev keyboard bridge" not in text:
        anchor = "int main(int argc, char **argv) {\n"
        if anchor not in text:
            raise SystemExit("patch_sway_min: main() not found")
        text = text.replace(anchor, helper + "\n" + anchor, 1)
        text = text.replace("\tconfig->active = true;\n",
                            "\tconfig->active = true;\n"
                            "\tlumen_evdev_keyboard_init(&server);\n", 1)
        main_cfg.write_text(text)

    seat_c = root / "sway" / "input" / "seat.c"
    replace_once(
        seat_c,
        "\twlr_idle_notifier_v1_notify_activity(server.idle_notifier_v1, seat->wlr_seat);\n",
        "\t(void)seat;\n\t(void)source;\n")
    replace_once(
        seat_c,
        "\treturn keyboard_shortcuts_inhibitor_get_for_surface(seat,\n"
        "\t\tseat->wlr_seat->keyboard_state.focused_surface);\n",
        "\t(void)seat;\n\treturn NULL;\n")

    exec_c = root / "sway" / "commands" / "exec_always.c"
    replace_once(
        exec_c,
        "\t\t\texeclp(\"sh\", \"sh\", \"-c\", cmd, (void *)NULL);\n",
        "\t\t\tif (strpbrk(cmd, \" |&;<>()$`\\\\\\\"\'\") == NULL) {\n"
        "\t\t\t\texecl(cmd, cmd, (void *)NULL);\n"
        "\t\t\t}\n"
        "\t\t\texeclp(\"sh\", \"sh\", \"-c\", cmd, (void *)NULL);\n")
    exec_c = root / "sway" / "commands" / "exec_always.c"
    replace_once(
        exec_c,
        "\tsway_log(SWAY_DEBUG, \"Executing %s\", cmd);\n",
        "\tsway_log(SWAY_DEBUG, \"Executing %s\", cmd);\n"
        "\tif (strpbrk(cmd, \" |&;<>()$`\\\\\\\"\'\") == NULL) {\n"
        "\t\tpid_t direct = fork();\n"
        "\t\tif (direct == 0) {\n"
        "\t\t\texecl(cmd, cmd, (void *)NULL);\n"
        "\t\t\t_exit(1);\n"
        "\t\t}\n"
        "\t\tif (direct > 0) {\n"
        "\t\t\treturn cmd_results_new(CMD_SUCCESS, NULL);\n"
        "\t\t}\n"
        "\t}\n")

    replace_once(
        exec_c,
        "\tsway_log(SWAY_DEBUG, \"Executing %s\", cmd);\n",
        "\tsway_log(SWAY_DEBUG, \"Executing %s\", cmd);\n"
        "\tif (strncmp(cmd, \"lumen-spawn \", 12) == 0) {\n"
        "\t\tconst char *target = cmd + 12;\n"
        "\t\tssize_t written = write(3, target, strlen(target) + 1);\n"
        "\t\t(void)written;\n"
        "\t\treturn cmd_results_new(CMD_SUCCESS, NULL);\n"
        "\t}\n")

    replace_once(exec_c, "#include <unistd.h>\n", "#include <unistd.h>\n#include <fcntl.h>\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
