#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pspretty.h"

static Node * is_expr_in_parenthesis(bool *error);
static Node * is_function_args(bool *error);
static Node * is_query(bool *error);
static Node * is_expr_list(bool *error);

static bool is_join_keyword(Token *t);
static Node * is_relation_expr(bool *error, bool join_required, Node *leftrel);
static Node * is_relation_expr_list(bool *error);

static Node * is_operand(bool *error);
static Node * new_node(NodeType type);


#define	ON_ERROR_RETURN()			do { if (*error) { return NULL;} } while (0)
#define	ON_EMPTY_RETURN_ERROR()		do { if (!_t) { *error = 1; return NULL; }} while (0)
#define	RETURN_ERROR()				do { *error = 1; return NULL; } while (0)

NodeAllocator	   *root_allocator;
NodeAllocator	   *current_allocator;


static Node *
new_node_value(NodeType type, Node *value)
{
	Node *result = new_node(type);

	result->value = value;
	if (type == n_expr)
		result->exprtype = expr_generic;

	return result;
}

static Node *
new_node_str(NodeType type, Token *token)
{
	Node *result = new_node(type);

	result->str = token->str;
	result->bytes = token->bytes;

	if (type == n_expr)
		result->exprtype = expr_generic;

	return result;
}

static bool
is_keyword(Token *token, KeywordValue k)
{
	return token->type == tt_keyword && token->value == k;
}

static bool
is_not_reserved_keyword(Token *token)
{
	return token->type == tt_keyword && !token->reserved;
}

static bool
is_enhanced_ident(Token *token)
{
	return token->type == tt_ident || is_not_reserved_keyword(token);
}

static bool
is_operator(Token *token, const char *op)
{
	if (token->type == tt_operator &&
			token->bytes == strlen(op) &&
			strncmp(token->str, op, token->bytes) == 0)
		return true;
	return false;
}

/*
 * ident.ident.ident[.ident ...]
 *
 */
static Node *
is_qualified_ident(bool *error)
{
	Token	t, *_t;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (is_enhanced_ident(_t))
	{
		Token	t2, *_t2;
		Node	*_node;
		bool	revert = false;

		_t2 = next_token(&t2);
		ON_EMPTY_RETURN_ERROR();

		if (t2.type == tt_dot)
		{
			Node   *other;

			other = is_qualified_ident(error);
			ON_ERROR_RETURN();
			if (other)
			{
				/* allocation is done in last moment, because there is not free */
				Node   *result = new_node_str(n_ident, _t);

				result->other = other;
				return result;
			}
			else
				revert = true;
		}

		push_token(_t2);
		if (!revert)
			return new_node_str(n_ident, _t);
	}

	push_token(_t);
	return NULL;
}

/*
 * [ ident.ident.ident[.ident ...]. ] *
 *
 */
static Node *
is_qualified_star(bool *error)
{
	Token	t, *_t;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (is_operator(_t, "*"))
	{
		return new_node_str(n_star, _t);
	}
	else if (is_enhanced_ident(_t))
	{
		Token	t2, *_t2;

		_t2 = next_token(&t2);
		ON_EMPTY_RETURN_ERROR();

		if (t2.type == tt_dot)
		{
			Node   *result;

			result = new_node_str(n_star, _t);
			if(result->other = is_qualified_star(error))
			{
				return result;
			}

			ON_ERROR_RETURN();
		}

		push_token(_t2);
	}

	push_token(_t);
	return NULL;
}


static Node *
is_signed_operand(bool *error)
{
	Token	t, *_t;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (is_operator(_t, "+") || is_operator(_t, "-"))
	{
		Node	*result;

		result = is_operand(error);
		ON_ERROR_RETURN();

		if (result)
		{
			if (strncmp(t.str, "-", 1) == 0)
			{
				if (result->type != n_expr && result->type != n_expr_wrapper)
					result = new_node_value(n_expr_wrapper, result);

				result->negative = !result->negative;
			}
			return result;
		}
	}

	push_token(_t);
	return NULL;
}

/*
 * NOT operand
 *
 */
static Node *
is_negated_operand(bool *error)
{
	Token	t, *_t;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (is_keyword(_t, k_NOT))
	{
		Node	*result;

		result = is_operand(error);
		ON_ERROR_RETURN();

		if (result)
		{
			if (result->type != n_expr && result->type != n_expr_wrapper)
				result = new_node_value(n_expr_wrapper, result);

			result->negate = true;
			return result;
		}
	}

	push_token(_t);
	return NULL;
}

