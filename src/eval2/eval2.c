// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Generated Code

#include "eval2/generated/eval2.meta.c"

////////////////////////////////
//~ rjf: Space -> Memory Map Helpers

internal void
e2_space_map_push(Arena *arena, E2_SpaceMap *map, E2_SpaceID space_id, Rng1U64 addr_range, void *data)
{
  if(map->slots_count == 0)
  {
    map->slots_count = 8;
    map->slots = push_array(arena, E2_SpaceMapNode *, map->slots_count);
  }
  U64 hash = u64_hash_from_str8(str8_struct(&space_id));
  U64 slot_idx = hash%map->slots_count;
  E2_SpaceMapNode *node = 0;
  for(E2_SpaceMapNode *n = map->slots[slot_idx]; n != 0; n = n->next)
  {
    if(n->space_id == space_id)
    {
      node = n;
      break;
    }
  }
  if(node == 0)
  {
    node = push_array(arena, E2_SpaceMapNode, 1);
    SLLStackPush(map->slots[slot_idx], node);
  }
  memory_map_push(arena, &node->memory_map, addr_range, data);
}

internal U64
e2_space_map_read(E2_SpaceMap *map, E2_SpaceID space_id, Rng1U64 addr_range, void *out)
{
  U64 result = 0;
  {
    U64 hash = u64_hash_from_str8(str8_struct(&space_id));
    U64 slot_idx = hash%map->slots_count;
    E2_SpaceMapNode *node = 0;
    for(E2_SpaceMapNode *n = map->slots[slot_idx]; n != 0; n = n->next)
    {
      if(n->space_id == space_id)
      {
        node = n;
        break;
      }
    }
    if(node != 0)
    {
      result = memory_map_read(&node->memory_map, addr_range, out);
    }
  }
  return result;
}

////////////////////////////////
//~ rjf: Name -> Expr Map Helpers

internal void
e2_expr_map_push(Arena *arena, E2_ExprMap *map, String8 name, E2_Expr *expr)
{
  if(map->slots_count == 0)
  {
    map->slots_count = 8;
    map->slots = push_array(arena, E2_ExprMapNode *, map->slots_count);
  }
  U64 hash = u64_hash_from_str8(name);
  U64 slot_idx = hash%map->slots_count;
  E2_ExprMapNode *node = 0;
  for(E2_ExprMapNode *n = map->slots[slot_idx]; n != 0; n = n->next)
  {
    if(str8_match(n->name, name, 0))
    {
      node = n;
      break;
    }
  }
  if(node == 0)
  {
    node = push_array(arena, E2_ExprMapNode, 1);
    node->name = str8_copy(arena, name);
    node->expr = expr;
    SLLStackPush(map->slots[slot_idx], node);
  }
}

internal E2_Expr *
e2_expr_from_name(E2_ExprMap *map, String8 name)
{
  E2_Expr *expr = &e2_expr_nil;
  U64 hash = u64_hash_from_str8(name);
  U64 slot_idx = hash%map->slots_count;
  E2_ExprMapNode *node = 0;
  for(E2_ExprMapNode *n = map->slots[slot_idx]; n != 0; n = n->next)
  {
    if(str8_match(n->name, name, 0))
    {
      node = n;
      break;
    }
  }
  if(node != 0)
  {
    expr = node->expr;
  }
  return expr;
}

////////////////////////////////
//~ rjf: Messages

internal E2_Msg *
e2_msg(Arena *arena, E2_MsgList *msgs, Rng1U64 src_range, String8 string)
{
  E2_Msg *msg = push_array(arena, E2_Msg, 1);
  SLLQueuePush(msgs->first, msgs->last, msg);
  msgs->count += 1;
  msg->src_range = src_range;
  msg->string = str8_copy(arena, string);
  return msg;
}

internal E2_Msg *
e2_msgf(Arena *arena, E2_MsgList *msgs, Rng1U64 src_range, char *fmt, ...)
{
  Temp scratch = scratch_begin(&arena, 1);
  va_list args;
  va_start(args, fmt);
  String8 string = str8fv(scratch.arena, fmt, args);
  E2_Msg *msg = e2_msg(arena, msgs, src_range, string);
  va_end(args);
  scratch_end(scratch);
  return msg;
}

////////////////////////////////
//~ rjf: Types

internal E2_TypeKey
e2_type_key_basic(E2_TypeKind kind)
{
  E2_TypeKey key = {E2_TypeKeyKind_Basic};
  key.u32[0] = (U32)kind;
  return key;
}

////////////////////////////////
//~ rjf: String -> Expression

