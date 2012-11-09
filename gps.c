#include "gps.h"

#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <xenomai/native/mutex.h>
#include <xenomai/native/task.h>
#include <xenomai/native/timer.h>

/* private variables ======================================================== */
static int running;

static FILE * istream;
static FILE * ostream;
static struct termios termios;
static struct termios otermios;
static RT_MUTEX mutex;
static RT_TASK task;

static char frame[GPS_FRAME_SIZE];

/* private functions ======================================================== */
static void task_routine(void * cookie) {
    char buffer[GPS_FRAME_SIZE];

    while (1) {
        fscanf(istream, "%s", buffer);

        if (strncmp(buffer, "$GPGGA", 6) == 0) {
            rt_mutex_acquire(&mutex, TM_INFINITE);
            strcpy(frame, buffer);
            rt_mutex_release(&mutex);

            fprintf(ostream, "%s,%llu\n", buffer, rt_timer_read());
        }
    }

    (void) cookie;
}

/* public functions ========================================================= */
int gps_init(void) {
    if (running) {
        goto err_running;
    }

    if ((istream = fopen("/dev/ttyUSB0", "r")) == NULL) {
        goto err_istream;
    }

    if ((ostream = fopen("gps", "w")) == NULL) {
        goto err_ostream;
    }

    tcgetattr(fileno(istream), &otermios);
    termios = otermios;
    cfsetspeed(&termios, B4800);
    tcsetattr(fileno(istream), TCSANOW, &termios);

    rt_mutex_create(&mutex, NULL);
    rt_task_spawn(&task, NULL, 0, 80, 0, task_routine, NULL);

    strcpy(frame, "");

    running = 1;

    return 0;

err_ostream:
    fclose(istream);

err_istream:
err_running:
    return -1;
}

int gps_exit(void) {
    if (!running) {
        goto err_not_running;
    }

    running = 0;

    rt_task_delete(&task);
    rt_mutex_delete(&mutex);

    tcsetattr(fileno(istream), TCSANOW, &otermios);

    fclose(ostream);
    fclose(istream);

    return 0;

err_not_running:
    return -1;
}

int gps_get_frame(char * dest) {
    if (!running) {
        goto err_not_running;
    }

    rt_mutex_acquire(&mutex, TM_INFINITE);
    strcpy(dest, frame);
    rt_mutex_release(&mutex);

    return 0;

err_not_running:
    return -1;
}