static Node *
is_operand(bool *error)
{
	Token	t, *_t;
	Node   *result;

	if (result = is_signed_operand(error))
		return result;
	ON_ERROR_RETURN();

	if (result = is_expr_in_parenthesis(error))
		return result;
	ON_ERROR_RETURN();

	if (result = is_qualified_ident(error))
	{
		Node   *fx;

		fx = is_function_args(error);
		ON_ERROR_RETURN();
		if (fx)
		{
			/* name is in other field */
			fx->other = result;
			return fx;
		}

		return result;
	}

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (is_keyword(_t, k_NULL))
		return new_node_str(n_null, _t);
	else if (is_keyword(_t, k_FALSE))
		return new_node_str(n_false, _t);
	else if (is_keyword(_t, k_TRUE))
		return new_node_str(n_true, _t);
	else if (t.type == tt_numeric)
		return new_node_str(n_numeric, _t);
	else if (t.type == tt_string)
		return new_node_str(n_string, _t);
	else if (is_not_reserved_keyword(_t))
		return new_node_str(n_string, _t);

	push_token(_t);
	return NULL;
}

/*
 * Operand | Operand op expr
 *
 */
static Node *
is_expr_00(bool *error)
{
	Token	t, *_t;
	Node   *result;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (is_keyword(_t, k_EXISTS))
	{
		Token	t2, *_t2;

		_t2 = next_token(&t2);
		ON_EMPTY_RETURN_ERROR();

		if (t2.type == tt_lparent)
		{
			Node	*query;
			push_token(_t2);

			query = is_expr_in_parenthesis(error);
			ON_ERROR_RETURN();

			if (query->type == n_query)
			{
				result = new_node_str(n_expr, _t);
				result->value = query;

				return result;
			}

			RETURN_ERROR();
		}

		push_token(_t2);
	}

	push_token(_t);

	if (result = is_operand(error))
	{
		Token	t, *_t;

		ON_ERROR_RETURN();

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		if (t.type == tt_operator && !t.comparing_op)
		{
			Node   *expr = new_node_str(n_expr, _t);

			expr->value = result;

			if (expr->other = is_expr_00(error))
				return expr;

			RETURN_ERROR();
		}

		push_token(_t);
		return result;
	}

	return NULL;
}

/*
 * Add expr IS FALSE, expr IS TRUE, expr IS UNKNOWN
 */
static Node *
is_expr_01(bool *error)
{
	Node	*result;

	if (result = is_expr_00(error))
	{
		Token	t, *_t;

		ON_ERROR_RETURN();

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		if (is_keyword(_t, k_IS))
		{
			bool	negate = false;

			_t = next_token(&t);
			ON_EMPTY_RETURN_ERROR();

			if (is_keyword(_t, k_NOT))
			{
				negate = true;
				_t = next_token(&t);
				ON_EMPTY_RETURN_ERROR();
			}

			if (is_keyword(_t, k_UNKNOWN) ||
				is_keyword(_t, k_FALSE) || is_keyword(_t, k_TRUE))
			{
				Node   *expr = new_node_str(n_is, _t);

				expr->value = result;
				expr->negate = negate;
				return expr;
			}

			RETURN_ERROR();
		}

		push_token(_t);
	}

	return result;
}

/*
 * Add expr IS NULL, expr IS NOT NULL
 *
 */
static Node *
is_expr_02(bool *error)
{
	Node	*result;

	if (result = is_expr_01(error))
	{
		Token	t, *_t;

		ON_ERROR_RETURN();

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		if (is_keyword(_t, k_IS_NULL) ||
			is_keyword(_t, k_IS_NOT_NULL))
		{
			Node   *expr = new_node_str(is_keyword(_t, k_IS_NULL) ? n_is_null : n_is_not_null, _t);

			expr->value = result;
			return expr;
		}

		push_token(_t);
	}

	return result;
}

/*
 * expr BETWEEN expr AND expr
 */
static Node *
is_expr_04(bool *error)
{
	Node   *result;

	if (result = is_expr_02(error))
	{
		Token	t, *_t;

		ON_ERROR_RETURN();

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		if (is_keyword(_t, k_BETWEEN))
		{
			Node   *expr = new_node_str(n_expr, _t);
			Node   *lval;

			expr->value = result;
			expr->exprtype = expr_between;

			lval = is_expr_02(error);
			ON_ERROR_RETURN();

			_t = next_token(&t);
			ON_EMPTY_RETURN_ERROR();

			if (is_keyword(_t, k_AND))
			{
				expr->other = new_node_str(n_expr,_t);
				expr->other->value = lval;

				if (expr->other->other = is_expr_02(error))
					return expr;
			}

			ON_ERROR_RETURN();
			RETURN_ERROR();
		}

		push_token(_t);
	}

	return result;
}

