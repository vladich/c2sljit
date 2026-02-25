/* Benchmark: c2sljit vs c2mir
   Compiles the same integer-only C programs with both compilers,
   measuring compilation speed and execution speed side-by-side.

   Note: we cannot include c2sljit.h here because its mir-compat.h
   conflicts with the real mir.h.  Instead we declare the c2sljit API
   manually and replicate the lightweight MIR_context layout it expects. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>

/* c2mir / MIR (real headers) */
#include "mir.h"
#include "mir-gen.h"
#include "c2mir.h"
#include "real-time.h"
#include "libtcc.h"

/* ---- c2sljit API (manual declarations) ----
   c2sljit functions take a pointer to c2sljit's own MIR_context struct
   (defined in mir-compat.h).  In C, name mangling is absent, so we can
   declare them with void* and cast.  The c2sljit_options struct has the
   same layout as c2mir_options minus the asm_p/object_p fields.  */

struct c2sljit_macro_command {
  int def_p;
  const char *name, *def;
};

struct c2sljit_options {
  FILE *message_file;
  int debug_p, verbose_p, ignore_warnings_p, no_prepro_p, prepro_only_p;
  int syntax_only_p, pedantic_p;
  int opt_mem_operands_p;
  int opt_reg_cache_p, opt_cmp_branch_p;
  int opt_strength_reduce_p, opt_commute_p, opt_smart_regs_p, opt_defer_store_p;
  size_t module_num;
  FILE *prepro_output_file;
  const char *output_file_name;
  size_t macro_commands_num, include_dirs_num;
  struct c2sljit_macro_command *macro_commands;
  const char **include_dirs;
};

typedef int (*c2sljit_main_func_t) (int argc, char **argv);

/* These link against the c2sljit.o object which expects MIR_context_t,
   but on all ABIs a pointer is a pointer. */
extern void c2sljit_init (void *ctx);
extern void c2sljit_finish (void *ctx);
extern int c2sljit_compile (void *ctx, struct c2sljit_options *ops, int (*getc_func) (void *),
                            void *getc_data, const char *source_name, FILE *output_file);
extern c2sljit_main_func_t c2sljit_get_main (void *ctx);

/* c2sljit's MIR_context layout (from mir-compat.h).
   We allocate this on the stack and pass its address. */
struct c2sljit_mir_context {
  void *alloc;         /* MIR_alloc_t */
  void *error_func;    /* MIR_error_func_t */
  uint32_t next_alias;
  void *curr_module;   /* MIR_module_t */
  void *data;          /* user data (c2m_ctx) */
};

/* Default allocator (same as in c2sljit-driver.c) */
static void *bench_malloc (size_t size, void *ud) { (void) ud; return malloc (size); }
static void *bench_calloc (size_t n, size_t sz, void *ud) { (void) ud; return calloc (n, sz); }
static void *bench_realloc (void *p, size_t old, size_t new_sz, void *ud) {
  (void) ud; (void) old; return realloc (p, new_sz);
}
static void bench_free (void *p, void *ud) { (void) ud; free (p); }

static struct MIR_alloc bench_alloc = {
  .malloc = bench_malloc, .calloc = bench_calloc,
  .realloc = bench_realloc, .free = bench_free,
  .user_data = NULL
};

/* ---- String-based getc ---- */

struct string_getc_data {
  const char *str;
  size_t pos;
};

static int string_getc (void *data) {
  struct string_getc_data *d = (struct string_getc_data *) data;
  if (d->str[d->pos] == '\0') return EOF;
  return (unsigned char) d->str[d->pos++];
}

/* ---- c2mir input wrapper ---- */

struct mir_input {
  const char *code;
  size_t len;
  size_t pos;
};

static int mir_getc (void *data) {
  struct mir_input *inp = (struct mir_input *) data;
  return inp->pos >= inp->len ? EOF : (unsigned char) inp->code[inp->pos++];
}

/* ---- Import resolver for MIR (programs have no external calls) ---- */

static void *import_resolver (const char *name) {
  return dlsym (RTLD_DEFAULT, name);
}

/* ---- Benchmark result ---- */

struct bench_result {
  double compile_us;
  double exec_us;
  int result;
  int ok;
};

/* ---- Run benchmark via c2sljit ---- */

static struct bench_result run_c2sljit (const char *name, const char *source, int opt_p) {
  struct bench_result r = {0, 0, -1, 0};

  struct c2sljit_mir_context ctx_storage;
  memset (&ctx_storage, 0, sizeof (ctx_storage));
  ctx_storage.alloc = &bench_alloc;
  void *ctx = &ctx_storage;

  struct c2sljit_options opts;
  memset (&opts, 0, sizeof (opts));
  opts.message_file = stderr;
  opts.ignore_warnings_p = 1;
  if (opt_p) {
    opts.opt_mem_operands_p = 1;
    opts.opt_reg_cache_p = opts.opt_cmp_branch_p = 1;
    opts.opt_strength_reduce_p = opts.opt_commute_p = 1;
    opts.opt_smart_regs_p = opts.opt_defer_store_p = 1;
  }

