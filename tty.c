/*********************************************************************
*
*       file:           tty.c
*       author:         betty o'neil
*                       paul cardoos
*                       luke vu
*
*       tty driver--device-specific routines for ttys
*
*       2/24/2021 - implemented writes with interrupts
*                 - implemented read/writes with queues
*
*/
#include <stdio.h>  /* for kprintf prototype */
#include <serial.h>
#include <cpu.h>
#include <pic.h>
#include "ioconf.h"
#include "tty_public.h"
#include "tty.h"
#include "queue/queue.h" /* import queue data structure */

struct tty ttytab[NTTYS];        /* software params/data for each SLU dev */

/* Record debug info in otherwise free memory between program and stack */
/* 0x300000 = 3M, the start of the last M of user memory on the SAPC */
#define DEBUG_AREA 0x300000
#define BUFLEN 20

char *debug_log_area = (char *)DEBUG_AREA;
char *debug_record;  /* current pointer into log area */

/* declare queues*/
Queue inQueue, outQueue, echoQueue;

/* tell C about the assembler shell routines */
extern void irq3inthand(void), irq4inthand(void);

/* C part of interrupt handlers--specific names called by the assembler code */
extern void irq3inthandc(void), irq4inthandc(void);

/* the common code for the two interrupt handlers */
static void irqinthandc(int dev);

/* prototype for debug_log */
void debug_log(char *);

/*====================================================================
*       tty specific initialization routine for COM devices         *
====================================================================*/

void ttyinit(int dev)
{
  int baseport;
  struct tty *tty;		/* ptr to tty software params/data block */

  /* Initialize queues */
  init_queue(&inQueue, MAXBUF);
  init_queue(&outQueue, MAXBUF);
  init_queue(&echoQueue, MAXBUF);

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

  /* enable interrupts on receiver */
  outpt(baseport+UART_IER, UART_IER_RDI); /* RDI = receiver data int */
}

/*====================================================================
*       tty-specific read routine for TTY devices
====================================================================*/

int ttyread(int dev, char *buf, int nchar)
{
  int ch, saved_eflags, i;
  char log[BUFLEN];

  i = 0;

  while (i < nchar) {
    /* Loop indefinetely until nchar are entered in */
    saved_eflags = get_eflags();
    cli();			                   /* disable ints in CPU */
    if((ch = dequeue(&inQueue)) != EMPTYQUE){
      buf[i] = ch;
      sprintf(log, ">%c", buf[i]); /* record input char-- */
      debug_log(log);
      i++;
    }
    set_eflags(saved_eflags);     /* back to previous CPU int. status */
  }
  return nchar;
}

/*====================================================================
*
*       tty-specific write routine for SAPC devices
*       (cs444: note that the buffer tbuf is superfluous in this code, but
*        it still gives you a hint as to what needs to be done for
*        the interrupt-driven case)
*
====================================================================*/

int ttywrite(int dev, char *buf, int nchar)
{
  int baseport, i;
  char log[BUFLEN];

  baseport = devtab[dev].dvbaseport; /* hardware addr from devtab */
  i = 0;

  cli();
  while (i < nchar) {
    if(enqueue(&outQueue, buf[i]) != FULLQUE){
        outpt(baseport+UART_IER, UART_IER_THRI);
        /* kick start TX interrupt */
        sprintf(log,"<%c", buf[i]); /* record input char-- */
        debug_log(log);
        i++;
    }
    sti();
  }
  return nchar;
}

/*====================================================================
*       tty-specific control routine for TTY devices
====================================================================*/

int ttycontrol(int dev, int fncode, int val)
{
  struct tty *this_tty = (struct tty *)(devtab[dev].dvdata);

  if (fncode == ECHOCONTROL)
    this_tty->echoflag = val;
  else return -1;
  return 0;
}

/*====================================================================
*       tty-specific interrupt routine for COM ports
*
*   Since interrupt handlers don't have parameters, we have two different
*   handlers.  However, all the common code has been placed in a helper
*   function.
====================================================================*/

void irq4inthandc()
{
  irqinthandc(TTY0);
}

void irq3inthandc()
{
  irqinthandc(TTY1);
}

void irqinthandc(int dev){
  int ch, baseport, iir;

  struct tty *tty = (struct tty *)(devtab[dev].dvdata);

  baseport = devtab[dev].dvbaseport; /* hardware i/o port */;
  iir = inpt(baseport+UART_IIR);

  pic_end_int();                /* notify PIC that its part is done */
  debug_log("*");

  switch (iir & UART_IIR_ID) {
    case UART_IIR_RDI:
      ch = inpt(baseport+UART_RX);
      enqueue(&inQueue, ch); // add to input queue
      if (tty->echoflag)
        enqueue(&echoQueue, ch); // add to echo queue

    case UART_IIR_THRI:
      if (queuecount(&echoQueue))
        outpt(baseport+UART_TX, dequeue(&echoQueue));
      if (queuecount(&outQueue)) {
        outpt(baseport+UART_TX, dequeue(&outQueue));
      }
        break;

    default:
      debug_log("#");
  }
  outpt(baseport+UART_IER, UART_IER_RDI); /* enable receiver interrupts again*/
}

/* append msg to memory log */
void debug_log(char *msg)
{
    strcpy(debug_record, msg);
    debug_record +=strlen(msg);
}