/*
 * expr LIKE expr, expr ILIKE expr
 */
static Node *
is_expr_05(bool *error)
{
	Node   *result;

	if (result = is_expr_04(error))
	{
		Token	t, *_t;

		ON_ERROR_RETURN();

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		if (is_keyword(_t, k_LIKE) || is_keyword(_t, k_ILIKE))
		{
			Node   *expr = new_node_str(n_expr, _t);

			expr->value = result;
			expr->exprtype = is_keyword(_t, k_LIKE) ? expr_like : expr_ilike;

			if (expr->other = is_expr_04(error))
				return expr;

			ON_ERROR_RETURN();
			RETURN_ERROR();
		}

		push_token(_t);
	}

	return result;
}

/*
 * expr <> expr
 */
static Node *
is_expr_06(bool *error)
{
	Node   *result;

	if (result = is_expr_05(error))
	{
		Token	t, *_t;

		ON_ERROR_RETURN();

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		if (t.type == tt_operator && t.comparing_op && !is_operator(_t, "="))
		{
			Node   *expr = new_node_str(n_expr, _t);

			expr->value = result;
			if (expr->other = is_expr_05(error))
				return expr;

			ON_ERROR_RETURN();
			RETURN_ERROR();
		}

		push_token(_t);
	}

	return result;
}

/*
 * expr = expr
 */
static Node *
is_expr_07(bool *error)
{
	Node   *result;

	if (result = is_expr_06(error))
	{
		Token	t, *_t;

		ON_ERROR_RETURN();

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		if (is_operator(_t, "="))
		{
			Node   *expr = new_node_str(n_expr, _t);

			expr->value = result;
			if (expr->other = is_expr_06(error))
				return expr;

			ON_ERROR_RETURN();
			RETURN_ERROR();
		}

		push_token(_t);
	}

	return result;
}

/*
 * NOT expr
 */
static Node *
is_expr_08(bool *error)
{
	Token	t, *_t;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (is_keyword(_t, k_NOT))
	{
		Node	*result;

		result = is_expr_07(error);
		ON_ERROR_RETURN();

		if (result)
		{
			result->negate = true;
			return result;
		}
	}

	push_token(_t);
	return is_expr_07(error);
}


/*
 * Add operator AND
 *
 */
static Node *
is_expr_09(bool *error)
{
	Node	*result;

	if (result = is_expr_08(error))
	{
		Token	t, *_t;

		ON_ERROR_RETURN();

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		if (is_keyword(_t, k_AND))
		{
			Node   *expr = new_node_str(n_logical_and, _t);

			expr->value = result;
			if (expr->other = is_expr_09(error))
				return expr;

			ON_ERROR_RETURN();
			RETURN_ERROR();
		}

		push_token(_t);
	}

	return result;
}

/*
 * Add operator OR
 *
 */
static Node *
is_expr_10(bool *error)
{
	Node	*result;

	if (result = is_expr_09(error))
	{
		Token	t, *_t;

		ON_ERROR_RETURN();

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		if (is_keyword(_t, k_OR))
		{
			Node   *expr = new_node_str(n_logical_or, _t);

			expr->value = result;
			if (expr->other = is_expr_10(error))
				return expr;

			ON_ERROR_RETURN();
			RETURN_ERROR();
		}

		push_token(_t);
	}

	return result;
}

#define	is_expr_top(e)		is_expr_10(e)

/*
 * parses a) ( expr ) b) (SELECT ...), c (expr, expr, expr, ...)
 *
 */
static Node *
is_expr_in_parenthesis(bool *error)
{
	Token	t, *_t;
	Node   *expr, *composite = NULL;
	bool	is_subquery = false;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (t.type != tt_lparent)
	{
		push_token(_t);
		return NULL;
	}

	expr = is_query(error);
	ON_ERROR_RETURN();

	if (!expr)
	{
		expr = is_expr_top(error);
		ON_ERROR_RETURN();
	}

	if (!expr)
		return NULL;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (t.type == tt_comma)
	{
		composite = new_node_value(n_composite,
					  new_node_value(n_list, expr));
		composite->value->other = is_expr_list(error);
		ON_ERROR_RETURN();

		_t = next_token(&t);
	}

	if (t.type != tt_rparent)
	{
		fprintf(stderr, "unclosed parenthesis\n");
		RETURN_ERROR();
	}

	if (composite)
		return composite;

	if (expr->type != n_expr && expr->type != n_expr_wrapper)
		expr = new_node_value(n_expr_wrapper, expr);

	expr->parenthesis = true;
	return expr;
}

