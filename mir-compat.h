/* Compatibility layer providing MIR types used by the c2mir frontend.
   This replaces "mir.h" in c2sljit — only the types/functions needed
   by passes 1-3 (preprocessor, parser, context checker) are defined here.
   The code generation pass (pass 4) uses sljit directly.  */

#ifndef MIR_COMPAT_H
#define MIR_COMPAT_H

#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include "mir-dlist.h"
#include "mir-varr.h"
#include "mir-hash.h"
#include "mir-htab.h"
#include "mir-alloc.h"

#ifdef NDEBUG
static inline int mir_assert (int cond) { return 0 && cond; }
#else
#define mir_assert(cond) assert (cond)
#endif

#define FALSE 0
#define TRUE 1

#ifdef __GNUC__
#define MIR_UNUSED __attribute__ ((unused))
#define MIR_NO_RETURN __attribute__ ((noreturn))
#else
#define MIR_UNUSED
#define MIR_NO_RETURN
#endif

/* ---- MIR_type_t enum (needed by the type system in the context checker) ---- */

#define REP2(M, a1, a2) M (a1) REP_SEP M (a2)
#define REP3(M, a1, a2, a3) REP2 (M, a1, a2) REP_SEP M (a3)
#define REP4(M, a1, a2, a3, a4) REP3 (M, a1, a2, a3) REP_SEP M (a4)
#define REP5(M, a1, a2, a3, a4, a5) REP4 (M, a1, a2, a3, a4) REP_SEP M (a5)
#define REP6(M, a1, a2, a3, a4, a5, a6) REP5 (M, a1, a2, a3, a4, a5) REP_SEP M (a6)
#define REP7(M, a1, a2, a3, a4, a5, a6, a7) REP6 (M, a1, a2, a3, a4, a5, a6) REP_SEP M (a7)
#define REP8(M, a1, a2, a3, a4, a5, a6, a7, a8) REP7 (M, a1, a2, a3, a4, a5, a6, a7) REP_SEP M (a8)

#define REP_SEP ,

#define TYPE_EL(t) MIR_T_##t

#define MIR_BLK_NUM 5

typedef enum {
  REP8 (TYPE_EL, I8, U8, I16, U16, I32, U32, I64, U64),
  REP3 (TYPE_EL, F, D, LD),
  REP2 (TYPE_EL, P, BLK),
  TYPE_EL (RBLK) = TYPE_EL (BLK) + MIR_BLK_NUM,
  REP2 (TYPE_EL, UNDEF, BOUND),
} MIR_type_t;

static inline int MIR_int_type_p (MIR_type_t t) {
  return (MIR_T_I8 <= t && t <= MIR_T_U64) || t == MIR_T_P;
}
static inline int MIR_fp_type_p (MIR_type_t t) { return MIR_T_F <= t && t <= MIR_T_LD; }
static inline int MIR_blk_type_p (MIR_type_t t) { return MIR_T_BLK <= t && t < MIR_T_RBLK; }
static inline int MIR_all_blk_type_p (MIR_type_t t) { return MIR_T_BLK <= t && t <= MIR_T_RBLK; }

/* ---- Pointer size ---- */

#if UINTPTR_MAX == 0xffffffff
#define MIR_PTR32 1
#define MIR_PTR64 0
#elif UINTPTR_MAX == 0xffffffffffffffffu
#define MIR_PTR32 0
#define MIR_PTR64 1
#else
#error "c2sljit: unsupported pointer size"
#endif

/* ---- Basic MIR types used by the frontend ---- */

typedef uint32_t MIR_alias_t;
typedef uint32_t MIR_reg_t;
typedef int64_t MIR_disp_t;
typedef uint8_t MIR_scale_t;

#define MIR_MAX_REG_NUM UINT32_MAX
#define MIR_NON_VAR MIR_MAX_REG_NUM
#define MIR_MAX_SCALE UINT8_MAX

struct MIR_str {
  size_t len;
  const char *s;
};
typedef struct MIR_str MIR_str_t;

typedef union {
  int64_t i;
  uint64_t u;
  float f;
  double d;
  long double ld;
} MIR_imm_t;

