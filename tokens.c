#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pspretty.h"

typedef struct
{
	KeywordValue	value;
	char		   *str;
	bool			reserved;
} KeywordPair;


static Token	tokenbuf[10];
static int		tokenidx;

static char	   *istr, *_istr, *STR;		/* STR is ptr to last read char */
static char	   *line, *LINE;			/* can be null, when we lost information, where current line starts */
static int		lineno, LINENO;			/* start from zero */
static int		pos, POS;				/* can be -1, when we lost information about position from start of line */

static bool		after_eoln;
static bool		force8bit;

/*
 * Keywords table, should be sorted.
 */
KeywordPair keywords[] = {
  { k_AND, "and", true },
  { k_AS, "as", false },
  { k_ASC, "asc", false },
  { k_BY, "by", false },
  { k_DELETE, "delete", false },
  { k_DESC, "desc", false },
  { k_EXISTS, "exists", false },
  { k_FROM, "from", true },
  { k_GROUP, "group", false },
  { k_GROUP_BY, "group by", true },
  { k_HAVING, "having", true },
  { k_IN, "in", true },
  { k_INSERT, "insert", false },
  { k_INTO, "into", true },
  { k_IS, "is", true },
  { k_IS_NOT, "is not", true },
  { k_IS_NOT_NULL, "is not null", true },
  { k_IS_NULL, "is null", true },
  { k_LIMIT, "limit", true },
  { k_NOT, "not", true },
  { k_NOT_IN, "not in", true },
  { k_NULL, "null", true },
  { k_OR, "or", true },
  { k_ORDER, "order", false },
  { k_ORDER_BY, "order by", true },
  { k_SELECT, "select", true },
  { k_VALUES, "values", false },
  { k_WHERE, "where", true },
  { k_WITH, "with", false }
};

/******************************************************
 *
 *  Tokenizer aux methods
 *
 ******************************************************/

/*
 * case insensitive searching (pstr is not zero cstring).
 */
static int
keyword_cmp(char *pstr, int bytes, char *keyword)
{
	int		c;

	while (bytes--)
	{
		c = tolower(*pstr++);

		if (c < *keyword)
			return -1;
		if (c > *keyword)
			return 1;

		keyword += 1;
	}

	if (*keyword)
		return -1;

	return 0;
}

/*
 * Returns -1 if string is not known keyword
 */
static int
search_keyword(char *pstr, int bytes)
{
	int		l = 0;
	int		h = sizeof(keywords)/sizeof(KeywordPair) - 1;

	while (l <= h)
	{
		int		m = (l + h) / 2;

		switch (keyword_cmp(pstr, bytes, keywords[m].str))
		{
			case -1:
				h = m - 1;
				break;
			case 1:
				l = l + 1;
				break;
			case 0:
				return keywords[m].value;
		}
	}

	return -1;
}

/*
 * returns true for chars allowed in SQL identifier.
 */
static bool
is_identifier(int c, bool first)
{
	if ((c >= 'a' && c <= 'z') ||
		(c >= 'A' && c <= 'Z'))
	  return true;

	if (c >= '0' && c <= '9')
	  return !first;

	if (c >= 128 && c <= 255)
		return true;

	if (c == '_')
		return true;

	return false;
}

/*
 * returns true for chars allowed in PostgreSQL operator
 */
static bool
is_operator(int c)
{
	switch (c)
	{
		case '~':
		case '@':
		case '%':
		case '+':
		case '-':
		case '*':
		case '/':
		case '^':
		case '?':
		case '<':
		case '>':
		case '=':
		case '!':
		case '|':
			return true;
	}

	return false;
}

/*
 * returns true if c is any white char
 */
static bool
is_white_char(int c)
{
	switch (c)
	{
		case ' ':
		case '\n':
		case '\r':
		case '\t':
		case '\f':
		case '\v':
			return true;
	}

	return false;
}