/*
 * expr [, expr ...]
 *
 * returns list of expr nodes.
 *
 */
static Node *
is_expr_list(bool *error)
{
	Node   *expr;

	if (expr = is_expr_top(error))
	{
		Token	t, *_t;
		Node   *result;

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		result = new_node_value(n_expr, expr);

		if (t.type == tt_comma)
		{
			if (result->other = is_expr_list(error))
				return result;

			ON_ERROR_RETURN();
			RETURN_ERROR();
		}

		push_token(_t);
		return result;
	}

	return NULL;
}

/*
 * [ AS] label
 *
 */
static Node *
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

		if (is_enhanced_ident(_t2))
			return new_node_str(n_labeled_expr, _t2);

		RETURN_ERROR();
	}

	if (is_enhanced_ident(_t))
		return new_node_str(n_labeled_expr, _t);

	push_token(_t);

	return NULL;
}

/*
 * expr [ [AS] label ] [, expr [ [AS] label ] ...]
 *
 * returns list of expr or labeled expressions
 *
 */
static Node *
is_labeled_expr_list(bool *error)
{
	Token	t, *_t;
	Node   *node;

	node = is_qualified_star(error);
	ON_ERROR_RETURN();

	if (!node)
	{
		node = is_expr_top(error);
		ON_ERROR_RETURN();
	}

	if (node)
	{
		Token	t, *_t;
		Node   *label;
		Node   *result;

		label = is_label(error);
		ON_ERROR_RETURN();

		if (label)
		{
			label->value = node;
			node = label;
		}

		result = new_node_value(n_list, node);

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		if (t.type == tt_comma)
		{
			if (result->other = is_labeled_expr_list(error))
				return result;

			ON_ERROR_RETURN();
			RETURN_ERROR();
		}

		push_token(_t);
		return result;
	}

	return NULL;
}


/*
 * name =>
 *
 */
static Node *
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
			return new_node_str(n_named_expr, _t);

		push_token(_t2);
	}

	push_token(_t);
	return NULL;
}

/*
 * [ name => ] value p [, name => ] value ... ]
 *
 * Retuns list of named expr or expr
 *
 */
static Node *
is_named_expr_list(bool *error)
{
	Node   *node;
	Node   *expr;

	node = is_name(error);
	ON_ERROR_RETURN();

	expr = is_expr_top(error);
	ON_ERROR_RETURN();

	if (node)
		node->value = expr;
	else
		node = expr;

	if (node)
	{
		Token	t, *_t;
		Node   *result;

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		result = new_node_value(n_list, node);

		if (t.type == tt_comma)
		{
			if (result->other = is_named_expr_list(error))
				return result;

			ON_ERROR_RETURN();
			RETURN_ERROR();
		}

		push_token(_t);
		return result;
	}

	return NULL;
}

/*
 * (), ( [ name => ] value [ , ... ] )
 *
 */
static Node *
is_function_args(bool *error)
{
	Token	t, *_t;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (t.type == tt_lparent)
	{
		Token	t2, *_t2;
		bool	has_args;
		Node   *result;

		result = new_node_value(n_function,
								is_named_expr_list(error));
		ON_ERROR_RETURN();

		_t2 = next_token(&t2);
		ON_EMPTY_RETURN_ERROR();

		if (t2.type == tt_rparent)
			return result;

		fprintf(stderr, "unclosed parenthesis\n");
		RETURN_ERROR();
	}

	push_token(_t);
	return NULL;
}

/*****************************************************************
 * Routines related to FROM clause
 *
 *****************************************************************/

static Node *
is_relation_source(bool *error)
{
	Node   *result;
	Token	t, *_t;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (t.type == tt_lparent)
	{
		Node   *label;

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		if (is_keyword(_t, k_SELECT))
		{
			push_token(_t);
			result = is_query(error);
		}
		else
		{
			push_token(_t);
			result = is_relation_expr(error, true, NULL);
			result->relexpr_parenthesis = true;
		}
		ON_ERROR_RETURN();

		/* empty content is not allowed */
		if (!result)
			RETURN_ERROR();

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		if (t.type != tt_rparent)
		{
			fprintf(stderr, "unclosed parenthesis\n");
			RETURN_ERROR();
		}
	}
	else
	{
		push_token(_t);
		result = is_qualified_ident(error);
		ON_ERROR_RETURN();
	}

	return result;
}

