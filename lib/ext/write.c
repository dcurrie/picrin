/**
 * See Copyright Notice in picrin.h
 */

#include "picrin.h"
#include "picrin/extra.h"
#include "../value.h"
#include "../object.h"

#if PIC_USE_WRITE

struct writer_control {
  int mode;
  int op;
  int cnt;
  pic_value shared;            /* is object shared? (yes if >0) */
  pic_value labels;            /* object -> int */
};

#define WRITE_MODE 1
#define DISPLAY_MODE 2

#define OP_WRITE 1
#define OP_WRITE_SHARED 2
#define OP_WRITE_SIMPLE 3

static void
writer_control_init(pic_state *pic, struct writer_control *p, int mode, int op)
{
  p->mode = mode;
  p->op = op;
  p->cnt = 0;
  p->shared = pic_make_attr(pic);
  p->labels = pic_make_attr(pic);
}

static void
traverse(pic_state *pic, pic_value obj, struct writer_control *p)
{
  pic_value shared = p->shared;

  if (p->op == OP_WRITE_SIMPLE) {
    return;
  }

  switch (pic_type(pic, obj)) {
  case PIC_TYPE_PAIR:
  case PIC_TYPE_VECTOR:
  case PIC_TYPE_DICT:
  case PIC_TYPE_RECORD: {

    if (! pic_attr_has(pic, shared, obj)) {
      /* first time */
      pic_attr_set(pic, shared, obj, pic_int_value(pic, 0));

      if (pic_pair_p(pic, obj)) {
        /* pair */
        traverse(pic, pic_car(pic, obj), p);
        traverse(pic, pic_cdr(pic, obj), p);
      } else if (pic_vec_p(pic, obj)) {
        /* vector */
        int i, len = pic_vec_len(pic, obj);
        for (i = 0; i < len; ++i) {
          traverse(pic, pic_vec_ref(pic, obj, i), p);
        }
      } else if (pic_dict_p(pic, obj)) {
        /* dictionary */
        int it = 0;
        pic_value val;
        while (pic_dict_next(pic, obj, &it, NULL, &val)) {
          traverse(pic, val, p);
        }
      } else {
        /* record */
        traverse(pic, pic_record_datum(pic, obj), p);
      }

      if (p->op == OP_WRITE) {
        if (pic_int(pic, pic_attr_ref(pic, shared, obj)) == 0) {
          pic_attr_del(pic, shared, obj);
        }
      }
    } else {
      /* second time */
      pic_attr_set(pic, shared, obj, pic_int_value(pic, 1));
    }
    break;
  }
  default:
    break;
  }
}

static bool
is_shared_object(pic_state *pic, pic_value obj, struct writer_control *p) {
  pic_value shared = p->shared;

  if (! pic_obj_p(pic, obj)) {
    return false;
  }
  if (! pic_attr_has(pic, shared, obj)) {
    return false;
  }
  return pic_int(pic, pic_attr_ref(pic, shared, obj)) > 0;
}

static void
write_symbol(pic_state *pic, pic_value sym, pic_value port)
{
  int len;
  const char *buf = pic_str(pic, pic_sym_name(pic, sym), &len);

  pic_fwrite(pic, buf, len, 1, port);
}

static void
write_blob(pic_state *pic, pic_value blob, pic_value port)
{
  const unsigned char *buf;
  int len, i;

  buf = pic_blob(pic, blob, &len);

  pic_fprintf(pic, port, "#u8(");
  for (i = 0; i < len; ++i) {
    pic_fprintf(pic, port, "%d", buf[i]);
    if (i + 1 < len) {
      pic_fprintf(pic, port, " ");
    }
  }
  pic_fprintf(pic, port, ")");
}

