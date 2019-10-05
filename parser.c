#include <stdio.h>
#include <string.h>

#include "pspretty.h"

static bool is_signed_operand(bool *error);
static bool is_expr_in_parenthesis(bool *error);
static bool is_function_args(bool *error);

#define	ON_ERROR_RETURN()			do { if (*error) { return 0;} } while (0)
#define	ON_EMPTY_RETURN_ERROR()		do { if (!_t) { *error = 1; return 0; }} while (0)
#define	RETURN_ERROR()				do { *error = 1; return 0; } while (0)

/*
 * ident.ident.ident[.ident ...]
 *
 */
static bool
is_qualified_ident(bool *error)
{
	Token	t, *_t;
	bool	result = true;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (t.type == tt_ident || (t.type == tt_keyword && !t.reserved))
	{
		Token	t2, *_t2;

		_t2 = next_token(&t2);
		ON_EMPTY_RETURN_ERROR();

		if (t2.type == tt_other && t2.value == '.')
		{
			result = is_qualified_ident(error);
			ON_ERROR_RETURN();

			if (result)
				return true;
		}

		push_token(_t2);
		if (result)
			return true;
	}

	push_token(_t);
	return false;
}

/*
 * [ ident.ident.ident[.ident ...]. ] *
 *
 */
static bool
is_qualified_star(bool *error)
{
	Token	t, *_t;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (t.type == tt_operator && t.bytes == 1 && strncmp(t.str, "*", 1) == 0)
	{
		return true;
	}
	else if (t.type == tt_ident || (t.type == tt_keyword && !t.reserved))
	{
		Token	t2, *_t2;

		_t2 = next_token(&t2);
		ON_EMPTY_RETURN_ERROR();

		if (t2.type == tt_other && t2.value == '.')
		{
			if (is_qualified_star(error))
				return true;
			ON_ERROR_RETURN();
		}

		push_token(_t2);
	}

	push_token(_t);
	return false;
}

static bool
is_operand(bool *error)
{
	Token	t, *_t;

	if (is_signed_operand(error))
		return true;
	ON_ERROR_RETURN();

	if (is_expr_in_parenthesis(error))
		return true;
	ON_ERROR_RETURN();

	if (is_qualified_ident(error))
	{
		(void) is_function_args(error);
		ON_ERROR_RETURN();

		return true;
	}

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (t.type == tt_numeric)
		return true;
	else if (t.type == tt_ident)
		return true;
	else if (t.type == tt_string)
		return true;
	else if (t.type == tt_keyword && !t.reserved)
		return true;

	push_token(_t);
	return false;
}

static bool
is_signed_operand(bool *error)
{
	Token	t, *_t;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (_t->type == tt_operator && 
		((strncmp(t.str, "+", 1) == 0 || strncmp(t.str, "-", 1) == 0)))
	{
		if (is_operand(error))
			return true;

		push_token(_t);
		return false;
	}
	else
	{
		push_token(_t);
		return false;
	}
}

/*
 * Operand | Operand op expr
 *
 */
static bool
is_expr(bool *error)
{
	if (is_operand(error))
	{
		Token	t, *_t;

		ON_ERROR_RETURN();

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		if (t.type == tt_operator)
		{
			if (is_expr(error))
				return true;

			RETURN_ERROR();
		}

		push_token(_t);
		return true;
	}

	return false;
}

static bool
is_expr_in_parenthesis(bool *error)
{
	Token	t, *_t;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (t.type != tt_lparent)
	{
		push_token(_t);
		return false;
	}

	if (!is_expr(error))
		RETURN_ERROR();

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (t.type != tt_rparent)
	{
		fprintf(stderr, "unclosed parenthesis\n");
		RETURN_ERROR();
	}

	return true;
}

/*
 * expr [, expr ...]
 *
 */
static bool
is_expr_list(bool *error)
{
	if (is_expr(error))
	{
		Token	t, *_t;

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		if (t.type == tt_comma)
		{
			return is_expr_list(error);
		}

		push_token(_t);
		return true;
	}

	return false;
}

/*
 * [ AS] label
 *
 */
static bool
is_label(bool *error)
{
	Token	t, *_t;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (t.type == tt_keyword && t.value == k_AS)
	{
		Token	t2, *_t2;

		_t2 = next_token(&t2);
		ON_EMPTY_RETURN_ERROR();

		if (t2.type == tt_ident ||
				(t2.type == tt_keyword && !t2.reserved))
		{
			return true;
		}

		RETURN_ERROR();
	}

	if (t.type == tt_ident ||
			(t.type == tt_keyword && !t.reserved))
	{
		return true;
	}

	push_token(_t);

	return false;
}

/*
 * expr [ [AS] label ] [, expr [ [AS] label ] ...]
 *
 */
static bool
is_labeled_expr_list(bool *error)
{
	Token	t, *_t;
	bool	is_star = false;

	is_star = is_qualified_star(error);
	ON_ERROR_RETURN();

	if (is_star || is_expr(error))
	{
		Token	t, *_t;

		(void) is_label(error);
		ON_ERROR_RETURN();

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		if (t.type == tt_comma)
		{
			return is_labeled_expr_list(error);
		}

		push_token(_t);
		return true;
	}

	return false;
}

/*
 * name => 
 *
 */
static bool
is_name(bool *error)
{
	Token	t, *_t;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (t.type == tt_ident ||
			(t.type == tt_keyword && !t.reserved))
	{
		Token	t2, *_t2;

		_t2 = next_token(&t2);
		ON_EMPTY_RETURN_ERROR();

		if (t2.type == tt_named_expr)
		{
			return true;
		}

		push_token(_t2);
	}

	push_token(_t);
	return false;
}

/*
 * [ name => ] value p [, name => ] value ... ]
 *
 */
static bool
is_named_expr_list(bool *error)
{
	bool	is_named;

	is_named = is_name(error);
	ON_ERROR_RETURN();

	if (is_expr(error))
	{
		Token	t, *_t;

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		if (t.type == tt_comma)
		{
			return is_named_expr_list(error);
		}

		push_token(_t);
		return true;
	}

	return false;
}

/*
 * (), ( [ name => ] value [ , ... ] )
 *
 */
static bool
is_function_args(bool *error)
{
	Token	t, *_t;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (t.type == tt_lparent)
	{
		Token	t2, *_t2;
		bool	has_args;

		has_args = is_named_expr_list(error);
		ON_ERROR_RETURN();

		_t2 = next_token(&t2);
		ON_EMPTY_RETURN_ERROR();

		if (t2.type == tt_rparent)
			return true;

		RETURN_ERROR();
	}

	push_token(_t);
	return false;
}

/*
 * Returns true, when there are not any syntax error
 *
 */
bool
parser(char *str, bool force8bit)
{
	bool	error;

	init_lexer(str, force8bit);

	fprintf(stderr, "is_labeled_expr_list %s\n", is_labeled_expr_list(&error) ? "yes" : "no");

	if (!error)
	{
		Token	t, *_t;

		_t = next_token(&t);
		if (!(_t && t.type == tt_EOF))
		{
			fprintf(stderr, "syntax error (not on the end)\n");
			return false;
		}
	}
	else
	{
		fprintf(stderr, "syntax error (parsing error)\n");
		return false;
	}

	return true;
}
