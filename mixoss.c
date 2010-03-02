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
#include <sys/stat.h>
#include <unistd.h>

#include <curses.h>

#include <soundcard.h>

struct control {
    struct oss_mixext info;
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
static int gauge_width = 25;

static int load_mixers();
static void free_mixers();

static int init_ui();
static void free_ui();
static void draw_ui();

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
        }
    }

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

static void
draw_ui() {
    int width, height;
    int py;

    width  = getmaxx(stdscr);
    height = getmaxy(stdscr);

    clear();

    mvaddstr(0, (width - strlen(title)) / 2, title);

    py = 3;
    for (int c = 0; c < cur_mixer->nb_controls; c++) {
        struct control *ctrl = &cur_mixer->controls[c];
        struct oss_mixext *ext = &ctrl->info;
        struct oss_mixer_value val;

        if (ext->type == MIXT_STEREOSLIDER
         || ext->type == MIXT_STEREOSLIDER16) {
            int min, max;
            int vleft, vright, vpercent;
            int nb_bars;
            int px;

            min = ctrl->info.minvalue;
            max = ctrl->info.maxvalue;

            val.dev = cur_mixer->info.dev;
            val.ctrl = c;
            val.timestamp = ctrl->info.timestamp;
            val.value = -1;
            if (ioctl (mixer_fd, SNDCTL_MIX_READ, &val) == -1) {
                /* TODO Add a proper way to report errors */
                mvprintw(0, 0, "cannot read control: %s",
                         strerror(errno));
                continue;
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

            mvprintw(py, 1, "%-*s", label_padding, ext->id);

            px = 1 + label_padding + 1;
            for (int g = 0; g < nb_bars; g++) {
                mvaddch(py, px, '|');
                px++;
            }
            px += gauge_width - nb_bars;

            px++;
            mvprintw(py, px, "%3d%%", vpercent);

            py++;
        }
    }

    refresh();
}

int
main(int argc, char **argv) {
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

    stop = 0;
    while (!stop) {
        int c;

        c = getch();

        switch (c) {
            case 'q':
                stop = 1;
                break;
        }
    }

    free_ui();
    free_mixers();
    close(mixer_fd);

    return 0;
}
