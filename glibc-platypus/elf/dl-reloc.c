/* Relocate a shared object and resolve its references to other loaded objects.
   Copyright (C) 1995-2025 Free Software Foundation, Inc.
   Copyright The GNU Toolchain Authors.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <https://www.gnu.org/licenses/>.  */

#include <errno.h>
#include <libintl.h>
#include <stdlib.h>
#include <unistd.h>
#include <ldsodefs.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/types.h>
#include <_itoa.h>
#include <libc-pointer-arith.h>
#include "dynamic-link.h"

/* Statistics function.  */
#ifdef SHARED
# define bump_num_cache_relocations() ++GL(dl_num_cache_relocations)
#else
# define bump_num_cache_relocations() ((void) 0)
#endif

/* We are trying to perform a static TLS relocation in MAP, but it was
   dynamically loaded.  This can only work if there is enough surplus in
   the static TLS area already allocated for each running thread.  If this
   object's TLS segment is too big to fit, we fail with -1.  If it fits,
   we set MAP->l_tls_offset and return 0.
   A portion of the surplus static TLS can be optionally used to optimize
   dynamic TLS access (with TLSDESC or powerpc TLS optimizations).
   If OPTIONAL is true then TLS is allocated for such optimization and
   the caller must have a fallback in case the optional portion of surplus
   TLS runs out.  If OPTIONAL is false then the entire surplus TLS area is
   considered and the allocation only fails if that runs out.  */
int
_dl_try_allocate_static_tls (struct link_map *map, bool optional)
{
  /* If we've already used the variable with dynamic access, or if the
     alignment requirements are too high, fail.  */
  if (map->l_tls_offset == FORCED_DYNAMIC_TLS_OFFSET
      || map->l_tls_align > GLRO (dl_tls_static_align))
    {
    fail:
      return -1;
    }

#if TLS_TCB_AT_TP
  size_t freebytes = GLRO (dl_tls_static_size) - GL(dl_tls_static_used);
  if (freebytes < TLS_TCB_SIZE)
    goto fail;
  freebytes -= TLS_TCB_SIZE;

  size_t blsize = map->l_tls_blocksize + map->l_tls_firstbyte_offset;
  if (freebytes < blsize)
    goto fail;

  size_t n = (freebytes - blsize) / map->l_tls_align;

  /* Account optional static TLS surplus usage.  */
  size_t use = freebytes - n * map->l_tls_align - map->l_tls_firstbyte_offset;
  if (optional && use > GL(dl_tls_static_optional))
    goto fail;
  else if (optional)
    GL(dl_tls_static_optional) -= use;

  size_t offset = GL(dl_tls_static_used) + use;

  map->l_tls_offset = GL(dl_tls_static_used) = offset;
#elif TLS_DTV_AT_TP
  /* dl_tls_static_used includes the TCB at the beginning.  */
  size_t offset = (ALIGN_UP(GL(dl_tls_static_used)
			    - map->l_tls_firstbyte_offset,
			    map->l_tls_align)
		   + map->l_tls_firstbyte_offset);
  size_t used = offset + map->l_tls_blocksize;

  if (used > GLRO (dl_tls_static_size))
    goto fail;

  /* Account optional static TLS surplus usage.  */
  size_t use = used - GL(dl_tls_static_used);
  if (optional && use > GL(dl_tls_static_optional))
    goto fail;
  else if (optional)
    GL(dl_tls_static_optional) -= use;

  map->l_tls_offset = offset;
  map->l_tls_firstbyte_offset = GL(dl_tls_static_used);
  GL(dl_tls_static_used) = used;
#else
# error "Either TLS_TCB_AT_TP or TLS_DTV_AT_TP must be defined"
#endif

  /* If the object is not yet relocated we cannot initialize the
     static TLS region.  Delay it.  */
  if (map->l_real->l_relocated)
    {
#ifdef SHARED
      /* Update the DTV of the current thread.  Note: GL(dl_load_tls_lock)
	 is held here so normal load of the generation counter is valid.  */
      if (__builtin_expect (THREAD_DTV()[0].counter != GL(dl_tls_generation),
			    0))
	(void) _dl_update_slotinfo (map->l_tls_modid, GL(dl_tls_generation));
#endif

      dl_init_static_tls (map);
    }
  else
    map->l_need_tls_init = 1;

  return 0;
}