static void
write_char(pic_state *pic, pic_value ch, pic_value port, struct writer_control *p)
{
  char c = pic_char(pic, ch);

  if (p->mode == DISPLAY_MODE) {
    pic_fputc(pic, c, port);
    return;
  }
  switch (c) {
  default: pic_fprintf(pic, port, "#\\%c", c); break;
  case '\a': pic_fprintf(pic, port, "#\\alarm"); break;
  case '\b': pic_fprintf(pic, port, "#\\backspace"); break;
  case 0x7f: pic_fprintf(pic, port, "#\\delete"); break;
  case 0x1b: pic_fprintf(pic, port, "#\\escape"); break;
  case '\n': pic_fprintf(pic, port, "#\\newline"); break;
  case '\r': pic_fprintf(pic, port, "#\\return"); break;
  case ' ': pic_fprintf(pic, port, "#\\space"); break;
  case '\t': pic_fprintf(pic, port, "#\\tab"); break;
  }
}

static void
write_str(pic_state *pic, pic_value str, pic_value port, struct writer_control *p)
{
  int i, len;
  const char *buf = pic_str(pic, str, &len);

  if (p->mode == DISPLAY_MODE) {
    pic_fwrite(pic, buf, len, 1, port);
    return;
  }
  pic_fputc(pic, '"', port);
  for (i = 0; i < len; ++i) {
    if (buf[i] == '"' || buf[i] == '\\') {
      pic_fputc(pic, '\\', port);
    }
    pic_fputc(pic, buf[i], port);
  }
  pic_fputc(pic, '"', port);
}

static void
write_float(pic_state *pic, pic_value flo, pic_value port)
{
  double f = pic_float(pic, flo);

  if (f != f) {
    pic_fprintf(pic, port, "+nan.0");
  } else if (f == 1.0 / 0.0) {
    pic_fprintf(pic, port, "+inf.0");
  } else if (f == -1.0 / 0.0) {
    pic_fprintf(pic, port, "-inf.0");
  } else {
    pic_fprintf(pic, port, "%f", f);
  }
}

static void write_core(pic_state *, pic_value, pic_value port, struct writer_control *p);

static void
write_pair_help(pic_state *pic, pic_value pair, pic_value port, struct writer_control *p)
{
  pic_value cdr = pic_cdr(pic, pair);

  write_core(pic, pic_car(pic, pair), port, p);

  if (pic_nil_p(pic, cdr)) {
    return;
  }
  else if (pic_pair_p(pic, cdr) && ! is_shared_object(pic, cdr, p)) {
    pic_fprintf(pic, port, " ");
    write_pair_help(pic, cdr, port, p);
  }
  else {
    pic_fprintf(pic, port, " . ");
    write_core(pic, cdr, port, p);
  }
}

#define EQ(sym, lit) (pic_eq_p(pic, sym, pic_intern_lit(pic, lit)))

static void
write_pair(pic_state *pic, pic_value pair, pic_value port, struct writer_control *p)
{
  pic_value tag;

  if (pic_pair_p(pic, pic_cdr(pic, pair)) && pic_nil_p(pic, pic_cddr(pic, pair)) && pic_sym_p(pic, pic_car(pic, pair))) {
    tag = pic_car(pic, pair);
    if (EQ(tag, "quote")) {
      pic_fprintf(pic, port, "'");
      write_core(pic, pic_cadr(pic, pair), port, p);
      return;
    }
    else if (EQ(tag, "unquote")) {
      pic_fprintf(pic, port, ",");
      write_core(pic, pic_cadr(pic, pair), port, p);
      return;
    }
    else if (EQ(tag, "unquote-splicing")) {
      pic_fprintf(pic, port, ",@");
      write_core(pic, pic_cadr(pic, pair), port, p);
      return;
    }
    else if (EQ(tag, "quasiquote")) {
      pic_fprintf(pic, port, "`");
      write_core(pic, pic_cadr(pic, pair), port, p);
      return;
    }
    else if (EQ(tag, "syntax-quote")) {
      pic_fprintf(pic, port, "#'");
      write_core(pic, pic_cadr(pic, pair), port, p);
      return;
    }
    else if (EQ(tag, "syntax-unquote")) {
      pic_fprintf(pic, port, "#,");
      write_core(pic, pic_cadr(pic, pair), port, p);
      return;
    }
    else if (EQ(tag, "syntax-unquote-splicing")) {
      pic_fprintf(pic, port, "#,@");
      write_core(pic, pic_cadr(pic, pair), port, p);
      return;
    }
    else if (EQ(tag, "syntax-quasiquote")) {
      pic_fprintf(pic, port, "#`");
      write_core(pic, pic_cadr(pic, pair), port, p);
      return;
    }
  }
  pic_fprintf(pic, port, "(");
  write_pair_help(pic, pair, port, p);
  pic_fprintf(pic, port, ")");
}