/*
 * [ AS ] label
 *
 */
static Node *
is_relation_label(bool *error)
{
	Token	t, *_t;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (t.type == tt_keyword && t.value == k_AS)
	{
		Token	t2, *_t2;

		_t2 = next_token(&t2);
		ON_EMPTY_RETURN_ERROR();

		if (is_enhanced_ident(_t2))
			return new_node_str(n_labeled_expr, _t2);

		RETURN_ERROR();
	}

	/* protect context important keywords JOIN, ON in this context */
	if (!is_keyword(_t, k_ON)
			&& !is_keyword(_t, k_USING)
			&& !is_join_keyword(_t)
			&& is_enhanced_ident(_t))
		return new_node_str(n_labeled_expr, _t);

	push_token(_t);

	return NULL;
}

/*
 * Is labeled (optional) relation
 *
 */
static Node *
is_relation(bool *error)
{
	Node   *result;

	result = is_relation_source(error);
	ON_ERROR_RETURN();

	if (result)
	{
		Node   *label;

		label = is_relation_label(error);
		ON_ERROR_RETURN();

		if (label)
		{
			label->value = result;
			result = label;
		}
	}

	return result;
}

/*
 * Returns true when keyword is one of JOIN keywords
 *
 */
static bool
is_join_keyword(Token *t)
{
	if (t->type == tt_keyword)
	{
		switch (t->value)
		{
			case k_JOIN:
			case k_INNER_JOIN:
			case k_CROSS_JOIN:
			case k_LEFT_OUTER_JOIN:
			case k_RIGHT_OUTER_JOIN:
			case k_FULL_OUTER_JOIN:
				return true;
			default:
				;
		}
	}

	return false;
}

/*
 * Returns ident list
 *
 */
static Node *
is_ident_list(bool *error)
{
	Token	t, *_t;
	Node   *result = NULL;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (is_enhanced_ident(_t))
	{
		Node    *ident = new_node_str(n_ident, _t);

		result = new_node_value(n_list, ident);

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		if (t.type == tt_comma)
		{
			result->other = is_relation_expr_list(error);
			ON_ERROR_RETURN();
			if (!result->other)
				RETURN_ERROR();
		}
		else
			push_token(_t);
	}
	else
		push_token(_t);

	return result;
}

/*
 * Returns ident list in parenthesis
 *
 */
static Node *
is_ident_p_list(bool *error)
{
	Token	t, *_t;
	Node   *result = NULL;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (t.type == tt_lparent)
	{
		result = is_ident_list(error);
		ON_ERROR_RETURN();

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		if (t.type != tt_rparent)
		{
			fprintf(stderr, "unclosed parenthesis\n");
			RETURN_ERROR();
		}
	}
	else
		push_token(_t);

	return result;
}

/*
 * left_rel JOIN right_rel ON expr
 *
 *
 */
static Node *
is_relation_expr(bool *error, bool join_required, Node *leftrel)
{
	Node   *result;
	bool	parenthesis = false;
	Token	t, *_t;

	if (!leftrel)
	{
		result = is_relation(error);
		ON_ERROR_RETURN();
	}
	else
		result = leftrel;

	if (result)
	{
		Token	t, *_t;

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		if (is_join_keyword(_t))
		{
			Node   *expr = new_node(n_join);

			expr->left = result;
			expr->jointype = t.value;
			expr->is_natural = t.natural_join;

			expr->right = is_relation_expr(error, false, NULL);
			ON_ERROR_RETURN();

			if (expr->right)
			{
				if (!expr->is_natural && expr->jointype != k_CROSS_JOIN)
				{
					_t = next_token(&t);
					ON_EMPTY_RETURN_ERROR();

					if (is_keyword(_t, k_ON))
					{
						expr->onexpr = is_expr_top(error);
						ON_ERROR_RETURN();
						if (!expr->onexpr)
							RETURN_ERROR();
					}
					else if (is_keyword(_t, k_USING))
					{
						expr->using = is_ident_p_list(error);
						ON_ERROR_RETURN();
						if (!expr->using)
							RETURN_ERROR();
					}
					else
						RETURN_ERROR();
				}

				_t = next_token(&t);
				ON_EMPTY_RETURN_ERROR();

				if (is_join_keyword(_t))
				{
					push_token(_t);
					expr = is_relation_expr(error, true, expr);
				}
				else
					push_token(_t);
			}
			else
				RETURN_ERROR();

			result = expr;
		}
		else
		{
			push_token(_t);
			if (join_required)
				RETURN_ERROR();
		}
	}
	else
		RETURN_ERROR();

	return result;
}

