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
#include "rdi/rdi_local.h"
#include "arch/arch_inc.h"
#include "eval2/eval2.h"

//- rjf: [c]
#include "base/base_inc.c"
#include "x64/x64.c"
#include "rdi/rdi_local.c"
#include "arch/arch_inc.c"
#include "eval2/eval2.c"

////////////////////////////////
//~ rjf: Entry Point

internal void
entry_point(CmdLine *cmdline)
{
  Temp scratch = scratch_begin(0, 0);
  E2_ParseState state = {0};
  E2_SpaceMap space_map = {0};
  E2_ExprMap expr_map = {0};
  for(;;)
  {
    E2_Parse parse = e2_parse_from_string(scratch.arena, &state, &space_map, &expr_map, s("!123"));
    if(parse.status == E2_Status_Good)
    {
      break;
    }
  }
  scratch_end(scratch);
}
