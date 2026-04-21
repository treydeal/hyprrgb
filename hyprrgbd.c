#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <wayland-client.h>

#include "hyprland-ctm-control-v1-client-protocol.h"

struct output_info {
    uint32_t global_name;
    struct wl_output *output;
    char name[128];
    bool has_name;
};

struct state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct hyprland_ctm_control_manager_v1 *ctm;

    struct output_info outputs[16];
    size_t outputs_len;

    const char *target;
    bool blocked;
    volatile sig_atomic_t running;
};

static struct state st = {0};

static void stop_handler(int sig) {
    (void)sig;
    st.running = 0;
}

static struct output_info *find_output(const char *name) {
    struct output_info *unnamed = NULL;

    for (size_t i = 0; i < st.outputs_len; ++i) {
        if (st.outputs[i].has_name && strcmp(st.outputs[i].name, name) == 0)
            return &st.outputs[i];

        if (!st.outputs[i].has_name && !unnamed)
            unnamed = &st.outputs[i];
    }

    return unnamed;
}

/* wl_output listeners */

static void on_output_geometry(void *data, struct wl_output *o,
                               int32_t x, int32_t y,
                               int32_t pw, int32_t ph,
                               int32_t subpixel,
                               const char *make,
                               const char *model,
                               int32_t transform) {
    (void)data;(void)o;(void)x;(void)y;(void)pw;(void)ph;
    (void)subpixel;(void)make;(void)model;(void)transform;
}

static void on_output_mode(void *data, struct wl_output *o,
                           uint32_t flags,
                           int32_t w, int32_t h, int32_t refresh) {
    (void)data;(void)o;(void)flags;(void)w;(void)h;(void)refresh;
}

static void on_output_done(void *data, struct wl_output *o) {
    (void)data;(void)o;
}

static void on_output_scale(void *data, struct wl_output *o, int32_t factor) {
    (void)data;(void)o;(void)factor;
}

static void on_output_name(void *data, struct wl_output *o, const char *name) {
    struct output_info *info = data;
    (void)o;
    snprintf(info->name, sizeof(info->name), "%s", name);
    info->has_name = true;
}

static void on_output_description(void *data, struct wl_output *o, const char *desc) {
    (void)data;(void)o;(void)desc;
}

static const struct wl_output_listener output_listener = {
    .geometry = on_output_geometry,
    .mode = on_output_mode,
    .done = on_output_done,
    .scale = on_output_scale,
    .name = on_output_name,
    .description = on_output_description,
};

/* CTM listener */

static void on_ctm_blocked(void *data,
    struct hyprland_ctm_control_manager_v1 *mgr) {
    (void)mgr;
    struct state *s = data;
    s->blocked = true;
    fprintf(stderr, "CTM manager blocked by another client\n");
}

static const struct hyprland_ctm_control_manager_v1_listener ctm_listener = {
    .blocked = on_ctm_blocked,
};

/* Registry */

static void on_global(void *data, struct wl_registry *registry,
                      uint32_t name,
                      const char *interface,
                      uint32_t version) {
    struct state *s = data;

    if (strcmp(interface, wl_output_interface.name) == 0) {
        if (s->outputs_len >= 16)
            return;

        struct output_info *info = &s->outputs[s->outputs_len++];
        memset(info, 0, sizeof(*info));

        info->global_name = name;

        uint32_t bind_version = version > 4 ? 4 : version;
        info->output = wl_registry_bind(registry, name,
                                        &wl_output_interface,
                                        bind_version);

        wl_output_add_listener(info->output, &output_listener, info);

    } else if (strcmp(interface,
               hyprland_ctm_control_manager_v1_interface.name) == 0) {

        uint32_t bind_version = version > 2 ? 2 : version;

        s->ctm = wl_registry_bind(registry, name,
                &hyprland_ctm_control_manager_v1_interface,
                bind_version);

        hyprland_ctm_control_manager_v1_add_listener(
            s->ctm, &ctm_listener, s);
    }
}

static void on_global_remove(void *data,
                             struct wl_registry *registry,
                             uint32_t name) {
    (void)data;(void)registry;(void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = on_global,
    .global_remove = on_global_remove,
};

/* helpers */

static wl_fixed_t pct(const char *s) {
    char *end = NULL;
    double v = strtod(s, &end);

    if (!end || *end != '\0' || v < 0.0) {
        fprintf(stderr, "invalid percentage: %s\n", s);
        exit(1);
    }

    return wl_fixed_from_double(v / 100.0);
}

static void apply_rgb(const char *r_s,
                      const char *g_s,
                      const char *b_s) {

    if (!st.ctm) {
        fprintf(stderr, "No CTM manager\n");
        return;
    }

    if (st.blocked) {
        fprintf(stderr, "Blocked by another CTM client\n");
        return;
    }

    struct output_info *out = find_output(st.target);

    if (!out) {
        fprintf(stderr, "Output '%s' not found\n", st.target);
        return;
    }

    hyprland_ctm_control_manager_v1_set_ctm_for_output(
        st.ctm,
        out->output,
        pct(r_s), 0, 0,
        0, pct(g_s), 0,
        0, 0, pct(b_s)
    );

    hyprland_ctm_control_manager_v1_commit(st.ctm);

    wl_display_roundtrip(st.display);

    printf("Applied to %s\n",
           out->has_name ? out->name : "[unnamed]");
}

/* main */

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <output> <R%%> <G%%> <B%%>\n", argv[0]);
        return 1;
    }

    st.target = argv[1];
    const char *r = argv[2];
    const char *g = argv[3];
    const char *b = argv[4];

    signal(SIGINT, stop_handler);
    signal(SIGTERM, stop_handler);

    st.display = wl_display_connect(NULL);
    if (!st.display) {
        fprintf(stderr, "Failed to connect to Wayland\n");
        return 1;
    }

    st.registry = wl_display_get_registry(st.display);
    wl_registry_add_listener(st.registry, &registry_listener, &st);

    wl_display_roundtrip(st.display);
    wl_display_roundtrip(st.display);

    if (!st.ctm) {
        fprintf(stderr, "hyprland_ctm_control_manager_v1 not available\n");
        return 1;
    }

    apply_rgb(r, g, b);

    st.running = 1;

    while (st.running) {
        /* non-blocking dispatch */
        wl_display_dispatch_pending(st.display);
        wl_display_flush(st.display);
        usleep(10000); // 10ms
    }

    if (st.ctm)
        hyprland_ctm_control_manager_v1_destroy(st.ctm);

    if (st.registry)
        wl_registry_destroy(st.registry);

    if (st.display)
        wl_display_disconnect(st.display);

    return 0;
}
