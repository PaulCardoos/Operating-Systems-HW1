/*********************************************************************
*
*       file:           tty.c
*       author:         betty o'neil
*
*       tty driver--device-specific routines for ttys
*
*
*
*
*
*
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

Queue RX_queue, TX_queue, ECHO_queue; /* declare queues globally*/

void ttyinit(int dev)
{
  int baseport;
  struct tty *tty;		/* ptr to tty software params/data block */

  /* Initialize queues */
  init_queue(&RX_queue, MAXBUF);
  init_queue(&TX_queue, MAXBUF);
  init_queue(&ECHO_queue, MAXBUF);

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
  int baseport;
  struct tty *tty;
  int ch;
  char log[BUFLEN];
  int saved_eflags;        /* old cpu control/status reg, so can restore it */
  int i = 0;

  baseport = devtab[dev].dvbaseport; /* hardware addr from devtab */
  tty = (struct tty *)devtab[dev].dvdata;   /* software data for line */

  /* In this function we are reading items off the Q */
  while (i < nchar) {
    saved_eflags = get_eflags();
    cli();			                   /* disable ints in CPU */
    if((ch = dequeue(&RX_queue)) != EMPTYQUE){
      buf[i] = ch;
      sprintf(log, ">%c", buf[i]);
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
  int baseport;
  struct tty *tty;
  char log[BUFLEN];
  int i = 0;

  baseport = devtab[dev].dvbaseport; /* hardware addr from devtab */
  tty = (struct tty *)devtab[dev].dvdata;   /* software data for line */

  cli();
  while (i < nchar) {
    //enqueue returns FULLQUE if queue is full
    if(enqueue(&TX_queue, buf[i]) != FULLQUE){
        //take a byte from buf and enqueue in TX
        outpt(baseport+UART_IER, UART_IER_THRI);
        // kick start TX interrupt
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

// void irqinthandc(int dev){
//   int ch;
//   struct tty *tty = (struct tty *)(devtab[dev].dvdata);
//   int baseport = devtab[dev].dvbaseport; /* hardware i/o port */;
//   unsigned char lsr;
//
//   pic_end_int();                /* notify PIC that its part is done */
//   debug_log("*");
//   lsr = inpt(baseport+UART_LSR);
//
//   if (lsr & UART_LSR_DR) {		/* handling recieve interrupt */
//     ch = inpt(baseport+UART_RX);	/* read char, ask the device */
//     enqueue(&RX_queue, ch);
//     if (tty->echoflag)	enqueue(&ECHO_queue, ch);
//   }
//   if (lsr & UART_LSR_THRE) {	/* handling transmit interrupt */
//     if (queuecount(&ECHO_queue))	outpt(baseport+UART_TX, dequeue(&ECHO_queue));
//     if (queuecount(&TX_queue)) {
//      ch = dequeue(&TX_queue);
//      outpt(baseport + UART_TX, ch);
//     }
//   }
//   outpt(baseport+UART_IER, UART_IER_RDI);
//
// }

void irqinthandc(int dev){
  int ch;
  struct tty *tty = (struct tty *)(devtab[dev].dvdata);
  int baseport = devtab[dev].dvbaseport; /* hardware i/o port */;
  int iir = inpt(baseport+UART_IIR);

  pic_end_int();                /* notify PIC that its part is done */
  debug_log("*");

  if (iir & UART_IIR_RDI) {
    ch = inpt(baseport+UART_RX);
    enqueue(&RX_queue, ch); // add to input queue
    if (tty->echoflag) enqueue(&ECHO_queue, ch); // add to echo queue
  }

  if (iir & UART_IIR_THRI) {
    // check echo queue and echo characters on TX ISR
    if (queuecount(&ECHO_queue)) outpt(baseport+UART_TX, dequeue(&ECHO_queue));
    if (queuecount(&TX_queue)) {
      ch = dequeue(&TX_queue);
      outpt(baseport+UART_TX, ch);
    }
  }
  outpt(baseport+UART_IER, UART_IER_RDI);
  /* enable receiver interrupts again*/
}

/* append msg to memory log */
void debug_log(char *msg)
{
    strcpy(debug_record, msg);
    debug_record +=strlen(msg);
}