static Node *
is_relation_expr_list(bool *error)
{
	Node   *re;

	re = is_relation_expr(error, false, NULL);
	if (re)
	{
		Token	t, *_t;
		Node   *result;

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		result = new_node_value(n_list, re);
		if (t.type == tt_comma)
		{
			if (result->other = is_relation_expr_list(error))
				return result;

			ON_ERROR_RETURN();
			RETURN_ERROR();
		}

		push_token(_t);
		return result;
	}

	return NULL;
}

static bool
check_order_by_flags(Token *token, Node *node, bool *error)
{
	if (is_keyword(token, k_DESC))
	{
		if (node->asc)
			RETURN_ERROR();
		node->desc = true;
		return true;
	}
	else if (is_keyword(token, k_ASC))
	{
		if (node->desc)
			RETURN_ERROR();
		node->asc = true;
		return true;
	}
	else if (is_keyword(token, k_NULLS_FIRST))
	{
		if (node->nulls_last)
			RETURN_ERROR();
		node->nulls_first = true;
		return true;
	}
	else if (is_keyword(token, k_NULLS_LAST))
	{
		if (node->nulls_first)
			RETURN_ERROR();
		node->nulls_last = true;
		return true;
	}
	else
		return false;
}

/*
 * expr [ DESC | ASC ] [ NULLS FIRST | NULLS LAST ]
 *
 */
static Node *
is_order_by_expr(bool *error)
{
	Node   *result;
	Token	t, *_t;

	result = is_expr_top(error);
	ON_ERROR_RETURN();

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	/* we should to assign following flags to expr only */
	if (result->type != n_expr && result->type != n_expr_wrapper)
		result = new_node_value(n_expr_wrapper, result);

	if (check_order_by_flags(_t, result, error))
	{
		ON_ERROR_RETURN();

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		if (!check_order_by_flags(_t, result, error))
			push_token(_t);

		ON_ERROR_RETURN();
	}
	else
		push_token(_t);

	return result;
}

static Node *
is_order_by_expr_list(bool *error)
{
	Node   *expr, *result = NULL;
	Token	t, *_t;

	expr = is_order_by_expr(error);
	ON_ERROR_RETURN();

	if (expr)
	{
		Token	t, *_t;

		result = new_node_value(n_list, expr);

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		if (t.type == tt_comma)
		{
			result->other = is_order_by_expr_list(error);
			ON_ERROR_RETURN();
			if (!result->other)
				RETURN_ERROR();
		}
		else
			push_token(_t);
	}

	return result;
}

static Node *
is_order_by_clause(bool *error)
{
	Node *result = NULL;
	Token	t, *_t;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (is_keyword(_t, k_ORDER_BY))
	{
		result = is_order_by_expr_list(error);
		ON_ERROR_RETURN();

		if (!result)
			RETURN_ERROR();
	}
	else
		push_token(_t);

	return result;
}

static Node *
is_from_clause(bool *error)
{
	Node *result = NULL;
	Token	t, *_t;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (is_keyword(_t, k_FROM))
	{
		result = is_relation_expr_list(error);
		ON_ERROR_RETURN();

		if (!result)
			RETURN_ERROR();
	}
	else
		push_token(_t);

	return result;
}

static Node *
is_expr_clause(bool *error, KeywordValue req)
{
	Node *result = NULL;
	Token	t, *_t;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (is_keyword(_t, req))
	{
		result = is_expr_top(error);
		ON_ERROR_RETURN();

		if (!result)
			RETURN_ERROR();
	}
	else
		push_token(_t);

	return result;
}

static Node *
is_group_by_clause(bool *error)
{
	Node *result = NULL;
	Token	t, *_t;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (is_keyword(_t, k_GROUP_BY))
	{
		result = is_expr_list(error);
		ON_ERROR_RETURN();

		if (!result)
			RETURN_ERROR();
	}
	else
		push_token(_t);

	return result;
}

/*
 * SELECT labeled_expr_list
 *
 */
