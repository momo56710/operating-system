#pragma once

int mm_selftest_quick(void);   /* PMM, VMM and a small heap check; runs on every boot */
int mm_selftest_full(void);    /* heap stress test and the 100 MiB cycle; make iso EXTRA=-DMM_TEST */
