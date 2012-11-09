#ifndef SPEED_H
#define SPEED_H

/*
 * speed_init()
 *
 * start speed sensor thread, which will save nanosecond timestamp in a
 * "./speed" text file and compute instant and average speed, each time the
 * sensor detects a wheel rotation
 *
 * returns -1 if:
 *  - speed sensor thread is already running
 *  - "./speed" text file could not be opened for writing
 */

int speed_init(void);

/*
 * speed_exit()
 *
 * stop speed sensor thread, which will close the "./speed" text file
 *
 * returns -1 if:
 *  - speed sensor thread is not running
 */

int speed_exit(void);

/*
 * speed_get_instant()
 *
 * write instant speed (in Hz) to dest
 *
 * returns -1 if:
 *  - speed sensor thread is not running
 */

int speed_get_instant(double * dest);

/*
 * speed_get_average()
 *
 * write average speed (in Hz) to dest
 *
 * returns -1 if:
 *  - speed sensor thread is not running
 */

int speed_get_average(double * dest);

#endif
