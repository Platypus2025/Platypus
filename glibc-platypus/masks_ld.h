/* masks.h */

#ifdef __ASSEMBLER__


#else /* not __ASSEMBLER__ (C/C++) */


extern long long int _DYNAMIC[];
long long int * LOD __attribute__((visibility("default"),weak)) = _DYNAMIC;
extern void * fini_callback_table;

#endif // __ASSEMBLER__