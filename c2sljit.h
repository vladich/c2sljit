/* c2sljit: C to sljit compiler.
   Based on c2mir by Vladimir Makarov (MIT license).
   sljit by Zoltan Herczeg (BSD-2-Clause license).  */

#ifndef C2SLJIT_H

#define C2SLJIT_H

#include "mir-compat.h"

#define COMMAND_LINE_SOURCE_NAME "<command-line>"
#define STDIN_SOURCE_NAME "<stdin>"

struct c2sljit_macro_command {
  int def_p;              /* #define or #undef */
  const char *name, *def; /* def is used only when def_p is true */
};

struct c2sljit_options {
  FILE *message_file;
  int debug_p, verbose_p, ignore_warnings_p, no_prepro_p, prepro_only_p;
  int syntax_only_p, pedantic_p;
  int opt_mem_operands_p;    /* Opt 2: direct memory/imm operands in binary ops */
  int opt_reg_cache_p;       /* Opt 4: register caching within basic blocks */
  int opt_cmp_branch_p;      /* Opt 5: comparison-branch fusion */
  int opt_strength_reduce_p; /* Opt 6: strength reduction for constant mul/div/mod */
  int opt_magic_div_p;       /* Opt 6b: magic number division (x86-64 only) */
  int opt_commute_p;         /* Opt 7: commutative operand swap */
  int opt_smart_regs_p;      /* Opt 8: cache-aware temp reg allocation */
  int opt_defer_store_p;     /* Opt 9: deferred write-back for cached vars */
  int opt_float_promote_p;   /* Opt 10: float register promotion to FS saved regs */
  int opt_float_cache_p;     /* Opt 11: float register caching in basic blocks */
  int opt_ind_cache_p;       /* Opt 12: array index address caching */
  int opt_inline_p;          /* Opt 13: small function inlining */
  int opt_float_chain_p;     /* Opt 14: float expression spill elimination */
  int opt_addr_cache_p;      /* Opt 15: address register caching */
  int opt_fmadd_p;           /* Opt 16: fused multiply-add (ARM64) */
  int opt_float_field_cache_p; /* Opt 17: float field load CSE */
  size_t module_num;
  FILE *prepro_output_file; /* non-null for prepro_only_p */
  const char *output_file_name;
  size_t macro_commands_num, include_dirs_num;
  struct c2sljit_macro_command *macro_commands;
  const char **include_dirs;
};

void c2sljit_init (MIR_context_t ctx);
void c2sljit_finish (MIR_context_t ctx);

/* Compile one C source.  Returns 0 on success.
   The compiled code can be retrieved and executed via the context.  */
int c2sljit_compile (MIR_context_t ctx, struct c2sljit_options *ops, int (*getc_func) (void *),
                     void *getc_data, const char *source_name, FILE *output_file);

/* After compilation, retrieve the JIT-compiled function pointer for "main".
   Returns NULL if main was not found.  */
typedef int (*c2sljit_main_func_t) (int argc, char **argv);
c2sljit_main_func_t c2sljit_get_main (MIR_context_t ctx);

#endif
