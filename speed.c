#include "speed.h"

#include <stdio.h>
#include <xenomai/native/intr.h>
#include <xenomai/native/mutex.h>
#include <xenomai/native/queue.h>
#include <xenomai/native/task.h>
#include <xenomai/native/timer.h>

/* constants ================================================================ */
#define QUEUE_SIZE (64 * sizeof (RTIME))

/* private variables ======================================================== */
static int running;

static FILE * ostream;
static RT_INTR intr;
static RT_MUTEX mutex_instant;
static RT_MUTEX mutex_average;
static RT_QUEUE queue;
static RT_TASK task_soft;
static RT_TASK task_hard;

static double instant;
static double average;

/* private functions ======================================================== */
static void task_soft_routine(void * cookie) {
    unsigned long n = 0; /* how many wheel rotations since init? */
    RTIME time_init = 0; /* when did we start? */
    RTIME time_prev = 0; /* when was the previous rotation? */
    RTIME time_curr = 0; /* when was the current rotation? */

    /* we start now! (first wheel rotation) */
    rt_queue_read(&queue, &time_init, sizeof time_curr, TM_INFINITE);
 
    /* previous rotation is now! (init value) */ 
    time_prev = time_init;

    while (1) {
        /* extract the current rotation timestamp from message queue */
        rt_queue_read(&queue, &time_curr, sizeof time_curr, TM_INFINITE);

        /* 
           Let's compute instant speed. We want an Hz value, we've got previous 
           rotation timestamp and current rotation timestamp, substracting them
           give us the time elapsed since previous rotation in nanoseconds. 
           Hz = 1 / s, so Hz = 10^9 / ns, our final value is 10^9 / dt
        */
        rt_mutex_acquire(&mutex_instant, TM_INFINITE);
        instant = 1e9 / (time_curr - time_prev);
        rt_mutex_release(&mutex_instant);

	/*
           Let's compute average speed. We want an Hz value, we've got init
           rotation timestamp and current rotation timestamp, substracting
           them gives us the time elapsed since init rotation in nanoseconds.
           ++n is the number of wheel rotations since init.
           Hz = 1 / s, so Hz = 10^9 / ns, our final value is 10^9 * dx / dt
        */
        rt_mutex_acquire(&mutex_average, TM_INFINITE);
        average = 1e9 * ++n / (time_curr - time_init);
        rt_mutex_release(&mutex_average);

        /* dump current timestamp to file */
        fprintf(ostream, "%llu\n", time_curr);

        /* our job is done, we are now the previous rotation */ 
        time_prev = time_curr;
    }

    (void) cookie;
}

static void task_hard_routine(void * cookie) {
    unsigned long n = 0; /* how many IRQ since init? */
    RTIME time_prev = 0; /* when was the previous IRQ? */
    RTIME time_curr = 0; /* when was the current IRQ? */

    /* last IRQ is now! (init value) */
    time_prev = rt_timer_read();

    while (1) {
        /* wait for an IRQ */
        rt_intr_wait(&intr, TM_INFINITE);

        /* fetch current timestamp */
        time_curr = rt_timer_read();

        /*
           Ok, now we've got to check two things:
           - is it a "good transition" (edge triggering, two IRQ = 1 hit)?
           - is it a "real wheel rotation" (our crappy sensor wire sometimes
             decides to become an antenna, so here's a nasty software filter...)
        */
#define EPSILON 100 * 1000 * 1000
        if (++n % 2 && time_curr > time_prev + EPSILON) {
#undef  EPSILON
            /* post the current timestamp to the message queue */
            rt_queue_write(&queue, &time_curr, sizeof time_curr, Q_NORMAL);
            
            /* our job is done, we are now the previous IRQ */
            time_prev = time_curr;
        }
    }

    (void) cookie;
}

/* public functions ========================================================= */
int speed_init(void) {
    if (running) {
        goto err_running;
    }

    if ((ostream = fopen("speed", "w")) == NULL) {
        goto err_ostream;
    }

    rt_intr_create(&intr, NULL, 81, 0);
    rt_intr_enable(&intr);
    rt_mutex_create(&mutex_instant, NULL);
    rt_mutex_create(&mutex_average, NULL);
    rt_queue_create(&queue, "irq81", QUEUE_SIZE, Q_UNLIMITED, Q_FIFO);
    rt_task_spawn(&task_soft, NULL, 0, 80, 0, task_soft_routine, NULL);
    rt_task_spawn(&task_hard, NULL, 0, 90, 0, task_hard_routine, NULL);

    instant = 0;
    average = 0;

    running = 1;

    return 0;

err_ostream:
err_running:
    return -1;
}

int speed_exit(void) {
    if (!running) {
        goto err_not_running;
    }

    running = 0;

    rt_task_delete(&task_hard);
    rt_task_delete(&task_soft);
    rt_queue_delete(&queue);
    rt_mutex_delete(&mutex_average);
    rt_mutex_delete(&mutex_instant);
    rt_intr_disable(&intr);
    rt_intr_delete(&intr);

    fclose(ostream);

/* START DEBUG DEBUG DEBUG */
    {
        FILE * fp = fopen("average", "w");
        fprintf(fp, "%f\n", average);
        fclose(fp);
    }
/* STOP DEBUG DEBUG DEBUG */

    return 0;

err_not_running:
    return -1;
}

int speed_get_instant(double * dest) {
    if (!running) {
        goto err_not_running;
    }

    rt_mutex_acquire(&mutex_instant, TM_INFINITE);
    *dest = instant;
    rt_mutex_release(&mutex_instant);

    return 0;

err_not_running:
    return -1;
}


int speed_get_average(double * dest) {
    if (!running) {
        goto err_not_running;
    }

    rt_mutex_acquire(&mutex_average, TM_INFINITE);
    *dest = average;
    rt_mutex_release(&mutex_average);

    return 0;

err_not_running:
    return -1;
}
