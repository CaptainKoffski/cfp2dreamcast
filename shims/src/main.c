#include "shim_iface.h"
void shim_die(unsigned int, unsigned int, unsigned int);
void shim_cart_service(void) { shim_die(0x10, 0, 0); }  /* Task 10 */
void shim_maple_entry(void)  { shim_die(0x11, 0, 0); }  /* Task 11 */
