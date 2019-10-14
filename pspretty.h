#ifndef PSPRETTY_H

#define PSPRETTY_H

#include <stdbool.h>

typedef enum
{
	tt_EOF = -1,
	tt_unknown = 0,
	tt_keyword = 1,
	tt_comment,
	tt_numeric,
	tt_string,
	tt_lparent,
	tt_rparent,
	tt_lbracket,
	tt_rbracket,
	tt_ident,
	tt_other,
	tt_operator,
	tt_cast_operator,
	tt_dot,
	tt_comma,
	tt_named_expr,
	tt_semicolon
} TokenType;


typedef struct
{
	TokenType type;
	int		lineno;			/* line number from start document */
	char   *line;			/* ptr to line where this token starts */
	int		pos;			/* position from start of line */
	char   *str;			/* pointr to first char of token */
	int		bytes;			/* how much bytes */
	int		value;			/* value of char type token or keyword token */
	bool	quoted;			/* true, when identifier is quoted */
	bool	escaped;		/* true, when string is scaped */
	bool	singleline;		/* true, when comment is singleline */
	bool	reserved;		/* keywords that cannot be used inside expression */
	bool	natural_join;	/* is natural JOIN */
	bool	comparing_op;	/* true, when operator is =, <>, <, >, <= or >= */
} Token;

typedef enum
{
	k_AND = 256,
	k_AS,
	k_ASC,
	k_BETWEEN,
	k_BY,
	k_CROSS,
	k_CROSS_JOIN,
	k_DELETE,
	k_DESC,
	k_EXISTS,
	k_FALSE,
	k_FIRST,
	k_FROM,
	k_FULL,
	k_FULL_OUTER_JOIN,
	k_GROUP,
	k_GROUP_BY,
	k_HAVING,
	k_ILIKE,
	k_IN,
	k_INNER,
	k_INNER_JOIN,
	k_INSERT,
	k_INTO,
	k_IS,
	k_IS_NOT,
	k_IS_NOT_NULL,
	k_IS_NULL,
	k_JOIN,
	k_LAST,
	k_LEFT,
	k_LEFT_OUTER_JOIN,
	k_LIKE,
	k_LIMIT,
	k_NATURAL,
	k_NOT,
	k_NOT_IN,
	k_NULL,
	k_NULLS,
	k_NULLS_FIRST,
	k_NULLS_LAST,
	k_OFFSET,
	k_ON,
	k_OR,
	k_ORDER,
	k_ORDER_BY,
	k_OUTER,
	k_OUTER_JOIN,
	k_RIGHT,
	k_RIGHT_OUTER_JOIN,
	k_SELECT,
	k_TRUE,
	k_UNKNOWN,
	k_USING,
	k_VALUES,
	k_WHERE,
	k_WITH,
} KeywordValue;

typedef enum
{
	n_null,
	n_true,
	n_false,
	n_numeric,
	n_string,
	n_function,
	n_ident,
	n_star,
	n_expr,
	n_expr_wrapper,
	n_named_expr,
	n_labeled_expr,
	n_list,
	n_logical_and,
	n_logical_or,
	n_is_null,
	n_is_not_null,
	n_query,
	n_composite,
	n_is,
	n_join
} NodeType;

typedef enum
{
	expr_generic,
	expr_like,
	expr_ilike,
	expr_between
} SpecialExprType;

typedef struct _node
{
	NodeType	type;
	union {
		struct {
			struct _node *value;
			struct _node *other;
			char   *str;
			int		bytes;
			bool	negative;		/* - expr */
			bool	negate;			/* NOT expr */
			bool	parenthesis;	/* (expr) */
			bool	desc;			/* ORDER BY DESC */
			bool	asc;			/* ORDER BY ASC */
			bool	nulls_first;	/* ORDER BY NULLS FIRST */
			bool	nulls_last;		/* ORDER BY NULLS LAST */
			SpecialExprType exprtype;
		};
		struct {
			struct _node *columns;
			struct _node *from;
			struct _node *where;
			struct _node *group_by;
			struct _node *having;
			struct _node *order_by;
			struct _node *offset;
			struct _node *limit;
		};
		struct {
			struct _node *left;
			struct _node *right;
			struct _node *onexpr;
			struct _node *using;
			KeywordValue jointype;
			bool	is_natural;
			bool	relexpr_parenthesis;
		};
	};
} Node;

typedef struct _nodeAllocator
{
	Node	   *nodes;
	int			size;
	int			used;
	struct _nodeAllocator *next;
} NodeAllocator;

extern void init_lexer(char *str, bool _force8bit);
extern Token *next_token(Token *token);
extern void push_token(Token *token);
extern void debug_print_token(Token *token);
extern void push_token_debug(Token *token, char *str);

extern Node *parser(char *str, bool force8bit);
extern void out_of_memory();

extern void debug_display_node(Node *node, int indent);

#endif
