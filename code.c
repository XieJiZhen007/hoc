#include "hoc.h"
#include "y.tab.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define NSTACK 256

static Datum stack[NSTACK]; /* the stack */
static Datum *stackp;		/* next free spot on stack */
extern Info Infostack[MAX];
extern Info *Infostackp; // pointer to top func/proc info
Info *cur_Infostackp;	 // pointer to current func/proc info

#define NPROG 2000
Inst prog[NPROG];	   /* the machine */
Inst *progp;		   /* next free spot for code generation */
Inst *pc;			   /* program counter during execution */
Inst *progbase = prog; /* start of current subprogram */
int returning;		   /* 1 if return stmt seen */

int execDepth;

extern int indef; /* 1 if parsing a func or proc */
extern int debugLevel;
extern int debugFlag;

typedef struct Frame
{						 /* proc/func call stack frame */
	Symbol *sp;			 /* symbol table entry */
	Inst *retpc;		 /* where to resume after return */
	ArgDatum *argn;		 /* n-th argument on stack */
	ArgDatum *argstackp; /* a pointer to argstack top , just like stackp */
	long nargs;			 /* number of arguments */
} Frame;
#define NFRAME 100
Frame frame[NFRAME];
Frame *fp; /* frame pointer */

void get_curInfo(Symbol *sp)
{
	cur_Infostackp = Infostackp - 1;
	// printf("sp name : %s\n", sp->name);
	// printf("info name : %s\n", cur_Infostackp->name);
	while (strcmp(cur_Infostackp->name, sp->name))
		--cur_Infostackp;
}

void getVF_type(Symbol *sp)
{
	Symbol *tmp = lookup_global(sp->name);
	// printf("type : %ld\n", tmp->type);
	sp->type = tmp->type;
	// printf("indef : %d, type : %ld, name : %s\n", indef, sp->type, sp->name);
}

void Infopush(Symbol *sp)
{
	if (Infostackp >= &Infostack[MAX])
		execerror("Infostack too deep", 0);
	(*Infostackp).name = (char *)emalloc(50 * sizeof(char));
	strcpy((*Infostackp).name, sp->name);
}

void getInfo_nargs(long nargs)
{
	Infostackp->nargs = nargs;
	// printf("nargs : %d\n", Infostackp->nargs);
}

void debugC(int flag, int level, const char *format, ...)
{
	va_list va;
	memset(&va, 0, sizeof(va_list));
	char buf[1024] = {0};

	if (debugLevel >= level && (debugFlag & flag) != 0)
	{
		if (flag == hocExec)
			for (int i = 0; i < execDepth; i++)
				printf("\t");

		va_start(va, format);
		vsprintf(buf, format, va);
		va_end(va);
	}
	printf("%s", buf);
}

void debug(int level, const char *format, ...)
{
	va_list va;
	memset(&va, 0, sizeof(va_list));
	char buf[1024] = {0};

	if (debugLevel >= level)
	{
		va_start(va, format);
		vsprintf(buf, format, va);
		va_end(va);
	}
	printf("%s", buf);
}

void initcode(void)
{
	progp = progbase;
	stackp = stack;
	Infostackp = Infostack;
	Infostackp->paras = 0;
	fp = frame;
	returning = 0;
	indef = 0;
}

void push(Datum d)
{
	if (stackp >= &stack[NSTACK])
		execerror("stack too deep", 0);
	*stackp++ = d;
}

Datum pop(void)
{
	if (stackp == stack)
		execerror("stack underflow", 0);
	return *--stackp;
}

void argpush(ArgDatum d)
{
	if (fp->argstackp >= fp->argn + NSTACK)
		execerror("argstack too deep", 0);
	*fp->argstackp++ = d;
	// printf("argpush : %s %lf\n", d.sym->name, d.sym->u.val);
	// printf("size : %ld\n", fp->argstackp - fp->argn);
}

ArgDatum argpop(void)
{
	if (fp->argstackp == fp->argn)
		execerror("argstack underflow", 0);
	return *--fp->argstackp;
}

void xpop(void) /* for when no value is wanted */
{
	if (stackp == stack)
		execerror("stack underflow(xpop)", (char *)0);
	--stackp;
}

void constpush(void)
{
	Datum d;
	d.val = ((Symbol *)*pc++)->u.val;
	// printf("constpush : %lf\n", d.val);
	push(d);
}

void varpush(void)
{
	Datum d;
	d.sym = (Symbol *)(*pc++);
	push(d);
	// printf("varpush : %lf\n", d.val);
}

void whilecode(void)
{
	Datum d;
	Inst *savepc = pc;

	execute(savepc + 2); /* condition */
	d = pop();
	while (d.val)
	{
		execute(*((Inst **)(savepc))); /* body */
		if (returning)
			break;
		execute(savepc + 2); /* condition */
		d = pop();
	}
	if (!returning)
		pc = *((Inst **)(savepc + 1)); /* next stmt */
}