internal E2_Token
e2_token_from_string_off(String8 string, U64 start_off)
{
  E2_Token token = {E2_TokenKind_Null};
  B32 identifier_tick_mode = 0;
  B32 comment_is_explicit_ender = 0;
  B32 numeric_exponent = 0;
  B32 escaped = 0;
  B32 done = 0;
  for(U64 off = start_off; !done && off <= string.size; off += 1)
  {
    U8 next_byte = (off < string.size ? string.str[off] : 0);
    U8 next2_byte = (off+1 < string.size ? string.str[off+1] : 0);
    U8 next3_byte = (off+2 < string.size ? string.str[off+2] : 0);
    switch(token.kind)
    {
      //- rjf: no set token kind => look for starters
      default:
      {
        if(next_byte == 0)
        {
          done = 1;
        }
        else if(next_byte <= 32)
        {
          token.kind = E2_TokenKind_Whitespace;
          token.range.min = token.range.max = off;
        }
        else if(next_byte == '$' || next_byte == '_' || next_byte == '`' ||
                ('A' <= next_byte && next_byte <= 'Z') ||
                ('a' <= next_byte && next_byte <= 'z'))
        {
          token.kind = E2_TokenKind_Identifier;
          token.range.min = token.range.max = off;
          identifier_tick_mode = (next_byte == '`');
        }
        else if(('0' <= next_byte && next_byte <= '9') ||
                (next_byte == '.' && '0' <= next2_byte && next2_byte <= '9'))
        {
          token.kind = E2_TokenKind_Numeric;
          token.range.min = token.range.max = off;
        }
        else if(next_byte == '\'')
        {
          token.kind = E2_TokenKind_CharLiteral;
          token.range.min = token.range.max = off;
        }
        else if(next_byte == '"')
        {
          token.kind = E2_TokenKind_StringLiteral;
          token.range.min = token.range.max = off;
        }
        else if(next_byte == '/' && next2_byte == '/')
        {
          token.kind = E2_TokenKind_Comment;
          token.range.min = token.range.max = off;
        }
        else if(next_byte == '/' && next2_byte == '*')
        {
          token.kind = E2_TokenKind_Comment;
          token.range.min = token.range.max = off;
          comment_is_explicit_ender = 1;
        }
        else if(next_byte == '~' || next_byte == '!' || next_byte == '%' ||
                next_byte == '^' || next_byte == '&' || next_byte == '*' ||
                next_byte == '(' || next_byte == ')' || next_byte == '[' ||
                next_byte == ']' || next_byte == '{' || next_byte == '}' ||
                next_byte == '-' || next_byte == '=' || next_byte == '+' ||
                next_byte == ':' || next_byte == ';' || next_byte == ',' ||
                next_byte == '.' || next_byte == '<' || next_byte == '>' ||
                next_byte == '/' || next_byte == '?' || next_byte == '#' ||
                next_byte == '@' || next_byte == '|')
        {
          token.kind = E2_TokenKind_Symbol;
          token.range.min = token.range.max = off;
        }
      }break;
      
      //- rjf: active tokens -> seek enders
      case E2_TokenKind_Whitespace:
      if(next_byte > 32)
      {
        done = 1;
      }break;
      case E2_TokenKind_Comment:
      if(comment_is_explicit_ender && next_byte == '*' && next2_byte == '/')
      {
        done = 1;
        token.range.max = off+2;
      }break;
      case E2_TokenKind_Identifier:
      {
        if(identifier_tick_mode && (next_byte == '`' || next_byte == '\''))
        {
          identifier_tick_mode = 0;
        }
        else if(!identifier_tick_mode &&
                (next_byte < 'a' || 'z' < next_byte) &&
                (next_byte < 'A' || 'Z' < next_byte) &&
                (next_byte < '0' || '9' < next_byte) &&
                next_byte != '$' &&
                next_byte != '_' &&
                next_byte != '@')
        {
          done = 1;
          token.range.max = off;
        }
      }break;
      case E2_TokenKind_Numeric:
      {
        if(numeric_exponent && (next_byte == '+' || next_byte == '-')){}
        else if((next_byte < 'a' || 'z' < next_byte) &&
                (next_byte < 'A' || 'Z' < next_byte) &&
                (next_byte < '0' || '9' < next_byte) &&
                next_byte != '.' &&
                next_byte != ':')
        {
          done = 1;
          token.range.max = off;
        }
        else
        {
          numeric_exponent = (next_byte == 'e');
        }
      }break;
      case E2_TokenKind_Symbol:
      if(next_byte != '~' && next_byte != '!' && next_byte != '%' &&
         next_byte != '^' && next_byte != '&' && next_byte != '*' &&
         next_byte != '(' && next_byte != ')' && next_byte != '[' &&
         next_byte != ']' && next_byte != '{' && next_byte != '}' &&
         next_byte != '-' && next_byte != '=' && next_byte != '+' &&
         next_byte != ':' && next_byte != ';' && next_byte != ',' &&
         next_byte != '.' && next_byte != '<' && next_byte != '>' &&
         next_byte != '/' && next_byte != '?' && next_byte != '#' &&
         next_byte != '@' && next_byte != '|')
      {
        done = 1;
        token.range.max = off;
      }break;
      case E2_TokenKind_CharLiteral:
      case E2_TokenKind_StringLiteral:
      {
        if(!escaped && next_byte == '\\')
        {
          escaped = 1;
        }
        else if(escaped)
        {
          escaped = 0;
        }
        else if(!escaped && ((next_byte == '"' && token.kind == E2_TokenKind_StringLiteral) ||
                             (next_byte == '\'' && token.kind == E2_TokenKind_CharLiteral)))
        {
          done = 1;
          token.range.max = off+1;
        }
      }break;
    }
    if(token.range.max > off+1)
    {
      off += (token.range.max - (off+1));
    }
    if(!done && off == string.size)
    {
      done = 1;
      token.range.max = off;
    }
  }
  return token;
}

internal U64
e2_read_token(String8 string, U64 off, E2_Token *token_out)
{
  E2_Token token = e2_token_from_string_off(string, off);
  if(token_out != 0)
  {
    token_out[0] = token;
  }
  U64 result = dim_1u64(token.range);
  return result;
}

