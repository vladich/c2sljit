/* c2sljit driver: command-line interface for the C-to-sljit compiler. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mir-alloc.h"
#include "mir-alloc-default.c"
#include "c2sljit.h"

/* Simple getc wrapper for file input */
struct file_getc_data {
  FILE *f;
};

static int file_getc (void *data) {
  struct file_getc_data *d = (struct file_getc_data *) data;
  return fgetc (d->f);
}

/* Simple getc wrapper for string input */
struct string_getc_data {
  const char *str;
  size_t pos;
};

static int string_getc (void *data) {
  struct string_getc_data *d = (struct string_getc_data *) data;
  if (d->str[d->pos] == '\0') return EOF;
  return (unsigned char) d->str[d->pos++];
}

static void usage (const char *prog) {
  fprintf (stderr,
           "Usage: %s [options] [file.c | -e 'code']\n"
           "Options:\n"
           "  -E           Preprocess only\n"
           "  -fsyntax-only  Parse and check only, no code gen\n"
           "  -v           Verbose output\n"
           "  -d           Debug AST output\n"
           "  -D name[=value]  Define preprocessor macro\n"
           "  -I dir       Add include directory\n"
           "  -w           Suppress warnings\n"
           "  -e code      Compile and run code string\n"
           "  -O1          Enable all optimizations\n"
           "  -fopt-cmp-branch     Comparison-branch fusion\n"
           "  -fopt-mem-operands   Direct memory/imm operands\n"
           "  -fopt-reg-cache      Register caching in basic blocks\n"
           "  -fopt-strength-reduce  Strength reduction (mul/div/mod)\n"
           "  -fopt-commute        Commutative operand swap\n"
           "  -fopt-smart-regs     Cache-aware temp reg allocation\n"
           "  -fopt-defer-store    Deferred write-back\n"
           "  -h           Show this help\n",
           prog);
}