void forcode(void)
{
	Datum d;
	Inst *savepc = pc;

	execute(savepc + 4); /* precharge */
	pop();
	execute(*((Inst **)(savepc))); /* condition */
	d = pop();
	while (d.val)
	{
		execute(*((Inst **)(savepc + 2))); /* body */
		if (returning)
			break;
		execute(*((Inst **)(savepc + 1))); /* post loop */
		pop();
		execute(*((Inst **)(savepc))); /* condition */
		d = pop();
	}
	if (!returning)
		pc = *((Inst **)(savepc + 3)); /* next stmt */
}

void ifcode(void)
{
	Datum d;
	Inst *savepc = pc; /* then part */

	execute(savepc + 3); /* condition */
	d = pop();
	if (d.val)
		execute(*((Inst **)(savepc)));
	else if (*((Inst **)(savepc + 1))) /* else part? */
		execute(*((Inst **)(savepc + 1)));
	if (!returning)
		pc = *((Inst **)(savepc + 2)); /* next stmt */
}

void define(Symbol *sp) /* put func/proc in symbol table */
{
	sp->u.defn = progbase; /* start of code */
	progbase = progp;	   /* next code starts here */
}

void call(void) /* call a function */
{
	Symbol *sp = (Symbol *)pc[0]; /* symbol table entry */
								  /* for function */
	if (fp++ >= &frame[NFRAME - 1])
		execerror(sp->name, "call nested too deeply");
	fp->sp = sp;
	fp->nargs = (long)pc[1];
	fp->retpc = pc + 2;

	// TODO : 1 2 4 8 动态增加内存
	get_curInfo(sp);
	if (cur_Infostackp->defn == NULL)
		cur_Infostackp->defn = sp->u.defn;

	// printf("nparas : %d\n", cur_Infostackp->nparas);
	fp->argn = (ArgDatum *)emalloc(sizeof(ArgDatum) * cur_Infostackp->nparas);
	fp->argstackp = fp->argn;

	debugC(hocExec, 1, "calling %s nargs %d type %d\n", sp->name, fp->nargs, sp->type);
	debugC(hocExec, 5, "entry address %p  return address %p\n", sp->u.defn, fp->retpc);

	int tmp_nargs = fp->nargs;
	while (tmp_nargs--)
	{
		Datum tmp_d = pop();
		ArgDatum tmp_arg;
		tmp_arg.sym = cur_Infostackp->paras;

		// 注意：之后如果要改nparas的含义的话 此处会出问题！！！
		int tmp = cur_Infostackp->nparas - tmp_nargs - 1;
		while (tmp--)
			tmp_arg.sym = tmp_arg.sym->next;
		tmp_arg.sym->u.val = tmp_d.val;
		// printf("arg : %s %lf\n", tmp_arg.sym->name, tmp_arg.sym->u.val);
		argpush(tmp_arg);
		// printf("argn : %s %lf\n", fp->argn->sym->name, fp->argn->sym->u.val);
	}
	if (fp->nargs != cur_Infostackp->nargs)
		execerror(sp->name, " paras match error");

	// printf("exec : %p\n", sp->u.defn);
	// execute(sp->u.defn);
	execute(cur_Infostackp->defn);
	returning = 0;
}

static void ret(void) /* common return from func or proc */
{
	int i;
	pc = (Inst *)fp->retpc;
	--fp;
	returning = 1;
}

void funcret(void) /* return from a function */
{
	Datum d;
	if (fp->sp->type == PROCEDURE)
		execerror(fp->sp->name, "(proc) returns value");
	d = pop(); /* preserve function return value */
	ret();
	push(d);
	// printf("ans : %lf\n", d.val);
}

void procret(void) /* return from a procedure */
{
	if (fp->sp->type == FUNCTION)
		execerror(fp->sp->name,
				  "(func) returns no value");
	ret();
}

double getvf(char *name)
{
	ArgDatum *vf = fp->argn;
	while (strcmp(vf->sym->name, name))
		++vf;
	// printf("%s : %lf\n", name, vf->sym->u.val);
	return vf->sym->u.val;
}

void vf(void)
{
	Symbol *sp = (Symbol *)(*pc++);
	char *name = sp->name;
	Datum d;
	d.val = getvf(name);
	// printf("get val : %lf\n", d.val);
	push(d);
}

double *getarg(void) /* return pointer to argument */
{
	int nargs = (long)*pc++;
	if (nargs > fp->nargs)
		execerror(fp->sp->name, "not enough arguments");
	return &fp->argstackp[-nargs].val;
}

