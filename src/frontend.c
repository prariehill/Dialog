#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "arena.h"
#include "ast.h"
#include "accesspred.h"
#include "frontend.h"
#include "compile.h"
#include "eval.h"
#include "parse.h"
#include "report.h"
#include "unicode.h"

struct predlist {
	struct predlist		*next;
	struct predicate	*pred;
};

char **sourcefile;
int nsourcefile;

static struct predicate **tracequeue;
static int tracequeue_r, tracequeue_w;

struct specialspec {
	int id;
	int prednameflags;
	int nword;
	char *word[8];
} specialspec[] = {
	{SP_AND_CHECK,		0,				3,	{"and", "check", 0}},
	{SP_RANDOM,		0,				2,	{"at", "random"}},
	{SP_COLLECT,		0,				2,	{"collect", 0}},
	{SP_COLLECT_WORDS,	0,				2,	{"collect", "words"}},
	{SP_CYCLING,		0,				1,	{"cycling"}},
	{SP_ENDIF,		0,				1,	{"endif"}},
	{SP_ELSE,		0,				1,	{"else"}},
	{SP_ELSEIF,		0,				1,	{"elseif"}},
	{SP_EXHAUST,		0,				1,	{"exhaust"}},
	{SP_GENERATE,		PREDNF_META,			3,	{"generate", 0, 0}},
	{SP_GLOBAL_VAR,		PREDNF_META,			3,	{"global", "variable", 0}},
	{SP_GLOBAL_VAR_2,	PREDNF_META,			4,	{"global", "variable", 0, 0}},
	{SP_IF,			0,				1,	{"if"}},
	{SP_INTO,		0,				2,	{"into", 0}},
	{SP_JUST,		0,				1,	{"just"}},
	{SP_NOW,		0,				1,	{"now"}},
	{SP_OR,			0,				1,	{"or"}},
	{SP_P_RANDOM,		0,				3,	{"purely", "at", "random"}},
	{SP_SELECT,		0,				1,	{"select"}},
	{SP_STATUSBAR,		0,				3,	{"status", "bar", 0}},
	{SP_STOPPABLE,		0,				1,	{"stoppable"}},
	{SP_STOPPING,		0,				1,	{"stopping"}},
	{SP_THEN,		0,				1,	{"then"}},
	{SP_T_RANDOM,		0,				3,	{"then", "at", "random"}},
	{SP_T_P_RANDOM,		0,				4,	{"then", "purely", "at", "random"}},
};

struct builtinspec {
	int id;
	int prednameflags;
	int predflags;
	int nword;
	char *word[8];
} builtinspec[] = {
	{BI_LESSTHAN,		0, 0,				3,	{0, "<", 0}},
	{BI_GREATERTHAN,	0, 0,				3,	{0, ">", 0}},
	{BI_PLUS,		0, 0,				5,	{0, "plus", 0, "into", 0}},
	{BI_MINUS,		0, 0,				5,	{0, "minus", 0, "into", 0}},
	{BI_TIMES,		0, 0,				5,	{0, "times", 0, "into", 0}},
	{BI_DIVIDED,		0, 0,				6,	{0, "divided", "by", 0, "into", 0}},
	{BI_MODULO,		0, 0,				5,	{0, "modulo", 0, "into", 0}},
	{BI_RANDOM,		0, 0,				7,	{"random", "from", 0, "to", 0, "into", 0}},
	{BI_FAIL,		0, PREDF_FAIL,			1,	{"fail"}},
	{BI_STOP,		0, PREDF_SUCCEEDS|PREDF_STOP,	1,	{"stop"}},
	{BI_REPEAT,		0, PREDF_SUCCEEDS,		2,	{"repeat", "forever"}},
	{BI_NUMBER,		0, 0,				2,	{"number", 0}},
	{BI_LIST,		0, 0,				2,	{"list", 0}},
	{BI_EMPTY,		0, 0,				2,	{"empty", 0}},
	{BI_NONEMPTY,		0, 0,				2,	{"nonempty", 0}},
	{BI_WORD,		0, 0,				2,	{"word", 0}},
	{BI_OBJECT,		0, 0,				2,	{"object", 0}},
	{BI_UNBOUND,		0, 0,				2,	{"unbound", 0}},
	{BI_QUIT,		0, PREDF_SUCCEEDS,		1,	{"quit"}},
	{BI_RESTART,		0, PREDF_SUCCEEDS,		1,	{"restart"}},
	{BI_BREAKPOINT,		0, PREDF_SUCCEEDS,		1,	{"breakpoint"}},
	{BI_SAVE,		0, 0,				2,	{"save", 0}},
	{BI_RESTORE,		0, PREDF_SUCCEEDS,		1,	{"restore"}},
	{BI_SAVE_UNDO,		0, 0,				3,	{"save", "undo", 0}},
	{BI_UNDO,		0, 0,				1,	{"undo"}},
	{BI_SCRIPT_ON,		0, 0,				2,	{"transcript", "on"}},
	{BI_SCRIPT_OFF,		0, PREDF_SUCCEEDS,		2,	{"transcript", "off"}},
	{BI_TRACE_ON,		0, PREDF_SUCCEEDS,		2,	{"trace", "on"}},
	{BI_TRACE_OFF,		0, PREDF_SUCCEEDS,		2,	{"trace", "off"}},
	{BI_NOSPACE,		PREDNF_OUTPUT, PREDF_SUCCEEDS,	2,	{"no", "space"}},
	{BI_SPACE,		PREDNF_OUTPUT, PREDF_SUCCEEDS,	1,	{"space"}},
	{BI_SPACE_N,		PREDNF_OUTPUT, PREDF_SUCCEEDS,	2,	{"space", 0}},
	{BI_LINE,		PREDNF_OUTPUT, PREDF_SUCCEEDS,	1,	{"line"}},
	{BI_PAR,		PREDNF_OUTPUT, PREDF_SUCCEEDS,	1,	{"par"}},
	{BI_PAR_N,		PREDNF_OUTPUT, PREDF_SUCCEEDS,	2,	{"par", 0}},
	{BI_ROMAN,		0, PREDF_SUCCEEDS,		1,	{"roman"}},
	{BI_BOLD,		0, PREDF_SUCCEEDS,		1,	{"bold"}},
	{BI_ITALIC,		0, PREDF_SUCCEEDS,		1,	{"italic"}},
	{BI_REVERSE,		0, PREDF_SUCCEEDS,		1,	{"reverse"}},
	{BI_FIXED,		0, PREDF_SUCCEEDS,		2,	{"fixed", "pitch"}},
	{BI_UPPER,		0, PREDF_SUCCEEDS,		1,	{"uppercase"}},
	{BI_CLEAR,		0, PREDF_SUCCEEDS,		1,	{"clear"}},
	{BI_CLEAR_ALL,		0, PREDF_SUCCEEDS,		2,	{"clear", "all"}},
	{BI_WINDOWWIDTH,	0, 0,				4,	{"status", "bar", "width", 0}},
	{BI_CURSORTO,		0, PREDF_SUCCEEDS,		6,	{"cursor", "to", "row", 0, "column", 0}},
	{BI_GETINPUT,		0, 0,				3,	{"get", "input", 0}},
	{BI_GETRAWINPUT,	0, 0,				4,	{"get", "raw", "input", 0}},
	{BI_GETKEY,		0, 0,				3,	{"get", "key", 0}},
	{BI_SERIALNUMBER,	0, PREDF_SUCCEEDS,		2,	{"serial", "number"}},
	{BI_COMPILERVERSION,	0, PREDF_SUCCEEDS,		2,	{"compiler", "version"}},
	{BI_MEMSTATS,		0, PREDF_SUCCEEDS,		3,	{"display", "memory", "statistics"}},
	{BI_HASPARENT,		0, PREDF_DYNAMIC,		4,	{0, "has", "parent", 0}},
	{BI_UNIFY,		0, 0,				3,	{0, "=", 0}},
	{BI_IS_ONE_OF,		0, 0,				5,	{0, "is", "one", "of", 0}},
	{BI_SPLIT,		0, 0,				8,	{"split", 0, "by", 0, "into", 0, "and", 0}},
	{BI_WORDREP_RETURN,	0, 0,				4,	{"word", "representing", "return", 0}},
	{BI_WORDREP_SPACE,	0, 0,				4,	{"word", "representing", "space", 0}},
	{BI_WORDREP_BACKSPACE,	0, 0,				4,	{"word", "representing", "backspace", 0}},
	{BI_WORDREP_UP,		0, 0,				4,	{"word", "representing", "up", 0}},
	{BI_WORDREP_DOWN,	0, 0,				4,	{"word", "representing", "down", 0}},
	{BI_WORDREP_LEFT,	0, 0,				4,	{"word", "representing", "left", 0}},
	{BI_WORDREP_RIGHT,	0, 0,				4,	{"word", "representing", "right", 0}},
	{BI_HAVE_UNDO,		0, 0,				3,	{"interpreter", "supports", "undo"}},
	{BI_PROGRAM_ENTRY,	PREDNF_DEFINABLE_BI, 0,		3,	{"program", "entry", "point"}},
	{BI_ERROR_ENTRY,	PREDNF_DEFINABLE_BI, 0,		4,	{"error", 0, "entry", "point"}},
	{BI_STORY_IFID,		PREDNF_DEFINABLE_BI, 0,		2,	{"story", "ifid"}},
	{BI_STORY_TITLE,	PREDNF_DEFINABLE_BI, 0,		2,	{"story", "title"}},
	{BI_STORY_AUTHOR,	PREDNF_DEFINABLE_BI, 0,		2,	{"story", "author"}},
	{BI_STORY_NOUN,		PREDNF_DEFINABLE_BI, 0,		2,	{"story", "noun"}},
	{BI_STORY_BLURB,	PREDNF_DEFINABLE_BI, 0,		2,	{"story", "blurb"}},
	{BI_STORY_RELEASE,	PREDNF_DEFINABLE_BI, 0,		3,	{"story", "release", 0}},
	{BI_ENDINGS,		PREDNF_DEFINABLE_BI, 0,		3,	{"removable", "word", "endings"}},
	{BI_INJECTED_QUERY,	PREDNF_DEFINABLE_BI, 0,		3,	{"", "query", 0}},
	{BI_BREAKPOINT_AGAIN,	0, 0,				2,	{"", "breakpoint"}},
	{BI_BREAK_GETKEY,	0, 0,				2,	{"", "key"}},
	{BI_BREAK_FAIL,		0, 0,				2,	{"", "fail"}},
};

