#include "speed.h"
#include "gps.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <math.h>
#include <float.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <xenomai/native/task.h>
#include <psgc.h>

/* types ==================================================================== */
typedef struct status_t status_t;
typedef struct sensor_t sensor_t;
typedef struct sector_t sector_t;
typedef struct config_file_t config_file_t;
typedef struct sector_file_t sector_file_t;

/* structures =============================================================== */
struct status_t {
    unsigned int loaded:1;
    unsigned int started:1;
};

struct sensor_t {
    int (* init)(void);
    int (* exit)(void);
};

struct sector_t {
    double latitude;
    double longitude;
    double speed_min;
    double speed_max;
};

struct config_file_t {
    unsigned int wheel_length;
    double gps_epsilon_latitude;
    double gps_epsilon_longitude;
};

struct sector_file_t {
    sector_t sectors[8192];
    size_t count;
};

/* constants ================================================================ */
#define ECOROOT "/var/lib/ecollect"

/* macros =================================================================== */
#define BUG_ON(assertion) \
    do { \
        if (assertion) { \
            fprintf(stderr, "%s(%d): errno=%d\n", __FILE__, __LINE__, errno); \
            abort(); \
        } \
    } while (0)

#define ARRAY_SIZE(t) \
    (sizeof (t) / sizeof *(t))

#define COLLIDE(x, y, x0, y0, w, h) \
    ((x) > (x0) && (x) < (x0) + (w) && (y) > (y0) && (y) < (y0) + (h))

/* private variables ======================================================== */
static volatile int shutdown;

static psgc_t * psgc;

static status_t status = {
    .loaded = 0,
    .started = 0
};

static const sensor_t sensors[] = {
    {
        .init = speed_init,
        .exit = speed_exit
    },
    {
        .init = gps_init,
        .exit = gps_exit
    }
};

static config_file_t config_file;
static sector_file_t sector_file;

/* private functions ======================================================== */
static void handler(int signum) {
    switch (signum) {
        case SIGINT:
        case SIGTERM:
            shutdown = 1;
            return;
        default:
            return;
    }
}

static void load(void) {
    /* check for USB key and mount it --------------------------------------- */
    if (mount("/dev/sda1", ECOROOT, "vfat", 0, NULL) != -1) {
        FILE * fp;

        /* open config file ------------------------------------------------- */
        if ((fp = fopen(ECOROOT "/config", "r")) != NULL) {
            /* load config file into memory --------------------------------- */
            fscanf(fp, "%u", &config_file.wheel_length);
            fscanf(fp, "%lf", &config_file.gps_epsilon_latitude);
            fscanf(fp, "%lf", &config_file.gps_epsilon_longitude);

            /* close config file -------------------------------------------- */
            fclose(fp);
        }

        /* open sector file ------------------------------------------------- */
        if ((fp = fopen(ECOROOT "/sectors", "r")) != NULL) {
            /* load sector file into memory --------------------------------- */
            while (
                sector_file.count < ARRAY_SIZE(sector_file.sectors) &&
                fscanf(
                    fp, "%lf,%lf,%lf,%lf",
                    &sector_file.sectors[sector_file.count].latitude,
                    &sector_file.sectors[sector_file.count].longitude,
                    &sector_file.sectors[sector_file.count].speed_min,
                    &sector_file.sectors[sector_file.count].speed_max
                ) == 4
            ) {
                ++sector_file.count;
            }

            /* close sector file -------------------------------------------- */
            fclose(fp);
        }

        /* OK, USB key is mounted and data have been loaded ----------------- */
        status.loaded = 1;
    }
}

static void unload(void) {
    /* clear loaded data ---------------------------------------------------- */
    memset(&config_file, 0, sizeof config_file);
    memset(&sector_file, 0, sizeof sector_file);

    /* unmount & flush any data written to ECOROOT -------------------------- */
    BUG_ON(umount(ECOROOT) == -1);

    /* OK, USB key is unmounted and can be removed -------------------------- */
    status.loaded = 0;
}

static void start() {
    size_t i;
    time_t time_curr;
    struct tm tm_curr;
    char pathname[PATH_MAX];

    /* create an unique folder in ECOROOT and chdir to it ------------------- */
    time(&time_curr);
    gmtime_r(&time_curr, &tm_curr);
    strftime(pathname, sizeof pathname, ECOROOT "/%Y-%m-%d %H-%M-%S", &tm_curr);
    mkdir(pathname, 0777);
    chdir(pathname);

    /* start sensor threads ------------------------------------------------- */
    for (i = 0; i < ARRAY_SIZE(sensors); i++) {
        BUG_ON(sensors[i].init() == -1);
    }

    /* ok, sensor threads are started --------------------------------------- */
    status.started = 1;
}

