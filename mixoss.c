/*
 * Copyright (c) 2010 Nicolas Martyanoff
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>

#include <curses.h>

#include <soundcard.h>

struct control {
    struct oss_mixext info;
    int is_vmix;

    struct control *ui_next;
};

struct mixer {
    struct oss_mixerinfo info;

    struct control *controls;
    int nb_controls;
};

static const char *mixer_dev = "/dev/mixer";
static int mixer_fd;

static struct mixer *mixers;
static int nb_mixers;
static struct mixer *cur_mixer;

static const char *title = "mixoss";
static int label_padding = 12;
static int gauge_width = 20;
static int poll_interval = 250; /* ms */
static struct control *ui_dev_controls;
static struct control *ui_vmix_controls;

static int get_mixer_info(struct oss_mixerinfo *);
static void reverse_control_list(struct control **);
static int load_mixers();
static void free_mixers();

static int init_ui();
static void free_ui();
static void set_ui_error(const char *, ...);
static int draw_control(struct control *, int, int);
static void draw_ui();

static int get_mixer_info(struct oss_mixerinfo *info)
{
    errno = 0;
    if (ioctl(mixer_fd, SNDCTL_MIXERINFO, info) == -1) {
        set_ui_error("cannot get mixer info: %s", strerror(errno));
        return -1;
    }

    return 0;
}

static void reverse_control_list(struct control **plist) {
    struct control *curr, *next, *res;

    curr = *plist;
    res = NULL;

    while (curr) {
        next = curr->ui_next;
        curr->ui_next = res;
        res = curr;
        curr = next;
    }

    *plist = res;
}

static int
load_mixers() {
    if (ioctl(mixer_fd, SNDCTL_MIX_NRMIX, &nb_mixers) == -1) {
        perror("cannot get number of mixers");
        return -1;
    }

    mixers = calloc(nb_mixers, sizeof(struct mixer));
    if (!mixers) {
        perror("cannot allocate mixer structures");
        return -1;
    }

    for (int m = 0; m < nb_mixers; m++) {
        struct mixer *mixer = &mixers[m];

        mixer->info.dev = m;

        errno = 0;
        if (ioctl(mixer_fd, SNDCTL_MIXERINFO, &mixer->info) == -1) {
            perror("cannot get mixer info");
            free_mixers();
            return -1;
        }

        mixer->nb_controls = mixer->info.nrext;
        mixer->controls = calloc(mixer->nb_controls, sizeof(struct control));
        if (!mixer->controls) {
            perror("cannot allocate control structures");
            free_mixers();
            return -1;
        }

        if (!mixer->info.enabled) {
            /* e.g. disconnected USB device */
            fprintf(stderr, "found a disabled device: '%s'\n",
                    mixer->info.name);
            continue;
        }

        for (int e = 0; e < mixer->nb_controls; e++) {
            struct control *ctrl = &mixers->controls[e];

            ctrl->info.dev = m;
            ctrl->info.ctrl = e;

            errno = 0;
            if (ioctl(mixer_fd, SNDCTL_MIX_EXTINFO, &ctrl->info) == -1) {
                perror("cannot get mixer extension info");
                free_mixers();
                break;
            }

            if (strstr(ctrl->info.extname, "vmix") == ctrl->info.extname)
                ctrl->is_vmix = 1;

            if (ctrl->info.type == MIXT_STEREOSLIDER
             || ctrl->info.type == MIXT_STEREOSLIDER16) {
                if (ctrl->is_vmix) {
                    ctrl->ui_next = ui_vmix_controls;
                    ui_vmix_controls = ctrl;
                } else {
                    ctrl->ui_next = ui_dev_controls;
                    ui_dev_controls = ctrl;
                }
            }
        }
    }

    reverse_control_list(&ui_dev_controls);
    reverse_control_list(&ui_vmix_controls);

    return 0;
}

static void
free_mixers() {
    if (nb_mixers == 0)
        return;

    for (int m = 0; m < nb_mixers; m++) {
        struct mixer * mixer = &mixers[m];
        free(mixer->controls);
    }

    free(mixers);
}

static int
init_ui() {
    initscr();
    keypad(stdscr, 1);
    nonl();
    cbreak();
    noecho();

    return 0;
}

static void
free_ui() {
    endwin();
}