static void
write_vec(pic_state *pic, pic_value vec, pic_value port, struct writer_control *p)
{
  int i, len = pic_vec_len(pic, vec);

  pic_fprintf(pic, port, "#(");
  for (i = 0; i < len; ++i) {
    write_core(pic, pic_vec_ref(pic, vec, i), port, p);
    if (i + 1 < len) {
      pic_fprintf(pic, port, " ");
    }
  }
  pic_fprintf(pic, port, ")");
}

static void
write_dict(pic_state *pic, pic_value dict, pic_value port, struct writer_control *p)
{
  pic_value key, val;
  int it = 0;

  pic_fprintf(pic, port, "#.(dictionary");
  while (pic_dict_next(pic, dict, &it, &key, &val)) {
    pic_fputs(pic, " '", port);
    write_symbol(pic, key, port);
    pic_fputc(pic, ' ', port);
    write_core(pic, val, port, p);
  }
  pic_fprintf(pic, port, ")");
}

static void
write_record(pic_state *pic, pic_value obj, pic_value port, struct writer_control *p)
{
  pic_fprintf(pic, port, "#<");
  write_core(pic, pic_record_type(pic, obj), port, p);
  pic_fprintf(pic, port, " ");
  write_core(pic, pic_record_datum(pic, obj), port, p);
  pic_fprintf(pic, port, ">");
}

static const char *
typename(pic_state *pic, pic_value obj)
{
  switch (pic_type(pic, obj)) {
  case PIC_TYPE_NIL:
    return "null";
  case PIC_TYPE_TRUE:
  case PIC_TYPE_FALSE:
    return "boolean";
  case PIC_TYPE_FLOAT:
    return "float";
  case PIC_TYPE_INT:
    return "int";
  case PIC_TYPE_SYMBOL:
    return "symbol";
  case PIC_TYPE_CHAR:
    return "char";
  case PIC_TYPE_EOF:
    return "eof-object";
  case PIC_TYPE_UNDEF:
    return "undefined";
  case PIC_TYPE_INVALID:
    return "invalid";
  case PIC_TYPE_PAIR:
    return "pair";
  case PIC_TYPE_STRING:
    return "string";
  case PIC_TYPE_VECTOR:
    return "vector";
  case PIC_TYPE_BLOB:
    return "bytevector";
  case PIC_TYPE_FRAME:
    return "frame";
  case PIC_TYPE_IREP:
    return "irep";
  case PIC_TYPE_PROC_FUNC:
  case PIC_TYPE_PROC_IREP:
    return "procedure";
  case PIC_TYPE_DATA:
    return "data";
  case PIC_TYPE_DICT:
    return "dictionary";
  case PIC_TYPE_ATTR:
    return "attribute";
  case PIC_TYPE_RECORD:
    return "record";
  default:
    pic_error(pic, "typename: invalid type given", 1, obj);
  }
}

