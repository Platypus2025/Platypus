/* masks.h */

#ifdef __ASSEMBLER__

    /* Assembly code ... */

#else /* not __ASSEMBLER__ (C/C++) */


//extern long long unsigned int MY_LIBC_BASE;
extern long long int _DYNAMIC[];
extern long long int * LIBC __attribute__((visibility("default")));
//extern long long int or_mask __attribute__((visibility("hidden")));
//extern void * callback_table;

#endif // __ASSEMBLER__



/*
#ifdef _LIBC

#  if IS_IN(rtld)
	long long int or_mask __attribute__((visibility("hidden"))) = 0;
#else
	extern long long int or_mask __attribute__((visibility("hidden")));
#  endif
#endif
*/