int body_can_be_fixed_flag(struct astnode *an, struct word *safevar) {
	struct predname *predname;
	int have_constrained_safevar = !safevar;
	struct astnode *sub;

	while(an) {
		if(an->kind == AN_RULE
		|| an->kind == AN_NEG_RULE) {
			predname = an->predicate;
			if(predname->builtin == BI_IS_ONE_OF
			&& (an->children[0]->kind == AN_VARIABLE)
			&& (an->children[0]->word == safevar)) {
				for(sub = an->children[1]; sub->kind == AN_PAIR; sub = sub->children[1]) {
					if(sub->children[0]->kind != AN_TAG) break;
				}
				return sub->kind == AN_EMPTY_LIST;
			} else if(predname->builtin) {
				if(predname->builtin != BI_FAIL
				&& predname->builtin != BI_OBJECT) {
					return 0;
				}
			} else if((predname->pred->flags & PREDF_FIXED_FLAG)
			&& (an->children[0]->kind == AN_VARIABLE)
			&& (an->children[0]->word == safevar)) {
				have_constrained_safevar = 1;
			} else {
				return 0;
			}
		} else if(an->kind == AN_BLOCK
		|| an->kind == AN_NEG_BLOCK) {
			if(body_can_be_fixed_flag(an->children[0], safevar)) {
				have_constrained_safevar = 1;
			} else {
				return 0;
			}
		} else if(an->kind != AN_JUST) {
			return 0;
		}
		an = an->next_in_body;
	}

	return have_constrained_safevar;
}

int pred_can_be_fixed_flag(struct predname *predname) {
	int i;
	struct predicate *pred = predname->pred;

	if(!predname->special
	&& !predname->builtin
	&& pred->nclause
	&& !pred->dynamic
	&& !(pred->flags & PREDF_MACRO)
	&& (pred->flags & PREDF_INVOKED)
	&& predname->arity == 1) {
		for(i = 0; i < pred->nclause; i++) {
			if(pred->clauses[i]->params[0]->kind == AN_TAG) {
				if(!body_can_be_fixed_flag(pred->clauses[i]->body, 0)) {
					return 0;
				}
			} else if(pred->clauses[i]->params[0]->kind == AN_VARIABLE
			&& pred->clauses[i]->params[0]->word->name[0]) {
				if(!body_can_be_fixed_flag(
					pred->clauses[i]->body,
					pred->clauses[i]->params[0]->word))
				{
					return 0;
				}
			} else {
				return 0;
			}
		}

		return 1;
	}

	return 0;
}

void find_fixed_flags(struct program *prg) {
	int i;
	struct predname *predname;
	int flag;

	do {
		flag = 0;
		for(i = 0; i < prg->npredicate; i++) {
			predname = prg->predicates[i];

			if(!(predname->pred->flags & PREDF_FIXED_FLAG)
			&& pred_can_be_fixed_flag(predname)) {
#if 0
				printf("candidate: %s\n", predname->printed_name);
#endif
				predname->pred->flags |= PREDF_DYNAMIC | PREDF_FIXED_FLAG;
				if(!predname->pred->dynamic) {
					predname->pred->dynamic = calloc(1, sizeof(struct dynamic));
				}
				if(predname->dyn_id == DYN_NONE) {
					predname->dyn_id = prg->nobjflag++;
					prg->objflagpred = realloc(prg->objflagpred, prg->nobjflag * sizeof(struct predname *));
					prg->objflagpred[predname->dyn_id] = predname;
				}
				flag = 1;
			}
		}
	} while(flag);
}

static void trace_enqueue(struct predicate *pred, int queuelen) {
	if(tracequeue && !(pred->flags & PREDF_IN_QUEUE)) {
		pred->flags |= PREDF_IN_QUEUE;
		tracequeue[tracequeue_w++] = pred;
		if(tracequeue_w == queuelen) tracequeue_w = 0;
	}
}

static struct predicate *trace_read_queue(int queuelen) {
	struct predicate *pred = 0;

	if(tracequeue_r != tracequeue_w) {
		pred = tracequeue[tracequeue_r++];
		if(tracequeue_r == queuelen) tracequeue_r = 0;
		pred->flags &= ~PREDF_IN_QUEUE;
	}

	return pred;
}

void add_bound_vars(struct astnode *an, uint8_t *bound, struct clause *cl) {
	int i;

	while(an) {
		if(an->kind == AN_VARIABLE) {
			if(an->word->name[0]) {
				for(i = 0; i < cl->nvar; i++) {
					if(an->word == cl->varnames[i]) break;
				}
				assert(i < cl->nvar);
#if 0
				if(!bound[i]) printf("now, $%s is bound\n", an->word->name);
#endif
				bound[i] = 1;
			}
		} else {
			for(i = 0; i < an->nchild; i++) {
				add_bound_vars(an->children[i], bound, cl);
			}
		}
		an = an->next_in_body;
	}
}

int any_unbound(struct astnode *an, const uint8_t *bound, struct clause *cl) {
	int i;

	while(an) {
		if(an->kind == AN_VARIABLE) {
			if(!an->word->name[0]) return 1;
			for(i = 0; i < cl->nvar; i++) {
				if(an->word == cl->varnames[i]) break;
			}
			assert(i < cl->nvar);
			if(!bound[i]) {
				return 1;
			}
		} else {
			for(i = 0; i < an->nchild; i++) {
				if(any_unbound(an->children[i], bound, cl)) {
					return 1;
				}
			}
		}
		an = an->next_in_body;
	}

	return 0;
}

void trace_add_caller(struct predicate *callee, struct predicate *caller) {
	struct predlist *pl;

	for(pl = callee->callers; pl; pl = pl->next) {
		if(pl->pred == caller) return;
	}

	pl = arena_alloc(&callee->arena, sizeof(*pl));
	pl->pred = caller;
	pl->next = callee->callers;
	callee->callers = pl;
}

void trace_invoke_pred(struct predname *predname, int flags, uint32_t unbound_in, struct clause *caller, struct program *prg);

void trace_now_expression(struct astnode *an, uint8_t *bound, struct clause *cl, struct program *prg) {
	int i;

	if(an->kind == AN_RULE || an->kind == AN_NEG_RULE) {
		if(!an->predicate->pred->dynamic) {
			// This error will be reported elsewhere.
			return;
		}

		for(i = 0; i < an->predicate->arity; i++) {
			if(any_unbound(an->children[i], bound, cl)) {
				an->children[i]->unbound = 1;
				if((prg->optflags & OPTF_BOUND_PARAMS)
				&& an->kind == AN_RULE
				&& (!(an->predicate->pred->flags & PREDF_GLOBAL_VAR) || an->predicate->pred->dynamic->global_bufsize == 1)) {
					report(
						LVL_WARN,
						an->line,
						"Argument %d of now-expression can be unbound, leading to runtime errors.",
						i + 1);
				}
			}
		}
		if(an->predicate->arity == 1 && !(an->predicate->pred->flags & PREDF_GLOBAL_VAR)) {
			if(an->kind == AN_RULE) {
				an->predicate->pred->dynamic->linkage_flags |= LINKF_SET;
			} else {
				if(an->children[0]->kind != AN_VARIABLE
				|| an->children[0]->word->name[0]) {
					an->predicate->pred->dynamic->linkage_flags |= LINKF_RESET;
				}
				if(an->children[0]->unbound) {
					an->predicate->pred->dynamic->linkage_flags |= LINKF_CLEAR;
					an->predicate->pred->dynamic->linkage_due_to_line = an->line;
				}
			}
		}
	} else if(an->kind == AN_BLOCK || an->kind == AN_FIRSTRESULT) {
		for(an = an->children[0]; an; an = an->next_in_body) {
			trace_now_expression(an, bound, cl, prg);
		}
	} else {
		assert(0); exit(1);
	}
}

