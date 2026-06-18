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
#include "http/http_inc.h"
#include "artifact_cache/artifact_cache.h"
#include "symbol_server/symbol_server_inc.h"
#include "rdi/rdi_local.h"
#include "dbg_info/dbg_info.h"
#include "arch/arch_inc.h"
#include "eval2/eval2.h"

//- rjf: [c]
#include "base/base_inc.c"
#include "x64/x64.c"
#include "http/http_inc.c"
#include "artifact_cache/artifact_cache.c"
#include "symbol_server/symbol_server_inc.c"
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
  };
  for EachElement(idx, strings)
  {
    Temp scratch = scratch_begin(0, 0);
    
    // rjf: string -> expr
    E2_Expr *expr = &e2_expr_nil;
    {
      E2_ParseState state = {0};
      E2_ExprMap expr_map = {0};
      for(;;)
      {
        E2_Parse parse = e2_parse_from_string(scratch.arena, &state, &expr_map, strings[idx]);
        if(parse.status == E2_Status_Good)
        {
          expr = parse.expr;
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
        if(interp.status == E2_Status_Good)
        {
          val = interp.val;
          break;
        }
        else if(E2_Status_FirstError <= interp.status <= E2_Status_LastError)
        {
          break;
        }
      }
    }
    
    // rjf: log
    printf("%.*s -> %I64u\n", str8_varg(strings[idx]), val.u64);
    fflush(stdout);
    
    scratch_end(scratch);
  }
}
