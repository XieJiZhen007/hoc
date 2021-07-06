#include "hoc.h"
#include "y.tab.h"
#include <math.h>

static struct
{ /* Keywords */
	char *name;
	int kval;
} keywords[] = {
	"proc",
	PROC,
	"func",
	FUNC,
	"return",
	RETURN,
	"if",
	IF,
	"else",
	ELSE,
	"while",
	WHILE,
	"for",
	FOR,
	"print",
	PRINT,
	"read",
	READ,
	"global",
	GLOBAL,
	0,
	0,
};

static struct
{ /* Constants */
	char *name;
	double cval;
} consts[] = {
	"PI", 3.14159265358979323846,
	"E", 2.71828182845904523536,
	"GAMMA", 0.57721566490153286060, /* Euler */
	"DEG", 57.29577951308232087680,	 /* deg/radian */
	"PHI", 1.61803398874989484820,	 /* golden ratio */
	"PREC", 15,						 /* output precision */
	0, 0};

static struct
{ /* Built-ins */
	char *name;
	double (*func)(double);
} builtins[] = {
	"sin", sin,
	"cos", cos,
	"tan", tan,
	"atan", atan,
	"asin", Asin, /* checks range */
	"acos", Acos, /* checks range */
	"sinh", Sinh, /* checks range */
	"cosh", Cosh, /* checks range */
	"tanh", tanh,
	"log", Log,		/* checks range */
	"log10", Log10, /* checks range */
	"exp", Exp,		/* checks range */
	"sqrt", Sqrt,	/* checks range */
	"gamma", Gamma, /* checks range */
	"int", integer,
	"abs", fabs,
	"erf", erf,
	"erfc", erfc,
	0, 0};

static struct
{
	char *name;
	Inst inst;
} codeLookupTable[] = {
	{"call", call},
	{"xpop", xpop},
	{"constpush", constpush},
	{"varpush", varpush},
	{"whilecode", whilecode},
	{"forcode", forcode},
	{"ifcode", ifcode},
	{"funcret", funcret},
	{"procret", procret},
	{"arg", arg},
	{"argassign", argassign},
	{"argaddeq", argaddeq},
	{"argsubeq", argsubeq},
	{"argmuleq", argmuleq},
	{"argdiveq", argdiveq},
	{"argmodeq", argmodeq},
	{"bltin", bltin},
	{"add", add},
	{"sub", sub},
	{"mul", mul},
	{"divop", divop},
	{"mod", mod},
	{"negate", negate},
	{"eval", eval},
	{"preinc", preinc},
	{"predec", predec},
	{"postinc", postinc},
	{"postdec", postdec},
	{"gt", gt},
	{"lt", lt},
	{"ge", ge},
	{"le", le},
	{"eq", eq},
	{"ne", ne},
	{"and", and},
	{"or", or },
	{"not", not },
	{"power", power},
	{"assign", assign},
	{"addeq", addeq},
	{"subeq", subeq},
	{"muleq", muleq},
	{"diveq", diveq},
	{"modeq", modeq},
	{"printop", printtop},
	{"prexpr", prexpr},
	{"prstr", prstr},
	{"varread", varread},
	{0, 0}};

void init(void) /* install constants and built-ins in table */
{
	int i;
	Symbol *s;
	for (i = 0; keywords[i].name; i++)
		install_key(keywords[i].name, keywords[i].kval, 0.0);
	for (i = 0; consts[i].name; i++)
		install_key(consts[i].name, VAR, consts[i].cval);
	for (i = 0; builtins[i].name; i++)
	{
		s = install_key(builtins[i].name, BLTIN, 0.0);
		s->u.ptr = builtins[i].func;
	}
}

char *getCodeThoughAddress(Inst inst)
{
	for (int i = 0; codeLookupTable[i].name; i++)
	{
		if (inst == codeLookupTable[i].inst)
			return codeLookupTable[i].name;
	}
	return 0;
}