int trace_invocations_body(struct astnode **anptr, int flags, uint8_t *bound, struct clause *cl, int tail, struct program *prg) {
	struct astnode *an;
	uint8_t bound_sub[cl->nvar], bound_accum[cl->nvar];
	int i, j, failed = 0;
	uint32_t unbound;
	int moreflags;

	while((an = *anptr) && !failed) {
		switch(an->kind) {
		case AN_RULE:
		case AN_NEG_RULE:
			an->predicate->pred->invoked_at_line = an->line;
			moreflags = 0;
			if(an->subkind == RULE_SIMPLE
			&& an->predicate->builtin == BI_REPEAT
			&& !prg->did_warn_about_repeat) {
				report(LVL_WARN, an->line, "(repeat forever) not invoked as a multi-query.");
				prg->did_warn_about_repeat = 1;
			}
			if(an->subkind == RULE_SIMPLE
			|| (tail && !an->next_in_body && (cl->predicate->pred->flags & PREDF_INVOKED_SIMPLE))) {
				moreflags |= PREDF_INVOKED_SIMPLE;
			} else {
				moreflags |= PREDF_INVOKED_MULTI;
			}
			for(i = 0; i < an->predicate->arity; i++) {
				if(any_unbound(an->children[i], bound, cl)) {
					an->children[i]->unbound = 1;
				}
			}
			if(an->predicate->pred->flags & PREDF_FAIL) {
				an->predicate->pred->flags |= flags | moreflags;
				failed = 1;
			} else if(an->predicate->pred->dynamic) {
				if(an->predicate->arity
				&& !(an->predicate->pred->flags & PREDF_GLOBAL_VAR)
				&& an->predicate->builtin != BI_HASPARENT) {
					if(an->children[0]->unbound) {
						if(an->predicate->arity > 1) {
							if(prg->optflags & OPTF_BOUND_PARAMS) report(
								LVL_WARN,
								an->line,
								"Dynamic predicate with unbound first argument will loop over all objects.");
							if(an->children[0]->kind != AN_VARIABLE
							|| an->children[0]->word->name[0]) {
								// won't be necessary when the backend uses
								// the intermediate code
								*anptr = mkast(AN_RULE, 1, cl->arena, an->line);
								(*anptr)->subkind = RULE_MULTI;
								(*anptr)->predicate = find_builtin(prg, BI_OBJECT);
								(*anptr)->children[0] = deepcopy_astnode(an->children[0], cl->arena, an->line);
								(*anptr)->unbound = 1;
								(*anptr)->next_in_body = an;
								an->children[0]->unbound = 0;
							}
						} else {
							an->predicate->pred->dynamic->linkage_flags |= LINKF_LIST;
							an->predicate->pred->dynamic->linkage_due_to_line = an->line;
						}
					}
				}
				for(i = 0; i < an->predicate->arity; i++) {
					add_bound_vars(an->children[i], bound, cl);
				}
			} else {
				unbound = 0;
				for(i = 0; i < an->predicate->arity; i++) {
					if(an->children[i]->unbound) {
						unbound |= 1 << i;
					}
				}
				if(!(cl->predicate->pred->flags & PREDF_VISITED)) {
					trace_add_caller(an->predicate->pred, cl->predicate->pred);
				}
				trace_invoke_pred(
					an->predicate,
					flags | moreflags,
					unbound,
					cl,
					prg);
				if(an->predicate->pred->flags & PREDF_STOP) {
					failed = 1;
				}
				if(an->kind == AN_RULE) {
					if(an->predicate->builtin == BI_UNIFY) {
						if(!an->children[0]->unbound || !an->children[1]->unbound) {
							for(i = 0; i < 2; i++) {
								add_bound_vars(an->children[i], bound, cl);
							}
						}
					} else {
						for(i = 0; i < an->predicate->arity; i++) {
							if(!(an->predicate->pred->unbound_out & (1 << i))) {
								add_bound_vars(an->children[i], bound, cl);
							}
						}
					}
				}
			}
			break;
		case AN_NOW:
			trace_now_expression(an->children[0], bound, cl, prg);
			break;
		case AN_BLOCK:
		case AN_FIRSTRESULT:
			if(flags & PREDF_INVOKED_FOR_WORDS) {
				flags |= PREDF_INVOKED_DEEP_WORDS;
			}
			failed = !trace_invocations_body(&an->children[0], flags, bound, cl, tail && !an->next_in_body, prg);
			break;
		case AN_STOPPABLE:
			if(flags & PREDF_INVOKED_FOR_WORDS) {
				flags |= PREDF_INVOKED_DEEP_WORDS;
			}
			memcpy(bound_sub, bound, cl->nvar);
			(void) trace_invocations_body(&an->children[0], flags, bound_sub, cl, 1, prg);
			break;
		case AN_STATUSBAR:
			if(flags & PREDF_INVOKED_FOR_WORDS) {
				flags |= PREDF_INVOKED_DEEP_WORDS;
			}
			memcpy(bound_sub, bound, cl->nvar);
			(void) trace_invocations_body(&an->children[1], flags, bound_sub, cl, 0, prg);
			break;
		case AN_NEG_BLOCK:
			if(flags & PREDF_INVOKED_FOR_WORDS) {
				flags |= PREDF_INVOKED_DEEP_WORDS;
			}
			for(i = 0; i < an->nchild; i++) {
				memcpy(bound_sub, bound, cl->nvar);
				(void) trace_invocations_body(&an->children[i], flags, bound_sub, cl, 0, prg);
			}
			break;
		case AN_SELECT:
			if(flags & PREDF_INVOKED_FOR_WORDS) {
				flags |= PREDF_INVOKED_DEEP_WORDS;
			}
			/* drop through */
		case AN_OR:
			memset(bound_accum, 1, cl->nvar);
			failed = 1;
			for(i = 0; i < an->nchild; i++) {
				memcpy(bound_sub, bound, cl->nvar);
				if(trace_invocations_body(&an->children[i], flags, bound_sub, cl, tail && !an->next_in_body, prg)) {
					for(j = 0; j < cl->nvar; j++) {
						bound_accum[j] &= bound_sub[j];
					}
					failed = 0;
				}
			}
			if(!failed) memcpy(bound, bound_accum, cl->nvar);
			break;
		case AN_EXHAUST:
			if(flags & PREDF_INVOKED_FOR_WORDS) {
				flags |= PREDF_INVOKED_DEEP_WORDS;
			}
			memcpy(bound_sub, bound, cl->nvar);
			(void) trace_invocations_body(&an->children[0], flags, bound_sub, cl, 0, prg);
			break;
		case AN_IF:
			if(flags & PREDF_INVOKED_FOR_WORDS) {
				flags |= PREDF_INVOKED_DEEP_WORDS;
			}
			failed = 1;
			memset(bound_accum, 1, cl->nvar);
			memcpy(bound_sub, bound, cl->nvar);
			if(trace_invocations_body(&an->children[0], flags, bound_sub, cl, 0, prg)
			&& trace_invocations_body(&an->children[1], flags, bound_sub, cl, tail && !an->next_in_body, prg)) {
				memcpy(bound_accum, bound_sub, cl->nvar);
				failed = 0;
			}
			memcpy(bound_sub, bound, cl->nvar);
			if(trace_invocations_body(&an->children[2], flags, bound_sub, cl, tail && !an->next_in_body, prg)) {
				for(j = 0; j < cl->nvar; j++) {
					bound_accum[j] &= bound_sub[j];
				}
				failed = 0;
			}
			if(!failed) memcpy(bound, bound_accum, cl->nvar);
			break;
		case AN_COLLECT:
			memcpy(bound_sub, bound, cl->nvar);
			(void) trace_invocations_body(&an->children[0], flags, bound_sub, cl, 0, prg);
			add_bound_vars(an->children[2], bound, cl);
			break;
		case AN_COLLECT_WORDS:
			memcpy(bound_sub, bound, cl->nvar);
			(void) trace_invocations_body(&an->children[0], PREDF_INVOKED_SHALLOW_WORDS, bound_sub, cl, 0, prg);
			add_bound_vars(an->children[1], bound, cl);
			break;
		case AN_COLLECT_WORDS_CHECK:
			memcpy(bound_sub, bound, cl->nvar);
			(void) trace_invocations_body(&an->children[0], PREDF_INVOKED_SHALLOW_WORDS, bound_sub, cl, 0, prg);
			break;
		}
		anptr = &an->next_in_body;
	}

	return !failed;
}

uint32_t trace_reconsider_clause(struct clause *cl, int flags, uint32_t unbound_in, struct program *prg) {
	int i;
	uint8_t bound[cl->nvar];
	uint32_t unbound_out = 0;

#if 0
	printf("considering clause at %s:%d\n", FILEPART(cl->line), LINEPART(cl->line));
#endif

	memset(bound, 0, cl->nvar);
	for(i = 0; i < cl->predicate->arity; i++) {
		if(!(unbound_in & (1 << i))) {
			add_bound_vars(cl->params[i], bound, cl);
		}
	}

	if(trace_invocations_body(&cl->body, flags, bound, cl, 1, prg)) {
		for(i = 0; i < cl->predicate->arity; i++) {
			if((unbound_in & (1 << i))
			&& any_unbound(cl->params[i], bound, cl)) {
				unbound_out |= 1 << i;
			}
		}
	}

	return unbound_out;
}

