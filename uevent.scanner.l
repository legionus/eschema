%option reentrant noyywrap nounput
%option outfile="uevent.scanner.c" header-file="uevent.scanner.h"
%option bison-bridge
%option stack
%option noyy_top_state

%{
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <error.h>

#include "uevent.parser.h"
#include "uevent.h"

extern struct dirent *rulefile;
extern char str[4096];
extern int linenumber;

char *p, *pmax;

static void
stringovfl(void)
{
	error(EXIT_FAILURE, 0, "%s:%d: string too long", rulefile->d_name, linenumber);
}

%}

%x STR
%x COM

True    #t(rue)?
False   #f(alse)?
Symbol  [A-Za-z0-9\?\+\-]+

%%
\n			{
				linenumber++;
			}
\;.*\n			{
				linenumber++;
			}
[ \t]			;
\(			{
				return LEFT_BRACKET;
			}
\)			{
				return RIGHT_BRACKET;
			}
{False}			{
				yylval->num = 0;
				return BOOL;
			}
{True}			{
				yylval->num = 1;
				return BOOL;
			}
[0-9]+			{
				yylval->num = strtoll(yytext, NULL, 0);
				return NUMBER;
			}
{Symbol}		{
				yylval->str = strdup(yytext);
				return SYMBOL;
			}
\"			{ //"
				str[0] = '\0';
				p = (char *) str;
				pmax = p + sizeof(str) - 1;
				yy_push_state(STR, yyscanner);
			}
<STR>\\\"		{ //"
				if (p >= pmax)
					stringovfl();
				*p++='"';
			}
<STR>\\\\		{
				if (p >= pmax)
					stringovfl();
				*p++='\\';
			}
<STR>\\n		{
				if (p >= pmax)
					stringovfl();
				*p++='\n';
			}
<STR>[^\"\\]*		{ //"
				char *ptmp = p;
				p += strlen(yytext);
				if (p >= pmax)
					stringovfl();
				strcpy(ptmp, yytext);
			}
<STR>\"			{ //"
				*p = '\0';
				yy_pop_state(yyscanner);
				yylval->str = strdup(str);
				return STRING;
			}
.			{
				/* report any unknown characters */
				error(EXIT_FAILURE, 0, "%s:%d: unexpected char: %s", rulefile->d_name, linenumber, yytext);
				return ERROR;
			}
%%