/* This function intentionally does not return any value but signals error
   directly, as static TLS should be rare and code handling it should
   not be inlined as much as possible.  */
void
__attribute_noinline__
_dl_allocate_static_tls (struct link_map *map)
{
  if (map->l_tls_offset == FORCED_DYNAMIC_TLS_OFFSET
      || _dl_try_allocate_static_tls (map, false))
    {
      _dl_signal_error (0, map->l_name, NULL, N_("\
cannot allocate memory in static TLS block"));
    }
}

#if !PTHREAD_IN_LIBC
/* Initialize static TLS area and DTV for current (only) thread.
   libpthread implementations should provide their own hook
   to handle all threads.  */
void
_dl_nothread_init_static_tls (struct link_map *map)
{
#if TLS_TCB_AT_TP
  void *dest = (char *) THREAD_SELF - map->l_tls_offset;
#elif TLS_DTV_AT_TP
  void *dest = (char *) THREAD_SELF + map->l_tls_offset + TLS_PRE_TCB_SIZE;
#else
# error "Either TLS_TCB_AT_TP or TLS_DTV_AT_TP must be defined"
#endif

  /* Initialize the memory.  */
  memset (__mempcpy (dest, map->l_tls_initimage, map->l_tls_initimage_size),
	  '\0', map->l_tls_blocksize - map->l_tls_initimage_size);
}
#endif /* !PTHREAD_IN_LIBC */

static uint8_t search_dlsymed = 1;

/*
 * Given a link_map, get pointer to the dynamic symtab and count of symbols.
 * Returns 0 (and sets *dynsym_ret to NULL) on failure.
 * Sets *dynsym_ret to the ElfW(Sym)* for symbol table if found.
 */
size_t get_dynsymtab_and_count_from_link_map(struct link_map *lmap, const ElfW(Sym) **dynsym_ret) {
    ElfW(Dyn) *dyn = lmap->l_ld;
    uintptr_t hash_addr = 0, gnu_hash_addr = 0;
    const ElfW(Sym) *syms = NULL;
    size_t nsyms = 0;
    // Find SYMTAB, HASH, and GNU_HASH
    for (; dyn->d_tag != DT_NULL; ++dyn) {
        if (dyn->d_tag == DT_SYMTAB)
            syms = (const ElfW(Sym) *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_HASH)
            hash_addr = dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_GNU_HASH)
            gnu_hash_addr = dyn->d_un.d_ptr;
    }
    if (dynsym_ret)
        *dynsym_ret = syms;

    if (hash_addr) {
        // SysV: 2nd word is nsyms
        const uint32_t *hashtab = (const uint32_t *)hash_addr;
        if (hashtab)
            nsyms = hashtab[1];
    } else if (gnu_hash_addr) {
        // GNU hash: must compute max symbol index used
        // Layout: nbuckets, symoffset, bloom_size, bloom_shift, ... (buckets), (chains)
        const uint32_t *gnu = (const uint32_t *)gnu_hash_addr;
        uint32_t nbuckets = gnu[0];
        uint32_t symoffset = gnu[1];
        // uint32_t bloom_size = gnu[2]; -- unused here
        // uint32_t bloom_shift = gnu[3];

        const uint32_t *buckets = gnu + 4 + (sizeof(uintptr_t)/4) * gnu[2];
        uint32_t max_sym = 0;
        for (uint32_t i = 0; i < nbuckets; i++) {
            if (buckets[i] > max_sym)
                max_sym = buckets[i];
        }
        const uint32_t *chains = buckets + nbuckets;
        // Now walk chain, stop at the first bit set for the last bucket's chain
        if (max_sym < symoffset) {
            // No chain, minimal table with no symbols
            nsyms = symoffset;
        } else {
            for (uint32_t idx = max_sym - symoffset; ; ++idx) {
                if (chains[idx] & 1)
                {
                    // The highest symbol used in the table is symoffset + idx
                    nsyms = symoffset + idx + 1;
                    break;
                }
            }
        }
    }
    // If neither hash present, fallback -- not safe to guess!
    return nsyms;
}