void trace_reconsider_pred(struct predicate *pred, struct program *prg) {
	int i;
	uint32_t unbound = pred->unbound_out;
	struct predlist *pl;

#if 0
	printf("reconsidering %s with bits %x %x\n", pred->predname->printed_name, pred->unbound_in, pred->unbound_out);
#endif
	for(i = 0; i < pred->nclause; i++) {
#if 0
		uint32_t old = unbound;
		int j;
#endif
		unbound |= trace_reconsider_clause(
			pred->clauses[i],
			pred->flags & PREDF_INVOKED,
			pred->unbound_in,
			prg);
#if 0
		for(j = 0; j < pred->predname->arity; j++) {
			if((unbound & ~old) & (1 << j)) {
				printf("now, parameter %d of %s can be left unbound\n", j, pred->predname->printed_name);
			}
		}
#endif
	}
	pred->flags |= PREDF_VISITED;

	if(unbound != pred->unbound_out) {
		pred->unbound_out = unbound;
		for(pl = pred->callers; pl; pl = pl->next) {
			trace_enqueue(pl->pred, prg->npredicate);
		}
	}
}

void trace_invoke_pred(struct predname *predname, int flags, uint32_t unbound_in, struct clause *caller, struct program *prg) {
	int i;
	struct predicate *pred = predname->pred;

	//printf("invoking %s, old flags %x, new flags %x\n", predname->printed_name, pred->flags, flags);

	if((pred->flags != (pred->flags | flags))
	|| (pred->unbound_in != (pred->unbound_in | unbound_in))) {
		for(i = 0; i < predname->arity; i++) {
			if((unbound_in & (1 << i))
			&& !(pred->unbound_in & (1 << i))) {
#if 0
				printf("now, %s was called with parameter %d potentially unbound\n", pred->predname->printed_name, i);
#endif
				pred->unbound_in_due_to[i] = caller;
			}
		}
		pred->flags |= flags;
		pred->unbound_in |= unbound_in;
		trace_enqueue(pred, prg->npredicate);
	}
}

void trace_entrypoint(struct predname *predname, struct program *prg, int mode) {
	predname->pred->flags |= PREDF_INVOKED_SIMPLE | PREDF_INVOKED_MULTI;
	trace_invoke_pred(predname, mode, 0, 0, prg);
}

void trace_invocations(struct program *prg) {
	struct predicate *pred;
	struct predname *predname;
	int i;

	// This is the only builtin that can succeed and leave an unbound parameter.
	find_builtin(prg, BI_UNBOUND)->pred->unbound_out = 1;

	tracequeue = malloc(prg->npredicate * sizeof(struct predicate *));

	trace_entrypoint(find_builtin(prg, BI_PROGRAM_ENTRY), prg, PREDF_INVOKED_BY_PROGRAM);
	trace_entrypoint(find_builtin(prg, BI_ERROR_ENTRY), prg, PREDF_INVOKED_BY_PROGRAM);

	if(!(prg->optflags & OPTF_BOUND_PARAMS)) {
		// in the debugger, all predicates are potential entry points, and all
		// incoming parameters are potentially unbound

		for(i = 0; i < prg->npredicate; i++) {
			predname = prg->predicates[i];
			if(!predname->special
			&& !(predname->pred->flags & PREDF_DYNAMIC)) {
				predname->pred->unbound_in = (1 << predname->arity) - 1;
				trace_entrypoint(predname, prg, PREDF_INVOKED_BY_DEBUGGER);
			}
		}
	}

	while((pred = trace_read_queue(prg->npredicate))) {
		trace_reconsider_pred(pred, prg);
	}

	free(tracequeue);
	tracequeue = 0;
	tracequeue_r = tracequeue_w = 0;

	for(i = 0; i < prg->npredicate; i++) {
		predname = prg->predicates[i];
		pred = predname->pred;
		if(!predname->special
		&& !(predname->builtin && !(predname->nameflags & PREDNF_DEFINABLE_BI))
		&& !pred->dynamic
		&& (pred->flags & PREDF_INVOKED_BY_PROGRAM)
		&& !(pred->flags & PREDF_DEFINED)) {
			report(
				LVL_WARN,
				pred->invoked_at_line,
				"Predicate %s is invoked but there is no matching rule.",
				predname->printed_name);
		}
	}
}

static int mark_all_dynamic(struct program *prg, struct astnode *an, line_t line, int allow_multi) {
	int success = 1;

	while(an) {
		if(an->kind == AN_RULE
		|| an->kind == AN_NEG_RULE) {
			if(an->predicate->builtin && an->predicate->builtin != BI_HASPARENT) {
				report(LVL_ERR, line, "(now) cannot be combined with this built-in predicate.");
				success = 0;
			} else if(an->subkind == RULE_MULTI && !allow_multi) {
				report(LVL_ERR, line, "(now) cannot be combined with a multi-call.");
				success = 0;
			} else if(an->predicate->arity == 0) {
				if(an->predicate->dyn_id == DYN_NONE) {
					an->predicate->dyn_id = prg->nglobalflag++;
					prg->globalflagpred = realloc(prg->globalflagpred, prg->nglobalflag * sizeof(struct predname *));
					prg->globalflagpred[an->predicate->dyn_id] = an->predicate;
				}
			} else if(an->predicate->arity == 1) {
				if(an->predicate->pred->flags & PREDF_GLOBAL_VAR) {
					assert(an->predicate->dyn_var_id != DYN_NONE);
				} else {
					if(an->predicate->dyn_id == DYN_NONE) {
						an->predicate->dyn_id = prg->nobjflag++;
						prg->objflagpred = realloc(prg->objflagpred, prg->nobjflag * sizeof(struct predname *));
						prg->objflagpred[an->predicate->dyn_id] = an->predicate;
					}
				}
			} else if(an->predicate->arity == 2) {
				if(an->predicate->dyn_id == DYN_NONE) {
					an->predicate->dyn_id = prg->nobjvar++;
					prg->objvarpred = realloc(prg->objvarpred, prg->nobjvar * sizeof(struct predname *));
					prg->objvarpred[an->predicate->dyn_id] = an->predicate;
				}
			} else {
				report(LVL_ERR, an->line, "Dynamic predicates can have a maximum of two parameters.");
				success = 0;
			}
			if(success) {
				an->predicate->pred->flags |= PREDF_DYNAMIC;
				if(!an->predicate->pred->dynamic) {
					an->predicate->pred->dynamic = calloc(1, sizeof(struct dynamic));
				}
			}
		} else if(an->kind == AN_BLOCK) {
			success &= mark_all_dynamic(prg, an->children[0], line, allow_multi);
		} else if(an->kind == AN_FIRSTRESULT) {
			success &= mark_all_dynamic(prg, an->children[0], line, 1);
		} else {
			report(LVL_ERR, line, "(now) only works with rules, negated rules, and blocks.");
			success = 0;
		}
		an = an->next_in_body;
	}

	return success;
}

static int find_dynamic(struct program *prg, struct astnode *an, line_t line) {
	int i, success = 1;

	while(an) {
		if(an->kind == AN_NOW) {
			success &= mark_all_dynamic(prg, an->children[0], line, 0);
		} else {
			for(i = 0; i < an->nchild; i++) {
				success &= find_dynamic(prg, an->children[i], line);
			}
		}
		an = an->next_in_body;
	}

	return success;
}

static void add_clause(struct clause *cl, struct predicate *pred) {
	pred->clauses = realloc(pred->clauses, (pred->nclause + 1) * sizeof(struct clause *));
	pred->clauses[pred->nclause++] = cl;
}

static struct word **clausevars;
static int nclausevar;
static int nalloc_var;

static int resolve_clause_var(struct word *w) {
	int i;

	for(i = 0; i < nclausevar; i++) {
		if(w == clausevars[i]) return i;
	}

	if(i == nclausevar) {
		if(nclausevar >= nalloc_var) {
			nalloc_var = nclausevar * 2 + 8;
			clausevars = realloc(clausevars, nalloc_var * sizeof(struct word *));
		}
		clausevars[nclausevar++] = w;
	}

	return i;
}

static void find_clause_vars(struct program *prg, struct clause *cl, struct astnode *an) {
	int i;

	while(an) {
		if(an->kind == AN_IF
		|| an->kind == AN_NEG_RULE
		|| an->kind == AN_NEG_BLOCK
		|| an->kind == AN_FIRSTRESULT
		|| an->kind == AN_STATUSBAR) {
			an->word = fresh_word(prg);
			(void) resolve_clause_var(an->word);
		} else if(an->kind == AN_VARIABLE) {
			if(an->word->name[0]) {
				(void) resolve_clause_var(an->word);
			}
		}
		if(an->kind == AN_RULE || an->kind == AN_NEG_RULE) {
			if(an->nchild > cl->max_call_arity) {
				cl->max_call_arity = an->nchild;
			}
		}
		for(i = 0; i < an->nchild; i++) {
			find_clause_vars(prg, cl, an->children[i]);
		}
		an = an->next_in_body;
	}
}

