/* masks.h */

#ifdef __ASSEMBLER__

#else /* not __ASSEMBLER__ (C/C++) */


extern long long int _DYNAMIC[];
long long int * LIBM __attribute__((visibility("default"),weak)) = _DYNAMIC;
extern long long int or_mask __attribute__((visibility("hidden")));
extern long long int and_mask __attribute__((visibility("hidden")));

extern void * libm_callback_table;

#endif // __ASSEMBLER__