static __always_inline lookup_t
resolve_map (lookup_t l, struct r_scope_elem *scope[], const ElfW(Sym) **ref,
	     const struct r_found_version *version, unsigned long int r_type)
{
  if (ELFW(ST_BIND) ((*ref)->st_info) == STB_LOCAL
      || __glibc_unlikely (dl_symbol_visibility_binds_local_p (*ref)))
    return l;

  // --- Custom logic for callback_table prefix ---
  const char *undef_name = (const char *) D_PTR(l, l_info[DT_STRTAB]) + (*ref)->st_name;
  if (search_dlsymed) {
    //_dl_debug_printf("Found %s\n", undef_name);
    // char prefix[20]; // Enough space for your pattern + any integer
    // snprintf(prefix, sizeof(prefix), "callback_table%u_", search_dlsymed);
    char prefix[32];
    char *p = stpcpy(prefix, "placeholder");
    // Let's use a basic trick for small uint8_t:
    *p++ = '0' + search_dlsymed;
    *p++ = '_';
    *p = '\0';
    size_t prefix_len = strlen(prefix);
    if (strncmp(undef_name, prefix, prefix_len-1) == 0)
    {
      //_dl_debug_printf("Found\n");
        struct link_map *main_map = GL(dl_ns)[LM_ID_BASE]._ns_loaded;
        ElfW(Sym) *syms = (ElfW(Sym) *) D_PTR(main_map, l_info[DT_SYMTAB]);
        const char *strtab = (const char *) D_PTR(main_map, l_info[DT_STRTAB]);
        size_t nsyms = 0;

        if (main_map->l_info[DT_HASH] != NULL) {
            // Use the classic ELF hash table for the symbol count
            const ElfW(Word) *hashtab = (const ElfW(Word) *) D_PTR(main_map, l_info[DT_HASH]);
            nsyms = hashtab[1];
        } else {
          const ElfW(Sym) *dynsyms;
          nsyms = get_dynsymtab_and_count_from_link_map(main_map, &dynsyms);
          //nsyms = 10000;
          //_dl_debug_printf("SYMS ARE %ld\n", (unsigned long) nsyms);
        }
        // const char *prefix = "lem";
        // size_t prefix_len = strlen(prefix);

        for (size_t i = 0; i < nsyms; ++i) {
          if (!syms[i].st_name)
              continue;
          const char *symname = strtab + syms[i].st_name;
          if (strncmp(symname, prefix, prefix_len) == 0) {
              const char *rest = symname + prefix_len;
              //_dl_debug_printf("Found prefix symbol: %s. Now searching for replacement symbol: %s\n", symname, rest);

              // Now search for symbol named 'rest' in all objects via _dl_lookup_symbol_x:
              const struct r_found_version *v = (version && version->hash != 0) ? version : NULL;
              const int tc = elf_machine_type_class(r_type);
              const ElfW(Sym) *found_ref = NULL;
              lookup_t lr = _dl_lookup_symbol_x(
                  rest,          // symbol name to look for
                  l,             // current link_map
                  &found_ref,    // OUT: pointer to ElfW(Sym) found
                  scope,         // search scope array
                  v,             // version info
                  tc,            // type_class
                  DL_LOOKUP_ADD_DEPENDENCY | DL_LOOKUP_FOR_RELOCATE, // flags match normal flow
                  NULL);

              if (lr) {
                *ref = found_ref;
                //_dl_debug_printf("Forwarding: %s -> %s in %s\nRef at 0x%lx\n", symname, rest, lr->l_name, (unsigned long)((lr->l_addr + found_ref->st_value)));

                if (search_dlsymed <= 10) {
                  strncpy(placeholder_symbols[search_dlsymed-1], rest, 47);
                  placeholder_symbols[search_dlsymed-1][47] = '\0';
                  search_dlsymed++;
                  placeholded++;
                } else {
                  _dl_debug_printf("[+] You need to upgrade the number of supported placeholder PLTs\n");
                }

                return lr;
              } else {
                  _dl_debug_printf("No object has symbol: %s\n", rest);
                  // Fall through to normal lookup for original symbol
              }
              break;
          }
        }
        search_dlsymed = 0;
      }
    }
  // --- End custom logic ---
    if (strncmp(undef_name, "placeholder", 11) == 0)
      return l;


  if (__glibc_unlikely (*ref == l->l_lookup_cache.sym)
      && elf_machine_type_class (r_type) == l->l_lookup_cache.type_class)
    {
      bump_num_cache_relocations ();
      *ref = l->l_lookup_cache.ret;
    }
  else
    {
      const int tc = elf_machine_type_class (r_type);
      l->l_lookup_cache.type_class = tc;
      l->l_lookup_cache.sym = *ref;
      const char *undef_name
	      = (const char *) D_PTR (l, l_info[DT_STRTAB]) + (*ref)->st_name;
      const struct r_found_version *v = NULL;
      if (version != NULL && version->hash != 0)
        v = version;
      //_dl_debug_printf("Handling the Symbol: %s\n", undef_name ? undef_name : "(no symbol)");
      lookup_t lr = _dl_lookup_symbol_x (
	  undef_name, l, ref, scope, v, tc,
	  DL_LOOKUP_ADD_DEPENDENCY | DL_LOOKUP_FOR_RELOCATE, NULL);
      l->l_lookup_cache.ret = *ref;
      l->l_lookup_cache.value = lr;
    }
  return l->l_lookup_cache.value;
}