static void analyse_clause(struct program *prg, struct clause *cl) {
	int i;

	nclausevar = 0;

	if((cl->predicate->pred->flags & PREDF_CONTAINS_JUST)
	&& contains_just(cl->body)) {
		(void) resolve_clause_var(find_word(prg, "*just"));
	}

	for(i = 0; i < cl->predicate->arity; i++) {
		find_clause_vars(prg, cl, cl->params[i]);
	}
	find_clause_vars(prg, cl, cl->body);

	cl->nvar = nclausevar;
	cl->varnames = arena_alloc(&cl->predicate->pred->arena, nclausevar * sizeof(struct word *));
	memcpy(cl->varnames, clausevars, nclausevar * sizeof(struct word *));
}

void find_dict_words(struct program *prg, struct astnode *an, int include_barewords) {
	int i;
	struct word *w;

	while(an) {
		if(an->kind == AN_DICTWORD
		|| (an->kind == AN_BAREWORD && include_barewords)) {
			char strbuf[strlen(an->word->name) + 1];

			assert(an->word->name[0]);
			for(i = 0; an->word->name[i]; i++) {
				if(an->word->name[i] >= 'A' && an->word->name[i] <= 'Z') {
					strbuf[i] = an->word->name[i] - 'A' + 'a';
				} else {
					strbuf[i] = an->word->name[i];
				}
			}
			strbuf[i] = 0;
			w = find_word(prg, strbuf);
			if(!(w->flags & WORDF_DICT)) {
				assert(w->name[1]);
				w->flags |= WORDF_DICT;
				w->dict_id = 256 + prg->ndictword;
				if(prg->ndictword >= prg->nalloc_dictword) {
					prg->nalloc_dictword = prg->ndictword * 2 + 8;
					prg->dictwordnames = realloc(prg->dictwordnames, prg->nalloc_dictword * sizeof(struct word *));
				}
				prg->dictwordnames[prg->ndictword++] = w;
			}
			if(!(an->word->flags & WORDF_DICT)) {
				an->word->flags |= WORDF_DICT;
				an->word->dict_id = w->dict_id;
			}
		}
		for(i = 0; i < an->nchild; i++) {
			find_dict_words(prg, an->children[i], include_barewords);
		}
		an = an->next_in_body;
	}
}

void build_dictionary(struct program *prg) {
	int i, j, k;
	struct predname *predname;
	struct predicate *pred;

	for(i = 0; i < prg->npredicate; i++) {
		predname = prg->predicates[i];
		pred = predname->pred;
		for(j = 0; j < pred->nclause; j++) {
			for(k = 0; k < predname->arity; k++) {
				find_dict_words(prg, pred->clauses[j]->params[k], 0);
			}
			find_dict_words(
				prg,
				pred->clauses[j]->body,
				!!(pred->flags & PREDF_INVOKED_FOR_WORDS));
		}
	}
}

/* Body is guaranteed to fail, regardless of parameters. Possibly with side-effects. */
int body_fails(struct astnode *an) {
	int i;

	while(an) {
		switch(an->kind) {
		case AN_BLOCK:
		case AN_FIRSTRESULT:
			if(body_fails(an->children[0])) return 1;
			break;
		case AN_NEG_BLOCK:
			if(body_succeeds(an->children[0])) return 1;
			break;
		case AN_RULE:
			if(an->predicate->pred->flags & PREDF_FAIL) return 1;
			break;
		case AN_NEG_RULE:
			if(an->predicate->pred->flags & PREDF_SUCCEEDS) return 1;
			break;
		case AN_OR:
		case AN_SELECT:
			for(i = 0; i < an->nchild; i++) {
				if(!body_fails(an->children[i])) break;
			}
			if(i == an->nchild) return 1;
			break;
		case AN_IF:
			if(body_fails(an->children[1])
			&& body_fails(an->children[2])) {
				return 1;
			}
			// todo also consider conditions with known outcome
			break;
		}
		an = an->next_in_body;
	}

	return 0;
}

/* Body is guaranteed to succeed at least once, regardless of parameters. Possibly with side-effects. */
int body_succeeds(struct astnode *an) {
	int i;

	while(an) {
		switch(an->kind) {
		case AN_BLOCK:
		case AN_FIRSTRESULT:
			if(!body_succeeds(an->children[0])) return 0;
			break;
		case AN_NEG_BLOCK:
			if(!body_fails(an->children[0])) return 0;
			break;
		case AN_RULE:
			if(!(an->predicate->pred->flags & PREDF_SUCCEEDS)) return 0;
			break;
		case AN_NEG_RULE:
			if(!(an->predicate->pred->flags & PREDF_FAIL)) return 0;
			break;
		case AN_OR:
			for(i = 0; i < an->nchild; i++) {
				if(body_succeeds(an->children[i])) break;
			}
			if(i == an->nchild) return 0;
			break;
		case AN_IF:
			if(!body_succeeds(an->children[1])) return 0;
			if(!body_succeeds(an->children[2])) return 0;
			// todo also consider conditions with known outcome
			break;
		case AN_COLLECT_WORDS_CHECK:
			return 0;
			break;
		case AN_SELECT:
			for(i = 0; i < an->nchild; i++) {
				if(!body_succeeds(an->children[i])) return 0;
			}
			break;
		}
		an = an->next_in_body;
	}

	return 1;
}

static int add_ending(struct program *prg, char *utf8) {
	uint16_t unicode[64], ch;
	int len;
	struct endings_point *pt;
	struct endings_way **ways;
	int i;

	if(utf8[utf8_to_unicode(unicode, sizeof(unicode) / sizeof(uint16_t), (uint8_t *) utf8)]) {
		report(LVL_ERR, 0, "Word ending too long: '%s'", utf8);
		return 0;
	}

	for(len = 0; unicode[len]; len++);
	assert(len);

	pt = &prg->endings_root;
	while(len--) {
		ch = unicode[len];
		if(ch >= 'A' && ch <= 'Z') {
			ch += 'a' - 'A';
		}
		for(i = 0; i < pt->nway; i++) {
			if(pt->ways[i]->letter == ch) {
				break;
			}
		}
		if(i == pt->nway) {
			pt->nway++;
			ways = arena_alloc(&prg->endings_arena, pt->nway * sizeof(struct endings_way *));
			memcpy(ways, pt->ways, i * sizeof(struct endings_way));
			ways[i] = arena_calloc(&prg->endings_arena, sizeof(struct endings_way));
			ways[i]->letter = ch;
			pt->ways = ways;
		}
		if(len) {
			pt = &pt->ways[i]->more;
		} else {
			pt->ways[i]->final = 1;
		}
	}

	return 1;
}

static int build_endings_tree(struct program *prg) {
	struct predicate *pred;
	struct astnode *an;
	int i;

	arena_free(&prg->endings_arena);
	arena_init(&prg->endings_arena, 1024);
	memset(&prg->endings_root, 0, sizeof(prg->endings_root));

	pred = find_builtin(prg, BI_ENDINGS)->pred;
	for(i = 0; i < pred->nclause; i++) {
		for(an = pred->clauses[i]->body; an; an = an->next_in_body) {
			if(an->kind != AN_BAREWORD) {
				report(LVL_ERR, an->line, "Body of (removable word endings) may only contain simple words.");
				return 0;
			}
			if(!add_ending(prg, an->word->name)) {
				return 0;
			}
		}
	}

	return 1;
}

void frontend_add_builtins(struct program *prg) {
	int i, j;
	struct word *words[8];
	struct predname *predname;
	struct predicate *pred;

	for(i = 0; i < sizeof(specialspec) / sizeof(*specialspec); i++) {
		for(j = 0; j < specialspec[i].nword; j++) {
			words[j] = specialspec[i].word[j]?
				find_word(prg, specialspec[i].word[j])
				: 0;
		}
		predname = find_predicate(prg, specialspec[i].nword, words);
		predname->special = specialspec[i].id;
		predname->nameflags |= specialspec[i].prednameflags;
	}

	for(i = 0; i < sizeof(builtinspec) / sizeof(*builtinspec); i++) {
		for(j = 0; j < builtinspec[i].nword; j++) {
			words[j] = builtinspec[i].word[j]?
				find_word(prg, builtinspec[i].word[j])
				: 0;
		}
		predname = find_predicate(prg, builtinspec[i].nword, words);
		pred = predname->pred;
		predname->builtin = builtinspec[i].id;
		predname->nameflags |= builtinspec[i].prednameflags;
		pred->flags |= builtinspec[i].predflags | PREDF_INVOKED_MULTI | PREDF_INVOKED_SIMPLE;
		if(pred->flags & PREDF_DYNAMIC) {
			assert(predname->builtin == BI_HASPARENT);
			assert(predname->arity == 2);
			assert(prg->nobjvar == DYN_HASPARENT);
			predname->dyn_id = prg->nobjvar++;
			prg->objvarpred = realloc(prg->objvarpred, prg->nobjvar * sizeof(struct predname *));
			prg->objvarpred[predname->dyn_id] = predname;
			pred->dynamic = calloc(1, sizeof(struct dynamic));
		}
	}
}

