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
	tt_comma,
	tt_named_expr
} TokenType;


typedef struct
{
	TokenType	type;
	int			lineno;		/* line number from start document */
	char	   *line;		/* ptr to line where this token starts */
	int			pos;		/* position from start of line */
	char	   *str;		/* pointr to first char of token */
	int			bytes;		/* how much bytes */
	int			value;		/* value of char type token or keyword token */
	bool		quoted;		/* true, when identifier is quoted */
	bool		escaped;	/* true, when string is scaped */
	bool		singleline;	/* true, when comment is singleline */
	bool		reserved;	/* keywords that cannot be used inside expression */
} Token;

typedef enum
{
	k_AND = 256,
	k_AS,
	k_ASC,
	k_BY,
	k_DELETE,
	k_DESC,
	k_FROM,
	k_GROUP,
	k_GROUP_BY,
	k_HAVING,
	k_INSERT,
	k_INTO,
	k_LIMIT,
	k_OR,
	k_ORDER,
	k_ORDER_BY,
	k_SELECT,
	k_VALUES,
	k_WHERE,
	k_WITH,
} KeywordValue;

typedef enum
{
	n_numeric,
	n_string,
	n_function,
	n_ident,
	n_star,
	n_expr,
	n_named_expr,
	n_labeled_expr,
	n_list
} NodeType;

typedef struct _node
{
	NodeType	type;
	struct _node *value;
	struct _node *other;
	char   *str;
	int		bytes;
	bool	negative;
	bool	parenthesis;
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
extern Node *parser(char *str, bool force8bit);
extern void out_of_memory();

extern void debug_display_node(Node *node, int indent);

#endif
