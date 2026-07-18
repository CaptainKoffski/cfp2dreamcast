#include "shim_iface.h"
void shim_die(unsigned int, unsigned int, unsigned int);
/* shim_cart_service now lives in src/cart.c (Task 10) */
void shim_maple_entry(void)  { shim_die(0x11, 0, 0); }  /* Task 11 */