/* This macro is used as a callback from the ELF_DYNAMIC_RELOCATE code.  */
#define RESOLVE_MAP resolve_map

#include "dynamic-link.h"

void
_dl_relocate_object_no_relro (struct link_map *l, struct r_scope_elem *scope[],
			      int reloc_mode, int consider_profiling)
{
  struct textrels
  {
    caddr_t start;
    size_t len;
    int prot;
    struct textrels *next;
  } *textrels = NULL;
  /* Initialize it to make the compiler happy.  */
  const char *errstring = NULL;
  int lazy = reloc_mode & RTLD_LAZY;
  int skip_ifunc = reloc_mode & __RTLD_NOIFUNC;

  bool consider_symbind = false;
#ifdef SHARED
  /* If we are auditing, install the same handlers we need for profiling.  */
  if ((reloc_mode & __RTLD_AUDIT) == 0)
    {
      struct audit_ifaces *afct = GLRO(dl_audit);
      for (unsigned int cnt = 0; cnt < GLRO(dl_naudit); ++cnt)
	{
	  /* Profiling is needed only if PLT hooks are provided.  */
	  if (afct->ARCH_LA_PLTENTER != NULL
	      || afct->ARCH_LA_PLTEXIT != NULL)
	    consider_profiling = 1;
	  if (afct->symbind != NULL)
	    consider_symbind = true;

	  afct = afct->next;
	}
    }
#elif defined PROF
  /* Never use dynamic linker profiling for gprof profiling code.  */
  consider_profiling = 0;
#endif

  /* If DT_BIND_NOW is set relocate all references in this object.  We
     do not do this if we are profiling, of course.  */
  // XXX Correct for auditing?
  if (!consider_profiling
      && __builtin_expect (l->l_info[DT_BIND_NOW] != NULL, 0))
    lazy = 0;

  if (__glibc_unlikely (GLRO(dl_debug_mask) & DL_DEBUG_RELOC))
    _dl_debug_printf ("\nrelocation processing: %s%s\n",
		      DSO_FILENAME (l->l_name), lazy ? " (lazy)" : "");

  /* DT_TEXTREL is now in level 2 and might phase out at some time.
     But we rewrite the DT_FLAGS entry to a DT_TEXTREL entry to make
     testing easier and therefore it will be available at all time.  */
  if (__glibc_unlikely (l->l_info[DT_TEXTREL] != NULL))
    {
      /* Bletch.  We must make read-only segments writable
	 long enough to relocate them.  */
      const ElfW(Phdr) *ph;
      for (ph = l->l_phdr; ph < &l->l_phdr[l->l_phnum]; ++ph)
	if (ph->p_type == PT_LOAD && (ph->p_flags & PF_W) == 0)
	  {
	    struct textrels *newp;

	    newp = (struct textrels *) alloca (sizeof (*newp));
	    newp->len = ALIGN_UP (ph->p_vaddr + ph->p_memsz, GLRO(dl_pagesize))
			- ALIGN_DOWN (ph->p_vaddr, GLRO(dl_pagesize));
	    newp->start = PTR_ALIGN_DOWN (ph->p_vaddr, GLRO(dl_pagesize))
			  + (caddr_t) l->l_addr;

	    newp->prot = 0;
	    if (ph->p_flags & PF_R)
	      newp->prot |= PROT_READ;
	    if (ph->p_flags & PF_W)
	      newp->prot |= PROT_WRITE;
	    if (ph->p_flags & PF_X)
	      newp->prot |= PROT_EXEC;

	    if (__mprotect (newp->start, newp->len, newp->prot|PROT_WRITE) < 0)
	      {
		errstring = N_("cannot make segment writable for relocation");
	      call_error:
		_dl_signal_error (errno, l->l_name, NULL, errstring);
	      }

	    newp->next = textrels;
	    textrels = newp;
	  }
    }

  {
    /* Do the actual relocation of the object's GOT and other data.  */

    ELF_DYNAMIC_RELOCATE (l, scope, lazy, consider_profiling, skip_ifunc);

    if ((consider_profiling || consider_symbind)
	&& l->l_info[DT_PLTRELSZ] != NULL)
      {
	/* Allocate the array which will contain the already found
	   relocations.  If the shared object lacks a PLT (for example
	   if it only contains lead function) the l_info[DT_PLTRELSZ]
	   will be NULL.  */
	size_t sizeofrel = l->l_info[DT_PLTREL]->d_un.d_val == DT_RELA
			   ? sizeof (ElfW(Rela))
			   : sizeof (ElfW(Rel));
	size_t relcount = l->l_info[DT_PLTRELSZ]->d_un.d_val / sizeofrel;
	l->l_reloc_result = calloc (sizeof (l->l_reloc_result[0]), relcount);

	if (l->l_reloc_result == NULL)
	  {
	    errstring = N_("\
%s: out of memory to store relocation results for %s\n");
	    _dl_fatal_printf (errstring, RTLD_PROGNAME, l->l_name);
	  }
      }
  }

  /* Mark the object so we know this work has been done.  */
  l->l_relocated = 1;

  /* Undo the segment protection changes.  */
  while (__builtin_expect (textrels != NULL, 0))
    {
      if (__mprotect (textrels->start, textrels->len, textrels->prot) < 0)
	{
	  errstring = N_("cannot restore segment prot after reloc");
	  goto call_error;
	}

#ifdef CLEAR_CACHE
      CLEAR_CACHE (textrels->start, textrels->start + textrels->len);
#endif

      textrels = textrels->next;
    }
}

