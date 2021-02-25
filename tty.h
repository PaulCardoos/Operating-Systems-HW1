/*********************************************************************
*
*       file:           tty.h
*       author:         betty o'neil
*
*       private header file for tty drivers
*       apps should not include this header
*
*       2/23/2021
*       imported queue
*       removed uncessary circular buffer variables
*
*
*/

#ifndef TTY_H
#define TTY_H
#define MAXBUF 6

struct tty {
  int echoflag;			/* echo chars in read */
};

extern struct tty ttytab[];

/* tty-specific device functions */
void ttyinit(int dev);
int ttyread(int dev, char *buf, int nchar);
int ttywrite(int dev, char *buf, int nchar);
int ttycontrol(int dev, int fncode, int val);

#endif
