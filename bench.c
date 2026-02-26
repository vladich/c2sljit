/* Benchmark: c2sljit vs c2mir
   Compiles the same C programs with both compilers,
   measuring compilation speed and execution speed side-by-side.

   Note: we cannot include c2sljit.h here because its mir-compat.h
   conflicts with the real mir.h.  Instead we declare the c2sljit API
   manually and replicate the lightweight MIR_context layout it expects. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>

/* c2mir / MIR (real headers) */
#include "mir.h"
#include "mir-gen.h"
#include "c2mir.h"
#include "real-time.h"

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
  int opt_reg_cache_p;
  int opt_cmp_branch_p;
  int opt_strength_reduce_p;
  int opt_magic_div_p;
  int opt_commute_p;
  int opt_smart_regs_p;
  int opt_defer_store_p;
  int opt_float_promote_p;
  int opt_float_cache_p;
  int opt_ind_cache_p;
  int opt_inline_p;
  int opt_float_chain_p;
  int opt_addr_cache_p;
  int opt_fmadd_p;
  int opt_float_field_cache_p;
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

/* ---- Import resolver ---- */

static void *import_resolver (const char *name) {
  return dlsym (RTLD_DEFAULT, name);
}

/* ---- stdout suppression for programs that print output ---- */

static int saved_stdout_fd = -1;
static int devnull_fd = -1;

static void suppress_stdout (void) {
  fflush (stdout);
  saved_stdout_fd = dup (STDOUT_FILENO);
  devnull_fd = open ("/dev/null", O_WRONLY);
  dup2 (devnull_fd, STDOUT_FILENO);
  close (devnull_fd);
}

static void restore_stdout (void) {
  fflush (stdout);
  dup2 (saved_stdout_fd, STDOUT_FILENO);
  close (saved_stdout_fd);
  saved_stdout_fd = -1;
}

/* ---- Benchmark result ---- */

struct bench_result {
  double compile_us;
  double exec_us;
  int result;
  int ok;
};

/* ---- Run benchmark via c2sljit ---- */