int frontend_visit_clauses(struct program *prg, struct arena *temp_arena, struct clause **first_ptr) {
	struct clause *cl, *sub, **clause_dest, **cld;
	struct astnode *an;
	int i, j;
	char buf[32];

	for(clause_dest = first_ptr; (cl = *clause_dest); ) {
		if(cl->predicate->special == SP_GLOBAL_VAR
		|| cl->predicate->special == SP_GLOBAL_VAR_2) {
			if(cl->body
			&& cl->body->kind == AN_RULE
			&& !cl->body->next_in_body
			&& cl->body->predicate->arity == 1) {
				/* presumably of the form (global variable (inner declaration ...)) */
				if(cl->body->children[0]->kind != cl->params[0]->kind
				|| (cl->params[0]->kind == AN_VARIABLE && cl->body->children[0]->word != cl->params[0]->word)) {
					/* somebody is attempting e.g. (global variable $X) (inner declaration $Y) */
					report(LVL_ERR, cl->line, "Syntax error in global variable declaration.");
					return 0;
				}
				if(cl->body->predicate->builtin) {
					report(LVL_ERR, cl->line, "Global variable declaration collides with built-in predicate.");
					return 0;
				}
				if(cl->body->predicate->pred->flags & PREDF_GLOBAL_VAR) {
					report(LVL_WARN, cl->line, "Multiple declarations of the same global variable.");
				}
				if(!(cl->body->predicate->pred->flags & PREDF_GLOBAL_VAR)) {
					cl->body->predicate->pred->flags |= PREDF_DYNAMIC | PREDF_GLOBAL_VAR;
					if(cl->body->predicate->dyn_var_id == DYN_NONE) {
						cl->body->predicate->dyn_var_id = prg->nglobalvar++;
						prg->globalvarpred = realloc(prg->globalvarpred, prg->nglobalvar * sizeof(struct predname *));
						prg->globalvarpred[cl->body->predicate->dyn_var_id] = cl->body->predicate;
					}
					if(!cl->body->predicate->pred->dynamic) {
						cl->body->predicate->pred->dynamic = calloc(1, sizeof(struct dynamic));
					}
				}
				if(cl->predicate->special == SP_GLOBAL_VAR) {
					cl->body->predicate->pred->dynamic->global_bufsize = 1;
				} else {
					if(cl->params[1]->kind == AN_INTEGER
					&& cl->params[1]->value >= 1) {
						cl->body->predicate->pred->dynamic->global_bufsize = cl->params[1]->value;
					} else {
						report(LVL_ERR, cl->line, "The declared size of a global variable must be a positive integer.");
						return 0;
					}
				}
				if(cl->body->children[0]->kind != AN_VARIABLE) {
					/* (global variable (inner declaration #foo)) so we add a separate rule for the initial value */
					sub = arena_calloc(&cl->body->predicate->pred->arena, sizeof(*sub));
					sub->predicate = cl->body->predicate;
					sub->arena = &cl->body->predicate->pred->arena;
					sub->params = cl->body->children;
					sub->body = 0;
					sub->line = cl->body->line;
					sub->next_in_source = cl->next_in_source;
					cl->next_in_source = sub;
				}
				clause_dest = &cl->next_in_source;
			} else {
				report(LVL_ERR, cl->line, "Syntax error in global variable declaration.");
				return 0;
			}
		} else if(cl->predicate->special == SP_GENERATE) {
			if(cl->body
			&& cl->body->kind == AN_RULE
			&& !cl->body->next_in_body
			&& cl->body->predicate->arity) {
				cld = clause_dest;

				/* presumably of the form (generate N of (inner declaration ...)) */
				if(cl->params[0]->kind != AN_INTEGER
				|| cl->body->children[0]->kind != cl->params[1]->kind
				|| (cl->params[1]->kind == AN_VARIABLE && cl->body->children[0]->word != cl->params[1]->word)) {
					/* somebody is attempting e.g. (generate $X of $Y) (inner declaration $Z) */
					report(LVL_ERR, cl->line, "Syntax error in (generate $ $) declaration.");
					return 0;
				}
				for(i = 0; i < cl->params[0]->value; i++) {
					sub = calloc(1, sizeof(*sub));
					sub->predicate = cl->body->predicate;
					sub->arena = &cl->body->predicate->pred->arena;
					sub->params = malloc(sub->predicate->arity * sizeof(struct astnode *));
					sub->params[0] = mkast(AN_TAG, 0, sub->arena, cl->line);
					snprintf(buf, sizeof(buf), "%d", prg->nworldobj + 1);
					sub->params[0]->word = find_word(prg, buf);
					create_worldobj(prg, sub->params[0]->word);
					for(j = 1; j < cl->body->predicate->arity; j++) {
						sub->params[j] = deepcopy_astnode(cl->body->children[j], sub->arena, cl->line);
					}
					sub->line = cl->line;
					*cld = sub;
					cld = &sub->next_in_source;
				}
				*cld = cl->next_in_source;
			} else {
				report(LVL_ERR, cl->line, "Syntax error in (generate $ $) declaration.");
				return 0;
			}
		} else if(cl->predicate->pred->flags & PREDF_MACRO) {
			cld = clause_dest;
			an = expand_macro_body(
				cl->predicate->pred->macrodef->body,
				cl->predicate->pred->macrodef,
				cl->params,
				0,
				cl->line,
				prg,
				temp_arena);
			for(; an; an = an->next_in_body) {
				if(an->kind == AN_RULE || an->kind == AN_NEG_RULE) {
					sub = arena_calloc(&an->predicate->pred->arena, sizeof(*sub));
					sub->predicate = an->predicate;
					sub->arena = &an->predicate->pred->arena;
					sub->params = arena_alloc(sub->arena, sub->predicate->arity * sizeof(struct astnode *));
					for(i = 0; i < sub->predicate->arity; i++) {
						sub->params[i] = deepcopy_astnode(an->children[i], sub->arena, cl->line);
					}
					sub->body = deepcopy_astnode(cl->body, sub->arena, cl->line);
					sub->line = cl->line;
					sub->negated = (an->kind == AN_NEG_RULE);
					*cld = sub;
					cld = &sub->next_in_source;
				} else {
					report(LVL_ERR, cl->line, "Access predicate must expand into a conjunction of queries.");
					return 0;
				}
			}
			*cld = cl->next_in_source;
		} else {
			cl->predicate->pred->flags |= PREDF_DEFINED;
			add_clause(cl, cl->predicate->pred);
			cl->body = expand_macros(cl->body, prg, cl->arena);
			clause_dest = &cl->next_in_source;
		}
	}

	return 1;
}

static void frontend_reset_program(struct program *prg) {
	int i;
	struct predname *predname;

	for(i = 0; i < prg->npredicate; i++) {
		predname = prg->predicates[i];

		if(!predname->special) {
			pred_clear(predname);
		}
	}

	for(i = 0; i < sizeof(builtinspec) / sizeof(*builtinspec); i++) {
		predname = find_builtin(prg, builtinspec[i].id);
		predname->pred->flags |=
			builtinspec[i].predflags |
			PREDF_INVOKED_MULTI |
			PREDF_INVOKED_SIMPLE;
		if(predname->builtin == BI_HASPARENT) {
			assert(predname->dyn_id == DYN_HASPARENT);
			predname->pred->dynamic = calloc(1, sizeof(struct dynamic));
		}
	}
}

static uint32_t *selectidbuf;
static int selectidpos, nalloc_selectid;

static void extract_select_statements(struct program *prg, struct astnode *an) {
	int i;

	while(an) {
		if(an->kind == AN_SELECT && an->subkind != SEL_P_RANDOM) {
			if(selectidpos >= nalloc_selectid) {
				nalloc_selectid = 2 * selectidpos + 8;
				selectidbuf = realloc(selectidbuf, nalloc_selectid * sizeof(uint32_t));
			}
			selectidbuf[selectidpos++] = an->value;
		}
		for(i = 0; i < an->nchild; i++) {
			extract_select_statements(prg, an->children[i]);
		}
		an = an->next_in_body;
	}
}

static void enumerate_select_statements(struct program *prg, struct astnode *an, uint32_t *mapping) {
	int i;

	while(an) {
		if(an->kind == AN_SELECT && an->subkind != SEL_P_RANDOM) {
			if(mapping) {
				an->value = *mapping++;
				assert(an->value < prg->nselect);
			} else {
				if(prg->nselect >= prg->nalloc_select) {
					prg->nalloc_select = 2 * prg->nselect + 8;
					prg->select = realloc(prg->select, prg->nalloc_select);
				}
				an->value = prg->nselect;
				prg->select[prg->nselect++] = 0;
			}
		}
		for(i = 0; i < an->nchild; i++) {
			enumerate_select_statements(prg, an->children[i], mapping);
		}
		an = an->next_in_body;
	}
}

static char *structbuf;
static int structpos, nalloc_struct;

static void add_structchar(char ch) {
	if(structpos >= nalloc_struct) {
		nalloc_struct = structpos * 2 + 8;
		structbuf = realloc(structbuf, nalloc_struct);
	}
	structbuf[structpos++] = ch;
}

static void determine_clause_struct(struct astnode *an) {
	int i;

	while(an) {
		if(an->kind == AN_SELECT && an->subkind != SEL_P_RANDOM) {
			add_structchar('A' + an->subkind);
			add_structchar('[');
			for(i = 0; i < an->nchild; i++) {
				if(i) add_structchar(',');
				determine_clause_struct(an->children[i]);
			}
			add_structchar(']');
		} else {
			for(i = 0; i < an->nchild; i++) {
				determine_clause_struct(an->children[i]);
			}
		}
		an = an->next_in_body;
	}
}

