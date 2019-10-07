#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pspretty.h"

static Node * is_expr_in_parenthesis(bool *error);
static Node * is_function_args(bool *error);
static Node * is_query(bool *error);
static Node * is_expr_list(bool *error);


static Node * is_operand(bool *error);


static Node * new_node();


#define	ON_ERROR_RETURN()			do { if (*error) { return NULL;} } while (0)
#define	ON_EMPTY_RETURN_ERROR()		do { if (!_t) { *error = 1; return NULL; }} while (0)
#define	RETURN_ERROR()				do { *error = 1; return NULL; } while (0)

NodeAllocator	   *root_allocator;
NodeAllocator	   *current_allocator;


static Node *
new_node_value(NodeType type, Node *value)
{
	Node *result = new_node();

	result->type = type;
	result->value = value;

	return result;
}

static Node *
new_node_str(NodeType type, Token *token)
{
	Node *result = new_node();

	result->type = type;
	result->str = token->str;
	result->bytes = token->bytes;

	return result;
}

static bool
is_keyword(Token *token, KeywordValue k)
{
	return token->type == tt_keyword && token->value == k;
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

	if (t.type == tt_ident || (t.type == tt_keyword && !t.reserved))
	{
		Token	t2, *_t2;
		Node	*_node;
		bool	revert = false;

		_t2 = next_token(&t2);
		ON_EMPTY_RETURN_ERROR();

		if (t2.type == tt_other && t2.value == '.')
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

	if (t.type == tt_operator && t.bytes == 1 && strncmp(t.str, "*", 1) == 0)
	{
		return new_node_str(n_star, _t);
	}
	else if (t.type == tt_ident || (t.type == tt_keyword && !t.reserved))
	{
		Token	t2, *_t2;

		_t2 = next_token(&t2);
		ON_EMPTY_RETURN_ERROR();

		if (t2.type == tt_other && t2.value == '.')
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

	if (_t->type == tt_operator && 
		((strncmp(t.str, "+", 1) == 0 || strncmp(t.str, "-", 1) == 0)))
	{
		Node	*result;

		result = is_operand(error);
		ON_ERROR_RETURN();

		if (result)
		{
			if (strncmp(t.str, "-", 1) == 0)
				result->negative = !result->negative;
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

	if (result = is_negated_operand(error))
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
	else if (t.type == tt_numeric)
		return new_node_str(n_numeric, _t);
	else if (t.type == tt_string)
		return new_node_str(n_string, _t);
	else if (t.type == tt_keyword && !t.reserved)
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

		if (t.type == tt_operator)
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
 * Add expr IS NULL, expr IS NOT NULL
 *
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

		if (is_keyword(_t, k_IS_NULL) ||
			is_keyword(_t, k_IS_NOT_NULL))
		{
			Node   *expr = new_node_str(is_keyword(_t, k_IS_NULL) ? n_is_null : n_is_not_null, _t);

			expr->value = result;
			return expr;

			ON_ERROR_RETURN();
			RETURN_ERROR();
		}

		push_token(_t);
		return result;
	}
}

/*
 * Add operator AND
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

		if (is_keyword(_t, k_AND))
		{
			Node   *expr = new_node_str(n_logical_and, _t);

			expr->value = result;
			if (expr->other = is_expr_02(error))
				return expr;

			ON_ERROR_RETURN();
			RETURN_ERROR();
		}

		push_token(_t);
		return result;
	}
}

/*
 * Add operator OR
 *
 */
static Node *
is_expr_03(bool *error)
{
	Node	*result;

	if (result = is_expr_02(error))
	{
		Token	t, *_t;

		ON_ERROR_RETURN();

		_t = next_token(&t);
		ON_EMPTY_RETURN_ERROR();

		if (is_keyword(_t, k_OR))
		{
			Node   *expr = new_node_str(n_logical_or, _t);

			expr->value = result;
			if (expr->other = is_expr_03(error))
				return expr;

			ON_ERROR_RETURN();
			RETURN_ERROR();
		}

		push_token(_t);
		return result;
	}
}

#define	is_expr_top(e)		is_expr_03(e)


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

		if (t2.type == tt_ident ||
				(t2.type == tt_keyword && !t2.reserved))
			return new_node_str(n_labeled_expr, _t2);

		RETURN_ERROR();
	}

	if (t.type == tt_ident ||
			(t.type == tt_keyword && !t.reserved))
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
 * SELECT labeled_expr_list
 *
 */
static Node *
is_query(bool *error)
{
	Token	t, *_t;

	_t = next_token(&t);
	ON_EMPTY_RETURN_ERROR();

	if (is_keyword(_t, k_SELECT))
	{
		Node   *cols = is_labeled_expr_list(error);
		Node   *result;

		ON_ERROR_RETURN();

		result = new_node();
		result->type = n_query;
		result->columns = cols;

		return result;
	}

	push_token(_t);
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
new_node()
{
	Node *result;

	if (current_allocator->used >= current_allocator->size)
	{
		NodeAllocator *n = node_allocator_init_block();

		current_allocator->next = n;
		current_allocator = n;
	}

	result = &current_allocator->nodes[current_allocator->used++];
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
		fprintf(stderr, "%*s%s", indent, "", "node is null\n");
		return;
	}

	fprintf(stderr, "%*s", indent, "");
	fprintf(stderr, "%s", node->negate ? "NOT " : "");
	fprintf(stderr, "%s", node->negative ? "-" : "");

	switch (node->type)
	{
		case n_numeric:
		case n_string:
		case n_null:
			fprintf(stderr, "%.*s", node->bytes, node->str);
			break;

		case n_ident:
		case n_star:
			debug_display_qident(node);
			break;

		case n_function:
			debug_display_qident(node->other);
			fprintf(stderr, "(\n");
			debug_display_node(node->value, indent + 4);
			fprintf(stderr, "%*s)", indent, "");
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
			fprintf(stderr, "%*s%s", indent, "", ")");
			break;

		case n_list:
			fprintf(stderr, "{\n");
			do
			{
				debug_display_node(node->value, indent + 4);
				node = node->other;
			} while (node);
			fprintf(stderr, "%*s}", indent, "");
			break;

		case n_is_null:
		case n_is_not_null:
			fprintf(stderr, "%.*s\n", node->bytes, node->str);
			debug_display_node(node->value, indent + 4);
			break;

		case n_expr:
		case n_logical_and:
		case n_logical_or:
			fprintf(stderr, "%s", node->parenthesis ? "(" : "");
			fprintf(stderr, "\"%.*s\"\n", node->bytes, node->str);
			debug_display_node(node->value, indent + 4);
			debug_display_node(node->other, indent + 4);
			fprintf(stderr, "%*s%s", indent, "", node->parenthesis ? ")" : "");
			break;

		case n_query:
			fprintf(stderr, "*** QUERY ***\n");
			debug_display_node(node->columns, indent + 4);
			debug_display_node(node->from, indent + 4);
			fprintf(stderr, "%*s%s", indent, "", "*** query ***");

			break;

		default:
			fprintf(stderr, "unknown type: %d\n", node->type);
	}

	fprintf(stderr, "\n");
}
