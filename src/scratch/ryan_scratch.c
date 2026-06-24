// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Build Options

#define BUILD_TITLE "ryan_scratch"
#define BUILD_CONSOLE_INTERFACE 1

////////////////////////////////
//~ rjf: Includes

//- rjf: [h]
#include "base/base_inc.h"
#include "x64/x64.h"
#include "artifact_cache/artifact_cache.h"
#include "rdi/rdi_local.h"
#include "dbg_info/dbg_info.h"
#include "arch/arch_inc.h"
#include "eval2/eval2.h"

//- rjf: [c]
#include "base/base_inc.c"
#include "x64/x64.c"
#include "artifact_cache/artifact_cache.c"
#include "rdi/rdi_local.c"
#include "dbg_info/dbg_info.c"
#include "arch/arch_inc.c"
#include "eval2/eval2.c"

////////////////////////////////
//~ rjf: Entry Point

internal void
entry_point(CmdLine *cmdline)
{
  String8 strings[] =
  {
    // (A)B
    // (A) & B
    // (A)
    // int & B
    // (1 + (int)&B)
    s("foo = 123"),
    s("1 > 2"),
    s("1 ? \"Test\" : 888"),
    s("'a'"),
    s("'b'"),
    s("float32(111)"),
    s("float64(111)"),
    s("222.f"),
    s("222.0"),
    s("(float32)222"),
    s("(float64)222"),
    s("[123]"),
    s("foo"),
    s("foo.bar"),
    s("123[456]"),
    s("123"),
    s("123 + 456"),
    s("!1"),
    s("!0"),
    s("1 + 1 + 1"),
    s("40 / 0"),
    s("3 * 4"),
    s("3 * 4 + 2"),
    s("(3 * 4) + 2"),
    s("2 + (3 * 4)"),
    s("123 | 456"),
    s("123 + "),
    s("-123"),
    s("sizeof 123"),
    s("1 ? 123 : 456"),
    s("0 ? 123 : 456"),
    s("123(1, 2, 3)"),
  };
  for EachElement(idx, strings)
  {
    Temp scratch = scratch_begin(0, 0);
    String8List msgs = {0};
    
    // rjf: string -> expr
    E2_Expr *expr = &e2_expr_nil;
    {
      E2_ParseState state = {0};
      E2_ExprMap expr_map = {0};
      E2_Expr *access_expr = &e2_expr_nil;
      E2_Val compile_time_eval_result = {0};
      for(;;)
      {
        E2_Parse parse = e2_parse_from_string(scratch.arena, &state, &expr_map, access_expr, compile_time_eval_result, strings[idx]);
        expr = parse.expr;
        access_expr = &e2_expr_nil;
        for EachNode(n, E2_Msg, parse.msgs.first)
        {
          str8_list_push(scratch.arena, &msgs, n->string);
        }
        if(parse.status == E2_ParseStatus_MissedIdentifierResolution)
        {
          E2_Expr *expr = &e2_expr_nil;
          if(str8_match(parse.identifier, s("float32"), 0))
          {
            expr = e2_expr_type(scratch.arena, e2_type_key_basic(E2_TypeKind_F32));
          }
          else if(str8_match(parse.identifier, s("float64"), 0))
          {
            expr = e2_expr_type(scratch.arena, e2_type_key_basic(E2_TypeKind_F64));
          }
          else if(str8_match(parse.identifier, s("int32"), 0))
          {
            expr = e2_expr_type(scratch.arena, e2_type_key_basic(E2_TypeKind_S32));
          }
          else if(str8_match(parse.identifier, s("int64"), 0))
          {
            expr = e2_expr_type(scratch.arena, e2_type_key_basic(E2_TypeKind_S64));
          }
          else
          {
            expr = e2_expr_const_u64_or_smaller(scratch.arena, 123);
          }
          e2_expr_map_push(scratch.arena, &expr_map, parse.identifier, expr);
        }
        if(parse.status == E2_ParseStatus_NewIdentifierDefinition)
        {
          e2_expr_map_push(scratch.arena, &expr_map, parse.identifier, expr);
        }
        if(parse.status == E2_ParseStatus_MemberAccess)
        {
          access_expr = e2_expr_const_u64_or_smaller(scratch.arena, 456);
        }
        if(parse.status == E2_ParseStatus_IndexAccess)
        {
          access_expr = e2_expr_const_u64_or_smaller(scratch.arena, 111);
        }
        if(parse.status == E2_ParseStatus_Call)
        {
          access_expr = e2_expr_const_u64_or_smaller(scratch.arena, 123456);
        }
        if(parse.status == E2_ParseStatus_CompileTimeEval)
        {
          String8 bytecode = e2_bytecode_from_expr(scratch.arena, expr);
          E2_InterpState interp_state = {0};
          E2_SpaceMap space_map = {0};
          E2_Interp interp = e2_interp_from_bytecode(scratch.arena, &interp_state, &space_map, bytecode);
          compile_time_eval_result = interp.val;
        }
        if(e2_parse_status_is_terminal(parse.status))
        {
          break;
        }
      }
    }
    
    // rjf: expr -> bytecode
    String8 bytecode = e2_bytecode_from_expr(scratch.arena, expr);
    
    // rjf: bytecode -> value
    E2_Val val = {0};
    {
      E2_InterpState state = {0};
      E2_SpaceMap space_map = {0};
      for(;;)
      {
        E2_Interp interp = e2_interp_from_bytecode(scratch.arena, &state, &space_map, bytecode);
        val = interp.val;
        if(e2_interp_status_is_terminal(interp.status))
        {
          if(interp.status != E2_InterpStatus_Good)
          {
            str8_list_pushf(scratch.arena, &msgs, "Interpretation error (%i).", interp.status);
          }
          break;
        }
      }
    }
    
    // rjf: message string list -> string
    StringJoin join = {.sep = s(" ")};
    String8 msgs_string = str8_list_join(scratch.arena, &msgs, &join);
    
    // rjf: log
    String8 log = str8f(scratch.arena, "%S -> %I64d (f32: %f) (f64: %f) *%s* %s%S\n",
                        strings[idx],
                        val.s64,
                        val.f32,
                        val.f64,
                        expr->mode == E2_Mode_Type ? "type" :
                        expr->mode == E2_Mode_Value ? "value" :
                        "address",
                        msgs_string.size != 0 ? " // " : "", msgs_string);
    raddbg_log("%S", log);
    printf("%.*s", str8_varg(log));
    fflush(stdout);
    
    scratch_end(scratch);
  }
}