void
_dl_relocate_object (struct link_map *l, struct r_scope_elem *scope[],
		     int reloc_mode, int consider_profiling)
{
  if (l->l_relocated)
    return;
  _dl_relocate_object_no_relro (l, scope, reloc_mode, consider_profiling);
  _dl_protect_relro (l);
}

void
_dl_protect_relro (struct link_map *l)
{
  if (l->l_relro_size == 0)
    return;

  ElfW(Addr) start = ALIGN_DOWN((l->l_addr
				 + l->l_relro_addr),
				GLRO(dl_pagesize));
  ElfW(Addr) end = ALIGN_DOWN((l->l_addr
			       + l->l_relro_addr
			       + l->l_relro_size),
			      GLRO(dl_pagesize));
  if (start != end
      && __mprotect ((void *) start, end - start, PROT_READ) < 0)
    {
      static const char errstring[] = N_("\
cannot apply additional memory protection after relocation");
      _dl_signal_error (errno, l->l_name, NULL, errstring);
    }
}

void
__attribute_noinline__
_dl_reloc_bad_type (struct link_map *map, unsigned int type, int plt)
{
#define DIGIT(b)	_itoa_lower_digits[(b) & 0xf];

  /* XXX We cannot translate these messages.  */
  static const char msg[2][32
#if __ELF_NATIVE_CLASS == 64
			   + 6
#endif
  ] = { "unexpected reloc type 0x",
	"unexpected PLT reloc type 0x" };
  char msgbuf[sizeof (msg[0])];
  char *cp;

  cp = __stpcpy (msgbuf, msg[plt]);
#if __ELF_NATIVE_CLASS == 64
  if (__builtin_expect(type > 0xff, 0))
    {
      *cp++ = DIGIT (type >> 28);
      *cp++ = DIGIT (type >> 24);
      *cp++ = DIGIT (type >> 20);
      *cp++ = DIGIT (type >> 16);
      *cp++ = DIGIT (type >> 12);
      *cp++ = DIGIT (type >> 8);
    }
#endif
  *cp++ = DIGIT (type >> 4);
  *cp++ = DIGIT (type);
  *cp = '\0';

  _dl_signal_error (0, map->l_name, NULL, msgbuf);
}
