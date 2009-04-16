/* radare - LGPL - Copyright 2009 nibble<.ds@gmail.com> */

#include <stdio.h>

#include <r_types.h>
#include <r_util.h>
#include <r_cmd.h>
#include <r_asm.h>
#include <list.h>
#include "../config.h"

static struct r_asm_handle_t *asm_static_plugins[] = 
	{ R_ASM_STATIC_PLUGINS };

static int r_asm_asciz(void *data, const char *input)
{
	int len = 0;
	struct r_asm_aop_t **aop = (struct r_asm_aop_t**)data;
	char *arg = strchr(input, ' ');
	if (arg && (len = strlen(arg+1))) {
		arg += 1; len += 1;
		r_hex_bin2str((u8*)arg, len, (*aop)->buf_hex);
		strncpy((char*)(*aop)->buf, arg, 1024);
	}
	return len;
}

static int r_asm_byte(void *data, const char *input)
{
	int len = 0;
	struct r_asm_aop_t **aop = (struct r_asm_aop_t**)data;
	char *arg = strchr(input, ' ');
	if (arg) {
		arg += 1;
		len = r_hex_str2bin(arg, (*aop)->buf);
		strncpy((*aop)->buf_hex, r_str_trim(arg), 1024);
	}
	return len;
}

R_API struct r_asm_t *r_asm_new()
{
	struct r_asm_t *a = MALLOC_STRUCT(struct r_asm_t);
	r_asm_init(a);
	return a;
}

R_API void r_asm_free(struct r_asm_t *a)
{
	free(a);
}

R_API int r_asm_init(struct r_asm_t *a)
{
	int i;
	a->user = NULL;
	a->cur = NULL;
	INIT_LIST_HEAD(&a->asms);
	a->bits = 32;
	a->big_endian = 0;
	a->syntax = R_ASM_SYN_INTEL;
	a->pc = 0;
	for(i=0;asm_static_plugins[i];i++)
		r_asm_add(a, asm_static_plugins[i]);
	r_cmd_init(&a->cmd);
	r_cmd_add(&a->cmd, "a", ".asciz", &r_asm_asciz);
	r_cmd_add_long(&a->cmd, "asciz", "a", ".asciz");
	r_cmd_add(&a->cmd, "b", ".byte", &r_asm_byte);
	r_cmd_add_long(&a->cmd, "byte", "b", ".byte");
	return R_TRUE;
}

R_API void r_asm_set_user_ptr(struct r_asm_t *a, void *user)
{
	a->user = user;
}

R_API int r_asm_add(struct r_asm_t *a, struct r_asm_handle_t *foo)
{
	struct list_head *pos;
	if (foo->init)
		foo->init(a->user);
	/* avoid dupped plugins */
	list_for_each_prev(pos, &a->asms) {
		struct r_asm_handle_t *h = list_entry(pos, struct r_asm_handle_t, list);
		if (!strcmp(h->name, foo->name))
			return R_FALSE;
	}
	
	list_add_tail(&(foo->list), &(a->asms));
	return R_TRUE;
}

R_API int r_asm_del(struct r_asm_t *a, const char *name)
{
	/* TODO: Implement r_asm_del */
	return R_FALSE;
}

R_API int r_asm_list(struct r_asm_t *a)
{
	struct list_head *pos;
	list_for_each_prev(pos, &a->asms) {
		struct r_asm_handle_t *h = list_entry(pos, struct r_asm_handle_t, list);
		printf(" %s: %s\n", h->name, h->desc);
	}
	return R_FALSE;
}

R_API int r_asm_set(struct r_asm_t *a, const char *name)
{
	struct list_head *pos;
	list_for_each_prev(pos, &a->asms) {
		struct r_asm_handle_t *h = list_entry(pos, struct r_asm_handle_t, list);
		if (!strcmp(h->name, name)) {
			a->cur = h;
			return R_TRUE;
		}
	}
	return R_FALSE;
}

static int has_bits(struct r_asm_handle_t *h, int bits)
{
	int i;
	if (h && h->bits) {
		for(i=0; h->bits[i]; i++) {
			if (bits == h->bits[i])
				return R_TRUE;
		}
	}
	return R_FALSE;
}


R_API int r_asm_set_bits(struct r_asm_t *a, int bits)
{
	if (has_bits(a->cur, bits)) {
		a->bits = bits;
		return R_TRUE;
	}
	return R_FALSE;
}

R_API int r_asm_set_big_endian(struct r_asm_t *a, int boolean)
{
	a->big_endian = boolean;
	return R_TRUE;
}

R_API int r_asm_set_syntax(struct r_asm_t *a, int syntax)
{
	switch (syntax) {
	case R_ASM_SYN_INTEL:
	case R_ASM_SYN_ATT:
		a->syntax = syntax;
		return R_TRUE;
	default:
		return R_FALSE;
	}
}