internal B32
e2_try_token(String8 string, E2_TokenKind kind, String8 expected_string, U64 *off_out, E2_Token *token_out)
{
  B32 result = 0;
  U64 off = off_out[0];
  for(;;)
  {
    E2_Token next_token = {E2_TokenKind_Null};
    off += e2_read_token(string, off, &next_token);
    if(next_token.kind == kind && (expected_string.size == 0 || str8_match(str8_substr(string, next_token.range), expected_string, 0)))
    {
      result = 1;
      if(token_out != 0)
      {
        token_out[0] = next_token;
      }
      break;
    }
    else if(next_token.kind != E2_TokenKind_Comment && next_token.kind != E2_TokenKind_Whitespace)
    {
      break;
    }
  }
  if(result && off_out != 0)
  {
    off_out[0] = off;
  }
  return result;
}

internal E2_Parse
e2_parse_from_string(Arena *arena, E2_ParseState *state, E2_SpaceMap *space_map, E2_ExprMap *expr_map, String8 string)
{
  U64 off = state->string_off;
  E2_Parse parse = {E2_Status_Error, .expr = &e2_expr_nil, .access_expr = &e2_expr_nil};
  
  //- rjf: parse & attach to top parsing task - if we don't have a top task
  // then it is just the result
  for(B32 done = 0; !done;)
  {
    S64 max_precedence = state->top_task ? state->top_task->max_precedence : max_S64;
    E2_Expr *expr = &e2_expr_nil;
    E2_Token token = {0};
    
    //- rjf: nested sub-expressions
    if(e2_try_token(string, E2_TokenKind_Symbol, s("("), &off, 0))
    {
      E2_ParseTask *task = state->free_task;
      if(task != 0)
      {
        SLLStackPop(state->free_task);
      }
      else
      {
        task = push_array(arena, E2_ParseTask, 1);
      }
      task->parent = push_array(arena, E2_Expr, 1);
      MemoryCopyStruct(task->parent, &e2_expr_nil);
      task->expected_closer = s(")");
      task->child_count_target = 1;
      task->max_precedence = max_S64;
      SLLStackPush(state->top_task, task);
    }
    
    //- rjf: prefix unaries
    else if(e2_try_token(string, E2_TokenKind_Symbol, s(""), &off, &token))
    {
      // rjf: string -> operator kind
      E2_OpKind op_kind = E2_OpKind_Null;
      {
        String8 token_string = str8_substr(string, token.range);
        for EachNonZeroEnumVal(E2_OpKind, k)
        {
          if(e2_op_kind_info_table[k].parse_kind == E2_OpParseKind_UnaryPrefix &&
             e2_op_kind_info_table[k].precedence <= max_precedence &&
             str8_match(token_string, e2_op_kind_info_table[k].pre, 0))
          {
            op_kind = k;
            break;
          }
        }
      }
      
      // rjf: push task for operand
      if(op_kind != E2_OpKind_Null)
      {
        E2_ParseTask *task = state->free_task;
        if(task != 0)
        {
          SLLStackPop(state->free_task);
        }
        else
        {
          task = push_array(arena, E2_ParseTask, 1);
        }
        task->parent = push_array(arena, E2_Expr, 1);
        MemoryCopyStruct(task->parent, &e2_expr_nil);
        task->child_count_target = 1;
        task->op_kind = op_kind;
        task->max_precedence = e2_op_kind_info_table[op_kind].precedence;
        SLLStackPush(state->top_task, task);
      }
    }
    
    //- rjf: leaf identifiers
    else if(e2_try_token(string, E2_TokenKind_Identifier, s(""), &off, &token))
    {
      String8 identifier = str8_substr(string, token.range);
      expr = e2_expr_from_name(expr_map, identifier);
      if(expr == &e2_expr_nil)
      {
        done = 1;
        parse.status = E2_Status_MissedIdentifierResolution;
        parse.missed_identifier = identifier;
      }
    }
    
    //- rjf: leaf numerics
    else if(e2_try_token(string, E2_TokenKind_Numeric, s(""), &off, &token))
    {
      String8 numeric_string = str8_substr(string, token.range);
      expr = push_array(arena, E2_Expr, 1);
      MemoryCopyStruct(expr, &e2_expr_nil);
      U64 dot_pos = str8_find_needle(numeric_string, 0, s("."), 0); 
      B32 f_suffix = str8_match(str8_postfix(numeric_string, 1), s("f"), 0);
      U64 colon_pos = str8_find_needle(numeric_string, 0, s(":"), 0);
      if(dot_pos < numeric_string.size && f_suffix)
      {
        expr->val.f32 = (F32)f64_from_str8(numeric_string);
        expr->type_key = e2_type_key_basic(E2_TypeKind_F32);
      }
      else if(dot_pos < numeric_string.size && !f_suffix)
      {
        expr->val.f64 = f64_from_str8(numeric_string);
        expr->type_key = e2_type_key_basic(E2_TypeKind_F64);
      }
      else if(colon_pos < numeric_string.size)
      {
        Temp scratch = scratch_begin(&arena, 1);
        String8List parts = str8_split(scratch.arena, numeric_string, (U8 *)":", 1, 0);
        U64 u64_idx = 0;
        for EachNode(n, String8Node, parts.first)
        {
          if(u64_idx >= ArrayCount(expr->val.u512.u64))
          {
            break;
          }
          try_u64_from_str8_c_rules(n->string, &expr->val.u512.u64[u64_idx]);
          u64_idx += 1;
        }
        switch(u64_idx)
        {
          case 1:{expr->op = RDI_EvalOp_ConstU64; expr->type_key = e2_type_key_basic(E2_TypeKind_U64);}break;
          case 2:{expr->op = RDI_EvalOp_ConstU128; expr->type_key = e2_type_key_basic(E2_TypeKind_U128);}break;
          case 4:{expr->op = RDI_EvalOp_ConstU256; expr->type_key = e2_type_key_basic(E2_TypeKind_U256);}break;
          case 8:{expr->op = RDI_EvalOp_ConstU512; expr->type_key = e2_type_key_basic(E2_TypeKind_U512);}break;
          default:
          {
            e2_msgf(arena, &parse.msgs, token.range, "Invalid number of numeric portions specified (%I64u; must be 2, 4, or 8).", u64_idx);
          }break;
        }
        scratch_end(scratch);
      }
      else if(try_u64_from_str8_c_rules(numeric_string, &expr->val.u64))
      {
        if(expr->val.u64 <= 0xff)
        {
          expr->op = RDI_EvalOp_ConstU8;
          expr->type_key = e2_type_key_basic(E2_TypeKind_U8);
        }
        else if(expr->val.u64 <= 0xffff)
        {
          expr->op = RDI_EvalOp_ConstU16;
          expr->type_key = e2_type_key_basic(E2_TypeKind_U16);
        }
        else if(expr->val.u64 <= 0xffffffff)
        {
          expr->op = RDI_EvalOp_ConstU32;
          expr->type_key = e2_type_key_basic(E2_TypeKind_U32);
        }
        else
        {
          expr->op = RDI_EvalOp_ConstU64;
          expr->type_key = e2_type_key_basic(E2_TypeKind_U64);
        }
      }
    }
    
    //- rjf: extend parsed expression tree with trailing binary operators
    if(expr != &e2_expr_nil)
    {
      U64 trailing_symbol_off = off;
      if(e2_try_token(string, E2_TokenKind_Symbol, s(""), &trailing_symbol_off, &token))
      {
        // rjf: token string -> operator kind
        E2_OpKind op_kind = E2_OpKind_Null;
        {
          String8 token_string = str8_substr(string, token.range);
          for EachNonZeroEnumVal(E2_OpKind, k)
          {
            if(e2_op_kind_info_table[k].parse_kind == E2_OpParseKind_Binary &&
               e2_op_kind_info_table[k].precedence <= max_precedence &&
               str8_match(token_string, e2_op_kind_info_table[k].pre, 0))
            {
              op_kind = k;
              break;
            }
          }
        }
        
        // rjf: non-null operator kind -> build binary expression node, push task to fill other child
        if(op_kind != E2_OpKind_Null)
        {
          E2_ParseTask *task = state->free_task;
          if(task != 0)
          {
            SLLStackPop(state->free_task);
          }
          else
          {
            task = push_array(arena, E2_ParseTask, 1);
          }
          task->parent = push_array(arena, E2_Expr, 1);
          MemoryCopyStruct(task->parent, &e2_expr_nil);
          SLLQueuePush_NZ(&e2_expr_nil, task->parent->first, task->parent->last, expr, next);
          task->child_count = 1;
          task->child_count_target = 2;
          task->op_kind = op_kind;
          task->max_precedence = e2_op_kind_info_table[op_kind].precedence;
          SLLStackPush(state->top_task, task);
        }
      }
    }
    
    //- rjf: attach formed expressions to parent task exprs; pop tasks when they're done.
    // if we have no parent task, then we just fill the result, and we're done with the parse.
    if(expr != &e2_expr_nil)
    {
      //- rjf: pop finished expressions
      E2_Expr *finished_expr = expr;
      for(;state->top_task != 0;)
      {
        //- rjf: add finished expression to current parent; increase task child count
        E2_Expr *finished_expr_parent = state->top_task->parent;
        SLLQueuePush_NZ(&e2_expr_nil, finished_expr_parent->first, finished_expr_parent->last, finished_expr, next);
        state->top_task->child_count += 1;
        
        //- rjf: task child count hits limit -> complete this child
        if(state->top_task->child_count >= state->top_task->child_count_target)
        {
          //- rjf: release task; iterate to this finished expression next
          E2_ParseTask *completed_task = state->top_task;
          SLLStackPop(state->top_task);
          SLLStackPush(state->free_task, completed_task);
          
          //- rjf: finish expression with operator info, if applicable
          {
            E2_Expr *root = completed_task->parent;
            switch(completed_task->op_kind)
            {
              default:{}break;
              
              //- rjf: dereference
              case E2_OpKind_Deref:
              {
                // rjf: unpack operand
                E2_Expr *rhs = root->first;
                // E2_TypeKey rhs_type_key = e2_type_key_unwrap(rhs->type_key, E2_TypeUnwrapFlag_AllDecorative & ~E2_TypeUnwrapFlag_Enums);
                // E2_TypeKind rhs_type_kind = e2_type_kind_from_key(rhs_type_key);
                // E2_TypeKey dereferenced_type = e2_type_key_unwrap(rhs->type_key, E2_TypeUnwrapFlag_All & ~E2_TypeUnwrapFlag_Enums);
                // U64 dereferenced_type_size = e2_byte_size_from_type_key(dereferenced_type);
                
                // rjf: collect info about malformed situations
                B32 malformed = 0;
                // if(dereferenced_type_size == 0 && e2_type_kind_is_ptr_or_ref(rhs_type_kind))
                {
                  malformed = 1;
                  // e2_msgf(arena, &parse.msgs, src->root_range, "");
                }
              }break;
              
              case E2_OpKind_Address:{}break;
              case E2_OpKind_Pos:{}break;
              case E2_OpKind_Neg:{}break;
              case E2_OpKind_LogNot:{}break;
              case E2_OpKind_BitNot:{}break;
              case E2_OpKind_Mul:{}break;
              case E2_OpKind_Div:{}break;
              case E2_OpKind_Mod:{}break;
              case E2_OpKind_Add:{}break;
              case E2_OpKind_Sub:{}break;
              case E2_OpKind_LShift:{}break;
              case E2_OpKind_RShift:{}break;
              case E2_OpKind_Less:{}break;
              case E2_OpKind_LtEq:{}break;
              case E2_OpKind_Grtr:{}break;
              case E2_OpKind_GrEq:{}break;
              case E2_OpKind_EqEq:{}break;
              case E2_OpKind_NtEq:{}break;
              case E2_OpKind_BitAnd:{}break;
              case E2_OpKind_BitXor:{}break;
              case E2_OpKind_BitOr:{}break;
              case E2_OpKind_LogAnd:{}break;
              case E2_OpKind_LogOr:{}break;
              case E2_OpKind_Define:{}break;
            }
          }
          
          //- rjf: iterate to the completed task's expression node next, to try & finish it for *its* parent
          finished_expr = completed_task->parent;
        }
      }
      
      //- rjf: for the last finished expression, if we do not have any further tasks,
      // this is our result.
      if(finished_expr != &e2_expr_nil && state->top_task == 0)
      {
        parse.expr = finished_expr;
        if(parse.status == E2_Status_Error)
        {
          parse.status = E2_Status_Good;
        }
        break;
      }
    }
  }
  
  //- rjf: advance offset if successful
  if(parse.status == E2_Status_Good)
  {
    state->string_off = off;
  }
  return parse;
}