static Node *
is_query(bool *error)
{
	Node   *result = NULL;
	Token	t, *_t;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (is_keyword(_t, k_SELECT))
	{
		Node   *cols = is_labeled_expr_list(error);

		ON_ERROR_RETURN();

		result = new_node(n_query);
		result->columns = cols;

		result->from = is_from_clause(error);
		ON_ERROR_RETURN();

		result->where = is_expr_clause(error, k_WHERE);
		ON_ERROR_RETURN();

		result->group_by = is_group_by_clause(error);
		ON_ERROR_RETURN();

		result->having = is_expr_clause(error, k_HAVING);
		ON_ERROR_RETURN();

		result->order_by = is_order_by_clause(error);
		ON_ERROR_RETURN();

		result->limit = is_expr_clause(error, k_LIMIT);
		ON_ERROR_RETURN();

		result->offset = is_expr_clause(error, k_OFFSET);
		ON_ERROR_RETURN();
	}
	else
		push_token(_t);

	return result;
}


static void
debug_display_qident(Node *node)
{
	bool first = true;

	while (node)
	{
		if (!first)
			fprintf(stderr, ".");
		else
			first = false;

		fprintf(stderr, "%.*s", node->bytes, node->str);
		node = node->other;
	}
}

/*
 * For effective work with nodes, we alloc memory in one block
 */
static NodeAllocator *
node_allocator_init_block()
{
	NodeAllocator *na = malloc(sizeof(NodeAllocator));

	if (!na)
		out_of_memory();

	na->size = 1000;		/* 10000 nodes */
	na->nodes = malloc(na->size * sizeof(Node));
	if (!na->nodes)
		out_of_memory();

	na->used = 0;
	memset(na->nodes, 0, na->size * sizeof(Node));

	return na;
}

static void
init_node_allocator()
{
	root_allocator = node_allocator_init_block();
	current_allocator = root_allocator;
}

static Node *
new_node(NodeType type)
{
	Node *result;

	if (current_allocator->used >= current_allocator->size)
	{
		NodeAllocator *n = node_allocator_init_block();

		current_allocator->next = n;
		current_allocator = n;
	}

	result = &current_allocator->nodes[current_allocator->used++];
	result->type = type;
	return result;
}

/******************************************************
 *  Public API
 *
 ******************************************************/
 
/*
 * Returns true, when there are not any syntax error
 *
 */
Node *
parser(char *str, bool force8bit)
{
	bool	error;
	Node   *result = NULL;

	init_lexer(str, force8bit);
	init_node_allocator();

	result = is_query(&error);

	if (!error)
	{
		Token	t, *_t;

		_t = next_token(&t);

		/* ignore last semicolon */
		if (_t && t.type == tt_semicolon)
			_t = next_token(&t);

		if (!(_t && t.type == tt_EOF))
		{
			fprintf(stderr, "syntax error (not on the end)\n");
			return NULL;
		}
	}
	else
	{
		fprintf(stderr, "syntax error (parsing error)\n");
		return NULL;
	}

	return result;
}