R_API int r_asm_set_pc(struct r_asm_t *a, u64 pc)
{
	a->pc = pc;
	return R_TRUE;
}

R_API int r_asm_disassemble(struct r_asm_t *a, struct r_asm_aop_t *aop, u8 *buf, u64 len)
{
	int ret = 0;
	if (a->cur && a->cur->disassemble)
		ret = a->cur->disassemble(a, aop, buf, len);
	if (ret > 0) {
		memcpy(aop->buf, buf, ret);
		r_hex_bin2str(buf, ret, aop->buf_hex);
	}
	return ret;
}

R_API int r_asm_assemble(struct r_asm_t *a, struct r_asm_aop_t *aop, const char *buf)
{
	int ret = 0;
	struct list_head *pos;
	if (a->cur) {
		if (a->cur->assemble)
			ret = a->cur->assemble(a, aop, buf);
		else /* find callback if no assembler support in current plugin */
			list_for_each_prev(pos, &a->asms) {
				struct r_asm_handle_t *h = list_entry(pos, struct r_asm_handle_t, list);
				if (h->arch && h->assemble && has_bits(h, a->bits) && !strcmp(a->cur->arch, h->arch)) {
					ret = h->assemble(a, aop, buf);
					break;
				}
			}
	}
	if (aop && ret > 0) {
		r_hex_bin2str(aop->buf, ret, aop->buf_hex);
		strncpy(aop->buf_asm, buf, 1024);
	}
	return ret;
}

R_API int r_asm_massemble(struct r_asm_t *a, struct r_asm_aop_t *aop, char *buf)
{
	struct {
		char name[256];
		u64 offset;
	} flags[1024];
	char *lbuf = NULL, buf_hex[1024], *ptr = NULL, *ptr_start = NULL, *tokens[1024],
		 *label_name = NULL, buf_token[1024], buf_token2[1024];
	u8 buf_bin[1024];
	int labels = 0, stage, ret, idx, ctr, i, j;
	u64 label_offset;

	if (buf == NULL)
		return 0;
	lbuf = strdup(buf);

	if (strchr(lbuf, '_'))
		labels = 1;

	for (tokens[0] = lbuf, ctr = 0;
		(ptr = strchr(tokens[ctr], ';'));
		tokens[++ctr] = ptr+1)
			*ptr = '\0';

	r_cmd_set_data(&a->cmd, &aop);

	/* Stage 1: Parse labels*/
	/* Stage 2: Assemble */
	for (stage = 0; stage < 2; stage++) {
		if (stage == 0 && !labels)
			continue;
		for (ret = i = j = 0, label_offset = a->pc; i <= ctr && j < 1024; i++, label_offset+=ret) {
			strncpy(buf_token, tokens[i], 1024);
			if (stage == 1)
				r_asm_set_pc(a, a->pc + ret);
			if (labels) { /* Labels */
				for (ptr_start = buf_token;*ptr_start&&isseparator(*ptr_start);ptr_start++);
				while ((ptr = strchr(ptr_start, '_'))) {
					if ((label_name = r_str_word_get_first(ptr))) {
						if ((ptr == ptr_start)) {
							if (stage == 0) {
								strncpy(flags[j].name, label_name, 256);
								flags[j].offset = label_offset;
								j++;
							}
							ptr_start += strlen(label_name)+1;
						} else {
							*ptr = '\0';
							if (stage == 1) {
								for (j = 0; j < 1024; j++)
									if (!strcmp(label_name, flags[j].name)) {
										label_offset = flags[j].offset;
										break;
									}
								if (j == 1024)
									return 0;
							}
							snprintf(buf_token2, 1024, "%s0x%llx%s",
									ptr_start, label_offset, ptr+strlen(label_name));
							strncpy(buf_token, buf_token2, 1024);
							ptr_start = buf_token;
						}
						free(label_name);
					}
				}
			} else {
				ptr_start = tokens[i];
			}
			if ((ptr = strchr(ptr_start, '.'))) /* Pseudo */
				ret = r_cmd_call_long(&a->cmd, ptr+1);
			else /* Instruction */
				ret = r_asm_assemble(a, aop, ptr_start);
			if (!ret) {
				return 0;
			} else if (stage == 1) {
				for (j = 0; j < ret && idx+j < 1024; j++)
					buf_bin[idx+j] = aop->buf[j];
				strcat(buf_hex, aop->buf_hex);
			}
		}
	}
	
	memcpy(aop->buf, buf_bin, 1024);
	memcpy(aop->buf_hex, buf_hex, 1024);

	return idx;
}
