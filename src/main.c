#include <stdio.h>

#include "picrin.h"

#if PIC_ENABLE_READLINE
# include <string.h>
# include <stdlib.h>
# include <readline/readline.h>
# include <readline/history.h>
#endif

void
test_object_creation(pic_state *pic)
{
  pic_value v;

  {
    v = pic_intern_cstr(pic, "symbol");
    pic_debug(pic, v);
    puts(" [should be `symbol`]");
  }
  {
    v = pic_nil_value();
    pic_debug(pic, v);
    puts(" [should be `()`]");
  }
  {
    v = pic_cons(pic, pic_intern_cstr(pic, "foo"), pic_intern_cstr(pic, "bar"));
    pic_debug(pic, v);
    puts(" [should be `(foo . bar)`]");
  }
}

#define CODE_MAX_LENGTH 1024
#define LINE_MAX_LENGTH 256

int
main()
{
  pic_state *pic;
  char code[CODE_MAX_LENGTH] = "", line[LINE_MAX_LENGTH];
  char last_char, *read_line, *prompt;
  int char_index;
  pic_value v;
  struct pic_proc *proc;
  int ai;
  bool r;

  pic = pic_open();

#if OBJECT_CREATION_DEBUG
  test_object_creation(pic);
#endif

  ai = pic_gc_arena_preserve(pic);

  while (1) {
    prompt = code[0] == '\0' ? "> " : "* ";

#if PIC_ENABLE_READLINE
    read_line = readline(prompt);
    if (read_line == NULL) {
      line[0] = '\0';
    }
    else {
      strncpy(line, read_line, LINE_MAX_LENGTH - 1);
      add_history(read_line);
      free(read_line);
    }
#else
    printf(prompt);

    char_index = 0;
    while ((last_char = getchar()) != '\n') {
      if (last_char == EOF)
	goto eof;
      if (char_index == LINE_MAX_LENGTH)
	goto overflow;
      line[char_index++] = last_char;
    }
    line[char_index] = '\0';
#endif

    if (strlen(code) + strlen(line) >= CODE_MAX_LENGTH)
      goto overflow;
    strcat(code, line);

    /* read */
    r = pic_parse(pic, code, &v);
    if (! r) {			/* wait for more input */
      pic_gc_arena_restore(pic, ai);
      continue;
    }
    code[0] = '\0';
    if (pic_undef_p(v)) {	/* parse error */
      pic_gc_arena_restore(pic, ai);
      continue;
    }

#if DEBUG
    printf("[read: ");
    pic_debug(pic, v);
    printf("]\n");
#endif

    /* eval */
    proc = pic_codegen(pic, v, pic->global_env);
    v = pic_run(pic, proc, pic_nil_value());

    /* print */
    printf("=> ");
    pic_debug(pic, v);
    printf("\n");

    pic_gc_arena_restore(pic, ai);
  }

 eof:
  puts("");
  goto exit;

 overflow:
  puts("** [fatal] line input overflow");
  goto exit;

 exit:
  pic_close(pic);

  return 0;
}