/* ---- MIR_item_t: opaque pointer used by frontend to store proto/decl references ---- */

struct MIR_item;
typedef struct MIR_item *MIR_item_t;

/* ---- MIR_op_t: operand representation ---- */
/* In c2sljit this is redefined for sljit. The frontend barely touches it
   (only through code gen), but the struct must exist for compilation.  */

#define OP_EL(o) MIR_OP_##o
typedef enum {
  REP8 (OP_EL, UNDEF, REG, VAR, INT, UINT, FLOAT, DOUBLE, LDOUBLE),
  REP6 (OP_EL, REF, STR, MEM, VAR_MEM, LABEL, BOUND),
} MIR_op_mode_t;

typedef struct {
  MIR_type_t type : 8;
  MIR_scale_t scale;
  MIR_alias_t alias;
  MIR_alias_t nonalias;
  uint32_t nloc;
  MIR_reg_t base, index;
  MIR_disp_t disp;
} MIR_mem_t;

typedef uint32_t MIR_label_t;

typedef struct {
  void *data;
  MIR_op_mode_t mode : 8;
  MIR_op_mode_t value_mode : 8;
  union {
    MIR_reg_t reg;
    MIR_reg_t var;
    int64_t i;
    uint64_t u;
    float f;
    double d;
    long double ld;
    MIR_item_t ref;
    MIR_str_t str;
    MIR_mem_t mem;
    MIR_mem_t var_mem;
    MIR_label_t label;
  } u;
} MIR_op_t;

DEF_VARR (MIR_op_t);

/* ---- MIR_var_t ---- */

typedef struct MIR_var {
  MIR_type_t type;
  const char *name;
  size_t size;
} MIR_var_t;

DEF_VARR (MIR_var_t);
DEF_VARR (MIR_type_t);

/* ---- MIR insn codes (needed by code gen dispatch) ---- */

#define INSN_EL(i) MIR_##i

typedef enum {
  REP4 (INSN_EL, MOV, FMOV, DMOV, LDMOV),
  REP6 (INSN_EL, EXT8, EXT16, EXT32, UEXT8, UEXT16, UEXT32),
  REP3 (INSN_EL, I2F, I2D, I2LD),
  REP3 (INSN_EL, UI2F, UI2D, UI2LD),
  REP3 (INSN_EL, F2I, D2I, LD2I),
  REP6 (INSN_EL, F2D, F2LD, D2F, D2LD, LD2F, LD2D),
  REP5 (INSN_EL, NEG, NEGS, FNEG, DNEG, LDNEG),
  REP4 (INSN_EL, ADDR, ADDR8, ADDR16, ADDR32),
  REP5 (INSN_EL, ADD, ADDS, FADD, DADD, LDADD),
  REP5 (INSN_EL, SUB, SUBS, FSUB, DSUB, LDSUB),
  REP5 (INSN_EL, MUL, MULS, FMUL, DMUL, LDMUL),
  REP7 (INSN_EL, DIV, DIVS, UDIV, UDIVS, FDIV, DDIV, LDDIV),
  REP4 (INSN_EL, MOD, MODS, UMOD, UMODS),
  REP6 (INSN_EL, AND, ANDS, OR, ORS, XOR, XORS),
  REP6 (INSN_EL, LSH, LSHS, RSH, RSHS, URSH, URSHS),
  REP5 (INSN_EL, EQ, EQS, FEQ, DEQ, LDEQ),
  REP5 (INSN_EL, NE, NES, FNE, DNE, LDNE),
  REP7 (INSN_EL, LT, LTS, ULT, ULTS, FLT, DLT, LDLT),
  REP7 (INSN_EL, LE, LES, ULE, ULES, FLE, DLE, LDLE),
  REP7 (INSN_EL, GT, GTS, UGT, UGTS, FGT, DGT, LDGT),
  REP7 (INSN_EL, GE, GES, UGE, UGES, FGE, DGE, LDGE),
  REP8 (INSN_EL, ADDO, ADDOS, SUBO, SUBOS, MULO, MULOS, UMULO, UMULOS),
  REP5 (INSN_EL, JMP, BT, BTS, BF, BFS),
  REP5 (INSN_EL, BEQ, BEQS, FBNE, FBEQ, DBEQ),
  INSN_EL (LDBEQ),
  REP5 (INSN_EL, BNE, BNES, FBNE2, DBNE, LDBNE),
  REP7 (INSN_EL, BLT, BLTS, UBLT, UBLTS, FBLT, DBLT, LDBLT),
  REP7 (INSN_EL, BLE, BLES, UBLE, UBLES, FBLE, DBLE, LDBLE),
  REP7 (INSN_EL, BGT, BGTS, UBGT, UBGTS, FBGT, DBGT, LDBGT),
  REP7 (INSN_EL, BGE, BGES, UBGE, UBGES, FBGE, DBGE, LDBGE),
  REP2 (INSN_EL, BO, UBO),
  REP2 (INSN_EL, BNO, UBNO),
  INSN_EL (JMPI),
  REP3 (INSN_EL, CALL, INLINE, JCALL),
  INSN_EL (SWITCH),
  INSN_EL (RET),
  INSN_EL (JRET),
  INSN_EL (ALLOCA),
  REP2 (INSN_EL, BSTART, BEND),
  INSN_EL (VA_ARG),
  INSN_EL (VA_BLOCK_ARG),
  INSN_EL (VA_START),
  INSN_EL (VA_END),
  INSN_EL (LABEL),
  INSN_EL (UNSPEC),
  REP3 (INSN_EL, PRSET, PRBEQ, PRBNE),
  INSN_EL (USE),
  INSN_EL (PHI),
  INSN_EL (INVALID_INSN),
  INSN_EL (INSN_BOUND),
} MIR_insn_code_t;

