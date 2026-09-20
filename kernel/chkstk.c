/*
 * Freestanding builds do not link the MinGW runtime.  GCC may still emit a
 * stack-probe call for the bounded VFS scratch buffers; the kernel has a
 * permanently mapped stack, so no probing is required.
 */
void __chkstk_ms(void) { }
void ___chkstk_ms(void) { }
