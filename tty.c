/*********************************************************************
*
*       file:           tty.c
*       author:         betty o'neil
*
*       tty driver--device-specific routines for ttys 
*
*/
#include <stdio.h>  /* for kprintf prototype */
#include <serial.h>
#include <cpu.h>
#include <pic.h>
#include "ioconf.h"
#include "tty_public.h"
#include "tty.h"

struct tty ttytab[NTTYS];        /* software params/data for each SLU dev */

/* Record debug info in otherwise free memory between program and stack */
/* 0x300000 = 3M, the start of the last M of user memory on the SAPC */
#define DEBUG_AREA 0x300000
#define BUFLEN 20

char *debug_log_area = (char *)DEBUG_AREA;
char *debug_record;  /* current pointer into log area */ 

/* tell C about the assembler shell routines */
extern void irq3inthand(void), irq4inthand(void);

/* C part of interrupt handlers--specific names called by the assembler code */
extern void irq3inthandc(void), irq4inthandc(void); 

/* the common code for the two interrupt handlers */                           static void irqinthandc(int dev);

/* prototype for debug_log */ 
void debug_log(char *);

/*====================================================================
*
*       tty specific initialization routine for COM devices
*
*/

void ttyinit(int dev)
{
  int baseport;
  struct tty *tty;		/* ptr to tty software params/data block */

  debug_record = debug_log_area; /* clear debug log */
  baseport = devtab[dev].dvbaseport; /* pick up hardware addr */
  tty = (struct tty *)devtab[dev].dvdata; /* and software params struct */

  if (baseport == COM1_BASE) {
      /* arm interrupts by installing int vec */
      set_intr_gate(COM1_IRQ+IRQ_TO_INT_N_SHIFT, &irq4inthand);
      pic_enable_irq(COM1_IRQ);
  } else if (baseport == COM2_BASE) {
      /* arm interrupts by installing int vec */
      set_intr_gate(COM2_IRQ+IRQ_TO_INT_N_SHIFT, &irq3inthand);
      pic_enable_irq(COM2_IRQ);
  } else {
      kprintf("Bad TTY device table entry, dev %d\n", dev);
      return;			/* give up */
  }
  tty->echoflag = 1;		/* default to echoing */
  tty->rin = 0;               /* initialize indices */
  tty->rout = 0;
  tty->rnum = 0;              /* initialize counter */
  tty->tin = 0;               /* initialize indices */
  tty->tout = 0;
  tty->tnum = 0;              /* initialize counter */

  /* enable interrupts on receiver */
  outpt(baseport+UART_IER, UART_IER_RDI); /* RDI = receiver data int */
}


/*====================================================================
*
*       Useful function when emptying/filling the read/write buffers
*
*/

#define min(x,y) (x < y ? x : y)


/*====================================================================
*
*       tty-specific read routine for TTY devices
*
*/

int ttyread(int dev, char *buf, int nchar)
{
  int baseport;
  struct tty *tty;
  int i, copychars;

  char log[BUFLEN];
  int saved_eflags;        /* old cpu control/status reg, so can restore it */

  baseport = devtab[dev].dvbaseport; /* hardware addr from devtab */
  tty = (struct tty *)devtab[dev].dvdata;   /* software data for line */

  copychars = min(nchar, tty->rnum);      /* chars to copy from buffer */
  for (i = 0; i < copychars; i++) {
    saved_eflags = get_eflags();
    cli();			/* disable ints in CPU */
    buf[i] = tty -> rbuf[tty -> rout++];      /* copy from ibuf to user buf */
    sprintf(log, ">%c", buf[i]);
    debug_log(log);
    tty -> rnum--;                          /* decrement count */
    if (tty -> rout >= MAXBUF) tty -> rout = 0;
    set_eflags(saved_eflags);     /* back to previous CPU int. status */
  }

  return copychars;       /* but should wait for rest of nchar chars if nec. */
  /* this is something for you to correct */
}


/*====================================================================
*
*       tty-specific write routine for SAPC devices
*       (cs444: note that the buffer tbuf is superfluous in this code, but
*        it still gives you a hint as to what needs to be done for
*        the interrupt-driven case)
*
*/

int ttywrite(int dev, char *buf, int nchar)
{
  int baseport;
  struct tty *tty;
  int i;
  char log[BUFLEN];

  baseport = devtab[dev].dvbaseport; /* hardware addr from devtab */
  tty = (struct tty *)devtab[dev].dvdata;   /* software data for line */

  for (i = 0; i < nchar; i++) {
    sprintf(log,"<%c", buf[i]); /* record input char-- */
    debug_log(log);
    tty -> tbuf[tty -> tin++] = buf[i];
    tty -> tnum++;
    if (tty -> tin >= MAXBUF) tty -> tin = 0;
    putc(dev + 1, buf[i]);	/* we will delete this function and make sure it works with an interruptt-----------------use lib for now--replace this! */
  }
  return nchar;
}

/*====================================================================
*
*       tty-specific control routine for TTY devices
*
*/

int ttycontrol(int dev, int fncode, int val)
{
  struct tty *this_tty = (struct tty *)(devtab[dev].dvdata);

  if (fncode == ECHOCONTROL)
    this_tty->echoflag = val;
  else return -1;
  return 0;
}



/*====================================================================
*
*       tty-specific interrupt routine for COM ports
*
*   Since interrupt handlers don't have parameters, we have two different
*   handlers.  However, all the common code has been placed in a helper 
*   function.
*/
  
void irq4inthandc()
{
  irqinthandc(TTY0);
}                              
  
void irq3inthandc()
{
  irqinthandc(TTY1);
}                              

void irqinthandc(int dev){  
  int ch;
  struct tty *tty = (struct tty *)(devtab[dev].dvdata);
  int baseport = devtab[dev].dvbaseport; /* hardware i/o port */;

  pic_end_int();                /* notify PIC that its part is done */
  debug_log("*");
  ch = inpt(baseport+UART_RX);	/* read char, ack the device */
  if (tty->rnum < MAXBUF) {   /* if space left in ring buffer */
    tty->rnum++;                 /* increase character count */
    tty->rbuf[tty->rin++] = ch; /* put char in ibuf, step ptr */
    if (tty->rin >= MAXBUF)     /* check if we need to wrap-around */
      tty->rin = 0;              /* and reset as appropriate */
  }
  if (tty->echoflag)             /* if echoing wanted */
    outpt(baseport+UART_TX,ch);   /* echo char: see note above */
}

/* append msg to memory log */
void debug_log(char *msg)
{
    strcpy(debug_record, msg);
    debug_record +=strlen(msg);
}