static void stop() {
    size_t i;

    /* stop sensor threads -------------------------------------------------- */
    for (i = 0; i < ARRAY_SIZE(sensors); i++) {
        BUG_ON(sensors[i].exit() == -1);
    }

    /* chdir to a safe value ------------------------------------------------ */
    chdir("/");

    /* ok, sensor threads are stopped --------------------------------------- */
    status.started = 0;
}

static void screen_1(void) {
    /* display static content ----------------------------------------------- */
    psgc_clear(psgc);

    psgc_draw_text(
        psgc, 16, 16, PSGC_FONT_12X16, PSGC_RGB555(31, 31, 31), 1, 1,
        "ECOLLECT @ ECOBOX"
    );

    psgc_draw_text(
        psgc, 16, 80, PSGC_FONT_12X16, PSGC_RGB555(31, 31, 31), 1, 1,
        "You can plug or unplug"
    );

    psgc_draw_text(
        psgc, 16, 112, PSGC_FONT_12X16, PSGC_RGB555(31, 31, 31), 1, 1,
        "your USB key now!"
    );

    psgc_draw_button(
        psgc, 0, 208, 192, PSGC_RGB555(0, 0, 31), PSGC_FONT_12X16,
        PSGC_RGB555(31, 31, 31), 2, 2, "LOAD"
    );

    /* start event loop ----------------------------------------------------- */
    while (!shutdown && !status.loaded) {
        u_int16_t event = PSGC_EVENT_NONE;
        u_int16_t x, y;

        /* check if user is pushing "LOAD" button --------------------------- */
        psgc_read_touchscreen(psgc, &event, &x, &y);

        if (event == PSGC_EVENT_PRESS) {
            if (COLLIDE(x, y, 192, 176, 128, 64)) {
                load();
            }
        }
    }
}

static void screen_2(int * next) {
    int id = 0;
    size_t i = 0;
    double speed_min = DBL_MIN;
    double speed_max = DBL_MAX;

    /* display static content ----------------------------------------------- */
    psgc_clear(psgc);

    psgc_draw_button(
        psgc, 0, 208, 192, PSGC_RGB555(0, 0, 31), PSGC_FONT_12X16,
        PSGC_RGB555(31, 31, 31), 2, 2, " GO "
    );

    psgc_draw_button(
        psgc, 0, 16, 192, PSGC_RGB555(0, 0, 31), PSGC_FONT_12X16,
        PSGC_RGB555(31, 31, 31), 2, 2, "BACK"
    );

    /* display config data (currently only wheel length) -------------------- */
    psgc_draw_text(
        psgc, 16, 16, PSGC_FONT_12X16, PSGC_RGB555(31, 31, 31), 1, 1,
        "WL %8u", config_file.wheel_length
    );

    /* display sector data (max. 4, not enough space for more...) ----------- */
    while (i < sector_file.count && id < 4) {
        if (
            sector_file.sectors[i].speed_min != speed_min ||
            sector_file.sectors[i].speed_max != speed_max
        ) {
            speed_min = sector_file.sectors[i].speed_min;
            speed_max = sector_file.sectors[i].speed_max;

            psgc_draw_text(
                psgc, 16, 48 + id * 32, PSGC_FONT_12X16,
                PSGC_RGB555(31, 31, 31), 1, 1,
                "S%d %8.1f %8.1f", id + 1, speed_min, speed_max
            );

            ++id;
        }

        ++i;
    }

    *next = 0;

    /* start event loop ----------------------------------------------------- */
    while (!shutdown && !*next) {
        u_int16_t event = PSGC_EVENT_NONE;
        u_int16_t x, y;

        /* check if user is pushing " GO " or "BACK" button ----------------- */
        psgc_read_touchscreen(psgc, &event, &x, &y);

        if (event == PSGC_EVENT_PRESS) {
            if (COLLIDE(x, y, 192, 176, 128, 64)) {
                start();
                *next = 3;
            }

            if (COLLIDE(x, y, 0, 176, 128, 64)) {
                unload();
                *next = 1;
            }
        }
    }
}