static bool
is_keyword(int c)
{
	return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

/*
 * return next char from input string
 */
static char
sgetc()
{
	if (*_istr != '\0')
	{
		if (after_eoln)
		{
			line = _istr;
			pos = 0;
			lineno += 1;
			after_eoln = false;
		}

		if (*_istr == '\n')
			after_eoln = true;

		/* save position of current char */
		STR = _istr;
		POS = pos++;
		LINENO = lineno;
		LINE = line;

		return *_istr++;
	}
	else
		return EOF;
}

/*
 * move read ptr back
 */
static void
sungetc()
{
	if (_istr > istr)
	{
		if (*--_istr == '\n')
		{
			if (!after_eoln)
			{
				lineno -= 1;
				pos = -1;
				line = NULL;
			}
			else
				after_eoln = true;
		}
		else
		{
			pos -= 1;
		}
	}
}

static Token *
read_operator(Token *token, int bytes)
{
	while (is_operator(sgetc()))
		bytes++;

	sungetc();

	token->type = tt_operator;
	token->bytes = bytes;

	return token;
}

static int
utf8charlen(char ch)
{
	if ((ch & 0x80) == 0)
		return 1;

	if ((ch & 0xF0) == 0xF0)
		return 4;

	if ((ch & 0xE0) == 0xE0)
		return 3;

	if ((ch & 0xC0) == 0xC0)
		return 2;

	return 1;
}

/******************************************************
 *
 *  Tokenizer main method
 *
 ******************************************************/

/*
 * The source string should be immutable for all time.
 */
static Token *
_next_token(Token *token)
{
	int		c;

	c = sgetc();
	while (c != EOF && is_white_char(c))
		c = sgetc();

	token->type = tt_unknown;
	token->lineno = LINENO;
	token->line = LINE;
	token->pos = POS;
	token->str = STR;
	token->quoted = false;
	token->escaped = false;
	token->singleline = false;
	token->value = -1;
	token->reserved = false;

	if (c >= '0' && c <= '9' || c == '.')
	{
		bool	was_dot = c == '.';
		int		bytes = 1;

		c = sgetc();
		while ((c != EOF) && ((c >= '0' && c <= '9') || (c == '.' && !was_dot)))
		{
			was_dot = (c == '.');
			bytes++;
			c = sgetc();
		}

		sungetc();

		token->bytes = bytes;

		if (was_dot && bytes == 1)
		{
			token->type = tt_other;
			token->value = '.';
		}
		else
			token->type = tt_numeric;
	}
	else if (is_identifier(c, true))
	{
		bool	is_keyword_char = is_keyword(c);
		int		bytes = 1;

		c = sgetc();
		while ((c != EOF) && is_identifier(c, false))
		{
			bytes++;
			if (is_keyword_char && !is_keyword(c))
					is_keyword_char = false;

			if (!force8bit)
			{
				int		i;

				/* read other bytes from multibyte char */
				for (i = 1; i < utf8charlen(c); i++)
				{
					if (sgetc() == EOF)
						break;
					bytes += 1;
				}
			}

			c = sgetc();
		}

		sungetc();

		token->type = tt_ident;
		token->bytes = bytes;

		/* Maybe it is a keyword */
		if (is_keyword_char)
		{
			int		keyword_id = search_keyword(token->str, token->bytes);

			if (keyword_id != -1)
			{
				token->type = tt_keyword;
				token->value = keyword_id;
				token->reserved = keywords[token->value - 256].reserved;
			}
		}
	}
	else if (c == ';')
	{
		token->type = tt_semicolon;
		token->bytes = 1;
		token->value = ';';
	}
	else if (c == ',')
	{
		token->type = tt_comma;
		token->bytes = 1;
		token->value = ',';
	}
	else if (c == '(')
	{
		token->type = tt_lparent;
		token->bytes = 1;
		token->value = '(';
	}
	else if (c == ')')
	{
		token->type = tt_rparent;
		token->bytes = 1;
		token->value = ')';
	}
	else if (c == '[')
	{
		token->type =  tt_lbracket;
		token->bytes = 1;
		token->value = '[';
	}
	else if (c == ']')
	{
		token->type = tt_rbracket;
		token->bytes = 1;
		token->value = ']';
	}
	else if (c == '=')
	{
		c = sgetc();
		if (c == '>')
		{
			token->type = tt_named_expr;
			token->bytes = 2;
		}
		else
		{
			sungetc();
			token = read_operator(token, 1);
		}
	}
	else if (c == '\'')
	{
		int		bytes = 1;
		bool	closed = false;

		c = sgetc();
		while (c != EOF)
		{
			if (c == '\'')
			{
				/* look ahead */
				c = sgetc();
				if (c == '\'')
				{
					/* double single quotes */
					bytes++;
				}
				else
				{
					sungetc();
					closed = true;
					bytes++;
					break;
				}
			}

			bytes++;
			c = sgetc();
		}

		if (!closed)
		{
			fprintf(stderr, "unclosed string on line %d position %d\n", token->lineno + 1, token->pos);
			return NULL;
		}

		token->type = tt_string;
		token->bytes = bytes;
	}
	else if (c == '"')
	{
		int		bytes = 1;
		bool	closed = false;

		/*
		 * SQL string can be multiline, so local buffer should be used
		 */
		c = sgetc();
		while (c != EOF)
		{
			if (c == '"')
			{
				/* look ahead */
				c = sgetc();
				if (c == '"')
				{
					/* double double quotes */
					bytes++;
				}
				else
				{
					sungetc();
					closed = true;
					bytes++;
					break;
				}
			}

			bytes++;
			c = sgetc();
		}

		if (!closed)
		{
			fprintf(stderr, "unclosed identifier on line %d position %d\n", token->lineno + 1, token->pos);
			return NULL;
		}

		token->type = tt_ident;
		token->bytes = bytes;
		token->quoted = true;
	}
	else if (c == '/')
	{

		c = sgetc();
		if (c == '*') /* multiline comment */
		{
			int		bytes = 2;
			bool	closed = false;

			c = sgetc();
			while (c != EOF)
			{
				if (c == '*')
				{
					c = sgetc();
					if (c == '/')
					{
						bytes += 2;
						closed = true;
						break;
					}
					else
						sungetc();
				}

				bytes++;
				c = sgetc();
			}

			if (!closed)
			{
				fprintf(stderr, "unclosed comments on line %d position %d\n", token->lineno + 1, token->pos);
				return NULL;
			}

			token->type = tt_comment;
			token->bytes = bytes;
		}
		else
		{
			sungetc();
			token = read_operator(token, 1);
		}
	}
	else if (c == '-')
	{
		c = sgetc();
		if (c == '-')
		{
			int bytes = 2;

			/* read to endof line */
			c = sgetc();
			while (c != EOF && c != '\n')
			{
				bytes++;
				c = sgetc();
			}

			sungetc();

			token->type = tt_comment;
			token->bytes = bytes;
			token->singleline = true;
		}
		else
		{
			sungetc();
			token = read_operator(token, 1);
		}
	}
	else if (is_operator(c))
	{
		token = read_operator(token, 1);
	}
	else if (c == ':')
	{
		c = sgetc();
		if (c == ':')
		{
			token->type = tt_cast_operator;
			token->bytes = 2;
		}
		else if (c == '=')
		{
			token->type = tt_named_expr;
			token->bytes = 2;
		}
		else
		{
			sungetc();
			token->type = tt_other;
			token->bytes = 1;
			token->value = ':';
		}
	}
	else
	{
		if (c != EOF)
		{
			token->type = tt_other;
			token->bytes = 1;
			token->value = c;
		}
		else
			token->type = tt_EOF;
	}

	return token;
}

/******************************************************
 *
 *  Public API
 *
 ******************************************************/

/*
 * ensure correct keyword table sorting
 */
static void
check_keyword_table()
{
	int		values = sizeof(keywords)/sizeof(KeywordPair);
	int		i;

	for (i = 0; i < values; i++)
	{
		KeywordPair *kp = &keywords[i];

		if (kp->value != i + 256)
		{
			fprintf(stderr, "unexpected keyword value (%d expected %d) for keyword \"%s\"\n", kp->value, i + 256, kp->str);
			exit(1);
		}

		if (i > 1)
		{
			if (keyword_cmp(keywords[i].str, strlen(keywords[i].str), keywords[i-1].str) != 1)
			{
				fprintf(stderr, "unsorted keyword table (%s %s)\n", keywords[i].str, keywords[i-1].str);
				exit(1);
			}
		}
	}
}

/*
 * Initialize module variables and parsed string
 */
void
init_lexer(char *str, bool _force8bit)
{
	_istr = istr = str;
	line = str;
	lineno = 0;
	pos = 0;
	tokenidx = 0;
	after_eoln = false;
	force8bit = _force8bit;

	/* check prereq. */
	check_keyword_table();
}

void
push_token(Token *token)
{
	/* tokenbuf[0] is reserved */
	if (tokenidx < 10)
		memcpy(&tokenbuf[tokenidx++], token, sizeof(Token));
	else
	{
		fprintf(stderr, "no space in token buffer");
		exit(1);
	}
}

static Token *
possible_multiverb2(Token *token,
					KeywordValue required,
					KeywordValue newval)
{
	Token	t, *_t;

	_t = next_token(&t);
	if (!_t)
		return token;

	if (t.type == tt_keyword && t.value == required)
	{
		token->value = newval;
		token->reserved = keywords[token->value - 256].reserved;
		token->str = keywords[token->value - 256].str;
		token->bytes = strlen(token->str);
		return token;
	}

	push_token(_t);

	return token;
}

static Token *
possible_multiverb3(Token *token,
					KeywordValue required,
					KeywordValue required2,
					KeywordValue newval,
					bool *changed)
{
	Token	t, *_t;

	*changed = false;

	_t = next_token(&t);
	if (!_t)
		return token;

	if (t.type == tt_keyword && t.value == required)
	{
		Token	t2, *_t2;

		_t2 = next_token(&t2);
		if (_t2)
		{
			if (t2.type == tt_keyword && t2.value == required2)
			{
				token->value = newval;
				token->reserved = keywords[token->value - 256].reserved;
				token->str = keywords[token->value - 256].str;
				token->bytes = strlen(token->str);
				*changed = true;
				return token;
			}

			push_token(_t2);
		}
	}

	push_token(_t);

	return token;
}


Token *
next_token(Token *token)
{
	if (tokenidx > 0)
		memcpy(token, &tokenbuf[--tokenidx], sizeof(Token));
	else
	{
		token = _next_token(token);

		/* possible multiverbs like GROUP BY, ORDER BY */
		if (token->type == tt_keyword)
		{
			if (token->value == k_GROUP)
				token = possible_multiverb2(token, k_BY, k_GROUP_BY);
			else if (token->value == k_ORDER)
				token = possible_multiverb2(token, k_BY, k_ORDER_BY);
			else if (token->value == k_NOT)
				token = possible_multiverb2(token, k_IN, k_NOT_IN);
			else if (token->value == k_IS)
			{
				bool	changed;

				token = possible_multiverb3(token, k_NOT, k_NULL, k_IS_NOT_NULL, &changed);
				if (!changed)
					token = possible_multiverb2(token, k_NULL, k_IS_NULL);
			}
		}
	}

	return token;
}


static char *
token_type_name(Token *token)
{
	switch (token->type)
	{
		case tt_EOF:
			return "EOF";
		case tt_unknown:
			return "noinit";
		case tt_keyword:
			return "Keyword";
		case tt_comment:
			return "Comment";
		case tt_numeric:
			return "Numeric";
		case tt_string:
			return "String";
		case tt_lparent:
			return "LParent";
		case tt_rparent:
			return "RParent";
		case tt_lbracket:
			return "LBracket";
		case tt_rbracket:
			return "RBracket";
		case tt_ident:
			return "Ident";
		case tt_other:
			return "Other";
		case tt_operator:
			return "Operator";
		case tt_cast_operator:
			return "Cast operator";
		case tt_comma:
			return "Comma";
		default:
			return "Unknown";
	}
}

void
debug_print_token(Token *token)
{

	if (token)
	{
		fprintf(stderr, "DEBUG: token: %-8s, content:\"%.*s\"",
							token_type_name(token),
							token->bytes,
							token->str);

		if (token->value != -1)
			fprintf(stderr, ", value: %d", token->value);

		if (token->type == tt_keyword)
			fprintf(stderr, ", keyword: \"%s\"", keywords[token->value - 256].str);

		fprintf(stderr, "\n");
	}
	else
		fprintf(stderr, "DEBUG: null\n");
}
