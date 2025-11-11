/* Look up a symbol in a shared object loaded by `dlopen'.
   Copyright (C) 1999-2025 Free Software Foundation, Inc.
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

#include <assert.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdlib.h>
#include <libintl.h>

#include <dlfcn.h>
#include <ldsodefs.h>
#include <dl-hash.h>
#include <sysdep-cancel.h>
#include <dl-tls.h>
#include <dl-irel.h>
#include <dl-sym-post.h>

/* THESE ARE ONLY FOR TESTING */
#include <stdarg.h>
#include <stdio.h>
void _dl_debug_printff(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

int search(struct link_map *lm, const char *symname)
{
    ElfW(Dyn) *dyn = lm->l_ld;
    ElfW(Sym) *symtab = NULL;
    const char *strtab = NULL;
    ElfW(Rela) *relaplt = NULL;
    size_t pltrelsz = 0;
    //size_t syment = sizeof(ElfW(Sym));
    int pltrel = 0;

    for (; dyn->d_tag != DT_NULL; ++dyn) {
        switch (dyn->d_tag) {
        case DT_SYMTAB: symtab = (ElfW(Sym) *)dyn->d_un.d_ptr; break;
        case DT_STRTAB: strtab = (const char *)dyn->d_un.d_ptr; break;
        case DT_JMPREL: relaplt = (ElfW(Rela) *)dyn->d_un.d_ptr; break;
        case DT_PLTRELSZ: pltrelsz = dyn->d_un.d_val; break;
        //case DT_SYMENT: syment = dyn->d_un.d_val; break;
        case DT_PLTREL: pltrel = dyn->d_un.d_val; break;
        }
    }

    _dl_debug_printff("SYMTAB: 0x%lx, PLTREL 0x%lx\n", symtab, pltrel);

    if (!symtab || !strtab || !relaplt || !pltrelsz)
        return 0;

    // Find the symbol index
    ssize_t symidx = -1;
    for (int i = 0; ; ++i) {
        if (symtab[i].st_name == 0 && i != 0) break;
        if (strcmp(strtab + symtab[i].st_name, symname) == 0) {
            symidx = i;
            break;
        }
    }
    if (symidx == -1)
        return 0;

    _dl_debug_printff("FOUND!!\n");

    // Determine how many PLT relocs
    size_t nrelocs = pltrelsz / ((pltrel == DT_RELA) ? sizeof(ElfW(Rela)) : sizeof(ElfW(Rel)));
    for (size_t i = 0; i < nrelocs; ++i) {
        size_t r_sym;
        if (pltrel == DT_RELA)
            r_sym = ELF64_R_SYM(relaplt[i].r_info);
        else
            r_sym = ELF64_R_SYM(((ElfW(Rel) *)relaplt)[i].r_info);
        if ((ssize_t)r_sym == symidx) {
            _dl_debug_printff("IDX is: 0x%lx\n", symidx);
            return 1;
        }
    }
    return 0;
}


/* END */

#ifdef SHARED
/* Systems which do not have tls_index also probably have to define
   DONT_USE_TLS_INDEX.  */

# ifndef __TLS_GET_ADDR
#  define __TLS_GET_ADDR __tls_get_addr
# endif

/* Return the symbol address given the map of the module it is in and
   the symbol record.  This is used in dl-sym.c.  */
static void *
_dl_tls_symaddr (struct link_map *map, const ElfW(Sym) *ref)
{
# ifndef DONT_USE_TLS_INDEX
  tls_index tmp =
    {
      .ti_module = map->l_tls_modid,
      .ti_offset = ref->st_value
    };

  return __TLS_GET_ADDR (&tmp);
# else
  return __TLS_GET_ADDR (map->l_tls_modid, ref->st_value);
# endif
}
#endif


struct call_dl_lookup_args
{
  /* Arguments to do_dlsym.  */
  struct link_map *map;
  const char *name;
  struct r_found_version *vers;
  int flags;

  /* Return values of do_dlsym.  */
  lookup_t loadbase;
  const ElfW(Sym) **refp;
};

static void
call_dl_lookup (void *ptr)
{
  struct call_dl_lookup_args *args = (struct call_dl_lookup_args *) ptr;
  args->map = GLRO(dl_lookup_symbol_x) (args->name, args->map, args->refp,
					args->map->l_scope, args->vers, 0,
					args->flags, NULL);
}

static void *
do_sym (void *handle, const char *name, void *who,
	struct r_found_version *vers, int flags)
{
  const ElfW(Sym) *ref = NULL;
  const ElfW(Sym) *refi = NULL;
  lookup_t result;
  lookup_t resulti;
  ElfW(Addr) caller = (ElfW(Addr)) who;

  /* Link map of the caller if needed.  */
  struct link_map *match = NULL;

  if (handle == RTLD_DEFAULT)
    {
      match = _dl_sym_find_caller_link_map (caller);

      /* Search the global scope.  We have the simple case where
	 we look up in the scope of an object which was part of
	 the initial binary.  And then the more complex part
	 where the object is dynamically loaded and the scope
	 array can change.  */
      if (RTLD_SINGLE_THREAD_P)
	result = GLRO(dl_lookup_symbol_x) (name, match, &ref,
					   match->l_scope, vers, 0,
					   flags | DL_LOOKUP_ADD_DEPENDENCY,
					   NULL);
      else
	{
	  struct call_dl_lookup_args args;
	  args.name = name;
	  args.map = match;
	  args.vers = vers;
	  args.flags
	    = flags | DL_LOOKUP_ADD_DEPENDENCY | DL_LOOKUP_GSCOPE_LOCK;
	  args.refp = &ref;

	  THREAD_GSCOPE_SET_FLAG ();
	  struct dl_exception exception;
	  int err = _dl_catch_exception (&exception, call_dl_lookup, &args);
	  THREAD_GSCOPE_RESET_FLAG ();
	  if (__glibc_unlikely (exception.errstring != NULL))
	    _dl_signal_exception (err, &exception, NULL);

	  result = args.map;
	}
    }
  else if (handle == RTLD_NEXT)
    {
      match = _dl_sym_find_caller_link_map (caller);

      if (__glibc_unlikely (match == GL(dl_ns)[LM_ID_BASE]._ns_loaded))
	{
	  if (match == NULL
	      || caller < match->l_map_start
	      || caller >= match->l_map_end)
	    _dl_signal_error (0, NULL, NULL, N_("\
RTLD_NEXT used in code not dynamically loaded"));
	}

      struct link_map *l = match;
      while (l->l_loader != NULL)
	l = l->l_loader;

      result = GLRO(dl_lookup_symbol_x) (name, match, &ref, l->l_local_scope,
					 vers, 0, flags, match);
    }
  else
    {
      /* Search the scope of the given object.  */
      struct link_map *map = handle;
      result = GLRO(dl_lookup_symbol_x) (name, map, &ref, map->l_local_scope,
					 vers, 0, flags, NULL);
      
      // lookup_t tmp = GLRO(dl_lookup_symbol_x) (name, mapi, &refi, mapi->l_local_scope,
			// 		 vers, 0, flags, NULL);;
      // match = _dl_sym_find_caller_link_map (caller);
      // char *trunc_name = strndup(name, strlen(name)-1);
      // resulti = GLRO(dl_lookup_symbol_x) (trunc_name, map, &refi, match->l_local_scope,
			// 		 vers, 0, flags, NULL);

      if (ref != NULL) {     
        // Choose which result to use based on symbol type
        if (ELFW(ST_TYPE)(ref->st_info) != STT_FUNC)
        {
            // The first lookup is not a function, so keep the first result
            //_dl_debug_printff("Symbol %s is not a function; using original lookup.\n", name);
            // result and ref already point to the first lookup, nothing to change
        }
        else
        {
            // The first lookup is a function, use the truncated result instead
            match = _dl_sym_find_caller_link_map (caller);
            size_t tmp_len = strlen(name)-1;
            char *trunc_name = malloc(5 + tmp_len + 1);
            if (!trunc_name) return NULL;
            memcpy(trunc_name, "dsym_", 5);
            memcpy(trunc_name + 5, name, tmp_len);
            trunc_name[5 + tmp_len] = '\0';
            //_dl_debug_printff("Symbol %s is a function; using truncated lookup (%s).\n", name, trunc_name);
            resulti = GLRO(dl_lookup_symbol_x) (trunc_name, map, &refi, match->l_local_scope,
                vers, 0, flags, NULL);
            result = resulti;
            ref = refi;
        }
      }
      
    }

  if (ref != NULL)
    {
      void *value;
      //_dl_debug_printff("Ref is %lx\n", result);
#ifdef SHARED
      if (ELFW(ST_TYPE) (ref->st_info) == STT_TLS)
	/* The found symbol is a thread-local storage variable.
	   Return the address for to the current thread.  */
	value = _dl_tls_symaddr (result, ref);
      else
#endif
	value = DL_SYMBOL_ADDRESS (result, ref);

      return _dl_sym_post (result, ref, value, caller, match);
    }

  return NULL;
}


void *
_dl_vsym (void *handle, const char *name, const char *version, void *who)
{
  struct r_found_version vers;

  /* Compute hash value to the version string.  */
  vers.name = version;
  vers.hidden = 1;
  vers.hash = _dl_elf_hash (version);
  /* We don't have a specific file where the symbol can be found.  */
  vers.filename = NULL;

  return do_sym (handle, name, who, &vers, 0);
}

void *
_dl_sym (void *handle, const char *name, void *who)
{
  return do_sym (handle, name, who, NULL, DL_LOOKUP_RETURN_NEWEST);
}