static int match_clause_param(struct astnode *a, struct astnode *b) {
	if(a->kind != b->kind) return 0;

	switch(a->kind) {
	case AN_BAREWORD:
	case AN_DICTWORD:
	case AN_TAG:
	case AN_VARIABLE:
		return a->word == b->word;
	case AN_INTEGER:
		return a->value == b->value;
	case AN_PAIR:
		return
			match_clause_param(a->children[0], b->children[0]) &&
			match_clause_param(a->children[1], b->children[1]);
	case AN_EMPTY_LIST:
		return 1;
	default:
		assert(0); exit(1);
	}
}

static void find_select_mapping(struct predicate *oldpred, int n_old, struct predicate *newpred, int n_new, uint16_t *result) {
	uint16_t cost[n_old + 1][n_new + 1], bestcost;
	char path[n_old + 1][n_new + 1], bestpath;
	int i, j, k;
	struct clause *oldcl, *newcl;

#if 0
	for(i = 0; i < n_old; i++) {
		printf("old clause %d: %04x \"%s\"\n",
			i,
			oldpred->selectclauses[i],
			oldpred->clauses[oldpred->selectclauses[i]]->structure);
	}

	for(i = 0; i < n_new; i++) {
		printf("new clause %d: %04x \"%s\"\n",
			i,
			newpred->selectclauses[i],
			newpred->clauses[newpred->selectclauses[i]]->structure);
	}
#endif

	for(i = n_old; i >= 0; i--) {
		for(j = n_new; j >= 0; j--) {
			bestcost = 0xffff;
			bestpath = 0xff;
			if(i == n_old && j == n_new) {
				// match end-of-list with end-of-list at zero cost
				bestcost = 0;
				bestpath = 'e';
			}
			if(j < n_new) {
				// we could insert a new clause here, at a cost of 1
				if(cost[i][j + 1] + 1 < bestcost) {
					bestcost = cost[i][j + 1] + 1;
					bestpath = 'i';
				}
			}
			if(i < n_old) {
				// we could remove an old clause here, at a cost of 1
				if(cost[i + 1][j] + 1 < bestcost) {
					bestcost = cost[i + 1][j] + 1;
					bestpath = 'r';
				}
			}
			if(i < n_old && j < n_new) {
				oldcl = oldpred->clauses[oldpred->selectclauses[i]];
				newcl = newpred->clauses[newpred->selectclauses[j]];
				if(!strcmp(oldcl->structure, newcl->structure)) {
					for(k = 0; k < newcl->predicate->arity; k++) {
						if(!match_clause_param(
							oldpred->clauses[oldpred->selectclauses[i]]->params[k],
							newpred->clauses[newpred->selectclauses[j]]->params[k]))
						{
							break;
						}
					}
					if(k == newcl->predicate->arity) {
						// a match is possible here, at zero cost
						if(cost[i + 1][j + 1] < bestcost) {
							bestcost = cost[i + 1][j + 1];
							bestpath = 'm';
						}
					}
				}
			}
			cost[i][j] = bestcost;
			path[i][j] = bestpath;
		}
	}

	i = j = 0;
	while(i < n_old || j < n_new) {
		switch(path[i][j]) {
		case 'i':
			//printf("generate new for %04x\n", j);
			result[j++] = 0xffff;
			break;
		case 'r':
			i++;
			break;
		case 'm':
			//printf("copy old %04x to new %04x\n", i, j);
			result[j++] = i++;
			break;
		default:
			assert(0); exit(1);
		}
	}
}

static void recover_select_statements(struct program *prg) {
	int i, j, n_new;
	struct predname *predname;
	struct predicate *pred;
	uint16_t *newtable = 0;
	struct clause *oldcl, *newcl;
	int nalloc_new = 0;

	structpos = 0;

	for(i = 0; i < prg->npredicate; i++) {
		predname = prg->predicates[i];
		pred = predname->pred;
		n_new = 0;
		for(j = 0; j < pred->nclause; j++) {
			structpos = 0;
			determine_clause_struct(pred->clauses[j]->body);
			if(structpos) {
				add_structchar(0);
				pred->clauses[j]->structure = arena_strdup(&pred->arena, structbuf);
				if(n_new >= nalloc_new) {
					nalloc_new = n_new * 2 + 8;
					newtable = realloc(newtable, nalloc_new * sizeof(uint16_t));
				}
				newtable[n_new++] = j;
			} else {
				pred->clauses[j]->structure = 0;
			}
		}
		pred->nselectclause = n_new;
		if(n_new) {
			pred->selectclauses = arena_alloc(&pred->arena, n_new * sizeof(uint16_t));
			memcpy(pred->selectclauses, newtable, n_new * sizeof(uint16_t));
			if(predname->old_pred && predname->old_pred->nselectclause) {
#if 0
				printf("trying to match %d old against %d new for %s\n",
					predname->old_pred->nselectclause,
					n_new,
					predname->printed_name);
#endif
				find_select_mapping(
					predname->old_pred,
					predname->old_pred->nselectclause,
					pred,
					pred->nselectclause,
					newtable);
				for(j = 0; j < n_new; j++) {
					newcl = pred->clauses[pred->selectclauses[j]];
					if(newtable[j] == 0xffff) {
						enumerate_select_statements(prg, newcl->body, 0);
					} else {
						oldcl = predname->old_pred->clauses[
							predname->old_pred->selectclauses[newtable[j]]];
						selectidpos = 0;
						extract_select_statements(prg, oldcl->body);
						enumerate_select_statements(prg, newcl->body, selectidbuf);
					}
				}
			} else {
#if 0
				printf("generating %d new for %s\n",
					n_new,
					predname->printed_name);
#endif
				for(j = 0; j < n_new; j++) {
					newcl = pred->clauses[pred->selectclauses[j]];
					enumerate_select_statements(prg, newcl->body, 0);
				}
			}
		}
		pred_release(predname->old_pred);
		predname->old_pred = 0;
	}

	free(structbuf);
	structbuf = 0;
	nalloc_struct = 0;
	free(selectidbuf);
	selectidbuf = 0;
	nalloc_selectid = 0;
	free(newtable);
}

