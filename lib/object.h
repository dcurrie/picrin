/**
 * See Copyright Notice in picrin.h
 */

#ifndef PICRIN_OBJECT_H
#define PICRIN_OBJECT_H

#if defined(__cplusplus)
extern "C" {
#endif

#include "khash.h"

#if PIC_BITMAP_GC
# define OBJECT_HEADER                           \
  unsigned char tt;
#else
# define OBJECT_HEADER                           \
  unsigned char tt;                              \
  char gc_mark;
#endif

struct object;              /* defined in gc.c */

struct basic {
  OBJECT_HEADER
};

struct symbol {
  OBJECT_HEADER
  struct string *str;
};

struct pair {
  OBJECT_HEADER
  pic_value car;
  pic_value cdr;
};

struct blob {
  OBJECT_HEADER
  unsigned char *data;
  int len;
};

struct string {
  OBJECT_HEADER
  struct rope *rope;
};

KHASH_DECLARE(dict, struct symbol *, pic_value)

struct dict {
  OBJECT_HEADER
  khash_t(dict) hash;
};

KHASH_DECLARE(weak, struct object *, pic_value)

struct weak {
  OBJECT_HEADER
  khash_t(weak) hash;
  struct weak *prev;         /* for GC */
};

struct vector {
  OBJECT_HEADER
  pic_value *data;
  int len;
};

struct data {
  OBJECT_HEADER
  const pic_data_type *type;
  void *data;
};

struct record {
  OBJECT_HEADER
  pic_value type;
  pic_value datum;
};

struct code {
  int insn;
  int a;
  int b;
};

struct irep {
  OBJECT_HEADER
  int argc, localc, capturec;
  bool varg;
  struct code *code;
  struct irep **irep;
  int *ints;
  double *nums;
  struct object **pool;
  size_t ncode, nirep, nints, nnums, npool;
};

struct context {
  OBJECT_HEADER
  pic_value *regs;
  int regc;
  struct context *up;
  pic_value storage[1];
};

struct proc {
  OBJECT_HEADER
  union {
    struct {
      pic_func_t func;
      int localc;
    } f;
    struct {
      struct irep *irep;
      struct context *cxt;
    } i;
  } u;
  pic_value locals[1];
};

enum {
  FILE_READ  = 01,
  FILE_WRITE = 02,
  FILE_UNBUF = 04,
  FILE_EOF   = 010,
  FILE_ERR   = 020,
  FILE_LNBUF = 040
};

struct port {
  OBJECT_HEADER
  struct file {
    /* buffer */
    char buf[1];                  /* fallback buffer */
    long cnt;                     /* characters left */
    char *ptr;                    /* next character position */
    char *base;                   /* location of the buffer */
    /* operators */
    void *cookie;
    const pic_port_type *vtable;
    int flag;                     /* mode of the file access */
  } file;
};

struct error {
  OBJECT_HEADER
  struct symbol *type;
  struct string *msg;
  pic_value irrs;
  struct string *stack;
};

#define TYPENAME_int   "integer"
#define TYPENAME_blob  "bytevector"
#define TYPENAME_char  "character"
#define TYPENAME_sym   "symbol"
#define TYPENAME_error "error"
#define TYPENAME_proc  "procedure"
#define TYPENAME_str   "string"
#define TYPENAME_vec   "vector"

#define TYPE_CHECK(pic, v, type) do {                           \
    if (! pic_##type##_p(pic, v))                               \
      pic_error(pic, TYPENAME_##type " required", 1, v);        \
  } while (0)

#define VALID_INDEX(pic, len, i) do {                                   \
    if (i < 0 || len <= i) pic_error(pic, "index out of range", 1, pic_int_value(pic, i)); \
  } while (0)
#define VALID_RANGE(pic, len, s, e) do {                                \
    if (s < 0 || len < s) pic_error(pic, "invalid start index", 1, pic_int_value(pic, s)); \
    if (e < s || len < e) pic_error(pic, "invalid end index", 1, pic_int_value(pic, e)); \
  } while (0)
