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

static const char *title = "mixoss";

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

        mixer->controls = calloc(mixer->info.nrext, sizeof(struct control));
        if (!mixer->controls) {
            perror("cannot allocate control structures");
            free_mixers();
            return -1;
        }

        for (int e = 0; e < mixer->info.nrext; e++) {
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

    width  = getmaxx(stdscr);
    height = getmaxy(stdscr);

    clear();

    mvaddstr(0, (width - strlen(title)) / 2, title);

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