void arg(void) /* push argument onto stack */
{
	Datum d;
	d.val = *getarg();
	push(d);
}

void argassign(void) /* store top of stack in argument */
{
	Datum d;
	d = pop();
	push(d); /* leave value on stack */
	*getarg() = d.val;
}

void argaddeq(void) /* store top of stack in argument */
{
	Datum d;
	d = pop();
	d.val = *getarg() += d.val;
	push(d); /* leave value on stack */
}

void argsubeq(void) /* store top of stack in argument */
{
	Datum d;
	d = pop();
	d.val = *getarg() -= d.val;
	push(d); /* leave value on stack */
}

void argmuleq(void) /* store top of stack in argument */
{
	Datum d;
	d = pop();
	d.val = *getarg() *= d.val;
	push(d); /* leave value on stack */
}

void argdiveq(void) /* store top of stack in argument */
{
	Datum d;
	d = pop();
	d.val = *getarg() /= d.val;
	push(d); /* leave value on stack */
}

void argmodeq(void) /* store top of stack in argument */
{
	Datum d;
	double *x;
	long y;
	d = pop();
	/* d.val = *getarg() %= d.val; */
	x = getarg();
	y = *x;
	d.val = *x = y % (long)d.val;
	push(d); /* leave value on stack */
}

void bltin(void)
{

	Datum d;
	d = pop();
	d.val = (*(double (*)(double)) * pc++)(d.val);
	push(d);
}

void add(void)
{
	Datum d1, d2;
	d2 = pop();
	d1 = pop();
	d1.val += d2.val;
	push(d1);
}

void sub(void)
{
	Datum d1, d2;
	d2 = pop();
	d1 = pop();
	d1.val -= d2.val;
	push(d1);
}

void mul(void)
{
	Datum d1, d2;
	d2 = pop();
	d1 = pop();
	d1.val *= d2.val;
	push(d1);
}

void divop(void)
{
	Datum d1, d2;
	d2 = pop();
	if (d2.val == 0.0)
		execerror("division by zero", (char *)0);
	d1 = pop();
	d1.val /= d2.val;
	push(d1);
}

void mod(void)
{
	Datum d1, d2;
	long x;
	d2 = pop();
	if (d2.val == 0.0)
		execerror("division by zero", (char *)0);
	d1 = pop();
	/* d1.val %= d2.val; */
	x = d1.val;
	x %= (long)d2.val;
	d1.val = d2.val = x;
	push(d1);
}

void negate(void)
{
	Datum d;
	d = pop();
	d.val = -d.val;
	push(d);
}

void verify(Symbol *s)
{
	if (s->type != VAR && s->type != UNDEF)
		execerror("attempt to evaluate non-variable", s->name);
	if (s->type == UNDEF)
		execerror("undefined variable", s->name);
}

void eval(void) /* evaluate variable on stack */
{
	Datum d;
	d = pop();
	verify(d.sym);
	d.val = d.sym->u.val;
	push(d);
}

void preinc(void)
{
	Datum d;
	d.sym = (Symbol *)(*pc++);
	verify(d.sym);
	d.val = d.sym->u.val += 1.0;
	push(d);
}

void predec(void)
{
	Datum d;
	d.sym = (Symbol *)(*pc++);
	verify(d.sym);
	d.val = d.sym->u.val -= 1.0;
	push(d);
}

void postinc(void)
{
	Datum d;
	double v;
	d.sym = (Symbol *)(*pc++);
	verify(d.sym);
	v = d.sym->u.val;
	d.sym->u.val += 1.0;
	d.val = v;
	push(d);
}

void postdec(void)
{
	Datum d;
	double v;
	d.sym = (Symbol *)(*pc++);
	verify(d.sym);
	v = d.sym->u.val;
	d.sym->u.val -= 1.0;
	d.val = v;
	push(d);
}

void gt(void)
{
	Datum d1, d2;
	d2 = pop();
	d1 = pop();
	d1.val = (double)(d1.val > d2.val);
	push(d1);
}

void lt(void)
{
	Datum d1, d2;
	d2 = pop();
	d1 = pop();
	d1.val = (double)(d1.val < d2.val);
	push(d1);
}

void ge(void)
{
	Datum d1, d2;
	d2 = pop();
	d1 = pop();
	d1.val = (double)(d1.val >= d2.val);
	push(d1);
}

void le(void)
{
	Datum d1, d2;
	d2 = pop();
	d1 = pop();
	d1.val = (double)(d1.val <= d2.val);
	push(d1);
}

void eq(void)
{
	Datum d1, d2;
	d2 = pop();
	d1 = pop();
	d1.val = (double)(d1.val == d2.val);
	push(d1);
}

void ne(void)
{
	Datum d1, d2;
	d2 = pop();
	d1 = pop();
	d1.val = (double)(d1.val != d2.val);
	push(d1);
}