////////////////////////////////
//~ rjf: Expression -> Bytecode

internal String8
e2_bytecode_from_expr(Arena *arena, E2_Expr *expr)
{
  
}

////////////////////////////////
//~ rjf: Bytecode -> Result

internal E2_Interp
e2_interp_from_bytecode(Arena *arena, E2_InterpState *state, E2_SpaceMap *space_map, String8 bytecode)
{
  E2_Interp interp = {E2_Status_Error};
  {
    U64 off = state->bytecode_off;
    U64 off_opl = bytecode.size;
    B32 done = 0;
    B32 good = 1;
    for(;off < off_opl && !done && good;)
    {
      Temp scratch = scratch_begin(&arena, 1);
      U64 start_off = off;
      
      //- rjf: read next opcode
      RDI_EvalOp op = 0;
      off += str8_deserial_read_struct(bytecode, off, &op);
      
      //- rjf: opcode -> ctrl bits
      U16 ctrlbits = 0;
      switch(op)
      {
        default:
        if(op < RDI_EvalOp_COUNT)
        {
          ctrlbits = rdi_eval_op_ctrlbits_table[op];
        }break;
        case E2_EvalOp_SetCtxID:{ctrlbits = RDI_EVAL_CTRLBITS(8, 0, 0);}break;
      }
      
      //- rjf: ctrlbits -> push/pop/decode count
      U64 decode_size = RDI_DECODEN_FROM_CTRLBITS(ctrlbits);
      U64 push_count = RDI_PUSHN_FROM_CTRLBITS(ctrlbits);
      U64 pop_count = RDI_POPN_FROM_CTRLBITS(ctrlbits);
      
      //- rjf: do extra decode
      String8 decode_data = str8_substr(bytecode, r1u64(off, off+decode_size));
      E2_Val decode_val = {0};
      MemoryCopy(&decode_val, decode_data.str, Min(sizeof(decode_val), decode_data.size));
      RDI_EvalTypeGroup type_group = (RDI_EvalTypeGroup)decode_val.u512.u8[0];
      U64 op_arithmetic_size = (U64)decode_val.u512.u8[1];
      off += decode_size;
      
      //- rjf: prepare popped stack values & to-push stack values
      E2_Val *popped_vals = push_array(scratch.arena, E2_Val, pop_count);
      E2_Val *push_vals = push_array(scratch.arena, E2_Val, push_count);
      {
        E2_InterpStackValNode *node = state->top_val;
        for(U64 pop_idx = 0; node != 0 && pop_idx < pop_count; pop_idx += 1, node = node->next)
        {
          popped_vals[pop_idx] = node->val;
        }
      }
      
      //- rjf: do opcode
      E2_CtxFlags ctx_flags = 0;
      U64 ctx_base_addr = 0;
      switch(op)
      {
        default:{}break;
        
        case E2_EvalOp_SetCtxID:
        {
          interp.status = E2_Status_NewCtxID;
          interp.ctx_id = decode_val.u64;
        }break;
        
        case RDI_EvalOp_Stop:
        {
          done = 1;
        }break;
        case RDI_EvalOp_Noop:{}break;
        case RDI_EvalOp_Cond:
        if(popped_vals[0].u64 != 0)
        {
          off += decode_val.u64;
        }break;
        case RDI_EvalOp_Skip:
        {
          off += decode_val.u64;
        }break;
        case RDI_EvalOp_MemRead:
        {
          U64 addr = popped_vals[0].u64;
          U64 size = decode_val.u64;
          Rng1U64 space_addr_range = r1u64(addr, addr+size);
          void *read_dst = &push_vals[0];
          if(size > sizeof(push_vals[0].u512))
          {
            read_dst = push_array(arena, U8, size);
          }
          U64 read_size = e2_space_map_read(space_map, state->selected_ctx.space_id, space_addr_range, read_dst);
          arena_pop(arena, size - read_size);
          push_vals[0].string = str8(read_dst, read_size);
          if(read_size != size)
          {
            good = 0;
            interp.status = E2_Status_MissedSpaceRead;
            interp.missed_read_space_addr_range = space_addr_range;
          }
        }break;
        case RDI_EvalOp_RegRead:
        {
          U8 rdi_reg_code = (decode_val.u64&0x0000FF)>>0;
          U8 byte_size    = (decode_val.u64&0x00FF00)>>8;
          U8 byte_off     = (decode_val.u64&0xFF0000)>>16;
          Arch arch = state->selected_ctx.arch;
          ARCH_Info *arch_info = arch_info_from_arch(arch);
          ARCH_RegCode base_reg_code = arch_reg_code_from_rdi(arch, rdi_reg_code);
          if(0 <= base_reg_code && base_reg_code < arch_info->reg_code_count)
          {
            Rng1U16 reg_rng = arch_info->reg_code_rng_table[base_reg_code];
            U64 off = (U64)reg_rng.min + byte_off;
            U64 size = (U64)byte_size;
            Rng1U64 space_addr_range = r1u64(off, off+size);
            U64 read_size = e2_space_map_read(space_map, state->selected_ctx.reg_space_id, space_addr_range, &push_vals[0]);
            if(read_size != size)
            {
              good = 0;
              interp.status = E2_Status_MissedSpaceRead;
              interp.missed_read_space_addr_range = space_addr_range;
            }
          }
          else
          {
            good = 0;
            interp.status = E2_Status_BadRegCode;
          }
        }break;
        case RDI_EvalOp_FrameOff:  {ctx_flags = E2_CtxFlag_HasFrameBase;  ctx_base_addr = state->selected_ctx.frame_base_addr;}goto optional_base_off;
        case RDI_EvalOp_ModuleOff: {ctx_flags = E2_CtxFlag_HasModuleBase; ctx_base_addr = state->selected_ctx.module_base_addr;}goto optional_base_off;
        case RDI_EvalOp_TLSOff:    {ctx_flags = E2_CtxFlag_HasTLSBase;    ctx_base_addr = state->selected_ctx.tls_base_addr;}goto optional_base_off;
        case RDI_EvalOp_PushCfa:   {ctx_flags = E2_CtxFlag_HasCFA;        ctx_base_addr = state->selected_ctx.cfa_addr;}goto optional_base_off;
        optional_base_off:;
        {
          if(state->selected_ctx.flags & ctx_flags)
          {
            push_vals[0].u64 = ctx_base_addr + decode_val.u64;
          }
          else
          {
            good = 0;
            interp.status = E2_Status_MissingCtxFlag;
            interp.missing_ctx_flags |= ctx_flags;
          }
        }break;
        
        case RDI_EvalOp_ConstU8:
        case RDI_EvalOp_ConstU16:
        case RDI_EvalOp_ConstU32:
        case RDI_EvalOp_ConstU64:
        case RDI_EvalOp_ConstU128:
        {
          push_vals[0] = decode_val;
        }break;
        
        case RDI_EvalOp_ConstString:
        {
          U64 string_size = decode_val.u64;
          String8 string = str8_substr(bytecode, r1u64(off, off+string_size));
          off += string_size;
          push_vals[0].string = string;
        }break;
        
        case RDI_EvalOp_Abs:
        {
          if(0){}
#define CaseA(target_type_group, target_type) else if(type_group == (RDI_EvalTypeGroup_##target_type_group)) do{push_vals[0].target_type = (popped_vals[0].target_type < 0 ? -popped_vals[0].target_type : +popped_vals[0].target_type);}while(0)
#define CaseB(target_type_group, size, target_type) else if(type_group == (RDI_EvalTypeGroup_##target_type_group) && op_arithmetic_size == (size)) do{push_vals[0].target_type = (popped_vals[0].target_type < 0 ? -popped_vals[0].target_type : +popped_vals[0].target_type);}while(0)
          CaseA(F32, f32);
          CaseA(F64, f64);
          CaseB(S, 8,  s8);
          CaseB(S, 16, s16);
          CaseB(S, 32, s32);
          CaseB(S, 64, s64);
          else { MemoryCopyStruct(&push_vals[0], &popped_vals[0]); }
#undef CaseA
#undef CaseB
        }break;
        
        case RDI_EvalOp_Neg:
        {
          if(0){}
#define CaseA(target_type_group, target_type) else if(type_group == (RDI_EvalTypeGroup_##target_type_group)) do{push_vals[0].target_type = (-popped_vals[0].target_type);}while(0)
#define CaseB(target_type_group, size, target_type) else if(type_group == (RDI_EvalTypeGroup_##target_type_group) && op_arithmetic_size == (size)) do{push_vals[0].target_type = (-popped_vals[0].target_type);}while(0)
          CaseA(F32, f32);
          CaseA(F64, f64);
          CaseB(S, 8,  s8);
          CaseB(S, 16, s16);
          CaseB(S, 32, s32);
          CaseB(S, 64, s64);
          else { MemoryCopyStruct(&push_vals[0], &popped_vals[0]); }
#undef CaseA
#undef CaseB
        }break;
        
#define BinOp(target_type_group, dst_type, src_type, symbol) else if(type_group == (RDI_EvalTypeGroup_##target_type_group)) do{push_vals[0].dst_type = popped_vals[1].src_type symbol popped_vals[0].src_type;}while(0)
#define SizedBinOp(target_type_group, size, dst_type, src_type, symbol) else if(type_group == (RDI_EvalTypeGroup_##target_type_group) && op_arithmetic_size == (size)) do{push_vals[0].dst_type = popped_vals[1].src_type symbol popped_vals[0].src_type;}while(0)
#define BinOpDiv(target_type_group, dst_type, src_type, symbol) else if(type_group == (RDI_EvalTypeGroup_##target_type_group)) do{if(popped_vals[0].src_type != 0) { push_vals[0].dst_type = popped_vals[1].src_type symbol popped_vals[0].src_type; } else {interp.status = E2_Status_DivideByZero; good = 0;} }while(0)
#define SizedBinOpDiv(target_type_group, size, dst_type, src_type, symbol) else if(type_group == (RDI_EvalTypeGroup_##target_type_group) && op_arithmetic_size == (size)) do{if(popped_vals[0].src_type != 0) { push_vals[0].dst_type = popped_vals[1].src_type symbol popped_vals[0].src_type; } else {interp.status = E2_Status_DivideByZero; good = 0;} }while(0)
#define BinOpDivFn(target_type_group, dst_type, src_type, fn) else if(type_group == (RDI_EvalTypeGroup_##target_type_group)) do{if(popped_vals[0].src_type != 0) { push_vals[0].dst_type = fn(popped_vals[1].src_type, popped_vals[0].src_type); } else {interp.status = E2_Status_DivideByZero; good = 0;} }while(0)
#define ArithTypeCasesInt(symbol)\
SizedBinOp(S, 8,  s8,  s8,  symbol);\
SizedBinOp(S, 16, s16, s16, symbol);\
SizedBinOp(S, 32, s32, s32, symbol);\
SizedBinOp(S, 64, s64, s64, symbol);\
SizedBinOp(U, 8,  u8,  u8,  symbol);\
SizedBinOp(U, 16, u16, u16, symbol);\
SizedBinOp(U, 32, u32, u32, symbol);\
SizedBinOp(U, 64, u64, u64, symbol);
#define ArithTypeCasesIntDiv(symbol)\
SizedBinOpDiv(S, 8,  s8,  s8,  symbol);\
SizedBinOpDiv(S, 16, s16, s16, symbol);\
SizedBinOpDiv(S, 32, s32, s32, symbol);\
SizedBinOpDiv(S, 64, s64, s64, symbol);\
SizedBinOpDiv(U, 8,  u8,  u8,  symbol);\
SizedBinOpDiv(U, 16, u16, u16, symbol);\
SizedBinOpDiv(U, 32, u32, u32, symbol);\
SizedBinOpDiv(U, 64, u64, u64, symbol);
#define LogTypeCases(symbol)\
BinOp(F32, u64, f32, symbol);\
BinOp(F64, u64, f64, symbol);\
ArithTypeCasesInt(symbol)
#define ArithTypeCases(symbol)\
BinOp(F32, f32, f32, symbol);\
BinOp(F64, f64, f64, symbol);\
ArithTypeCasesInt(symbol)
#define ArithTypeCasesDiv(symbol)\
BinOpDiv(F32, f32, f32, symbol);\
BinOpDiv(F64, f64, f64, symbol);\
ArithTypeCasesIntDiv(symbol)
#define ArithTypeCasesIntAllU64(symbol)\
BinOp(S, u64, u64, symbol);\
BinOp(U, u64, u64, symbol);
#define Case(name, ...) case RDI_EvalOp_##name:{if(0){} __VA_ARGS__ else {interp.status = E2_Status_BadOpTypes; good = 0;}}break
        Case(Add, ArithTypeCases(+));
        Case(Sub, ArithTypeCases(-));
        Case(Mul, ArithTypeCases(*));
        Case(Div, ArithTypeCasesDiv(/));
        Case(Mod, ArithTypeCasesIntDiv(%) BinOpDivFn(F32, f32, f32, mod_f32); BinOpDivFn(F64, f64, f64, mod_f64););
        Case(LShift, ArithTypeCasesInt(<<));
        Case(RShift, ArithTypeCasesInt(>>));
        Case(BitAnd, ArithTypeCasesIntAllU64(&));
        Case(BitOr,  ArithTypeCasesIntAllU64(|));
        Case(BitXor, ArithTypeCasesIntAllU64(^));
        Case(LogAnd, ArithTypeCasesIntAllU64(&&));
        Case(LogOr,  ArithTypeCasesIntAllU64(||));
        Case(LsEq,   LogTypeCases(<=));
        Case(GrEq,   LogTypeCases(>=));
        Case(Less,   LogTypeCases(<));
        Case(Grtr,   LogTypeCases(>));
#undef Case
#undef BinOp
#undef SizedBinOp
#undef BinOpDiv
#undef SizedBinOpDiv
#undef BinOpDivFn
#undef ArithTypeCasesInt
#undef ArithTypeCasesIntDiv
#undef ArithTypeCases
#undef ArithTypeCasesDiv
#undef ArithTypeCasesIntAllU64
        
        case RDI_EvalOp_EqEq:
        case RDI_EvalOp_NtEq:
        {
          if(popped_vals[0].string.size != 0 && popped_vals[1].string.size != 0)
          {
            push_vals[0].u64 = str8_match(popped_vals[0].string, popped_vals[1].string, 0);
          }
          else
          {
            push_vals[0].u64 = MemoryMatchStruct(&popped_vals[0].u512, &popped_vals[1].u512);
          }
          if(op == RDI_EvalOp_NtEq)
          {
            push_vals[0].u64 = !push_vals[0].u64;
          }
        }break;
        
        case RDI_EvalOp_Trunc:
        {
          if(0 < decode_val.u64)
          {
            U64 mask = 0;
            if(decode_val.u64 < 64)
            {
              mask = max_U64 >> (64 - decode_val.u64);
            }
            push_vals[0].u64 = popped_vals[0].u64&mask;
          }
        }break;
        
        case RDI_EvalOp_TruncSigned:
        {
          if(0 < decode_val.u64)
          {
            U64 mask = 0;
            if(decode_val.u64 < 64)
            {
              mask = max_U64 >> (64 - decode_val.u64);
            }
            U64 high = 0;
            if(popped_vals[0].u64 & (1 << (decode_val.u64 - 1)))
            {
              high = ~mask;
            }
            push_vals[0].u64 = high|(popped_vals[0].u64&mask);
          }
        }break;
        
        case RDI_EvalOp_Convert:
        {
          U32 in = decode_val.u64&0xFF;
          U32 out = (decode_val.u64 >> 8)&0xFF;
          if(in != 0)
          {
            switch(in + out*RDI_EvalTypeGroup_COUNT)
            {
              default:{good = 0; interp.status = E2_Status_BadOpTypes;}break;
#define Case(dst_tg, dst_slot, dst_type, src_tg, src_slot) case RDI_EvalTypeGroup_##src_tg + RDI_EvalTypeGroup_##dst_tg*RDI_EvalTypeGroup_COUNT:{push_vals[0].dst_slot = (dst_type)popped_vals[0].src_slot;}break
              Case(U, u64, U64, F32, f32);
              Case(U, u64, U64, F64, f64);
              Case(S, s64, S64, F32, f32);
              Case(S, s64, S64, F64, f64);
              Case(F32, f32, F32, S, s64);
              Case(F64, f64, F64, S, s64);
              Case(F32, f32, F32, U, u64);
              Case(F64, f64, F64, U, u64); 
              Case(F32, f32, F32, F64, f64);
              Case(F64, f64, F64, F32, f32);
#undef Case
            }
          }
        }break;
        
        case RDI_EvalOp_Pick:
        {
          U64 target_idx = decode_val.u64;
          U64 idx = 0;
          B32 found = 0;
          for(E2_InterpStackValNode *n = state->top_val; n != 0; n = n->next, idx += 1)
          {
            if(idx == target_idx)
            {
              found = 1;
              push_vals[0] = n->val;
              break;
            }
          }
          if(!found)
          {
            good = 0;
            interp.status = E2_Status_InsufficientStackSpace;
          }
        }break;
        
        case RDI_EvalOp_Swap:
        {
          push_vals[0] = popped_vals[1];
          push_vals[1] = popped_vals[0];
        }break;
        
        case RDI_EvalOp_Pop:{NoOp;}break;
        
        case RDI_EvalOp_ValueRead:
        {
          U64 bytes_to_read = decode_val.u64;
          U64 offset = popped_vals[0].u64;
          if(offset + bytes_to_read <= popped_vals[1].string.size)
          {
            MemoryCopy(&push_vals[0].u512, popped_vals[1].string.str + offset, bytes_to_read);
          }
          else
          {
            good = 0;
            interp.status = E2_Status_BadOffset;
          }
        }break;
        
        case RDI_EvalOp_ByteSwap:
        {
          U64 byte_size = decode_val.u64;
          switch(byte_size)
          {
            default:
            {
              good = 0;
              interp.status = E2_Status_UnsupportedOp;
            }break;
            case 2:{push_vals[0].u16 = bswap_u16(popped_vals[0].u16);}break;
            case 4:{push_vals[0].u32 = bswap_u32(popped_vals[0].u32);}break;
            case 8:{push_vals[0].u64 = bswap_u64(popped_vals[0].u64);}break;
          }
        }break;
        
        case RDI_EvalOp_CallSiteValue:
        {
          // DW_OP_entry_value: requires evaluating the embedded sub-bytecode
          // in this frame's entry-time register state (i.e. caller's state
          // at the call instruction). that state is not currently plumbed
          // into the eval context. interpreting in the current ctx gives
          // wrong values for spilled args, so report BadOp instead.
          // skip past the embedded sub-bytecode in the outer stream so the
          // bytes are not interpreted as outer ops on resume.
          good = 0;
          interp.status = E2_Status_UnsupportedOp;
        }break;
        
        case RDI_EvalOp_PartialValue:
        {
          // DW_OP_piece marker: top-of-stack is a piece of a composite value.
          // we do not assemble composites; for single-piece expressions the
          // value already on the stack is the result. for multi-piece, only
          // the first piece is returned (stack[0] is the final result).
          good = 0;
          interp.status = E2_Status_UnsupportedOp;
        }break;
        
        case RDI_EvalOp_PartialValueBit:
        {
          // DW_OP_bit_piece marker. same caveat as PartialValue.
          good = 0;
          interp.status = E2_Status_UnsupportedOp;
        }break;
      }
      
      //- rjf: if opcode successful -> do stack pops
      if(good) for EachIndex(pop_idx, pop_count)
      {
        E2_InterpStackValNode *popped = state->top_val;
        if(popped != 0)
        {
          SLLStackPop(state->top_val);
          SLLStackPush(state->free_val, popped);
        }
        else
        {
          break;
        }
      }
      
      //- rjf: if opcode successful -> do stack pushes
      if(good) for EachIndex(push_idx, push_count)
      {
        E2_InterpStackValNode *node = state->free_val;
        if(node != 0)
        {
          SLLStackPop(state->free_val);
        }
        else
        {
          node = push_array(arena, E2_InterpStackValNode, 1);
        }
        node->val = push_vals[push_idx];
        SLLStackPush(state->top_val, node);
      }
      
      //- rjf: if opcode successful -> commit new bytecode offset
      if(good)
      {
        state->bytecode_off = off;
      }
      
      scratch_end(scratch);
      if(off == start_off)
      {
        break;
      }
    }
    if(off == off_opl && good)
    {
      interp.status = E2_Status_Good;
      if(state->top_val != 0)
      {
        interp.val = state->top_val->val;
      }
    }
  }
  return interp;
}