static void
write_core(pic_state *pic, pic_value obj, pic_value port, struct writer_control *p)
{
  pic_value labels = p->labels;
  int i;

  /* shared objects */
  if (is_shared_object(pic, obj, p)) {
    if (pic_attr_has(pic, labels, obj)) {
      pic_fprintf(pic, port, "#%d#", pic_int(pic, pic_attr_ref(pic, labels, obj)));
      return;
    }
    i = p->cnt++;
    pic_fprintf(pic, port, "#%d=", i);
    pic_attr_set(pic, labels, obj, pic_int_value(pic, i));
  }

  switch (pic_type(pic, obj)) {
  case PIC_TYPE_UNDEF:
    pic_fprintf(pic, port, "#undefined");
    break;
  case PIC_TYPE_NIL:
    pic_fprintf(pic, port, "()");
    break;
  case PIC_TYPE_TRUE:
    pic_fprintf(pic, port, "#t");
    break;
  case PIC_TYPE_FALSE:
    pic_fprintf(pic, port, "#f");
    break;
  case PIC_TYPE_EOF:
    pic_fprintf(pic, port, "#.(eof-object)");
    break;
  case PIC_TYPE_INT:
    pic_fprintf(pic, port, "%d", pic_int(pic, obj));
    break;
  case PIC_TYPE_SYMBOL:
    write_symbol(pic, obj, port);
    break;
  case PIC_TYPE_FLOAT:
    write_float(pic, obj, port);
    break;
  case PIC_TYPE_BLOB:
    write_blob(pic, obj, port);
    break;
  case PIC_TYPE_CHAR:
    write_char(pic, obj, port, p);
    break;
  case PIC_TYPE_STRING:
    write_str(pic, obj, port, p);
    break;
  case PIC_TYPE_PAIR:
    write_pair(pic, obj, port, p);
    break;
  case PIC_TYPE_VECTOR:
    write_vec(pic, obj, port, p);
    break;
  case PIC_TYPE_DICT:
    write_dict(pic, obj, port, p);
    break;
  case PIC_TYPE_RECORD:
    write_record(pic, obj, port, p);
    break;
  default:
    pic_fprintf(pic, port, "#<%s %p>", typename(pic, obj), pic_ptr(pic, obj));
    break;
  }

  if (p->op == OP_WRITE) {
    if (is_shared_object(pic, obj, p)) {
      pic_attr_del(pic, labels, obj);
    }
  }
}

static void
write_value(pic_state *pic, pic_value obj, pic_value port, int mode, int op)
{
  struct writer_control p;

  writer_control_init(pic, &p, mode, op);

  traverse(pic, obj, &p);

  write_core(pic, obj, port, &p);
}

static pic_value
pic_write_write(pic_state *pic)
{
  pic_value v, port = pic_stdout(pic);

  pic_get_args(pic, "o|o", &v, &port);
  write_value(pic, v, port, WRITE_MODE, OP_WRITE);
  return pic_undef_value(pic);
}

static pic_value
pic_write_write_simple(pic_state *pic)
{
  pic_value v, port = pic_stdout(pic);

  pic_get_args(pic, "o|o", &v, &port);
  write_value(pic, v, port, WRITE_MODE, OP_WRITE_SIMPLE);
  return pic_undef_value(pic);
}

static pic_value
pic_write_write_shared(pic_state *pic)
{
  pic_value v, port = pic_stdout(pic);

  pic_get_args(pic, "o|o", &v, &port);
  write_value(pic, v, port, WRITE_MODE, OP_WRITE_SHARED);
  return pic_undef_value(pic);
}

static pic_value
pic_write_display(pic_state *pic)
{
  pic_value v, port = pic_stdout(pic);

  pic_get_args(pic, "o|o", &v, &port);
  write_value(pic, v, port, DISPLAY_MODE, OP_WRITE);
  return pic_undef_value(pic);
}

void
pic_init_write(pic_state *pic)
{
  pic_defun(pic, "write", pic_write_write);
  pic_defun(pic, "write-simple", pic_write_write_simple);
  pic_defun(pic, "write-shared", pic_write_write_shared);
  pic_defun(pic, "display", pic_write_display);
}

#endif