static void set_ui_error(const char *fmt, ...) {
    int width, height;
    char buf[1024];
    va_list ap;

    width  = getmaxx(stdscr);
    height = getmaxy(stdscr);

    move(height - 1, 0);
    clrtoeol();

    if (fmt) {
        va_start(ap, fmt);
        vsnprintf(buf, 1024, fmt, ap);
        va_end(ap);

        mvaddstr(height - 1, (width - strlen(buf)) / 2, buf);
    }

    refresh();
}

static int
draw_control(struct control *ctrl, int py, int px) {
    struct oss_mixext *ext = &ctrl->info;
    struct oss_mixer_value val;

    int min, max;
    int vleft, vright, vpercent;
    int nb_bars;
    int x;

    min = ctrl->info.minvalue;
    max = ctrl->info.maxvalue;

    val.dev = cur_mixer->info.dev;
    val.ctrl = ctrl->info.ctrl;
    val.timestamp = ctrl->info.timestamp;
    val.value = -1;
    if (ioctl (mixer_fd, SNDCTL_MIX_READ, &val) == -1) {
        set_ui_error("cannot read control %s: %s",
                     ctrl->info.id, strerror(errno));
        return -1;
    }

    if (ext->type == MIXT_STEREOSLIDER) {
        vleft = val.value & 0xff;
        vright = (val.value >> 8) & 0xffff;
    } else if (ext->type == MIXT_STEREOSLIDER16) {
        vleft = val.value & 0xffff;
        vright = (val.value >> 16) & 0xffff;
    }

    vpercent = min + (vleft * 100) / (max - min);
    nb_bars = (vpercent * gauge_width) / 100;

    x = px;
    mvprintw(py, x, "%.*s", label_padding, ext->id);

    x += label_padding + 1;
    for (int g = 0; g < nb_bars; g++) {
        mvaddch(py, x, '|');
        x++;
    }
    x += gauge_width - nb_bars;

    x++;
    mvprintw(py, x, "%3d%%", vpercent);

    return 0;
}

static void
draw_ui() {
    struct control *ctrl;
    int width, height;
    int py_left, py_right;
    int px;
    int y_max;

    width  = getmaxx(stdscr);
    height = getmaxy(stdscr);

    clear();

    mvaddstr(0, (80 - strlen(title)) / 2, title);

    py_left = 2;
    for (ctrl = ui_dev_controls; ctrl; ctrl = ctrl->ui_next) {
        px = 0;

        if (draw_control(ctrl, py_left, px) == 0)
            py_left++;
    }

    py_right = 2;
    for (ctrl = ui_vmix_controls; ctrl; ctrl = ctrl->ui_next) {
        px = 1 + label_padding + 2 + gauge_width + 1 + 6;

        if (draw_control(ctrl, py_right, px) == 0)
            py_right++;
    }

    y_max = py_left > py_right ? py_left : py_right;
    for (int y = 2; y < y_max; y++)
        mvaddch(y, 40, ACS_VLINE);

    refresh();
}

int
main(int argc, char **argv) {
    int modify_counter;
    int stop;
    int opt;

    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
            case 'h':
                printf("usage: %s [-h]", argv[0]);
                exit(0);

            default:
                fprintf(stderr, "unknown option: -%c\n", opt);
                exit(1);
        }
    }

    if ((mixer_fd = open(mixer_dev, O_RDWR)) < 0) {
        perror("cannot open mixer");
        exit(1);
    }

    if (load_mixers() < 0)
        exit(1);
    cur_mixer = &mixers[0];

    if (init_ui() < 0) {
        free_mixers();
        exit(1);
    }

    draw_ui();

    modify_counter = -1;

    stop = 0;
    while (!stop) {
        fd_set readfds;
        struct timeval stimeout;

        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);

        stimeout.tv_sec = poll_interval / 1000;
        stimeout.tv_usec = (poll_interval % 1000) * 1000;

        if (select(1, &readfds, NULL, NULL, &stimeout) < 0) {
            set_ui_error("select() failed: %s", strerror(errno));
        }

        if (get_mixer_info(&cur_mixer->info) == 0) {
            if (cur_mixer->info.modify_counter != modify_counter) {
                modify_counter = cur_mixer->info.modify_counter;
                draw_ui();
            }
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            int c;

            switch (c = getch()) {
                case 'q':
                    stop = 1;
                    break;
            }
        }
    }

    free_ui();
    free_mixers();
    close(mixer_fd);

    return 0;
}
