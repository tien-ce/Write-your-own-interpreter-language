#include "include/AST.h"
#include "include/debug.h"
#include <stdio.h>
#include <stdbool.h>

#define MAX_DEPTH 256

static void print_node_label(ast_t *node)
{
  if (!node)
  {
    printf("(NULL)\n");
    return;
  }

  switch (node->type)
  {
    case AST_INT_LITERAL:
      printf("INT: %d\n", node->value.int_value);
      break;
    case AST_FLOAT_LITERAL:
      printf("FLOAT: %f\n", node->value.float_value);
      break;
    case AST_STRING_LITERAL:
      printf("STRING: \"%s\"\n", node->value.string_value);
      break;
    case AST_BOOLEAN:
      printf("BOOL: %s\n", node->value.bool_value ? "true" : "false");
      break;
    case AST_IDENTIFIER:
      printf("ID: %s\n", node->value.identifier);
      break;
    case AST_BINARY_EXPR:
      printf("BIN_OP: (%s)\n", binary_op_to_str(node->value.binary_expr.op));
      break;
    case AST_UNARY_EXPR:
      printf("UNARY_OP: (%s)\n", unary_op_to_str(node->value.unary_expr.op));
      break;
    case AST_ARRAY_ACCESS:
      printf("ARRAY_ACCESS: %s[]\n", node->value.array_access.id);
      break;
    case AST_VARIABLE_DEFINITION:
      printf("VAR_DEF: %s %s\n",
             var_type_to_str(node->value.variable_definition.variable_type),
             node->value.variable_definition.variable_name);
      break;
    case AST_ASSIGNMENT:
      printf("ASSIGNMENT (=)\n");
      break;
    case AST_WHILE_STATEMENT:
      printf("WHILE\n");
      break;
    case AST_FUNCTION_CALL:
      printf("CALL\n");
      break;
    case AST_COMPOUND:
      printf("COMPOUND (%d stmts)\n", node->value.compound.compound_size);
      break;
    default:
      printf("%s\n", ast_type_to_str(node->type));
      break;
  }
}

static void draw_ast_internal(ast_t *node, bool is_last, bool is_root, bool has_next_sibling[], int depth)
{
  if (!node)
    return;

  // Print vertical guide lines for previous levels
  if (!is_root)
  {
    for (int i = 0; i < depth - 1; i++)
    {
      if (has_next_sibling[i])
        printf("│   ");
      else
        printf("    ");
    }

    // Print branch connector for current level
    if (is_last)
      printf("└── ");
    else
      printf("├── ");
  }

  // Print the current node's info
  print_node_label(node);

  // Mark whether the current level still has siblings below it
  has_next_sibling[depth - 1] = !is_last;

  // Dispatch and render children
  switch (node->type)
  {
    case AST_BINARY_EXPR:
      draw_ast_internal(node->value.binary_expr.left, false, false, has_next_sibling, depth + 1);
      draw_ast_internal(node->value.binary_expr.right, true, false, has_next_sibling, depth + 1);
      break;

    case AST_UNARY_EXPR:
      draw_ast_internal(node->value.unary_expr.operand, true, false, has_next_sibling, depth + 1);
      break;

    case AST_VARIABLE_DEFINITION:
      draw_ast_internal(node->value.variable_definition.value, true, false, has_next_sibling, depth + 1);
      break;

    case AST_ASSIGNMENT:
      draw_ast_internal(node->value.assignment.id, false, false, has_next_sibling, depth + 1);
      draw_ast_internal(node->value.assignment.value, true, false, has_next_sibling, depth + 1);
      break;

    case AST_ARRAY_ACCESS:
      draw_ast_internal(node->value.array_access.index_expr, true, false, has_next_sibling, depth + 1);
      break;

    case AST_WHILE_STATEMENT:
      draw_ast_internal(node->value.while_statement.condition, false, false, has_next_sibling, depth + 1);
      draw_ast_internal(node->value.while_statement.body, true, false, has_next_sibling, depth + 1);
      break;

    case AST_FUNCTION_CALL:
    {
      int total_children = 1 + node->value.function_call.num_arg;
      // Draw Callee
      draw_ast_internal(node->value.function_call.func, total_children == 1, false, has_next_sibling, depth + 1);
      // Draw Arguments
      for (int i = 0; i < node->value.function_call.num_arg; i++)
      {
        bool last_child = (i == node->value.function_call.num_arg - 1);
        draw_ast_internal(node->value.function_call.args[i], last_child, false, has_next_sibling, depth + 1);
      }
      break;
    }

    case AST_COMPOUND:
    {
      for (int i = 0; i < node->value.compound.compound_size; i++)
      {
        bool last_child = (i == node->value.compound.compound_size - 1);
        draw_ast_internal(node->value.compound.compound_value[i], last_child, false, has_next_sibling, depth + 1);
      }
      break;
    }

    default:
      // Leaf nodes (LITERALS, IDENTIFIERS) have no children
      break;
  }
}

void ast_draw(ast_t *root)
{
  bool has_next_sibling[MAX_DEPTH] = {false};
  draw_ast_internal(root, true, true, has_next_sibling, 0);
}