void and (void)
{
	Datum d1, d2;
	d2 = pop();
	d1 = pop();
	d1.val = (double)(d1.val != 0.0 && d2.val != 0.0);
	push(d1);
}

void or (void)
{
	Datum d1, d2;
	d2 = pop();
	d1 = pop();
	d1.val = (double)(d1.val != 0.0 || d2.val != 0.0);
	push(d1);
}

void not(void)
{
	Datum d;
	d = pop();
	d.val = (double)(d.val == 0.0);
	push(d);
}

void power(void)
{
	Datum d1, d2;
	d2 = pop();
	d1 = pop();
	d1.val = Pow(d1.val, d2.val);
	push(d1);
}

void assign(void)
{
	Datum d1, d2;
	d1 = pop();
	d2 = pop();
	if (d1.sym->type != VAR && d1.sym->type != UNDEF)
		execerror("assignment to non-variable",
				  d1.sym->name);
	d1.sym->u.val = d2.val;
	d1.sym->type = VAR;
	push(d2);
}

void addeq(void)
{
	Datum d1, d2;
	d1 = pop();
	d2 = pop();
	if (d1.sym->type != VAR && d1.sym->type != UNDEF)
		execerror("assignment to non-variable",
				  d1.sym->name);
	d2.val = d1.sym->u.val += d2.val;
	d1.sym->type = VAR;
	push(d2);
}

void subeq(void)
{
	Datum d1, d2;
	d1 = pop();
	d2 = pop();
	if (d1.sym->type != VAR && d1.sym->type != UNDEF)
		execerror("assignment to non-variable",
				  d1.sym->name);
	d2.val = d1.sym->u.val -= d2.val;
	d1.sym->type = VAR;
	push(d2);
}

void muleq(void)
{
	Datum d1, d2;
	d1 = pop();
	d2 = pop();
	if (d1.sym->type != VAR && d1.sym->type != UNDEF)
		execerror("assignment to non-variable",
				  d1.sym->name);
	d2.val = d1.sym->u.val *= d2.val;
	d1.sym->type = VAR;
	push(d2);
}

void diveq(void)
{
	Datum d1, d2;
	d1 = pop();
	d2 = pop();
	if (d1.sym->type != VAR && d1.sym->type != UNDEF)
		execerror("assignment to non-variable",
				  d1.sym->name);
	d2.val = d1.sym->u.val /= d2.val;
	d1.sym->type = VAR;
	push(d2);
}

void modeq(void)
{
	Datum d1, d2;
	long x;
	d1 = pop();
	d2 = pop();
	if (d1.sym->type != VAR && d1.sym->type != UNDEF)
		execerror("assignment to non-variable",
				  d1.sym->name);
	/* d2.val = d1.sym->u.val %= d2.val; */
	x = d1.sym->u.val;
	x %= (long)d2.val;
	d2.val = d1.sym->u.val = x;
	d1.sym->type = VAR;
	push(d2);
}

void printtop(void) /* pop top value from stack, print it */
{
	Datum d;
	static Symbol *s; /* last value computed */
	if (s == 0)
		s = install_global("_", VAR, 0.0);
	d = pop();
	printf("%.*g\n", (int)lookup_key("PREC")->u.val, d.val);
	s->u.val = d.val;
}

void prexpr(void) /* print numeric value */
{
	Datum d;
	d = pop();
	printf("%.*g ", (int)lookup_key("PREC")->u.val, d.val);
}

void prstr(void) /* print string value */
{
	printf("%s", (char *)*pc++);
}

void varread(void) /* read into variable */
{
	Datum d;
	extern FILE *fin;
	Symbol *var = (Symbol *)*pc++;
Again:
	switch (fscanf(fin, "%lf", &var->u.val))
	{
	case EOF:
		if (moreinput())
			goto Again;
		d.val = var->u.val = 0.0;
		break;
	case 0:
		execerror("non-number read into", var->name);
		break;
	default:
		d.val = var->u.val;
		break;
	}
	var->type = VAR;
	push(d);
}

Inst *code(Inst f) /* install one instruction or operand */
{
	Inst *oprogp = progp;
	if (progp >= &prog[NPROG])
		execerror("program too big", (char *)0);
	*progp++ = f;
	return oprogp;
}

void execute(Inst *p)
{
	debugC(hocExec, 4, "Executing from %ld\n", p - progbase);

	execDepth++;
	for (pc = p; *pc != STOP && !returning;)
		(*((++pc)[-1]))();
	execDepth--;

	debugC(hocExec, 4, "Executing ended at %ld, starting from %ld\n", pc - progbase, p - progbase);
}

void testaction(void)
{
	printf("test action\n");
}