static void screen_3(void) {
    size_t sector_curr = 0;

    /* display static content ----------------------------------------------- */
    psgc_clear(psgc);

    psgc_draw_button(
        psgc, 0, 16, 192, PSGC_RGB555(0, 0, 31), PSGC_FONT_12X16,
        PSGC_RGB555(31, 31, 31), 2, 2, "STOP"
    );

    /* next blits must be in opaque mode ------------------------------------ */
    psgc_set_opaque(psgc, PSGC_OPAQUE_ON);

    /* start event loop ----------------------------------------------------- */
    while (!shutdown && status.started) {
        double speed_instant, speed_average;
        u_int16_t color = PSGC_RGB555(31, 31, 31);
        u_int16_t event = PSGC_EVENT_NONE;
        u_int16_t x, y;

        /* fetch speed sensor data ------------------------------------------ */
        speed_get_instant(&speed_instant);
        speed_get_average(&speed_average);

        /* convert speed from Hz to km/h ------------------------------------ */
        speed_instant *= config_file.wheel_length / 1000.0 * 3.6;
        speed_average *= config_file.wheel_length / 1000.0 * 3.6;

        /* if sector file has been loaded and was not empty ----------------- */
        if (sector_file.count > 0) {
            char gps_frame[GPS_FRAME_SIZE];
            double latitude, longitude;
            double latitude_min, longitude_min;
            char latitude_dir, longitude_dir, fix;

            /* fetch GPS sensor data ---------------------------------------- */
            gps_get_frame(gps_frame);

            /* decode GPS frame and check if it is valid (fix is 1 or 2) ---- */
            if (
                sscanf(
                    gps_frame, "$GPGGA,%*f,%2lf%lf,%c,%3lf%lf,%c,%c",
                    &latitude, &latitude_min, &latitude_dir,
                    &longitude, &longitude_min, &longitude_dir,
                    &fix
                ) == 7 && (
                    fix == '1' || fix == '2'
                )
            ) {
                size_t k = 0;

                /* convert dumb NMEA latitude format to real degrees -------- */
                latitude += latitude_min / 60;
                latitude *= latitude_dir == 'N' ? 1 : -1;

                /* convert dumb NMEA longitude format to real degrees ------- */
                longitude += longitude_min / 60;
                longitude *= longitude_dir == 'E' ? 1 : -1;

                /* try to match a sector ------------------------------------ */
                while (
                    k < sector_file.count &&
                    ! COLLIDE(
                        latitude, longitude,
                        sector_file.sectors[sector_curr].latitude -
                        config_file.gps_epsilon_latitude,
                        sector_file.sectors[sector_curr].longitude -
                        config_file.gps_epsilon_longitude,
                        2 * config_file.gps_epsilon_latitude,
                        2 * config_file.gps_epsilon_longitude
                    )
                ) {
                    if (++sector_curr == sector_file.count) {
                        sector_curr = 0;
                    }

                    ++k;
                }

                /* if we've matched a sector -------------------------------- */
                if (k != sector_file.count) {
                    /* change color to green, yellow or red ----------------- */
                    sector_t sector = sector_file.sectors[sector_curr];

                    if (
                        speed_instant > sector.speed_min &&
                        speed_instant < sector.speed_max
                    ) {
                        color = PSGC_RGB555(31, 31, 0);
                    }
                    else {
                        if (speed_instant < sector.speed_min) {
                            color = PSGC_RGB555(0, 31, 0);
                        }
                        else {
                            color = PSGC_RGB555(31, 0, 0);
                        }
                    }
                }
            }
        }

        /* display instant speed -------------------------------------------- */
        psgc_draw_text(
            psgc, 16, 16, PSGC_FONT_12X16, color, 4, 4,
            "%5.1f", speed_instant
        );

        /* display average speed -------------------------------------------- */
        psgc_draw_text(
            psgc, 16, 112, PSGC_FONT_12X16, PSGC_RGB555(31, 31, 31), 4, 4,
            "%5.1f", speed_average
        );

        /* check if user is pushing "STOP" button --------------------------- */
        psgc_read_touchscreen(psgc, &event, &x, &y);

        if (event == PSGC_EVENT_PRESS) {
            if (COLLIDE(x, y, 0, 176, 128, 64)) {
                stop();
                unload();
            }
        }
    }

    /* reset opaque mode ---------------------------------------------------- */
    psgc_set_opaque(psgc, PSGC_OPAQUE_OFF);
}

/* entry point ============================================================== */
int main(void) {
    int screen;

    const struct sigaction sa = {
        .sa_handler = handler,
        .sa_flags = SA_RESTART
    };

    /* register signal handler ---------------------------------------------- */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* init and setup Picaso LCD -------------------------------------------- */
    BUG_ON(psgc_init(&psgc, "/dev/ttyS3") == -1);

    psgc_set_background(psgc, PSGC_RGB555(0, 0, 0));
    psgc_set_orientation(psgc, PSGC_ORIENTATION_270);
    psgc_set_touchscreen(psgc, PSGC_TOUCHSCREEN_ON);

    /* Xenomai obviously requires virtual address space locking into RAM ---- */
    mlockall(MCL_CURRENT | MCL_FUTURE);

    /* let's become a native Xenomai real-time thread ----------------------- */
    rt_task_shadow(NULL, NULL, 80, 0);

    /* start event loop ----------------------------------------------------- */
    screen = 1;

    while (!shutdown) {
        int next;

        switch (screen) {
        case 1:
            screen_1();
            next = 2;
            break;
        case 2:
            screen_2(&next);
            break;
        case 3:
            screen_3();
            next = 1;
            break;
        default:
            ;
        }

        screen = next;
    }

    /* exit cleanly --------------------------------------------------------- */
    if (status.started) {
        stop();
    }

    if (status.loaded) {
        unload();
    }

    BUG_ON(psgc_exit(psgc) == -1);

    return 0;
}