int frontend(struct program *prg, int nfile, char **fname) {
	struct clause **clause_dest, *first_clause, *cl;
	struct predname *predname;
	struct predicate *pred;
	struct astnode *an, **anptr;
	int fnum, i, j, k, m;
	int flag;
	struct lexer lexer = {0};
	struct eval_state es;
	int success;

	for(i = 0; i < prg->npredicate; i++) {
		predname = prg->predicates[i];
		if(!predname->special) {
			pred_release(predname->old_pred);
			if(predname->pred->nselectclause) {
				predname->old_pred = predname->pred;
				pred_claim(predname->old_pred);
			} else {
				predname->old_pred = 0;
			}
			pred_clear(predname);
		}
	}

	frontend_reset_program(prg);

	arena_init(&lexer.temp_arena, 16384);
	lexer.program = prg;

	nsourcefile = nfile;
	sourcefile = fname;

	clause_dest = &first_clause;
	for(fnum = 0; fnum < nfile; fnum++) {
		lexer.file = fopen(fname[fnum], "r");
		if(!lexer.file) {
			report(LVL_ERR, 0, "Failed to open \"%s\": %s", fname[fnum], strerror(errno));
			arena_free(&lexer.temp_arena);
			frontend_reset_program(prg);
			return 0;
		}
		success = parse_file(&lexer, fnum, &clause_dest);
		fclose(lexer.file);
		if(!success) {
			arena_free(&lexer.temp_arena);
			frontend_reset_program(prg);
			return 0;
		}
	}
	*clause_dest = 0;

	report(LVL_INFO, 0, "Total word count: %d", lexer.totalwords);

	if(!frontend_visit_clauses(prg, &lexer.temp_arena, &first_clause)) {
		arena_free(&lexer.temp_arena);
		frontend_reset_program(prg);
		return 0;
	}

	for(i = 0; i < prg->npredicate; i++) {
		pred = prg->predicates[i]->pred;
		for(j = 0; j < pred->nclause; j++) {
			if(pred->clauses[j]->negated) {
				for(anptr = &pred->clauses[j]->body; *anptr; anptr = &(*anptr)->next_in_body);
				*anptr = mkast(AN_JUST, 0, pred->clauses[j]->arena, pred->clauses[j]->line);
				(*anptr)->next_in_body = an = mkast(AN_RULE, 0, pred->clauses[j]->arena, pred->clauses[j]->line);
				an->predicate = find_builtin(prg, BI_FAIL);
				pred->flags |= PREDF_CONTAINS_JUST;
			} else {
				if(!(pred->flags & PREDF_CONTAINS_JUST)
				&& contains_just(pred->clauses[j]->body)) {
					pred->flags |= PREDF_CONTAINS_JUST;
				}
			}
		}
	}

	success = 1;
	for(i = 0; i < prg->npredicate; i++) {
		pred = prg->predicates[i]->pred;
		for(j = 0; j < pred->nclause; j++) {
			success &= find_dynamic(
				prg,
				pred->clauses[j]->body,
				pred->clauses[j]->line);
		}
	}
	if(!success) {
		arena_free(&lexer.temp_arena);
		frontend_reset_program(prg);
		return 0;
	}

#if 0
	for(i = 0; i < npredicate; i++) {
		if(!predicates[i]->special
		&& !(predicates[i]->pred->flags & PREDF_MACRO)
		&& !(predicates[i]->pred->flags & PREDF_DYNAMIC)) {
			printf("%s %s\n",
				(predicates[i]->pred->flags & PREDF_CONTAINS_JUST)? "J" : " ",
				predicates[i]->printed_name);
		}
	}
#endif

	// these will not be necessary when using the intermediate code:

	if(!(predname = find_builtin(prg, BI_ERROR_ENTRY))->pred->nclause) {
		cl = calloc(1, sizeof(*cl));
		cl->predicate = predname;
		cl->arena = &predname->pred->arena;
		cl->params = calloc(1, sizeof(struct astnode *));
		cl->params[0] = mkast(AN_VARIABLE, 0, cl->arena, 0);
		cl->params[0]->word = find_word(prg, "");
		cl->body = mkast(AN_RULE, 0, cl->arena, 0);
		cl->body->subkind = RULE_SIMPLE;
		cl->body->predicate = find_builtin(prg, BI_QUIT);
		add_clause(cl, predname->pred);
	}

	if(!(predname = find_builtin(prg, BI_PROGRAM_ENTRY))->pred->nclause) {
		cl = calloc(1, sizeof(*cl));
		cl->predicate = predname;
		cl->arena = &predname->pred->arena;
		cl->body = mkast(AN_RULE, 0, cl->arena, 0);
		cl->body->subkind = RULE_SIMPLE;
		cl->body->predicate = find_builtin(prg, BI_QUIT);
		add_clause(cl, predname->pred);
	}

	for(i = 0; i < prg->npredicate; i++) {
		predname = prg->predicates[i];
		pred = predname->pred;
		for(j = 0; j < pred->nclause; j++) {
			analyse_clause(prg, pred->clauses[j]);
		}
	}

	for(i = 0; i < prg->npredicate; i++) {
		predname = prg->predicates[i];
		pred = predname->pred;
		if(!predname->special
		&& !(predname->builtin && !(predname->nameflags & PREDNF_DEFINABLE_BI))
		&& !pred->dynamic
		&& !(pred->nclause)) {
			pred->flags |= PREDF_FAIL;
		}
	}

	do {
		flag = 0;
		for(i = 0; i < prg->npredicate; i++) {
			predname = prg->predicates[i];
			if(!predname->special
			&& !(predname->builtin && !(predname->nameflags & PREDNF_DEFINABLE_BI))) {
				pred = predname->pred;
				for(j = 0; j < pred->nclause; j++) {
					if(!pred->dynamic
					&& pred->clauses[j]->body
					&& pred->clauses[j]->body->kind == AN_RULE
					&& (pred->clauses[j]->body->predicate->pred->flags & PREDF_FAIL)) {
						memmove(pred->clauses + j, pred->clauses + j + 1, (pred->nclause - j - 1) * sizeof(struct clause *));
						pred->nclause--;
						j--;
						if(!pred->nclause) {
							pred->flags |= PREDF_FAIL;
							flag = 1;
						}
					}
				}
			}
		}
	} while(flag);

	recover_select_statements(prg);

	trace_invocations(prg);

	find_fixed_flags(prg);

	build_dictionary(prg);

	do {
		flag = 0;
		for(i = 0; i < prg->npredicate; i++) {
			predname = prg->predicates[i];
			pred = predname->pred;
			if((!predname->builtin || (predname->nameflags & PREDNF_DEFINABLE_BI))
			&& !pred->dynamic
			&& !(pred->flags & PREDF_SUCCEEDS)) {
				for(j = 0; j < pred->nclause; j++) {
					for(k = 0; k < predname->arity; k++) {
						if(pred->clauses[j]->params[k]->kind != AN_VARIABLE) break;
						if(pred->clauses[j]->params[k]->word->name[0]) {
							for(m = 0; m < k; m++) {
								if(pred->clauses[j]->params[k]->word
								== pred->clauses[j]->params[m]->word) {
									break;
								}
							}
							if(m < k) break;
						}
					}
					if(k == predname->arity) {
						if(body_succeeds(pred->clauses[j]->body)) {
#if 0
							printf("Succeeds: %s\n", predname->printed_name);
#endif
							pred->flags |= PREDF_SUCCEEDS;
							flag = 1;
							if(!(pred->flags & PREDF_INVOKED_MULTI)) {
								pred->nclause = j + 1;
							}
							break;
						}
					}
					if(contains_just(pred->clauses[j]->body)) break;
				}
			}
		}
	} while(flag);

	if(verbose >= 3) {
		for(i = 0; i < prg->npredicate; i++) {
			if(!prg->predicates[i]->special
			&& !(prg->predicates[i]->pred->flags & PREDF_MACRO)) {
				pp_predicate(prg->predicates[i], prg);
			}
		}
	}

#if 0
	for(i = 0; i < prg->nworldobj; i++) {
		printf("%5d %s\n", i + 1, prg->worldobjnames[i]->name);
	}
#endif

#if 0
	for(i = 0; i < npredicate; i++) {
		predname = predicates[i];
		if(predname->builtin != BI_HASPARENT) {
			struct dynamic *dyn = predname->pred->dynamic;
			if(dyn) {
				if(dyn->linkage_flags & (LINKF_LIST | LINKF_CLEAR)) {
					printf("Needs dynamic linkage: %s", predname->printed_name);
					printf(" (due to %s:%d)\n",
						FILEPART(dyn->linkage_due_to_line),
						LINEPART(dyn->linkage_due_to_line));
				}
			}
		}
	}
#endif

#if 0
	for(i = 0; i < npredicate; i++) {
		predname = predicates[i];
		if(!predname->special) {
			printf("%c %c %s\n",
				(predname->pred->flags & PREDF_INVOKED_SIMPLE)? 'S' : '-',
				(predname->pred->flags & PREDF_INVOKED_MULTI)? 'M' : '-',
				predname->printed_name);
		}
	}
#endif
	arena_free(&lexer.temp_arena);

	prg->errorflag = 0;
	comp_builtins(prg);
	comp_program(prg);
	comp_cleanup();

	if(prg->errorflag || !build_endings_tree(prg)) {
		arena_free(&lexer.temp_arena);
		frontend_reset_program(prg);
		return 0;
	}

	init_evalstate(&es, prg);
	//es.trace = 1;
	for(i = 0; i < prg->npredicate; i++) {
		predname = prg->predicates[i];
		free(predname->fixedvalues);
		predname->fixedvalues = 0;
		predname->nfixedvalue = 0;
		if(predname->pred->flags & PREDF_FIXED_FLAG) {
			ensure_fixed_values(&es, prg, predname);
		}
	}
	free_evalstate(&es);

	free(clausevars);
	clausevars = 0;
	nalloc_var = 0;

	prg->totallines = lexer.totallines;

	return 1;
}

int frontend_inject_query(struct program *prg, struct predname *predname, struct predname *tailpred, struct word *prompt, const uint8_t *str) {
	struct lexer lexer = {0};
	struct clause *cl;
	struct astnode *body, *an;
	struct word *vname = find_word(prg, "Input");
	struct predicate *pred;

	pred_clear(predname);
	pred = predname->pred;

	arena_init(&lexer.temp_arena, 4096);
 	lexer.program = prg;
	lexer.string = str;

	cl = arena_calloc(&pred->arena, sizeof(*cl));
	cl->predicate = predname;
	cl->arena = &pred->arena;
	cl->line = 0;

	cl->params = arena_alloc(cl->arena, sizeof(struct astnode *));
	cl->params[0] = mkast(AN_VARIABLE, 0, cl->arena, 0);
	cl->params[0]->word = vname;

	body = parse_injected_query(&lexer, predname->pred);
	if(!body) {
		arena_free(&lexer.temp_arena);
		return 0;
	}

	body = expand_macros(body, prg, cl->arena);

	cl->body = an = mkast(AN_EXHAUST, 1, cl->arena, 0);
	an->children[0] = body;

	an = an->next_in_body = mkast(AN_RULE, 0, cl->arena, 0);
	an->predicate = find_builtin(prg, BI_LINE);

	if(prompt) {
		an = an->next_in_body = mkast(AN_BAREWORD, 0, cl->arena, 0);
		an->word = prompt;
	}

	an = an->next_in_body = mkast(AN_RULE, 1, cl->arena, 0);
	an->predicate = tailpred;
	an->children[0] = mkast(AN_VARIABLE, 0, cl->arena, 0);
	an->children[0]->word = vname;

	add_clause(cl, pred);
	analyse_clause(prg, cl);

	arena_free(&lexer.temp_arena);

	pred->unbound_in = 1;
	trace_reconsider_pred(pred, prg);
	//pp_predicate(predname, prg);

	free(clausevars);
	clausevars = 0;
	nalloc_var = 0;

	prg->errorflag = 0;
	comp_predicate(prg, predname);
	comp_cleanup();

	return !prg->errorflag;
}