  struct string_getc_data sgd = {.str = source, .pos = 0};

  double t0 = real_usec_time ();
  c2sljit_init (ctx);
  int ok = c2sljit_compile (ctx, &opts, string_getc, &sgd, name, NULL);
  double t1 = real_usec_time ();
  r.compile_us = t1 - t0;

  if (ok) {
    c2sljit_main_func_t main_func = c2sljit_get_main (ctx);
    if (main_func != NULL) {
      t0 = real_usec_time ();
      r.result = main_func (0, NULL);
      t1 = real_usec_time ();
      r.exec_us = t1 - t0;
      r.ok = 1;
    } else {
      fprintf (stderr, "c2sljit: %s: main() not found\n", name);
    }
  } else {
    fprintf (stderr, "c2sljit: %s: compilation failed\n", name);
  }

  c2sljit_finish (ctx);
  return r;
}

/* ---- Run benchmark via c2mir ---- */

static struct bench_result run_c2mir (const char *name, const char *source) {
  struct bench_result r = {0, 0, -1, 0};

  struct c2mir_options opts;
  memset (&opts, 0, sizeof (opts));
  opts.message_file = stderr;
  opts.ignore_warnings_p = 1;

  struct mir_input inp = {.code = source, .len = strlen (source), .pos = 0};

  double t0 = real_usec_time ();

  MIR_context_t ctx = MIR_init ();
  c2mir_init (ctx);
  int ok = c2mir_compile (ctx, &opts, mir_getc, &inp, name, NULL);

  if (!ok) {
    fprintf (stderr, "c2mir: %s: compilation failed\n", name);
    c2mir_finish (ctx);
    MIR_finish (ctx);
    return r;
  }

  /* Find main function in compiled modules */
  MIR_module_t module;
  MIR_item_t func, main_func = NULL;
  for (module = DLIST_HEAD (MIR_module_t, *MIR_get_module_list (ctx)); module != NULL;
       module = DLIST_NEXT (MIR_module_t, module)) {
    for (func = DLIST_HEAD (MIR_item_t, module->items); func != NULL;
         func = DLIST_NEXT (MIR_item_t, func))
      if (func->item_type == MIR_func_item && strcmp (func->u.func->name, "main") == 0)
        main_func = func;
    MIR_load_module (ctx, module);
  }

  if (main_func == NULL) {
    fprintf (stderr, "c2mir: %s: main() not found\n", name);
    c2mir_finish (ctx);
    MIR_finish (ctx);
    return r;
  }

  /* Generate native code */
  MIR_gen_init (ctx);
  MIR_load_external (ctx, "_MIR_flush_code_cache", _MIR_flush_code_cache);
  MIR_link (ctx, MIR_set_gen_interface, import_resolver);

  double t1 = real_usec_time ();
  r.compile_us = t1 - t0;

  /* Execute */
  int (*fun_addr) (int, char **) = (int (*) (int, char **)) main_func->addr;
  t0 = real_usec_time ();
  r.result = fun_addr (0, NULL);
  t1 = real_usec_time ();
  r.exec_us = t1 - t0;
  r.ok = 1;

  MIR_gen_finish (ctx);
  c2mir_finish (ctx);
  MIR_finish (ctx);
  return r;
}

/* ---- Test programs ---- */

static const char accumulate_src[] =
  "int main() {\n"
  "  int sum = 0;\n"
  "  for (int i = 1; i <= 100000; i++) {\n"
  "    int v = (i * 7 + 13) % 1000;\n"
  "    sum = sum + v;\n"
  "  }\n"
  "  return sum % 256;\n"
  "}\n";

static const char collatz_src[] =
  "int main() {\n"
  "  int total = 0;\n"
  "  for (int n = 1; n <= 1000; n++) {\n"
  "    int x = n;\n"
  "    while (x != 1) {\n"
  "      if (x % 2 == 0) x = x / 2;\n"
  "      else x = 3 * x + 1;\n"
  "      total = total + 1;\n"
  "    }\n"
  "  }\n"
  "  return total % 256;\n"
  "}\n";

static const char gcd_sum_src[] =
  "int main() {\n"
  "  int sum = 0;\n"
  "  for (int i = 1; i <= 100; i++) {\n"
  "    for (int j = 1; j <= 100; j++) {\n"
  "      int a = i, b = j;\n"
  "      while (b != 0) {\n"
  "        int t = b;\n"
  "        b = a % b;\n"
  "        a = t;\n"
  "      }\n"
  "      sum = sum + a;\n"
  "    }\n"
  "  }\n"
  "  return sum % 256;\n"
  "}\n";

