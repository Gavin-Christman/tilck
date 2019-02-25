/* SPDX-License-Identifier: BSD-2-Clause */

#pragma GCC diagnostic push

static enum term_fret
tty_def_state_esc(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   term_write_filter_ctx_t *const ctx = ctx_arg;

   ctx->state = TERM_WFILTER_STATE_ESC1;
   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_nl(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   term_write_filter_ctx_t *const ctx = ctx_arg;

   if (ctx->t->c_term.c_oflag & (OPOST | ONLCR)) {

      *a = (term_action) {
         .type3 = a_dwrite_no_filter,
         .len = 2,
         .col = *color,
         .ptr = (uptr)"\n\r"
      };

      return TERM_FILTER_WRITE_BLANK;
   }

   return TERM_FILTER_WRITE_C;
}

static enum term_fret
tty_def_state_keep(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   return TERM_FILTER_WRITE_C;
}

static enum term_fret
tty_def_state_ignore(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_shift_out(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   term_write_filter_ctx_t *const ctx = ctx_arg;

   /* shift out: use alternate charset G1 */
   ctx->t->c_set = 1;

   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_shift_in(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   term_write_filter_ctx_t *const ctx = ctx_arg;

   /* shift in: return to the default charset G0 */
   ctx->t->c_set = 0;

   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_verase(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   *a = (term_action) {
      .type1 = a_del,
      .arg = TERM_DEL_PREV_CHAR
   };

   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_vwerase(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   *a = (term_action) {
      .type1 = a_del,
      .arg = TERM_DEL_PREV_WORD
   };

   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_vkill(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   /* TODO: add support for VKILL */
   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_def_state_csi(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   term_write_filter_ctx_t *const ctx = ctx_arg;
   ctx->state = TERM_WFILTER_STATE_ESC2_CSI;
   ctx->pbc = ctx->ibc = 0;
   return TERM_FILTER_WRITE_BLANK;
}

void tty_update_default_state_tables(tty *t)
{
   const struct termios *const c_term = &t->c_term;
   bzero(t->default_state_funcs, sizeof(t->default_state_funcs));

   t->default_state_funcs['\n'] = tty_def_state_nl;
   t->default_state_funcs['\r'] = tty_def_state_keep;
   t->default_state_funcs['\a'] = tty_def_state_ignore;
   t->default_state_funcs['\f'] = tty_def_state_ignore;
   t->default_state_funcs['\v'] = tty_def_state_ignore;
   t->default_state_funcs['\033'] = tty_def_state_esc;
   t->default_state_funcs['\016'] = tty_def_state_shift_out;
   t->default_state_funcs['\017'] = tty_def_state_shift_in;
   t->default_state_funcs[0x7f] = tty_def_state_ignore;
   t->default_state_funcs[0x9b] = tty_def_state_csi;

   t->default_state_funcs[c_term->c_cc[VERASE]] = tty_def_state_verase;
   t->default_state_funcs[c_term->c_cc[VWERASE]] = tty_def_state_vwerase;
   t->default_state_funcs[c_term->c_cc[VKILL]] = tty_def_state_vkill;
}

static enum term_fret
tty_def_print_untrasl_char(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   term_write_filter_ctx_t *const ctx = ctx_arg;
   int len = snprintk(ctx->tmpbuf, sizeof(ctx->tmpbuf), "{0x%x}", *c);

   *a = (term_action) {
      .type3 = a_dwrite_no_filter,
      .len = (u32)len,
      .col = *color,
      .ptr = (uptr)ctx->tmpbuf
   };

   return TERM_FILTER_WRITE_BLANK;
}

static enum term_fret
tty_handle_default_state(u8 *c, u8 *color, term_action *a, void *ctx_arg)
{
   term_write_filter_ctx_t *const ctx = ctx_arg;
   tty *const t = ctx->t;
   s16 tv = t->c_sets_tables[t->c_set][*c];

   if (tv >= 0) {
      *c = (u8) tv;
      return TERM_FILTER_WRITE_C;
   }

   if (t->default_state_funcs[*c])
      return t->default_state_funcs[*c](c, color, a, ctx_arg);

   /* unknown character */
   *c = '?';
   return TERM_FILTER_WRITE_C;

   // DEBUG: uncomment for debugging untranslated characters
   // return tty_def_print_untrasl_char(c, color, a, ctx_arg);
}