static struct bench_result run_c2sljit (const char *name, const char *source,
                                        int opt_p, int has_output) {
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
    opts.opt_float_promote_p = opts.opt_float_cache_p = 1;
    opts.opt_ind_cache_p = opts.opt_inline_p = 1;
    opts.opt_float_chain_p = opts.opt_addr_cache_p = 1;
    opts.opt_float_field_cache_p = 1;
#if defined(__aarch64__) || defined(_M_ARM64)
    opts.opt_fmadd_p = 1;
#endif
#if defined(SLJIT_CONFIG_X86_64) && SLJIT_CONFIG_X86_64
    opts.opt_magic_div_p = 1;
#endif
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
      if (has_output) suppress_stdout ();
      t0 = real_usec_time ();
      r.result = main_func (0, NULL);
      t1 = real_usec_time ();
      if (has_output) restore_stdout ();
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

static struct bench_result run_c2mir (const char *name, const char *source,
                                      int opt_level, int has_output) {
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
  MIR_gen_set_optimize_level (ctx, opt_level);
  MIR_load_external (ctx, "_MIR_flush_code_cache", _MIR_flush_code_cache);
  MIR_link (ctx, MIR_set_gen_interface, import_resolver);

  double t1 = real_usec_time ();
  r.compile_us = t1 - t0;

  /* Execute */
  int (*fun_addr) (int, char **) = (int (*) (int, char **)) main_func->addr;
  if (has_output) suppress_stdout ();
  t0 = real_usec_time ();
  r.result = fun_addr (0, NULL);
  t1 = real_usec_time ();
  if (has_output) restore_stdout ();
  r.exec_us = t1 - t0;
  r.ok = 1;

  MIR_gen_finish (ctx);
  c2mir_finish (ctx);
  MIR_finish (ctx);
  return r;
}

/* ---- File reading ---- */

static char *read_file (const char *path) {
  FILE *f = fopen (path, "r");
  if (!f) return NULL;
  fseek (f, 0, SEEK_END);
  long len = ftell (f);
  fseek (f, 0, SEEK_SET);
  char *buf = (char *) malloc (len + 1);
  fread (buf, 1, len, f);
  buf[len] = '\0';
  fclose (f);
  return buf;
}

/* ---- Test programs (inline — no external calls, integer-only) ---- */

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

struct benchmark {
  const char *name;
  const char *source;       /* inline source or NULL for file-based */
  const char *file_path;    /* file path or NULL for inline */
  int has_output;           /* 1 if program prints to stdout */
};

static struct benchmark benchmarks[] = {
  {"accumulate",    accumulate_src,  NULL, 0},
  {"collatz",       collatz_src,     NULL, 0},
  {"gcd_sum",       gcd_sum_src,     NULL, 0},
  {"prime_count",   prime_count_src, NULL, 0},
  {"bitops",        bitops_src,      NULL, 0},
  {"mandelbrot",    NULL, "tests/mandelbrot.c",    1},
  {"nbody",         NULL, "tests/nbody.c",         1},
  {"spectral_norm", NULL, "tests/spectral_norm.c", 1},
  {"pi_digits",     NULL, "tests/pi_digits.c",     1},
  {"expr_eval",     NULL, "tests/expr_eval.c",     1},
  {"sort",          NULL, "tests/sort.c",           1},
};

#define N_BENCH (sizeof (benchmarks) / sizeof (benchmarks[0]))

/* Pick the run with the lowest total (compile + exec) time. */
static struct bench_result best_of (struct bench_result *runs, int n) {
  int best = 0;
  for (int i = 1; i < n; i++)
    if (runs[i].compile_us + runs[i].exec_us < runs[best].compile_us + runs[best].exec_us)
      best = i;
  return runs[best];
}

#define N_RUNS 5

int main (void) {
  /* Header */
  printf ("%-16s  ----------- compile (us) -----------   ------------ execute (us) ------------\n", "");
  printf ("%-16s  %6s %6s %6s %6s %6s %6s   %6s %6s %6s %6s %6s %6s\n",
          "Benchmark", "sljit", "slj-1", "mir", "mir-1", "mir-2", "mir-3",
          "sljit", "slj-1", "mir", "mir-1", "mir-2", "mir-3");
  printf ("%-16s  %6s %6s %6s %6s %6s %6s   %6s %6s %6s %6s %6s %6s\n",
          "----------------",
          "------", "------", "------", "------", "------", "------",
          "------", "------", "------", "------", "------", "------");

  for (size_t i = 0; i < N_BENCH; i++) {
    const char *source = benchmarks[i].source;
    char *file_buf = NULL;
    if (source == NULL && benchmarks[i].file_path != NULL) {
      file_buf = read_file (benchmarks[i].file_path);
      if (file_buf == NULL) {
        fprintf (stderr, "cannot read %s\n", benchmarks[i].file_path);
        continue;
      }
      source = file_buf;
    }

    int has_out = benchmarks[i].has_output;

    struct bench_result sr[N_RUNS], s1r[N_RUNS];
    struct bench_result mr[N_RUNS], m1r[N_RUNS], m2r[N_RUNS], m3r[N_RUNS];

    for (int r = 0; r < N_RUNS; r++) {
      sr[r]  = run_c2sljit (benchmarks[i].name, source, 0, has_out);
      s1r[r] = run_c2sljit (benchmarks[i].name, source, 1, has_out);
      mr[r]  = run_c2mir (benchmarks[i].name, source, 0, has_out);
      m1r[r] = run_c2mir (benchmarks[i].name, source, 1, has_out);
      m2r[r] = run_c2mir (benchmarks[i].name, source, 2, has_out);
      m3r[r] = run_c2mir (benchmarks[i].name, source, 3, has_out);
    }

    struct bench_result s  = best_of (sr,  N_RUNS);
    struct bench_result s1 = best_of (s1r, N_RUNS);
    struct bench_result m  = best_of (mr,  N_RUNS);
    struct bench_result m1 = best_of (m1r, N_RUNS);
    struct bench_result m2 = best_of (m2r, N_RUNS);
    struct bench_result m3 = best_of (m3r, N_RUNS);

    printf ("%-16s  %6.0f %6.0f %6.0f %6.0f %6.0f %6.0f   %6.0f %6.0f %6.0f %6.0f %6.0f %6.0f\n",
            benchmarks[i].name,
            s.compile_us, s1.compile_us,
            m.compile_us, m1.compile_us, m2.compile_us, m3.compile_us,
            s.exec_us, s1.exec_us,
            m.exec_us, m1.exec_us, m2.exec_us, m3.exec_us);

    free (file_buf);
  }

  return 0;
}