void
debug_display_node(Node *node, int indent)
{
	if (!node)
	{
		fprintf(stderr, "%*s%s", indent, "", "** NULL node **\n");
		return;
	}

	fprintf(stderr, "%*s", indent, "");

	if (node->type != n_join && node->type != n_query)
	{
		fprintf(stderr, "%s", node->negate ? "NOT " : "");
		fprintf(stderr, "%s", node->negative ? "-" : "");
	}

	switch (node->type)
	{
		case n_numeric:
		case n_string:
		case n_null:
		case n_false:
		case n_true:
			fprintf(stderr, "%.*s", node->bytes, node->str);
			fprintf(stderr, "\n");
			break;

		case n_ident:
		case n_star:
			debug_display_qident(node);
			fprintf(stderr, "\n");
			break;

		case n_is:
			fprintf(stderr, "IS %.*s\n", node->bytes, node->str);
			debug_display_node(node->value, indent + 4);
			break;

		case n_function:
			debug_display_qident(node->other);
			fprintf(stderr, "(\n");
			debug_display_node(node->value, indent + 4);
			fprintf(stderr, "%*s)\n", indent, "");
			break;

		case n_named_expr:
			fprintf(stderr, "%.*s => \n", node->bytes, node->str);
			debug_display_node(node->value, indent + 4);
			break;

		case n_labeled_expr:
			fprintf(stderr, "%.*s AS\n", node->bytes, node->str);
			debug_display_node(node->value, indent + 4);
			break;

		case n_composite:
			fprintf(stderr, "C(\n");
			debug_display_node(node->value, indent + 4);
			fprintf(stderr, "%*s%s\n", indent, "", ")");
			break;

		case n_list:
			fprintf(stderr, "{\n");
			do
			{
				debug_display_node(node->value, indent + 4);
				node = node->other;
			} while (node);
			fprintf(stderr, "%*s}\n", indent, "");
			break;

		case n_is_null:
		case n_is_not_null:
			fprintf(stderr, "%.*s\n", node->bytes, node->str);
			debug_display_node(node->value, indent + 4);
			break;

		case n_expr:
		case n_logical_and:
		case n_logical_or:
		case n_expr_wrapper:
			fprintf(stderr, "%s", node->parenthesis ? "(" : "");

			if (node->type != n_expr_wrapper)
				fprintf(stderr, "\"%.*s\"\n", node->bytes, node->str);
			else
				fprintf(stderr, "##>\n");

			debug_display_node(node->value, indent + 4);

			if (node->type != n_expr_wrapper)
				debug_display_node(node->other, indent + 4);

			if (node->asc)
				fprintf(stderr, "%*sASC\n", indent, "");
			if (node->desc)
				fprintf(stderr, "%*sDESC\n", indent, "");
			if (node->nulls_first)
				fprintf(stderr, "%*sNULLS FIRST\n", indent, "");
			if (node->nulls_last)
				fprintf(stderr, "%*sNULLS LAST\n", indent, "");
			if (node->parenthesis)
				fprintf(stderr, "%*s)\n", indent, "");
			break;

		case n_query:
			fprintf(stderr, "SELECT\n");
			debug_display_node(node->columns, indent + 4);
			if (node->from)
			{
				fprintf(stderr, "%*s%s", indent, "", "FROM\n");
				debug_display_node(node->from, indent + 4);
			}
			if (node->where)
			{
				fprintf(stderr, "%*s%s", indent, "", "WHERE\n");
				debug_display_node(node->where, indent + 4);
			}
			if (node->group_by)
			{
				fprintf(stderr, "%*s%s", indent, "", "GROUP BY\n");
				debug_display_node(node->group_by, indent + 4);
			}
			if (node->having)
			{
				fprintf(stderr, "%*s%s", indent, "", "HAVING\n");
				debug_display_node(node->having, indent + 4);
			}
			if (node->order_by)
			{
				fprintf(stderr, "%*s%s", indent, "", "ORDER BY\n");
				debug_display_node(node->order_by, indent + 4);
			}
			if (node->limit)
			{
				fprintf(stderr, "%*s%s", indent, "", "LIMIT\n");
				debug_display_node(node->limit, indent + 4);
			}
			if (node->offset)
			{
				fprintf(stderr, "%*s%s", indent, "", "OFFSET\n");
				debug_display_node(node->offset, indent + 4);
			}
			break;

		case n_join:
			if (node->relexpr_parenthesis)
			{
				fprintf(stderr, "(\n");
				indent += 4;
				fprintf(stderr, "%*s", indent, "");
			}

			fprintf(stderr, "%s", node->is_natural ? "NATURAL " : "");
			switch (node->jointype)
			{
				case k_JOIN:
				case k_INNER_JOIN:
					fprintf(stderr, "INNER JOIN\n");
					break;
				case k_CROSS_JOIN:
					fprintf(stderr, "CROSS JOIN\n");
					break;
				case k_LEFT_OUTER_JOIN:
					fprintf(stderr, "LEFT OUTER JOIN\n");
					break;
				case k_RIGHT_OUTER_JOIN:
					fprintf(stderr, "RIGHT OUTER JOIN\n");
					break;
				case k_FULL_OUTER_JOIN:
					fprintf(stderr, "FULL OUTER JOIN\n");
					break;
			}

			debug_display_node(node->left, indent + 4);
			debug_display_node(node->right, indent + 4);

			if (node->onexpr)
			{
				fprintf(stderr, "%*s%s", indent, "", "ON\n");
				debug_display_node(node->onexpr, indent + 4);
			}
			else if (node->using)
			{
				fprintf(stderr, "%*s%s", indent, "", "USING\n");
				debug_display_node(node->using, indent + 4);
			}

			if (node->relexpr_parenthesis)
				fprintf(stderr, "%*s%s", indent - 4, "", ")\n");
			break;

		default:
			fprintf(stderr, "unknown type: %d\n", node->type);
	}
}