/* ---- MIR insn/module/func structures (minimal stubs for compilation) ---- */

/* Forward-declare and set up DLIST infrastructure for all linked types.
   Order matters: DEF_DLIST_LINK must come before any struct that uses DLIST_LINK,
   and DEF_DLIST must come after the struct definition.  We use a forward-declare
   pattern to break the circular dependencies.  */

struct MIR_insn;
typedef struct MIR_insn *MIR_insn_t;
DEF_DLIST_LINK (MIR_insn_t);

struct MIR_insn {
  void *data;
  DLIST_LINK (MIR_insn_t) insn_link;
  MIR_insn_code_t code : 32;
  unsigned int nops : 32;
  MIR_op_t ops[1];
};

DEF_DLIST (MIR_insn_t, insn_link);

/* MIR_item_t: forward-declare link and list type before MIR_module uses DLIST(MIR_item_t).
   The full DEF_DLIST_CODE comes later after struct MIR_item is defined.  */
DEF_DLIST_LINK (MIR_item_t);
DEF_DLIST_TYPE (MIR_item_t);

struct MIR_module;
typedef struct MIR_module *MIR_module_t;
DEF_DLIST_LINK (MIR_module_t);

struct MIR_module {
  void *data;
  const char *name;
  DLIST (MIR_item_t) items;
  DLIST_LINK (MIR_module_t) module_link;
  uint32_t last_temp_item_num;
};

DEF_DLIST (MIR_module_t, module_link);

struct MIR_func {
  const char *name;
  DLIST (MIR_insn_t) insns;
  uint32_t nargs, ntemps;
  MIR_type_t *res_types;
  uint32_t nres;
  char vararg_p;
  VARR (MIR_var_t) *vars;
};

typedef struct MIR_func *MIR_func_t;

typedef enum {
  MIR_func_item,
  MIR_proto_item,
  MIR_import_item,
  MIR_export_item,
  MIR_forward_item,
  MIR_data_item,
  MIR_ref_data_item,
  MIR_lref_data_item,
  MIR_expr_data_item,
  MIR_bss_item,
  MIR_global_item,
} MIR_item_type_t;

struct MIR_item {
  void *data;
  MIR_module_t module;
  DLIST_LINK (MIR_item_t) item_link;
  MIR_item_type_t item_type;
  char ref_p, addr_p, export_p, section_head_p;
  union {
    MIR_func_t func;
    void *proto;  /* simplified */
    const char *import_id;
    const char *export_id;
    const char *forward_id;
    void *bss;
    void *data_item;
    void *ref_data;
    void *global_data;
  } u;
};