int main (int argc, char *argv[]) {
  struct c2sljit_options opts;
  memset (&opts, 0, sizeof (opts));
  opts.message_file = stderr;

  /* Collect -D and -I options */
  struct c2sljit_macro_command macros[256];
  const char *include_dirs[256];
  int n_macros = 0, n_includes = 0;
  const char *source_file = NULL;
  const char *eval_code = NULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp (argv[i], "-E") == 0) {
      opts.prepro_only_p = 1;
      opts.prepro_output_file = stdout;
    } else if (strcmp (argv[i], "-fsyntax-only") == 0) {
      opts.syntax_only_p = 1;
    } else if (strcmp (argv[i], "-v") == 0) {
      opts.verbose_p = 1;
    } else if (strcmp (argv[i], "-d") == 0) {
      opts.debug_p = 1;
    } else if (strcmp (argv[i], "-w") == 0) {
      opts.ignore_warnings_p = 1;
    } else if (strcmp (argv[i], "-pedantic") == 0) {
      opts.pedantic_p = 1;
    } else if (strcmp (argv[i], "-fopt-mem-operands") == 0) {
      opts.opt_mem_operands_p = 1;
    } else if (strcmp (argv[i], "-fopt-reg-cache") == 0) {
      opts.opt_reg_cache_p = 1;
    } else if (strcmp (argv[i], "-fopt-cmp-branch") == 0) {
      opts.opt_cmp_branch_p = 1;
    } else if (strcmp (argv[i], "-fopt-strength-reduce") == 0) {
      opts.opt_strength_reduce_p = 1;
    } else if (strcmp (argv[i], "-fopt-commute") == 0) {
      opts.opt_commute_p = 1;
    } else if (strcmp (argv[i], "-fopt-smart-regs") == 0) {
      opts.opt_smart_regs_p = 1;
    } else if (strcmp (argv[i], "-fopt-defer-store") == 0) {
      opts.opt_defer_store_p = 1;
    } else if (strcmp (argv[i], "-O1") == 0) {
      opts.opt_mem_operands_p = 1;
      opts.opt_reg_cache_p = opts.opt_cmp_branch_p = 1;
      opts.opt_strength_reduce_p = opts.opt_commute_p = 1;
      opts.opt_smart_regs_p = opts.opt_defer_store_p = 1;
    } else if (strncmp (argv[i], "-D", 2) == 0) {
      const char *macro = argv[i] + 2;
      if (*macro == '\0' && i + 1 < argc) macro = argv[++i];
      macros[n_macros].def_p = 1;
      char *eq = strchr (macro, '=');
      if (eq != NULL) {
        macros[n_macros].name = strndup (macro, eq - macro);
        macros[n_macros].def = eq + 1;
      } else {
        macros[n_macros].name = macro;
        macros[n_macros].def = "1";
      }
      n_macros++;
    } else if (strncmp (argv[i], "-U", 2) == 0) {
      const char *macro = argv[i] + 2;
      if (*macro == '\0' && i + 1 < argc) macro = argv[++i];
      macros[n_macros].def_p = 0;
      macros[n_macros].name = macro;
      macros[n_macros].def = NULL;
      n_macros++;
    } else if (strncmp (argv[i], "-I", 2) == 0) {
      const char *dir = argv[i] + 2;
      if (*dir == '\0' && i + 1 < argc) dir = argv[++i];
      include_dirs[n_includes++] = dir;
    } else if (strcmp (argv[i], "-e") == 0) {
      if (i + 1 < argc)
        eval_code = argv[++i];
      else {
        fprintf (stderr, "%s: -e requires an argument\n", argv[0]);
        return 1;
      }
    } else if (strcmp (argv[i], "-h") == 0 || strcmp (argv[i], "--help") == 0) {
      usage (argv[0]);
      return 0;
    } else if (argv[i][0] != '-') {
      source_file = argv[i];
    } else {
      fprintf (stderr, "%s: unknown option: %s\n", argv[0], argv[i]);
      return 1;
    }
  }

  opts.macro_commands = macros;
  opts.macro_commands_num = n_macros;
  opts.include_dirs = include_dirs;
  opts.include_dirs_num = n_includes;

  if (source_file == NULL && eval_code == NULL) {
    fprintf (stderr, "%s: no input file or -e code\n", argv[0]);
    usage (argv[0]);
    return 1;
  }

  /* Create MIR context (our lightweight version) */
  struct MIR_context mir_ctx_storage;
  memset (&mir_ctx_storage, 0, sizeof (mir_ctx_storage));
  mir_ctx_storage.alloc = &default_alloc;
  MIR_context_t ctx = &mir_ctx_storage;

  c2sljit_init (ctx);

  int success;
  if (eval_code != NULL) {
    struct string_getc_data sgd = {.str = eval_code, .pos = 0};
    success = c2sljit_compile (ctx, &opts, string_getc, &sgd, COMMAND_LINE_SOURCE_NAME, NULL);
  } else {
    FILE *f = fopen (source_file, "r");
    if (f == NULL) {
      fprintf (stderr, "%s: cannot open %s\n", argv[0], source_file);
      c2sljit_finish (ctx);
      return 1;
    }
    struct file_getc_data fgd = {.f = f};
    success = c2sljit_compile (ctx, &opts, file_getc, &fgd, source_file, NULL);
    fclose (f);
  }

  int ret = 0;
  if (success && !opts.prepro_only_p && !opts.syntax_only_p) {
    /* Execute main() */
    c2sljit_main_func_t main_func = c2sljit_get_main (ctx);
    if (main_func != NULL) {
      ret = main_func (0, NULL);
      if (opts.verbose_p) fprintf (stderr, "main() returned %d\n", ret);
    } else {
      fprintf (stderr, "c2sljit: main() not found\n");
      ret = 1;
    }
  } else if (!success) {
    ret = 1;
  }

  c2sljit_finish (ctx);
  return ret;
}
