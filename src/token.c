#include "include/token.h"
#include "include/tracked_memory.h"
#include <stdlib.h>

token_t *init_token(int type, char *value){
  token_t *token = tracked_calloc(1, sizeof(struct TOKEN_STRUCT));
  token->type = type;
  token->value = value;
  return token;
}