/* DEF_DLIST_TYPE was done earlier; now emit the code part: */
DEF_DLIST_CODE (MIR_item_t, item_link);

/* ---- Error handling ---- */

typedef enum MIR_error_type {
  MIR_no_error,
  MIR_alloc_error,
} MIR_error_type_t;

typedef void MIR_NO_RETURN (*MIR_error_func_t) (MIR_error_type_t error_type, const char *format,
                                                 ...);

/* ---- MIR context: wraps allocator and error handling for the frontend ---- */

struct MIR_context {
  MIR_alloc_t alloc;
  MIR_error_func_t error_func;
  /* Alias management for the frontend's MIR_alias() calls */
  uint32_t next_alias;
  /* Modules */
  MIR_module_t curr_module;
  /* User data — used by c2sljit to store c2m_ctx pointer */
  void *data;
};

typedef struct MIR_context *MIR_context_t;

/* Alias hash table support */
typedef struct {
  const char *name;
  MIR_alias_t id;
} alias_entry_t;

/* ---- Functions that the frontend calls ---- */

static inline MIR_alloc_t MIR_get_alloc (MIR_context_t ctx) { return ctx->alloc; }

static inline MIR_error_func_t MIR_get_error_func (MIR_context_t ctx) { return ctx->error_func; }

static inline void MIR_set_error_func (MIR_context_t ctx, MIR_error_func_t func) {
  ctx->error_func = func;
}

/* MIR_alias: return a unique alias id for a given name string.
   Simple implementation: linear search with a small array.  */
static inline MIR_alias_t MIR_alias (MIR_context_t ctx MIR_UNUSED, const char *name MIR_UNUSED) {
  /* Simplified: always return 0 (no aliasing info).
     A proper implementation would maintain a name->id mapping.  */
  return 0;
}

/* _MIR_name_char_p: check if ch is valid in a MIR identifier name */
static inline int _MIR_name_char_p (MIR_context_t ctx MIR_UNUSED, int ch, int first_p) {
  if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' || ch == '$'
      || ch == '.' || ch == '@')
    return TRUE;
  return !first_p && ch >= '0' && ch <= '9';
}

/* Branch code predicates (used in code gen) */
static inline int MIR_FP_branch_code_p (MIR_insn_code_t code) {
  return (code == MIR_FBEQ || code == MIR_DBEQ || code == MIR_LDBEQ || code == MIR_DBNE
          || code == MIR_LDBNE || code == MIR_FBLT || code == MIR_DBLT || code == MIR_LDBLT
          || code == MIR_FBLE || code == MIR_DBLE || code == MIR_LDBLE || code == MIR_FBGT
          || code == MIR_DBGT || code == MIR_LDBGT || code == MIR_FBGE || code == MIR_DBGE
          || code == MIR_LDBGE);
}

static inline int MIR_call_code_p (MIR_insn_code_t code) {
  return code == MIR_CALL || code == MIR_INLINE || code == MIR_JCALL;
}

static inline int MIR_int_branch_code_p (MIR_insn_code_t code) {
  return (code == MIR_BT || code == MIR_BTS || code == MIR_BF || code == MIR_BFS || code == MIR_BEQ
          || code == MIR_BEQS || code == MIR_BNE || code == MIR_BNES || code == MIR_BLT
          || code == MIR_BLTS || code == MIR_UBLT || code == MIR_UBLTS || code == MIR_BLE
          || code == MIR_BLES || code == MIR_UBLE || code == MIR_UBLES || code == MIR_BGT
          || code == MIR_BGTS || code == MIR_UBGT || code == MIR_UBGTS || code == MIR_BGE
          || code == MIR_BGES || code == MIR_UBGE || code == MIR_UBGES || code == MIR_BO
          || code == MIR_UBO || code == MIR_BNO || code == MIR_UBNO);
}

static inline int MIR_branch_code_p (MIR_insn_code_t code) {
  return (code == MIR_JMP || MIR_int_branch_code_p (code) || MIR_FP_branch_code_p (code));
}

static inline int MIR_any_branch_code_p (MIR_insn_code_t code) {
  return (MIR_branch_code_p (code) || code == MIR_JMPI || code == MIR_SWITCH);
}

