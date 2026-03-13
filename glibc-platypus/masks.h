/* masks.h */

#ifdef __ASSEMBLER__


#else /* not __ASSEMBLER__ (C/C++) */


extern long long int _DYNAMIC[];
long long int * LIBC __attribute__((visibility("default"),weak)) = _DYNAMIC;
extern long long int or_mask __attribute__((visibility("hidden")));
extern long long int and_mask __attribute__((visibility("hidden")));

extern void * callback_table;
extern void * threadkey_callback_table;
extern void placeholder1(void) __attribute__((section(".fakeplt.sec")));
extern void placeholder2(void) __attribute__((section(".fakeplt.sec")));
extern void placeholder3(void) __attribute__((section(".fakeplt.sec")));
extern void placeholder4(void) __attribute__((section(".fakeplt.sec")));
extern void placeholder5(void) __attribute__((section(".fakeplt.sec")));
extern void placeholder6(void) __attribute__((section(".fakeplt.sec")));
extern void placeholder7(void) __attribute__((section(".fakeplt.sec")));
extern void placeholder8(void) __attribute__((section(".fakeplt.sec")));
extern void placeholder9(void) __attribute__((section(".fakeplt.sec")));
extern void placeholder10(void) __attribute__((section(".fakeplt.sec")));

#endif // __ASSEMBLER__