#define VALID_ATRANGE(pic, tolen, at, fromlen, s, e) do {               \
    VALID_INDEX(pic, tolen, at);                                        \
    VALID_RANGE(pic, fromlen, s, e);                                    \
    if (tolen - at < e - s) pic_error(pic, "invalid range", 0);        \
  } while (0)

PIC_STATIC_INLINE int
obj_tt(pic_state *PIC_UNUSED(pic), void *ptr)
{
  return ((struct basic *)ptr)->tt;
}

#if !PIC_NAN_BOXING

PIC_STATIC_INLINE struct object *
obj_ptr(pic_state *PIC_UNUSED(pic), pic_value v)
{
  return (struct object *)(v.u.data);
}

PIC_STATIC_INLINE bool
obj_p(pic_state *PIC_UNUSED(pic), pic_value v)
{
  return v.type > PIC_IVAL_END;
}

PIC_STATIC_INLINE pic_value
obj_value(pic_state *PIC_UNUSED(pic), void *ptr)
{
  pic_value v = pic_make_value(obj_tt(pic, ptr));
  v.u.data = ptr;
  return v;
}

#else  /* NAN_BOXING */

PIC_STATIC_INLINE struct object *
obj_ptr(pic_state *PIC_UNUSED(pic), pic_value v)
{
  return (struct object *)((0x3ffffffffffful & v.v) << 2);
}

PIC_STATIC_INLINE bool
obj_p(pic_state *PIC_UNUSED(pic), pic_value v)
{
  return v.v > ((0x3ffC0ul + (0x3f & PIC_IVAL_END)) << 46);
}

PIC_STATIC_INLINE pic_value
obj_value(pic_state *PIC_UNUSED(pic), void *ptr)
{
  pic_value v = pic_make_value(obj_tt(pic, ptr));
  v.v |= 0x3ffffffffffful & ((uint64_t)ptr >> 2);
  return v;
}

#endif  /* NAN_BOXING */

#define DEFPTR(name,type)                                               \
  PIC_STATIC_INLINE type *                                              \
  pic_##name##_ptr(pic_state *PIC_UNUSED(pic), pic_value o) {           \
    assert(pic_##name##_p(pic,o));                                      \
    return (type *) obj_ptr(pic, o);                                    \
  }

#define pic_data_p(pic,o) (pic_data_p(pic,o,NULL))
#define pic_port_p(pic,o) (pic_port_p(pic,o,NULL))
DEFPTR(sym, struct symbol)
DEFPTR(str, struct string)
DEFPTR(blob, struct blob)
DEFPTR(pair, struct pair)
DEFPTR(vec, struct vector)
DEFPTR(dict, struct dict)
DEFPTR(weak, struct weak)
DEFPTR(data, struct data)
DEFPTR(proc, struct proc)
DEFPTR(port, struct port)
DEFPTR(error, struct error)
DEFPTR(rec, struct record)
DEFPTR(irep, struct irep)
#undef pic_data_p
#undef pic_port_p

struct object *pic_obj_alloc(pic_state *, size_t, int type);

pic_value pic_make_proc(pic_state *, pic_func_t, int, pic_value *);
pic_value pic_make_proc_irep(pic_state *, struct irep *, struct context *);
pic_value pic_make_record(pic_state *, pic_value type, pic_value datum);
pic_value pic_record_type(pic_state *pic, pic_value record);
pic_value pic_record_datum(pic_state *pic, pic_value record);

struct rope *pic_rope_incref(struct rope *);
void pic_rope_decref(pic_state *, struct rope *);

struct cont *pic_alloca_cont(pic_state *);
pic_value pic_make_cont(pic_state *, struct cont *);
void pic_save_point(pic_state *, struct cont *, PIC_JMPBUF *);
void pic_exit_point(pic_state *);

void pic_warnf(pic_state *pic, const char *fmt, ...); /* deprecated */

#if defined(__cplusplus)
}
#endif

#endif