static inline int MIR_addr_code_p (MIR_insn_code_t code) {
  return (code == MIR_ADDR || code == MIR_ADDR8 || code == MIR_ADDR16 || code == MIR_ADDR32);
}

static inline int MIR_overflow_insn_code_p (MIR_insn_code_t code) {
  return (code == MIR_ADDO || code == MIR_ADDOS || code == MIR_SUBO || code == MIR_SUBOS
          || code == MIR_MULO || code == MIR_MULOS || code == MIR_UMULO || code == MIR_UMULOS);
}

/* ---- Stub MIR API functions used by the code gen (will be replaced) ---- */
/* These exist only so the file compiles. In c2sljit, pass 4 is completely rewritten. */

static inline MIR_op_t MIR_new_int_op (MIR_context_t ctx MIR_UNUSED, int64_t v) {
  MIR_op_t op = {.mode = MIR_OP_INT};
  op.u.i = v;
  return op;
}

static inline MIR_op_t MIR_new_uint_op (MIR_context_t ctx MIR_UNUSED, uint64_t v) {
  MIR_op_t op = {.mode = MIR_OP_UINT};
  op.u.u = v;
  return op;
}

static inline MIR_op_t MIR_new_float_op (MIR_context_t ctx MIR_UNUSED, float v) {
  MIR_op_t op = {.mode = MIR_OP_FLOAT};
  op.u.f = v;
  return op;
}

static inline MIR_op_t MIR_new_double_op (MIR_context_t ctx MIR_UNUSED, double v) {
  MIR_op_t op = {.mode = MIR_OP_DOUBLE};
  op.u.d = v;
  return op;
}

static inline MIR_op_t MIR_new_ldouble_op (MIR_context_t ctx MIR_UNUSED, long double v) {
  MIR_op_t op = {.mode = MIR_OP_LDOUBLE};
  op.u.ld = v;
  return op;
}

static inline MIR_op_t MIR_new_ref_op (MIR_context_t ctx MIR_UNUSED, MIR_item_t item) {
  MIR_op_t op = {.mode = MIR_OP_REF};
  op.u.ref = item;
  return op;
}

static inline MIR_op_t MIR_new_str_op (MIR_context_t ctx MIR_UNUSED, MIR_str_t str) {
  MIR_op_t op = {.mode = MIR_OP_STR};
  op.u.str = str;
  return op;
}

static inline MIR_op_t MIR_new_reg_op (MIR_context_t ctx MIR_UNUSED, MIR_reg_t reg) {
  MIR_op_t op = {.mode = MIR_OP_REG};
  op.u.reg = reg;
  return op;
}

static inline MIR_op_t MIR_new_mem_op (MIR_context_t ctx MIR_UNUSED, MIR_type_t type,
                                        MIR_disp_t disp, MIR_reg_t base, MIR_reg_t index,
                                        MIR_scale_t scale) {
  MIR_op_t op = {.mode = MIR_OP_MEM};
  op.u.mem.type = type;
  op.u.mem.disp = disp;
  op.u.mem.base = base;
  op.u.mem.index = index;
  op.u.mem.scale = scale;
  op.u.mem.alias = 0;
  op.u.mem.nonalias = 0;
  op.u.mem.nloc = 0;
  return op;
}

static inline MIR_op_t MIR_new_alias_mem_op (MIR_context_t ctx MIR_UNUSED, MIR_type_t type,
                                              MIR_disp_t disp, MIR_reg_t base, MIR_reg_t index,
                                              MIR_scale_t scale, MIR_alias_t alias,
                                              MIR_alias_t nonalias) {
  MIR_op_t op = MIR_new_mem_op (ctx, type, disp, base, index, scale);
  op.u.mem.alias = alias;
  op.u.mem.nonalias = nonalias;
  return op;
}

static inline MIR_op_t MIR_new_label_op (MIR_context_t ctx MIR_UNUSED, MIR_insn_t label) {
  MIR_op_t op = {.mode = MIR_OP_LABEL};
  (void) label;
  return op;
}

#endif /* MIR_COMPAT_H */