static const char prime_count_src[] =
  "int main() {\n"
  "  int count = 0;\n"
  "  for (int n = 2; n <= 10000; n++) {\n"
  "    int is_prime = 1;\n"
  "    for (int d = 2; d * d <= n; d++) {\n"
  "      if (n % d == 0) { is_prime = 0; break; }\n"
  "    }\n"
  "    if (is_prime) count = count + 1;\n"
  "  }\n"
  "  return count % 256;\n"
  "}\n";

static const char bitops_src[] =
  "int main() {\n"
  "  int x = 0x12345678;\n"
  "  for (int i = 0; i < 100000; i++) {\n"
  "    x = x ^ (x << 13);\n"
  "    x = x ^ (x >> 17);\n"
  "    x = x ^ (x << 5);\n"
  "    x = x & 0x7fffffff;\n"
  "  }\n"
  "  return x % 256;\n"
  "}\n";

/* ---- Run benchmark via TCC (libtcc) ---- */

static struct bench_result run_tcc (const char *name, const char *source) {
  struct bench_result r = {0, 0, -1, 0};

  double t0 = real_usec_time ();

  TCCState *s = tcc_new ();
  if (s == NULL) {
    fprintf (stderr, "tcc: %s: tcc_new failed\n", name);
    return r;
  }
  tcc_set_lib_path (s, TCC_LIB_PATH);
  tcc_set_output_type (s, TCC_OUTPUT_MEMORY);
  if (tcc_compile_string (s, source) == -1) {
    fprintf (stderr, "tcc: %s: compilation failed\n", name);
    tcc_delete (s);
    return r;
  }
  if (tcc_relocate (s) < 0) {
    fprintf (stderr, "tcc: %s: relocate failed\n", name);
    tcc_delete (s);
    return r;
  }

  int (*main_func) (int, char **) = (int (*) (int, char **)) tcc_get_symbol (s, "main");
  double t1 = real_usec_time ();
  r.compile_us = t1 - t0;

  if (main_func == NULL) {
    fprintf (stderr, "tcc: %s: main() not found\n", name);
    tcc_delete (s);
    return r;
  }

  t0 = real_usec_time ();
  r.result = main_func (0, NULL);
  t1 = real_usec_time ();
  r.exec_us = t1 - t0;
  r.ok = 1;

  tcc_delete (s);
  return r;
}

struct benchmark {
  const char *name;
  const char *source;
};

static struct benchmark benchmarks[] = {
  {"accumulate",  accumulate_src},
  {"collatz",     collatz_src},
  {"gcd_sum",     gcd_sum_src},
  {"prime_count", prime_count_src},
  {"bitops",      bitops_src},
};

#define N_BENCH (sizeof (benchmarks) / sizeof (benchmarks[0]))

int main (void) {
  printf ("%-16s %14s %14s %14s %14s %14s %14s %14s %14s %8s\n",
          "Benchmark", "sljit compile", "sljit-O1 comp", "mir compile", "tcc compile",
          "sljit-O1 exec", "mir exec", "tcc exec", "sljit/mir", "result");
  printf ("%-16s %14s %14s %14s %14s %14s %14s %14s %14s %8s\n",
          "----------------", "--------------", "--------------", "--------------",
          "--------------", "--------------", "--------------", "--------------",
          "--------------", "--------");

  for (size_t i = 0; i < N_BENCH; i++) {
    struct bench_result sljit_r = run_c2sljit (benchmarks[i].name, benchmarks[i].source, 0);
    struct bench_result sljit_o1_r = run_c2sljit (benchmarks[i].name, benchmarks[i].source, 1);
    struct bench_result mir_r = run_c2mir (benchmarks[i].name, benchmarks[i].source);
    struct bench_result tcc_r = run_tcc (benchmarks[i].name, benchmarks[i].source);

    double exec_ratio = mir_r.exec_us > 0 ? sljit_o1_r.exec_us / mir_r.exec_us : 0;

    const char *result_str;
    char result_buf[64];
    if (!sljit_o1_r.ok || !mir_r.ok || !tcc_r.ok) {
      result_str = "FAIL";
    } else if (sljit_o1_r.result != mir_r.result || tcc_r.result != mir_r.result) {
      snprintf (result_buf, sizeof (result_buf), "MISMATCH %d/%d/%d",
                sljit_o1_r.result, mir_r.result, tcc_r.result);
      result_str = result_buf;
    } else {
      snprintf (result_buf, sizeof (result_buf), "%d", mir_r.result);
      result_str = result_buf;
    }

    printf ("%-16s %11.0f us %11.0f us %11.0f us %11.0f us %11.0f us %11.0f us %11.0f us %7.2fx %8s\n",
            benchmarks[i].name,
            sljit_r.compile_us, sljit_o1_r.compile_us, mir_r.compile_us, tcc_r.compile_us,
            sljit_o1_r.exec_us, mir_r.exec_us, tcc_r.exec_us, exec_ratio,
            result_str);
  }

  return 0;
}
