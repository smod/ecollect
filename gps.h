#ifndef GPS_H
#define GPS_H

#define GPS_FRAME_SIZE 128

/*
 * gps_init()
 *
 * start GPS sensor thread, which will save NMEA $GPGGA frames followed by
 * ",%llu", where %llu is nanosecond timestamp, in a "./gps" text file.
 *
 * returns -1 if:
 *  - gps sensor thread is already running
 *  - "/dev/ttyUSB0" device file could not be opened for reading
 *  - "./gps" text file could not be opened for writing
 */

int gps_init(void);

/*
 * gps_exit()
 *
 * stop gps sensor thread, and close the "./gps" text file
 *
 * returns -1 if:
 *  - gps sensor thread is not running
 */

int gps_exit(void);

/*
 * gps_get_frame()
 *
 * write current NMEA $GPGGA to dest, which size is max GPS_FRAME_SIZE
 *
 * returns -1 if:
 *  - gps sensor thread is not running
 */

int gps_get_frame(char * dest);

#endif
