// Copyright (c) 2024 Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Code Views

internal void
rd_code_view_init(RD_CodeViewState *cv)
{
  if(cv->initialized == 0)
  {
    cv->initialized = 1;
    cv->preferred_column = 1;
    cv->find_text_arena = rd_push_view_arena();
    cv->center_cursor = 1;
    rd_store_view_loading_info(1, 0, 0);
  }
}

internal RD_CodeViewBuildResult
rd_code_view_build(Arena *arena, RD_CodeViewState *cv, RD_CodeViewBuildFlags flags, Rng2F32 rect, String8 text_data, TXT_TextInfo *text_info, DASM_LineArray *dasm_lines, Rng1U64 dasm_vaddr_range, DI_Key dasm_dbgi_key)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(&arena, 1);
  HS_Scope *hs_scope = hs_scope_open();
  TXT_Scope *txt_scope = txt_scope_open();
  
  //////////////////////////////
  //- rjf: unpack state
  //
  UI_ScrollPt2 scroll_pos = rd_view_scroll_pos();
  
  //////////////////////////////
  //- rjf: extract invariants
  //
  FNT_Tag code_font = rd_font_from_slot(RD_FontSlot_Code);
  F32 code_font_size = rd_font_size_from_slot(RD_FontSlot_Code);
  F32 code_tab_size = fnt_column_size_from_tag_size(code_font, code_font_size)*rd_setting_val_from_code(RD_SettingCode_TabWidth).s32;
  FNT_Metrics code_font_metrics = fnt_metrics_from_tag_size(code_font, code_font_size);
  F32 code_line_height = ceil_f32(fnt_line_height_from_metrics(&code_font_metrics) * 1.5f);
  F32 big_glyph_advance = fnt_dim_from_tag_size_string(code_font, code_font_size, 0, 0, str8_lit("H")).x;
  Vec2F32 panel_box_dim = dim_2f32(rect);
  F32 scroll_bar_dim = floor_f32(ui_top_font_size()*1.5f);
  Vec2F32 code_area_dim = v2f32(panel_box_dim.x - scroll_bar_dim, panel_box_dim.y - scroll_bar_dim);
  S64 num_possible_visible_lines = (S64)(code_area_dim.y/code_line_height)+1;
  CTRL_Entity *thread = ctrl_entity_from_handle(d_state->ctrl_entity_store, rd_regs()->thread);
  CTRL_Entity *process = ctrl_entity_ancestor_from_kind(thread, CTRL_EntityKind_Process);
  
  //////////////////////////////
  //- rjf: process commands
  //
  for(RD_Cmd *cmd = 0; rd_next_cmd(&cmd);)
  {
    // rjf: mismatched window/panel => skip
    if(!rd_handle_match(rd_regs()->view, cmd->regs->view))
    {
      continue;
    }
    
    // rjf: process
    RD_CmdKind kind = rd_cmd_kind_from_string(cmd->name);
    switch(kind)
    {
      default: break;
      case RD_CmdKind_GoToLine:
      {
        cv->goto_line_num = cmd->regs->cursor.line;
      }break;
      case RD_CmdKind_CenterCursor:
      {
        cv->center_cursor = 1;
      }break;
      case RD_CmdKind_ContainCursor:
      {
        cv->contain_cursor = 1;
      }break;
      case RD_CmdKind_FindTextForward:
      {
        arena_clear(cv->find_text_arena);
        cv->find_text_fwd = push_str8_copy(cv->find_text_arena, cmd->regs->string);
      }break;
      case RD_CmdKind_FindTextBackward:
      {
        arena_clear(cv->find_text_arena);
        cv->find_text_bwd = push_str8_copy(cv->find_text_arena, cmd->regs->string);
      }break;
      case RD_CmdKind_ToggleWatchExpressionAtMouse:
      {
        cv->watch_expr_at_mouse = 1;
      }break;
    }
  }
  
  //////////////////////////////
  //- rjf: determine visible line range / count
  //
  Rng1S64 visible_line_num_range = r1s64(scroll_pos.y.idx + (S64)(scroll_pos.y.off) + 1 - !!(scroll_pos.y.off < 0),
                                         scroll_pos.y.idx + (S64)(scroll_pos.y.off) + 1 + num_possible_visible_lines);
  Rng1S64 target_visible_line_num_range = r1s64(scroll_pos.y.idx + 1,
                                                scroll_pos.y.idx + 1 + num_possible_visible_lines);
  U64 visible_line_count = 0;
  {
    visible_line_num_range.min = Clamp(1, visible_line_num_range.min, (S64)text_info->lines_count);
    visible_line_num_range.max = Clamp(1, visible_line_num_range.max, (S64)text_info->lines_count);
    visible_line_num_range.min = Max(1, visible_line_num_range.min);
    visible_line_num_range.max = Max(1, visible_line_num_range.max);
    target_visible_line_num_range.min = Clamp(1, target_visible_line_num_range.min, (S64)text_info->lines_count);
    target_visible_line_num_range.max = Clamp(1, target_visible_line_num_range.max, (S64)text_info->lines_count);
    target_visible_line_num_range.min = Max(1, target_visible_line_num_range.min);
    target_visible_line_num_range.max = Max(1, target_visible_line_num_range.max);
    visible_line_count = (U64)dim_1s64(visible_line_num_range)+1;
  }
  
  //////////////////////////////
  //- rjf: calculate scroll bounds
  //
  S64 line_size_x = 0;
  Rng1S64 scroll_idx_rng[Axis2_COUNT] = {0};
  {
    line_size_x = (text_info->lines_max_size*big_glyph_advance*3)/2;
    line_size_x = ClampBot(line_size_x, (S64)big_glyph_advance*120);
    line_size_x = ClampBot(line_size_x, (S64)code_area_dim.x);
    scroll_idx_rng[Axis2_X] = r1s64(0, line_size_x-(S64)code_area_dim.x);
    scroll_idx_rng[Axis2_Y] = r1s64(0, (S64)text_info->lines_count-1);
  }
  
  //////////////////////////////
  //- rjf: calculate line-range-dependent info
  //
  F32 line_num_width_px = big_glyph_advance * (log10(visible_line_num_range.max) + 3);
  F32 priority_margin_width_px = 0;
  F32 catchall_margin_width_px = 0;
  if(flags & RD_CodeViewBuildFlag_Margins)
  {
    priority_margin_width_px = big_glyph_advance*3.5f;
    catchall_margin_width_px = big_glyph_advance*3.5f;
  }
  TXT_LineTokensSlice slice = txt_line_tokens_slice_from_info_data_line_range(scratch.arena, text_info, text_data, visible_line_num_range);
  
  //////////////////////////////
  //- rjf: get active search query
  //
  String8 search_query = {0};
  Side search_query_side = Side_Invalid;
  B32 search_query_is_active = 0;
  {
    RD_Window *window = rd_window_from_handle(rd_regs()->window);
    RD_CmdKind query_cmd_kind = rd_cmd_kind_from_string(window->query_cmd_name);
    if(query_cmd_kind == RD_CmdKind_FindTextForward ||
       query_cmd_kind == RD_CmdKind_FindTextBackward)
    {
      search_query = str8(window->query_view_stack_top->query_buffer, window->query_view_stack_top->query_string_size);
      search_query_is_active = 1;
      search_query_side = (query_cmd_kind == RD_CmdKind_FindTextForward) ? Side_Max : Side_Min;
    }
  }
  
  //////////////////////////////
  //- rjf: prepare code slice info bundle, for the viewable region of text
  //
  RD_CodeSliceParams code_slice_params = {0};
  {
    // rjf: fill basics
    code_slice_params.flags                     = RD_CodeSliceFlag_LineNums|RD_CodeSliceFlag_Clickable;
    if(flags & RD_CodeViewBuildFlag_Margins)
    {
      code_slice_params.flags |= RD_CodeSliceFlag_PriorityMargin|RD_CodeSliceFlag_CatchallMargin;
    }
    code_slice_params.line_num_range            = visible_line_num_range;
    code_slice_params.line_text                 = push_array(scratch.arena, String8, visible_line_count);
    code_slice_params.line_ranges               = push_array(scratch.arena, Rng1U64, visible_line_count);
    code_slice_params.line_tokens               = push_array(scratch.arena, TXT_TokenArray, visible_line_count);
    code_slice_params.line_bps                  = push_array(scratch.arena, RD_EntityList, visible_line_count);
    code_slice_params.line_ips                  = push_array(scratch.arena, CTRL_EntityList, visible_line_count);
    code_slice_params.line_pins                 = push_array(scratch.arena, RD_EntityList, visible_line_count);
    code_slice_params.line_vaddrs               = push_array(scratch.arena, U64, visible_line_count);
    code_slice_params.line_infos                = push_array(scratch.arena, D_LineList, visible_line_count);
    code_slice_params.font                      = code_font;
    code_slice_params.font_size                 = code_font_size;
    code_slice_params.tab_size                  = code_tab_size;
    code_slice_params.line_height_px            = code_line_height;
    code_slice_params.search_query              = search_query;
    code_slice_params.priority_margin_width_px  = priority_margin_width_px;
    code_slice_params.catchall_margin_width_px  = catchall_margin_width_px;
    code_slice_params.line_num_width_px         = line_num_width_px;
    code_slice_params.line_text_max_width_px    = (F32)line_size_x;
    code_slice_params.margin_float_off_px       = scroll_pos.x.idx + scroll_pos.x.off;
    
    // rjf: fill text info
    {
      S64 line_num = visible_line_num_range.min;
      U64 line_idx = visible_line_num_range.min-1;
      for(U64 visible_line_idx = 0;
          visible_line_idx < visible_line_count && line_idx < text_info->lines_count;
          visible_line_idx += 1, line_idx += 1, line_num += 1)
      {
        code_slice_params.line_text[visible_line_idx]   = str8_substr(text_data, text_info->lines_ranges[line_idx]);
        code_slice_params.line_ranges[visible_line_idx] = text_info->lines_ranges[line_idx];
        code_slice_params.line_tokens[visible_line_idx] = slice.line_tokens[visible_line_idx];
      }
    }
    
    // rjf: find visible breakpoints for source code
    ProfScope("find visible breakpoints")
    {
      RD_EntityList bps = rd_query_cached_entity_list_with_kind(RD_EntityKind_Breakpoint);
      for(RD_EntityNode *n = bps.first; n != 0; n = n->next)
      {
        RD_Entity *bp = n->entity;
        RD_Entity *loc = rd_entity_child_from_kind(bp, RD_EntityKind_Location);
        if(path_match_normalized(loc->string, rd_regs()->file_path) &&
           visible_line_num_range.min <= loc->text_point.line && loc->text_point.line <= visible_line_num_range.max)
        {
          U64 slice_line_idx = (loc->text_point.line-visible_line_num_range.min);
          rd_entity_list_push(scratch.arena, &code_slice_params.line_bps[slice_line_idx], bp);
        }
      }
    }
    
    // rjf: find live threads mapping to source code
    ProfScope("find live threads mapping to this file")
    {
      String8 file_path = rd_regs()->file_path;
      CTRL_Entity *selected_thread = ctrl_entity_from_handle(d_state->ctrl_entity_store, rd_regs()->thread);
      CTRL_EntityList threads = ctrl_entity_list_from_kind(d_state->ctrl_entity_store, CTRL_EntityKind_Thread);
      for(CTRL_EntityNode *thread_n = threads.first; thread_n != 0; thread_n = thread_n->next)
      {
        CTRL_Entity *thread = thread_n->v;
        CTRL_Entity *process = ctrl_entity_ancestor_from_kind(thread, CTRL_EntityKind_Process);
        U64 unwind_count = (thread == selected_thread) ? rd_regs()->unwind_count : 0;
        U64 inline_depth = (thread == selected_thread) ? rd_regs()->inline_depth : 0;
        U64 rip_vaddr = d_query_cached_rip_from_thread_unwind(thread, unwind_count);
        U64 last_inst_on_unwound_rip_vaddr = rip_vaddr - !!unwind_count;
        CTRL_Entity *module = ctrl_module_from_process_vaddr(process, last_inst_on_unwound_rip_vaddr);
        U64 rip_voff = ctrl_voff_from_vaddr(module, last_inst_on_unwound_rip_vaddr);
        DI_Key dbgi_key = ctrl_dbgi_key_from_module(module);
        D_LineList lines = d_lines_from_dbgi_key_voff(scratch.arena, &dbgi_key, rip_voff);
        for(D_LineNode *n = lines.first; n != 0; n = n->next)
        {
          if(path_match_normalized(n->v.file_path, file_path) && visible_line_num_range.min <= n->v.pt.line && n->v.pt.line <= visible_line_num_range.max)
          {
            U64 slice_line_idx = n->v.pt.line-visible_line_num_range.min;
            ctrl_entity_list_push(scratch.arena, &code_slice_params.line_ips[slice_line_idx], thread);
          }
        }
      }
    }
    
    // rjf: find visible watch pins for source code
    ProfScope("find visible watch pins")
    {
      RD_EntityList wps = rd_query_cached_entity_list_with_kind(RD_EntityKind_WatchPin);
      for(RD_EntityNode *n = wps.first; n != 0; n = n->next)
      {
        RD_Entity *wp = n->entity;
        RD_Entity *loc = rd_entity_child_from_kind(wp, RD_EntityKind_Location);
        if(path_match_normalized(loc->string, rd_regs()->file_path) &&
           visible_line_num_range.min <= loc->text_point.line && loc->text_point.line <= visible_line_num_range.max)
        {
          U64 slice_line_idx = (loc->text_point.line-visible_line_num_range.min);
          rd_entity_list_push(scratch.arena, &code_slice_params.line_pins[slice_line_idx], wp);
        }
      }
    }
    
    // rjf: find all src -> dasm info
    ProfScope("find all src -> dasm info")
    {
      String8 file_path = rd_regs()->file_path;
      D_LineListArray lines_array = d_lines_array_from_file_path_line_range(scratch.arena, file_path, visible_line_num_range);
      if(lines_array.count != 0)
      {
        MemoryCopy(code_slice_params.line_infos, lines_array.v, sizeof(D_LineList)*lines_array.count);
      }
      code_slice_params.relevant_dbgi_keys = lines_array.dbgi_keys;
    }
    
    // rjf: find live threads mapping to disasm
    if(dasm_lines) ProfScope("find live threads mapping to this disassembly")
    {
      CTRL_Entity *selected_thread = ctrl_entity_from_handle(d_state->ctrl_entity_store, rd_regs()->thread);
      CTRL_EntityList threads = ctrl_entity_list_from_kind(d_state->ctrl_entity_store, CTRL_EntityKind_Thread);
      for(CTRL_EntityNode *thread_n = threads.first; thread_n != 0; thread_n = thread_n->next)
      {
        CTRL_Entity *thread = thread_n->v;
        U64 unwind_count = (thread == selected_thread) ? rd_regs()->unwind_count : 0;
        U64 rip_vaddr = d_query_cached_rip_from_thread_unwind(thread, unwind_count);
        if(ctrl_entity_ancestor_from_kind(thread, CTRL_EntityKind_Process) == process && contains_1u64(dasm_vaddr_range, rip_vaddr))
        {
          U64 rip_off = rip_vaddr - dasm_vaddr_range.min;
          S64 line_num = dasm_line_array_idx_from_code_off__linear_scan(dasm_lines, rip_off)+1;
          if(contains_1s64(visible_line_num_range, line_num))
          {
            U64 slice_line_idx = (line_num-visible_line_num_range.min);
            ctrl_entity_list_push(scratch.arena, &code_slice_params.line_ips[slice_line_idx], thread);
          }
        }
      }
    }
    
    // rjf: find breakpoints mapping to this disasm
    if(dasm_lines) ProfScope("find breakpoints mapping to this disassembly")
    {
      RD_EntityList bps = rd_query_cached_entity_list_with_kind(RD_EntityKind_Breakpoint);
      for(RD_EntityNode *n = bps.first; n != 0; n = n->next)
      {
        RD_Entity *bp = n->entity;
        RD_Entity *loc = rd_entity_child_from_kind(bp, RD_EntityKind_Location);
        if(loc->flags & RD_EntityFlag_HasVAddr && contains_1u64(dasm_vaddr_range, loc->vaddr))
        {
          U64 off = loc->vaddr-dasm_vaddr_range.min;
          U64 idx = dasm_line_array_idx_from_code_off__linear_scan(dasm_lines, off);
          S64 line_num = (S64)(idx+1);
          if(contains_1s64(visible_line_num_range, line_num))
          {
            U64 slice_line_idx = (line_num-visible_line_num_range.min);
            rd_entity_list_push(scratch.arena, &code_slice_params.line_bps[slice_line_idx], bp);
          }
        }
      }
    }
    
    // rjf: find watch pins mapping to this disasm
    if(dasm_lines) ProfScope("find watch pins mapping to this disassembly")
    {
      RD_EntityList pins = rd_query_cached_entity_list_with_kind(RD_EntityKind_WatchPin);
      for(RD_EntityNode *n = pins.first; n != 0; n = n->next)
      {
        RD_Entity *pin = n->entity;
        RD_Entity *loc = rd_entity_child_from_kind(pin, RD_EntityKind_Location);
        if(loc->flags & RD_EntityFlag_HasVAddr && contains_1u64(dasm_vaddr_range, loc->vaddr))
        {
          U64 off = loc->vaddr-dasm_vaddr_range.min;
          U64 idx = dasm_line_array_idx_from_code_off__linear_scan(dasm_lines, off);
          S64 line_num = (S64)(idx+1);
          if(contains_1s64(visible_line_num_range, line_num))
          {
            U64 slice_line_idx = (line_num-visible_line_num_range.min);
            rd_entity_list_push(scratch.arena, &code_slice_params.line_pins[slice_line_idx], pin);
          }
        }
      }
    }
    
    // rjf: fill dasm -> src info
    if(dasm_lines)
    {
      CTRL_Entity *module = ctrl_module_from_process_vaddr(process, dasm_vaddr_range.min);
      DI_Key dbgi_key = ctrl_dbgi_key_from_module(module);
      for(S64 line_num = visible_line_num_range.min; line_num < visible_line_num_range.max; line_num += 1)
      {
        U64 vaddr = dasm_vaddr_range.min + dasm_line_array_code_off_from_idx(dasm_lines, line_num-1);
        U64 voff = ctrl_voff_from_vaddr(module, vaddr);
        U64 slice_idx = line_num-visible_line_num_range.min;
        code_slice_params.line_vaddrs[slice_idx] = vaddr;
        code_slice_params.line_infos[slice_idx] = d_lines_from_dbgi_key_voff(scratch.arena, &dbgi_key, voff);
      }
    }
    
    // rjf: add dasm dbgi key to relevant dbgis
    if(dasm_lines != 0)
    {
      di_key_list_push(scratch.arena, &code_slice_params.relevant_dbgi_keys, &dasm_dbgi_key);
    }
  }
  
  //////////////////////////////
  //- rjf: build container
  //
  UI_Box *container_box = &ui_nil_box;
  {
    ui_set_next_pref_width(ui_px(code_area_dim.x, 1));
    ui_set_next_pref_height(ui_px(code_area_dim.y, 1));
    ui_set_next_child_layout_axis(Axis2_Y);
    container_box = ui_build_box_from_stringf(UI_BoxFlag_Clip|
                                              UI_BoxFlag_Scroll|
                                              UI_BoxFlag_AllowOverflowX|
                                              UI_BoxFlag_AllowOverflowY,
                                              "###code_area");
  }
  
  //////////////////////////////
  //- rjf: cancelled search query -> center cursor
  //
  if(!search_query_is_active && cv->drifted_for_search)
  {
    cv->drifted_for_search = 0;
    cv->center_cursor = 1;
  }
  
  //////////////////////////////
  //- rjf: do searching operations
  //
  {
    //- rjf: find text (forward)
    if(cv->find_text_fwd.size != 0)
    {
      Temp scratch = scratch_begin(0, 0);
      B32 found = 0;
      B32 first = 1;
      S64 line_num_start = rd_regs()->cursor.line;
      S64 line_num_last = (S64)text_info->lines_count;
      for(S64 line_num = line_num_start;; first = 0)
      {
        // rjf: pop scratch
        temp_end(scratch);
        
        // rjf: gather line info
        String8 line_string = str8_substr(text_data, text_info->lines_ranges[line_num-1]);
        U64 search_start = 0;
        if(rd_regs()->cursor.line == line_num && first)
        {
          search_start = rd_regs()->cursor.column;
        }
        
        // rjf: search string
        U64 needle_pos = str8_find_needle(line_string, search_start, cv->find_text_fwd, StringMatchFlag_CaseInsensitive);
        if(needle_pos < line_string.size)
        {
          rd_regs()->cursor.line = line_num;
          rd_regs()->cursor.column = needle_pos+1;
          rd_regs()->mark = rd_regs()->cursor;
          found = 1;
          break;
        }
        
        // rjf: break if circled back around to cursor
        else if(line_num == line_num_start && !first)
        {
          break;
        }
        
        // rjf: increment
        line_num += 1;
        if(line_num > line_num_last)
        {
          line_num = 1;
        }
      }
      cv->center_cursor = found;
      if(found == 0)
      {
        log_user_errorf("Could not find \"%S\"", cv->find_text_fwd);
      }
      scratch_end(scratch);
    }
    
    //- rjf: find text (backward)
    if(cv->find_text_bwd.size != 0)
    {
      Temp scratch = scratch_begin(0, 0);
      B32 found = 0;
      B32 first = 1;
      S64 line_num_start = rd_regs()->cursor.line;
      S64 line_num_last = (S64)text_info->lines_count;
      for(S64 line_num = line_num_start;; first = 0)
      {
        // rjf: pop scratch
        temp_end(scratch);
        
        // rjf: gather line info
        String8 line_string = str8_substr(text_data, text_info->lines_ranges[line_num-1]);
        if(rd_regs()->cursor.line == line_num && first)
        {
          line_string = str8_prefix(line_string, rd_regs()->cursor.column-1);
        }
        
        // rjf: search string
        U64 next_needle_pos = line_string.size;
        for(U64 needle_pos = 0; needle_pos < line_string.size;)
        {
          needle_pos = str8_find_needle(line_string, needle_pos, cv->find_text_bwd, StringMatchFlag_CaseInsensitive);
          if(needle_pos < line_string.size)
          {
            next_needle_pos = needle_pos;
            needle_pos += 1;
          }
        }
        if(next_needle_pos < line_string.size)
        {
          rd_regs()->cursor.line = line_num;
          rd_regs()->cursor.column = next_needle_pos+1;
          rd_regs()->mark = rd_regs()->cursor;
          found = 1;
          break;
        }
        
        // rjf: break if circled back around to cursor line
        else if(line_num == line_num_start && !first)
        {
          break;
        }
        
        // rjf: increment
        line_num -= 1;
        if(line_num == 0)
        {
          line_num = line_num_last;
        }
      }
      cv->center_cursor = found;
      if(found == 0)
      {
        log_user_errorf("Could not find \"%S\"", cv->find_text_bwd);
      }
      scratch_end(scratch);
    }
    
    MemoryZeroStruct(&cv->find_text_fwd);
    MemoryZeroStruct(&cv->find_text_bwd);
    arena_clear(cv->find_text_arena);
  }
  
  //////////////////////////////
  //- rjf: do goto line
  //
  if(cv->goto_line_num != 0 && text_info->lines_count != 0)
  {
    S64 line_num = cv->goto_line_num;
    cv->goto_line_num = 0;
    line_num = Clamp(1, line_num, text_info->lines_count);
    rd_regs()->cursor = rd_regs()->mark = txt_pt(line_num, 1);
    cv->center_cursor = !cv->contain_cursor || (line_num < target_visible_line_num_range.min+4 || target_visible_line_num_range.max-4 < line_num);
  }
  
  //////////////////////////////
  //- rjf: do keyboard interaction
  //
  B32 snap[Axis2_COUNT] = {0};
  UI_Focus(UI_FocusKind_On)
  {
    if(ui_is_focus_active() && visible_line_num_range.max >= visible_line_num_range.min)
    {
      snap[Axis2_X] = snap[Axis2_Y] = rd_do_txt_controls(text_info, text_data, ClampBot(num_possible_visible_lines, 10) - 10, &rd_regs()->cursor, &rd_regs()->mark, &cv->preferred_column);
    }
  }
  
  //////////////////////////////
  //- rjf: build container contents
  //
  UI_Parent(container_box)
  {
    //- rjf: build fractional space
    container_box->view_off.x = container_box->view_off_target.x = scroll_pos.x.idx + scroll_pos.x.off;
    container_box->view_off.y = container_box->view_off_target.y = code_line_height*mod_f32(scroll_pos.y.off, 1.f) + code_line_height*(scroll_pos.y.off < 0) - code_line_height*(scroll_pos.y.off == -1.f && scroll_pos.y.idx == 1);
    
    //- rjf: build code slice
    RD_CodeSliceSignal sig = {0};
    UI_Focus(UI_FocusKind_On)
    {
      sig = rd_code_slicef(&code_slice_params, &rd_regs()->cursor, &rd_regs()->mark, &cv->preferred_column, "code_slice");
    }
    
    //- rjf: press code slice? -> focus panel
    if(ui_pressed(sig.base))
    {
      rd_cmd(RD_CmdKind_FocusPanel);
    }
    
    //- rjf: dragging & outside region? -> contain cursor
    if(ui_dragging(sig.base) && sig.base.event_flags == 0)
    {
      if(!contains_2f32(sig.base.box->rect, ui_mouse()))
      {
        cv->contain_cursor = 1;
      }
      else
      {
        snap[Axis2_X] = 1;
      }
    }
    
    //- rjf: ctrl+pressed? -> go to name
    if(ui_pressed(sig.base) && sig.base.event_flags & OS_Modifier_Ctrl)
    {
      ui_kill_action();
      rd_cmd(RD_CmdKind_GoToName, .string = txt_string_from_info_data_txt_rng(text_info, text_data, sig.mouse_expr_rng));
    }
    
    //- rjf: watch expr at mouse
    if(cv->watch_expr_at_mouse)
    {
      cv->watch_expr_at_mouse = 0;
      rd_cmd(RD_CmdKind_ToggleWatchExpression, .string = txt_string_from_info_data_txt_rng(text_info, text_data, sig.mouse_expr_rng));
    }
    
    //- rjf: selected text on single line, no query? -> set search text
    if(!txt_pt_match(rd_regs()->cursor, rd_regs()->mark) && rd_regs()->cursor.line == rd_regs()->mark.line && search_query.size == 0)
    {
      String8 text = txt_string_from_info_data_txt_rng(text_info, text_data, txt_rng(rd_regs()->cursor, rd_regs()->mark));
      rd_set_search_string(text);
    }
  }
  
  //////////////////////////////
  //- rjf: apply post-build view snapping rules
  //
  {
    // rjf: contain => snap
    if(cv->contain_cursor && text_info->lines_count != 0)
    {
      cv->contain_cursor = 0;
      snap[Axis2_X] = 1;
      snap[Axis2_Y] = 1;
    }
    
    // rjf: center cursor
    if(cv->center_cursor && text_info->lines_count != 0)
    {
      cv->center_cursor = 0;
      String8 cursor_line = str8_substr(text_data, text_info->lines_ranges[rd_regs()->cursor.line-1]);
      F32 cursor_advance = fnt_dim_from_tag_size_string(code_font, code_font_size, 0, code_tab_size, str8_prefix(cursor_line, rd_regs()->cursor.column-1)).x;
      
      // rjf: scroll x
      {
        S64 new_idx = (S64)(cursor_advance - code_area_dim.x/2);
        new_idx = Clamp(scroll_idx_rng[Axis2_X].min, new_idx, scroll_idx_rng[Axis2_X].max);
        ui_scroll_pt_target_idx(&scroll_pos.x, new_idx);
        snap[Axis2_X] = 0;
      }
      
      // rjf: scroll y
      {
        S64 new_idx = (rd_regs()->cursor.line-1) - num_possible_visible_lines/2 + 2;
        new_idx = Clamp(scroll_idx_rng[Axis2_Y].min, new_idx, scroll_idx_rng[Axis2_Y].max);
        ui_scroll_pt_target_idx(&scroll_pos.y, new_idx);
        snap[Axis2_Y] = 0;
      }
    }
    
    // rjf: snap in X
    if(snap[Axis2_X])
    {
      String8 cursor_line = str8_substr(text_data, text_info->lines_ranges[rd_regs()->cursor.line-1]);
      S64 cursor_off = (S64)(fnt_dim_from_tag_size_string(code_font, code_font_size, 0, code_tab_size, str8_prefix(cursor_line, rd_regs()->cursor.column-1)).x + priority_margin_width_px + catchall_margin_width_px + line_num_width_px);
      Rng1S64 visible_pixel_range =
      {
        scroll_pos.x.idx,
        scroll_pos.x.idx + (S64)code_area_dim.x,
      };
      Rng1S64 cursor_pixel_range =
      {
        cursor_off - (S64)(big_glyph_advance*4) - (S64)(priority_margin_width_px + catchall_margin_width_px + line_num_width_px),
        cursor_off + (S64)(big_glyph_advance*4),
      };
      S64 min_delta = Min(0, cursor_pixel_range.min - visible_pixel_range.min);
      S64 max_delta = Max(0, cursor_pixel_range.max - visible_pixel_range.max);
      S64 new_idx = scroll_pos.x.idx+min_delta+max_delta;
      new_idx = Clamp(scroll_idx_rng[Axis2_X].min, new_idx, scroll_idx_rng[Axis2_X].max);
      ui_scroll_pt_target_idx(&scroll_pos.x, new_idx);
    }
    
    // rjf: snap in Y
    if(snap[Axis2_Y])
    {
      Rng1S64 cursor_visibility_range = r1s64(rd_regs()->cursor.line-4, rd_regs()->cursor.line+4);
      cursor_visibility_range.min = ClampBot(0, cursor_visibility_range.min);
      cursor_visibility_range.max = ClampBot(0, cursor_visibility_range.max);
      S64 min_delta = Min(0, cursor_visibility_range.min-(target_visible_line_num_range.min));
      S64 max_delta = Max(0, cursor_visibility_range.max-(target_visible_line_num_range.min+num_possible_visible_lines));
      S64 new_idx = scroll_pos.y.idx+min_delta+max_delta;
      new_idx = Clamp(0, new_idx, (S64)text_info->lines_count-1);
      ui_scroll_pt_target_idx(&scroll_pos.y, new_idx);
    }
  }
  
  //////////////////////////////
  //- rjf: build horizontal scroll bar
  //
  {
    ui_set_next_fixed_x(0);
    ui_set_next_fixed_y(code_area_dim.y);
    ui_set_next_fixed_width(panel_box_dim.x - scroll_bar_dim);
    ui_set_next_fixed_height(scroll_bar_dim);
    {
      scroll_pos.x = ui_scroll_bar(Axis2_X,
                                   ui_px(scroll_bar_dim, 1.f),
                                   scroll_pos.x,
                                   scroll_idx_rng[Axis2_X],
                                   (S64)code_area_dim.x);
    }
  }
  
  //////////////////////////////
  //- rjf: build vertical scroll bar
  //
  {
    ui_set_next_fixed_x(code_area_dim.x);
    ui_set_next_fixed_y(0);
    ui_set_next_fixed_width(scroll_bar_dim);
    ui_set_next_fixed_height(panel_box_dim.y - scroll_bar_dim);
    {
      scroll_pos.y = ui_scroll_bar(Axis2_Y,
                                   ui_px(scroll_bar_dim, 1.f),
                                   scroll_pos.y,
                                   scroll_idx_rng[Axis2_Y],
                                   num_possible_visible_lines);
    }
  }
  
  //////////////////////////////
  //- rjf: top-level container interaction (scrolling)
  //
  {
    UI_Signal sig = ui_signal_from_box(container_box);
    if(sig.scroll.x != 0)
    {
      S64 new_idx = scroll_pos.x.idx+sig.scroll.x*big_glyph_advance;
      new_idx = clamp_1s64(scroll_idx_rng[Axis2_X], new_idx);
      ui_scroll_pt_target_idx(&scroll_pos.x, new_idx);
    }
    if(sig.scroll.y != 0)
    {
      S64 new_idx = scroll_pos.y.idx + sig.scroll.y;
      new_idx = clamp_1s64(scroll_idx_rng[Axis2_Y], new_idx);
      ui_scroll_pt_target_idx(&scroll_pos.y, new_idx);
    }
    ui_scroll_pt_clamp_idx(&scroll_pos.x, scroll_idx_rng[Axis2_X]);
    ui_scroll_pt_clamp_idx(&scroll_pos.y, scroll_idx_rng[Axis2_Y]);
    if(ui_mouse_over(sig))
    {
      for(UI_Event *evt = 0; ui_next_event(&evt);)
      {
        if(evt->kind == UI_EventKind_Scroll && evt->modifiers & OS_Modifier_Ctrl)
        {
          ui_eat_event(evt);
          if(evt->delta_2f32.y < 0)
          {
            rd_cmd(RD_CmdKind_IncCodeFontScale);
          }
          else if(evt->delta_2f32.y > 0)
          {
            rd_cmd(RD_CmdKind_DecCodeFontScale);
          }
        }
      }
    }
  }
  
  //////////////////////////////
  //- rjf: build result
  //
  RD_CodeViewBuildResult result = {0};
  {
    for(DI_KeyNode *n = code_slice_params.relevant_dbgi_keys.first; n != 0; n = n->next)
    {
      DI_Key copy = di_key_copy(arena, &n->v);
      di_key_list_push(arena, &result.dbgi_keys, &copy);
    }
  }
  
  //////////////////////////////
  //- rjf: store state
  //
  rd_store_view_scroll_pos(scroll_pos);
  
  txt_scope_close(txt_scope);
  hs_scope_close(hs_scope);
  scratch_end(scratch);
  ProfEnd();
  return result;
}

////////////////////////////////
//~ rjf: Watch Views

//- rjf: index -> column

internal RD_WatchViewColumn *
rd_watch_view_column_from_x(RD_WatchViewState *wv, S64 index)
{
  RD_WatchViewColumn *result = wv->first_column;
  S64 idx = 0;
  for(RD_WatchViewColumn *c = wv->first_column; c != 0; c = c->next, idx += 1)
  {
    result = c;
    if(idx == index)
    {
      break;
    }
  }
  return result;
}

//- rjf: watch view points <-> table coordinates

internal B32
rd_watch_view_point_match(RD_WatchViewPoint a, RD_WatchViewPoint b)
{
  return (a.x == b.x &&
          ev_key_match(a.parent_key, b.parent_key) &&
          ev_key_match(a.key, b.key));
}

internal RD_WatchViewPoint
rd_watch_view_point_from_tbl(EV_BlockList *blocks, Vec2S64 tbl)
{
  RD_WatchViewPoint pt = zero_struct;
  pt.x           = tbl.x;
  pt.key         = ev_key_from_block_list_row_num(blocks, tbl.y);
  pt.parent_key  = ev_parent_key_from_block_list_row_num(blocks, tbl.y);
  return pt;
}

internal Vec2S64
rd_tbl_from_watch_view_point(EV_BlockList *blocks, RD_WatchViewPoint pt)
{
  Vec2S64 tbl = {0};
  tbl.x = pt.x;
  tbl.y = ev_row_num_from_block_list_key(blocks, pt.key);
  return tbl;
}

//- rjf: table coordinates -> strings

internal String8
rd_string_from_eval_viz_row_column(Arena *arena, EV_View *ev, EV_Row *row, RD_WatchViewColumn *col, B32 editable, U32 default_radix, FNT_Tag font, F32 font_size, F32 max_size_px)
{
  String8 result = {0};
  EV_ViewRuleList *view_rules = row->view_rules;
  if(col->view_rule_size != 0)
  {
    view_rules = ev_view_rule_list_copy(arena, row->view_rules);
    ev_view_rule_list_push_string(arena, view_rules, str8(col->view_rule_buffer, col->view_rule_size));
  }
  switch(col->kind)
  {
    default:{}break;
    case RD_WatchViewColumnKind_Expr:
    {
      result = ev_expr_string_from_row(arena, row);
    }break;
    case RD_WatchViewColumnKind_Value:
    {
      E_Eval eval = e_eval_from_expr(arena, row->expr);
      result = rd_value_string_from_eval(arena, !editable * EV_StringFlag_ReadOnlyDisplayRules, default_radix, font, font_size, max_size_px, eval, row->member, view_rules);
    }break;
    case RD_WatchViewColumnKind_Type:
    {
      E_IRTreeAndType irtree = e_irtree_and_type_from_expr(arena, row->expr);
      E_TypeKey type_key = irtree.type_key;
      result = !e_type_key_match(type_key, e_type_key_zero()) ? e_type_string_from_key(arena, type_key) : str8_zero();
      result = str8_skip_chop_whitespace(result);
    }break;
    case RD_WatchViewColumnKind_ViewRule:
    {
      result = ev_view_rule_from_key(ev, row->key);
    }break;
    case RD_WatchViewColumnKind_Module:
    {
      E_Eval eval = e_eval_from_expr(arena, row->expr);
      E_Eval value_eval = e_value_eval_from_eval(eval);
      CTRL_Entity *process = ctrl_entity_from_handle(d_state->ctrl_entity_store, rd_regs()->process);
      CTRL_Entity *module = ctrl_module_from_process_vaddr(process, value_eval.value.u64);
      result = str8_skip_last_slash(module->string);
    }break;
    case RD_WatchViewColumnKind_Member:
    {
      Temp scratch = scratch_begin(&arena, 1);
      String8 access_string = str8(col->string_buffer, col->string_size);
      String8List accesses = str8_split(scratch.arena, access_string, (U8 *)".", 1, 0);
      E_Expr *expr = row->expr;
      for(String8Node *n = accesses.first; n != 0; n = n->next)
      {
        expr = e_expr_ref_member_access(arena, expr, n->string);
      }
      E_Eval eval = e_eval_from_expr(arena, expr);
      result = rd_value_string_from_eval(arena, !editable * EV_StringFlag_ReadOnlyDisplayRules, default_radix, font, font_size, max_size_px, eval, row->member, view_rules);
      scratch_end(scratch);
    }break;
  }
  if(col->dequote_string &&
     result.size >= 2 &&
     result.str[0] == '"' &&
     result.str[result.size-1] == '"')
  {
    result = str8_skip(str8_chop(result, 1), 1);
    result = raw_from_escaped_str8(arena, result);
  }
  if(col->rangify_braces && result.size >= 2 &&
     result.str[0] == '{' &&
     result.str[result.size-1] == '}')
  {
    result = push_str8_copy(arena, result);
    result.str[0] = '[';
    result.str[result.size-1] = ')';
  }
  return result;
}

//- rjf: table coordinates -> text edit state

internal RD_WatchViewTextEditState *
rd_watch_view_text_edit_state_from_pt(RD_WatchViewState *wv, RD_WatchViewPoint pt)
{
  RD_WatchViewTextEditState *result = &wv->dummy_text_edit_state;
  if(wv->text_edit_state_slots_count != 0 && wv->text_editing != 0)
  {
    U64 hash = ev_hash_from_key(pt.key);
    U64 slot_idx = hash%wv->text_edit_state_slots_count;
    for(RD_WatchViewTextEditState *s = wv->text_edit_state_slots[slot_idx]; s != 0; s = s->pt_hash_next)
    {
      if(rd_watch_view_point_match(pt, s->pt))
      {
        result = s;
        break;
      }
    }
  }
  return result;
}

//- rjf: watch view column state mutation

internal RD_WatchViewColumn *
rd_watch_view_column_alloc_(RD_WatchViewState *wv, RD_WatchViewColumnKind kind, F32 pct, RD_WatchViewColumnParams *params)
{
  if(!wv->free_column)
  {
    RD_WatchViewColumn *col = push_array(wv->column_arena, RD_WatchViewColumn, 1);
    SLLStackPush(wv->free_column, col);
  }
  RD_WatchViewColumn *col = wv->free_column;
  SLLStackPop(wv->free_column);
  DLLPushBack(wv->first_column, wv->last_column, col);
  wv->column_count += 1;
  col->kind = kind;
  col->pct = pct;
  col->string_size = Min(sizeof(col->string_buffer), params->string.size);
  MemoryCopy(col->string_buffer, params->string.str, col->string_size);
  col->display_string_size = Min(sizeof(col->display_string_buffer), params->display_string.size);
  MemoryCopy(col->display_string_buffer, params->display_string.str, col->display_string_size);
  col->view_rule_size = Min(sizeof(col->view_rule_buffer), params->view_rule.size);
  MemoryCopy(col->view_rule_buffer, params->view_rule.str, col->view_rule_size);
  col->is_non_code = params->is_non_code;
  col->dequote_string = params->dequote_string;
  col->rangify_braces = params->rangify_braces;
  return col;
}

internal void
rd_watch_view_column_release(RD_WatchViewState *wv, RD_WatchViewColumn *col)
{
  DLLRemove(wv->first_column, wv->last_column, col);
  SLLStackPush(wv->free_column, col);
  wv->column_count -= 1;
}

//- rjf: watch view main hooks

internal void
rd_watch_view_init(RD_WatchViewState *ewv)
{
  if(ewv->initialized == 0)
  {
    ewv->initialized = 1;
    ewv->column_arena = rd_push_view_arena();
    ewv->text_edit_arena = rd_push_view_arena();
  }
}

internal void
rd_watch_view_build(RD_WatchViewState *ewv, RD_WatchViewFlags flags, String8 root_expr, String8 root_view_rule, B32 modifiable, U32 default_radix, Rng2F32 rect)
{
  ProfBeginFunction();
  DI_Scope *di_scope = di_scope_open();
  FZY_Scope *fzy_scope = fzy_scope_open();
  Temp scratch = scratch_begin(0, 0);
  UI_ScrollPt2 scroll_pos = rd_view_scroll_pos();
  
  //////////////////////////////
  //- rjf: unpack arguments
  //
  EV_ViewRuleList *top_level_view_rules = ev_view_rule_list_from_string(scratch.arena, root_view_rule);
  String8 eval_view_key_string = push_str8f(scratch.arena, "eval_view_watch_%p", ewv);
  EV_View *eval_view = rd_ev_view_from_key(d_hash_from_string(eval_view_key_string));
  String8 filter = rd_view_filter();
  F32 row_height_px = floor_f32(ui_top_font_size()*2.5f);
  S64 num_possible_visible_rows = (S64)(dim_2f32(rect).y/row_height_px);
  RD_EntityKind mutable_entity_kind = RD_EntityKind_Nil;
  F32 row_string_max_size_px = dim_2f32(rect).x;
  
  //////////////////////////////
  //- rjf: root-level view rule which has a ui hook? call into that to build the UI
  //
  B32 is_top_level_hook = 0;
  {
    RD_ViewRuleInfo *hook_rule_info = 0;
    MD_Node *hook_rule_root = &md_nil_node;
    for(EV_ViewRuleNode *n = top_level_view_rules->first; n != 0; n = n->next)
    {
      RD_ViewRuleInfo *rule_info = rd_view_rule_info_from_string(n->v.root->string);
      if(rule_info != &rd_nil_view_rule_info && rule_info->ui != 0)
      {
        hook_rule_info = rule_info;
        hook_rule_root = n->v.root;
        break;
      }
    }
    if(hook_rule_info)
    {
      hook_rule_info->ui(root_expr, hook_rule_root, rect);
      is_top_level_hook = 1;
    }
  }
  
  //////////////////////////////
  //- rjf: determine autocompletion string
  //
  String8 autocomplete_hint_string = {0};
  if(!is_top_level_hook)
  {
    for(UI_Event *evt = 0; ui_next_event(&evt);)
    {
      if(evt->kind == UI_EventKind_AutocompleteHint)
      {
        autocomplete_hint_string = evt->string;
        break;
      }
    }
  }
  
  //////////////////////////////
  //- rjf: consume events & perform navigations/edits - calculate state
  //
  typedef struct FrameRow FrameRow;
  struct FrameRow
  {
    void *regs;
    RDI_Parsed *rdi;
    RDI_Procedure *procedure;
    RDI_InlineSite *inline_site;
    U64 unwind_idx;
    U64 inline_depth;
  };
  U64 frame_rows_count = 0;
  FrameRow *frame_rows = 0;
  EV_BlockList blocks = {0};
  UI_ScrollListRowBlockArray row_blocks = {0};
  Vec2S64 cursor_tbl = {0};
  Vec2S64 mark_tbl = {0};
  Rng2S64 selection_tbl = {0};
  if(!is_top_level_hook) UI_Focus(UI_FocusKind_On)
  {
    B32 state_dirty = 1;
    B32 snap_to_cursor = 0;
    B32 cursor_dirty__tbl = 0;
    B32 take_autocomplete = 0;
    for(UI_Event *event = 0;;)
    {
      //////////////////////////
      //- rjf: state -> viz blocks
      //
      if(state_dirty)
      {
        MemoryZeroStruct(&blocks);
        mutable_entity_kind = RD_EntityKind_Watch;
        EV_Key root_parent_key = ev_key_make(5381, 0);
        EV_Key root_key = ev_key_make(ev_hash_from_key(root_parent_key), 1);
        ev_key_set_expansion(eval_view, root_parent_key, root_key, 1);
        blocks = ev_block_list_from_view_expr_keys(scratch.arena, eval_view, filter, top_level_view_rules, root_expr, root_parent_key, root_key, -1);
        blocks.first = blocks.first->next;
        blocks.count -= 1;
        blocks.total_visual_row_count -= 1;
        blocks.total_semantic_row_count -= 1;
        
#if 0
        RDI_SectionKind fzy_target = RDI_SectionKind_UDTs;
        switch(ewv->fill_kind)
        {
          ////////////////////////////
          //- rjf: watch fill -> build blocks from top-level watch expressions
          //
          default:
          case RD_WatchViewFillKind_Watch:
          {
            mutable_entity_kind = RD_EntityKind_Watch;
            RD_EntityList watches = rd_query_cached_entity_list_with_kind(mutable_entity_kind);
            for(RD_EntityNode *n = watches.first; n != 0; n = n->next)
            {
              RD_Entity *watch = n->entity;
              if(watch->flags & RD_EntityFlag_MarkedForDeletion)
              {
                continue;
              }
              EV_Key parent_key = rd_parent_ev_key_from_entity(watch);
              EV_Key key = rd_ev_key_from_entity(watch);
              RD_Entity *view_rule = rd_entity_child_from_kind(watch, RD_EntityKind_ViewRule);
              ev_key_set_view_rule(eval_view, key, view_rule->string);
              String8 expr_string = watch->string;
              EV_BlockList watch_blocks = ev_block_list_from_view_expr_keys(scratch.arena, eval_view, filter, &top_level_view_rules, expr_string, parent_key, key, 0);
              FuzzyMatchRangeList matches = fuzzy_match_find(scratch.arena, filter, expr_string);
              if(watch_blocks.total_semantic_row_count > 1 || matches.count == matches.needle_part_count)
              {
                ev_block_list_concat__in_place(&blocks, &watch_blocks);
              }
            }
          }break;
          
          ////////////////////////////
          //- rjf: breakpoint fill -> build blocks from all breakpoints
          //
          case RD_WatchViewFillKind_Breakpoints:
          {
            E_TypeKey meta_eval_type = e_type_key_cons_base(type(CTRL_MetaEval));
            mutable_entity_kind = RD_EntityKind_Breakpoint;
            ev_view_rule_list_push_string(scratch.arena, &top_level_view_rules, str8_lit("no_addr"));
            RD_EntityList bps = rd_query_cached_entity_list_with_kind(mutable_entity_kind);
            for(RD_EntityNode *n = bps.first; n != 0; n = n->next)
            {
              RD_Entity *bp = n->entity;
              if(bp->flags & RD_EntityFlag_MarkedForDeletion)
              {
                continue;
              }
              EV_Key parent_key = rd_parent_ev_key_from_entity(bp);
              EV_Key key = rd_ev_key_from_entity(bp);
              String8 title = rd_display_string_from_entity(scratch.arena, bp);
              FuzzyMatchRangeList matches = fuzzy_match_find(scratch.arena, filter, title);
              if(matches.count == matches.needle_part_count)
              {
                String8 expr_string = push_str8f(scratch.arena, "$%I64u", bp->id);
                EV_BlockList watch_blocks = ev_block_list_from_view_expr_keys(scratch.arena, eval_view, str8_zero(), &top_level_view_rules, expr_string, parent_key, key, 0);
                ev_block_list_concat__in_place(&blocks, &watch_blocks);
              }
            }
          }break;
          
          ////////////////////////////
          //- rjf: watch pin fill -> build blocks from all watch pins
          //
          case RD_WatchViewFillKind_WatchPins:
          {
            mutable_entity_kind = RD_EntityKind_WatchPin;
            RD_EntityList wps = rd_query_cached_entity_list_with_kind(mutable_entity_kind);
            for(RD_EntityNode *n = wps.first; n != 0; n = n->next)
            {
              RD_Entity *wp = n->entity;
              if(wp->flags & RD_EntityFlag_MarkedForDeletion)
              {
                continue;
              }
              EV_Key parent_key = rd_parent_ev_key_from_entity(wp);
              EV_Key key = rd_ev_key_from_entity(wp);
              String8 title = wp->string;
              FuzzyMatchRangeList matches = fuzzy_match_find(scratch.arena, filter, title);
              if(matches.count == matches.needle_part_count)
              {
                String8 expr_string = push_str8f(scratch.arena, "$%I64u", wp->id);
                EV_BlockList watch_blocks = ev_block_list_from_view_expr_keys(scratch.arena, eval_view, str8_zero(), &top_level_view_rules, expr_string, parent_key, key, 0);
                ev_block_list_concat__in_place(&blocks, &watch_blocks);
              }
            }
          }break;
          
          ////////////////////////////
          //- rjf: call stack fill -> build blocks for each call frame
          //
          case RD_WatchViewFillKind_CallStack:
          {
            //- rjf: produce per-row info for callstack
            CTRL_Entity *thread = ctrl_entity_from_handle(d_state->ctrl_entity_store, rd_regs()->thread);
            CTRL_Entity *process = ctrl_entity_ancestor_from_kind(thread, CTRL_EntityKind_Process);
            CTRL_Unwind base_unwind = d_query_cached_unwind_from_thread(thread);
            D_Unwind rich_unwind = d_unwind_from_ctrl_unwind(scratch.arena, di_scope, process, &base_unwind);
            frame_rows_count = rich_unwind.frames.total_frame_count;
            frame_rows = push_array(scratch.arena, FrameRow, frame_rows_count);
            {
              U64 concrete_frame_idx = 0;
              U64 row_idx = 0;
              for(;concrete_frame_idx < rich_unwind.frames.concrete_frame_count; concrete_frame_idx += 1, row_idx += 1)
              {
                D_UnwindFrame *f = &rich_unwind.frames.v[concrete_frame_idx];
                
                // rjf: fill frame_rows for inline frames
                {
                  U64 inline_unwind_idx = 0;
                  for(D_UnwindInlineFrame *fin = f->last_inline_frame; fin != 0; fin = fin->prev, row_idx += 1, inline_unwind_idx += 1)
                  {
                    frame_rows[row_idx].regs         = f->regs;
                    frame_rows[row_idx].rdi          = f->rdi;
                    frame_rows[row_idx].inline_site  = fin->inline_site;
                    frame_rows[row_idx].unwind_idx   = concrete_frame_idx;
                    frame_rows[row_idx].inline_depth = f->inline_frame_count - inline_unwind_idx;
                  }
                }
                
                // rjf: fill row for concrete frame
                {
                  frame_rows[row_idx].regs      = f->regs;
                  frame_rows[row_idx].rdi       = f->rdi;
                  frame_rows[row_idx].procedure = f->procedure;
                  frame_rows[row_idx].unwind_idx= concrete_frame_idx;
                }
              }
            }
            
            //- rjf: build viz blocks
            for(U64 row_idx = 0; row_idx < frame_rows_count; row_idx += 1)
            {
              FrameRow *row = &frame_rows[row_idx];
              EV_Key parent_key = ev_key_make(5381, 0);
              EV_Key key = ev_key_make(ev_hash_from_key(parent_key), row_idx+1);
              EV_Block *block = ev_block_begin(scratch.arena, EV_BlockKind_Root, parent_key, key, 0);
              {
                E_TypeKey type_key = zero_struct;
                if(row->procedure != 0)
                {
                  type_key = e_type_key_ext(E_TypeKind_Function, row->procedure->type_idx, e_parse_ctx_module_idx_from_rdi(row->rdi));
                }
                else if(row->inline_site != 0)
                {
                  type_key = e_type_key_ext(E_TypeKind_Function, row->inline_site->type_idx, e_parse_ctx_module_idx_from_rdi(row->rdi));
                }
                U64 row_vaddr = regs_rip_from_arch_block(thread->arch, row->regs);
                E_OpList ops = {0};
                e_oplist_push_op(scratch.arena, &ops, RDI_EvalOp_ConstU64, e_value_u64(row_vaddr));
                String8 bytecode = e_bytecode_from_oplist(scratch.arena, &ops);
                E_Expr *expr = e_push_expr(scratch.arena, E_ExprKind_LeafBytecode, 0);
                expr->bytecode = bytecode;
                expr->mode = E_Mode_Value;
                expr->space = rd_eval_space_from_ctrl_entity(process, RD_EvalSpaceKind_CtrlEntity);
                expr->type_key = type_key;
                block->expr = expr;
                block->visual_idx_range   = r1u64(row_idx, row_idx+1);
                block->semantic_idx_range = r1u64(row_idx, row_idx+1);
              }
              ev_block_end(&blocks, block);
            }
          }break;
          
          ////////////////////////////
          //- rjf: registers fill -> build blocks via iterating all registers/aliases as root-level expressions
          //
          case RD_WatchViewFillKind_Registers:
          {
            CTRL_Entity *thread = ctrl_entity_from_handle(d_state->ctrl_entity_store, rd_regs()->thread);
            Arch arch = thread->arch;
            U64 reg_count = regs_reg_code_count_from_arch(arch);
            String8 *reg_strings = regs_reg_code_string_table_from_arch(arch);
            U64 alias_count = regs_alias_code_count_from_arch(arch);
            String8 *alias_strings = regs_alias_code_string_table_from_arch(arch);
            U64 num = 1;
            for(U64 reg_idx = 1; reg_idx < reg_count; reg_idx += 1, num += 1)
            {
              String8 root_expr_string = push_str8f(scratch.arena, "reg:%S", reg_strings[reg_idx]);
              FuzzyMatchRangeList matches = fuzzy_match_find(scratch.arena, filter, root_expr_string);
              if(matches.count == matches.needle_part_count)
              {
                EV_Key parent_key = ev_key_make(5381, 0);
                EV_Key key = ev_key_make(ev_hash_from_key(parent_key), num);
                EV_BlockList root_blocks = ev_block_list_from_view_expr_keys(scratch.arena, eval_view, str8_zero(), &top_level_view_rules, root_expr_string, parent_key, key, 0);
                ev_block_list_concat__in_place(&blocks, &root_blocks);
              }
            }
            for(U64 alias_idx = 1; alias_idx < alias_count; alias_idx += 1, num += 1)
            {
              String8 root_expr_string = push_str8f(scratch.arena, "reg:%S", alias_strings[alias_idx]);
              FuzzyMatchRangeList matches = fuzzy_match_find(scratch.arena, filter, root_expr_string);
              if(matches.count == matches.needle_part_count)
              {
                EV_Key parent_key = ev_key_make(5381, 0);
                EV_Key key = ev_key_make(ev_hash_from_key(parent_key), num);
                EV_BlockList root_blocks = ev_block_list_from_view_expr_keys(scratch.arena, eval_view, str8_zero(), &top_level_view_rules, root_expr_string, parent_key, key, 0);
                ev_block_list_concat__in_place(&blocks, &root_blocks);
              }
            }
          }break;
          
          ////////////////////////////
          //- rjf: locals fill -> build blocks via iterating all locals as root-level expressions
          //
          case RD_WatchViewFillKind_Locals:
          {
            E_String2NumMapNodeArray nodes = e_string2num_map_node_array_from_map(scratch.arena, e_parse_ctx->locals_map);
            e_string2num_map_node_array_sort__in_place(&nodes);
            for(U64 idx = 0; idx < nodes.count; idx += 1)
            {
              E_String2NumMapNode *n = nodes.v[idx];
              String8 root_expr_string = n->string;
              FuzzyMatchRangeList matches = fuzzy_match_find(scratch.arena, filter, root_expr_string);
              if(matches.count == matches.needle_part_count)
              {
                EV_Key parent_key = ev_key_make(5381, 0);
                EV_Key key = ev_key_make(ev_hash_from_key(parent_key), idx+1);
                EV_BlockList root_blocks = ev_block_list_from_view_expr_keys(scratch.arena, eval_view, str8_zero(), &top_level_view_rules, root_expr_string, parent_key, key, 0);
                ev_block_list_concat__in_place(&blocks, &root_blocks);
              }
            }
          }break;
          
          ////////////////////////////
          //- rjf: debug info table fill -> build split debug info table blocks
          //
          case RD_WatchViewFillKind_Globals:      fzy_target = RDI_SectionKind_GlobalVariables; goto dbgi_table;
          case RD_WatchViewFillKind_ThreadLocals: fzy_target = RDI_SectionKind_ThreadVariables; goto dbgi_table;
          case RD_WatchViewFillKind_Types:        fzy_target = RDI_SectionKind_UDTs;            goto dbgi_table;
          case RD_WatchViewFillKind_Procedures:   fzy_target = RDI_SectionKind_Procedures;      goto dbgi_table;
          dbgi_table:;
          {
            U64 endt_us = os_now_microseconds()+200;
            
            //- rjf: unpack context
            DI_KeyList dbgi_keys_list = d_push_active_dbgi_key_list(scratch.arena);
            DI_KeyArray dbgi_keys = di_key_array_from_list(scratch.arena, &dbgi_keys_list);
            U64 rdis_count = dbgi_keys.count;
            RDI_Parsed **rdis = push_array(scratch.arena, RDI_Parsed *, rdis_count);
            for(U64 idx = 0; idx < rdis_count; idx += 1)
            {
              rdis[idx] = di_rdi_from_key(di_scope, &dbgi_keys.v[idx], endt_us);
            }
            
            //- rjf: calculate top-level keys, expand root-level, grab root expansion node
            EV_Key parent_key = ev_key_make(5381, 0);
            EV_Key root_key = ev_key_make(ev_hash_from_key(parent_key), 0);
            ev_key_set_expansion(eval_view, ev_key_zero(), parent_key, 1);
            EV_ExpandNode *root_node = ev_expand_node_from_key(eval_view, parent_key);
            
            //- rjf: query all filtered items from dbgi searching system
            U128 fuzzy_search_key = {rd_regs()->view.u64[0], rd_regs()->view.u64[1]};
            B32 items_stale = 0;
            FZY_Params params = {fzy_target, dbgi_keys};
            FZY_ItemArray items = fzy_items_from_key_params_query(fzy_scope, fuzzy_search_key, &params, filter, endt_us, &items_stale);
            if(items_stale)
            {
              rd_request_frame();
            }
            
            //- rjf: gather unsorted child expansion keys
            //
            // Nodes are sorted in the underlying expansion tree data structure, but
            // ONLY by THEIR ORDER IN THE UNDERLYING DEBUG INFO TABLE. This is
            // because debug info watch rows use the DEBUG INFO TABLE INDEX to form
            // their key - this provides more stable/predictable behavior as rows
            // are reordered, filtered, and shuffled around, as the user filters.
            //
            // When we actually build viz blocks, however, we want to produce viz
            // blocks BY THE ORDER OF SUB-EXPANSIONS IN THE FILTERED ITEM ARRAY
            // SPACE, so that all of the expansions come out in the right order.
            //
            EV_Key *sub_expand_keys = 0;
            U64 *sub_expand_item_idxs = 0;
            U64 sub_expand_keys_count = 0;
            {
              for(EV_ExpandNode *child = root_node->first; child != 0; child = child->next)
              {
                sub_expand_keys_count += 1;
              }
              sub_expand_keys = push_array(scratch.arena, EV_Key, sub_expand_keys_count);
              sub_expand_item_idxs = push_array(scratch.arena, U64, sub_expand_keys_count);
              U64 idx = 0;
              for(EV_ExpandNode *child = root_node->first; child != 0; child = child->next)
              {
                U64 item_num = fzy_item_num_from_array_element_idx__linear_search(&items, child->key.child_num);
                if(item_num != 0)
                {
                  sub_expand_keys[idx] = child->key;
                  sub_expand_item_idxs[idx] = item_num-1;
                  idx += 1;
                }
                else
                {
                  sub_expand_keys_count -= 1;
                }
              }
            }
            
            //- rjf: sort child expansion keys
            {
              for(U64 idx1 = 0; idx1 < sub_expand_keys_count; idx1 += 1)
              {
                U64 min_idx2 = 0;
                U64 min_item_idx = sub_expand_item_idxs[idx1];
                for(U64 idx2 = idx1+1; idx2 < sub_expand_keys_count; idx2 += 1)
                {
                  if(sub_expand_item_idxs[idx2] < min_item_idx)
                  {
                    min_idx2 = idx2;
                    min_item_idx = sub_expand_item_idxs[idx2];
                  }
                }
                if(min_idx2 != 0)
                {
                  Swap(EV_Key, sub_expand_keys[idx1], sub_expand_keys[min_idx2]);
                  Swap(U64, sub_expand_item_idxs[idx1], sub_expand_item_idxs[min_idx2]);
                }
              }
            }
            
            //- rjf: build blocks for all table items, split by sorted sub-expansions
            EV_Block *last_vb = ev_block_begin(scratch.arena, EV_BlockKind_DebugInfoTable, parent_key, root_key, 0);
            {
              last_vb->visual_idx_range = last_vb->semantic_idx_range = r1u64(0, items.count);
              last_vb->fzy_target = fzy_target;
              last_vb->fzy_backing_items = items;
            }
            for(U64 sub_expand_idx = 0; sub_expand_idx < sub_expand_keys_count; sub_expand_idx += 1)
            {
              FZY_Item *item = &items.v[sub_expand_item_idxs[sub_expand_idx]];
              E_Expr *child_expr = ev_expr_from_block_index(scratch.arena, last_vb, sub_expand_item_idxs[sub_expand_idx]);
              
              // rjf: form split: truncate & complete last block; begin next block
              last_vb = ev_block_split_and_continue(scratch.arena, &blocks, last_vb, sub_expand_item_idxs[sub_expand_idx]);
              
              // rjf: build child view rules
              EV_ViewRuleList *child_view_rules = ev_view_rule_list_from_inheritance(scratch.arena, &top_level_view_rules);
              {
                String8 view_rule_string = ev_view_rule_from_key(eval_view, sub_expand_keys[sub_expand_idx]);
                if(view_rule_string.size != 0)
                {
                  ev_view_rule_list_push_string(scratch.arena, child_view_rules, view_rule_string);
                }
              }
              
              // rjf: recurse for child
              ev_append_expr_blocks__rec(scratch.arena, eval_view, str8_zero(), parent_key, sub_expand_keys[sub_expand_idx], str8_zero(), child_expr, child_view_rules, 0, &blocks);
            }
            ev_block_end(&blocks, last_vb);
          }break;
        }
#endif
      }
      
      //////////////////////////
      //- rjf: does this eval watch view allow mutation? -> add extra block for editable empty row
      //
      EV_Key empty_row_parent_key = ev_key_make(max_U64, max_U64);
      EV_Key empty_row_key = ev_key_make(ev_hash_from_key(empty_row_parent_key), 1);
      if(state_dirty && modifiable && filter.size == 0)
      {
        EV_Block *b = ev_block_begin(scratch.arena, EV_BlockKind_Null, empty_row_parent_key, empty_row_key, 0);
        b->visual_idx_range = b->semantic_idx_range = r1u64(0, 1);
        ev_block_end(&blocks, b);
      }
      
      //////////////////////////
      //- rjf: viz blocks -> ui row blocks
      //
      {
        UI_ScrollListRowBlockChunkList row_block_chunks = {0};
        for(EV_BlockNode *n = blocks.first; n != 0; n = n->next)
        {
          EV_Block *vb = &n->v;
          UI_ScrollListRowBlock block = {0};
          block.row_count = dim_1u64(vb->visual_idx_range);
          block.item_count = dim_1u64(vb->semantic_idx_range);
          ui_scroll_list_row_block_chunk_list_push(scratch.arena, &row_block_chunks, 256, &block);
        }
        row_blocks = ui_scroll_list_row_block_array_from_chunk_list(scratch.arena, &row_block_chunks);
      }
      
      //////////////////////////
      //- rjf: conclude state update
      //
      if(state_dirty)
      {
        state_dirty = 0;
      }
      
      //////////////////////////////
      //- rjf: 2D table coordinates * blocks -> stable cursor state
      //
      if(cursor_dirty__tbl)
      {
        cursor_dirty__tbl = 0;
        struct
        {
          RD_WatchViewPoint *pt_state;
          Vec2S64 pt_tbl;
        }
        points[] =
        {
          {&ewv->cursor, cursor_tbl},
          {&ewv->mark, mark_tbl},
        };
        for(U64 point_idx = 0; point_idx < ArrayCount(points); point_idx += 1)
        {
          EV_Key last_key = points[point_idx].pt_state->key;
          EV_Key last_parent_key = points[point_idx].pt_state->parent_key;
          points[point_idx].pt_state[0] = rd_watch_view_point_from_tbl(&blocks, points[point_idx].pt_tbl);
          if(ev_key_match(ev_key_zero(), points[point_idx].pt_state->key))
          {
            points[point_idx].pt_state->key = last_parent_key;
            EV_ExpandNode *node = ev_expand_node_from_key(eval_view, last_parent_key);
            for(EV_ExpandNode *n = node; n != 0; n = n->parent)
            {
              points[point_idx].pt_state->key = n->key;
              if(n->expanded == 0)
              {
                break;
              }
            }
          }
          if(point_idx == 0 &&
             (!ev_key_match(ewv->cursor.key, last_key) ||
              !ev_key_match(ewv->cursor.parent_key, last_parent_key)))
          {
            ewv->text_editing = 0;
          }
        }
        ewv->next_cursor = ewv->cursor;
        ewv->next_mark = ewv->mark;
      }
      
      //////////////////////////
      //- rjf: stable cursor state * blocks -> 2D table coordinates
      //
      {
        cursor_tbl = rd_tbl_from_watch_view_point(&blocks, ewv->cursor);
        mark_tbl = rd_tbl_from_watch_view_point(&blocks, ewv->mark);
        selection_tbl = r2s64p(Min(cursor_tbl.x, mark_tbl.x), Min(cursor_tbl.y, mark_tbl.y),
                               Max(cursor_tbl.x, mark_tbl.x), Max(cursor_tbl.y, mark_tbl.y));
      }
      
      //////////////////////////
      //- rjf: [table] snap to cursor
      //
      if(snap_to_cursor)
      {
        Rng1S64 item_range = r1s64(0, 1 + blocks.total_visual_row_count);
        Rng1S64 scroll_row_idx_range = r1s64(item_range.min, ClampBot(item_range.min, item_range.max-1));
        S64 cursor_item_idx = cursor_tbl.y-1;
        if(item_range.min <= cursor_item_idx && cursor_item_idx <= item_range.max)
        {
          UI_ScrollPt *scroll_pt = &scroll_pos.y;
          
          //- rjf: compute visible row range
          Rng1S64 visible_row_range = r1s64(scroll_pt->idx + 0 - !!(scroll_pt->off < 0),
                                            scroll_pt->idx + 0 + num_possible_visible_rows + 1);
          
          //- rjf: compute cursor row range from cursor item
          Rng1S64 cursor_visibility_row_range = {0};
          if(row_blocks.count == 0)
          {
            cursor_visibility_row_range = r1s64(cursor_item_idx-1, cursor_item_idx+3);
          }
          else
          {
            cursor_visibility_row_range.min = (S64)ui_scroll_list_row_from_item(&row_blocks, (U64)cursor_item_idx);
            cursor_visibility_row_range.max = cursor_visibility_row_range.min + 4;
          }
          
          //- rjf: compute deltas & apply
          S64 min_delta = Min(0, cursor_visibility_row_range.min-visible_row_range.min);
          S64 max_delta = Max(0, cursor_visibility_row_range.max-visible_row_range.max);
          S64 new_idx = scroll_pt->idx+min_delta+max_delta;
          new_idx = clamp_1s64(scroll_row_idx_range, new_idx);
          ui_scroll_pt_target_idx(scroll_pt, new_idx);
        }
      }
      
      //////////////////////////////
      //- rjf: apply cursor/mark rugpull change
      //
      B32 cursor_rugpull = 0;
      if(!rd_watch_view_point_match(ewv->cursor, ewv->next_cursor))
      {
        cursor_rugpull = 1;
        ewv->cursor = ewv->next_cursor;
        ewv->mark = ewv->next_mark;
      }
      
      //////////////////////////
      //- rjf: grab next event, if any - otherwise exit the loop, as we now have
      // the most up-to-date state
      //
      B32 next_event_good = ui_next_event(&event);
      if(!cursor_rugpull && (!next_event_good || !ui_is_focus_active()))
      {
        break;
      }
      UI_Event dummy_evt = zero_struct;
      UI_Event *evt = &dummy_evt;
      if(next_event_good)
      {
        evt = event;
      }
      B32 taken = 0;
      
      //////////////////////////
      //- rjf: begin editing on some operations
      //
      if(!ewv->text_editing &&
         (evt->kind == UI_EventKind_Text ||
          evt->flags & UI_EventFlag_Paste ||
          (evt->kind == UI_EventKind_Press && evt->slot == UI_EventActionSlot_Edit)) &&
         selection_tbl.min.x == selection_tbl.max.x &&
         (selection_tbl.min.y != 0 || selection_tbl.min.y != 0))
      {
        Vec2S64 selection_dim = dim_2s64(selection_tbl);
        ewv->text_editing = 1;
        arena_clear(ewv->text_edit_arena);
        ewv->text_edit_state_slots_count = u64_up_to_pow2(selection_dim.y+1);
        ewv->text_edit_state_slots_count = Max(ewv->text_edit_state_slots_count, 64);
        ewv->text_edit_state_slots = push_array(ewv->text_edit_arena, RD_WatchViewTextEditState*, ewv->text_edit_state_slots_count);
        EV_WindowedRowList rows = ev_windowed_row_list_from_block_list(scratch.arena, eval_view, r1s64(ui_scroll_list_row_from_item(&row_blocks, selection_tbl.min.y-1),
                                                                                                       ui_scroll_list_row_from_item(&row_blocks, selection_tbl.max.y-1)+1), &blocks);
        EV_Row *row = rows.first;
        for(S64 y = selection_tbl.min.y; y <= selection_tbl.max.y; y += 1, row = row->next)
        {
          for(S64 x = selection_tbl.min.x; x <= selection_tbl.max.x; x += 1)
          {
            RD_WatchViewColumn *col = rd_watch_view_column_from_x(ewv, x);
            String8 string = rd_string_from_eval_viz_row_column(scratch.arena, eval_view, row, col, 1, default_radix, ui_top_font(), ui_top_font_size(), row_string_max_size_px);
            string.size = Min(string.size, sizeof(ewv->dummy_text_edit_state.input_buffer));
            RD_WatchViewPoint pt = {x, row->parent_key, row->key};
            U64 hash = ev_hash_from_key(pt.key);
            U64 slot_idx = hash%ewv->text_edit_state_slots_count;
            RD_WatchViewTextEditState *edit_state = push_array(ewv->text_edit_arena, RD_WatchViewTextEditState, 1);
            SLLStackPush_N(ewv->text_edit_state_slots[slot_idx], edit_state, pt_hash_next);
            edit_state->pt = pt;
            edit_state->cursor = txt_pt(1, string.size+1);
            edit_state->mark = txt_pt(1, 1);
            edit_state->input_size = string.size;
            MemoryCopy(edit_state->input_buffer, string.str, string.size);
            edit_state->initial_size = string.size;
            MemoryCopy(edit_state->initial_buffer, string.str, string.size);
          }
        }
      }
      
      //////////////////////////
      //- rjf: [table] do cell-granularity expansions / tab-opens
      //
      if(!ewv->text_editing && evt->slot == UI_EventActionSlot_Accept)
      {
        EV_WindowedRowList rows = ev_windowed_row_list_from_block_list(scratch.arena, eval_view, r1s64(ui_scroll_list_row_from_item(&row_blocks, selection_tbl.min.y-1),
                                                                                                       ui_scroll_list_row_from_item(&row_blocks, selection_tbl.max.y-1)+1), &blocks);
        EV_Row *row = rows.first;
        for(S64 y = selection_tbl.min.y; y <= selection_tbl.max.y && row != 0; y += 1, row = row->next)
        {
          if(selection_tbl.min.x <= 0 && ev_row_is_expandable(row))
          {
            B32 is_expanded = ev_expansion_from_key(eval_view, row->key);
            ev_key_set_expansion(eval_view, row->parent_key, row->key, !is_expanded);
            taken = 1;
          }
          RD_ViewRuleInfo *view_rule_info = &rd_nil_view_rule_info;
          MD_Node *view_rule_params_root = &md_nil_node;
          for(EV_ViewRuleNode *n = row->view_rules->first; n != 0; n = n->next)
          {
            RD_ViewRuleInfo *info = rd_view_rule_info_from_string(n->v.root->string);
            if(info->ui != 0)
            {
              view_rule_info = info;
              view_rule_params_root = n->v.root;
            }
          }
          if(view_rule_info != &rd_nil_view_rule_info && row->block_kind == EV_BlockKind_Canvas)
          {
            rd_cmd(RD_CmdKind_OpenTab,
                   .string      = e_string_from_expr(scratch.arena, row->expr),
                   .params_tree = view_rule_params_root);
            taken = 1;
          }
        }
      }
      
      //////////////////////////
      //- rjf: [table] do cell-granularity go-to-locations / frame selections
      //
      if(!ewv->text_editing && evt->slot == UI_EventActionSlot_Accept &&
         selection_tbl.min.x == selection_tbl.max.x &&
         selection_tbl.min.y == selection_tbl.max.y &&
         selection_tbl.min.x == 1)
      {
        taken = 1;
        EV_WindowedRowList rows = ev_windowed_row_list_from_block_list(scratch.arena, eval_view, r1s64(ui_scroll_list_row_from_item(&row_blocks, selection_tbl.min.y-1),
                                                                                                       ui_scroll_list_row_from_item(&row_blocks, selection_tbl.max.y-1)+1), &blocks);
        EV_Row *row = rows.first;
        B32 row_is_editable = ev_row_is_editable(row);
        if(!row_is_editable)
        {
          E_Eval eval = e_eval_from_expr(scratch.arena, row->expr);
          U64 vaddr = eval.value.u64;
          CTRL_Entity *eval_space_ctrl_entity = rd_ctrl_entity_from_eval_space(eval.space);
          if(eval_space_ctrl_entity->kind == CTRL_EntityKind_Process)
          {
            CTRL_Entity *module = ctrl_module_from_process_vaddr(eval_space_ctrl_entity, vaddr);
            DI_Key dbgi_key = ctrl_dbgi_key_from_module(module);
            U64 voff = ctrl_voff_from_vaddr(module, vaddr);
            D_LineList lines = d_lines_from_dbgi_key_voff(scratch.arena, &dbgi_key, voff);
            String8 file_path = {0};
            TxtPt pt = {0};
            if(lines.first != 0)
            {
              file_path = lines.first->v.file_path;
              pt        = lines.first->v.pt;
            }
            rd_cmd(RD_CmdKind_FindCodeLocation,
                   .process   = eval_space_ctrl_entity->handle,
                   .vaddr     = vaddr,
                   .file_path = file_path,
                   .cursor    = pt);
          }
        }
        if(1 <= selection_tbl.min.y && selection_tbl.min.y <= frame_rows_count)
        {
          FrameRow *frame_row = &frame_rows[selection_tbl.min.y-1];
          rd_cmd(RD_CmdKind_SelectUnwind,
                 .unwind_count = frame_row->unwind_idx,
                 .inline_depth = frame_row->inline_depth);
        }
      }
      
      //////////////////////////
      //- rjf: [text] apply textual edits
      //
      if(ewv->text_editing)
      {
        B32 editing_complete = ((evt->kind == UI_EventKind_Press && (evt->slot == UI_EventActionSlot_Cancel || evt->slot == UI_EventActionSlot_Accept)) ||
                                (evt->kind == UI_EventKind_Navigate && evt->delta_2s32.y != 0) ||
                                cursor_rugpull);
        if(editing_complete ||
           ((evt->kind == UI_EventKind_Edit ||
             evt->kind == UI_EventKind_Navigate ||
             evt->kind == UI_EventKind_Text) &&
            evt->delta_2s32.y == 0))
        {
          taken = 1;
          for(S64 y = selection_tbl.min.y; y <= selection_tbl.max.y; y += 1)
          {
            for(S64 x = selection_tbl.min.x; x <= selection_tbl.max.x; x += 1)
            {
              RD_WatchViewPoint pt = rd_watch_view_point_from_tbl(&blocks, v2s64(x, y));
              RD_WatchViewTextEditState *edit_state = rd_watch_view_text_edit_state_from_pt(ewv, pt);
              String8 string = str8(edit_state->input_buffer, edit_state->input_size);
              UI_TxtOp op = ui_single_line_txt_op_from_event(scratch.arena, evt, string, edit_state->cursor, edit_state->mark);
              
              // rjf: copy
              if(op.flags & UI_TxtOpFlag_Copy && selection_tbl.min.x == selection_tbl.max.x && selection_tbl.min.y == selection_tbl.max.y)
              {
                os_set_clipboard_text(op.copy);
              }
              
              // rjf: any valid op & autocomplete hint? -> perform autocomplete first, then re-compute op
              if(autocomplete_hint_string.size != 0)
              {
                take_autocomplete = 1;
                String8 word_query = rd_autocomp_query_word_from_input_string_off(string, edit_state->cursor.column-1);
                U64 word_off = (U64)(word_query.str - string.str);
                String8 new_string = ui_push_string_replace_range(scratch.arena, string, r1s64(word_off+1, word_off+1+word_query.size), autocomplete_hint_string);
                new_string.size = Min(sizeof(edit_state->input_buffer), new_string.size);
                MemoryCopy(edit_state->input_buffer, new_string.str, new_string.size);
                edit_state->input_size = new_string.size;
                edit_state->cursor = edit_state->mark = txt_pt(1, word_off+1+autocomplete_hint_string.size);
                string = str8(edit_state->input_buffer, edit_state->input_size);
                op = ui_single_line_txt_op_from_event(scratch.arena, evt, string, edit_state->cursor, edit_state->mark);
              }
              
              // rjf: cancel? -> revert to initial string
              if(editing_complete && evt->slot == UI_EventActionSlot_Cancel)
              {
                string = str8(edit_state->initial_buffer, edit_state->initial_size);
              }
              
              // rjf: obtain edited string
              String8 new_string = string;
              if(!txt_pt_match(op.range.min, op.range.max) || op.replace.size != 0)
              {
                new_string = ui_push_string_replace_range(scratch.arena, string, r1s64(op.range.min.column, op.range.max.column), op.replace);
              }
              
              // rjf: commit to edit state
              new_string.size = Min(new_string.size, sizeof(edit_state->input_buffer));
              MemoryCopy(edit_state->input_buffer, new_string.str, new_string.size);
              edit_state->input_size = new_string.size;
              edit_state->cursor = op.cursor;
              edit_state->mark = op.mark;
              
              // rjf: commit edited cell string
              Vec2S64 tbl = v2s64(x, y);
              RD_WatchViewColumn *col = rd_watch_view_column_from_x(ewv, x);
              switch(col->kind)
              {
                default:{}break;
                case RD_WatchViewColumnKind_Expr:
                if(modifiable && filter.size == 0)
                {
                  RD_WatchViewPoint pt = rd_watch_view_point_from_tbl(&blocks, tbl);
                  RD_Entity *watch = rd_entity_from_ev_key_and_kind(pt.key, mutable_entity_kind);
                  if(!rd_entity_is_nil(watch))
                  {
                    rd_entity_equip_name(watch, new_string);
                    state_dirty = 1;
                    snap_to_cursor = 1;
                  }
                  else if(editing_complete && new_string.size != 0 && ev_key_match(pt.key, empty_row_key))
                  {
                    watch = rd_entity_alloc(rd_entity_root(), mutable_entity_kind);
                    rd_entity_equip_cfg_src(watch, RD_CfgSrc_Project);
                    rd_entity_equip_name(watch, new_string);
                    EV_Key key = rd_ev_key_from_entity(watch);
                    ev_key_set_view_rule(eval_view, key, str8_zero());
                    state_dirty = 1;
                    snap_to_cursor = 1;
                  }
                }break;
                case RD_WatchViewColumnKind_Member:
                case RD_WatchViewColumnKind_Value:
                if(editing_complete && evt->slot != UI_EventActionSlot_Cancel)
                {
                  EV_WindowedRowList rows = ev_windowed_row_list_from_block_list(scratch.arena, eval_view, r1s64(ui_scroll_list_row_from_item(&row_blocks, y-1),
                                                                                                                 ui_scroll_list_row_from_item(&row_blocks, y-1)+1), &blocks);
                  B32 success = 0;
                  if(rows.first != 0)
                  {
                    E_Expr *expr = rows.first->expr;
                    if(col->kind == RD_WatchViewColumnKind_Member)
                    {
                      expr = e_expr_ref_member_access(scratch.arena, expr, str8(col->string_buffer, col->string_size));
                    }
                    E_Eval dst_eval = e_eval_from_expr(scratch.arena, expr);
                    success = rd_commit_eval_value_string(dst_eval, new_string);
                  }
                  if(!success)
                  {
                    log_user_error(str8_lit("Could not commit value successfully."));
                  }
                }break;
                case RD_WatchViewColumnKind_Type:{}break;
                case RD_WatchViewColumnKind_ViewRule:
                if(editing_complete)
                {
                  RD_WatchViewPoint pt = rd_watch_view_point_from_tbl(&blocks, tbl);
                  ev_key_set_view_rule(eval_view, pt.key, new_string);
                  RD_Entity *watch = rd_entity_from_ev_key_and_kind(pt.key, mutable_entity_kind);
                  RD_Entity *view_rule = rd_entity_child_from_kind(watch, RD_EntityKind_ViewRule);
                  if(new_string.size != 0 && rd_entity_is_nil(view_rule))
                  {
                    view_rule = rd_entity_alloc(watch, RD_EntityKind_ViewRule);
                  }
                  else if(new_string.size == 0 && !rd_entity_is_nil(view_rule))
                  {
                    rd_entity_mark_for_deletion(view_rule);
                  }
                  if(new_string.size != 0)
                  {
                    rd_entity_equip_name(view_rule, new_string);
                  }
                  state_dirty = 1;
                  snap_to_cursor = 1;
                }break;
              }
            }
          }
        }
        if(editing_complete)
        {
          ewv->text_editing = 0;
        }
      }
      
      //////////////////////////
      //- rjf: [table] do cell-granularity copies
      //
      if(!ewv->text_editing && evt->flags & UI_EventFlag_Copy)
      {
        taken = 1;
        String8List strs = {0};
        EV_WindowedRowList rows = ev_windowed_row_list_from_block_list(scratch.arena, eval_view, r1s64(ui_scroll_list_row_from_item(&row_blocks, selection_tbl.min.y-1),
                                                                                                       ui_scroll_list_row_from_item(&row_blocks, selection_tbl.max.y-1)+1), &blocks);
        EV_Row *row = rows.first;
        for(S64 y = selection_tbl.min.y; y <= selection_tbl.max.y && row != 0; y += 1, row = row->next)
        {
          for(S64 x = selection_tbl.min.x; x <= selection_tbl.max.x; x += 1)
          {
            RD_WatchViewColumn *col = rd_watch_view_column_from_x(ewv, x);
            String8 cell_string = rd_string_from_eval_viz_row_column(scratch.arena, eval_view, row, col, 0, default_radix, ui_top_font(), ui_top_font_size(), row_string_max_size_px);
            cell_string = str8_skip_chop_whitespace(cell_string);
            U64 comma_pos = str8_find_needle(cell_string, 0, str8_lit(","), 0);
            if(selection_tbl.min.x != selection_tbl.max.x || selection_tbl.min.y != selection_tbl.max.y)
            {
              str8_list_pushf(scratch.arena, &strs, "%s%S%s%s",
                              comma_pos < cell_string.size ? "\"" : "",
                              cell_string,
                              comma_pos < cell_string.size ? "\"" : "",
                              x+1 <= selection_tbl.max.x ? "," : "");
            }
            else
            {
              str8_list_push(scratch.arena, &strs, cell_string);
            }
          }
          if(y+1 <= selection_tbl.max.y)
          {
            str8_list_push(scratch.arena, &strs, str8_lit("\n"));
          }
        }
        String8 string = str8_list_join(scratch.arena, &strs, 0);
        os_set_clipboard_text(string);
      }
      
      //////////////////////////
      //- rjf: [table] do cell-granularity deletions
      //
      if(!ewv->text_editing && evt->flags & UI_EventFlag_Delete)
      {
        taken = 1;
        state_dirty = 1;
        snap_to_cursor = 1;
        for(S64 y = selection_tbl.min.y; y <= selection_tbl.max.y; y += 1)
        {
          RD_WatchViewPoint pt = rd_watch_view_point_from_tbl(&blocks, v2s64(0, y));
          
          // rjf: row deletions
          if(selection_tbl.min.x <= 0)
          {
            RD_WatchViewPoint fallback_pt_prev = rd_watch_view_point_from_tbl(&blocks, v2s64(0, y - 1));
            RD_WatchViewPoint fallback_pt_next = rd_watch_view_point_from_tbl(&blocks, v2s64(0, y + 1));
            RD_Entity *watch = rd_entity_from_ev_key_and_kind(pt.key, mutable_entity_kind);
            if(!rd_entity_is_nil(watch))
            {
              EV_Key new_cursor_key = empty_row_key;
              EV_Key new_cursor_parent_key = empty_row_parent_key;
              if((evt->delta_2s32.x < 0 || evt->delta_2s32.y < 0) && !ev_key_match(ev_key_zero(), fallback_pt_prev.key))
              {
                RD_Entity *fallback_watch = rd_entity_from_ev_key_and_kind(fallback_pt_prev.key, mutable_entity_kind);
                if(!rd_entity_is_nil(fallback_watch))
                {
                  new_cursor_key = fallback_pt_prev.key;
                  new_cursor_parent_key = rd_parent_ev_key_from_entity(fallback_watch);
                }
              }
              else if(!ev_key_match(ev_key_zero(), fallback_pt_next.key))
              {
                RD_Entity *fallback_watch = rd_entity_from_ev_key_and_kind(fallback_pt_next.key, mutable_entity_kind);
                if(!rd_entity_is_nil(fallback_watch))
                {
                  new_cursor_key = fallback_pt_next.key;
                  new_cursor_parent_key = rd_parent_ev_key_from_entity(fallback_watch);
                }
              }
              RD_WatchViewPoint new_cursor_pt = {0, new_cursor_parent_key, new_cursor_key};
              rd_entity_mark_for_deletion(watch);
              ewv->cursor = ewv->mark = ewv->next_cursor = ewv->next_mark = new_cursor_pt;
            }
          }
          
          // rjf: view rule deletions
          else if(selection_tbl.min.x <= RD_WatchViewColumnKind_ViewRule && RD_WatchViewColumnKind_ViewRule <= selection_tbl.max.x)
          {
            RD_Entity *watch = rd_entity_from_ev_key_and_kind(pt.key, mutable_entity_kind);
            RD_Entity *view_rule = rd_entity_child_from_kind(watch, RD_EntityKind_ViewRule);
            rd_entity_mark_for_deletion(view_rule);
            ev_key_set_view_rule(eval_view, pt.key, str8_zero());
          }
        }
      }
      
      //////////////////////////
      //- rjf: [table] apply deltas to cursor & mark
      //
      if(!ewv->text_editing && !(evt->flags & UI_EventFlag_Delete) && !(evt->flags & UI_EventFlag_Reorder))
      {
        B32 cursor_tbl_min_is_empty_selection[Axis2_COUNT] = {0, 1};
        Rng2S64 cursor_tbl_range = r2s64(v2s64(0, 0), v2s64(ewv->column_count-1, blocks.total_semantic_row_count));
        Vec2S32 delta = evt->delta_2s32;
        if(evt->flags & UI_EventFlag_PickSelectSide && !MemoryMatchStruct(&selection_tbl.min, &selection_tbl.max))
        {
          if(delta.x > 0 || delta.y > 0)
          {
            cursor_tbl.x = selection_tbl.max.x;
            cursor_tbl.y = selection_tbl.max.y;
          }
          else if(delta.x < 0 || delta.y < 0)
          {
            cursor_tbl.x = selection_tbl.min.x;
            cursor_tbl.y = selection_tbl.min.y;
          }
        }
        if(evt->flags & UI_EventFlag_ZeroDeltaOnSelect && !MemoryMatchStruct(&selection_tbl.min, &selection_tbl.max))
        {
          MemoryZeroStruct(&delta);
        }
        B32 moved = 1;
        switch(evt->delta_unit)
        {
          default:{moved = 0;}break;
          case UI_EventDeltaUnit_Char:
          {
            for EachEnumVal(Axis2, axis)
            {
              cursor_tbl.v[axis] += delta.v[axis];
              if(cursor_tbl.v[axis] < cursor_tbl_range.min.v[axis])
              {
                cursor_tbl.v[axis] = cursor_tbl_range.max.v[axis];
              }
              if(cursor_tbl.v[axis] > cursor_tbl_range.max.v[axis])
              {
                cursor_tbl.v[axis] = cursor_tbl_range.min.v[axis];
              }
              cursor_tbl.v[axis] = clamp_1s64(r1s64(cursor_tbl_range.min.v[axis], cursor_tbl_range.max.v[axis]), cursor_tbl.v[axis]);
            }
          }break;
          case UI_EventDeltaUnit_Word:
          case UI_EventDeltaUnit_Line:
          case UI_EventDeltaUnit_Page:
          {
            cursor_tbl.x  = (delta.x>0 ? (cursor_tbl_range.max.x) :
                             delta.x<0 ? (cursor_tbl_range.min.x + !!cursor_tbl_min_is_empty_selection[Axis2_X]) :
                             cursor_tbl.x);
            cursor_tbl.y += ((delta.y>0 ? +(num_possible_visible_rows-3) :
                              delta.y<0 ? -(num_possible_visible_rows-3) :
                              0));
            cursor_tbl.y = clamp_1s64(r1s64(cursor_tbl_range.min.y + !!cursor_tbl_min_is_empty_selection[Axis2_Y],
                                            cursor_tbl_range.max.y),
                                      cursor_tbl.y);
          }break;
          case UI_EventDeltaUnit_Whole:
          {
            for EachEnumVal(Axis2, axis)
            {
              cursor_tbl.v[axis] = (delta.v[axis]>0 ? cursor_tbl_range.max.v[axis] : delta.v[axis]<0 ? cursor_tbl_range.min.v[axis] + !!cursor_tbl_min_is_empty_selection[axis] : cursor_tbl.v[axis]);
            }
          }break;
        }
        if(moved)
        {
          taken = 1;
          cursor_dirty__tbl = 1;
          snap_to_cursor = 1;
        }
      }
      
      //////////////////////////
      //- rjf: [table] stick table mark to cursor if needed
      //
      if(!ewv->text_editing)
      {
        if(taken && !(evt->flags & UI_EventFlag_KeepMark))
        {
          mark_tbl = cursor_tbl;
        }
      }
      
      //////////////////////////
      //- rjf: [table] do cell-granularity reorders
      //
      if(!ewv->text_editing && evt->flags & UI_EventFlag_Reorder)
      {
        taken = 1;
        EV_Key first_watch_key = ev_key_from_block_list_row_num(&blocks, selection_tbl.min.y);
        EV_Key reorder_group_prev_watch_key = ev_key_from_block_list_row_num(&blocks, selection_tbl.min.y - 1);
        EV_Key reorder_group_next_watch_key = ev_key_from_block_list_row_num(&blocks, selection_tbl.max.y + 1);
        RD_Entity *reorder_group_prev = rd_entity_from_ev_key_and_kind(reorder_group_prev_watch_key, mutable_entity_kind);
        RD_Entity *reorder_group_next = rd_entity_from_ev_key_and_kind(reorder_group_next_watch_key, mutable_entity_kind);
        RD_Entity *first_watch = rd_entity_from_ev_key_and_kind(first_watch_key, mutable_entity_kind);
        RD_Entity *last_watch = first_watch;
        if(!rd_entity_is_nil(first_watch))
        {
          for(S64 y = selection_tbl.min.y+1; y <= selection_tbl.max.y; y += 1)
          {
            EV_Key key = ev_key_from_block_list_row_num(&blocks, y);
            RD_Entity *new_last = rd_entity_from_ev_key_and_kind(key, mutable_entity_kind);
            if(!rd_entity_is_nil(new_last))
            {
              last_watch = new_last;
            }
          }
        }
        if(evt->delta_2s32.y < 0 && !rd_entity_is_nil(first_watch) && !rd_entity_is_nil(reorder_group_prev))
        {
          state_dirty = 1;
          snap_to_cursor = 1;
          rd_entity_change_parent(reorder_group_prev, reorder_group_prev->parent, reorder_group_prev->parent, last_watch);
        }
        if(evt->delta_2s32.y > 0 && !rd_entity_is_nil(last_watch) && !rd_entity_is_nil(reorder_group_next))
        {
          state_dirty = 1;
          snap_to_cursor = 1;
          rd_entity_change_parent(reorder_group_next, reorder_group_next->parent, reorder_group_next->parent, reorder_group_prev);
        }
      }
      
      //////////////////////////
      //- rjf: consume event, if taken
      //
      if(taken && evt != &dummy_evt)
      {
        ui_eat_event(evt);
      }
    }
    if(take_autocomplete)
    {
      for(UI_Event *evt = 0; ui_next_event(&evt);)
      {
        if(evt->kind == UI_EventKind_AutocompleteHint)
        {
          ui_eat_event(evt);
          break;
        }
      }
    }
  }
  
  //////////////////////////////
  //- rjf: build ui
  //
  B32 pressed = 0;
  if(!is_top_level_hook)
  {
    F32 **col_pcts = push_array(scratch.arena, F32*, ewv->column_count);
    {
      S64 x = 0;
      for(RD_WatchViewColumn *c = ewv->first_column; c != 0; c = c->next, x += 1)
      {
        col_pcts[x] = &c->pct;
      }
    }
    Rng1S64 visible_row_rng = {0};
    UI_ScrollListParams scroll_list_params = {0};
    {
      scroll_list_params.flags         = UI_ScrollListFlag_All;
      scroll_list_params.row_height_px = floor_f32(ui_top_font_size()*2.5f);
      scroll_list_params.dim_px        = dim_2f32(rect);
      scroll_list_params.cursor_range  = r2s64(v2s64(0, 0), v2s64(3, blocks.total_semantic_row_count));
      scroll_list_params.item_range    = r1s64(0, blocks.total_visual_row_count + 1*!(flags & RD_WatchViewFlag_NoHeader));
      scroll_list_params.cursor_min_is_empty_selection[Axis2_Y] = 1;
      UI_ScrollListRowBlockChunkList row_block_chunks = {0};
      for(EV_BlockNode *n = blocks.first; n != 0; n = n->next)
      {
        EV_Block *vb = &n->v;
        UI_ScrollListRowBlock block = {0};
        block.row_count = dim_1u64(vb->visual_idx_range);
        block.item_count = dim_1u64(vb->semantic_idx_range);
        ui_scroll_list_row_block_chunk_list_push(scratch.arena, &row_block_chunks, 256, &block);
      }
      scroll_list_params.row_blocks = ui_scroll_list_row_block_array_from_chunk_list(scratch.arena, &row_block_chunks);
    }
    UI_BoxFlags disabled_flags = ui_top_flags();
    if(d_ctrl_targets_running())
    {
      disabled_flags |= UI_BoxFlag_Disabled;
    }
    UI_ScrollListSignal scroll_list_sig = {0};
    UI_Focus(UI_FocusKind_On)
      UI_ScrollList(&scroll_list_params, &scroll_pos.y,
                    0,
                    0,
                    &visible_row_rng,
                    &scroll_list_sig)
      UI_Focus(UI_FocusKind_Null)
      UI_TableF(ewv->column_count, col_pcts, "table")
    {
      Vec2F32 scroll_list_view_off_px = ui_top_parent()->parent->view_off;
      
      ////////////////////////////
      //- rjf: build table header
      //
      if(~flags & RD_WatchViewFlag_NoHeader && visible_row_rng.min == 0) UI_TableVector UI_FlagsAdd(UI_BoxFlag_DrawTextWeak)
      {
        for(RD_WatchViewColumn *col = ewv->first_column; col != 0; col = col->next)
          UI_TableCell
        {
          String8 name = str8(col->display_string_buffer, col->display_string_size);
          if(name.size == 0)
          {
            switch(col->kind)
            {
              default:{}break;
              case RD_WatchViewColumnKind_Expr:    {name = str8_lit("Expression");}break;
              case RD_WatchViewColumnKind_Value:   {name = str8_lit("Value");}break;
              case RD_WatchViewColumnKind_Type:    {name = str8_lit("Type");}break;
              case RD_WatchViewColumnKind_ViewRule:{name = str8_lit("View Rule");}break;
              case RD_WatchViewColumnKind_Module:  {name = str8_lit("Module");}break;
              case RD_WatchViewColumnKind_Member:
              {
                name = str8(col->string_buffer, col->string_size);
              }break;
            }
          }
          switch(col->kind)
          {
            default:
            {
              ui_label(name);
            }break;
            case RD_WatchViewColumnKind_ViewRule:
            {
              if(rd_help_label(name)) UI_Tooltip
              {
                F32 max_width = ui_top_font_size()*35;
                ui_label_multiline(max_width, str8_lit("View rules are used to tweak the way evaluated expressions are visualized. Multiple rules can be specified on each row. They are specified in a key:(value) form. Some examples follow:"));
                ui_spacer(ui_em(1.5f, 1));
                RD_Font(RD_FontSlot_Code) ui_labelf("array:(N)");
                UI_FlagsAdd(UI_BoxFlag_DrawTextWeak) ui_label_multiline(max_width, str8_lit("Specifies that a pointer points to N elements, rather than only 1."));
                ui_spacer(ui_em(1.5f, 1));
                RD_Font(RD_FontSlot_Code) ui_labelf("omit:(member_1 ... member_n)");
                UI_FlagsAdd(UI_BoxFlag_DrawTextWeak) ui_label_multiline(max_width, str8_lit("Omits a list of member names from appearing in struct, union, or class evaluations."));
                ui_spacer(ui_em(1.5f, 1));
                RD_Font(RD_FontSlot_Code) ui_labelf("only:(member_1 ... member_n)");
                UI_FlagsAdd(UI_BoxFlag_DrawTextWeak) ui_label_multiline(max_width, str8_lit("Specifies that only the specified members should appear in struct, union, or class evaluations."));
                RD_Font(RD_FontSlot_Code) ui_labelf("list:(next_link_member_name)");
                UI_FlagsAdd(UI_BoxFlag_DrawTextWeak) ui_label_multiline(max_width, str8_lit("Specifies that some struct, union, or class forms the top of a linked list, with next_link_member_name being the member which points at the next element in the list."));
                ui_spacer(ui_em(1.5f, 1));
                RD_Font(RD_FontSlot_Code) ui_labelf("dec");
                UI_FlagsAdd(UI_BoxFlag_DrawTextWeak) ui_label_multiline(max_width, str8_lit("Specifies that all integral evaluations should appear in base-10 form."));
                ui_spacer(ui_em(1.5f, 1));
                RD_Font(RD_FontSlot_Code) ui_labelf("hex");
                UI_FlagsAdd(UI_BoxFlag_DrawTextWeak) ui_label_multiline(max_width, str8_lit("Specifies that all integral evaluations should appear in base-16 form."));
                ui_spacer(ui_em(1.5f, 1));
                RD_Font(RD_FontSlot_Code) ui_labelf("oct");
                UI_FlagsAdd(UI_BoxFlag_DrawTextWeak) ui_label_multiline(max_width, str8_lit("Specifies that all integral evaluations should appear in base-8 form."));
                ui_spacer(ui_em(1.5f, 1));
                RD_Font(RD_FontSlot_Code) ui_labelf("bin");
                UI_FlagsAdd(UI_BoxFlag_DrawTextWeak) ui_label_multiline(max_width, str8_lit("Specifies that all integral evaluations should appear in base-2 form."));
                ui_spacer(ui_em(1.5f, 1));
                RD_Font(RD_FontSlot_Code) ui_labelf("no_addr");
                UI_FlagsAdd(UI_BoxFlag_DrawTextWeak) ui_label_multiline(max_width, str8_lit("Displays only what pointers point to, if possible, without the pointer's address value."));
                ui_spacer(ui_em(1.5f, 1));
              }
            }break;
          }
        }
      }
      
      ////////////////////////////
      //- rjf: viz blocks -> rows
      //
      EV_WindowedRowList rows = {0};
      {
        rows = ev_windowed_row_list_from_block_list(scratch.arena, eval_view,
                                                    r1s64(visible_row_rng.min - 1*!(flags & RD_WatchViewFlag_NoHeader),
                                                          visible_row_rng.max + 1*!!(flags & RD_WatchViewFlag_NoHeader)),
                                                    &blocks);
      }
      
      ////////////////////////////
      //- rjf: build table
      //
      ProfScope("build table")
      {
        U64 semantic_idx = rows.count_before_semantic;
        for(EV_Row *row = rows.first; row != 0; row = row->next, semantic_idx += 1)
        {
          ////////////////////////
          //- rjf: unpack row info
          //
          U64 row_hash = ev_hash_from_key(row->key);
          B32 row_selected = (selection_tbl.min.y <= (semantic_idx+1) && (semantic_idx+1) <= selection_tbl.max.y);
          B32 row_expanded = ev_expansion_from_key(eval_view, row->key);
          E_Eval row_eval = e_eval_from_expr(scratch.arena, row->expr);
          E_Type *row_type = e_type_from_key(scratch.arena, row_eval.type_key);
          B32 row_is_expandable = ev_row_is_expandable(row);
          B32 row_is_editable = ev_row_is_editable(row);
          B32 next_row_expanded = row_expanded;
          RD_ViewRuleInfo *ui_view_rule_info = &rd_nil_view_rule_info;
          MD_Node *ui_view_rule_params_root = &md_nil_node;
          for(EV_ViewRuleNode *n = row->view_rules->first; n != 0; n = n->next)
          {
            RD_ViewRuleInfo *info = rd_view_rule_info_from_string(n->v.root->string);
            if(info->ui != 0)
            {
              ui_view_rule_info = info;
              ui_view_rule_params_root = n->v.root;
            }
          }
          
          ////////////////////////
          //- rjf: determine if row's data is fresh and/or bad
          //
          B32 row_is_fresh = 0;
          B32 row_is_bad = 0;
          switch(row_eval.mode)
          {
            default:{}break;
            case E_Mode_Offset:
            {
              CTRL_Entity *space_entity = rd_ctrl_entity_from_eval_space(row_eval.space);
              if(space_entity->kind == CTRL_EntityKind_Process)
              {
                U64 size = e_type_byte_size_from_key(row_eval.type_key);
                size = Min(size, 64);
                Rng1U64 vaddr_rng = r1u64(row_eval.value.u64, row_eval.value.u64+size);
                CTRL_ProcessMemorySlice slice = ctrl_query_cached_data_from_process_vaddr_range(scratch.arena, space_entity->handle, vaddr_rng, 0);
                for(U64 idx = 0; idx < (slice.data.size+63)/64; idx += 1)
                {
                  if(slice.byte_changed_flags[idx] != 0)
                  {
                    row_is_fresh = 1;
                  }
                  if(slice.byte_bad_flags[idx] != 0)
                  {
                    row_is_bad = 1;
                  }
                }
              }
            }break;
          }
          
          ////////////////////////
          //- rjf: determine row's color palette
          //
          UI_BoxFlags row_flags = 0;
          UI_Palette *palette = ui_top_palette();
          {
            if(row_is_fresh)
            {
              palette = ui_build_palette(ui_top_palette(), .background = rd_rgba_from_theme_color(RD_ThemeColor_HighlightOverlay));
              row_flags |= UI_BoxFlag_DrawBackground;
            }
          }
          
          ////////////////////////
          //- rjf: build row box
          //
          ui_set_next_palette(palette);
          ui_set_next_flags(disabled_flags);
          ui_set_next_pref_width(ui_pct(1, 0));
          ui_set_next_pref_height(ui_px(scroll_list_params.row_height_px*row->size_in_rows, 1.f));
          ui_set_next_focus_hot(row_selected ? UI_FocusKind_On : UI_FocusKind_Off);
          UI_Box *row_box = ui_build_box_from_stringf(row_flags|
                                                      UI_BoxFlag_DrawSideBottom|
                                                      UI_BoxFlag_Clickable|
                                                      ((row->block_kind != EV_BlockKind_Canvas) * UI_BoxFlag_DisableFocusOverlay)|
                                                      ((row->block_kind == EV_BlockKind_Canvas) * UI_BoxFlag_Clip),
                                                      "row_%I64x", row_hash);
          ui_ts_vector_idx += 1;
          ui_ts_cell_idx = 0;
          
          ////////////////////////
          //- rjf: canvas block row -> build singular row for "escape hatch" ui
          //
          B32 did_row_build = 0;
          if(!did_row_build && row->block_kind == EV_BlockKind_Canvas)
            UI_Parent(row_box) UI_FocusHot(row_selected ? UI_FocusKind_On : UI_FocusKind_Off)
          {
            //- rjf: build ui hook row contents
            if(ui_view_rule_info != &rd_nil_view_rule_info && ui_view_rule_info->ui != 0)
            {
              did_row_build = 1;
              
              //- rjf: unpack
              RD_WatchViewPoint pt = {0, row->parent_key, row->key};
              RD_View *view = rd_view_from_handle(rd_regs()->view);
              RD_TransientViewNode *canvas_view_node = rd_transient_view_node_from_ev_key(view, row->key);
              RD_View *canvas_view = canvas_view_node->view;
              String8 canvas_view_expr = e_string_from_expr(scratch.arena, row->expr);
              B32 need_new_spec = (!str8_match(str8(canvas_view->query_buffer, canvas_view->query_string_size), canvas_view_expr, 0) ||
                                   !md_tree_match(canvas_view_node->initial_params, ui_view_rule_params_root, 0));
              if(need_new_spec)
              {
                arena_clear(canvas_view_node->initial_params_arena);
                canvas_view_node->initial_params = md_tree_copy(canvas_view_node->initial_params_arena, ui_view_rule_params_root);
                rd_view_equip_spec(canvas_view, ui_view_rule_info, canvas_view_expr, ui_view_rule_params_root);
              }
              Vec2F32 canvas_dim = v2f32(scroll_list_params.dim_px.x - ui_top_font_size()*1.5f,
                                         (row->skipped_size_in_rows+row->size_in_rows+row->chopped_size_in_rows)*scroll_list_params.row_height_px);
              Rng2F32 canvas_rect = r2f32p(rect.x0,
                                           rect.y0 + ui_top_fixed_y(),
                                           rect.x0 + canvas_dim.x,
                                           rect.y0 + ui_top_fixed_y() + canvas_dim.y);
              
              //- rjf: peek clicks in canvas region, mark clicked
              for(UI_Event *evt = 0; ui_next_event(&evt);)
              {
                if(evt->kind == UI_EventKind_Press && evt->key == OS_Key_LeftMouseButton && contains_2f32(canvas_rect, evt->pos))
                {
                  pressed = 1;
                  break;
                }
              }
              
              //- rjf: build meta controls
              ui_set_next_fixed_x(ui_top_font_size()*1.f);
              ui_set_next_fixed_y(ui_top_font_size()*1.f + scroll_list_view_off_px.y*(row == rows.first));
              ui_set_next_pref_width(ui_em(3, 1));
              ui_set_next_pref_height(ui_em(3, 1));
              UI_Flags(UI_BoxFlag_DrawDropShadow) UI_CornerRadius(ui_top_font_size()*0.5f)
              {
                UI_Signal sig = rd_icon_buttonf(RD_IconKind_Window, 0, "###pop_out");
                if(ui_hovering(sig)) UI_Tooltip
                {
                  ui_labelf("Pop out");
                }
                if(ui_pressed(sig))
                {
                  pressed = 1;
                }
                if(ui_clicked(sig))
                {
                  rd_cmd(RD_CmdKind_OpenTab,
                         .string      = e_string_from_expr(scratch.arena, row->expr),
                         .params_tree = ui_view_rule_params_root);
                }
              }
              
              //- rjf: build main column for canvas
              ui_set_next_fixed_y(-1.f * (row->skipped_size_in_rows) * scroll_list_params.row_height_px);
              ui_set_next_fixed_height((row->skipped_size_in_rows + row->size_in_rows + row->chopped_size_in_rows) * scroll_list_params.row_height_px);
              ui_set_next_child_layout_axis(Axis2_X);
              UI_Box *canvas_box = ui_build_box_from_stringf(UI_BoxFlag_FloatingY, "###canvas_%I64x", row_hash);
              UI_Parent(canvas_box) UI_WidthFill UI_HeightFill
              {
                //- rjf: loading animation container
                UI_Box *loading_overlay_container = &ui_nil_box;
                UI_Parent(canvas_box) UI_WidthFill UI_HeightFill
                {
                  loading_overlay_container = ui_build_box_from_key(UI_BoxFlag_FloatingX|UI_BoxFlag_FloatingY, ui_key_zero());
                }
                
                //- rjf: push interaction registers, fill with per-view states
                rd_push_regs();
                {
                  rd_regs()->view      = rd_handle_from_view(canvas_view);
                  rd_regs()->file_path = rd_file_path_from_eval_string(rd_frame_arena(), str8(canvas_view->query_buffer, canvas_view->query_string_size));
                }
                
                //- rjf: build
                UI_PermissionFlags(UI_PermissionFlag_Clicks|UI_PermissionFlag_ScrollX)
                {
                  ui_view_rule_info->ui(str8(canvas_view->query_buffer, canvas_view->query_string_size), canvas_view->params_roots[canvas_view->params_read_gen%ArrayCount(canvas_view->params_roots)], canvas_rect);
                }
                
                //- rjf: loading overlay fill
                UI_Parent(loading_overlay_container)
                {
                  rd_loading_overlay(canvas_rect, canvas_view->loading_t, canvas_view->loading_progress_v, canvas_view->loading_progress_v_target);
                }
                
                //- rjf: pop interaction registers
                rd_pop_regs();
              }
            }
          }
          
          ////////////////////////
          //- rjf: build button row
          //
          if(!did_row_build && row->depth == 0 && flags & RD_WatchViewFlag_RootButtons) UI_Parent(row_box) UI_HeightFill UI_FocusHot(row_selected ? UI_FocusKind_On : UI_FocusKind_Off)
          {
            did_row_build = 1;
            switch(row_eval.space.kind)
            {
              default:{did_row_build = 0;}break;
              case RD_EvalSpaceKind_MetaEntity:
              {
                RD_Entity *entity = rd_entity_from_eval_space(row_eval.space);
                UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_Clickable|UI_BoxFlag_DrawHotEffects|UI_BoxFlag_DrawActiveEffects, "###entity_%p", entity);
                UI_Parent(box) UI_Focus(UI_FocusKind_Null)
                {
                  if(row_is_expandable) UI_PrefWidth(ui_em(2.f, 1.f))
                  {
                    if(ui_pressed(ui_expanderf(next_row_expanded, "###expand_%p", entity)))
                    {
                      next_row_expanded ^= 1;
                    }
                  }
                  DR_FancyStringList fstrs = rd_title_fstrs_from_entity(scratch.arena, entity, rd_rgba_from_theme_color(RD_ThemeColor_TextWeak), ui_top_font_size());
                  UI_Box *label = ui_build_box_from_key(UI_BoxFlag_DrawText, ui_key_zero());
                  ui_box_equip_display_fancy_strings(label, &fstrs);
                  if(entity->kind == RD_EntityKind_Target) UI_PrefWidth(ui_em(3.f, 1.f))
                  {
                    rd_icon_buttonf(RD_IconKind_Play, 0, "###run");
                    rd_icon_buttonf(RD_IconKind_StepInto, 0, "###step_into");
                    rd_icon_buttonf(RD_IconKind_CheckHollow, 0, "###selected");
                  }
                }
                UI_Signal sig = ui_signal_from_box(box);
                if(ui_pressed(sig))
                {
                  RD_WatchViewPoint cell_pt = {0, row->parent_key, row->key};
                  ewv->next_cursor = ewv->next_mark = cell_pt;
                  pressed = 1;
                }
              }break;
              case RD_EvalSpaceKind_MetaCtrlEntity:
              {
                
              }break;
            }
          }
          
          ////////////////////////
          //- rjf: build regular row contents in all other cases
          //
          if(!did_row_build) UI_Parent(row_box) UI_HeightFill
          {
            did_row_build = 1;
            
            //////////////////////
            //- rjf: draw start of cache lines in expansions
            //
            if((row_eval.mode == E_Mode_Offset || row_eval.mode == E_Mode_Null) &&
               row_eval.value.u64%64 == 0 && row->depth > 0 &&
               !row_expanded)
            {
              ui_set_next_fixed_x(0);
              ui_set_next_fixed_y(0);
              ui_set_next_fixed_height(ui_top_font_size()*0.1f);
              ui_set_next_palette(ui_build_palette(ui_top_palette(), .background = rd_rgba_from_theme_color(RD_ThemeColor_HighlightOverlay)));
              ui_build_box_from_key(UI_BoxFlag_Floating|UI_BoxFlag_DrawBackground, ui_key_zero());
            }
            
            //////////////////////
            //- rjf: draw mid-row cache line boundaries in expansions
            //
            if((row_eval.mode == E_Mode_Offset || row_eval.mode == E_Mode_Null) &&
               row_eval.value.u64%64 != 0 &&
               row->depth > 0 &&
               !row_expanded)
            {
              U64 next_off = (row_eval.value.u64 + e_type_byte_size_from_key(row_eval.type_key));
              if(next_off%64 != 0 && row_eval.value.u64/64 < next_off/64)
              {
                ui_set_next_fixed_x(0);
                ui_set_next_fixed_y(scroll_list_params.row_height_px - ui_top_font_size()*0.5f);
                ui_set_next_fixed_height(ui_top_font_size()*1.f);
                Vec4F32 boundary_color = rd_rgba_from_theme_color(RD_ThemeColor_HighlightOverlay);
                ui_set_next_palette(ui_build_palette(ui_top_palette(), .background = boundary_color));
                ui_build_box_from_key(UI_BoxFlag_Floating|UI_BoxFlag_DrawBackground, ui_key_zero());
              }
            }
            
            //////////////////////
            //- rjf: build all columns
            //
            {
              S64 x = 0;
              F32 x_px = 0;
              for(RD_WatchViewColumn *col = ewv->first_column; col != 0; col = col->next, x += 1)
              {
                //- rjf: unpack cell info
                RD_WatchViewPoint cell_pt = {x, row->parent_key, row->key};
                RD_WatchViewTextEditState *cell_edit_state = rd_watch_view_text_edit_state_from_pt(ewv, cell_pt);
                B32 cell_selected = (row_selected && selection_tbl.min.x <= cell_pt.x && cell_pt.x <= selection_tbl.max.x);
                String8 cell_pre_edit_string = rd_string_from_eval_viz_row_column(scratch.arena, eval_view, row, col, 0, default_radix, ui_top_font(), ui_top_font_size(), row_string_max_size_px);
                
                //- rjf: unpack column-kind-specific info
                E_Eval cell_eval = row_eval;
                B32 cell_can_edit = 0;
                FuzzyMatchRangeList cell_matches = {0};
                String8 cell_inheritance_string = {0};
                String8 cell_error_string = {0};
                String8 cell_error_tooltip_string = {0};
                RD_AutoCompListerFlags cell_autocomp_flags = 0;
                RD_ViewRuleUIFunctionType *cell_ui_hook = 0;
                MD_Node *cell_ui_params = &md_nil_node;
                Vec4F32 cell_base_color = ui_top_palette()->text;
                RD_IconKind cell_icon = RD_IconKind_Null;
                String8 cell_ghost_text = {0};
                switch(col->kind)
                {
                  default:{}break;
                  case RD_WatchViewColumnKind_Expr:
                  {
                    cell_can_edit = (row->depth == 0 && modifiable && filter.size == 0);
                    if(filter.size != 0)
                    {
                      cell_matches = fuzzy_match_find(scratch.arena, filter, ev_expr_string_from_row(scratch.arena, row));
                    }
                    cell_autocomp_flags = (RD_AutoCompListerFlag_Locals|
                                           RD_AutoCompListerFlag_Procedures|
                                           RD_AutoCompListerFlag_Globals|
                                           RD_AutoCompListerFlag_ThreadLocals|
                                           RD_AutoCompListerFlag_Types);
                    if(row->member != 0 && row->member->inheritance_key_chain.first != 0)
                    {
                      String8List inheritance_chain_type_names = {0};
                      for(E_TypeKeyNode *n = row->member->inheritance_key_chain.first; n != 0; n = n->next)
                      {
                        String8 inherited_type_name = e_type_string_from_key(scratch.arena, n->v);
                        inherited_type_name = str8_skip_chop_whitespace(inherited_type_name);
                        str8_list_push(scratch.arena, &inheritance_chain_type_names, inherited_type_name);
                      }
                      if(inheritance_chain_type_names.node_count != 0)
                      {
                        StringJoin join = {0};
                        join.sep = str8_lit("::");
                        String8 inheritance_type = str8_list_join(scratch.arena, &inheritance_chain_type_names, &join);
                        cell_inheritance_string = inheritance_type;
                      }
                    }
                  }break;
                  case RD_WatchViewColumnKind_Value:
                  {
                  }goto value_cell;
                  case RD_WatchViewColumnKind_Member:
                  {
                    cell_eval = e_member_eval_from_eval_member_name(cell_eval, str8(col->string_buffer, col->string_size));
                  }goto value_cell;
                  value_cell:;
                  {
                    E_MsgList msgs = cell_eval.msgs;
                    if(row->depth == 0 && row->string.size != 0)
                    {
                      E_TokenArray tokens = e_token_array_from_text(scratch.arena, row->string);
                      E_Parse parse = e_parse_expr_from_text_tokens(scratch.arena, row->string, &tokens);
                      e_msg_list_concat_in_place(&parse.msgs, &msgs);
                      msgs = parse.msgs;
                    }
                    if(msgs.max_kind > E_MsgKind_Null)
                    {
                      String8List strings = {0};
                      for(E_Msg *msg = msgs.first; msg != 0; msg = msg->next)
                      {
                        str8_list_push(scratch.arena, &strings, msg->text);
                      }
                      StringJoin join = {str8_lit(""), str8_lit(" "), str8_lit("")};
                      cell_error_string = str8_list_join(scratch.arena, &strings, &join);
                    }
                    if(row_is_bad)
                    {
                      cell_error_tooltip_string = str8_lit("Could not read memory successfully.");
                    }
                    cell_autocomp_flags = RD_AutoCompListerFlag_Locals;
                    if(ui_view_rule_info->flags & RD_ViewRuleInfoFlag_CanFillValueCell)
                    {
                      cell_ui_hook = ui_view_rule_info->ui;
                      cell_ui_params = ui_view_rule_params_root;
                    }
                    cell_can_edit = ev_type_key_is_editable(cell_eval.type_key);
                  }break;
                  case RD_WatchViewColumnKind_Type:
                  {
                    cell_can_edit = 0;
                  }break;
                  case RD_WatchViewColumnKind_ViewRule:
                  {
                    cell_can_edit = 1;
                    cell_autocomp_flags = RD_AutoCompListerFlag_ViewRules;
                    if(cell_pre_edit_string.size == 0)
                    {
                      cell_ghost_text = ev_auto_view_rule_from_type_key(row_eval.type_key);
                    }
                  }break;
                  case RD_WatchViewColumnKind_FrameSelection:
                  {
                    if(semantic_idx == rd_regs()->unwind_count - rd_regs()->inline_depth)
                    {
                      cell_icon = RD_IconKind_RightArrow;
                      CTRL_Entity *thread = ctrl_entity_from_handle(d_state->ctrl_entity_store, rd_regs()->thread);
                      cell_base_color = rd_rgba_from_ctrl_entity(thread);
                    }
                  }break;
                }
                
                //- rjf: apply column-specified view rules
                if(col->view_rule_size != 0)
                {
                  String8 col_view_rule = str8(col->view_rule_buffer, col->view_rule_size);
                  EV_ViewRuleList *view_rules = ev_view_rule_list_from_string(scratch.arena, col_view_rule);
                  for(EV_ViewRuleNode *n = view_rules->first; n != 0; n = n->next)
                  {
                    EV_ViewRule *vr = &n->v;
                    RD_ViewRuleInfo *info = rd_view_rule_info_from_string(vr->root->string);
                    if(info->flags & RD_ViewRuleInfoFlag_CanFillValueCell && info->ui != 0)
                    {
                      cell_ui_hook = info->ui;
                      cell_ui_params = vr->root;
                    }
                  }
                }
                
                //- rjf: determine cell's palette
                UI_BoxFlags cell_flags = 0;
                UI_Palette *palette = ui_top_palette();
                {
                  if(cell_error_tooltip_string.size != 0 ||
                     cell_error_string.size != 0)
                  {
                    palette = ui_build_palette(ui_top_palette(), .text = rd_rgba_from_theme_color(RD_ThemeColor_TextNegative), .text_weak = rd_rgba_from_theme_color(RD_ThemeColor_TextNegative), .background = rd_rgba_from_theme_color(RD_ThemeColor_HighlightOverlayError));
                    cell_flags |= UI_BoxFlag_DrawBackground;
                  }
                  else
                  {
                    palette = ui_build_palette(ui_top_palette(), .text = cell_base_color);
                  }
                }
                
                //- rjf: determine if cell is code
                B32 cell_is_code = !col->is_non_code;
                if(flags & RD_WatchViewFlag_PrettyNameMembers && cell_is_code && row->member != 0 && row->member->pretty_name.size != 0 && col->kind == RD_WatchViewColumnKind_Expr)
                {
                  cell_is_code = 0;
                }
                if(col->kind == RD_WatchViewColumnKind_Value && row_type->flags & E_TypeFlag_IsCode)
                {
                  cell_is_code = 1;
                }
                
                //- rjf: build cell
                UI_Signal sig = {0};
                UI_Palette(palette) UI_TableCell
                  UI_FocusHot(cell_selected ? UI_FocusKind_On : UI_FocusKind_Off)
                  UI_FocusActive((cell_selected && ewv->text_editing) ? UI_FocusKind_On : UI_FocusKind_Off)
                  RD_Font(cell_is_code ? RD_FontSlot_Code : RD_FontSlot_Main)
                  UI_FlagsAdd(cell_flags | (row->depth > 0 ? UI_BoxFlag_DrawTextWeak : 0))
                {
                  // rjf: cell has errors? -> build error box
                  if(cell_error_string.size != 0) RD_Font(RD_FontSlot_Main)
                  {
                    UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_Clip|UI_BoxFlag_Clickable, "###%I64x_row_%I64x", x, row_hash);
                    sig = ui_signal_from_box(box);
                    UI_Parent(box) UI_Flags(0)
                    {
                      rd_error_label(cell_error_string);
                    }
                  }
                  
                  // rjf: cell has hook? -> build ui by calling hook
                  else if(cell_ui_hook != 0)
                  {
                    UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_Clip|UI_BoxFlag_Clickable, "###val_%I64x", row_hash);
                    UI_Parent(box)
                    {
                      String8 row_expr = e_string_from_expr(scratch.arena, row->expr);
                      cell_ui_hook(row_expr, cell_ui_params, r2f32p(x_px, 0, x_px + col->pct*dim_2f32(rect).x, row_height_px));
                    }
                    sig = ui_signal_from_box(box);
                  }
                  
                  // rjf: cell has icon? build icon
                  else if(cell_icon != RD_IconKind_Null)
                  {
                    UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_Clickable, "###cell_%I64x", row_hash);
                    UI_Parent(box) RD_Font(RD_FontSlot_Icons) UI_WidthFill UI_TextAlignment(UI_TextAlign_Center)
                    {
                      ui_label(rd_icon_kind_text_table[cell_icon]);
                    }
                    sig = ui_signal_from_box(box);
                  }
                  
                  // rjf: build cell line edit
                  else
                  {
                    sig = rd_line_editf((RD_LineEditFlag_CodeContents*(!!cell_is_code)|
                                         RD_LineEditFlag_NoBackground|
                                         RD_LineEditFlag_DisableEdit*(!cell_can_edit)|
                                         RD_LineEditFlag_Expander*!!(x == 0 && row_is_expandable && col->kind == RD_WatchViewColumnKind_Expr)|
                                         RD_LineEditFlag_ExpanderPlaceholder*(x == 0 && row->depth==0 && col->kind == RD_WatchViewColumnKind_Expr)|
                                         RD_LineEditFlag_ExpanderSpace*(x == 0 && row->depth!=0 && col->kind == RD_WatchViewColumnKind_Expr)),
                                        x == 0 ? row->depth : 0,
                                        &cell_matches,
                                        &cell_edit_state->cursor, &cell_edit_state->mark, cell_edit_state->input_buffer, sizeof(cell_edit_state->input_buffer), &cell_edit_state->input_size, &next_row_expanded,
                                        cell_pre_edit_string,
                                        "%S###%I64x_row_%I64x", cell_ghost_text, x, row_hash);
                    if(ui_is_focus_active() &&
                       selection_tbl.min.x == selection_tbl.max.x && selection_tbl.min.y == selection_tbl.max.y &&
                       txt_pt_match(cell_edit_state->cursor, cell_edit_state->mark))
                    {
                      String8 input = str8(cell_edit_state->input_buffer, cell_edit_state->input_size);
                      RD_AutoCompListerParams params = {cell_autocomp_flags};
                      if(col->kind == RD_WatchViewColumnKind_ViewRule)
                      {
                        params = rd_view_rule_autocomp_lister_params_from_input_cursor(scratch.arena, input, cell_edit_state->cursor.column-1);
                        if(params.flags == 0)
                        {
                          params.flags = cell_autocomp_flags;
                        }
                      }
                      rd_set_autocomp_lister_query(sig.box->key, &params, input, cell_edit_state->cursor.column-1);
                    }
                  }
                }
                
                //- rjf: handle interactions
                {
                  // rjf: single-click -> move selection here
                  if(ui_pressed(sig))
                  {
                    ewv->next_cursor = ewv->next_mark = cell_pt;
                    pressed = 1;
                  }
                  
                  // rjf: double-click -> start editing
                  if(ui_double_clicked(sig) && cell_can_edit)
                  {
                    ui_kill_action();
                    rd_cmd(RD_CmdKind_Edit);
                  }
                  
                  // rjf: double-click, not editable -> go-to-location
                  if(ui_double_clicked(sig) && !cell_can_edit)
                  {
                    U64 vaddr = cell_eval.value.u64;
                    CTRL_Entity *process = rd_ctrl_entity_from_eval_space(cell_eval.space);
                    CTRL_Entity *module = ctrl_module_from_process_vaddr(process, vaddr);
                    DI_Key dbgi_key = ctrl_dbgi_key_from_module(module);
                    U64 voff = ctrl_voff_from_vaddr(module, vaddr);
                    D_LineList lines = d_lines_from_dbgi_key_voff(scratch.arena, &dbgi_key, voff);
                    String8 file_path = {0};
                    TxtPt pt = {0};
                    if(lines.first != 0)
                    {
                      file_path = lines.first->v.file_path;
                      pt        = lines.first->v.pt;
                    }
                    rd_cmd(RD_CmdKind_FindCodeLocation,
                           .process    = process->handle,
                           .vaddr      = vaddr,
                           .file_path  = file_path,
                           .cursor     = pt);
                  }
                  
                  // rjf: double-click, not editable, callstack frame -> select frame
                  if(ui_double_clicked(sig) && !cell_can_edit && semantic_idx < frame_rows_count)
                  {
                    FrameRow *frame_row = &frame_rows[semantic_idx];
                    rd_cmd(RD_CmdKind_SelectUnwind,
                           .unwind_count = frame_row->unwind_idx,
                           .inline_depth = frame_row->inline_depth);
                  }
                  
                  // rjf: hovering with inheritance string -> show tooltip
                  if(ui_hovering(sig) && cell_inheritance_string.size != 0) UI_Tooltip
                  {
                    UI_PrefWidth(ui_children_sum(1)) UI_Row UI_PrefWidth(ui_text_dim(1, 1))
                    {
                      ui_labelf("Inherited from ");
                      RD_Font(RD_FontSlot_Code) rd_code_label(1.f, 0, rd_rgba_from_theme_color(RD_ThemeColor_CodeDefault), cell_inheritance_string);
                    }
                  }
                  
                  // rjf: hovering with error tooltip -> show tooltip
                  if(ui_hovering(sig) && cell_error_tooltip_string.size != 0) UI_Tooltip
                  {
                    UI_PrefWidth(ui_children_sum(1)) rd_error_label(cell_error_tooltip_string);
                  }
                }
                
                //- rjf: bump x pixel coordinate
                x_px += col->pct*dim_2f32(rect).x;
                
                //- rjf: [DEV] hovering -> tooltips
                if(DEV_eval_compiler_tooltips && x == 0 && ui_hovering(sig)) UI_Tooltip RD_Font(RD_FontSlot_Code)
                {
                  local_persist char *spaces = "                                                                        ";
                  String8         string      = ev_expr_string_from_row(scratch.arena, row);
                  E_TokenArray    tokens      = e_token_array_from_text(scratch.arena, string);
                  E_Parse         parse       = e_parse_expr_from_text_tokens(scratch.arena, string, &tokens);
                  E_IRTreeAndType irtree      = e_irtree_and_type_from_expr(scratch.arena, parse.expr);
                  E_OpList        oplist      = e_oplist_from_irtree(scratch.arena, irtree.root);
                  String8         bytecode    = e_bytecode_from_oplist(scratch.arena, &oplist);
                  UI_Flags(UI_BoxFlag_DrawTextWeak) ui_labelf("Text:");
                  ui_label(string);
                  ui_spacer(ui_em(2.f, 1.f));
                  UI_Flags(UI_BoxFlag_DrawTextWeak) ui_labelf("Tokens:");
                  for(U64 idx = 0; idx < tokens.count; idx += 1)
                  {
                    ui_labelf("%S: '%S'", e_token_kind_strings[tokens.v[idx].kind], str8_substr(string, tokens.v[idx].range));
                  }
                  ui_spacer(ui_em(2.f, 1.f));
                  UI_Flags(UI_BoxFlag_DrawTextWeak) ui_labelf("Expression:");
                  {
                    typedef struct Task Task;
                    struct Task
                    {
                      Task *next;
                      Task *prev;
                      E_Expr *expr;
                      S64 depth;
                    };
                    Task start_task = {0, 0, parse.expr};
                    Task *first_task = &start_task;
                    Task *last_task = first_task;
                    for(Task *t = first_task; t != 0; t = t->next)
                    {
                      String8 ext = {0};
                      switch(t->expr->kind)
                      {
                        default:
                        {
                          if(t->expr->string.size != 0)
                          {
                            ext = push_str8f(scratch.arena, "'%S'", t->expr->string);
                          }
                          else if(t->expr->value.u32 != 0)
                          {
                            ext = push_str8f(scratch.arena, "0x%x", t->expr->value.u32);
                          }
                          else if(t->expr->value.f32 != 0)
                          {
                            ext = push_str8f(scratch.arena, "%f", t->expr->value.f32);
                          }
                          else if(t->expr->value.f64 != 0)
                          {
                            ext = push_str8f(scratch.arena, "%f", t->expr->value.f64);
                          }
                          else if(t->expr->value.u64 != 0)
                          {
                            ext = push_str8f(scratch.arena, "0x%I64x", t->expr->value.u64);
                          }
                        }break;
                      }
                      ui_labelf("%.*s%S%s%S", (int)t->depth*2, spaces, e_expr_kind_strings[t->expr->kind], ext.size ? " " : "", ext);
                      for(E_Expr *child = t->expr->first; child != &e_expr_nil; child = child->next)
                      {
                        Task *task = push_array(scratch.arena, Task, 1);
                        task->expr = child;
                        task->depth = t->depth+1;
                        DLLInsert(first_task, last_task, t, task);
                      }
                    }
                  }
                  ui_spacer(ui_em(2.f, 1.f));
                  UI_Flags(UI_BoxFlag_DrawTextWeak) ui_labelf("IR Tree:");
                  {
                    typedef struct Task Task;
                    struct Task
                    {
                      Task *next;
                      Task *prev;
                      E_IRNode *node;
                      S64 depth;
                    };
                    Task start_task = {0, 0, irtree.root};
                    Task *first_task = &start_task;
                    Task *last_task = first_task;
                    for(Task *t = first_task; t != 0; t = t->next)
                    {
                      String8 op_string = {0};
                      switch(t->node->op)
                      {
                        default:{}break;
                        case E_IRExtKind_Bytecode:{op_string = str8_lit("Bytecode");}break;
                        case E_IRExtKind_SetSpace:{op_string = str8_lit("SetSpace");}break;
#define X(name) case RDI_EvalOp_##name:{op_string = str8_lit(#name);}break;
                        RDI_EvalOp_XList
#undef X
                      }
                      String8 ext = {0};
                      ui_labelf("%.*s%S", (int)t->depth*2, spaces, op_string);
                      for(E_IRNode *child = t->node->first; child != &e_irnode_nil; child = child->next)
                      {
                        Task *task = push_array(scratch.arena, Task, 1);
                        task->node = child;
                        task->depth = t->depth+1;
                        DLLInsert(first_task, last_task, t, task);
                      }
                    }
                  }
                  ui_spacer(ui_em(2.f, 1.f));
                  UI_Flags(UI_BoxFlag_DrawTextWeak) ui_labelf("Op List:");
                  {
                    for(E_Op *op = oplist.first; op != 0; op = op->next)
                    {
                      String8 op_string = {0};
                      switch(op->opcode)
                      {
                        default:{}break;
                        case E_IRExtKind_Bytecode:{op_string = str8_lit("Bytecode");}break;
                        case E_IRExtKind_SetSpace:{op_string = str8_lit("SetSpace");}break;
#define X(name) case RDI_EvalOp_##name:{op_string = str8_lit(#name);}break;
                        RDI_EvalOp_XList
#undef X
                      }
                      String8 ext = {0};
                      switch(op->opcode)
                      {
                        case E_IRExtKind_Bytecode:{ext = str8_lit("[bytecode]");}break;
                        default:
                        {
                          ext = str8_from_u64(scratch.arena, op->value.u64, 16, 0, 0);
                        }break;
                      }
                      ui_labelf("  %S%s%S", op_string, ext.size ? " " : "", ext);
                    }
                  }
                  ui_spacer(ui_em(2.f, 1.f));
                  UI_Flags(UI_BoxFlag_DrawTextWeak) ui_labelf("Bytecode:");
                  {
                    for(U64 idx = 0; idx < bytecode.size; idx += 1)
                    {
                      ui_labelf("  0x%x ('%c')", (U32)bytecode.str[idx], (char)bytecode.str[idx]);
                    }
                  }
                }
              }
            }
          }
          
          //////////////////////
          //- rjf: commit expansion state changes
          //
          if(next_row_expanded != row_expanded)
          {
            ev_key_set_expansion(eval_view, row->parent_key, row->key, next_row_expanded);
          }
        }
      }
    }
  }
  
  //////////////////////////////
  //- rjf: general table-wide press logic
  //
  if(!is_top_level_hook) if(pressed)
  {
    rd_cmd(RD_CmdKind_FocusPanel);
  }
  
  if(!is_top_level_hook) { rd_store_view_scroll_pos(scroll_pos); }
  scratch_end(scratch);
  fzy_scope_close(fzy_scope);
  di_scope_close(di_scope);
  ProfEnd();
}

////////////////////////////////
//~ rjf: null @view_hook_impl

RD_VIEW_RULE_UI_FUNCTION_DEF(null) {}

////////////////////////////////
//~ rjf: empty @view_hook_impl

RD_VIEW_RULE_UI_FUNCTION_DEF(empty)
{
  ui_set_next_flags(UI_BoxFlag_DefaultFocusNav);
  UI_Focus(UI_FocusKind_On) UI_WidthFill UI_HeightFill UI_NamedColumn(str8_lit("empty_view")) UI_FlagsAdd(UI_BoxFlag_DrawTextWeak)
    UI_Padding(ui_pct(1, 0)) UI_Focus(UI_FocusKind_Null)
  {
    UI_PrefHeight(ui_em(3.f, 1.f))
      UI_Row
      UI_Padding(ui_pct(1, 0))
      UI_TextAlignment(UI_TextAlign_Center)
      UI_PrefWidth(ui_em(15.f, 1.f))
      UI_CornerRadius(ui_top_font_size()/2.f)
      RD_Palette(RD_PaletteCode_NegativePopButton)
    {
      if(ui_clicked(rd_icon_buttonf(RD_IconKind_X, 0, "Close Panel")))
      {
        rd_cmd(RD_CmdKind_ClosePanel);
      }
    }
  }
}

////////////////////////////////
//~ rjf: getting_started @view_hook_impl

RD_VIEW_RULE_UI_FUNCTION_DEF(getting_started)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(0, 0);
  ui_set_next_flags(UI_BoxFlag_DefaultFocusNav);
  UI_Focus(UI_FocusKind_On) UI_WidthFill UI_HeightFill UI_NamedColumn(str8_lit("empty_view"))
    UI_FlagsAdd(UI_BoxFlag_DrawTextWeak)
    UI_Padding(ui_pct(1, 0)) UI_Focus(UI_FocusKind_Null)
  {
    RD_EntityList targets = rd_push_active_target_list(scratch.arena);
    RD_EntityList processes = rd_query_cached_entity_list_with_kind(RD_EntityKind_Process);
    
    //- rjf: icon & info
    UI_Padding(ui_em(2.f, 1.f))
    {
      //- rjf: icon
      {
        F32 icon_dim = ui_top_font_size()*10.f;
        UI_PrefHeight(ui_px(icon_dim, 1.f))
          UI_Row
          UI_Padding(ui_pct(1, 0))
          UI_PrefWidth(ui_px(icon_dim, 1.f))
        {
          R_Handle texture = rd_state->icon_texture;
          Vec2S32 texture_dim = r_size_from_tex2d(texture);
          ui_image(texture, R_Tex2DSampleKind_Linear, r2f32p(0, 0, texture_dim.x, texture_dim.y), v4f32(1, 1, 1, 1), 0, str8_lit(""));
        }
      }
      
      //- rjf: info
      UI_Padding(ui_em(2.f, 1.f))
        UI_WidthFill UI_PrefHeight(ui_em(2.f, 1.f))
        UI_Row
        UI_Padding(ui_pct(1, 0))
        UI_TextAlignment(UI_TextAlign_Center)
        UI_PrefWidth(ui_text_dim(10, 1))
      {
        ui_label(str8_lit(BUILD_TITLE_STRING_LITERAL));
      }
    }
    
    //- rjf: targets state dependent helper
    B32 helper_built = 0;
    if(processes.count == 0)
    {
      helper_built = 1;
      switch(targets.count)
      {
        //- rjf: user has no targets. build helper for adding them
        case 0:
        {
          UI_PrefHeight(ui_em(3.75f, 1.f))
            UI_Row
            UI_Padding(ui_pct(1, 0))
            UI_TextAlignment(UI_TextAlign_Center)
            UI_PrefWidth(ui_em(22.f, 1.f))
            UI_CornerRadius(ui_top_font_size()/2.f)
            RD_Palette(RD_PaletteCode_NeutralPopButton)
            if(ui_clicked(rd_icon_buttonf(RD_IconKind_Add, 0, "Add Target")))
          {
            rd_cmd(RD_CmdKind_RunCommand, .string = rd_cmd_kind_info_table[RD_CmdKind_AddTarget].string);
          }
        }break;
        
        //- rjf: user has 1 target. build helper for launching it
        case 1:
        {
          RD_Entity *target = rd_first_entity_from_list(&targets);
          String8 target_full_path = target->string;
          String8 target_name = str8_skip_last_slash(target_full_path);
          UI_PrefHeight(ui_em(3.75f, 1.f))
            UI_Row
            UI_Padding(ui_pct(1, 0))
            UI_TextAlignment(UI_TextAlign_Center)
            UI_PrefWidth(ui_em(22.f, 1.f))
            UI_CornerRadius(ui_top_font_size()/2.f)
            RD_Palette(RD_PaletteCode_PositivePopButton)
          {
            if(ui_clicked(rd_icon_buttonf(RD_IconKind_Play, 0, "Launch %S", target_name)))
            {
              rd_cmd(RD_CmdKind_LaunchAndRun, .entity = rd_handle_from_entity(target));
            }
            ui_spacer(ui_em(1.5f, 1));
            if(ui_clicked(rd_icon_buttonf(RD_IconKind_Play, 0, "Step Into %S", target_name)))
            {
              rd_cmd(RD_CmdKind_LaunchAndInit, .entity = rd_handle_from_entity(target));
            }
          }
        }break;
        
        //- rjf: user has N targets.
        default:
        {
          helper_built = 0;
        }break;
      }
    }
    
    //- rjf: or text
    if(helper_built)
    {
      UI_PrefHeight(ui_em(2.25f, 1.f))
        UI_Row
        UI_Padding(ui_pct(1, 0))
        UI_TextAlignment(UI_TextAlign_Center)
        UI_WidthFill
        ui_labelf("- or -");
    }
    
    //- rjf: helper text for command lister activation
    UI_PrefHeight(ui_em(2.25f, 1.f)) UI_Row
      UI_PrefWidth(ui_text_dim(10, 1))
      UI_TextAlignment(UI_TextAlign_Center)
      UI_Padding(ui_pct(1, 0))
    {
      ui_labelf("use");
      UI_Flags(UI_BoxFlag_DrawBorder) UI_TextAlignment(UI_TextAlign_Center) rd_cmd_binding_buttons(rd_cmd_kind_info_table[RD_CmdKind_RunCommand].string);
      ui_labelf("to open command menu");
    }
  }
  scratch_end(scratch);
  ProfEnd();
}

////////////////////////////////
//~ rjf: commands @view_hook_impl

typedef struct RD_CmdListerItem RD_CmdListerItem;
struct RD_CmdListerItem
{
  String8 cmd_name;
  U64 registrar_idx;
  U64 ordering_idx;
  FuzzyMatchRangeList name_match_ranges;
  FuzzyMatchRangeList desc_match_ranges;
  FuzzyMatchRangeList tags_match_ranges;
};

typedef struct RD_CmdListerItemNode RD_CmdListerItemNode;
struct RD_CmdListerItemNode
{
  RD_CmdListerItemNode *next;
  RD_CmdListerItem item;
};

typedef struct RD_CmdListerItemList RD_CmdListerItemList;
struct RD_CmdListerItemList
{
  RD_CmdListerItemNode *first;
  RD_CmdListerItemNode *last;
  U64 count;
};

typedef struct RD_CmdListerItemArray RD_CmdListerItemArray;
struct RD_CmdListerItemArray
{
  RD_CmdListerItem *v;
  U64 count;
};

internal RD_CmdListerItemList
rd_cmd_lister_item_list_from_needle(Arena *arena, String8 needle)
{
  Temp scratch = scratch_begin(&arena, 1);
  RD_CmdListerItemList result = {0};
  // TODO(rjf): extend this with dynamically-registered command info
  for EachNonZeroEnumVal(RD_CmdKind, k)
  {
    RD_CmdKindInfo *info = &rd_cmd_kind_info_table[k];
    if(info->flags & RD_CmdKindFlag_ListInUI)
    {
      String8 cmd_display_name = info->display_name;
      String8 cmd_desc = info->description;
      String8 cmd_tags = info->search_tags;
      FuzzyMatchRangeList name_matches = fuzzy_match_find(arena, needle, cmd_display_name);
      FuzzyMatchRangeList desc_matches = fuzzy_match_find(arena, needle, cmd_desc);
      FuzzyMatchRangeList tags_matches = fuzzy_match_find(arena, needle, cmd_tags);
      if(name_matches.count == name_matches.needle_part_count ||
         desc_matches.count == name_matches.needle_part_count ||
         tags_matches.count > 0 ||
         name_matches.needle_part_count == 0)
      {
        RD_CmdListerItemNode *node = push_array(arena, RD_CmdListerItemNode, 1);
        node->item.cmd_name = info->string;
        node->item.registrar_idx = (U64)k;
        node->item.ordering_idx = (U64)k;
        node->item.name_match_ranges = name_matches;
        node->item.desc_match_ranges = desc_matches;
        node->item.tags_match_ranges = tags_matches;
        SLLQueuePush(result.first, result.last, node);
        result.count += 1;
      }
    }
  }
  scratch_end(scratch);
  return result;
}

internal RD_CmdListerItemArray
rd_cmd_lister_item_array_from_list(Arena *arena, RD_CmdListerItemList list)
{
  RD_CmdListerItemArray result = {0};
  result.count = list.count;
  result.v = push_array(arena, RD_CmdListerItem, result.count);
  U64 idx = 0;
  for(RD_CmdListerItemNode *n = list.first; n != 0; n = n->next, idx += 1)
  {
    result.v[idx] = n->item;
  }
  return result;
}

internal int
rd_qsort_compare_cmd_lister__strength(RD_CmdListerItem *a, RD_CmdListerItem *b)
{
  int result = 0;
  if(a->name_match_ranges.count > b->name_match_ranges.count)
  {
    result = -1;
  }
  else if(a->name_match_ranges.count < b->name_match_ranges.count)
  {
    result = +1;
  }
  else if(a->desc_match_ranges.count > b->desc_match_ranges.count)
  {
    result = -1;
  }
  else if(a->desc_match_ranges.count < b->desc_match_ranges.count)
  {
    result = +1;
  }
  else if(a->tags_match_ranges.count > b->tags_match_ranges.count)
  {
    result = -1;
  }
  else if(a->tags_match_ranges.count < b->tags_match_ranges.count)
  {
    result = +1;
  }
  else if(a->registrar_idx < b->registrar_idx)
  {
    result = -1;
  }
  else if(a->registrar_idx > b->registrar_idx)
  {
    result = +1;
  }
  else if(a->ordering_idx < b->ordering_idx)
  {
    result = -1;
  }
  else if(a->ordering_idx > b->ordering_idx)
  {
    result = +1;
  }
  return result;
}

internal void
rd_cmd_lister_item_array_sort_by_strength__in_place(RD_CmdListerItemArray array)
{
  quick_sort(array.v, array.count, sizeof(RD_CmdListerItem), rd_qsort_compare_cmd_lister__strength);
}

RD_VIEW_RULE_UI_FUNCTION_DEF(commands)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(0, 0);
  
  //- rjf: grab state
  typedef struct RD_CmdsViewState RD_CmdsViewState;
  struct RD_CmdsViewState
  {
    U64 selected_cmd_hash;
  };
  UI_ScrollPt2 scroll_pos = rd_view_scroll_pos();
  RD_CmdsViewState *cv = rd_view_state(RD_CmdsViewState);
  
  //- rjf: build filtered array of commands
  RD_CmdListerItemList cmd_list = rd_cmd_lister_item_list_from_needle(scratch.arena, string);
  RD_CmdListerItemArray cmd_array = rd_cmd_lister_item_array_from_list(scratch.arena, cmd_list);
  rd_cmd_lister_item_array_sort_by_strength__in_place(cmd_array);
  
  //- rjf: submit best match when hitting enter w/ no selection
  if(cv->selected_cmd_hash == 0 && ui_slot_press(UI_EventActionSlot_Accept))
  {
    rd_cmd(RD_CmdKind_CompleteQuery, .string = (cmd_array.count > 0 ? cmd_array.v[0].cmd_name : str8_zero()));
  }
  
  //- rjf: selected kind -> cursor
  Vec2S64 cursor = {0};
  {
    for(U64 idx = 0; idx < cmd_array.count; idx += 1)
    {
      if(d_hash_from_string(cmd_array.v[idx].cmd_name) == cv->selected_cmd_hash)
      {
        cursor.y = (S64)idx+1;
        break;
      }
    }
  }
  
  //- rjf: build contents
  Rng1S64 visible_row_range = {0};
  UI_ScrollListParams scroll_list_params = {0};
  {
    scroll_list_params.flags         = UI_ScrollListFlag_All;
    scroll_list_params.row_height_px = floor_f32(ui_top_font_size()*6.5f);
    scroll_list_params.dim_px        = dim_2f32(rect);
    scroll_list_params.cursor_range  = r2s64(v2s64(0, 0), v2s64(0, cmd_array.count));
    scroll_list_params.item_range    = r1s64(0, cmd_array.count);
    scroll_list_params.cursor_min_is_empty_selection[Axis2_Y] = 1;
  }
  UI_ScrollListSignal scroll_list_sig = {0};
  UI_Focus(UI_FocusKind_On)
    UI_ScrollList(&scroll_list_params,
                  &scroll_pos.y,
                  &cursor,
                  0,
                  &visible_row_range,
                  &scroll_list_sig)
    UI_Focus(UI_FocusKind_Null)
  {
    //- rjf: build buttons
    for(S64 row_idx = visible_row_range.min;
        row_idx <= visible_row_range.max && row_idx < cmd_array.count;
        row_idx += 1)
    {
      RD_CmdListerItem *item = &cmd_array.v[row_idx];
      RD_CmdKindInfo *info = rd_cmd_kind_info_from_string(item->cmd_name);
      
      //- rjf: build row contents
      ui_set_next_hover_cursor(OS_Cursor_HandPoint);
      ui_set_next_child_layout_axis(Axis2_X);
      UI_Box *box = &ui_nil_box;
      UI_Focus(cursor.y == row_idx+1 ? UI_FocusKind_On : UI_FocusKind_Off)
      {
        box = ui_build_box_from_stringf(UI_BoxFlag_Clickable|
                                        UI_BoxFlag_DrawBorder|
                                        UI_BoxFlag_DrawBackground|
                                        UI_BoxFlag_DrawHotEffects|
                                        UI_BoxFlag_DrawActiveEffects,
                                        "###cmd_button_%S", item->cmd_name);
      }
      UI_Parent(box) UI_PrefHeight(ui_em(1.65f, 1.f))
      {
        //- rjf: icon
        UI_PrefWidth(ui_em(3.f, 1.f))
          UI_HeightFill
          UI_Column
          RD_Font(RD_FontSlot_Icons)
          UI_FontSize(rd_font_size_from_slot(RD_FontSlot_Icons))
          UI_FlagsAdd(UI_BoxFlag_DrawTextWeak)
          UI_HeightFill
          UI_TextAlignment(UI_TextAlign_Center)
        {
          RD_IconKind icon = info->icon_kind;
          if(icon != RD_IconKind_Null)
          {
            ui_label(rd_icon_kind_text_table[icon]);
          }
        }
        
        //- rjf: name + description
        ui_set_next_pref_height(ui_pct(1, 0));
        UI_Column UI_Padding(ui_pct(1, 0))
        {
          FNT_Tag font = ui_top_font();
          F32 font_size = ui_top_font_size();
          FNT_Metrics font_metrics = fnt_metrics_from_tag_size(font, font_size);
          F32 font_line_height = fnt_line_height_from_metrics(&font_metrics);
          String8 cmd_display_name = info->display_name;
          String8 cmd_desc = info->description;
          UI_Box *name_box = ui_build_box_from_stringf(UI_BoxFlag_DrawText, "%S##name_%S", cmd_display_name, info->string);
          UI_Box *desc_box = &ui_nil_box;
          UI_PrefHeight(ui_em(1.8f, 1.f)) UI_FlagsAdd(UI_BoxFlag_DrawTextWeak)
          {
            desc_box = ui_build_box_from_stringf(UI_BoxFlag_DrawText, "%S##desc_%S", cmd_desc, info->string);
          }
          ui_box_equip_fuzzy_match_ranges(name_box, &item->name_match_ranges);
          ui_box_equip_fuzzy_match_ranges(desc_box, &item->desc_match_ranges);
        }
        
        //- rjf: bindings
        ui_set_next_flags(UI_BoxFlag_Clickable);
        UI_PrefWidth(ui_children_sum(1.f)) UI_HeightFill UI_NamedColumn(str8_lit("binding_column")) UI_Padding(ui_em(1.5f, 1.f))
        {
          ui_set_next_flags(UI_BoxFlag_Clickable);
          UI_NamedRow(str8_lit("binding_row")) UI_Padding(ui_em(1.f, 1.f))
          {
            rd_cmd_binding_buttons(item->cmd_name);
          }
        }
      }
      
      //- rjf: interact
      UI_Signal sig = ui_signal_from_box(box);
      if(ui_clicked(sig))
      {
        rd_cmd(RD_CmdKind_CompleteQuery, .string = item->cmd_name);
      }
    }
  }
  
  //- rjf: map selected num -> selected kind
  if(1 <= cursor.y && cursor.y <= cmd_array.count)
  {
    cv->selected_cmd_hash = d_hash_from_string(cmd_array.v[cursor.y-1].cmd_name);
  }
  else
  {
    cv->selected_cmd_hash = 0;
  }
  
  rd_store_view_scroll_pos(scroll_pos);
  scratch_end(scratch);
  ProfEnd();
}

////////////////////////////////
//~ rjf: file_system @view_hook_impl

typedef enum RD_FileSortKind
{
  RD_FileSortKind_Null,
  RD_FileSortKind_Filename,
  RD_FileSortKind_LastModified,
  RD_FileSortKind_Size,
  RD_FileSortKind_COUNT
}
RD_FileSortKind;

typedef struct RD_FileInfo RD_FileInfo;
struct RD_FileInfo
{
  String8 filename;
  FileProperties props;
  FuzzyMatchRangeList match_ranges;
};

typedef struct RD_FileInfoNode RD_FileInfoNode;
struct RD_FileInfoNode
{
  RD_FileInfoNode *next;
  RD_FileInfo file_info;
};

typedef struct RD_FileSystemViewPathState RD_FileSystemViewPathState;
struct RD_FileSystemViewPathState
{
  RD_FileSystemViewPathState *hash_next;
  String8 normalized_path;
  Vec2S64 cursor;
};

typedef struct RD_FileSystemViewState RD_FileSystemViewState;
struct RD_FileSystemViewState
{
  B32 initialized;
  U64 path_state_table_size;
  RD_FileSystemViewPathState **path_state_table;
  RD_FileSortKind sort_kind;
  Side sort_side;
  Arena *cached_files_arena;
  String8 cached_files_path;
  RD_FileSortKind cached_files_sort_kind;
  Side cached_files_sort_side;
  U64 cached_file_count;
  RD_FileInfo *cached_files;
  F32 col_pcts[3];
};

typedef struct RD_PathQuery RD_PathQuery;
struct RD_PathQuery
{
  String8 prefix;
  String8 path;
  String8 search;
};

internal RD_PathQuery
rd_path_query_from_string(String8 string)
{
  String8 dir_str_in_input = {0};
  for(U64 i = 0; i < string.size; i += 1)
  {
    String8 substr1 = str8_substr(string, r1u64(i, i+1));
    String8 substr2 = str8_substr(string, r1u64(i, i+2));
    String8 substr3 = str8_substr(string, r1u64(i, i+3));
    if(str8_match(substr1, str8_lit("/"), StringMatchFlag_SlashInsensitive))
    {
      dir_str_in_input = str8_substr(string, r1u64(i, string.size));
    }
    else if(i != 0 && str8_match(substr2, str8_lit(":/"), StringMatchFlag_SlashInsensitive))
    {
      dir_str_in_input = str8_substr(string, r1u64(i-1, string.size));
    }
    else if(str8_match(substr2, str8_lit("./"), StringMatchFlag_SlashInsensitive))
    {
      dir_str_in_input = str8_substr(string, r1u64(i, string.size));
    }
    else if(str8_match(substr3, str8_lit("../"), StringMatchFlag_SlashInsensitive))
    {
      dir_str_in_input = str8_substr(string, r1u64(i, string.size));
    }
    if(dir_str_in_input.size != 0)
    {
      break;
    }
  }
  
  RD_PathQuery path_query = {0};
  if(dir_str_in_input.size != 0)
  {
    String8 dir = dir_str_in_input;
    String8 search = {0};
    U64 one_past_last_slash = dir.size;
    for(U64 i = 0; i < dir_str_in_input.size; i += 1)
    {
      if(dir_str_in_input.str[i] == '/' || dir_str_in_input.str[i] == '\\')
      {
        one_past_last_slash = i+1;
      }
    }
    dir.size = one_past_last_slash;
    search = str8_substr(dir_str_in_input, r1u64(one_past_last_slash, dir_str_in_input.size));
    path_query.path = dir;
    path_query.search = search;
    path_query.prefix = str8_substr(string, r1u64(0, path_query.path.str - string.str));
  }
  return path_query;
}

internal int
rd_qsort_compare_file_info__filename(RD_FileInfo *a, RD_FileInfo *b)
{
  return strncmp((char *)a->filename.str, (char *)b->filename.str, Min(a->filename.size, b->filename.size));
}

internal int
rd_qsort_compare_file_info__default(RD_FileInfo *a, RD_FileInfo *b)
{
  int result = 0;
  if(a->props.flags & FilePropertyFlag_IsFolder && !(b->props.flags & FilePropertyFlag_IsFolder))
  {
    result = -1;
  }
  else if(b->props.flags & FilePropertyFlag_IsFolder && !(a->props.flags & FilePropertyFlag_IsFolder))
  {
    result = +1;
  }
  else
  {
    result = rd_qsort_compare_file_info__filename(a, b);
  }
  return result;
}

internal int
rd_qsort_compare_file_info__default_filtered(RD_FileInfo *a, RD_FileInfo *b)
{
  int result = 0;
  if(a->filename.size < b->filename.size)
  {
    result = -1;
  }
  else if(a->filename.size > b->filename.size)
  {
    result = +1;
  }
  return result;
}

internal int
rd_qsort_compare_file_info__last_modified(RD_FileInfo *a, RD_FileInfo *b)
{
  return ((a->props.modified < b->props.modified) ? -1 :
          (a->props.modified > b->props.modified) ? +1 :
          0);
}

internal int
rd_qsort_compare_file_info__size(RD_FileInfo *a, RD_FileInfo *b)
{
  return ((a->props.size < b->props.size) ? -1 :
          (a->props.size > b->props.size) ? +1 :
          0);
}

RD_VIEW_RULE_UI_FUNCTION_DEF(file_system)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(0, 0);
  String8 query = string;
  String8 query_normalized = path_normalized_from_string(scratch.arena, query);
  B32 query_has_slash = (query.size != 0 && char_to_correct_slash(query.str[query.size-1]) == '/');
  String8 query_normalized_with_opt_slash = push_str8f(scratch.arena, "%S%s", query_normalized, query_has_slash ? "/" : "");
  RD_PathQuery path_query = rd_path_query_from_string(query_normalized_with_opt_slash);
  F32 row_height_px = floor_f32(ui_top_font_size()*2.5f);
  F32 scroll_bar_dim = floor_f32(ui_top_font_size()*1.5f);
  RD_Window *window = rd_window_from_handle(rd_regs()->window);
  RD_CmdKindInfo *cmd_kind_info = rd_cmd_kind_info_from_string(window->query_cmd_name);
  B32 file_selection = !!(cmd_kind_info->query.flags & RD_QueryFlag_AllowFiles);
  B32 dir_selection = !!(cmd_kind_info->query.flags & RD_QueryFlag_AllowFolders);
  
  //- rjf: get extra state for this view
  UI_ScrollPt2 scroll_pos = rd_view_scroll_pos();
  RD_FileSystemViewState *fs = rd_view_state(RD_FileSystemViewState);
  if(fs->initialized == 0)
  {
    fs->initialized = 1;
    fs->path_state_table_size = 256;
    fs->path_state_table = push_array(rd_view_arena(), RD_FileSystemViewPathState *, fs->path_state_table_size);
    fs->cached_files_arena = rd_push_view_arena();
    fs->col_pcts[0] = 0.60f;
    fs->col_pcts[1] = 0.20f;
    fs->col_pcts[2] = 0.20f;
  }
  
  //- rjf: grab state for the current path
  RD_FileSystemViewPathState *ps = 0;
  {
    String8 key = query_normalized;
    U64 hash = d_hash_from_string(key);
    U64 slot = hash % fs->path_state_table_size;
    for(RD_FileSystemViewPathState *p = fs->path_state_table[slot]; p != 0; p = p->hash_next)
    {
      if(str8_match(p->normalized_path, key, 0))
      {
        ps = p;
        break;
      }
    }
    if(ps == 0)
    {
      ps = push_array(rd_view_arena(), RD_FileSystemViewPathState, 1);
      ps->hash_next = fs->path_state_table[slot];
      fs->path_state_table[slot] = ps;
      ps->normalized_path = push_str8_copy(rd_view_arena(), key);
    }
  }
  
  //- rjf: get file array from the current path
  U64 file_count = fs->cached_file_count;
  RD_FileInfo *files = fs->cached_files;
  if(!str8_match(fs->cached_files_path, query_normalized_with_opt_slash, 0) ||
     fs->cached_files_sort_kind != fs->sort_kind ||
     fs->cached_files_sort_side != fs->sort_side)
  {
    arena_clear(fs->cached_files_arena);
    
    //- rjf: store off path that we're gathering from
    fs->cached_files_path = push_str8_copy(fs->cached_files_arena, query_normalized_with_opt_slash);
    fs->cached_files_sort_kind = fs->sort_kind;
    fs->cached_files_sort_side = fs->sort_side;
    
    //- rjf: use stored path as the new browse path for the whole frontend
    // (multiple file system views may conflict here. that's okay. we'll just always
    // choose the most recent change to a file browser path, and live with the
    // consequences).
    {
      rd_cmd(RD_CmdKind_SetCurrentPath, .file_path = path_query.path);
    }
    
    //- rjf: get files, filtered
    U64 new_file_count = 0;
    RD_FileInfoNode *first_file = 0;
    RD_FileInfoNode *last_file = 0;
    {
      OS_FileIter *it = os_file_iter_begin(scratch.arena, path_query.path, 0);
      for(OS_FileInfo info = {0}; os_file_iter_next(scratch.arena, it, &info);)
      {
        FuzzyMatchRangeList match_ranges = fuzzy_match_find(fs->cached_files_arena, path_query.search, info.name);
        B32 fits_search = (path_query.search.size == 0 || match_ranges.count == match_ranges.needle_part_count);
        B32 fits_dir_only = !!(info.props.flags & FilePropertyFlag_IsFolder) || !dir_selection;
        if(fits_search && fits_dir_only)
        {
          RD_FileInfoNode *node = push_array(scratch.arena, RD_FileInfoNode, 1);
          node->file_info.filename = push_str8_copy(fs->cached_files_arena, info.name);
          node->file_info.props = info.props;
          node->file_info.match_ranges = match_ranges;
          SLLQueuePush(first_file, last_file, node);
          new_file_count += 1;
        }
      }
      os_file_iter_end(it);
    }
    
    //- rjf: convert list to array
    RD_FileInfo *new_files = push_array(fs->cached_files_arena, RD_FileInfo, new_file_count);
    {
      U64 idx = 0;
      for(RD_FileInfoNode *n = first_file; n != 0; n = n->next, idx += 1)
      {
        new_files[idx] = n->file_info;
      }
    }
    
    //- rjf: apply sort
    switch(fs->sort_kind)
    {
      default:
      {
        if(path_query.search.size != 0)
        {
          quick_sort(new_files, new_file_count, sizeof(RD_FileInfo), rd_qsort_compare_file_info__default_filtered);
        }
        else
        {
          quick_sort(new_files, new_file_count, sizeof(RD_FileInfo), rd_qsort_compare_file_info__default);
        }
      }break;
      case RD_FileSortKind_Filename:
      {
        quick_sort(new_files, new_file_count, sizeof(RD_FileInfo), rd_qsort_compare_file_info__filename);
      }break;
      case RD_FileSortKind_LastModified:
      {
        quick_sort(new_files, new_file_count, sizeof(RD_FileInfo), rd_qsort_compare_file_info__last_modified);
      }break;
      case RD_FileSortKind_Size:
      {
        quick_sort(new_files, new_file_count, sizeof(RD_FileInfo), rd_qsort_compare_file_info__size);
      }break;
    }
    
    //- rjf: apply reverse
    if(fs->sort_kind != RD_FileSortKind_Null && fs->sort_side == Side_Max)
    {
      for(U64 idx = 0; idx < new_file_count/2; idx += 1)
      {
        U64 rev_idx = new_file_count - idx - 1;
        Swap(RD_FileInfo, new_files[idx], new_files[rev_idx]);
      }
    }
    
    fs->cached_file_count = file_count = new_file_count;
    fs->cached_files = files = new_files;
  }
  
  //- rjf: submit best match when hitting enter w/ no selection
  if(ps->cursor.y == 0 && ui_slot_press(UI_EventActionSlot_Accept))
  {
    FileProperties query_normalized_with_opt_slash_props = os_properties_from_file_path(query_normalized_with_opt_slash);
    FileProperties path_query_path_props = os_properties_from_file_path(path_query.path);
    
    // rjf: command search part is empty, but directory matches some file:
    if(path_query_path_props.created != 0 && path_query.search.size == 0)
    {
      rd_cmd(RD_CmdKind_CompleteQuery, .file_path = query_normalized_with_opt_slash);
    }
    
    // rjf: command argument exactly matches some file:
    else if(query_normalized_with_opt_slash_props.created != 0 && path_query.search.size != 0)
    {
      // rjf: is a folder -> autocomplete to slash
      if(query_normalized_with_opt_slash_props.flags & FilePropertyFlag_IsFolder)
      {
        String8 new_path = push_str8f(scratch.arena, "%S%S/", path_query.path, path_query.search);
        rd_store_view_filter(new_path);
      }
      
      // rjf: is a file -> complete view
      else
      {
        rd_cmd(RD_CmdKind_CompleteQuery, .file_path = query_normalized_with_opt_slash);
      }
    }
    
    // rjf: command argument is empty, picking folders -> use current folder
    else if(path_query.search.size == 0 && dir_selection)
    {
      rd_cmd(RD_CmdKind_CompleteQuery, .file_path = path_query.path);
    }
    
    // rjf: command argument does not exactly match any file, but lister results are in:
    else if(file_count != 0)
    {
      String8 filename = files[0].filename;
      if(files[0].props.flags & FilePropertyFlag_IsFolder)
      {
        String8 existing_path = str8_chop_last_slash(path_query.path);
        String8 new_path = push_str8f(scratch.arena, "%S/%S/", existing_path, files[0].filename);
        rd_store_view_filter(new_path);
      }
      else
      {
        String8 file_path = push_str8f(scratch.arena, "%S%S", path_query.path, filename);
        rd_cmd(RD_CmdKind_CompleteQuery, .file_path = file_path);
      }
    }
    
    // rjf: command argument does not match any file, and lister is empty (new file)
    else
    {
      rd_cmd(RD_CmdKind_CompleteQuery, .file_path = query);
    }
  }
  
  //- rjf: build non-scrolled table header
  U64 row_num = 1;
  F32 **col_pcts = push_array(scratch.arena, F32 *, ArrayCount(fs->col_pcts));
  for(U64 idx = 0; idx < ArrayCount(fs->col_pcts); idx += 1)
  {
    col_pcts[idx] = &fs->col_pcts[idx];
  }
  UI_PrefHeight(ui_px(row_height_px, 1)) UI_Focus(UI_FocusKind_Off) UI_TableF(ArrayCount(fs->col_pcts), col_pcts, "###fs_tbl")
  {
    UI_TableVector
    {
      struct
      {
        RD_FileSortKind kind;
        String8 string;
      }
      kinds[] =
      {
        { RD_FileSortKind_Filename,     str8_lit_comp("Filename") },
        { RD_FileSortKind_LastModified, str8_lit_comp("Last Modified") },
        { RD_FileSortKind_Size,         str8_lit_comp("Size") },
      };
      for(U64 idx = 0; idx < ArrayCount(kinds); idx += 1)
      {
        B32 sorting = (fs->sort_kind == kinds[idx].kind);
        UI_TableCell UI_FlagsAdd(sorting ? 0 : UI_BoxFlag_DrawTextWeak)
        {
          UI_Signal sig = ui_sort_header(sorting,
                                         fs->cached_files_sort_side == Side_Min,
                                         kinds[idx].string);
          if(ui_clicked(sig))
          {
            if(fs->sort_kind != kinds[idx].kind)
            {
              fs->sort_kind = kinds[idx].kind;
              fs->sort_side = Side_Max;
            }
            else if(fs->sort_kind == kinds[idx].kind && fs->sort_side == Side_Max)
            {
              fs->sort_side = Side_Min;
            }
            else if(fs->sort_kind == kinds[idx].kind && fs->sort_side == Side_Min)
            {
              fs->sort_kind = RD_FileSortKind_Null;
            }
          }
        }
      }
    }
  }
  
  //- rjf: build file list
  Rng1S64 visible_row_range = {0};
  UI_ScrollListParams scroll_list_params = {0};
  {
    Vec2F32 content_dim = dim_2f32(rect);
    scroll_list_params.flags         = UI_ScrollListFlag_All;
    scroll_list_params.row_height_px = row_height_px;
    scroll_list_params.dim_px        = v2f32(content_dim.x, content_dim.y-row_height_px);
    scroll_list_params.cursor_range  = r2s64(v2s64(0, 0), v2s64(0, file_count+1));
    scroll_list_params.item_range    = r1s64(0, file_count+1);
    scroll_list_params.cursor_min_is_empty_selection[Axis2_Y] = 1;
  }
  UI_ScrollListSignal scroll_list_sig = {0};
  UI_Focus(UI_FocusKind_On)
    UI_ScrollList(&scroll_list_params,
                  &scroll_pos.y,
                  &ps->cursor,
                  0,
                  &visible_row_range,
                  &scroll_list_sig)
    UI_Focus(UI_FocusKind_Null)
  {
    // rjf: up-one-directory button (at idx 0)
    if(visible_row_range.min == 0)
    {
      // rjf: build
      UI_Signal sig = {0};
      UI_FocusHot(ps->cursor.y == row_num ? UI_FocusKind_On : UI_FocusKind_Off)
      {
        sig = ui_buttonf("###up_one");
      }
      
      // rjf: make content
      UI_Parent(sig.box)
      {
        // rjf: icons
        RD_Font(RD_FontSlot_Icons)
          UI_FontSize(rd_font_size_from_slot(RD_FontSlot_Icons))
          UI_PrefWidth(ui_em(3.f, 1.f))
          UI_TextAlignment(UI_TextAlign_Center)
        {
          ui_label(rd_icon_kind_text_table[RD_IconKind_LeftArrow]);
        }
        
        // rjf: text
        {
          ui_label(str8_lit("Up One Directory"));
        }
        
        row_num += 1;
      }
      
      // rjf: click => up one directory
      if(ui_clicked(sig))
      {
        String8 new_path = str8_chop_last_slash(str8_chop_last_slash(path_query.path));
        new_path = path_normalized_from_string(scratch.arena, new_path);
        String8 new_cmd = push_str8f(scratch.arena, "%S%s", new_path, new_path.size != 0 ? "/" : "");
        rd_store_view_filter(new_cmd);
      }
    }
    
    // rjf: file buttons
    for(U64 row_idx = Max(visible_row_range.min, 1);
        row_idx <= visible_row_range.max && row_idx <= file_count;
        row_idx += 1, row_num += 1)
    {
      U64 file_idx = row_idx-1;
      RD_FileInfo *file = &files[file_idx];
      B32 file_kb_focus = (ps->cursor.y == (row_idx+1));
      
      // rjf: make button
      UI_Signal file_sig = {0};
      UI_FocusHot(file_kb_focus ? UI_FocusKind_On : UI_FocusKind_Off)
      {
        file_sig = ui_buttonf("##%S", file->filename);
      }
      
      // rjf: make content
      UI_Parent(file_sig.box)
      {
        UI_PrefWidth(ui_pct(fs->col_pcts[0], 1)) UI_Row
        {
          // rjf: icon to signify directory
          RD_Font(RD_FontSlot_Icons)
            UI_FontSize(rd_font_size_from_slot(RD_FontSlot_Icons))
            UI_PrefWidth(ui_em(3.f, 1.f))
            UI_TextAlignment(UI_TextAlign_Center)
          {
            if(file->props.flags & FilePropertyFlag_IsFolder)
            {
              ui_label((ui_key_match(ui_hot_key(), file_sig.box->key) || file_kb_focus)
                       ? rd_icon_kind_text_table[RD_IconKind_FolderOpenFilled]
                       : rd_icon_kind_text_table[RD_IconKind_FolderClosedFilled]);
            }
            else
            {
              ui_label(rd_icon_kind_text_table[RD_IconKind_FileOutline]);
            }
          }
          
          // rjf: filename
          UI_PrefWidth(ui_pct(1, 0))
          {
            UI_Box *box = ui_build_box_from_string(UI_BoxFlag_DrawText|UI_BoxFlag_DisableIDString, file->filename);
            ui_box_equip_fuzzy_match_ranges(box, &file->match_ranges);
          }
        }
        
        // rjf: last-modified time
        UI_PrefWidth(ui_pct(fs->col_pcts[1], 1)) UI_Row
          UI_PrefWidth(ui_pct(1, 0))
          UI_FlagsAdd(UI_BoxFlag_DrawTextWeak)
        {
          DateTime time = date_time_from_dense_time(file->props.modified);
          DateTime time_local = os_local_time_from_universal(&time);
          String8 string = push_date_time_string(scratch.arena, &time_local);
          ui_label(string);
        }
        
        // rjf: file size
        UI_PrefWidth(ui_pct(fs->col_pcts[2], 1)) UI_Row
          UI_PrefWidth(ui_pct(1, 0))
        {
          if(file->props.size != 0)
          {
            UI_FlagsAdd(UI_BoxFlag_DrawTextWeak) ui_label(str8_from_memory_size(scratch.arena, file->props.size));
          }
        }
      }
      
      // rjf: click => activate this file
      if(ui_clicked(file_sig))
      {
        String8 existing_path = str8_chop_last_slash(path_query.path);
        String8 new_path = push_str8f(scratch.arena, "%S%s%S/", existing_path, existing_path.size != 0 ? "/" : "", file->filename);
        new_path = path_normalized_from_string(scratch.arena, new_path);
        if(file->props.flags & FilePropertyFlag_IsFolder)
        {
          String8 new_cmd = push_str8f(scratch.arena, "%S%s", new_path, new_path.size != 0 ? "/" : "");
          rd_store_view_filter(new_cmd);
        }
        else
        {
          rd_cmd(RD_CmdKind_CompleteQuery, .file_path = new_path);
        }
      }
    }
  }
  
  rd_store_view_scroll_pos(scroll_pos);
  scratch_end(scratch);
  ProfEnd();
}

////////////////////////////////
//~ rjf: system_processes  @view_hook_impl

typedef struct RD_ProcessInfo RD_ProcessInfo;
struct RD_ProcessInfo
{
  DMN_ProcessInfo info;
  B32 is_attached;
  FuzzyMatchRangeList attached_match_ranges;
  FuzzyMatchRangeList name_match_ranges;
  FuzzyMatchRangeList pid_match_ranges;
};

typedef struct RD_ProcessInfoNode RD_ProcessInfoNode;
struct RD_ProcessInfoNode
{
  RD_ProcessInfoNode *next;
  RD_ProcessInfo info;
};

typedef struct RD_ProcessInfoList RD_ProcessInfoList;
struct RD_ProcessInfoList
{
  RD_ProcessInfoNode *first;
  RD_ProcessInfoNode *last;
  U64 count;
};

typedef struct RD_ProcessInfoArray RD_ProcessInfoArray;
struct RD_ProcessInfoArray
{
  RD_ProcessInfo *v;
  U64 count;
};

internal RD_ProcessInfoList
rd_process_info_list_from_query(Arena *arena, String8 query)
{
  Temp scratch = scratch_begin(&arena, 1);
  
  //- rjf: gather PIDs that we're currently attached to
  U64 attached_process_count = 0;
  U32 *attached_process_pids = 0;
  {
    RD_EntityList processes = rd_query_cached_entity_list_with_kind(RD_EntityKind_Process);
    attached_process_count = processes.count;
    attached_process_pids = push_array(scratch.arena, U32, attached_process_count);
    U64 idx = 0;
    for(RD_EntityNode *n = processes.first; n != 0; n = n->next, idx += 1)
    {
      RD_Entity *process = n->entity;
      attached_process_pids[idx] = process->ctrl_id;
    }
  }
  
  //- rjf: build list
  RD_ProcessInfoList list = {0};
  {
    DMN_ProcessIter iter = {0};
    dmn_process_iter_begin(&iter);
    for(DMN_ProcessInfo info = {0}; dmn_process_iter_next(scratch.arena, &iter, &info);)
    {
      // rjf: skip root-level or otherwise 0-pid processes
      if(info.pid == 0)
      {
        continue;
      }
      
      // rjf: determine if this process is attached
      B32 is_attached = 0;
      for(U64 attached_idx = 0; attached_idx < attached_process_count; attached_idx += 1)
      {
        if(attached_process_pids[attached_idx] == info.pid)
        {
          is_attached = 1;
          break;
        }
      }
      
      // rjf: gather fuzzy matches
      FuzzyMatchRangeList attached_match_ranges = {0};
      FuzzyMatchRangeList name_match_ranges     = fuzzy_match_find(arena, query, info.name);
      FuzzyMatchRangeList pid_match_ranges      = fuzzy_match_find(arena, query, push_str8f(scratch.arena, "%i", info.pid));
      if(is_attached)
      {
        attached_match_ranges = fuzzy_match_find(arena, query, str8_lit("[attached]"));
      }
      
      // rjf: determine if this item is filtered out
      B32 matches_query = (query.size == 0 ||
                           (attached_match_ranges.needle_part_count != 0 && attached_match_ranges.count >= attached_match_ranges.needle_part_count) ||
                           (name_match_ranges.count != 0 && name_match_ranges.count >= name_match_ranges.needle_part_count) ||
                           (pid_match_ranges.count != 0 && pid_match_ranges.count >= pid_match_ranges.needle_part_count));
      
      // rjf: push if unfiltered
      if(matches_query)
      {
        RD_ProcessInfoNode *n = push_array(arena, RD_ProcessInfoNode, 1);
        n->info.info = info;
        n->info.info.name = push_str8_copy(arena, info.name);
        n->info.is_attached = is_attached;
        n->info.attached_match_ranges = attached_match_ranges;
        n->info.name_match_ranges = name_match_ranges;
        n->info.pid_match_ranges = pid_match_ranges;
        SLLQueuePush(list.first, list.last, n);
        list.count += 1;
      }
    }
    dmn_process_iter_end(&iter);
  }
  
  scratch_end(scratch);
  return list;
}

internal RD_ProcessInfoArray
rd_process_info_array_from_list(Arena *arena, RD_ProcessInfoList list)
{
  RD_ProcessInfoArray array = {0};
  array.count = list.count;
  array.v = push_array(arena, RD_ProcessInfo, array.count);
  U64 idx = 0;
  for(RD_ProcessInfoNode *n = list.first; n != 0; n = n->next, idx += 1)
  {
    array.v[idx] = n->info;
  }
  return array;
}

internal int
rd_qsort_compare_process_info(RD_ProcessInfo *a, RD_ProcessInfo *b)
{
  int result = 0;
  if(a->pid_match_ranges.count > b->pid_match_ranges.count)
  {
    result = -1;
  }
  else if(a->pid_match_ranges.count < b->pid_match_ranges.count)
  {
    result = +1;
  }
  else if(a->name_match_ranges.count < b->name_match_ranges.count)
  {
    result = -1;
  }
  else if(a->name_match_ranges.count > b->name_match_ranges.count)
  {
    result = +1;
  }
  else if(a->attached_match_ranges.count < b->attached_match_ranges.count)
  {
    result = -1;
  }
  else if(a->attached_match_ranges.count > b->attached_match_ranges.count)
  {
    result = +1;
  }
  return result;
}

internal void
rd_process_info_array_sort_by_strength__in_place(RD_ProcessInfoArray array)
{
  quick_sort(array.v, array.count, sizeof(RD_ProcessInfo), rd_qsort_compare_process_info);
}

RD_VIEW_RULE_UI_FUNCTION_DEF(system_processes)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(0, 0);
  F32 row_height_px = floor_f32(ui_top_font_size()*2.5f);
  
  //- rjf: grab state
  typedef struct RD_SystemProcessesViewState RD_SystemProcessesViewState;
  struct RD_SystemProcessesViewState
  {
    B32 initialized;
    B32 need_initial_gather;
    U32 selected_pid;
    Arena *cached_process_arena;
    String8 cached_process_arg;
    RD_ProcessInfoArray cached_process_array;
  };
  UI_ScrollPt2 scroll_pos = rd_view_scroll_pos();
  RD_SystemProcessesViewState *sp = rd_view_state(RD_SystemProcessesViewState);
  if(sp->initialized == 0)
  {
    sp->initialized = 1;
    sp->need_initial_gather = 1;
    sp->cached_process_arena = rd_push_view_arena();
  }
  
  //- rjf: gather list of filtered process infos
  String8 query = string;
  RD_ProcessInfoArray process_info_array = sp->cached_process_array;
  if(sp->need_initial_gather || !str8_match(sp->cached_process_arg, query, 0))
  {
    arena_clear(sp->cached_process_arena);
    sp->need_initial_gather = 0;
    sp->cached_process_arg = push_str8_copy(sp->cached_process_arena, query);
    RD_ProcessInfoList list = rd_process_info_list_from_query(sp->cached_process_arena, query);
    sp->cached_process_array = rd_process_info_array_from_list(sp->cached_process_arena, list);
    process_info_array = sp->cached_process_array;
    rd_process_info_array_sort_by_strength__in_place(process_info_array);
  }
  
  //- rjf: submit best match when hitting enter w/ no selection
  if(sp->selected_pid == 0 && process_info_array.count > 0 && ui_slot_press(UI_EventActionSlot_Accept))
  {
    RD_ProcessInfo *info = &process_info_array.v[0];
    rd_cmd(RD_CmdKind_CompleteQuery, .pid = info->info.pid);
  }
  
  //- rjf: selected PID -> cursor
  Vec2S64 cursor = {0};
  {
    for(U64 idx = 0; idx < process_info_array.count; idx += 1)
    {
      if(process_info_array.v[idx].info.pid == sp->selected_pid)
      {
        cursor.y = idx+1;
        break;
      }
    }
  }
  
  //- rjf: build contents
  Rng1S64 visible_row_range = {0};
  UI_ScrollListParams scroll_list_params = {0};
  {
    Vec2F32 content_dim = dim_2f32(rect);
    scroll_list_params.flags         = UI_ScrollListFlag_All;
    scroll_list_params.row_height_px = row_height_px;
    scroll_list_params.dim_px        = v2f32(content_dim.x, content_dim.y);
    scroll_list_params.cursor_range  = r2s64(v2s64(0, 0), v2s64(0, process_info_array.count));
    scroll_list_params.item_range    = r1s64(0, process_info_array.count);
    scroll_list_params.cursor_min_is_empty_selection[Axis2_Y] = 1;
  }
  UI_ScrollListSignal scroll_list_sig = {0};
  UI_Focus(UI_FocusKind_On)
    UI_ScrollList(&scroll_list_params,
                  &scroll_pos.y,
                  &cursor,
                  0,
                  &visible_row_range,
                  &scroll_list_sig)
    UI_Focus(UI_FocusKind_Null)
  {
    //- rjf: build rows
    for(U64 idx = visible_row_range.min;
        idx <= visible_row_range.max && idx < process_info_array.count;
        idx += 1)
    {
      RD_ProcessInfo *info = &process_info_array.v[idx];
      B32 is_attached = info->is_attached;
      UI_Signal sig = {0};
      UI_FocusHot(cursor.y == idx+1 ? UI_FocusKind_On : UI_FocusKind_Off)
      {
        sig = ui_buttonf("###proc_%i", info->info.pid);
      }
      UI_Parent(sig.box)
      {
        // rjf: icon
        RD_Font(RD_FontSlot_Icons)
          UI_FontSize(rd_font_size_from_slot(RD_FontSlot_Icons))
          UI_PrefWidth(ui_em(3.f, 1.f))
          UI_TextAlignment(UI_TextAlign_Center)
        {
          ui_label(rd_icon_kind_text_table[RD_IconKind_Threads]);
        }
        
        // rjf: attached indicator
        if(is_attached) UI_PrefWidth(ui_text_dim(10, 1)) UI_FlagsAdd(UI_BoxFlag_DrawTextWeak)
        {
          UI_Box *attached_label = ui_build_box_from_stringf(UI_BoxFlag_DrawText, "[attached]##attached_label_%i", (int)info->info.pid);
          ui_box_equip_fuzzy_match_ranges(attached_label, &info->attached_match_ranges);
        }
        
        // rjf: process name
        UI_PrefWidth(ui_text_dim(10, 1))
        {
          UI_Box *name_label = ui_build_box_from_stringf(UI_BoxFlag_DrawText, "%S##name_label_%i", info->info.name, (int)info->info.pid);
          ui_box_equip_fuzzy_match_ranges(name_label, &info->name_match_ranges);
        }
        
        // rjf: process number
        UI_PrefWidth(ui_text_dim(1, 1)) UI_TextAlignment(UI_TextAlign_Center) UI_TextPadding(0)
        {
          ui_labelf("[PID: ");
          UI_Box *pid_label = ui_build_box_from_stringf(UI_BoxFlag_DrawText, "%i##pid_label", info->info.pid);
          ui_box_equip_fuzzy_match_ranges(pid_label, &info->pid_match_ranges);
          ui_labelf("]");
        }
      }
      
      // rjf: click => activate this specific process
      if(ui_clicked(sig))
      {
        rd_cmd(RD_CmdKind_CompleteQuery, .pid = info->info.pid);
      }
    }
  }
  
  //- rjf: selected num -> selected PID
  {
    if(1 <= cursor.y && cursor.y <= process_info_array.count)
    {
      sp->selected_pid = process_info_array.v[cursor.y-1].info.pid;
    }
    else
    {
      sp->selected_pid = 0;
    }
  }
  
  rd_store_view_scroll_pos(scroll_pos);
  scratch_end(scratch);
  ProfEnd();
}

////////////////////////////////
//~ rjf: entity_lister @view_hook_impl

typedef struct RD_EntityListerItem RD_EntityListerItem;
struct RD_EntityListerItem
{
  RD_Entity *entity;
  FuzzyMatchRangeList name_match_ranges;
};

typedef struct RD_EntityListerItemNode RD_EntityListerItemNode;
struct RD_EntityListerItemNode
{
  RD_EntityListerItemNode *next;
  RD_EntityListerItem item;
};

typedef struct RD_EntityListerItemList RD_EntityListerItemList;
struct RD_EntityListerItemList
{
  RD_EntityListerItemNode *first;
  RD_EntityListerItemNode *last;
  U64 count;
};

typedef struct RD_EntityListerItemArray RD_EntityListerItemArray;
struct RD_EntityListerItemArray
{
  RD_EntityListerItem *v;
  U64 count;
};

internal RD_EntityListerItemList
rd_entity_lister_item_list_from_needle(Arena *arena, RD_EntityKind kind, RD_EntityFlags omit_flags, String8 needle)
{
  Temp scratch = scratch_begin(&arena, 1);
  RD_EntityListerItemList result = {0};
  RD_EntityList ent_list = rd_query_cached_entity_list_with_kind(kind);
  for(RD_EntityNode *n = ent_list.first; n != 0; n = n->next)
  {
    RD_Entity *entity = n->entity;
    if(!(entity->flags & omit_flags))
    {
      String8 display_string = rd_display_string_from_entity(scratch.arena, entity);
      FuzzyMatchRangeList match_rngs = fuzzy_match_find(arena, needle, display_string);
      if(match_rngs.count != 0 || needle.size == 0)
      {
        RD_EntityListerItemNode *item_n = push_array(arena, RD_EntityListerItemNode, 1);
        item_n->item.entity = entity;
        item_n->item.name_match_ranges = match_rngs;
        SLLQueuePush(result.first, result.last, item_n);
        result.count += 1;
      }
    }
  }
  scratch_end(scratch);
  return result;
}

internal RD_EntityListerItemArray
rd_entity_lister_item_array_from_list(Arena *arena, RD_EntityListerItemList list)
{
  RD_EntityListerItemArray result = {0};
  result.count = list.count;
  result.v = push_array(arena, RD_EntityListerItem, result.count);
  {
    U64 idx = 0;
    for(RD_EntityListerItemNode *n = list.first; n != 0; n = n->next, idx += 1)
    {
      result.v[idx] = n->item;
    }
  }
  return result;
}

internal int
rd_qsort_compare_entity_lister__strength(RD_EntityListerItem *a, RD_EntityListerItem *b)
{
  int result = 0;
  if(a->name_match_ranges.count > b->name_match_ranges.count)
  {
    result = -1;
  }
  else if(a->name_match_ranges.count < b->name_match_ranges.count)
  {
    result = +1;
  }
  return result;
}

internal void
rd_entity_lister_item_array_sort_by_strength__in_place(RD_EntityListerItemArray array)
{
  quick_sort(array.v, array.count, sizeof(RD_EntityListerItem), rd_qsort_compare_entity_lister__strength);
}

RD_VIEW_RULE_UI_FUNCTION_DEF(entity_lister)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(0, 0);
  RD_Window *window = rd_window_from_handle(rd_regs()->window);
  RD_CmdKindInfo *cmd_kind_info = rd_cmd_kind_info_from_string(window->query_cmd_name);
  RD_EntityKind entity_kind = cmd_kind_info->query.entity_kind;
  RD_EntityFlags entity_flags_omit = RD_EntityFlag_IsFolder;
  F32 row_height_px = floor_f32(ui_top_font_size()*2.5f);
  F32 scroll_bar_dim = floor_f32(ui_top_font_size()*1.5f);
  
  //- rjf: grab state
  typedef struct RD_EntityListerViewState RD_EntityListerViewState;
  struct RD_EntityListerViewState
  {
    RD_Handle selected_entity_handle;
  };
  UI_ScrollPt2 scroll_pos = rd_view_scroll_pos();
  RD_EntityListerViewState *fev = rd_view_state(RD_EntityListerViewState);
  RD_Handle selected_entity_handle = fev->selected_entity_handle;
  RD_Entity *selected_entity = rd_entity_from_handle(selected_entity_handle);
  
  //- rjf: build filtered array of entities
  RD_EntityListerItemList ent_list = rd_entity_lister_item_list_from_needle(scratch.arena, entity_kind, entity_flags_omit, string);
  RD_EntityListerItemArray ent_arr = rd_entity_lister_item_array_from_list(scratch.arena, ent_list);
  rd_entity_lister_item_array_sort_by_strength__in_place(ent_arr);
  
  //- rjf: submit best match when hitting enter w/ no selection
  if(rd_entity_is_nil(rd_entity_from_handle(fev->selected_entity_handle)) && ent_arr.count != 0 && ui_slot_press(UI_EventActionSlot_Accept))
  {
    RD_Entity *ent = ent_arr.v[0].entity;
    rd_cmd(RD_CmdKind_CompleteQuery, .entity = rd_handle_from_entity(ent));
  }
  
  //- rjf: selected entity -> cursor
  Vec2S64 cursor = {0};
  {
    for(U64 idx = 0; idx < ent_arr.count; idx += 1)
    {
      if(ent_arr.v[idx].entity == selected_entity)
      {
        cursor.y = (S64)(idx+1);
        break;
      }
    }
  }
  
  //- rjf: build list
  Rng1S64 visible_row_range = {0};
  UI_ScrollListParams scroll_list_params = {0};
  {
    Vec2F32 content_dim = dim_2f32(rect);
    scroll_list_params.flags         = UI_ScrollListFlag_All;
    scroll_list_params.row_height_px = row_height_px;
    scroll_list_params.dim_px        = v2f32(content_dim.x, content_dim.y);
    scroll_list_params.cursor_range  = r2s64(v2s64(0, 0), v2s64(0, ent_arr.count));
    scroll_list_params.item_range    = r1s64(0, ent_arr.count);
    scroll_list_params.cursor_min_is_empty_selection[Axis2_Y] = 1;
  }
  UI_ScrollListSignal scroll_list_sig = {0};
  UI_Focus(UI_FocusKind_On)
    UI_ScrollList(&scroll_list_params,
                  &scroll_pos.y,
                  &cursor,
                  0,
                  &visible_row_range,
                  &scroll_list_sig)
    UI_Focus(UI_FocusKind_Null)
  {
    for(S64 idx = visible_row_range.min;
        idx <= visible_row_range.max && idx < ent_arr.count;
        idx += 1)
    {
      RD_EntityListerItem item = ent_arr.v[idx];
      RD_Entity *ent = item.entity;
      ui_set_next_hover_cursor(OS_Cursor_HandPoint);
      ui_set_next_child_layout_axis(Axis2_X);
      UI_Box *box = &ui_nil_box;
      UI_FocusHot(idx+1 == cursor.y ? UI_FocusKind_On : UI_FocusKind_Off)
      {
        box = ui_build_box_from_stringf(UI_BoxFlag_Clickable|
                                        UI_BoxFlag_DrawBorder|
                                        UI_BoxFlag_DrawBackground|
                                        UI_BoxFlag_DrawHotEffects|
                                        UI_BoxFlag_DrawActiveEffects,
                                        "###ent_btn_%p", ent);
      }
      UI_Parent(box)
      {
        RD_IconKind icon_kind = rd_entity_kind_icon_kind_table[ent->kind];
        if(icon_kind != RD_IconKind_Null)
        {
          UI_TextAlignment(UI_TextAlign_Center)
            RD_Font(RD_FontSlot_Icons)
            UI_FontSize(rd_font_size_from_slot(RD_FontSlot_Icons))
            UI_FlagsAdd(UI_BoxFlag_DrawTextWeak)
            UI_PrefWidth(ui_text_dim(10, 1))
            ui_label(rd_icon_kind_text_table[icon_kind]);
        }
        String8 display_string = rd_display_string_from_entity(scratch.arena, ent);
        Vec4F32 color = rd_rgba_from_entity(ent);
        if(color.w != 0)
        {
          ui_set_next_palette(ui_build_palette(ui_top_palette(), .text = color));
        }
        UI_Box *name_label = ui_build_box_from_stringf(UI_BoxFlag_DrawText, "%S##label_%p", display_string, ent);
        ui_box_equip_fuzzy_match_ranges(name_label, &item.name_match_ranges);
      }
      if(ui_clicked(ui_signal_from_box(box)))
      {
        rd_cmd(RD_CmdKind_CompleteQuery, .entity = rd_handle_from_entity(ent));
      }
    }
  }
  
  //- rjf: selected entity num -> handle
  {
    fev->selected_entity_handle = (1 <= cursor.y && cursor.y <= ent_arr.count) ? rd_handle_from_entity(ent_arr.v[cursor.y-1].entity) : rd_handle_zero();
  }
  
  rd_store_view_scroll_pos(scroll_pos);
  scratch_end(scratch);
  ProfEnd();
}

////////////////////////////////
//~ rjf: symbol_lister @view_hook_impl

RD_VIEW_RULE_UI_FUNCTION_DEF(symbol_lister)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(0, 0);
  DI_Scope *di_scope = di_scope_open();
  FZY_Scope *fzy_scope = fzy_scope_open();
  F32 row_height_px = floor_f32(ui_top_font_size()*2.5f);
  DI_KeyList dbgi_keys_list = d_push_active_dbgi_key_list(scratch.arena);
  DI_KeyArray dbgi_keys = di_key_array_from_list(scratch.arena, &dbgi_keys_list);
  FZY_Params fuzzy_search_params = {RDI_SectionKind_Procedures, dbgi_keys};
  U64 endt_us = os_now_microseconds()+200;
  
  //- rjf: grab rdis
  U64 rdis_count = dbgi_keys.count;
  RDI_Parsed **rdis = push_array(scratch.arena, RDI_Parsed *, rdis_count);
  {
    for(U64 idx = 0; idx < rdis_count; idx += 1)
    {
      RDI_Parsed *rdi = di_rdi_from_key(di_scope, &dbgi_keys.v[idx], endt_us);
      RDI_TopLevelInfo *tli = rdi_element_from_name_idx(rdi, TopLevelInfo, 0);
      rdis[idx] = rdi;
    }
  }
  
  //- rjf: grab state
  typedef struct RD_SymbolListerViewState RD_SymbolListerViewState;
  struct RD_SymbolListerViewState
  {
    Vec2S64 cursor;
  };
  UI_ScrollPt2 scroll_pos = rd_view_scroll_pos();
  RD_SymbolListerViewState *slv = rd_view_state(RD_SymbolListerViewState);
  
  //- rjf: query -> raddbg, filtered items
  U128 fuzzy_search_key = {rd_regs()->view.u64[0], rd_regs()->view.u64[1]};
  B32 items_stale = 0;
  FZY_ItemArray items = fzy_items_from_key_params_query(fzy_scope, fuzzy_search_key, &fuzzy_search_params, string, endt_us, &items_stale);
  if(items_stale)
  {
    rd_request_frame();
  }
  
  //- rjf: submit best match when hitting enter w/ no selection
  if(slv->cursor.y == 0 && items.count != 0 && ui_slot_press(UI_EventActionSlot_Accept))
  {
    FZY_Item *item = &items.v[0];
    U64 base_idx = 0;
    for(U64 rdi_idx = 0; rdi_idx < rdis_count; rdi_idx += 1)
    {
      RDI_Parsed *rdi = rdis[rdi_idx];
      U64 rdi_procedures_count = 0;
      rdi_section_raw_table_from_kind(rdi, RDI_SectionKind_Procedures, &rdi_procedures_count);
      if(base_idx <= item->idx && item->idx < base_idx + rdi_procedures_count)
      {
        RDI_Procedure *procedure = rdi_element_from_name_idx(rdi, Procedures, item->idx-base_idx);
        U64 name_size = 0;
        U8 *name_base = rdi_string_from_idx(rdi, procedure->name_string_idx, &name_size);
        String8 name = str8(name_base, name_size);
        if(name.size != 0)
        {
          rd_cmd(RD_CmdKind_CompleteQuery, .string = name);
        }
        break;
      }
      base_idx += rdi_procedures_count;
    }
  }
  
  //- rjf: build contents
  Rng1S64 visible_row_range = {0};
  UI_ScrollListParams scroll_list_params = {0};
  {
    Vec2F32 content_dim = dim_2f32(rect);
    scroll_list_params.flags         = UI_ScrollListFlag_All;
    scroll_list_params.row_height_px = row_height_px;
    scroll_list_params.dim_px        = v2f32(content_dim.x, content_dim.y);
    scroll_list_params.cursor_range  = r2s64(v2s64(0, 0), v2s64(0, items.count));
    scroll_list_params.item_range    = r1s64(0, items.count);
    scroll_list_params.cursor_min_is_empty_selection[Axis2_Y] = 1;
  }
  UI_ScrollListSignal scroll_list_sig = {0};
  UI_Focus(UI_FocusKind_On)
    UI_ScrollList(&scroll_list_params,
                  &scroll_pos.y,
                  &slv->cursor,
                  0,
                  &visible_row_range,
                  &scroll_list_sig)
    UI_Focus(UI_FocusKind_Null)
    UI_TextRasterFlags(rd_raster_flags_from_slot(RD_FontSlot_Code))
  {
    //- rjf: build rows
    for(U64 idx = visible_row_range.min;
        idx <= visible_row_range.max && idx < items.count;
        idx += 1)
      UI_Focus((slv->cursor.y == idx+1) ? UI_FocusKind_On : UI_FocusKind_Off)
    {
      FZY_Item *item = &items.v[idx];
      
      //- rjf: determine dbgi/rdi to which this item belongs
      DI_Key dbgi_key = {0};
      RDI_Parsed *rdi = &di_rdi_parsed_nil;
      U64 base_idx = 0;
      {
        for(U64 rdi_idx = 0; rdi_idx < rdis_count; rdi_idx += 1)
        {
          U64 procedures_count = 0;
          rdi_section_raw_table_from_kind(rdis[rdi_idx], RDI_SectionKind_Procedures, &procedures_count);
          if(base_idx <= item->idx && item->idx < base_idx + procedures_count)
          {
            dbgi_key = dbgi_keys.v[rdi_idx];
            rdi = rdis[rdi_idx];
            break;
          }
          base_idx += procedures_count;
        }
      }
      
      //- rjf: unpack this item's info
      RDI_Procedure *procedure = rdi_element_from_name_idx(rdi, Procedures, item->idx-base_idx);
      U64 name_size = 0;
      U8 *name_base = rdi_string_from_idx(rdi, procedure->name_string_idx, &name_size);
      String8 name = str8(name_base, name_size);
      RDI_TypeNode *type_node = rdi_element_from_name_idx(rdi, TypeNodes, procedure->type_idx);
      E_TypeKey type_key = e_type_key_ext(e_type_kind_from_rdi(type_node->kind), procedure->type_idx, e_parse_ctx_module_idx_from_rdi(rdi));
      
      //- rjf: build item button
      ui_set_next_hover_cursor(OS_Cursor_HandPoint);
      UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_Clickable|
                                              UI_BoxFlag_DrawBackground|
                                              UI_BoxFlag_DrawBorder|
                                              UI_BoxFlag_DrawText|
                                              UI_BoxFlag_DrawHotEffects|
                                              UI_BoxFlag_DrawActiveEffects,
                                              "###procedure_%I64x", item->idx);
      UI_Parent(box) UI_PrefWidth(ui_text_dim(10, 1)) RD_Font(RD_FontSlot_Code)
      {
        UI_Box *box = rd_code_label(1.f, 0, rd_rgba_from_theme_color(RD_ThemeColor_CodeSymbol), name);
        ui_box_equip_fuzzy_match_ranges(box, &item->match_ranges);
        if(!e_type_key_match(e_type_key_zero(), type_key))
        {
          String8 type_string = e_type_string_from_key(scratch.arena, type_key);
          rd_code_label(0.5f, 0, rd_rgba_from_theme_color(RD_ThemeColor_TextWeak), type_string);
        }
      }
      
      //- rjf: interact
      UI_Signal sig = ui_signal_from_box(box);
      if(ui_clicked(sig))
      {
        rd_cmd(RD_CmdKind_CompleteQuery, .string = name);
      }
      if(ui_hovering(sig)) UI_Tooltip
      {
        RD_Font(RD_FontSlot_Code) rd_code_label(1.f, 0, rd_rgba_from_theme_color(RD_ThemeColor_CodeSymbol), name);
        RD_Font(RD_FontSlot_Main) UI_FlagsAdd(UI_BoxFlag_DrawTextWeak)
          ui_labelf("Procedure #%I64u", item->idx);
        U64 binary_voff = d_voff_from_dbgi_key_symbol_name(&dbgi_key, name);
        D_LineList lines = d_lines_from_dbgi_key_voff(scratch.arena, &dbgi_key, binary_voff);
        if(lines.first != 0)
        {
          String8 file_path = lines.first->v.file_path;
          S64 line_num = lines.first->v.pt.line;
          RD_Font(RD_FontSlot_Main) UI_FlagsAdd(UI_BoxFlag_DrawTextWeak)
            ui_labelf("%S:%I64d", file_path, line_num);
        }
        else
        {
          RD_Font(RD_FontSlot_Main) UI_FlagsAdd(UI_BoxFlag_DrawTextWeak)
            ui_label(str8_lit("(No source code location found)"));
        }
      }
    }
  }
  
  rd_store_view_scroll_pos(scroll_pos);
  fzy_scope_close(fzy_scope);
  di_scope_close(di_scope);
  scratch_end(scratch);
  ProfEnd();
}

////////////////////////////////
//~ rjf: target @view_hook_impl

typedef struct RD_TargetViewState RD_TargetViewState;
struct RD_TargetViewState
{
  B32 initialized;
  
  // rjf: pick file kind
  RD_EntityKind pick_dst_kind;
  
  // rjf: selection cursor
  Vec2S64 cursor;
  
  // rjf: text input state
  TxtPt input_cursor;
  TxtPt input_mark;
  U8 input_buffer[1024];
  U64 input_size;
  B32 input_editing;
  
  // rjf: table column pcts
  F32 key_pct;
  F32 value_pct;
};

RD_VIEW_RULE_UI_FUNCTION_DEF(target)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(0, 0);
#if 0 // TODO(rjf): @msgs
  RD_Entity *entity = rd_entity_from_eval_string(string);
  RD_EntityList custom_entry_points = rd_push_entity_child_list_with_kind(scratch.arena, entity, RD_EntityKind_EntryPoint);
  F32 row_height_px = floor_f32(ui_top_font_size()*2.5f);
  
  RD_TargetViewState *tv = rd_view_state(RD_TargetViewState);
  RD_Entity *entity = rd_entity_from_eval_string(string);
  // rjf: process commands
  for(RD_Cmd *cmd = 0; rd_next_cmd(&cmd);)
  {
    // rjf: mismatched window/panel => skip
    if(!rd_handle_match(rd_regs()->view, cmd->regs->view))
    {
      continue;
    }
    
    // rjf: process command
    RD_CmdKind kind = rd_cmd_kind_from_string(cmd->spec->info.string);
    switch(kind)
    {
      default:break;
      case RD_CmdKind_PickFile:
      case RD_CmdKind_PickFolder:
      {
        String8 pick_string = cmd->params.file_path;
        RD_Entity *storage_entity = entity;
        if(tv->pick_dst_kind != RD_EntityKind_Nil)
        {
          RD_Entity *child = rd_entity_child_from_kind(entity, tv->pick_dst_kind);
          if(rd_entity_is_nil(child))
          {
            child = rd_entity_alloc(entity, tv->pick_dst_kind);
          }
          storage_entity = child;
        }
        rd_entity_equip_name(storage_entity, pick_string);
      }break;
    }
  }
  
  //- rjf: grab state
  RD_TargetViewState *tv = rd_view_state(RD_TargetViewState);
  if(tv->initialized == 0)
  {
    tv->initialized = 1;
    tv->key_pct = 0.2f;
    tv->value_pct = 0.8f;
  }
  
  //- rjf: set up key-value-pair info
  struct
  {
    B32 fill_with_file;
    B32 fill_with_folder;
    B32 use_code_font;
    String8 key;
    RD_EntityKind storage_child_kind;
    String8 current_text;
  }
  kv_info[] =
  {
    { 0, 0, 0, str8_lit("Label"),                RD_EntityKind_Nil,              entity->string },
    { 1, 0, 0, str8_lit("Executable"),           RD_EntityKind_Executable,       rd_entity_child_from_kind(entity, RD_EntityKind_Executable)->string },
    { 0, 0, 0, str8_lit("Arguments"),            RD_EntityKind_Arguments,        rd_entity_child_from_kind(entity, RD_EntityKind_Arguments)->string },
    { 0, 1, 0, str8_lit("Working Directory"),    RD_EntityKind_WorkingDirectory, rd_entity_child_from_kind(entity, RD_EntityKind_WorkingDirectory)->string },
    { 0, 0, 1, str8_lit("Entry Point Override"), RD_EntityKind_EntryPoint,       rd_entity_child_from_kind(entity, RD_EntityKind_EntryPoint)->string },
  };
  
  //- rjf: take controls to start/end editing
  B32 edit_begin  = 0;
  B32 edit_end    = 0;
  B32 edit_commit = 0;
  B32 edit_submit = 0;
  UI_Focus(UI_FocusKind_On) if(ui_is_focus_active())
  {
    if(!tv->input_editing)
    {
      for(UI_Event *evt = 0; ui_next_event(&evt);)
      {
        if(evt->string.size != 0 || evt->flags & UI_EventFlag_Paste)
        {
          edit_begin = 1;
          break;
        }
      }
      if(ui_slot_press(UI_EventActionSlot_Edit))
      {
        edit_begin = 1;
      }
      if(ui_slot_press(UI_EventActionSlot_Accept))
      {
        edit_begin = 1;
      }
    }
    if(tv->input_editing)
    {
      if(ui_slot_press(UI_EventActionSlot_Cancel))
      {
        edit_end = 1;
        edit_commit = 0;
      }
      if(ui_slot_press(UI_EventActionSlot_Accept))
      {
        edit_end = 1;
        edit_commit = 1;
        edit_submit = 1;
      }
    }
  }
  
  //- rjf: build
  Rng1S64 visible_row_range = {0};
  UI_ScrollListParams scroll_list_params = {0};
  {
    scroll_list_params.flags         = UI_ScrollListFlag_All;
    scroll_list_params.row_height_px = row_height_px;
    scroll_list_params.dim_px        = dim_2f32(rect);
    scroll_list_params.cursor_range  = r2s64(v2s64(0, 0), v2s64(0, (S64)ArrayCount(kv_info)));
    scroll_list_params.item_range    = r1s64(0, (S64)ArrayCount(kv_info));
    scroll_list_params.cursor_min_is_empty_selection[Axis2_Y] = 1;
  }
  RD_EntityKind commit_storage_child_kind = RD_EntityKind_Nil;
  Vec2S64 next_cursor = tv->cursor;
  UI_ScrollListSignal scroll_list_sig = {0};
  UI_Focus(UI_FocusKind_On)
    UI_ScrollList(&scroll_list_params,
                  &scroll_pos.y,
                  tv->input_editing ? 0 : &tv->cursor,
                  0,
                  &visible_row_range,
                  &scroll_list_sig)
    UI_Focus(UI_FocusKind_Null)
  {
    next_cursor = tv->cursor;
    F32 *col_pcts[] = {&tv->key_pct, &tv->value_pct};
    UI_TableF(ArrayCount(col_pcts), col_pcts, "###target_%p", view)
    {
      //- rjf: build fixed rows
      S64 row_idx = 0;
      for(S64 idx = visible_row_range.min;
          idx <= visible_row_range.max && idx < ArrayCount(kv_info);
          idx += 1, row_idx += 1)
        UI_TableVector
      {
        B32 row_selected = (tv->cursor.y == idx+1);
        B32 has_browse = kv_info[idx].fill_with_file || kv_info[idx].fill_with_folder;
        
        //- rjf: key (label)
        UI_TableCell UI_FlagsAdd(UI_BoxFlag_DrawTextWeak)
        {
          if(kv_info[idx].storage_child_kind == RD_EntityKind_EntryPoint)
          {
            if(rd_help_label(str8_lit("Custom Entry Point"))) UI_Tooltip
            {
              ui_label_multiline(ui_top_font_size()*30.f, str8_lit("By default, the debugger attempts to find a target's entry point with a set of default names, such as:"));
              ui_spacer(ui_em(1.5f, 1.f));
              RD_Font(RD_FontSlot_Code) UI_Palette(ui_build_palette(ui_top_palette(), .text = rd_rgba_from_theme_color(RD_ThemeColor_CodeSymbol)))
              {
                ui_label(str8_lit("WinMain"));
                ui_label(str8_lit("wWinMain"));
                ui_label(str8_lit("main"));
                ui_label(str8_lit("wmain"));
                ui_label(str8_lit("WinMainCRTStartup"));
                ui_label(str8_lit("wWinMainCRTStartup"));
              }
              ui_spacer(ui_em(1.5f, 1.f));
              ui_label_multiline(ui_top_font_size()*30.f, str8_lit("A Custom Entry Point can be used to override these default symbol names with a symbol name of your choosing. If a symbol matching the Custom Entry Point is not found, the debugger will fall back to its default rules."));
            }
          }
          else
          {
            ui_build_box_from_string(UI_BoxFlag_Clickable|UI_BoxFlag_DrawText, kv_info[idx].key);
          }
        }
        
        //- rjf: value
        UI_TableCell
        {
          // rjf: value editor
          UI_WidthFill RD_Font(kv_info[idx].use_code_font ? RD_FontSlot_Code : RD_FontSlot_Main)
          {
            // rjf: * => focus
            B32 value_selected = row_selected && (next_cursor.x == 0 || !has_browse);
            
            // rjf: begin editing
            if(value_selected && edit_begin)
            {
              tv->input_editing = 1;
              tv->input_size = Min(sizeof(tv->input_buffer), kv_info[idx].current_text.size);
              MemoryCopy(tv->input_buffer, kv_info[idx].current_text.str, tv->input_size);
              tv->input_cursor = txt_pt(1, 1+tv->input_size);
              tv->input_mark = txt_pt(1, 1);
            }
            
            // rjf: build main editor ui
            UI_Signal sig = {0};
            UI_FocusHot(value_selected ? UI_FocusKind_On : UI_FocusKind_Off)
              UI_FocusActive((value_selected && tv->input_editing) ? UI_FocusKind_On : UI_FocusKind_Off)
            {
              sig = rd_line_editf(RD_LineEditFlag_NoBackground, 0, 0, &tv->input_cursor, &tv->input_mark, tv->input_buffer, sizeof(tv->input_buffer), &tv->input_size, 0, kv_info[idx].current_text, "###kv_editor_%i", (S32)idx);
              edit_commit = edit_commit || ui_committed(sig);
            }
            
            // rjf: focus panel on press
            if(ui_pressed(sig))
            {
              rd_cmd(RD_CmdKind_FocusPanel);
            }
            
            // rjf: begin editing on double-click
            if(!tv->input_editing && ui_double_clicked(sig))
            {
              ui_kill_action();
              tv->input_editing = 1;
              tv->input_size = Min(sizeof(tv->input_buffer), kv_info[idx].current_text.size);
              MemoryCopy(tv->input_buffer, kv_info[idx].current_text.str, tv->input_size);
              tv->input_cursor = txt_pt(1, 1+tv->input_size);
              tv->input_mark = txt_pt(1, 1);
            }
            
            // rjf: press on non-selected => commit edit, change selected cell
            if(ui_pressed(sig) && !value_selected)
            {
              edit_end = 1;
              edit_commit = tv->input_editing;
              next_cursor = v2s64(0, idx+1);
            }
            
            // rjf: apply commit deltas
            if(ui_committed(sig))
            {
              next_cursor.y += 1;
            }
            
            // rjf: grab commit destination
            if(value_selected)
            {
              commit_storage_child_kind = kv_info[idx].storage_child_kind;
            }
          }
          
          // rjf: browse button to fill text field
          if(has_browse) UI_PrefWidth(ui_text_dim(10, 1))
          {
            UI_FocusHot((row_selected && next_cursor.x == 1) ? UI_FocusKind_On : UI_FocusKind_Off)
              UI_TextAlignment(UI_TextAlign_Center)
              if(ui_clicked(ui_buttonf("Browse...")))
            {
              RD_CmdKind cmd_kind = kv_info[idx].fill_with_file ? RD_CmdKind_PickFile : RD_CmdKind_PickFolder;
              rd_cmd(RD_CmdKind_RunCommand, .string = rd_cmd_kind_info_table[cmd_kind].string);
              tv->pick_dst_kind = kv_info[idx].storage_child_kind;
            }
          }
        }
      }
    }
  }
  
  //- rjf: apply commit
  if(edit_commit)
  {
    String8 new_string = str8(tv->input_buffer, tv->input_size);
    switch(commit_storage_child_kind)
    {
      default:
      {
        RD_Entity *child = rd_entity_child_from_kind(entity, commit_storage_child_kind);
        if(rd_entity_is_nil(child))
        {
          child = rd_entity_alloc(entity, commit_storage_child_kind);
        }
        rd_entity_equip_name(child, new_string);
      }break;
      case RD_EntityKind_Nil:
      {
        rd_entity_equip_name(entity, new_string);
      }break;
    }
  }
  
  //- rjf: apply editing finish
  if(edit_end)
  {
    tv->input_editing = 0;
  }
  if(edit_submit)
  {
    next_cursor.y += 1;
  }
  
  //- rjf: apply moves to selection
  tv->cursor = next_cursor;
  
#endif
  scratch_end(scratch);
  ProfEnd();
}

////////////////////////////////
//~ rjf: targets @view_hook_impl

RD_VIEW_RULE_UI_FUNCTION_DEF(targets)
{
  ProfBeginFunction();
  RD_WatchViewState *wv = rd_view_state(RD_WatchViewState);
  if(!wv->initialized)
  {
    rd_watch_view_init(wv);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Expr,    0.30f, .display_string = str8_lit("Expression"));
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Value,   0.70f, .display_string = str8_lit("Value"), .dequote_string = 1, .is_non_code = 1);
  }
  rd_watch_view_build(wv,
                      RD_WatchViewFlag_NoHeader|
                      RD_WatchViewFlag_RootButtons|
                      RD_WatchViewFlag_PrettyNameMembers,
                      str8_lit("targets"),
                      str8_lit("only:label exe args working_directory entry_point str"), 0, 10, rect);
  ProfEnd();
}

#if 0 // TODO(rjf): @msgs
RD_VIEW_RULE_UI_FUNCTION_DEF(targets)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(0, 0);
  RD_EntityList targets_list = rd_query_cached_entity_list_with_kind(RD_EntityKind_Target);
  RD_EntityFuzzyItemArray targets = rd_entity_fuzzy_item_array_from_entity_list_needle(scratch.arena, &targets_list, string);
  F32 row_height_px = floor_f32(ui_top_font_size()*2.5f);
  
  //- rjf: grab state
  typedef struct RD_TargetsViewState RD_TargetsViewState;
  struct RD_TargetsViewState
  {
    B32 selected_add;
    RD_Handle selected_target_handle;
    S64 selected_column;
  };
  UI_ScrollPt2 scroll_pos = rd_view_scroll_pos();
  RD_TargetsViewState *tv = rd_view_state(RD_TargetsViewState);
  
  //- rjf: determine table bounds
  Vec2S64 table_bounds = {5, (S64)targets.count+1};
  
  //- rjf: selection state => cursor
  // NOTE(rjf): 0 => nothing, 1 => add new, 2 => first target
  Vec2S64 cursor = {0};
  {
    RD_Entity *selected_target = rd_entity_from_handle(tv->selected_target_handle);
    for(U64 idx = 0; idx < targets.count; idx += 1)
    {
      if(selected_target == targets.v[idx].entity)
      {
        cursor.y = (S64)idx+2;
        break;
      }
    }
    if(tv->selected_add)
    {
      cursor.y = 1;
    }
    cursor.x = tv->selected_column;
  }
  
  //- rjf: build
  Rng1S64 visible_row_range = {0};
  UI_ScrollListParams scroll_list_params = {0};
  {
    scroll_list_params.flags         = UI_ScrollListFlag_All;
    scroll_list_params.row_height_px = row_height_px;
    scroll_list_params.dim_px        = dim_2f32(rect);
    scroll_list_params.cursor_range  = r2s64(v2s64(0, 0), v2s64(5, Max(0, (S64)targets.count+1)));
    scroll_list_params.item_range    = r1s64(0, Max(0, (S64)targets.count+1));
    scroll_list_params.cursor_min_is_empty_selection[Axis2_Y] = 1;
  }
  UI_ScrollListSignal scroll_list_sig = {0};
  UI_Focus(UI_FocusKind_On)
    UI_ScrollList(&scroll_list_params,
                  &scroll_pos.y,
                  &cursor,
                  0,
                  &visible_row_range,
                  &scroll_list_sig)
    UI_Focus(UI_FocusKind_Null)
  {
    // rjf: add new ctrl
    if(visible_row_range.min == 0)
    {
      UI_Signal add_sig = {0};
      UI_FocusHot(cursor.y == 1 ? UI_FocusKind_On : UI_FocusKind_Off)
        add_sig = rd_icon_buttonf(RD_IconKind_Add, 0, "Add New Target");
      if(ui_clicked(add_sig))
      {
        rd_cmd(RD_CmdKind_RunCommand, .string = rd_cmd_kind_info_table[RD_CmdKind_AddTarget].string);
      }
    }
    
    // rjf: target rows
    for(S64 row_idx = Max(1, visible_row_range.min);
        row_idx <= visible_row_range.max && row_idx <= targets.count;
        row_idx += 1)
      UI_Row
    {
      RD_Entity *target = targets.v[row_idx-1].entity;
      B32 row_selected = ((U64)cursor.y == row_idx+1);
      
      // rjf: enabled
      UI_PrefWidth(ui_em(2.25f, 1))
        UI_FocusHot((row_selected && cursor.x == 0) ? UI_FocusKind_On : UI_FocusKind_Off)
      {
        UI_Signal sig = rd_icon_buttonf(!target->disabled ? RD_IconKind_CheckFilled : RD_IconKind_CheckHollow, 0, "###ebl_%p", target);
        if(ui_clicked(sig))
        {
          rd_cmd(!target->disabled ? RD_CmdKind_DisableTarget : RD_CmdKind_EnableTarget, .entity = rd_handle_from_entity(target));
        }
      }
      
      // rjf: target name
      UI_WidthFill UI_FocusHot((row_selected && cursor.x == 1) ? UI_FocusKind_On : UI_FocusKind_Off)
      {
        rd_entity_desc_button(target, &targets.v[row_idx-1].matches, string, 0);
      }
      
      // rjf: controls
      UI_PrefWidth(ui_em(2.25f, 1.f))
      {
        struct
        {
          RD_IconKind icon;
          String8 text;
          RD_CmdKind cmd;
        }
        ctrls[] =
        {
          { RD_IconKind_PlayStepForward,  str8_lit("Launch and Initialize"), RD_CmdKind_LaunchAndInit },
          { RD_IconKind_Play,             str8_lit("Launch and Run"),        RD_CmdKind_LaunchAndRun  },
          { RD_IconKind_Pencil,           str8_lit("Edit"),                  RD_CmdKind_Target        },
          { RD_IconKind_Trash,            str8_lit("Delete"),                RD_CmdKind_RemoveTarget  },
        };
        for(U64 ctrl_idx = 0; ctrl_idx < ArrayCount(ctrls); ctrl_idx += 1)
        {
          UI_Signal sig = {0};
          UI_FocusHot((row_selected && cursor.x == 2+ctrl_idx) ? UI_FocusKind_On : UI_FocusKind_Off)
          {
            sig = rd_icon_buttonf(ctrls[ctrl_idx].icon, 0, "###%p_ctrl_%i", target, (int)ctrl_idx);
          }
          if(ui_hovering(sig)) UI_Tooltip
          {
            ui_label(ctrls[ctrl_idx].text);
          }
          if(ui_clicked(sig))
          {
            rd_cmd(ctrls[ctrl_idx].cmd, .entity = rd_handle_from_entity(target));
          }
        }
      }
    }
  }
  
  //- rjf: commit cursor to selection state
  {
    tv->selected_column = cursor.x;
    tv->selected_target_handle = (1 < cursor.y && cursor.y < targets.count+2) ? rd_handle_from_entity(targets.v[cursor.y-2].entity) : rd_handle_zero();
    tv->selected_add = (cursor.y == 1);
  }
  
  rd_store_view_scroll_pos(scroll_pos);
  scratch_end(scratch);
  ProfEnd();
}
#endif

////////////////////////////////
//~ rjf: file_path_map @view_hook_impl

typedef struct RD_FilePathMapViewState RD_FilePathMapViewState;
struct RD_FilePathMapViewState
{
  B32 initialized;
  Vec2S64 cursor;
  TxtPt input_cursor;
  TxtPt input_mark;
  U8 input_buffer[1024];
  U64 input_size;
  B32 input_editing;
  RD_Handle pick_file_dst_map;
  Side pick_file_dst_side;
  F32 src_column_pct;
  F32 dst_column_pct;
};

RD_VIEW_RULE_UI_FUNCTION_DEF(file_path_map)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(0, 0);
  RD_EntityList maps_list = rd_query_cached_entity_list_with_kind(RD_EntityKind_FilePathMap);
  RD_EntityArray maps = rd_entity_array_from_list(scratch.arena, &maps_list);
  F32 row_height_px = floor_f32(ui_top_font_size()*2.5f);
  
  RD_FilePathMapViewState *fpms = rd_view_state(RD_FilePathMapViewState);
  UI_ScrollPt2 scroll_pos = rd_view_scroll_pos();
  if(fpms->initialized == 0)
  {
    fpms->initialized = 1;
    fpms->src_column_pct = 0.5f;
    fpms->dst_column_pct = 0.5f;
  }
  
#if 0 // TODO(rjf): @msgs
  RD_FilePathMapViewState *fpms = rd_view_state(RD_FilePathMapViewState);
  
  // rjf: process commands
  for(RD_Cmd *cmd = 0; rd_next_cmd(&cmd);)
  {
    // rjf: mismatched window/panel => skip
    if(!rd_handle_match(rd_regs()->view, cmd->regs->view))
    {
      continue;
    }
    
    //rjf: process
    RD_CmdKind kind = rd_cmd_kind_from_string(cmd->name);
    switch(kind)
    {
      default:break;
      case RD_CmdKind_PickFile:
      case RD_CmdKind_PickFolder:
      case RD_CmdKind_PickFileOrFolder:
      {
        String8 pick_string = cmd->regs->file_path;
        Side pick_side = fpms->pick_file_dst_side;
        RD_Entity *storage_entity = rd_entity_from_handle(fpms->pick_file_dst_map);
        rd_cmd(pick_side == Side_Min ?
               RD_CmdKind_SetFileOverrideLinkSrc :
               RD_CmdKind_SetFileOverrideLinkDst,
               .entity    = rd_handle_from_entity(storage_entity),
               .file_path = pick_string);
      }break;
    }
  }
#endif
  
  //- rjf: take controls to start/end editing
  B32 edit_begin  = 0;
  B32 edit_end    = 0;
  B32 edit_commit = 0;
  B32 edit_submit = 0;
  UI_Focus(UI_FocusKind_On) if(ui_is_focus_active())
  {
    if(!fpms->input_editing)
    {
      for(UI_Event *evt = 0; ui_next_event(&evt);)
      {
        if(evt->string.size != 0 || evt->flags & UI_EventFlag_Paste)
        {
          edit_begin = 1;
          break;
        }
      }
      if(ui_slot_press(UI_EventActionSlot_Edit))
      {
        edit_begin = 1;
      }
    }
    if(fpms->input_editing)
    {
      if(ui_slot_press(UI_EventActionSlot_Cancel))
      {
        edit_end = 1;
        edit_commit = 0;
      }
      if(ui_slot_press(UI_EventActionSlot_Accept))
      {
        edit_end = 1;
        edit_commit = 1;
        edit_submit = 1;
      }
    }
  }
  
  //- rjf: build
  RD_Handle commit_map = rd_handle_zero();
  Side commit_side = Side_Invalid;
  F32 *col_pcts[] = { &fpms->src_column_pct, &fpms->dst_column_pct };
  Vec2S64 next_cursor = fpms->cursor;
  Rng1S64 visible_row_range = {0};
  UI_ScrollListParams scroll_list_params = {0};
  {
    scroll_list_params.flags         = UI_ScrollListFlag_All;
    scroll_list_params.row_height_px = row_height_px;
    scroll_list_params.dim_px        = dim_2f32(rect);
    scroll_list_params.cursor_range  = r2s64(v2s64(0, 0), v2s64(3, maps.count + 1));
    scroll_list_params.item_range    = r1s64(0, maps.count+2);
    scroll_list_params.cursor_min_is_empty_selection[Axis2_Y] = 1;
  }
  UI_ScrollListSignal scroll_list_sig = {0};
  UI_Focus(UI_FocusKind_On)
    UI_ScrollList(&scroll_list_params,
                  &scroll_pos.y,
                  fpms->input_editing ? 0 : &fpms->cursor,
                  0,
                  &visible_row_range,
                  &scroll_list_sig)
    UI_Focus(UI_FocusKind_Null)
    UI_TableF(ArrayCount(col_pcts), col_pcts, "###tbl")
  {
    next_cursor = fpms->cursor;
    
    //- rjf: header
    if(visible_row_range.min == 0) UI_TableVector UI_FlagsAdd(UI_BoxFlag_DrawTextWeak)
    {
      UI_TableCell if(rd_help_label(str8_lit("Source Path"))) UI_Tooltip
      {
        ui_label_multiline(ui_top_font_size()*30, str8_lit("When the debugger attempts to open a file or folder at a Source Path specified in this table, it will redirect to the file or folder specified by the Destination Path."));
      }
      UI_TableCell ui_label(str8_lit("Destination Path"));
    }
    
    //- rjf: map rows
    for(S64 row_idx = Max(1, visible_row_range.min);
        row_idx <= visible_row_range.max && row_idx <= maps.count+1;
        row_idx += 1) UI_TableVector
    {
      U64 map_idx = row_idx-1;
      RD_Entity *map = (map_idx < maps.count ? maps.v[map_idx] : &d_nil_entity);
      RD_Entity *map_src = rd_entity_child_from_kind(map, RD_EntityKind_Source);
      RD_Entity *map_dst = rd_entity_child_from_kind(map, RD_EntityKind_Dest);
      String8 map_src_path = map_src->string;
      String8 map_dst_path = map_dst->string;
      B32 row_selected = (fpms->cursor.y == row_idx);
      
      //- rjf: src
      UI_TableCell UI_WidthFill
      {
        //- rjf: editor
        {
          B32 value_selected = (row_selected && fpms->cursor.x == 0);
          
          // rjf: begin editing
          if(value_selected && edit_begin)
          {
            fpms->input_editing = 1;
            fpms->input_size = Min(sizeof(fpms->input_buffer), map_src_path.size);
            MemoryCopy(fpms->input_buffer, map_src_path.str, fpms->input_size);
            fpms->input_cursor = txt_pt(1, 1+fpms->input_size);
            fpms->input_mark = txt_pt(1, 1);
          }
          
          // rjf: build
          UI_Signal sig = {0};
          UI_FocusHot(value_selected ? UI_FocusKind_On : UI_FocusKind_Off)
            UI_FocusActive((value_selected && fpms->input_editing) ? UI_FocusKind_On : UI_FocusKind_Off)
          {
            sig = rd_line_editf(RD_LineEditFlag_NoBackground, 0, 0, &fpms->input_cursor, &fpms->input_mark, fpms->input_buffer, sizeof(fpms->input_buffer), &fpms->input_size, 0, map_src_path, "###src_editor_%p", map);
            edit_commit = edit_commit || ui_committed(sig);
          }
          
          // rjf: focus panel on press
          if(ui_pressed(sig))
          {
            rd_cmd(RD_CmdKind_FocusPanel);
          }
          
          // rjf: begin editing on double-click
          if(!fpms->input_editing && ui_double_clicked(sig))
          {
            fpms->input_editing = 1;
            fpms->input_size = Min(sizeof(fpms->input_buffer), map_src_path.size);
            MemoryCopy(fpms->input_buffer, map_src_path.str, fpms->input_size);
            fpms->input_cursor = txt_pt(1, 1+fpms->input_size);
            fpms->input_mark = txt_pt(1, 1);
          }
          
          // rjf: press on non-selected => commit edit, change selected cell
          if(ui_pressed(sig) && !value_selected)
          {
            edit_end = 1;
            edit_commit = fpms->input_editing;
            next_cursor.x = 0;
            next_cursor.y = map_idx+1;
          }
          
          // rjf: store commit information
          if(value_selected)
          {
            commit_side = Side_Min;
            commit_map = rd_handle_from_entity(map);
          }
        }
        
        //- rjf: browse button
        UI_FocusHot((row_selected && fpms->cursor.x == 1) ? UI_FocusKind_On : UI_FocusKind_Off)
          UI_PrefWidth(ui_text_dim(10, 1))
          if(ui_clicked(ui_buttonf("Browse...")))
        {
          rd_cmd(RD_CmdKind_RunCommand, .string = rd_cmd_kind_info_table[RD_CmdKind_PickFileOrFolder].string);
          fpms->pick_file_dst_map = rd_handle_from_entity(map);
          fpms->pick_file_dst_side = Side_Min;
        }
      }
      
      //- rjf: dst
      UI_TableCell UI_WidthFill
      {
        //- rjf: editor
        {
          B32 value_selected = (row_selected && fpms->cursor.x == 2);
          
          // rjf: begin editing
          if(value_selected && edit_begin)
          {
            fpms->input_editing = 1;
            fpms->input_size = Min(sizeof(fpms->input_buffer), map_dst_path.size);
            MemoryCopy(fpms->input_buffer, map_dst_path.str, fpms->input_size);
            fpms->input_cursor = txt_pt(1, 1+fpms->input_size);
            fpms->input_mark = txt_pt(1, 1);
          }
          
          // rjf: build
          UI_Signal sig = {0};
          UI_FocusHot(value_selected ? UI_FocusKind_On : UI_FocusKind_Off)
            UI_FocusActive((value_selected && fpms->input_editing) ? UI_FocusKind_On : UI_FocusKind_Off)
          {
            sig = rd_line_editf(RD_LineEditFlag_NoBackground, 0, 0, &fpms->input_cursor, &fpms->input_mark, fpms->input_buffer, sizeof(fpms->input_buffer), &fpms->input_size, 0, map_dst_path, "###dst_editor_%p", map);
            edit_commit = edit_commit || ui_committed(sig);
          }
          
          // rjf: focus panel on press
          if(ui_pressed(sig))
          {
            rd_cmd(RD_CmdKind_FocusPanel);
          }
          
          // rjf: begin editing on double-click
          if(!fpms->input_editing && ui_double_clicked(sig))
          {
            fpms->input_editing = 1;
            fpms->input_size = Min(sizeof(fpms->input_buffer), map_dst_path.size);
            MemoryCopy(fpms->input_buffer, map_dst_path.str, fpms->input_size);
            fpms->input_cursor = txt_pt(1, 1+fpms->input_size);
            fpms->input_mark = txt_pt(1, 1);
          }
          
          // rjf: press on non-selected => commit edit, change selected cell
          if(ui_pressed(sig) && !value_selected)
          {
            edit_end = 1;
            edit_commit = fpms->input_editing;
            next_cursor.x = 2;
            next_cursor.y = map_idx+1;
          }
          
          // rjf: store commit information
          if(value_selected)
          {
            commit_side = Side_Max;
            commit_map = rd_handle_from_entity(map);
          }
        }
        
        //- rjf: browse button
        {
          UI_FocusHot((row_selected && fpms->cursor.x == 3) ? UI_FocusKind_On : UI_FocusKind_Off)
            UI_PrefWidth(ui_text_dim(10, 1))
            if(ui_clicked(ui_buttonf("Browse...")))
          {
            rd_cmd(RD_CmdKind_RunCommand, .string = rd_cmd_kind_info_table[RD_CmdKind_PickFileOrFolder].string);
            fpms->pick_file_dst_map = rd_handle_from_entity(map);
            fpms->pick_file_dst_side = Side_Max;
          }
        }
      }
    }
  }
  
  //- rjf: apply commit
  if(edit_commit && commit_side != Side_Invalid)
  {
    String8 new_string = str8(fpms->input_buffer, fpms->input_size);
    rd_cmd(commit_side == Side_Min ?
           RD_CmdKind_SetFileOverrideLinkSrc :
           RD_CmdKind_SetFileOverrideLinkDst,
           .entity    = commit_map,
           .file_path = new_string);
  }
  
  //- rjf: apply editing finish
  if(edit_end)
  {
    fpms->input_editing = 0;
  }
  
  //- rjf: move down one row if submitted
  if(edit_submit)
  {
    next_cursor.y += 1;
  }
  
  //- rjf: apply moves to selection
  fpms->cursor = next_cursor;
  
  rd_store_view_scroll_pos(scroll_pos);
  scratch_end(scratch);
  ProfEnd();
}

////////////////////////////////
//~ rjf: auto_view_rules @view_hook_impl

RD_VIEW_RULE_UI_FUNCTION_DEF(auto_view_rules){}

////////////////////////////////
//~ rjf: breakpoints @view_hook_impl

RD_VIEW_RULE_UI_FUNCTION_DEF(breakpoints)
{
  ProfBeginFunction();
  RD_WatchViewState *wv = rd_view_state(RD_WatchViewState);
  if(!wv->initialized)
  {
    rd_watch_view_init(wv);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Member, 0.25f, .string = str8_lit("label.str"),    .display_string = str8_lit("Label"), .dequote_string = 1);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Member, 0.35f, .string = str8_lit("location.str"), .display_string = str8_lit("Location"), .dequote_string = 1, .is_non_code = 1);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Member, 0.20f, .string = str8_lit("condition.str"),.display_string = str8_lit("Condition"), .dequote_string = 1);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Member, 0.10f, .string = str8_lit("enabled"),      .display_string = str8_lit("Enabled"), .view_rule = str8_lit("checkbox"));
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Member, 0.10f, .string = str8_lit("hit_count"),    .display_string = str8_lit("Hit Count"));
  }
  rd_watch_view_build(wv, 0, str8_lit("breakpoints"), str8_lit(""), 0, 10, rect);
  ProfEnd();
}

////////////////////////////////
//~ rjf: watch_pins @view_hook_impl

RD_VIEW_RULE_UI_FUNCTION_DEF(watch_pins)
{
  ProfBeginFunction();
  RD_WatchViewState *wv = rd_view_state(RD_WatchViewState);
  if(!wv->initialized)
  {
    rd_watch_view_init(wv);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Member, 0.5f, .string = str8_lit("Label"), .dequote_string = 1);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Member, 0.5f, .string = str8_lit("Location"), .dequote_string = 1, .is_non_code = 1);
  }
  rd_watch_view_build(wv, 0, str8_lit("watch_pins"), str8_lit(""), 0, 10, rect);
  ProfEnd();
}

////////////////////////////////
//~ rjf: scheduler @view_hook_impl

RD_VIEW_RULE_UI_FUNCTION_DEF(scheduler)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(0, 0);
  UI_ScrollPt2 scroll_pos = rd_view_scroll_pos();
  String8 query = string;
  
  //- rjf: get state
  typedef struct RD_SchedulerViewState RD_SchedulerViewState;
  struct RD_SchedulerViewState
  {
    RD_Handle selected_entity;
    S64 selected_column;
  };
  RD_SchedulerViewState *sv = rd_view_state(RD_SchedulerViewState);
  
  //- rjf: get entities
  RD_EntityList machines  = rd_query_cached_entity_list_with_kind(RD_EntityKind_Machine);
  RD_EntityList processes = rd_query_cached_entity_list_with_kind(RD_EntityKind_Process);
  RD_EntityList threads   = rd_query_cached_entity_list_with_kind(RD_EntityKind_Thread);
  
  //- rjf: produce list of items; no query -> all entities, in tree; query -> only show threads
  RD_EntityFuzzyItemArray items = {0};
  ProfScope("query -> entities")
  {
    if(query.size == 0)
    {
      //- rjf: build flat array of entities, arranged into row order
      RD_EntityArray entities = {0};
      {
        entities.count = machines.count+processes.count+threads.count;
        entities.v = push_array_no_zero(scratch.arena, RD_Entity *, entities.count);
        U64 idx = 0;
        for(RD_EntityNode *machine_n = machines.first; machine_n != 0; machine_n = machine_n->next)
        {
          RD_Entity *machine = machine_n->entity;
          entities.v[idx] = machine;
          idx += 1;
          for(RD_EntityNode *process_n = processes.first; process_n != 0; process_n = process_n->next)
          {
            RD_Entity *process = process_n->entity;
            if(rd_entity_ancestor_from_kind(process, RD_EntityKind_Machine) != machine)
            {
              continue;
            }
            entities.v[idx] = process;
            idx += 1;
            for(RD_EntityNode *thread_n = threads.first; thread_n != 0; thread_n = thread_n->next)
            {
              RD_Entity *thread = thread_n->entity;
              if(rd_entity_ancestor_from_kind(thread, RD_EntityKind_Process) != process)
              {
                continue;
              }
              entities.v[idx] = thread;
              idx += 1;
            }
          }
        }
      }
      
      //- rjf: entities -> fuzzy-filtered entities
      items = rd_entity_fuzzy_item_array_from_entity_array_needle(scratch.arena, &entities, query);
    }
    else
    {
      items = rd_entity_fuzzy_item_array_from_entity_list_needle(scratch.arena, &threads, query);
    }
  }
  
  //- rjf: selected column/entity -> selected cursor
  Vec2S64 cursor = {sv->selected_column};
  for(U64 idx = 0; idx < items.count; idx += 1)
  {
    if(items.v[idx].entity == rd_entity_from_handle(sv->selected_entity))
    {
      cursor.y = (S64)(idx+1);
      break;
    }
  }
  
  //- rjf: build table
  Rng1S64 visible_row_range = {0};
  UI_ScrollListParams scroll_list_params = {0};
  {
    scroll_list_params.flags         = UI_ScrollListFlag_All;
    scroll_list_params.row_height_px = floor_f32(ui_top_font_size()*2.5f);
    scroll_list_params.dim_px        = dim_2f32(rect);
    scroll_list_params.cursor_range  = r2s64(v2s64(0, 0), v2s64(4, items.count));
    scroll_list_params.item_range    = r1s64(0, items.count);
    scroll_list_params.cursor_min_is_empty_selection[Axis2_Y] = 1;
  }
  UI_ScrollListSignal scroll_list_sig = {0};
  UI_Focus(UI_FocusKind_On)
    UI_ScrollList(&scroll_list_params,
                  &scroll_pos.y,
                  &cursor,
                  0,
                  &visible_row_range,
                  &scroll_list_sig)
    UI_Focus(UI_FocusKind_Null)
    UI_TableF(0, 0, "scheduler_table")
  {
    Vec2S64 next_cursor = cursor;
    for(U64 idx = visible_row_range.min;
        idx <= visible_row_range.max && idx < items.count;
        idx += 1)
    {
      RD_Entity *entity = items.v[idx].entity;
      CTRL_Entity *entity_ctrl = ctrl_entity_from_handle(d_state->ctrl_entity_store, entity->ctrl_handle);
      B32 row_is_selected = (cursor.y == (S64)(idx+1));
      F32 depth = 0.f;
      if(query.size == 0) switch(entity->kind)
      {
        default:{}break;
        case RD_EntityKind_Machine:{depth = 0.f;}break;
        case RD_EntityKind_Process:{depth = 1.f;}break;
        case RD_EntityKind_Thread: {depth = 2.f;}break;
      }
      Rng1S64 desc_col_rng = r1s64(1, 1);
      switch(entity->kind)
      {
        default:{}break;
        case RD_EntityKind_Machine:{desc_col_rng = r1s64(1, 4);}break;
        case RD_EntityKind_Process:{desc_col_rng = r1s64(1, 1);}break;
        case RD_EntityKind_Thread: {desc_col_rng = r1s64(1, 1);}break;
      }
      UI_NamedTableVectorF("entity_row_%p", entity)
      {
        UI_TableCellSized(ui_em(1.5f*depth, 1.f)) {}
        UI_TableCellSized(ui_em(2.25f, 1.f)) UI_FocusHot((row_is_selected && cursor.x == 0) ? UI_FocusKind_On : UI_FocusKind_Off)
        {
          B32 frozen = ctrl_entity_tree_is_frozen(entity_ctrl);
          UI_Palette *palette = ui_top_palette();
          if(frozen)
          {
            palette = rd_palette_from_code(RD_PaletteCode_NegativePopButton);
          }
          else
          {
            palette = rd_palette_from_code(RD_PaletteCode_PositivePopButton);
          }
          UI_Signal sig = {0};
          UI_Palette(palette) sig = rd_icon_buttonf(frozen ? RD_IconKind_Locked : RD_IconKind_Unlocked, 0, "###lock_%p", entity);
          if(ui_clicked(sig))
          {
            D_CmdKind cmd_kind = frozen ? D_CmdKind_ThawEntity : D_CmdKind_FreezeEntity;
            rd_cmd(cmd_kind, .entity = rd_handle_from_entity(entity));
          }
        }
        UI_TableCellSized(ui_pct(1, 0))
          UI_FocusHot((row_is_selected && desc_col_rng.min <= cursor.x && cursor.x <= desc_col_rng.max) ? UI_FocusKind_On : UI_FocusKind_Off)
        {
          rd_entity_desc_button(entity, &items.v[idx].matches, query, 0);
        }
        switch(entity->kind)
        {
          default:{}break;
          case RD_EntityKind_Machine:
          {
            
          }break;
          case RD_EntityKind_Process:
          {
            UI_TableCellSized(ui_children_sum(1.f)) UI_FocusHot((row_is_selected && cursor.x == 2) ? UI_FocusKind_On : UI_FocusKind_Off)
            {
              UI_PrefWidth(ui_text_dim(10, 1))
                UI_TextAlignment(UI_TextAlign_Center)
                if(ui_clicked(ui_buttonf("Detach")))
              {
                rd_cmd(D_CmdKind_Detach, .entity = rd_handle_from_entity(entity));
              }
            }
            UI_TableCellSized(ui_em(2.25f, 1.f)) UI_FocusHot((row_is_selected && cursor.x == 3) ? UI_FocusKind_On : UI_FocusKind_Off)
            {
              if(ui_clicked(rd_icon_buttonf(RD_IconKind_Redo, 0, "###retry")))
              {
                rd_cmd(D_CmdKind_Restart, .entity = rd_handle_from_entity(entity));
              }
            }
            UI_TableCellSized(ui_em(2.25f, 1.f)) UI_FocusHot((row_is_selected && cursor.x == 4) ? UI_FocusKind_On : UI_FocusKind_Off)
            {
              RD_Palette(RD_PaletteCode_NegativePopButton)
                if(ui_clicked(rd_icon_buttonf(RD_IconKind_X, 0, "###kill")))
              {
                rd_cmd(D_CmdKind_Kill, .entity = rd_handle_from_entity(entity));
              }
            }
          }break;
          case RD_EntityKind_Thread:
          {
            UI_TableCellSized(ui_children_sum(1.f)) UI_FocusHot((row_is_selected && cursor.x >= 2) ? UI_FocusKind_On : UI_FocusKind_Off)
            {
              CTRL_Entity *entity_ctrl = ctrl_entity_from_handle(d_state->ctrl_entity_store, entity->ctrl_handle);
              CTRL_Entity *process = ctrl_entity_ancestor_from_kind(entity_ctrl, RD_EntityKind_Process);
              U64 rip_vaddr = d_query_cached_rip_from_thread(entity_ctrl);
              CTRL_Entity *module = ctrl_module_from_process_vaddr(process, rip_vaddr);
              U64 rip_voff = ctrl_voff_from_vaddr(module, rip_vaddr);
              DI_Key dbgi_key = ctrl_dbgi_key_from_module(module);
              D_LineList lines = d_lines_from_dbgi_key_voff(scratch.arena, &dbgi_key, rip_voff);
              if(lines.first != 0)
              {
                UI_PrefWidth(ui_children_sum(0)) rd_src_loc_button(lines.first->v.file_path, lines.first->v.pt);
              }
            }
          }break;
        }
      }
    }
    cursor = next_cursor;
  }
  
  //- rjf: selected num -> selected entity
  sv->selected_column = cursor.x;
  sv->selected_entity = (1 <= cursor.y && cursor.y <= items.count) ? rd_handle_from_entity(items.v[cursor.y-1].entity) : rd_handle_zero();
  
  rd_store_view_scroll_pos(scroll_pos);
  scratch_end(scratch);
  ProfEnd();
}

////////////////////////////////
//~ rjf: call_stack @view_hook_impl

RD_VIEW_RULE_UI_FUNCTION_DEF(call_stack)
{
  ProfBeginFunction();
  RD_WatchViewState *wv = rd_view_state(RD_WatchViewState);
  if(!wv->initialized)
  {
    rd_watch_view_init(wv);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_FrameSelection,  0.05f);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Value,  0.7f);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Module, 0.25f, .is_non_code = 1);
  }
  rd_watch_view_build(wv, 0, str8_lit("current_thread.callstack.v"), str8_lit("array:current_thread.callstack.count"), 0, 10, rect);
  ProfEnd();
}

////////////////////////////////
//~ rjf: modules @view_hook_impl

RD_VIEW_RULE_UI_FUNCTION_DEF(modules)
{
  ProfBeginFunction();
  RD_WatchViewState *wv = rd_view_state(RD_WatchViewState);
  if(!wv->initialized)
  {
    rd_watch_view_init(wv);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Member, 0.20f, .string = str8_lit("label.str"),             .display_string = str8_lit("Name"),            .dequote_string = 1, .is_non_code = 1);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Member, 0.30f, .string = str8_lit("exe.str"),               .display_string = str8_lit("Executable Path"), .dequote_string = 1, .is_non_code = 1);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Member, 0.30f, .string = str8_lit("dbg.str"),               .display_string = str8_lit("Debug Info Path"), .dequote_string = 1, .is_non_code = 1);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Member, 0.20f, .string = str8_lit("vaddr_range"),           .display_string = str8_lit("Address Range"),   .rangify_braces = 1, .view_rule = str8_lit("hex"));
  }
  rd_watch_view_build(wv, 0, str8_lit("modules"), str8_lit(""), 0, 10, rect);
  ProfEnd();
}

#if 0 // TODO(rjf): @msgs
typedef struct RD_ModulesViewState RD_ModulesViewState;
struct RD_ModulesViewState
{
  B32 initialized;
  RD_Handle selected_entity;
  S64 selected_column;
  B32 txt_editing;
  TxtPt txt_cursor;
  TxtPt txt_mark;
  U8 txt_buffer[1024];
  U64 txt_size;
  RD_Handle pick_file_dst_entity;
  F32 idx_col_pct;
  F32 desc_col_pct;
  F32 range_col_pct;
  F32 dbg_col_pct;
};

RD_VIEW_RULE_UI_FUNCTION_DEF(modules)
{
  RD_ModulesViewState *mv = rd_view_state(RD_ModulesViewState);
  if(mv->initialized == 0)
  {
    mv->initialized = 1;
    mv->idx_col_pct   = 0.05f;
    mv->desc_col_pct  = 0.15f;
    mv->range_col_pct = 0.30f;
    mv->dbg_col_pct   = 0.50f;
  }
  RD_ModulesViewState *mv = rd_view_state(RD_ModulesViewState);
  for(RD_Cmd *cmd = 0; rd_next_cmd(&cmd);)
  {
    // rjf: mismatched window/panel => skip
    if(!rd_handle_match(rd_regs()->view, cmd->regs->view))
    {
      continue;
    }
    
    //rjf: process
    RD_CmdKind kind = rd_cmd_kind_from_string(cmd->name);
    switch(kind)
    {
      default:break;
      case RD_CmdKind_PickFile:
      {
        Temp scratch = scratch_begin(0, 0);
        String8 pick_string = cmd->regs->file_path;
        RD_Entity *module = rd_entity_from_handle(mv->pick_file_dst_entity);
        if(module->kind == RD_EntityKind_Module)
        {
          String8 exe_path = module->string;
          String8 dbg_path = pick_string;
          // TODO(rjf)
        }
        scratch_end(scratch);
      }break;
    }
  }
  ProfBeginFunction();
  Temp scratch = scratch_begin(0, 0);
  DI_Scope *scope = di_scope_open();
  String8 query = str8(view->query_buffer, view->query_string_size);
  
  //- rjf: get state
  RD_ModulesViewState *mv = rd_view_state(RD_ModulesViewState);
  F32 *col_pcts[] = {&mv->idx_col_pct, &mv->desc_col_pct, &mv->range_col_pct, &mv->dbg_col_pct};
  
  //- rjf: get entities
  RD_EntityList processes = rd_query_cached_entity_list_with_kind(RD_EntityKind_Process);
  RD_EntityList modules = rd_query_cached_entity_list_with_kind(RD_EntityKind_Module);
  
  //- rjf: make filtered item array
  RD_EntityFuzzyItemArray items = {0};
  if(query.size == 0)
  {
    RD_EntityArray entities = {0};
    {
      entities.count = processes.count+modules.count;
      entities.v = push_array_no_zero(scratch.arena, RD_Entity *, entities.count);
      U64 idx = 0;
      for(RD_EntityNode *process_n = processes.first; process_n != 0; process_n = process_n->next)
      {
        RD_Entity *process = process_n->entity;
        entities.v[idx] = process;
        idx += 1;
        for(RD_EntityNode *module_n = modules.first; module_n != 0; module_n = module_n->next)
        {
          RD_Entity *module = module_n->entity;
          if(rd_entity_ancestor_from_kind(module, RD_EntityKind_Process) != process)
          {
            continue;
          }
          entities.v[idx] = module;
          idx += 1;
        }
      }
    }
    items = rd_entity_fuzzy_item_array_from_entity_array_needle(scratch.arena, &entities, query);
  }
  else
  {
    items = rd_entity_fuzzy_item_array_from_entity_list_needle(scratch.arena, &modules, query);
  }
  
  //- rjf: selected column/entity -> selected cursor
  Vec2S64 cursor = {mv->selected_column};
  for(U64 idx = 0; idx < items.count; idx += 1)
  {
    if(items.v[idx].entity == rd_entity_from_handle(mv->selected_entity))
    {
      cursor.y = (S64)(idx+1);
      break;
    }
  }
  
  //////////////////////////////
  //- rjf: do start/end editing interaction
  //
  B32 edit_begin           = 0;
  B32 edit_commit          = 0;
  B32 edit_end             = 0;
  B32 edit_submit          = 0;
  if(!mv->txt_editing && ui_is_focus_active())
  {
    for(UI_Event *evt = 0; ui_next_event(&evt);)
    {
      if(evt->string.size != 0 || evt->flags & UI_EventFlag_Paste)
      {
        edit_begin = 1;
        break;
      }
    }
    if(ui_slot_press(UI_EventActionSlot_Edit))
    {
      edit_begin = 1;
    }
  }
  if(mv->txt_editing && ui_is_focus_active())
  {
    if(ui_slot_press(UI_EventActionSlot_Cancel))
    {
      edit_end = 1;
      edit_commit = 0;
    }
    if(ui_slot_press(UI_EventActionSlot_Accept))
    {
      edit_end = 1;
      edit_commit = 1;
      edit_submit = 1;
    }
  }
  
  //- rjf: build table
  RD_Entity *commit_module = &d_nil_entity;
  Rng1S64 visible_row_range = {0};
  UI_ScrollListParams scroll_list_params = {0};
  {
    scroll_list_params.flags         = UI_ScrollListFlag_All;
    scroll_list_params.row_height_px = floor_f32(ui_top_font_size()*2.5f);
    scroll_list_params.dim_px        = dim_2f32(rect);
    scroll_list_params.cursor_range  = r2s64(v2s64(0, 0), v2s64(3, items.count));
    scroll_list_params.item_range    = r1s64(0, items.count);
    scroll_list_params.cursor_min_is_empty_selection[Axis2_Y] = 1;
  }
  UI_ScrollListSignal scroll_list_sig = {0};
  UI_Focus(UI_FocusKind_On)
    UI_ScrollList(&scroll_list_params,
                  &scroll_pos.y,
                  mv->txt_editing ? 0 : &cursor,
                  0,
                  &visible_row_range,
                  &scroll_list_sig)
    UI_Focus(UI_FocusKind_Null)
    UI_TableF(ArrayCount(col_pcts), col_pcts, "modules_table")
  {
    Vec2S64 next_cursor = cursor;
    U64 idx_in_process = 0;
    for(U64 idx = 0; idx < items.count; idx += 1)
    {
      RD_Entity *entity = items.v[idx].entity;
      B32 row_is_selected = (cursor.y == (S64)(idx+1));
      idx_in_process += (entity->kind == RD_EntityKind_Module);
      if(visible_row_range.min <= idx && idx <= visible_row_range.max)
      {
        switch(entity->kind)
        {
          default:{}break;
          case RD_EntityKind_Process:
          {
            UI_NamedTableVectorF("process_%p", entity)
            {
              UI_TableCellSized(ui_pct(1, 0)) UI_FocusHot((row_is_selected) ? UI_FocusKind_On : UI_FocusKind_Off)
              {
                rd_entity_desc_button(entity, &items.v[idx].matches, query, 0);
              }
            }
            idx_in_process = 0;
          }break;
          case RD_EntityKind_Module:
          UI_NamedTableVectorF("module_%p", entity)
          {
            UI_TableCell UI_TextAlignment(UI_TextAlign_Center) UI_FlagsAdd(UI_BoxFlag_DrawTextWeak)
            {
              ui_labelf("%I64u", idx_in_process);
            }
            UI_TableCell UI_FocusHot((row_is_selected && cursor.x == 0) ? UI_FocusKind_On : UI_FocusKind_Off)
            {
              rd_entity_desc_button(entity, &items.v[idx].matches, query, 1);
            }
            UI_TableCell RD_Font(RD_FontSlot_Code) UI_FocusHot((row_is_selected && cursor.x == 1) ? UI_FocusKind_On : UI_FocusKind_Off)
            {
              UI_Box *range_box = ui_build_box_from_stringf(UI_BoxFlag_Clickable|UI_BoxFlag_DrawText, "[0x%I64x, 0x%I64x)###vaddr_range_%p", entity->vaddr_rng.min, entity->vaddr_rng.max, entity);
              UI_Signal sig = ui_signal_from_box(range_box);
              if(ui_pressed(sig))
              {
                next_cursor = v2s64(1, (S64)idx+1);
                rd_cmd(RD_CmdKind_FocusPanel);
              }
            }
            UI_TableCell
            {
              B32 txt_is_selected = (row_is_selected && cursor.x == 2);
              B32 brw_is_selected = (row_is_selected && cursor.x == 3);
              
              // rjf: unpack module info
              DI_Key dbgi_key = d_dbgi_key_from_module(entity);
              String8 dbgi_path = dbgi_key.path;
              RDI_Parsed *rdi = di_rdi_from_key(scope, &dbgi_key, 0);
              B32 dbgi_is_valid = (rdi != &di_rdi_parsed_nil);
              
              // rjf: begin editing
              if(txt_is_selected && edit_begin)
              {
                mv->txt_editing = 1;
                mv->txt_size = Min(sizeof(mv->txt_buffer), dbgi_path.size);
                MemoryCopy(mv->txt_buffer, dbgi_path.str, mv->txt_size);
                mv->txt_cursor = txt_pt(1, 1+mv->txt_size);
                mv->txt_mark = txt_pt(1, 1);
              }
              
              // rjf: build
              UI_Signal sig = {0};
              UI_FocusHot(txt_is_selected ? UI_FocusKind_On : UI_FocusKind_Off)
                UI_FocusActive((txt_is_selected && mv->txt_editing) ? UI_FocusKind_On : UI_FocusKind_Off)
                UI_WidthFill
              {
                UI_Palette(dbgi_is_valid ? ui_top_palette() : ui_build_palette(ui_top_palette(), .text = rd_rgba_from_theme_color(RD_ThemeColor_TextNegative)))
                  sig = rd_line_editf(RD_LineEditFlag_NoBackground, 0, 0, &mv->txt_cursor, &mv->txt_mark, mv->txt_buffer, sizeof(mv->txt_buffer), &mv->txt_size, 0, dbgi_path, "###dbg_path_%p", entity);
                edit_commit = (edit_commit || ui_committed(sig));
              }
              
              // rjf: press -> focus
              if(ui_pressed(sig))
              {
                edit_commit = (mv->txt_editing && !txt_is_selected);
                next_cursor = v2s64(2, (S64)idx+1);
                rd_cmd(RD_CmdKind_FocusPanel);
              }
              
              // rjf: double-click -> begin editing
              if(ui_double_clicked(sig) && !mv->txt_editing)
              {
                ui_kill_action();
                mv->txt_editing = 1;
                mv->txt_size = Min(sizeof(mv->txt_buffer), dbgi_path.size);
                MemoryCopy(mv->txt_buffer, dbgi_path.str, mv->txt_size);
                mv->txt_cursor = txt_pt(1, 1+mv->txt_size);
                mv->txt_mark = txt_pt(1, 1);
              }
              
              // rjf: store commit info
              if(txt_is_selected && edit_commit)
              {
                commit_module = entity;
              }
              
              // rjf: build browse button
              UI_FocusHot(brw_is_selected ? UI_FocusKind_On : UI_FocusKind_Off) UI_PrefWidth(ui_text_dim(10, 1))
              {
                if(ui_clicked(ui_buttonf("Browse...")) || (brw_is_selected && edit_begin))
                {
                  rd_cmd(RD_CmdKind_RunCommand, .string = rd_cmd_kind_info_table[RD_CmdKind_PickFile].string);
                  mv->pick_file_dst_entity = rd_handle_from_entity(entity);
                }
              }
            }
          }break;
        }
      }
    }
    cursor = next_cursor;
  }
  
  //- rjf: apply commits
  if(edit_commit)
  {
    mv->txt_editing = 0;
    if(!rd_entity_is_nil(commit_module))
    {
      String8 exe_path = commit_module->string;
      String8 dbg_path = str8(mv->txt_buffer, mv->txt_size);
      // TODO(rjf)
    }
    if(edit_submit)
    {
      cursor.y += 1;
    }
  }
  
  //- rjf: apply edit state changes
  if(edit_end)
  {
    mv->txt_editing = 0;
  }
  
  //- rjf: selected num -> selected entity
  mv->selected_column = cursor.x;
  mv->selected_entity = (1 <= cursor.y && cursor.y <= items.count) ? rd_handle_from_entity(items.v[cursor.y-1].entity) : rd_handle_zero();
  
  di_scope_close(scope);
  scratch_end(scratch);
  ProfEnd();
}
#endif

////////////////////////////////
//~ rjf: watch @view_hook_impl

RD_VIEW_RULE_UI_FUNCTION_DEF(watch)
{
  ProfBeginFunction();
  RD_WatchViewState *wv = rd_view_state(RD_WatchViewState);
  if(!wv->initialized)
  {
    rd_watch_view_init(wv);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Expr,      0.25f);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Value,     0.3f);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Type,      0.15f);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_ViewRule,  0.30f);
  }
  rd_watch_view_build(wv, 0, str8_lit("watches"), str8_lit(""), 1, 10, rect);
  ProfEnd();
}

////////////////////////////////
//~ rjf: locals @view_hook_impl

RD_VIEW_RULE_UI_FUNCTION_DEF(locals)
{
  ProfBeginFunction();
  RD_WatchViewState *wv = rd_view_state(RD_WatchViewState);
  if(!wv->initialized)
  {
    rd_watch_view_init(wv);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Expr,      0.25f);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Value,     0.3f);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Type,      0.15f);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_ViewRule,  0.30f);
  }
  rd_watch_view_build(wv, 0, str8_lit("locals"), str8_lit(""), 0, 10, rect);
  ProfEnd();
}

////////////////////////////////
//~ rjf: registers @view_hook_impl

RD_VIEW_RULE_UI_FUNCTION_DEF(registers)
{
  ProfBeginFunction();
  RD_WatchViewState *wv = rd_view_state(RD_WatchViewState);
  if(!wv->initialized)
  {
    rd_watch_view_init(wv);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Expr,      0.25f);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Value,     0.3f);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Type,      0.15f);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_ViewRule,  0.30f);
  }
  rd_watch_view_build(wv, 0, str8_lit("registers"), str8_lit("hex"), 0, 16, rect);
  ProfEnd();
}

////////////////////////////////
//~ rjf: globals @view_hook_impl

RD_VIEW_RULE_UI_FUNCTION_DEF(globals)
{
  ProfBeginFunction();
  RD_WatchViewState *wv = rd_view_state(RD_WatchViewState);
  if(!wv->initialized)
  {
    rd_watch_view_init(wv);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Expr,      0.25f);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Value,     0.3f);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Type,      0.15f);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_ViewRule,  0.30f);
  }
  rd_watch_view_build(wv, 0, str8_lit("globals"), str8_lit(""), 0, 10, rect);
  ProfEnd();
}

////////////////////////////////
//~ rjf: thread_locals @view_hook_impl

RD_VIEW_RULE_UI_FUNCTION_DEF(thread_locals)
{
  ProfBeginFunction();
  RD_WatchViewState *wv = rd_view_state(RD_WatchViewState);
  if(!wv->initialized)
  {
    rd_watch_view_init(wv);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Expr,      0.25f);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Value,     0.3f);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Type,      0.15f);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_ViewRule,  0.30f);
  }
  rd_watch_view_build(wv, 0, str8_lit("thread_locals"), str8_lit(""), 0, 10, rect);
  ProfEnd();
}

////////////////////////////////
//~ rjf: types @view_hook_impl

RD_VIEW_RULE_UI_FUNCTION_DEF(types)
{
  ProfBeginFunction();
  RD_WatchViewState *wv = rd_view_state(RD_WatchViewState);
  if(!wv->initialized)
  {
    rd_watch_view_init(wv);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Expr,      0.25f);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Value,     0.3f);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Type,      0.15f);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_ViewRule,  0.30f);
  }
  rd_watch_view_build(wv, 0, str8_lit("types"), str8_lit(""), 0, 10, rect);
  ProfEnd();
}

////////////////////////////////
//~ rjf: procedures @view_hook_impl

RD_VIEW_RULE_UI_FUNCTION_DEF(procedures)
{
  ProfBeginFunction();
  RD_WatchViewState *wv = rd_view_state(RD_WatchViewState);
  if(!wv->initialized)
  {
    rd_watch_view_init(wv);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Expr,      0.2f);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_Value,     0.6f);
    rd_watch_view_column_alloc(wv, RD_WatchViewColumnKind_ViewRule,  0.2f);
  }
  rd_watch_view_build(wv, 0, str8_lit("procedures"), str8_lit(""), 0, 10, rect);
  ProfEnd();
}

////////////////////////////////
//~ rjf: pending_file @view_hook_impl

typedef struct RD_PendingFileViewState RD_PendingFileViewState;
struct RD_PendingFileViewState
{
  Arena *deferred_cmd_arena;
  RD_CmdList deferred_cmds;
};

RD_VIEW_RULE_UI_FUNCTION_DEF(pending_file)
{
  Temp scratch = scratch_begin(0, 0);
  RD_PendingFileViewState *pves = rd_view_state(RD_PendingFileViewState);
  if(pves->deferred_cmd_arena == 0)
  {
    pves->deferred_cmd_arena = rd_push_view_arena();
  }
  rd_store_view_loading_info(1, 0, 0);
  
  //////////////////////////////
  //- rjf: process commands
  //
  for(RD_Cmd *cmd = 0; rd_next_cmd(&cmd);)
  {
    // rjf: mismatched window/panel => skip
    if(!rd_handle_match(rd_regs()->view, cmd->regs->view))
    {
      continue;
    }
    
    // rjf: process
    RD_CmdKind kind = rd_cmd_kind_from_string(cmd->name);
    switch(kind)
    {
      default:break;
      
      // rjf: gather deferred commands to redispatch when entity is ready
      case RD_CmdKind_GoToLine:
      case RD_CmdKind_GoToAddress:
      case RD_CmdKind_CenterCursor:
      case RD_CmdKind_ContainCursor:
      {
        rd_cmd_list_push_new(pves->deferred_cmd_arena, &pves->deferred_cmds, cmd->name, cmd->regs);
      }break;
    }
  }
  
  //- rjf: determine if file is ready, and which viewer to use
  String8 expr_string = rd_view_expr_string();
  String8 file_path = rd_file_path_from_eval_string(scratch.arena, expr_string);
  Rng1U64 file_range = r1u64(0, 1024);
  U128 file_hash = fs_hash_from_path_range(file_path, file_range, 0);
  B32 file_is_ready = 0;
  RD_ViewRuleKind viewer_kind = RD_ViewRuleKind_Text;
  {
    HS_Scope *hs_scope = hs_scope_open();
    String8 data = hs_data_from_hash(hs_scope, file_hash);
    if(data.size != 0)
    {
      U64 num_utf8_bytes = 0;
      U64 num_unknown_bytes = 0;
      for(U64 idx = 0; idx < data.size && idx < file_range.max;)
      {
        UnicodeDecode decode = utf8_decode(data.str+idx, data.size-idx);
        if(decode.codepoint != max_U32 && (decode.inc > 1 ||
                                           (10 <= decode.codepoint && decode.codepoint <= 13) ||
                                           (32 <= decode.codepoint && decode.codepoint <= 126)))
        {
          num_utf8_bytes += decode.inc;
          idx += decode.inc;
        }
        else
        {
          num_unknown_bytes += 1;
          idx += 1;
        }
      }
      file_is_ready = 1;
      if(num_utf8_bytes > num_unknown_bytes*4)
      {
        viewer_kind = RD_ViewRuleKind_Text;
      }
      else
      {
        viewer_kind = RD_ViewRuleKind_Memory;
      }
    }
    hs_scope_close(hs_scope);
  }
  
  //- rjf: if file is ready, dispatch all deferred commands
  if(file_is_ready)
  {
    for(RD_CmdNode *cmd_node = pves->deferred_cmds.first; cmd_node != 0; cmd_node = cmd_node->next)
    {
      RD_Cmd *cmd = &cmd_node->cmd;
      rd_push_cmd(cmd->name, cmd->regs);
    }
    arena_clear(pves->deferred_cmd_arena);
    MemoryZeroStruct(&pves->deferred_cmds);
  }
  
  //- rjf: if file is ready, move params tree to scratch for new command
  MD_Node *params_copy = &md_nil_node;
  if(file_is_ready)
  {
    params_copy = md_tree_copy(scratch.arena, params);
  }
  
  //- rjf: if file is ready, replace this view with the correct one, if any viewer is specified
  if(file_is_ready && viewer_kind != RD_ViewRuleKind_Null)
  {
    RD_ViewRuleInfo *view_rule_info = rd_view_rule_info_from_kind(viewer_kind);
    String8 query = rd_eval_string_from_file_path(scratch.arena, file_path);
    RD_View *view = rd_view_from_handle(rd_regs()->view);
    rd_view_equip_spec(view, view_rule_info, query, params_copy);
  }
  
  //- rjf: if entity is ready, but we have no viewer for it, then just close this tab
  if(file_is_ready && viewer_kind == RD_ViewRuleKind_Null)
  {
    rd_cmd(RD_CmdKind_CloseTab);
  }
  
  scratch_end(scratch);
}

////////////////////////////////
//~ rjf: text @view_hook_impl

EV_VIEW_RULE_BLOCK_PROD_FUNCTION_DEF(text)
{
  EV_Block *vb = ev_block_begin(arena, EV_BlockKind_Canvas, key, ev_key_make(ev_hash_from_key(key), 1), depth);
  vb->string             = string;
  vb->expr               = expr;
  vb->visual_idx_range   = r1u64(0, 8);
  vb->semantic_idx_range = r1u64(0, 1);
  vb->view_rules         = view_rules;
  ev_block_end(out, vb);
}

RD_VIEW_RULE_UI_FUNCTION_DEF(text)
{
  RD_CodeViewState *cv = rd_view_state(RD_CodeViewState);
  rd_code_view_init(cv);
  Temp scratch = scratch_begin(0, 0);
  HS_Scope *hs_scope = hs_scope_open();
  TXT_Scope *txt_scope = txt_scope_open();
  
  //////////////////////////////
  //- rjf: set up invariants
  //
  F32 bottom_bar_height = ui_top_font_size()*2.f;
  Rng2F32 code_area_rect = r2f32p(rect.x0, rect.y0, rect.x1, rect.y1 - bottom_bar_height);
  Rng2F32 bottom_bar_rect = r2f32p(rect.x0, rect.y1 - bottom_bar_height, rect.x1, rect.y1);
  
  //////////////////////////////
  //- rjf: process code-file commands
  //
  for(RD_Cmd *cmd = 0; rd_next_cmd(&cmd);)
  {
    // rjf: mismatched window/panel => skip
    if(!rd_handle_match(rd_regs()->view, cmd->regs->view))
    {
      continue;
    }
    
    // rjf: process
    RD_CmdKind kind = rd_cmd_kind_from_string(cmd->name);
    switch(kind)
    {
      default:{}break;
      
      // rjf: override file picking
      case RD_CmdKind_PickFile:
      {
        String8 src = rd_file_path_from_eval_string(scratch.arena, rd_view_expr_string());
        String8 dst = cmd->regs->file_path;
        if(src.size != 0 && dst.size != 0)
        {
          // rjf: record src -> dst mapping
          rd_cmd(RD_CmdKind_SetFileReplacementPath, .string = src, .file_path = dst);
          
          // rjf: switch this view to viewing replacement file
          rd_store_view_expr_string(rd_eval_string_from_file_path(scratch.arena, dst));
        }
      }break;
    }
  }
  
  //////////////////////////////
  //- rjf: unpack parameterization info
  //
  rd_regs()->cursor.line = rd_value_from_params_key(params, str8_lit("cursor_line")).s64;
  rd_regs()->cursor.column = rd_value_from_params_key(params, str8_lit("cursor_column")).s64;
  rd_regs()->mark.line = rd_value_from_params_key(params, str8_lit("mark_line")).s64;
  rd_regs()->mark.column = rd_value_from_params_key(params, str8_lit("mark_column")).s64;
  String8 path = rd_file_path_from_eval_string(scratch.arena, string);
  E_Eval eval = e_eval_from_string(scratch.arena, string);
  Rng1U64 range = rd_range_from_eval_params(eval, params);
  rd_regs()->text_key = rd_key_from_eval_space_range(eval.space, range, 1);
  rd_regs()->lang_kind = rd_lang_kind_from_eval_params(eval, params);
  U128 hash = {0};
  TXT_TextInfo info = txt_text_info_from_key_lang(txt_scope, rd_regs()->text_key, rd_regs()->lang_kind, &hash);
  String8 data = hs_data_from_hash(hs_scope, hash);
  B32 file_is_missing = (path.size != 0 && os_properties_from_file_path(path).modified == 0);
  B32 key_has_data = !u128_match(hash, u128_zero()) && info.lines_count;
  
  //////////////////////////////
  //- rjf: build missing file interface
  //
  if(file_is_missing && !key_has_data)
  {
    UI_WidthFill UI_HeightFill UI_Column UI_Padding(ui_pct(1, 0))
    {
      Temp scratch = scratch_begin(0, 0);
      UI_PrefWidth(ui_children_sum(1)) UI_PrefHeight(ui_em(3, 1))
        UI_Row UI_Padding(ui_pct(1, 0))
        UI_PrefWidth(ui_text_dim(10, 1))
        UI_Palette(ui_build_palette(ui_top_palette(), .text = rd_rgba_from_theme_color(RD_ThemeColor_TextNegative)))
      {
        RD_Font(RD_FontSlot_Icons) ui_label(rd_icon_kind_text_table[RD_IconKind_WarningBig]);
        ui_labelf("Could not find \"%S\".", path);
      }
      UI_PrefHeight(ui_em(3, 1))
        UI_Row UI_Padding(ui_pct(1, 0))
        UI_PrefWidth(ui_text_dim(10, 1))
        UI_CornerRadius(ui_top_font_size()/3)
        UI_PrefWidth(ui_text_dim(10, 1))
        UI_Focus(UI_FocusKind_On)
        RD_Palette(RD_PaletteCode_NeutralPopButton)
        UI_TextAlignment(UI_TextAlign_Center)
        if(ui_clicked(ui_buttonf("Find alternative...")))
      {
        rd_cmd(RD_CmdKind_RunCommand, .string = rd_cmd_kind_info_table[RD_CmdKind_PickFile].string);
      }
      scratch_end(scratch);
    }
  }
  
  //////////////////////////////
  //- rjf: code is not missing, but not ready -> equip loading info to this view
  //
  if(!file_is_missing && info.lines_count == 0 && eval.msgs.max_kind == E_MsgKind_Null)
  {
    rd_store_view_loading_info(1, info.bytes_processed, info.bytes_to_process);
  }
  
  //////////////////////////////
  //- rjf: build code contents
  //
  DI_KeyList dbgi_keys = {0};
  if(!file_is_missing)
  {
    RD_CodeViewBuildResult result = rd_code_view_build(scratch.arena, cv, RD_CodeViewBuildFlag_All, code_area_rect, data, &info, 0, r1u64(0, 0), di_key_zero());
    dbgi_keys = result.dbgi_keys;
  }
  
  //////////////////////////////
  //- rjf: unpack cursor info
  //
  if(path.size != 0)
  {
    rd_regs()->lines = d_lines_from_file_path_line_num(rd_frame_arena(), path, rd_regs()->cursor.line);
  }
  
  //////////////////////////////
  //- rjf: determine if file is out-of-date
  //
  B32 file_is_out_of_date = 0;
  String8 out_of_date_dbgi_name = {0};
  {
    U64 file_timestamp = fs_timestamp_from_path(path);
    if(file_timestamp != 0)
    {
      for(DI_KeyNode *n = dbgi_keys.first; n != 0; n = n->next)
      {
        DI_Key key = n->v;
        if(key.min_timestamp < file_timestamp)
        {
          file_is_out_of_date = 1;
          out_of_date_dbgi_name = str8_skip_last_slash(key.path);
          break;
        }
      }
    }
  }
  
  //////////////////////////////
  //- rjf: build bottom bar
  //
  if(!file_is_missing && key_has_data)
  {
    ui_set_next_rect(shift_2f32(bottom_bar_rect, scale_2f32(rect.p0, -1.f)));
    ui_set_next_flags(UI_BoxFlag_DrawBackground);
    UI_Palette *palette = ui_top_palette();
    if(file_is_out_of_date)
    {
      palette = rd_palette_from_code(RD_PaletteCode_NegativePopButton);
    }
    UI_Palette(palette)
      UI_Row
      UI_TextAlignment(UI_TextAlign_Center)
      UI_PrefWidth(ui_text_dim(10, 1))
      UI_FlagsAdd(UI_BoxFlag_DrawTextWeak)
    {
      if(file_is_out_of_date)
      {
        UI_Box *box = &ui_nil_box;
        UI_Palette(ui_build_palette(ui_top_palette(), .text = rd_rgba_from_theme_color(RD_ThemeColor_TextNegative)))
          RD_Font(RD_FontSlot_Icons)
        {
          box = ui_build_box_from_stringf(UI_BoxFlag_DrawText|UI_BoxFlag_Clickable, "%S###file_ood_warning", rd_icon_kind_text_table[RD_IconKind_WarningBig]);
        }
        UI_Signal sig = ui_signal_from_box(box);
        if(ui_hovering(sig)) UI_Tooltip
        {
          UI_PrefWidth(ui_children_sum(1)) UI_Row UI_PrefWidth(ui_text_dim(1, 1))
          {
            ui_labelf("This file has changed since ", out_of_date_dbgi_name);
            UI_Palette(ui_build_palette(ui_top_palette(), .text = rd_rgba_from_theme_color(RD_ThemeColor_TextNeutral))) ui_label(out_of_date_dbgi_name);
            ui_labelf(" was produced.");
          }
        }
      }
      RD_Font(RD_FontSlot_Code)
      {
        if(path.size != 0)
        {
          ui_label(path);
          ui_spacer(ui_em(1.5f, 1));
        }
        ui_labelf("Line: %I64d, Column: %I64d", rd_regs()->cursor.line, rd_regs()->cursor.column);
        ui_spacer(ui_pct(1, 0));
        ui_labelf("(read only)");
        ui_labelf("%s",
                  info.line_end_kind == TXT_LineEndKind_LF   ? "lf" :
                  info.line_end_kind == TXT_LineEndKind_CRLF ? "crlf" :
                  "bin");
      }
    }
  }
  
  //////////////////////////////
  //- rjf: store params
  //
  rd_store_view_param_s64(str8_lit("cursor_line"), rd_regs()->cursor.line);
  rd_store_view_param_s64(str8_lit("cursor_column"), rd_regs()->cursor.column);
  rd_store_view_param_s64(str8_lit("mark_line"), rd_regs()->mark.line);
  rd_store_view_param_s64(str8_lit("mark_column"), rd_regs()->mark.column);
  
  txt_scope_close(txt_scope);
  hs_scope_close(hs_scope);
  scratch_end(scratch);
}

////////////////////////////////
//~ rjf: disasm @view_hook_impl

typedef struct RD_DisasmViewState RD_DisasmViewState;
struct RD_DisasmViewState
{
  B32 initialized;
  TxtPt cursor;
  TxtPt mark;
  DASM_StyleFlags style_flags;
  U64 goto_vaddr;
  RD_CodeViewState cv;
};

EV_VIEW_RULE_BLOCK_PROD_FUNCTION_DEF(disasm)
{
  EV_Block *vb = ev_block_begin(arena, EV_BlockKind_Canvas, key, ev_key_make(ev_hash_from_key(key), 1), depth);
  vb->string             = string;
  vb->expr               = expr;
  vb->visual_idx_range   = r1u64(0, 8);
  vb->semantic_idx_range = r1u64(0, 1);
  vb->view_rules         = view_rules;
  ev_block_end(out, vb);
}

RD_VIEW_RULE_UI_FUNCTION_DEF(disasm)
{
  RD_DisasmViewState *dv = rd_view_state(RD_DisasmViewState);
  if(dv->initialized == 0)
  {
    dv->initialized = 1;
    dv->cursor = txt_pt(1, 1);
    dv->mark = txt_pt(1, 1);
    dv->style_flags = DASM_StyleFlag_Addresses|DASM_StyleFlag_SourceFilesNames|DASM_StyleFlag_SourceLines|DASM_StyleFlag_SymbolNames;
    rd_code_view_init(&dv->cv);
  }
  RD_CodeViewState *cv = &dv->cv;
  Temp scratch = scratch_begin(0, 0);
  HS_Scope *hs_scope = hs_scope_open();
  DASM_Scope *dasm_scope = dasm_scope_open();
  TXT_Scope *txt_scope = txt_scope_open();
  
  //////////////////////////////
  //- rjf: if disassembly views are not parameterized by anything, they
  // automatically snap to the selected thread's RIP, rounded down to the
  // nearest 16K boundary
  //
  B32 auto_selected_thread = 0;
  if(string.size == 0)
  {
    auto_selected_thread = 1;
    string = str8_lit("(rip.u64 & (~(0x4000 - 1))");
  }
  
  //////////////////////////////
  //- rjf: set up invariants
  //
  F32 bottom_bar_height = ui_top_font_size()*2.f;
  Rng2F32 code_area_rect = r2f32p(rect.x0, rect.y0, rect.x1, rect.y1 - bottom_bar_height);
  Rng2F32 bottom_bar_rect = r2f32p(rect.x0, rect.y1 - bottom_bar_height, rect.x1, rect.y1);
  rd_regs()->cursor = dv->cursor;
  rd_regs()->mark = dv->mark;
  
  //////////////////////////////
  //- rjf: unpack parameterization info
  //
  E_Eval eval = e_eval_from_string(scratch.arena, string);
  E_Space space = eval.space;
  if(auto_selected_thread)
  {
    space = rd_eval_space_from_ctrl_entity(ctrl_entity_from_handle(d_state->ctrl_entity_store, rd_regs()->process), RD_EvalSpaceKind_CtrlEntity);
  }
  Rng1U64 range = rd_range_from_eval_params(eval, params);
  Arch arch = rd_arch_from_eval_params(eval, params);
  CTRL_Entity *space_entity = rd_ctrl_entity_from_eval_space(space);
  CTRL_Entity *dasm_module = &ctrl_entity_nil;
  DI_Key dbgi_key = {0};
  U64 base_vaddr = 0;
  switch(space_entity->kind)
  {
    default:{}break;
    case CTRL_EntityKind_Process:
    {
      arch        = space_entity->arch;
      dasm_module = ctrl_module_from_process_vaddr(space_entity, range.min);
      dbgi_key    = ctrl_dbgi_key_from_module(dasm_module);
      base_vaddr  = dasm_module->vaddr_range.min;
    }break;
  }
  U128 dasm_key = rd_key_from_eval_space_range(space, range, 0);
  U128 dasm_data_hash = {0};
  DASM_Params dasm_params = {0};
  {
    dasm_params.vaddr       = range.min;
    dasm_params.arch        = arch;
    dasm_params.style_flags = dv->style_flags;
    dasm_params.syntax      = DASM_Syntax_Intel;
    dasm_params.base_vaddr  = base_vaddr;
    dasm_params.dbgi_key    = dbgi_key;
  }
  DASM_Info dasm_info = dasm_info_from_key_params(dasm_scope, dasm_key, &dasm_params, &dasm_data_hash);
  rd_regs()->text_key = dasm_info.text_key;
  rd_regs()->lang_kind = txt_lang_kind_from_arch(arch);
  U128 dasm_text_hash = {0};
  TXT_TextInfo dasm_text_info = txt_text_info_from_key_lang(txt_scope, rd_regs()->text_key, rd_regs()->lang_kind, &dasm_text_hash);
  String8 dasm_text_data = hs_data_from_hash(hs_scope, dasm_text_hash);
  B32 has_disasm = (dasm_info.lines.count != 0 && dasm_text_info.lines_count != 0);
  B32 is_loading = (!has_disasm && dim_1u64(range) != 0 && eval.msgs.max_kind == E_MsgKind_Null);
  
  //////////////////////////////
  //- rjf: process disassembly-specific commands
  //
  for(RD_Cmd *cmd = 0; rd_next_cmd(&cmd);)
  {
    // rjf: mismatched window/panel => skip
    if(!rd_handle_match(rd_regs()->view, cmd->regs->view))
    {
      continue;
    }
    
    // rjf: process
    RD_CmdKind kind = rd_cmd_kind_from_string(cmd->name);
    switch(kind)
    {
      default: break;
      case RD_CmdKind_GoToAddress:
      {
        RD_Entity *process = &d_nil_entity;
        {
          RD_Entity *entity = rd_entity_from_handle(cmd->regs->entity);
          if(!rd_entity_is_nil(entity) &&
             (entity->kind == RD_EntityKind_Process ||
              entity->kind == RD_EntityKind_Thread ||
              entity->kind == RD_EntityKind_Module))
          {
            process = entity;
            if(entity->kind == RD_EntityKind_Thread ||
               entity->kind == RD_EntityKind_Module)
            {
              process = rd_entity_ancestor_from_kind(process, RD_EntityKind_Process);
            }
          }
        }
        dv->goto_vaddr = cmd->regs->vaddr;
      }break;
      case RD_CmdKind_ToggleCodeBytesVisibility: {dv->style_flags ^= DASM_StyleFlag_CodeBytes;}break;
      case RD_CmdKind_ToggleAddressVisibility:   {dv->style_flags ^= DASM_StyleFlag_Addresses;}break;
    }
  }
  
  //////////////////////////////
  //- rjf: is loading -> equip view with loading information
  //
  if(is_loading && !d_ctrl_targets_running())
  {
    rd_store_view_loading_info(is_loading, 0, 0);
  }
  
  //////////////////////////////
  //- rjf: do goto vaddr
  //
  if(!is_loading && has_disasm && dv->goto_vaddr != 0)
  {
    U64 vaddr = dv->goto_vaddr;
    dv->goto_vaddr = 0;
    U64 line_idx = dasm_line_array_idx_from_code_off__linear_scan(&dasm_info.lines, vaddr-range.min);
    S64 line_num = (S64)(line_idx+1);
    cv->goto_line_num = line_num;
  }
  
  //////////////////////////////
  //- rjf: build code contents
  //
  if(!is_loading && has_disasm)
  {
    rd_code_view_build(scratch.arena, cv, RD_CodeViewBuildFlag_All, code_area_rect, dasm_text_data, &dasm_text_info, &dasm_info.lines, range, dbgi_key);
  }
  
  //////////////////////////////
  //- rjf: unpack cursor info
  //
  if(!is_loading && has_disasm)
  {
    U64 off = dasm_line_array_code_off_from_idx(&dasm_info.lines, rd_regs()->cursor.line-1);
    rd_regs()->vaddr_range = r1u64(base_vaddr+off, base_vaddr+off);
    rd_regs()->voff_range = ctrl_voff_range_from_vaddr_range(dasm_module, rd_regs()->vaddr_range);
    rd_regs()->lines = d_lines_from_dbgi_key_voff(rd_frame_arena(), &dbgi_key, rd_regs()->voff_range.min);
  }
  
  //////////////////////////////
  //- rjf: build bottom bar
  //
  if(!is_loading && has_disasm)
  {
    ui_set_next_rect(shift_2f32(bottom_bar_rect, scale_2f32(rect.p0, -1.f)));
    ui_set_next_flags(UI_BoxFlag_DrawBackground);
    UI_Row
      UI_TextAlignment(UI_TextAlign_Center)
      UI_PrefWidth(ui_text_dim(10, 1))
      UI_FlagsAdd(UI_BoxFlag_DrawTextWeak)
      RD_Font(RD_FontSlot_Code)
    {
      U64 cursor_vaddr = (1 <= rd_regs()->cursor.line && rd_regs()->cursor.line <= dasm_info.lines.count) ? (range.min+dasm_info.lines.v[rd_regs()->cursor.line-1].code_off) : 0;
      if(dasm_module != &ctrl_entity_nil)
      {
        ui_labelf("%S", path_normalized_from_string(scratch.arena, dasm_module->string));
        ui_spacer(ui_em(1.5f, 1));
      }
      ui_labelf("Address: 0x%I64x, Line: %I64d, Column: %I64d", cursor_vaddr, rd_regs()->cursor.line, rd_regs()->cursor.column);
      ui_spacer(ui_pct(1, 0));
      ui_labelf("(read only)");
      ui_labelf("bin");
    }
  }
  
  //////////////////////////////
  //- rjf: commit storage
  //
  dv->cursor = rd_regs()->cursor;
  dv->mark = rd_regs()->mark;
  
  txt_scope_close(txt_scope);
  dasm_scope_close(dasm_scope);
  hs_scope_close(hs_scope);
  scratch_end(scratch);
}

////////////////////////////////
//~ rjf: output @view_hook_impl

RD_VIEW_RULE_UI_FUNCTION_DEF(output)
{
  RD_CodeViewState *cv = rd_view_state(RD_CodeViewState);
  rd_code_view_init(cv);
  Temp scratch = scratch_begin(0, 0);
  HS_Scope *hs_scope = hs_scope_open();
  TXT_Scope *txt_scope = txt_scope_open();
  
  //////////////////////////////
  //- rjf: set up invariants
  //
  F32 bottom_bar_height = ui_top_font_size()*2.f;
  Rng2F32 code_area_rect = r2f32p(rect.x0, rect.y0, rect.x1, rect.y1 - bottom_bar_height);
  Rng2F32 bottom_bar_rect = r2f32p(rect.x0, rect.y1 - bottom_bar_height, rect.x1, rect.y1);
  
  //////////////////////////////
  //- rjf: unpack text info
  //
  U128 key = d_state->output_log_key;
  TXT_LangKind lang_kind = TXT_LangKind_Null;
  U128 hash = {0};
  TXT_TextInfo info = txt_text_info_from_key_lang(txt_scope, key, lang_kind, &hash);
  String8 data = hs_data_from_hash(hs_scope, hash);
  Rng1U64 empty_range = {0};
  if(info.lines_count == 0)
  {
    info.lines_count = 1;
    info.lines_ranges = &empty_range;
  }
  
  //////////////////////////////
  //- rjf: build code contents
  //
  {
    rd_code_view_build(scratch.arena, cv, 0, code_area_rect, data, &info, 0, r1u64(0, 0), di_key_zero());
  }
  
  //////////////////////////////
  //- rjf: build bottom bar
  //
  {
    ui_set_next_rect(shift_2f32(bottom_bar_rect, scale_2f32(rect.p0, -1.f)));
    ui_set_next_flags(UI_BoxFlag_DrawBackground);
    UI_Row
      UI_TextAlignment(UI_TextAlign_Center)
      UI_PrefWidth(ui_text_dim(10, 1))
      UI_FlagsAdd(UI_BoxFlag_DrawTextWeak)
      RD_Font(RD_FontSlot_Code)
    {
      ui_labelf("(Debug String Output)");
      ui_spacer(ui_em(1.5f, 1));
      ui_labelf("Line: %I64d, Column: %I64d", rd_regs()->cursor.line, rd_regs()->cursor.column);
      ui_spacer(ui_pct(1, 0));
      ui_labelf("(read only)");
    }
  }
  
  txt_scope_close(txt_scope);
  hs_scope_close(hs_scope);
  scratch_end(scratch);
}

////////////////////////////////
//~ rjf: memory @view_hook_impl

typedef struct RD_MemoryViewState RD_MemoryViewState;
struct RD_MemoryViewState
{
  B32 center_cursor;
  B32 contain_cursor;
};

EV_VIEW_RULE_BLOCK_PROD_FUNCTION_DEF(memory)
{
  EV_Block *vb = ev_block_begin(arena, EV_BlockKind_Canvas, key, ev_key_make(ev_hash_from_key(key), 1), depth);
  vb->string             = string;
  vb->expr               = expr;
  vb->visual_idx_range   = r1u64(0, 16);
  vb->semantic_idx_range = r1u64(0, 1);
  vb->view_rules         = view_rules;
  ev_block_end(out, vb);
}

RD_VIEW_RULE_UI_FUNCTION_DEF(memory)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(0, 0);
  HS_Scope *hs_scope = hs_scope_open();
  RD_MemoryViewState *mv = rd_view_state(RD_MemoryViewState);
  
  //////////////////////////////
  //- rjf: unpack parameterization info
  //
  E_Eval eval = e_eval_from_string(scratch.arena, string);
  Rng1U64 space_range = rd_range_from_eval_params(eval, params);
  if(eval.space.kind == 0)
  {
    eval.space = rd_eval_space_from_ctrl_entity(ctrl_entity_from_handle(d_state->ctrl_entity_store, rd_regs()->process), RD_EvalSpaceKind_CtrlEntity);
    space_range = rd_whole_range_from_eval_space(eval.space);
  }
  U64 cursor          = rd_value_from_params_key(params, str8_lit("cursor_vaddr")).u64;
  U64 mark            = rd_value_from_params_key(params, str8_lit("mark_vaddr")).u64;
  U64 bytes_per_cell  = rd_value_from_params_key(params, str8_lit("bytes_per_cell")).u64;
  U64 num_columns     = rd_value_from_params_key(params, str8_lit("num_columns")).u64;
  if(num_columns == 0)
  {
    num_columns = 16;
  }
  num_columns = ClampBot(1, num_columns);
  bytes_per_cell = ClampBot(1, bytes_per_cell);
  UI_ScrollPt2 scroll_pos = rd_view_scroll_pos();
  
  //////////////////////////////
  //- rjf: process commands
  //
  for(RD_Cmd *cmd = 0; rd_next_cmd(&cmd);)
  {
    // rjf: mismatched window/panel => skip
    if(!rd_handle_match(rd_regs()->view, cmd->regs->view))
    {
      continue;
    }
    
    // rjf: process
    RD_CmdKind kind = rd_cmd_kind_from_string(cmd->name);
    switch(kind)
    {
      default: break;
      case RD_CmdKind_CenterCursor:
      {
        mv->center_cursor = 1;
      }break;
      case RD_CmdKind_ContainCursor:
      {
        mv->contain_cursor = 1;
      }break;
      case RD_CmdKind_GoToAddress:
      {
        // NOTE(rjf): go-to-address occurs with disassembly snaps, and we don't
        // generally want to respond to those in thise view, so just skip any
        // go-to-address commands that haven't been *explicitly* parameterized
        // with this view.
      }break;
      case RD_CmdKind_SetColumns:
      {
        // TODO(rjf)
      }break;
    }
  }
  
  //////////////////////////////
  //- rjf: unpack visual params
  //
  FNT_Tag font = rd_font_from_slot(RD_FontSlot_Code);
  F32 font_size = rd_font_size_from_slot(RD_FontSlot_Code);
  F32 big_glyph_advance = fnt_dim_from_tag_size_string(font, font_size, 0, 0, str8_lit("H")).x;
  F32 row_height_px = floor_f32(font_size*2.f);
  F32 cell_width_px = floor_f32(font_size*2.f * bytes_per_cell);
  F32 scroll_bar_dim = floor_f32(ui_top_font_size()*1.5f);
  Vec2F32 panel_dim = dim_2f32(rect);
  F32 footer_dim = font_size*10.f;
  Rng2F32 header_rect = r2f32p(0, 0, panel_dim.x, row_height_px);
  Rng2F32 footer_rect = r2f32p(0, panel_dim.y-footer_dim, panel_dim.x, panel_dim.y);
  Rng2F32 content_rect = r2f32p(0, row_height_px, panel_dim.x-scroll_bar_dim, footer_rect.y0);
  
  //////////////////////////////
  //- rjf: determine legal scroll range
  //
  Rng1S64 scroll_idx_rng = r1s64(0, space_range.max/num_columns);
  
  //////////////////////////////
  //- rjf: determine info about visible range of rows
  //
  Rng1S64 viz_range_rows = {0};
  Rng1U64 viz_range_bytes = {0};
  S64 num_possible_visible_rows = 0;
  {
    num_possible_visible_rows = dim_2f32(content_rect).y/row_height_px;
    viz_range_rows.min = scroll_pos.y.idx + (S64)scroll_pos.y.off - !!(scroll_pos.y.off<0);
    viz_range_rows.max = scroll_pos.y.idx + (S64)scroll_pos.y.off + num_possible_visible_rows,
    viz_range_rows.min = clamp_1s64(scroll_idx_rng, viz_range_rows.min);
    viz_range_rows.max = clamp_1s64(scroll_idx_rng, viz_range_rows.max);
    viz_range_bytes.min = viz_range_rows.min*num_columns;
    viz_range_bytes.max = (viz_range_rows.max+1)*num_columns+1;
    if(viz_range_bytes.min > viz_range_bytes.max)
    {
      Swap(U64, viz_range_bytes.min, viz_range_bytes.max);
    }
  }
  
  //////////////////////////////
  //- rjf: take keyboard controls
  //
  UI_Focus(UI_FocusKind_On) if(ui_is_focus_active())
  {
    U64 next_cursor = cursor;
    U64 next_mark = mark;
    for(UI_Event *evt = 0; ui_next_event(&evt);)
    {
      Vec2S64 cell_delta = {0};
      switch(evt->delta_unit)
      {
        default:{}break;
        case UI_EventDeltaUnit_Char:
        {
          cell_delta.x = (S64)evt->delta_2s32.x;
          cell_delta.y = (S64)evt->delta_2s32.y;
        }break;
        case UI_EventDeltaUnit_Word:
        case UI_EventDeltaUnit_Page:
        {
          if(evt->delta_2s32.x < 0)
          {
            cell_delta.x = -(S64)(cursor%num_columns);
          }
          else if(evt->delta_2s32.x > 0)
          {
            cell_delta.x = (num_columns-1) - (S64)(cursor%num_columns);
          }
          if(evt->delta_2s32.y < 0)
          {
            cell_delta.y = -4;
          }
          else if(evt->delta_2s32.y > 0)
          {
            cell_delta.y = +4;
          }
        }break;
      }
      B32 good_action = 0;
      if(evt->delta_2s32.x != 0 || evt->delta_2s32.y != 0)
      {
        good_action = 1;
      }
      if(good_action && evt->flags & UI_EventFlag_ZeroDeltaOnSelect && cursor != mark)
      {
        MemoryZeroStruct(&cell_delta);
      }
      if(good_action)
      {
        cell_delta.x = ClampBot(cell_delta.x, (S64)-next_cursor);
        cell_delta.y = ClampBot(cell_delta.y, (S64)-(next_cursor/num_columns));
        next_cursor += cell_delta.x;
        next_cursor += cell_delta.y*num_columns;
        next_cursor = ClampTop(0x7FFFFFFFFFFFull, next_cursor);
      }
      if(good_action && evt->flags & UI_EventFlag_PickSelectSide && cursor != mark)
      {
        if(evt->delta_2s32.x < 0 || evt->delta_2s32.y < 0)
        {
          next_cursor = Min(cursor, mark);
        }
        else
        {
          next_cursor = Max(cursor, mark);
        }
      }
      if(good_action && !(evt->flags & UI_EventFlag_KeepMark))
      {
        next_mark = next_cursor;
      }
      if(good_action)
      {
        mv->contain_cursor = 1;
        ui_eat_event(evt);
      }
    }
    cursor = next_cursor;
    mark = next_mark;
  }
  
  //////////////////////////////
  //- rjf: clamp cursor
  //
  {
    Rng1U64 cursor_valid_rng = space_range;
    cursor = clamp_1u64(cursor_valid_rng, cursor);
    mark = clamp_1u64(cursor_valid_rng, mark);
  }
  
  //////////////////////////////
  //- rjf: center cursor
  //
  if(mv->center_cursor)
  {
    mv->center_cursor = 0;
    S64 cursor_row_idx = cursor/num_columns;
    S64 new_idx = (cursor_row_idx-num_possible_visible_rows/2+1);
    new_idx = clamp_1s64(scroll_idx_rng, new_idx);
    ui_scroll_pt_target_idx(&scroll_pos.y, new_idx);
  }
  
  //////////////////////////////
  //- rjf: contain cursor
  //
  if(mv->contain_cursor)
  {
    mv->contain_cursor = 0;
    S64 cursor_row_idx = cursor/num_columns;
    Rng1S64 cursor_viz_range = r1s64(clamp_1s64(scroll_idx_rng, cursor_row_idx-2), clamp_1s64(scroll_idx_rng, cursor_row_idx+3));
    S64 min_delta = Min(0, cursor_viz_range.min-viz_range_rows.min);
    S64 max_delta = Max(0, cursor_viz_range.max-viz_range_rows.max);
    S64 new_idx = scroll_pos.y.idx+min_delta+max_delta;
    new_idx = clamp_1s64(scroll_idx_rng, new_idx);
    ui_scroll_pt_target_idx(&scroll_pos.y, new_idx);
  }
  
  //////////////////////////////
  //- rjf: produce fancy string runs for all possible byte values in all cells
  //
  DR_FancyStringList byte_fancy_strings[256] = {0};
  {
    Vec4F32 full_color = rd_rgba_from_theme_color(RD_ThemeColor_TextPositive);
    Vec4F32 zero_color = rd_rgba_from_theme_color(RD_ThemeColor_TextWeak);
    for(U64 idx = 0; idx < ArrayCount(byte_fancy_strings); idx += 1)
    {
      U8 byte = (U8)idx;
      F32 pct = (byte/255.f);
      Vec4F32 text_color = mix_4f32(zero_color, full_color, pct);
      if(byte == 0)
      {
        text_color.w *= 0.5f;
      }
      DR_FancyString fstr = {font, push_str8f(scratch.arena, "%02x", byte), text_color, font_size, 0, 0};
      dr_fancy_string_list_push(scratch.arena, &byte_fancy_strings[idx], &fstr);
    }
  }
  
  //////////////////////////////
  //- rjf: grab windowed memory
  //
  U64 visible_memory_size = dim_1u64(viz_range_bytes);
  U8 *visible_memory = push_array(scratch.arena, U8, visible_memory_size);
  {
    e_space_read(eval.space, visible_memory, viz_range_bytes);
  }
  
  //////////////////////////////
  //- rjf: grab annotations for windowed range of memory
  //
  typedef struct Annotation Annotation;
  struct Annotation
  {
    Annotation *next;
    String8 name_string;
    String8 kind_string;
    String8 type_string;
    Vec4F32 color;
    Rng1U64 vaddr_range;
  };
  typedef struct AnnotationList AnnotationList;
  struct AnnotationList
  {
    Annotation *first;
    Annotation *last;
  };
  AnnotationList *visible_memory_annotations = push_array(scratch.arena, AnnotationList, visible_memory_size);
  {
    CTRL_Entity *thread = ctrl_entity_from_handle(d_state->ctrl_entity_store, rd_regs()->thread);
    CTRL_Entity *process = ctrl_entity_ancestor_from_kind(thread, CTRL_EntityKind_Process);
    CTRL_Unwind unwind = d_query_cached_unwind_from_thread(thread);
    
    //- rjf: fill unwind frame annotations
    if(unwind.frames.count != 0)
    {
      U64 last_stack_top = regs_rsp_from_arch_block(thread->arch, unwind.frames.v[0].regs);
      for(U64 idx = 1; idx < unwind.frames.count; idx += 1)
      {
        CTRL_UnwindFrame *f = &unwind.frames.v[idx];
        U64 f_stack_top = regs_rsp_from_arch_block(thread->arch, f->regs);
        Rng1U64 frame_vaddr_range = r1u64(last_stack_top, f_stack_top);
        Rng1U64 frame_vaddr_range_in_viz = intersect_1u64(frame_vaddr_range, viz_range_bytes);
        last_stack_top = f_stack_top;
        if(dim_1u64(frame_vaddr_range_in_viz) != 0)
        {
          U64 f_rip = regs_rip_from_arch_block(thread->arch, f->regs);
          CTRL_Entity *module = ctrl_module_from_process_vaddr(process, f_rip);
          DI_Key dbgi_key = ctrl_dbgi_key_from_module(module);
          U64 rip_voff = ctrl_voff_from_vaddr(module, f_rip);
          String8 symbol_name = d_symbol_name_from_dbgi_key_voff(scratch.arena, &dbgi_key, rip_voff, 1);
          Annotation *annotation = push_array(scratch.arena, Annotation, 1);
          annotation->name_string = symbol_name.size != 0 ? symbol_name : str8_lit("[external code]");
          annotation->kind_string = str8_lit("Call Stack Frame");
          annotation->color = symbol_name.size != 0 ? rd_rgba_from_theme_color(RD_ThemeColor_CodeSymbol) : rd_rgba_from_theme_color(RD_ThemeColor_TextWeak);
          annotation->vaddr_range = frame_vaddr_range;
          for(U64 vaddr = frame_vaddr_range_in_viz.min; vaddr < frame_vaddr_range_in_viz.max; vaddr += 1)
          {
            U64 visible_byte_idx = vaddr - viz_range_bytes.min;
            SLLQueuePush(visible_memory_annotations[visible_byte_idx].first, visible_memory_annotations[visible_byte_idx].last, annotation);
          }
        }
      }
    }
    
    //- rjf: fill selected thread stack range annotation
    if(unwind.frames.count > 0)
    {
      U64 stack_base_vaddr = thread->stack_base;
      U64 stack_top_vaddr = regs_rsp_from_arch_block(thread->arch, unwind.frames.v[0].regs);
      Rng1U64 stack_vaddr_range = r1u64(stack_base_vaddr, stack_top_vaddr);
      Rng1U64 stack_vaddr_range_in_viz = intersect_1u64(stack_vaddr_range, viz_range_bytes);
      if(dim_1u64(stack_vaddr_range_in_viz) != 0)
      {
        Annotation *annotation = push_array(scratch.arena, Annotation, 1);
        annotation->name_string = thread->string.size ? thread->string : push_str8f(scratch.arena, "TID: %I64u", thread->id);
        annotation->kind_string = str8_lit("Stack");
        annotation->color = rd_rgba_from_ctrl_entity(thread);
        annotation->vaddr_range = stack_vaddr_range;
        for(U64 vaddr = stack_vaddr_range_in_viz.min; vaddr < stack_vaddr_range_in_viz.max; vaddr += 1)
        {
          U64 visible_byte_idx = vaddr - viz_range_bytes.min;
          SLLQueuePush(visible_memory_annotations[visible_byte_idx].first, visible_memory_annotations[visible_byte_idx].last, annotation);
        }
      }
    }
    
    //- rjf: fill local variable annotations
    {
      DI_Scope *scope = di_scope_open();
      Vec4F32 color_gen_table[] =
      {
        rd_rgba_from_theme_color(RD_ThemeColor_Thread0),
        rd_rgba_from_theme_color(RD_ThemeColor_Thread1),
        rd_rgba_from_theme_color(RD_ThemeColor_Thread2),
        rd_rgba_from_theme_color(RD_ThemeColor_Thread3),
        rd_rgba_from_theme_color(RD_ThemeColor_Thread4),
        rd_rgba_from_theme_color(RD_ThemeColor_Thread5),
        rd_rgba_from_theme_color(RD_ThemeColor_Thread6),
        rd_rgba_from_theme_color(RD_ThemeColor_Thread7),
      };
      U64 thread_rip_vaddr = d_query_cached_rip_from_thread_unwind(thread, rd_regs()->unwind_count);
      for(E_String2NumMapNode *n = e_parse_ctx->locals_map->first; n != 0; n = n->order_next)
      {
        String8 local_name = n->string;
        E_Eval local_eval = e_eval_from_string(scratch.arena, local_name);
        if(local_eval.mode == E_Mode_Offset)
        {
          E_TypeKind local_eval_type_kind = e_type_kind_from_key(local_eval.type_key);
          U64 local_eval_type_size = e_type_byte_size_from_key(local_eval.type_key);
          Rng1U64 vaddr_rng = r1u64(local_eval.value.u64, local_eval.value.u64+local_eval_type_size);
          Rng1U64 vaddr_rng_in_visible = intersect_1u64(viz_range_bytes, vaddr_rng);
          if(vaddr_rng_in_visible.max != vaddr_rng_in_visible.min)
          {
            Annotation *annotation = push_array(scratch.arena, Annotation, 1);
            {
              annotation->name_string = push_str8_copy(scratch.arena, local_name);
              annotation->kind_string = str8_lit("Local");
              annotation->type_string = e_type_string_from_key(scratch.arena, local_eval.type_key);
              annotation->color = color_gen_table[(vaddr_rng.min/8)%ArrayCount(color_gen_table)];
              annotation->vaddr_range = vaddr_rng;
            }
            for(U64 vaddr = vaddr_rng_in_visible.min; vaddr < vaddr_rng_in_visible.max; vaddr += 1)
            {
              SLLQueuePushFront(visible_memory_annotations[vaddr-viz_range_bytes.min].first, visible_memory_annotations[vaddr-viz_range_bytes.min].last, annotation);
            }
          }
        }
      }
      di_scope_close(scope);
    }
  }
  
  //////////////////////////////
  //- rjf: build main container
  //
  UI_Box *container_box = &ui_nil_box;
  {
    Vec2F32 dim = dim_2f32(rect);
    ui_set_next_fixed_width(dim.x);
    ui_set_next_fixed_height(dim.y);
    ui_set_next_child_layout_axis(Axis2_Y);
    container_box = ui_build_box_from_stringf(0, "memory_view_container");
  }
  
  //////////////////////////////
  //- rjf: build header
  //
  UI_Box *header_box = &ui_nil_box;
  UI_Parent(container_box)
  {
    UI_WidthFill UI_PrefHeight(ui_px(row_height_px, 1.f)) UI_Row
      header_box = ui_build_box_from_stringf(UI_BoxFlag_DrawSideBottom, "table_header");
    UI_Parent(header_box)
      RD_Font(RD_FontSlot_Code)
      UI_FontSize(font_size)
      UI_FlagsAdd(UI_BoxFlag_DrawTextWeak)
    {
      UI_PrefWidth(ui_px(big_glyph_advance*18.f, 1.f)) ui_labelf("Address");
      UI_PrefWidth(ui_px(cell_width_px, 1.f))
        UI_TextAlignment(UI_TextAlign_Center)
      {
        Rng1U64 col_selection_rng = r1u64(cursor%num_columns, mark%num_columns);
        for(U64 row_off = 0; row_off < num_columns*bytes_per_cell; row_off += bytes_per_cell)
        {
          if(!(col_selection_rng.min <= row_off && row_off <= col_selection_rng.max))
          {
            ui_set_next_flags(UI_BoxFlag_DrawTextWeak);
          }
          ui_labelf("%I64X", row_off);
        }
      }
      ui_spacer(ui_px(big_glyph_advance*1.5f, 1.f));
      UI_WidthFill ui_labelf("ASCII");
    }
  }
  
  //////////////////////////////
  //- rjf: build scroll bar
  //
  UI_Parent(container_box)
  {
    ui_set_next_fixed_x(content_rect.x1);
    ui_set_next_fixed_y(content_rect.y0);
    ui_set_next_fixed_width(scroll_bar_dim);
    ui_set_next_fixed_height(dim_2f32(content_rect).y);
    {
      scroll_pos.y = ui_scroll_bar(Axis2_Y,
                                   ui_px(scroll_bar_dim, 1.f),
                                   scroll_pos.y,
                                   scroll_idx_rng,
                                   num_possible_visible_rows);
    }
  }
  
  //////////////////////////////
  //- rjf: build scrollable box
  //
  UI_Box *scrollable_box = &ui_nil_box;
  UI_Parent(container_box)
  {
    ui_set_next_fixed_x(content_rect.x0);
    ui_set_next_fixed_y(content_rect.y0);
    ui_set_next_fixed_width(dim_2f32(content_rect).x);
    ui_set_next_fixed_height(dim_2f32(content_rect).y);
    ui_set_next_child_layout_axis(Axis2_Y);
    scrollable_box = ui_build_box_from_stringf(UI_BoxFlag_Clip|
                                               UI_BoxFlag_Scroll|
                                               UI_BoxFlag_AllowOverflowX|
                                               UI_BoxFlag_AllowOverflowY,
                                               "scrollable_box");
    container_box->view_off.x = container_box->view_off_target.x = scroll_pos.x.idx + scroll_pos.x.off;
    scrollable_box->view_off.y = scrollable_box->view_off_target.y = floor_f32(row_height_px*mod_f32(scroll_pos.y.off, 1.f) + row_height_px*(scroll_pos.y.off < 0));
  }
  
  //////////////////////////////
  //- rjf: build row container/overlay
  //
  UI_Box *row_container_box = &ui_nil_box;
  UI_Box *row_overlay_box = &ui_nil_box;
  UI_Parent(scrollable_box) UI_WidthFill UI_HeightFill
  {
    ui_set_next_child_layout_axis(Axis2_Y);
    ui_set_next_hover_cursor(OS_Cursor_IBar);
    row_container_box = ui_build_box_from_stringf(UI_BoxFlag_Clickable, "row_container");
    UI_Parent(row_container_box)
    {
      row_overlay_box = ui_build_box_from_stringf(UI_BoxFlag_Floating, "row_overlay");
    }
  }
  
  //////////////////////////////
  //- rjf: interact with row container
  //
  U64 mouse_hover_byte_num = 0;
  {
    UI_Signal sig = ui_signal_from_box(row_container_box);
    
    // rjf: calculate hovered byte
    if(ui_hovering(sig) || ui_dragging(sig))
    {
      Vec2F32 mouse_rel = sub_2f32(ui_mouse(), row_container_box->rect.p0);
      U64 row_idx = ClampBot(0, mouse_rel.y) / row_height_px;
      
      // rjf: try from cells
      if(mouse_hover_byte_num == 0)
      {
        U64 col_idx = ClampBot(mouse_rel.x-big_glyph_advance*18.f, 0)/cell_width_px;
        if(col_idx < num_columns)
        {
          mouse_hover_byte_num = viz_range_bytes.min + row_idx*num_columns + col_idx + 1;
        }
      }
      
      // rjf: try from ascii
      if(mouse_hover_byte_num == 0)
      {
        U64 col_idx = ClampBot(mouse_rel.x - (big_glyph_advance*18.f + cell_width_px*num_columns + big_glyph_advance*1.5f), 0)/big_glyph_advance;
        col_idx = ClampTop(col_idx, num_columns-1);
        mouse_hover_byte_num = viz_range_bytes.min + row_idx*num_columns + col_idx + 1;
      }
      
      mouse_hover_byte_num = Clamp(1, mouse_hover_byte_num, 0x7FFFFFFFFFFFull+1);
    }
    
    // rjf: press -> focus panel
    if(ui_pressed(sig))
    {
      rd_cmd(RD_CmdKind_FocusPanel);
    }
    
    // rjf: click & drag -> select
    if(ui_dragging(sig) && mouse_hover_byte_num != 0)
    {
      if(!contains_2f32(sig.box->rect, ui_mouse()))
      {
        mv->contain_cursor = 1;
      }
      cursor = mouse_hover_byte_num-1;
      if(ui_pressed(sig))
      {
        mark = cursor;
      }
    }
    
    // rjf: ctrl+scroll -> change font size
    if(ui_hovering(sig))
    {
      for(UI_Event *evt = 0; ui_next_event(&evt);)
      {
        if(evt->kind == UI_EventKind_Scroll && evt->modifiers & OS_Modifier_Ctrl)
        {
          ui_eat_event(evt);
          if(evt->delta_2f32.y < 0)
          {
            rd_cmd(RD_CmdKind_IncCodeFontScale);
          }
          else if(evt->delta_2f32.y > 0)
          {
            rd_cmd(RD_CmdKind_DecCodeFontScale);
          }
        }
      }
    }
  }
  
  //////////////////////////////
  //- rjf: build rows
  //
  UI_Parent(row_container_box) RD_Font(RD_FontSlot_Code) UI_FontSize(font_size)
  {
    Rng1U64 selection = r1u64(cursor, mark);
    U8 *row_ascii_buffer = push_array(scratch.arena, U8, num_columns);
    UI_WidthFill UI_PrefHeight(ui_px(row_height_px, 1.f))
      for(S64 row_idx = viz_range_rows.min; row_idx <= viz_range_rows.max; row_idx += 1)
    {
      Rng1U64 row_range_bytes = r1u64(row_idx*num_columns, (row_idx+1)*num_columns);
      B32 row_is_boundary = 0;
      Vec4F32 row_boundary_color = {0};
      if(row_range_bytes.min%64 == 0)
      {
        row_is_boundary = 1;
        row_boundary_color = rd_rgba_from_theme_color(RD_ThemeColor_BaseBorder);
      }
      UI_Box *row = ui_build_box_from_stringf(UI_BoxFlag_DrawSideTop*!!row_is_boundary, "row_%I64x", row_range_bytes.min);
      UI_Parent(row)
      {
        UI_PrefWidth(ui_px(big_glyph_advance*18.f, 1.f))
        {
          if(!(selection.max >= row_range_bytes.min && selection.min < row_range_bytes.max))
          {
            ui_set_next_flags(UI_BoxFlag_DrawTextWeak);
          }
          ui_labelf("%016I64X", row_range_bytes.min);
        }
        UI_PrefWidth(ui_px(cell_width_px, 1.f))
          UI_TextAlignment(UI_TextAlign_Center)
          UI_CornerRadius(0)
        {
          for(U64 col_idx = 0; col_idx < num_columns; col_idx += 1)
          {
            U64 visible_byte_idx = (row_idx-viz_range_rows.min)*num_columns + col_idx;
            U64 global_byte_idx = viz_range_bytes.min+visible_byte_idx;
            U64 global_byte_num = global_byte_idx+1;
            U8 byte_value = visible_memory[visible_byte_idx];
            Annotation *annotation = visible_memory_annotations[visible_byte_idx].first;
            UI_BoxFlags cell_flags = 0;
            Vec4F32 cell_border_rgba = {0};
            Vec4F32 cell_bg_rgba = {0};
            if(global_byte_num == mouse_hover_byte_num)
            {
              cell_flags |= UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawSideTop|UI_BoxFlag_DrawSideBottom|UI_BoxFlag_DrawSideLeft|UI_BoxFlag_DrawSideRight;
              cell_border_rgba = rd_rgba_from_theme_color(RD_ThemeColor_Hover);
            }
            if(annotation != 0)
            {
              cell_flags |= UI_BoxFlag_DrawBackground;
              cell_bg_rgba = annotation->color;
              if(contains_1u64(annotation->vaddr_range, mouse_hover_byte_num-1))
              {
                cell_bg_rgba.w *= 0.15f;
              }
              else
              {
                cell_bg_rgba.w *= 0.08f;
              }
            }
            if(selection.min <= global_byte_idx && global_byte_idx <= selection.max)
            {
              cell_flags |= UI_BoxFlag_DrawBackground;
              cell_bg_rgba = rd_rgba_from_theme_color(RD_ThemeColor_SelectionOverlay);
            }
            ui_set_next_palette(ui_build_palette(ui_top_palette(), .background = cell_bg_rgba));
            UI_Box *cell_box = ui_build_box_from_key(UI_BoxFlag_DrawText|cell_flags, ui_key_zero());
            ui_box_equip_display_fancy_strings(cell_box, &byte_fancy_strings[byte_value]);
            {
              F32 off = 0;
              for(Annotation *a = annotation; a != 0; a = a->next)
              {
                if(global_byte_idx == a->vaddr_range.min) UI_Parent(row_overlay_box)
                {
                  ui_set_next_palette(ui_build_palette(ui_top_palette(), .background = annotation->color));
                  ui_set_next_fixed_x(big_glyph_advance*18.f + col_idx*cell_width_px + -cell_width_px/8.f + off);
                  ui_set_next_fixed_y((row_idx-viz_range_rows.min)*row_height_px + -cell_width_px/8.f);
                  ui_set_next_fixed_width(cell_width_px/4.f);
                  ui_set_next_fixed_height(cell_width_px/4.f);
                  ui_set_next_corner_radius_00(cell_width_px/8.f);
                  ui_set_next_corner_radius_01(cell_width_px/8.f);
                  ui_set_next_corner_radius_10(cell_width_px/8.f);
                  ui_set_next_corner_radius_11(cell_width_px/8.f);
                  ui_build_box_from_key(UI_BoxFlag_Floating|UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawDropShadow, ui_key_zero());
                  off += cell_width_px/8.f + cell_width_px/16.f;
                }
              }
            }
            if(annotation != 0 && mouse_hover_byte_num == global_byte_num) UI_Tooltip UI_FontSize(ui_top_font_size()) UI_PrefHeight(ui_px(ui_top_font_size()*1.75f, 1.f))
            {
              for(Annotation *a = annotation; a != 0; a = a->next)
              {
                UI_PrefWidth(ui_children_sum(1)) UI_Row UI_PrefWidth(ui_text_dim(10, 1))
                {
                  RD_Font(RD_FontSlot_Code) ui_label(a->name_string);
                  RD_Font(RD_FontSlot_Main) UI_FlagsAdd(UI_BoxFlag_DrawTextWeak) ui_label(a->kind_string);
                }
                if(a->type_string.size != 0)
                {
                  rd_code_label(1.f, 1, rd_rgba_from_theme_color(RD_ThemeColor_CodeType), a->type_string);
                }
                UI_FlagsAdd(UI_BoxFlag_DrawTextWeak) ui_label(str8_from_memory_size(scratch.arena, dim_1u64(a->vaddr_range)));
                if(a->next != 0)
                {
                  ui_spacer(ui_em(1.5f, 1.f));
                }
              }
            }
          }
        }
        ui_spacer(ui_px(big_glyph_advance*1.5f, 1.f));
        UI_WidthFill
        {
          MemoryZero(row_ascii_buffer, num_columns);
          for(U64 col_idx = 0; col_idx < num_columns; col_idx += 1)
          {
            U8 byte_value = visible_memory[(row_idx-viz_range_rows.min)*num_columns + col_idx];
            row_ascii_buffer[col_idx] = byte_value;
            if(byte_value <= 32 || 127 < byte_value)
            {
              row_ascii_buffer[col_idx] = '.';
            }
          }
          String8 ascii_text = str8(row_ascii_buffer, num_columns);
          UI_Box *ascii_box = ui_build_box_from_stringf(UI_BoxFlag_DrawText, "%S###ascii_row_%I64x", ascii_text, row_range_bytes.min);
          if(selection.max >= row_range_bytes.min && selection.min < row_range_bytes.max)
          {
            Rng1U64 selection_in_row = intersect_1u64(row_range_bytes, selection);
            DR_Bucket *bucket = dr_bucket_make();
            D_BucketScope(bucket)
            {
              Vec2F32 text_pos = ui_box_text_position(ascii_box);
              dr_rect(r2f32p(text_pos.x + fnt_dim_from_tag_size_string(font, font_size, 0, 0, str8_prefix(ascii_text, selection_in_row.min+0-row_range_bytes.min)).x - font_size/8.f,
                             ascii_box->rect.y0,
                             text_pos.x + fnt_dim_from_tag_size_string(font, font_size, 0, 0, str8_prefix(ascii_text, selection_in_row.max+1-row_range_bytes.min)).x + font_size/4.f,
                             ascii_box->rect.y1),
                      rd_rgba_from_theme_color(RD_ThemeColor_SelectionOverlay),
                      0, 0, 1.f);
            }
            ui_box_equip_draw_bucket(ascii_box, bucket);
          }
          if(mouse_hover_byte_num != 0 && contains_1u64(row_range_bytes, mouse_hover_byte_num-1))
          {
            DR_Bucket *bucket = dr_bucket_make();
            D_BucketScope(bucket)
            {
              Vec2F32 text_pos = ui_box_text_position(ascii_box);
              Vec4F32 color = rd_rgba_from_theme_color(RD_ThemeColor_HighlightOverlay);
              dr_rect(r2f32p(text_pos.x + fnt_dim_from_tag_size_string(font, font_size, 0, 0, str8_prefix(ascii_text, mouse_hover_byte_num-1-row_range_bytes.min)).x - font_size/8.f,
                             ascii_box->rect.y0,
                             text_pos.x + fnt_dim_from_tag_size_string(font, font_size, 0, 0, str8_prefix(ascii_text, mouse_hover_byte_num+0-row_range_bytes.min)).x + font_size/4.f,
                             ascii_box->rect.y1),
                      color,
                      1.f, 3.f, 1.f);
            }
            ui_box_equip_draw_bucket(ascii_box, bucket);
          }
        }
      }
    }
  }
  
  //////////////////////////////
  //- rjf: build footer
  //
  UI_Box *footer_box = &ui_nil_box;
  UI_Parent(container_box)
  {
    ui_set_next_fixed_x(footer_rect.x0);
    ui_set_next_fixed_y(footer_rect.y0);
    ui_set_next_fixed_width(dim_2f32(footer_rect).x);
    ui_set_next_fixed_height(dim_2f32(footer_rect).y);
    footer_box = ui_build_box_from_stringf(UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawDropShadow, "footer");
    UI_Parent(footer_box) RD_Font(RD_FontSlot_Code) UI_FontSize(font_size)
    {
      UI_PrefWidth(ui_em(7.5f, 1.f)) UI_HeightFill UI_Column UI_FlagsAdd(UI_BoxFlag_DrawTextWeak)
        UI_PrefHeight(ui_px(row_height_px, 0.f))
      {
        ui_labelf("Address:");
        ui_labelf("U8:");
        ui_labelf("U16:");
        ui_labelf("U32:");
        ui_labelf("U64:");
      }
      UI_PrefWidth(ui_em(45.f, 1.f)) UI_HeightFill UI_Column
        UI_PrefHeight(ui_px(row_height_px, 0.f))
      {
        B32 cursor_in_range = (viz_range_bytes.min <= cursor && cursor+8 <= viz_range_bytes.max);
        ui_labelf("%016I64X", cursor);
        if(cursor_in_range)
        {
          U64 as_u8  = 0;
          U64 as_u16 = 0;
          U64 as_u32 = 0;
          U64 as_u64 = 0;
          U64 cursor_off = cursor-viz_range_bytes.min;
          as_u8  = (U64)*(U8 *)(visible_memory + cursor_off);
          as_u16 = (U64)*(U16*)(visible_memory + cursor_off);
          as_u32 = (U64)*(U32*)(visible_memory + cursor_off);
          as_u64 = (U64)*(U64*)(visible_memory + cursor_off);
          ui_labelf("%02X (%I64u)",  as_u8,  as_u8);
          ui_labelf("%04X (%I64u)",  as_u16, as_u16);
          ui_labelf("%08X (%I64u)",  as_u32, as_u32);
          ui_labelf("%016I64X (%I64u)", as_u64, as_u64);
        }
      }
    }
  }
  
  //////////////////////////////
  //- rjf: scroll
  //
  {
    UI_Signal sig = ui_signal_from_box(scrollable_box);
    if(sig.scroll.y != 0)
    {
      S64 new_idx = scroll_pos.y.idx + sig.scroll.y;
      new_idx = clamp_1s64(scroll_idx_rng, new_idx);
      ui_scroll_pt_target_idx(&scroll_pos.y, new_idx);
    }
  }
  
  //////////////////////////////
  //- rjf: save parameters
  //
  rd_store_view_param_u64(str8_lit("cursor_vaddr"), cursor);
  rd_store_view_param_u64(str8_lit("mark_vaddr"), mark);
  rd_store_view_param_u64(str8_lit("bytes_per_cell"), bytes_per_cell);
  rd_store_view_param_u64(str8_lit("num_columns"), num_columns);
  rd_store_view_scroll_pos(scroll_pos);
  
  hs_scope_close(hs_scope);
  scratch_end(scratch);
  ProfEnd();
}

////////////////////////////////
//~ rjf: "graph"

EV_VIEW_RULE_BLOCK_PROD_FUNCTION_DEF(graph)
{
  EV_Block *vb = ev_block_begin(arena, EV_BlockKind_Canvas, key, ev_key_make(ev_hash_from_key(key), 1), depth);
  vb->string             = string;
  vb->expr               = expr;
  vb->visual_idx_range   = r1u64(0, 8);
  vb->semantic_idx_range = r1u64(0, 1);
  vb->view_rules         = view_rules;
  ev_block_end(out, vb);
}

////////////////////////////////
//~ rjf: bitmap @view_hook_impl

typedef struct RD_BitmapBoxDrawData RD_BitmapBoxDrawData;
struct RD_BitmapBoxDrawData
{
  Rng2F32 src;
  R_Handle texture;
  F32 loaded_t;
  B32 hovered;
  Vec2S32 mouse_px;
  F32 ui_per_bmp_px;
};

typedef struct RD_BitmapCanvasBoxDrawData RD_BitmapCanvasBoxDrawData;
struct RD_BitmapCanvasBoxDrawData
{
  Vec2F32 view_center_pos;
  F32 zoom;
};

internal Vec2F32
rd_bitmap_screen_from_canvas_pos(Vec2F32 view_center_pos, F32 zoom, Rng2F32 rect, Vec2F32 cvs)
{
  Vec2F32 scr =
  {
    (rect.x0+rect.x1)/2 + (cvs.x - view_center_pos.x) * zoom,
    (rect.y0+rect.y1)/2 + (cvs.y - view_center_pos.y) * zoom,
  };
  return scr;
}

internal Rng2F32
rd_bitmap_screen_from_canvas_rect(Vec2F32 view_center_pos, F32 zoom, Rng2F32 rect, Rng2F32 cvs)
{
  Rng2F32 scr = r2f32(rd_bitmap_screen_from_canvas_pos(view_center_pos, zoom, rect, cvs.p0), rd_bitmap_screen_from_canvas_pos(view_center_pos, zoom, rect, cvs.p1));
  return scr;
}

internal Vec2F32
rd_bitmap_canvas_from_screen_pos(Vec2F32 view_center_pos, F32 zoom, Rng2F32 rect, Vec2F32 scr)
{
  Vec2F32 cvs =
  {
    (scr.x - (rect.x0+rect.x1)/2) / zoom + view_center_pos.x,
    (scr.y - (rect.y0+rect.y1)/2) / zoom + view_center_pos.y,
  };
  return cvs;
}

internal Rng2F32
rd_bitmap_canvas_from_screen_rect(Vec2F32 view_center_pos, F32 zoom, Rng2F32 rect, Rng2F32 scr)
{
  Rng2F32 cvs = r2f32(rd_bitmap_canvas_from_screen_pos(view_center_pos, zoom, rect, scr.p0), rd_bitmap_canvas_from_screen_pos(view_center_pos, zoom, rect, scr.p1));
  return cvs;
}

internal UI_BOX_CUSTOM_DRAW(rd_bitmap_view_canvas_box_draw)
{
  RD_BitmapCanvasBoxDrawData *draw_data = (RD_BitmapCanvasBoxDrawData *)user_data;
  Rng2F32 rect_scrn = box->rect;
  Rng2F32 rect_cvs = rd_bitmap_canvas_from_screen_rect(draw_data->view_center_pos, draw_data->zoom, rect_scrn, rect_scrn);
  F32 grid_cell_size_cvs = box->font_size*10.f;
  F32 grid_line_thickness_px = Max(2.f, box->font_size*0.1f);
  Vec4F32 grid_line_color = rd_rgba_from_theme_color(RD_ThemeColor_TextWeak);
  for EachEnumVal(Axis2, axis)
  {
    for(F32 v = rect_cvs.p0.v[axis] - mod_f32(rect_cvs.p0.v[axis], grid_cell_size_cvs);
        v < rect_cvs.p1.v[axis];
        v += grid_cell_size_cvs)
    {
      Vec2F32 p_cvs = {0};
      p_cvs.v[axis] = v;
      Vec2F32 p_scr = rd_bitmap_screen_from_canvas_pos(draw_data->view_center_pos, draw_data->zoom, rect_scrn, p_cvs);
      Rng2F32 rect = {0};
      rect.p0.v[axis] = p_scr.v[axis] - grid_line_thickness_px/2;
      rect.p1.v[axis] = p_scr.v[axis] + grid_line_thickness_px/2;
      rect.p0.v[axis2_flip(axis)] = box->rect.p0.v[axis2_flip(axis)];
      rect.p1.v[axis2_flip(axis)] = box->rect.p1.v[axis2_flip(axis)];
      dr_rect(rect, grid_line_color, 0, 0, 1.f);
    }
  }
}

EV_VIEW_RULE_BLOCK_PROD_FUNCTION_DEF(bitmap)
{
  EV_Block *vb = ev_block_begin(arena, EV_BlockKind_Canvas, key, ev_key_make(ev_hash_from_key(key), 1), depth);
  vb->string             = string;
  vb->expr               = expr;
  vb->visual_idx_range   = r1u64(0, 8);
  vb->semantic_idx_range = r1u64(0, 1);
  vb->view_rules         = view_rules;
  ev_block_end(out, vb);
}

RD_VIEW_RULE_UI_FUNCTION_DEF(bitmap)
{
  Temp scratch = scratch_begin(0, 0);
  HS_Scope *hs_scope = hs_scope_open();
  TEX_Scope *tex_scope = tex_scope_open();
  
  //////////////////////////////
  //- rjf: evaluate expression
  //
  E_Eval eval = e_eval_from_string(scratch.arena, string);
  Vec2S32 dim = rd_dim2s32_from_eval_params(eval, params);
  R_Tex2DFormat fmt = rd_tex2dformat_from_eval_params(eval, params);
  U64 base_offset = rd_base_offset_from_eval(eval);
  U64 expected_size = dim.x*dim.y*r_tex2d_format_bytes_per_pixel_table[fmt];
  Rng1U64 offset_range = r1u64(base_offset, base_offset + expected_size);
  
  //////////////////////////////
  //- rjf: unpack params
  //
  F32 zoom = rd_value_from_params_key(params, str8_lit("zoom")).f32;
  Vec2F32 view_center_pos =
  {
    rd_value_from_params_key(params, str8_lit("x")).f32,
    rd_value_from_params_key(params, str8_lit("y")).f32,
  };
  if(zoom == 0)
  {
    F32 available_dim_y = dim_2f32(rect).y;
    F32 image_dim_y = (F32)dim.y;
    if(image_dim_y != 0)
    {
      zoom = (available_dim_y / image_dim_y) * 0.8f;
    }
    else
    {
      zoom = 1.f;
    }
  }
  
  //////////////////////////////
  //- rjf: map expression artifacts -> texture
  //
  U128 texture_key = rd_key_from_eval_space_range(eval.space, offset_range, 0);
  TEX_Topology topology = tex_topology_make(dim, fmt);
  U128 data_hash = {0};
  R_Handle texture = tex_texture_from_key_topology(tex_scope, texture_key, topology, &data_hash);
  String8 data = hs_data_from_hash(hs_scope, data_hash);
  
  //////////////////////////////
  //- rjf: equip loading info
  //
  if(offset_range.max != offset_range.min &&
     eval.msgs.max_kind == E_MsgKind_Null &&
     (u128_match(data_hash, u128_zero()) ||
      r_handle_match(texture, r_handle_zero()) ||
      data.size == 0))
  {
    rd_store_view_loading_info(1, 0, 0);
  }
  
  //////////////////////////////
  //- rjf: build canvas box
  //
  UI_Box *canvas_box = &ui_nil_box;
  Vec2F32 canvas_dim = dim_2f32(rect);
  Rng2F32 canvas_rect = r2f32p(0, 0, canvas_dim.x, canvas_dim.y);
  UI_Rect(canvas_rect)
  {
    canvas_box = ui_build_box_from_stringf(UI_BoxFlag_Clip|UI_BoxFlag_Clickable|UI_BoxFlag_Scroll, "bmp_canvas");
  }
  
  //////////////////////////////
  //- rjf: canvas dragging
  //
  UI_Signal canvas_sig = ui_signal_from_box(canvas_box);
  {
    if(ui_dragging(canvas_sig))
    {
      if(ui_pressed(canvas_sig))
      {
        rd_cmd(RD_CmdKind_FocusPanel);
        ui_store_drag_struct(&view_center_pos);
      }
      Vec2F32 start_view_center_pos = *ui_get_drag_struct(Vec2F32);
      Vec2F32 drag_delta_scr = ui_drag_delta();
      Vec2F32 drag_delta_cvs = scale_2f32(drag_delta_scr, 1.f/zoom);
      Vec2F32 new_view_center_pos = sub_2f32(start_view_center_pos, drag_delta_cvs);
      view_center_pos = new_view_center_pos;
    }
    if(canvas_sig.scroll.y != 0)
    {
      F32 new_zoom = zoom - zoom*canvas_sig.scroll.y/10.f;
      new_zoom = Clamp(1.f/256.f, new_zoom, 256.f);
      Vec2F32 mouse_scr_pre = sub_2f32(ui_mouse(), rect.p0);
      Vec2F32 mouse_cvs = rd_bitmap_canvas_from_screen_pos(view_center_pos, zoom, canvas_rect, mouse_scr_pre);
      zoom = new_zoom;
      Vec2F32 mouse_scr_pst = rd_bitmap_screen_from_canvas_pos(view_center_pos, zoom, canvas_rect, mouse_cvs);
      Vec2F32 drift_scr = sub_2f32(mouse_scr_pst, mouse_scr_pre);
      view_center_pos = add_2f32(view_center_pos, scale_2f32(drift_scr, 1.f/new_zoom));
    }
    if(ui_double_clicked(canvas_sig))
    {
      ui_kill_action();
      MemoryZeroStruct(&view_center_pos);
      zoom = 1.f;
    }
  }
  
  //////////////////////////////
  //- rjf: equip canvas draw info
  //
  {
    RD_BitmapCanvasBoxDrawData *draw_data = push_array(ui_build_arena(), RD_BitmapCanvasBoxDrawData, 1);
    draw_data->view_center_pos = view_center_pos;
    draw_data->zoom = zoom;
    ui_box_equip_custom_draw(canvas_box, rd_bitmap_view_canvas_box_draw, draw_data);
  }
  
  //////////////////////////////
  //- rjf: calculate image coordinates
  //
  Rng2F32 img_rect_cvs = r2f32p(-topology.dim.x/2, -topology.dim.y/2, +topology.dim.x/2, +topology.dim.y/2);
  Rng2F32 img_rect_scr = rd_bitmap_screen_from_canvas_rect(view_center_pos, zoom, canvas_rect, img_rect_cvs);
  
  //////////////////////////////
  //- rjf: image-region canvas interaction
  //
  Vec2S32 mouse_bmp = {-1, -1};
  if(ui_hovering(canvas_sig) && !ui_dragging(canvas_sig))
  {
    Vec2F32 mouse_scr = sub_2f32(ui_mouse(), rect.p0);
    Vec2F32 mouse_cvs = rd_bitmap_canvas_from_screen_pos(view_center_pos, zoom, canvas_rect, mouse_scr);
    if(contains_2f32(img_rect_cvs, mouse_cvs))
    {
      mouse_bmp = v2s32((S32)(mouse_cvs.x-img_rect_cvs.x0), (S32)(mouse_cvs.y-img_rect_cvs.y0));
      S64 off_px = mouse_bmp.y*topology.dim.x + mouse_bmp.x;
      S64 off_bytes = off_px*r_tex2d_format_bytes_per_pixel_table[topology.fmt];
      if(0 <= off_bytes && off_bytes+r_tex2d_format_bytes_per_pixel_table[topology.fmt] <= data.size &&
         r_tex2d_format_bytes_per_pixel_table[topology.fmt] != 0)
      {
        B32 color_is_good = 1;
        Vec4F32 color = {0};
        switch(topology.fmt)
        {
          default:{color_is_good = 0;}break;
          case R_Tex2DFormat_R8:     {color = v4f32(((U8 *)(data.str+off_bytes))[0]/255.f, 0, 0, 1);}break;
          case R_Tex2DFormat_RG8:    {color = v4f32(((U8 *)(data.str+off_bytes))[0]/255.f, ((U8 *)(data.str+off_bytes))[1]/255.f, 0, 1);}break;
          case R_Tex2DFormat_RGBA8:  {color = v4f32(((U8 *)(data.str+off_bytes))[0]/255.f, ((U8 *)(data.str+off_bytes))[1]/255.f, ((U8 *)(data.str+off_bytes))[2]/255.f, ((U8 *)(data.str+off_bytes))[3]/255.f);}break;
          case R_Tex2DFormat_BGRA8:  {color = v4f32(((U8 *)(data.str+off_bytes))[3]/255.f, ((U8 *)(data.str+off_bytes))[2]/255.f, ((U8 *)(data.str+off_bytes))[1]/255.f, ((U8 *)(data.str+off_bytes))[0]/255.f);}break;
          case R_Tex2DFormat_R16:    {color = v4f32(((U16 *)(data.str+off_bytes))[0]/(F32)max_U16, 0, 0, 1);}break;
          case R_Tex2DFormat_RGBA16: {color = v4f32(((U16 *)(data.str+off_bytes))[0]/(F32)max_U16, ((U16 *)(data.str+off_bytes))[1]/(F32)max_U16, ((U16 *)(data.str+off_bytes))[2]/(F32)max_U16, ((U16 *)(data.str+off_bytes))[3]/(F32)max_U16);}break;
          case R_Tex2DFormat_R32:    {color = v4f32(((F32 *)(data.str+off_bytes))[0], 0, 0, 1);}break;
          case R_Tex2DFormat_RG32:   {color = v4f32(((F32 *)(data.str+off_bytes))[0], ((F32 *)(data.str+off_bytes))[1], 0, 1);}break;
          case R_Tex2DFormat_RGBA32: {color = v4f32(((F32 *)(data.str+off_bytes))[0], ((F32 *)(data.str+off_bytes))[1], ((F32 *)(data.str+off_bytes))[2], ((F32 *)(data.str+off_bytes))[3]);}break;
        }
        if(color_is_good)
        {
          Vec4F32 hsva = hsva_from_rgba(color);
          ui_do_color_tooltip_hsva(hsva);
        }
      }
    }
  }
  
  //////////////////////////////
  //- rjf: build image
  //
  UI_Parent(canvas_box)
  {
    if(0 <= mouse_bmp.x && mouse_bmp.x < dim.x &&
       0 <= mouse_bmp.x && mouse_bmp.x < dim.y)
    {
      F32 pixel_size_scr = 1.f*zoom;
      Rng2F32 indicator_rect_scr = r2f32p(img_rect_scr.x0 + mouse_bmp.x*pixel_size_scr,
                                          img_rect_scr.y0 + mouse_bmp.y*pixel_size_scr,
                                          img_rect_scr.x0 + (mouse_bmp.x+1)*pixel_size_scr,
                                          img_rect_scr.y0 + (mouse_bmp.y+1)*pixel_size_scr);
      UI_Rect(indicator_rect_scr)
      {
        ui_build_box_from_key(UI_BoxFlag_DrawBorder|UI_BoxFlag_Floating, ui_key_zero());
      }
    }
    UI_Rect(img_rect_scr) UI_Flags(UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawDropShadow|UI_BoxFlag_Floating)
    {
      ui_image(texture, R_Tex2DSampleKind_Nearest, r2f32p(0, 0, (F32)dim.x, (F32)dim.y), v4f32(1, 1, 1, 1), 0, str8_lit("bmp_image"));
    }
  }
  
  //////////////////////////////
  //- rjf: store params
  //
  rd_store_view_param_f32(str8_lit("zoom"), zoom);
  rd_store_view_param_f32(str8_lit("x"), view_center_pos.x);
  rd_store_view_param_f32(str8_lit("y"), view_center_pos.y);
  
  hs_scope_close(hs_scope);
  tex_scope_close(tex_scope);
  scratch_end(scratch);
}

////////////////////////////////
//~ rjf: "checkbox"

RD_VIEW_RULE_UI_FUNCTION_DEF(checkbox)
{
  Temp scratch = scratch_begin(0, 0);
  E_Eval eval = e_eval_from_string(scratch.arena, string);
  E_Eval value_eval = e_value_eval_from_eval(eval);
  if(ui_clicked(rd_icon_buttonf(value_eval.value.u64 == 0 ? RD_IconKind_CheckHollow : RD_IconKind_CheckFilled, 0, "###check")))
  {
    rd_commit_eval_value_string(eval, value_eval.value.u64 == 0 ? str8_lit("1") : str8_lit("0"));
  }
  scratch_end(scratch);
}

////////////////////////////////
//~ rjf: color_rgba @view_hook_impl

internal Vec4F32
rd_rgba_from_eval_params(E_Eval eval, MD_Node *params)
{
  Vec4F32 rgba = {0};
  {
    E_Eval value_eval = e_value_eval_from_eval(eval);
    E_TypeKey type_key = eval.type_key;
    E_TypeKind type_kind = e_type_kind_from_key(type_key);
    U64 type_size = e_type_byte_size_from_key(type_key);
    if(16 <= type_size)
    {
      e_space_read(eval.space, &rgba, r1u64(eval.value.u64, eval.value.u64 + 16));
    }
    else if(4 <= type_size)
    {
      U32 hex_val = value_eval.value.u32;
      rgba = rgba_from_u32(hex_val);
    }
  }
  return rgba;
}

EV_VIEW_RULE_BLOCK_PROD_FUNCTION_DEF(color_rgba)
{
  EV_Block *vb = ev_block_begin(arena, EV_BlockKind_Canvas, key, ev_key_make(ev_hash_from_key(key), 1), depth);
  vb->string             = string;
  vb->expr               = expr;
  vb->visual_idx_range   = r1u64(0, 8);
  vb->semantic_idx_range = r1u64(0, 1);
  vb->view_rules         = view_rules;
  ev_block_end(out, vb);
}

RD_VIEW_RULE_UI_FUNCTION_DEF(color_rgba)
{
  Temp scratch = scratch_begin(0, 0);
  Vec2F32 dim = dim_2f32(rect);
  F32 padding = ui_top_font_size()*3.f;
  E_Eval eval = e_eval_from_string(scratch.arena, string);
  Vec4F32 rgba = rd_rgba_from_eval_params(eval, params);
  Vec4F32 hsva = hsva_from_rgba(rgba);
  
  //- rjf: too small -> just show components
  if(dim.y <= ui_top_font_size()*8.f)
  {
    //- rjf: build text box
    UI_Box *text_box = &ui_nil_box;
    UI_WidthFill RD_Font(RD_FontSlot_Code)
    {
      text_box = ui_build_box_from_key(UI_BoxFlag_DrawText, ui_key_zero());
      DR_FancyStringList fancy_strings = {0};
      {
        DR_FancyString open_paren = {ui_top_font(), str8_lit("("), ui_top_palette()->text, ui_top_font_size(), 0, 0};
        DR_FancyString comma = {ui_top_font(), str8_lit(", "), ui_top_palette()->text, ui_top_font_size(), 0, 0};
        DR_FancyString r_fstr = {ui_top_font(), push_str8f(scratch.arena, "%.2f", rgba.x), v4f32(1.f, 0.25f, 0.25f, 1.f), ui_top_font_size(), 4.f, 0};
        DR_FancyString g_fstr = {ui_top_font(), push_str8f(scratch.arena, "%.2f", rgba.y), v4f32(0.25f, 1.f, 0.25f, 1.f), ui_top_font_size(), 4.f, 0};
        DR_FancyString b_fstr = {ui_top_font(), push_str8f(scratch.arena, "%.2f", rgba.z), v4f32(0.25f, 0.25f, 1.f, 1.f), ui_top_font_size(), 4.f, 0};
        DR_FancyString a_fstr = {ui_top_font(), push_str8f(scratch.arena, "%.2f", rgba.w), v4f32(1.f,   1.f,   1.f, 1.f), ui_top_font_size(), 4.f, 0};
        DR_FancyString clse_paren = {ui_top_font(), str8_lit(")"), ui_top_palette()->text, ui_top_font_size(), 0, 0};
        dr_fancy_string_list_push(scratch.arena, &fancy_strings, &open_paren);
        dr_fancy_string_list_push(scratch.arena, &fancy_strings, &r_fstr);
        dr_fancy_string_list_push(scratch.arena, &fancy_strings, &comma);
        dr_fancy_string_list_push(scratch.arena, &fancy_strings, &g_fstr);
        dr_fancy_string_list_push(scratch.arena, &fancy_strings, &comma);
        dr_fancy_string_list_push(scratch.arena, &fancy_strings, &b_fstr);
        dr_fancy_string_list_push(scratch.arena, &fancy_strings, &comma);
        dr_fancy_string_list_push(scratch.arena, &fancy_strings, &a_fstr);
        dr_fancy_string_list_push(scratch.arena, &fancy_strings, &clse_paren);
      }
      ui_box_equip_display_fancy_strings(text_box, &fancy_strings);
    }
    
    //- rjf: build color box
    UI_Box *color_box = &ui_nil_box;
    UI_PrefWidth(ui_em(1.875f, 1.f)) UI_ChildLayoutAxis(Axis2_Y)
    {
      color_box = ui_build_box_from_stringf(UI_BoxFlag_Clickable, "color_box");
      UI_Parent(color_box) UI_PrefHeight(ui_em(1.875f, 1.f)) UI_Padding(ui_pct(1, 0))
      {
        UI_Palette(ui_build_palette(ui_top_palette(), .background = rgba)) UI_CornerRadius(ui_top_font_size()*0.5f)
          ui_build_box_from_key(UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawBorder, ui_key_zero());
      }
    }
    
    //- rjf: space
    ui_spacer(ui_em(0.375f, 1.f));
    
    //- rjf: hover color box -> show components
    UI_Signal sig = ui_signal_from_box(color_box);
    if(ui_hovering(sig))
    {
      ui_do_color_tooltip_hsva(hsva);
    }
  }
  
  //- rjf: large enough -> full color picker
  else
  {
    UI_WidthFill UI_HeightFill UI_Column UI_Padding(ui_px(padding, 1.f)) UI_Row UI_Padding(ui_pct(1.f, 0.f)) UI_HeightFill
    {
      UI_PrefWidth(ui_px(dim.y - padding*2, 1.f))
      {
        UI_Signal sv_sig = ui_sat_val_pickerf(hsva.x, &hsva.y, &hsva.z, "sat_val_picker");
      }
      UI_PrefWidth(ui_em(3.f, 1.f))
      {
        UI_Signal h_sig  = ui_hue_pickerf(&hsva.x, hsva.y, hsva.z, "hue_picker");
      }
      UI_PrefWidth(ui_children_sum(1)) UI_Column UI_PrefWidth(ui_text_dim(10, 1)) UI_PrefHeight(ui_em(2.f, 0.f)) RD_Font(RD_FontSlot_Code) UI_FlagsAdd(UI_BoxFlag_DrawTextWeak)
      {
        ui_labelf("Hex");
        ui_labelf("R");
        ui_labelf("G");
        ui_labelf("B");
        ui_labelf("H");
        ui_labelf("S");
        ui_labelf("V");
        ui_labelf("A");
      }
      UI_PrefWidth(ui_children_sum(1)) UI_Column UI_PrefWidth(ui_text_dim(10, 1)) UI_PrefHeight(ui_em(2.f, 0.f)) RD_Font(RD_FontSlot_Code)
      {
        String8 hex_string = hex_string_from_rgba_4f32(scratch.arena, rgba);
        ui_label(hex_string);
        ui_labelf("%.2f", rgba.x);
        ui_labelf("%.2f", rgba.y);
        ui_labelf("%.2f", rgba.z);
        ui_labelf("%.2f", hsva.x);
        ui_labelf("%.2f", hsva.y);
        ui_labelf("%.2f", hsva.z);
        ui_labelf("%.2f", rgba.w);
      }
    }
  }
  scratch_end(scratch);
}

////////////////////////////////
//~ rjf: geo3d @view_hook_impl

typedef struct RD_Geo3DViewState RD_Geo3DViewState;
struct RD_Geo3DViewState
{
  F32 yaw;
  F32 pitch;
  F32 zoom;
};

typedef struct RD_Geo3DBoxDrawData RD_Geo3DBoxDrawData;
struct RD_Geo3DBoxDrawData
{
  F32 yaw;
  F32 pitch;
  F32 zoom;
  R_Handle vertex_buffer;
  R_Handle index_buffer;
};

internal UI_BOX_CUSTOM_DRAW(rd_geo3d_box_draw)
{
  RD_Geo3DBoxDrawData *draw_data = (RD_Geo3DBoxDrawData *)user_data;
  
  // rjf: get clip
  Rng2F32 clip = box->rect;
  for(UI_Box *b = box->parent; !ui_box_is_nil(b); b = b->parent)
  {
    if(b->flags & UI_BoxFlag_Clip)
    {
      clip = intersect_2f32(b->rect, clip);
    }
  }
  
  // rjf: calculate eye/target
  Vec3F32 target = {0};
  Vec3F32 eye = v3f32(draw_data->zoom*cos_f32(draw_data->yaw)*sin_f32(draw_data->pitch),
                      draw_data->zoom*sin_f32(draw_data->yaw)*sin_f32(draw_data->pitch),
                      draw_data->zoom*cos_f32(draw_data->pitch));
  
  // rjf: mesh
  Vec2F32 box_dim = dim_2f32(box->rect);
  R_PassParams_Geo3D *pass = dr_geo3d_begin(box->rect,
                                            make_look_at_4x4f32(eye, target, v3f32(0, 0, 1)),
                                            make_perspective_4x4f32(0.25f, box_dim.x/box_dim.y, 0.1f, 500.f));
  pass->clip = clip;
  dr_mesh(draw_data->vertex_buffer, draw_data->index_buffer, R_GeoTopologyKind_Triangles, R_GeoVertexFlag_TexCoord|R_GeoVertexFlag_Normals|R_GeoVertexFlag_RGB, r_handle_zero(), mat_4x4f32(1.f));
}

EV_VIEW_RULE_BLOCK_PROD_FUNCTION_DEF(geo3d)
{
  EV_Block *vb = ev_block_begin(arena, EV_BlockKind_Canvas, key, ev_key_make(ev_hash_from_key(key), 1), depth);
  vb->string             = string;
  vb->expr               = expr;
  vb->visual_idx_range   = r1u64(0, 16);
  vb->semantic_idx_range = r1u64(0, 1);
  vb->view_rules         = view_rules;
  ev_block_end(out, vb);
}

RD_VIEW_RULE_UI_FUNCTION_DEF(geo3d)
{
  Temp scratch = scratch_begin(0, 0);
  GEO_Scope *geo_scope = geo_scope_open();
  RD_Geo3DViewState *state = rd_view_state(RD_Geo3DViewState);
  
  //////////////////////////////
  //- rjf: unpack parameters
  //
  U64 count        = rd_value_from_params_key(params, str8_lit("count")).u64;
  U64 vtx_base_off = rd_value_from_params_key(params, str8_lit("vtx")).u64;
  U64 vtx_size     = rd_value_from_params_key(params, str8_lit("vtx_size")).u64;
  F32 yaw_target   = rd_value_from_params_key(params, str8_lit("yaw")).f32;
  F32 pitch_target = rd_value_from_params_key(params, str8_lit("pitch")).f32;
  F32 zoom_target  = rd_value_from_params_key(params, str8_lit("zoom")).f32;
  
  //////////////////////////////
  //- rjf: evaluate & unpack expression
  //
  E_Eval eval = e_eval_from_string(scratch.arena, string);
  U64 base_offset = rd_base_offset_from_eval(eval);
  Rng1U64 idxs_range = r1u64(base_offset, base_offset+count*sizeof(U32));
  Rng1U64 vtxs_range = r1u64(vtx_base_off, vtx_base_off+vtx_size);
  U128 idxs_key = rd_key_from_eval_space_range(eval.space, idxs_range, 0);
  U128 vtxs_key = rd_key_from_eval_space_range(eval.space, vtxs_range, 0);
  R_Handle idxs_buffer = geo_buffer_from_key(geo_scope, idxs_key);
  R_Handle vtxs_buffer = geo_buffer_from_key(geo_scope, vtxs_key);
  
  //////////////////////////////
  //- rjf: equip loading info
  //
  if(eval.msgs.max_kind == E_MsgKind_Null &&
     (r_handle_match(idxs_buffer, r_handle_zero()) ||
      r_handle_match(vtxs_buffer, r_handle_zero())))
  {
    rd_store_view_loading_info(1, 0, 0);
  }
  
  //////////////////////////////
  //- rjf: do first-time camera initialization, if needed
  //
  if(zoom_target == 0)
  {
    yaw_target   = -0.125f;
    pitch_target = -0.125f;
    zoom_target  = 3.5f;
  }
  
  //////////////////////////////
  //- rjf: animate camera
  //
  {
    F32 fast_rate = 1 - pow_f32(2, (-60.f * rd_state->frame_dt));
    F32 slow_rate = 1 - pow_f32(2, (-30.f * rd_state->frame_dt));
    state->zoom  += (zoom_target - state->zoom) * slow_rate;
    state->yaw   += (yaw_target - state->yaw) * fast_rate;
    state->pitch += (pitch_target - state->pitch) * fast_rate;
    if(abs_f32(state->zoom  - zoom_target)  > 0.001f ||
       abs_f32(state->yaw   - yaw_target)   > 0.001f ||
       abs_f32(state->pitch - pitch_target) > 0.001f)
    {
      rd_request_frame();
    }
  }
  
  //////////////////////////////
  //- rjf: build
  //
  if(count != 0 && !r_handle_match(idxs_buffer, r_handle_zero()) && !r_handle_match(vtxs_buffer, r_handle_zero()))
  {
    Vec2F32 dim = dim_2f32(rect);
    UI_Box *box = &ui_nil_box;
    UI_FixedSize(dim)
    {
      box = ui_build_box_from_stringf(UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawBackground|UI_BoxFlag_Clickable|UI_BoxFlag_Scroll, "geo_box");
    }
    UI_Signal sig = ui_signal_from_box(box);
    if(ui_dragging(sig))
    {
      if(ui_pressed(sig))
      {
        rd_cmd(RD_CmdKind_FocusPanel);
        Vec2F32 data = v2f32(yaw_target, pitch_target);
        ui_store_drag_struct(&data);
      }
      Vec2F32 drag_delta      = ui_drag_delta();
      Vec2F32 drag_start_data = *ui_get_drag_struct(Vec2F32);
      yaw_target   = drag_start_data.x + drag_delta.x/dim.x;
      pitch_target = drag_start_data.y + drag_delta.y/dim.y;
    }
    zoom_target += sig.scroll.y;
    zoom_target = Clamp(0.1f, zoom_target, 100.f);
    pitch_target = Clamp(-0.49f, pitch_target, -0.01f);
    RD_Geo3DBoxDrawData *draw_data = push_array(ui_build_arena(), RD_Geo3DBoxDrawData, 1);
    draw_data->yaw   = state->yaw;
    draw_data->pitch = state->pitch;
    draw_data->zoom  = state->zoom;
    draw_data->vertex_buffer  = vtxs_buffer;
    draw_data->index_buffer   = idxs_buffer;
    ui_box_equip_custom_draw(box, rd_geo3d_box_draw, draw_data);
  }
  
  //////////////////////////////
  //- rjf: commit parameters
  //
  rd_store_view_param_f32(str8_lit("yaw"),   yaw_target);
  rd_store_view_param_f32(str8_lit("pitch"), pitch_target);
  rd_store_view_param_f32(str8_lit("zoom"),  zoom_target);
  
  geo_scope_close(geo_scope);
  scratch_end(scratch);
}

////////////////////////////////
//~ rjf: exception_filters @view_hook_impl

RD_VIEW_RULE_UI_FUNCTION_DEF(exception_filters)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(0, 0);
  F32 row_height_px = floor_f32(ui_top_font_size()*2.5f);
  UI_ScrollPt2 scroll_pos = rd_view_scroll_pos();
  String8 query = string;
  
  //- rjf: get state
  typedef struct RD_ExceptionFiltersViewState RD_ExceptionFiltersViewState;
  struct RD_ExceptionFiltersViewState
  {
    Vec2S64 cursor;
  };
  RD_ExceptionFiltersViewState *sv = rd_view_state(RD_ExceptionFiltersViewState);
  
  //- rjf: get list of options
  typedef struct RD_ExceptionFiltersOption RD_ExceptionFiltersOption;
  struct RD_ExceptionFiltersOption
  {
    String8 name;
    FuzzyMatchRangeList matches;
    B32 is_enabled;
    CTRL_ExceptionCodeKind exception_code_kind;
  };
  typedef struct RD_ExceptionFiltersOptionChunkNode RD_ExceptionFiltersOptionChunkNode;
  struct RD_ExceptionFiltersOptionChunkNode
  {
    RD_ExceptionFiltersOptionChunkNode *next;
    RD_ExceptionFiltersOption *v;
    U64 cap;
    U64 count;
  };
  typedef struct RD_ExceptionFiltersOptionChunkList RD_ExceptionFiltersOptionChunkList;
  struct RD_ExceptionFiltersOptionChunkList
  {
    RD_ExceptionFiltersOptionChunkNode *first;
    RD_ExceptionFiltersOptionChunkNode *last;
    U64 option_count;
    U64 node_count;
  };
  typedef struct RD_ExceptionFiltersOptionArray RD_ExceptionFiltersOptionArray;
  struct RD_ExceptionFiltersOptionArray
  {
    RD_ExceptionFiltersOption *v;
    U64 count;
  };
  RD_ExceptionFiltersOptionChunkList opts_list = {0};
  for(CTRL_ExceptionCodeKind k = (CTRL_ExceptionCodeKind)(CTRL_ExceptionCodeKind_Null+1);
      k < CTRL_ExceptionCodeKind_COUNT;
      k = (CTRL_ExceptionCodeKind)(k+1))
  {
    RD_ExceptionFiltersOptionChunkNode *node = opts_list.last;
    String8 name = push_str8f(scratch.arena, "0x%x %S", ctrl_exception_code_kind_code_table[k], ctrl_exception_code_kind_display_string_table[k]);
    FuzzyMatchRangeList matches = fuzzy_match_find(scratch.arena, query, name);
    if(matches.count >= matches.needle_part_count)
    {
      if(node == 0 || node->count >= node->cap)
      {
        node = push_array(scratch.arena, RD_ExceptionFiltersOptionChunkNode, 1);
        node->cap = 256;
        node->v = push_array_no_zero(scratch.arena, RD_ExceptionFiltersOption, node->cap);
        SLLQueuePush(opts_list.first, opts_list.last, node);
        opts_list.node_count += 1;
      }
      node->v[node->count].name = name;
      node->v[node->count].matches = matches;
      node->v[node->count].is_enabled = !!(rd_state->ctrl_exception_code_filters[k/64] & (1ull<<(k%64)));
      node->v[node->count].exception_code_kind = k;
      node->count += 1;
      opts_list.option_count += 1;
    }
  }
  RD_ExceptionFiltersOptionArray opts = {0};
  {
    opts.count = opts_list.option_count;
    opts.v = push_array_no_zero(scratch.arena, RD_ExceptionFiltersOption, opts.count);
    U64 idx = 0;
    for(RD_ExceptionFiltersOptionChunkNode *n = opts_list.first; n != 0; n = n->next)
    {
      MemoryCopy(opts.v+idx, n->v, n->count*sizeof(RD_ExceptionFiltersOption));
      idx += n->count;
    }
  }
  
  //- rjf: build option table
  Rng1S64 visible_row_range = {0};
  UI_ScrollListParams scroll_list_params = {0};
  {
    Vec2F32 rect_dim = dim_2f32(rect);
    scroll_list_params.flags         = UI_ScrollListFlag_All;
    scroll_list_params.row_height_px = row_height_px;
    scroll_list_params.dim_px        = rect_dim;
    scroll_list_params.cursor_range  = r2s64(v2s64(0, 0), v2s64(0, opts.count));
    scroll_list_params.item_range    = r1s64(0, opts.count);
    scroll_list_params.cursor_min_is_empty_selection[Axis2_Y] = 1;
  }
  UI_ScrollListSignal scroll_list_sig = {0};
  UI_Focus(UI_FocusKind_On)
    UI_ScrollList(&scroll_list_params,
                  &scroll_pos.y,
                  &sv->cursor,
                  0,
                  &visible_row_range,
                  &scroll_list_sig)
    UI_Focus(UI_FocusKind_Null)
  {
    for(S64 row = visible_row_range.min; row <= visible_row_range.max && row < opts.count; row += 1)
      UI_FocusHot(sv->cursor.y == row+1 ? UI_FocusKind_On : UI_FocusKind_Off)
    {
      RD_ExceptionFiltersOption *opt = &opts.v[row];
      UI_Signal sig = rd_icon_buttonf(opt->is_enabled ? RD_IconKind_CheckFilled : RD_IconKind_CheckHollow, &opt->matches, "%S", opt->name);
      if(ui_clicked(sig))
      {
        if(opt->exception_code_kind != CTRL_ExceptionCodeKind_Null)
        {
          CTRL_ExceptionCodeKind k = opt->exception_code_kind;
          if(opt->is_enabled)
          {
            rd_state->ctrl_exception_code_filters[k/64] &= ~(1ull<<(k%64));
          }
          else
          {
            rd_state->ctrl_exception_code_filters[k/64] |= (1ull<<(k%64));
          }
        }
      }
    }
  }
  
  rd_store_view_scroll_pos(scroll_pos);
  scratch_end(scratch);
  ProfEnd();
}

////////////////////////////////
//~ rjf: settings @view_hook_impl

typedef enum RD_SettingsItemKind
{
  RD_SettingsItemKind_CategoryHeader,
  RD_SettingsItemKind_GlobalSetting,
  RD_SettingsItemKind_WindowSetting,
  RD_SettingsItemKind_ThemeColor,
  RD_SettingsItemKind_ThemePreset,
  RD_SettingsItemKind_COUNT
}
RD_SettingsItemKind;

typedef struct RD_SettingsItem RD_SettingsItem;
struct RD_SettingsItem
{
  RD_SettingsItemKind kind;
  String8 kind_string;
  String8 string;
  FuzzyMatchRangeList kind_string_matches;
  FuzzyMatchRangeList string_matches;
  RD_IconKind icon_kind;
  RD_SettingCode code;
  RD_ThemeColor color;
  RD_ThemePreset preset;
  RD_SettingsItemKind category;
};

typedef struct RD_SettingsItemNode RD_SettingsItemNode;
struct RD_SettingsItemNode
{
  RD_SettingsItemNode *next;
  RD_SettingsItem v;
};

typedef struct RD_SettingsItemList RD_SettingsItemList;
struct RD_SettingsItemList
{
  RD_SettingsItemNode *first;
  RD_SettingsItemNode *last;
  U64 count;
};

typedef struct RD_SettingsItemArray RD_SettingsItemArray;
struct RD_SettingsItemArray
{
  RD_SettingsItem *v;
  U64 count;
};

internal int
rd_qsort_compare_settings_item(RD_SettingsItem *a, RD_SettingsItem *b)
{
  int result = 0;
  if(a->string_matches.count > b->string_matches.count)
  {
    result = -1;
  }
  else if(a->string_matches.count < b->string_matches.count)
  {
    result = +1;
  }
  else if(a->kind_string_matches.count > b->kind_string_matches.count)
  {
    result = -1;
  }
  else if(a->kind_string_matches.count < b->kind_string_matches.count)
  {
    result = +1;
  }
  return result;
}

RD_VIEW_RULE_UI_FUNCTION_DEF(settings)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(0, 0);
  F32 row_height_px = floor_f32(ui_top_font_size()*2.5f);
  String8 query = string;
  RD_Window *window = rd_window_from_handle(rd_regs()->window);
  UI_ScrollPt2 scroll_pos = rd_view_scroll_pos();
  
  //////////////////////////////
  //- rjf: get state
  //
  typedef struct RD_SettingsViewState RD_SettingsViewState;
  struct RD_SettingsViewState
  {
    B32 initialized;
    Vec2S64 cursor;
    TxtPt txt_cursor;
    TxtPt txt_mark;
    U8 txt_buffer[1024];
    U64 txt_size;
    RD_ThemeColor color_ctx_menu_color;
    Vec4F32 color_ctx_menu_color_hsva;
    RD_ThemePreset preset_apply_confirm;
    B32 category_opened[RD_SettingsItemKind_COUNT];
  };
  RD_SettingsViewState *sv = rd_view_state(RD_SettingsViewState);
  if(!sv->initialized)
  {
    sv->initialized = 1;
    sv->preset_apply_confirm = RD_ThemePreset_COUNT;
  }
  
  //////////////////////////////
  //- rjf: gather all filtered settings items
  //
  RD_SettingsItemArray items = {0};
  {
    RD_SettingsItemList items_list = {0};
    
    //- rjf: global settings header
    if(query.size == 0)
    {
      RD_SettingsItemNode *n = push_array(scratch.arena, RD_SettingsItemNode, 1);
      SLLQueuePush(items_list.first, items_list.last, n);
      items_list.count += 1;
      n->v.kind = RD_SettingsItemKind_CategoryHeader;
      n->v.string = str8_lit("Global Interface Settings");
      n->v.icon_kind = sv->category_opened[RD_SettingsItemKind_GlobalSetting] ? RD_IconKind_DownCaret : RD_IconKind_RightCaret;
      n->v.category = RD_SettingsItemKind_GlobalSetting;
    }
    
    //- rjf: gather all global settings
    if(sv->category_opened[RD_SettingsItemKind_GlobalSetting] || query.size != 0)
    {
      for EachEnumVal(RD_SettingCode, code)
      {
        if(rd_setting_code_default_is_per_window_table[code])
        {
          continue;
        }
        String8 kind_string = str8_lit("Global Interface Setting");
        String8 string = rd_setting_code_display_string_table[code];
        FuzzyMatchRangeList kind_string_matches = fuzzy_match_find(scratch.arena, query, kind_string);
        FuzzyMatchRangeList string_matches = fuzzy_match_find(scratch.arena, query, string);
        if(string_matches.count == string_matches.needle_part_count ||
           kind_string_matches.count == kind_string_matches.needle_part_count)
        {
          RD_SettingsItemNode *n = push_array(scratch.arena, RD_SettingsItemNode, 1);
          SLLQueuePush(items_list.first, items_list.last, n);
          items_list.count += 1;
          n->v.kind = RD_SettingsItemKind_GlobalSetting;
          n->v.kind_string = kind_string;
          n->v.string = string;
          n->v.kind_string_matches = kind_string_matches;
          n->v.string_matches = string_matches;
          n->v.icon_kind = RD_IconKind_Window;
          n->v.code = code;
        }
      }
    }
    
    //- rjf: window settings header
    if(query.size == 0)
    {
      RD_SettingsItemNode *n = push_array(scratch.arena, RD_SettingsItemNode, 1);
      SLLQueuePush(items_list.first, items_list.last, n);
      items_list.count += 1;
      n->v.kind = RD_SettingsItemKind_CategoryHeader;
      n->v.string = str8_lit("Window Interface Settings");
      n->v.icon_kind = sv->category_opened[RD_SettingsItemKind_WindowSetting] ? RD_IconKind_DownCaret : RD_IconKind_RightCaret;
      n->v.category = RD_SettingsItemKind_WindowSetting;
    }
    
    //- rjf: gather all window settings
    if(sv->category_opened[RD_SettingsItemKind_WindowSetting] || query.size != 0)
    {
      for EachEnumVal(RD_SettingCode, code)
      {
        if(!rd_setting_code_default_is_per_window_table[code])
        {
          continue;
        }
        String8 kind_string = str8_lit("Window Interface Setting");
        String8 string = rd_setting_code_display_string_table[code];
        FuzzyMatchRangeList kind_string_matches = fuzzy_match_find(scratch.arena, query, kind_string);
        FuzzyMatchRangeList string_matches = fuzzy_match_find(scratch.arena, query, string);
        if(string_matches.count == string_matches.needle_part_count ||
           kind_string_matches.count == kind_string_matches.needle_part_count)
        {
          RD_SettingsItemNode *n = push_array(scratch.arena, RD_SettingsItemNode, 1);
          SLLQueuePush(items_list.first, items_list.last, n);
          items_list.count += 1;
          n->v.kind = RD_SettingsItemKind_WindowSetting;
          n->v.kind_string = kind_string;
          n->v.string = string;
          n->v.kind_string_matches = kind_string_matches;
          n->v.string_matches = string_matches;
          n->v.icon_kind = RD_IconKind_Window;
          n->v.code = code;
        }
      }
    }
    
    //- rjf: theme presets header
    if(query.size == 0)
    {
      RD_SettingsItemNode *n = push_array(scratch.arena, RD_SettingsItemNode, 1);
      SLLQueuePush(items_list.first, items_list.last, n);
      items_list.count += 1;
      n->v.kind = RD_SettingsItemKind_CategoryHeader;
      n->v.string = str8_lit("Theme Presets");
      n->v.icon_kind = sv->category_opened[RD_SettingsItemKind_ThemePreset] ? RD_IconKind_DownCaret : RD_IconKind_RightCaret;
      n->v.category = RD_SettingsItemKind_ThemePreset;
    }
    
    //- rjf: gather theme presets
    if(sv->category_opened[RD_SettingsItemKind_ThemePreset] || query.size != 0)
    {
      for EachEnumVal(RD_ThemePreset, preset)
      {
        String8 kind_string = str8_lit("Theme Preset");
        String8 string = rd_theme_preset_display_string_table[preset];
        FuzzyMatchRangeList kind_string_matches = fuzzy_match_find(scratch.arena, query, kind_string);
        FuzzyMatchRangeList string_matches = fuzzy_match_find(scratch.arena, query, string);
        if(string_matches.count == string_matches.needle_part_count ||
           kind_string_matches.count == kind_string_matches.needle_part_count)
        {
          RD_SettingsItemNode *n = push_array(scratch.arena, RD_SettingsItemNode, 1);
          SLLQueuePush(items_list.first, items_list.last, n);
          items_list.count += 1;
          n->v.kind = RD_SettingsItemKind_ThemePreset;
          n->v.kind_string = kind_string;
          n->v.string = string;
          n->v.kind_string_matches = kind_string_matches;
          n->v.string_matches = string_matches;
          n->v.icon_kind = RD_IconKind_Palette;
          n->v.preset = preset;
        }
      }
    }
    
    //- rjf: theme colors header
    if(query.size == 0)
    {
      RD_SettingsItemNode *n = push_array(scratch.arena, RD_SettingsItemNode, 1);
      SLLQueuePush(items_list.first, items_list.last, n);
      items_list.count += 1;
      n->v.kind = RD_SettingsItemKind_CategoryHeader;
      n->v.string = str8_lit("Theme Colors");
      n->v.icon_kind = sv->category_opened[RD_SettingsItemKind_ThemeColor] ? RD_IconKind_DownCaret : RD_IconKind_RightCaret;
      n->v.category = RD_SettingsItemKind_ThemeColor;
    }
    
    //- rjf: gather all theme colors
    if(sv->category_opened[RD_SettingsItemKind_ThemeColor] || query.size != 0)
    {
      for EachNonZeroEnumVal(RD_ThemeColor, color)
      {
        String8 kind_string = str8_lit("Theme Color");
        String8 string = rd_theme_color_display_string_table[color];
        FuzzyMatchRangeList kind_string_matches = fuzzy_match_find(scratch.arena, query, kind_string);
        FuzzyMatchRangeList string_matches = fuzzy_match_find(scratch.arena, query, string);
        if(string_matches.count == string_matches.needle_part_count ||
           kind_string_matches.count == kind_string_matches.needle_part_count)
        {
          RD_SettingsItemNode *n = push_array(scratch.arena, RD_SettingsItemNode, 1);
          SLLQueuePush(items_list.first, items_list.last, n);
          items_list.count += 1;
          n->v.kind = RD_SettingsItemKind_ThemeColor;
          n->v.kind_string = kind_string;
          n->v.string = string;
          n->v.kind_string_matches = kind_string_matches;
          n->v.string_matches = string_matches;
          n->v.icon_kind = RD_IconKind_Palette;
          n->v.color = color;
        }
      }
    }
    
    //- rjf: convert to array
    items.count = items_list.count;
    items.v = push_array(scratch.arena, RD_SettingsItem, items.count);
    {
      U64 idx = 0;
      for(RD_SettingsItemNode *n = items_list.first; n != 0; n = n->next, idx += 1)
      {
        items.v[idx] = n->v;
      }
    }
  }
  
  //////////////////////////////
  //- rjf: sort filtered settings item list
  //
  if(query.size != 0)
  {
    quick_sort(items.v, items.count, sizeof(items.v[0]), rd_qsort_compare_settings_item);
  }
  
  //////////////////////////////
  //- rjf: produce per-color context menu keys
  //
  UI_Key *color_ctx_menu_keys = push_array(scratch.arena, UI_Key, RD_ThemeColor_COUNT);
  {
    for(RD_ThemeColor color = (RD_ThemeColor)(RD_ThemeColor_Null+1);
        color < RD_ThemeColor_COUNT;
        color = (RD_ThemeColor)(color+1))
    {
      color_ctx_menu_keys[color] = ui_key_from_stringf(ui_key_zero(), "###settings_color_ctx_menu_%I64x", (U64)color);
    }
  }
  
  //////////////////////////////
  //- rjf: build color context menus
  //
  for(RD_ThemeColor color = (RD_ThemeColor)(RD_ThemeColor_Null+1);
      color < RD_ThemeColor_COUNT;
      color = (RD_ThemeColor)(color+1))
  {
    RD_Palette(RD_PaletteCode_Floating)
      UI_CtxMenu(color_ctx_menu_keys[color])
      UI_Padding(ui_em(1.5f, 1.f))
      UI_PrefWidth(ui_em(28.5f, 1)) UI_PrefHeight(ui_children_sum(1.f))
    {
      // rjf: build title
      UI_Row
      {
        ui_spacer(ui_em(1.5f, 1.f));
        UI_FlagsAdd(UI_BoxFlag_DrawTextWeak) ui_label(rd_theme_color_display_string_table[color]);
      }
      
      ui_spacer(ui_em(1.5f, 1.f));
      
      // rjf: build picker
      {
        ui_set_next_pref_height(ui_em(22.f, 1.f));
        UI_Row UI_Padding(ui_pct(1, 0))
        {
          UI_PrefWidth(ui_em(22.f, 1.f)) UI_PrefHeight(ui_em(22.f, 1.f)) UI_Flags(UI_BoxFlag_FocusNavSkip)
          {
            ui_sat_val_pickerf(sv->color_ctx_menu_color_hsva.x, &sv->color_ctx_menu_color_hsva.y, &sv->color_ctx_menu_color_hsva.z, "###settings_satval_picker");
          }
          
          ui_spacer(ui_em(0.75f, 1.f));
          
          UI_PrefWidth(ui_em(1.5f, 1.f)) UI_PrefHeight(ui_em(22.f, 1.f)) UI_Flags(UI_BoxFlag_FocusNavSkip)
            ui_hue_pickerf(&sv->color_ctx_menu_color_hsva.x, sv->color_ctx_menu_color_hsva.y, sv->color_ctx_menu_color_hsva.z, "###settings_hue_picker");
          
          UI_PrefWidth(ui_em(1.5f, 1.f)) UI_PrefHeight(ui_em(22.f, 1.f)) UI_Flags(UI_BoxFlag_FocusNavSkip)
            ui_alpha_pickerf(&sv->color_ctx_menu_color_hsva.w, "###settings_alpha_picker");
        }
      }
      
      ui_spacer(ui_em(1.5f, 1.f));
      
      // rjf: build line edits
      UI_Row
        UI_WidthFill
        UI_Padding(ui_em(1.5f, 1.f))
        UI_PrefHeight(ui_children_sum(1.f))
        UI_Column
        UI_PrefHeight(ui_em(2.25f, 1.f))
      {
        Vec4F32 hsva = sv->color_ctx_menu_color_hsva;
        Vec3F32 hsv = v3f32(hsva.x, hsva.y, hsva.z);
        Vec3F32 rgb = rgb_from_hsv(hsv);
        Vec4F32 rgba = v4f32(rgb.x, rgb.y, rgb.z, sv->color_ctx_menu_color_hsva.w);
        String8 hex_string = hex_string_from_rgba_4f32(scratch.arena, rgba);
        hex_string = push_str8f(scratch.arena, "#%S", hex_string);
        String8 r_string = push_str8f(scratch.arena, "%.2f", rgba.x);
        String8 g_string = push_str8f(scratch.arena, "%.2f", rgba.y);
        String8 b_string = push_str8f(scratch.arena, "%.2f", rgba.z);
        String8 h_string = push_str8f(scratch.arena, "%.2f", hsva.x);
        String8 s_string = push_str8f(scratch.arena, "%.2f", hsva.y);
        String8 v_string = push_str8f(scratch.arena, "%.2f", hsva.z);
        String8 a_string = push_str8f(scratch.arena, "%.2f", rgba.w);
        UI_Row RD_Font(RD_FontSlot_Code)
        {
          UI_FlagsAdd(UI_BoxFlag_DrawTextWeak) UI_PrefWidth(ui_em(4.5f, 1.f)) ui_labelf("Hex");
          UI_Signal sig = rd_line_editf(RD_LineEditFlag_Border, 0, 0, &sv->txt_cursor, &sv->txt_mark, sv->txt_buffer, sizeof(sv->txt_buffer), &sv->txt_size, 0, hex_string, "###hex_edit");
          if(ui_committed(sig))
          {
            String8 string = str8(sv->txt_buffer, sv->txt_size);
            Vec4F32 new_rgba = rgba_from_hex_string_4f32(string);
            Vec4F32 new_hsva = hsva_from_rgba(new_rgba);
            sv->color_ctx_menu_color_hsva = new_hsva;
          }
        }
        ui_spacer(ui_em(0.75f, 1.f));
        UI_Row
        {
          UI_FlagsAdd(UI_BoxFlag_DrawTextWeak) UI_PrefWidth(ui_em(4.5f, 1.f)) ui_labelf("R");
          UI_Signal sig = rd_line_editf(RD_LineEditFlag_Border, 0, 0, &sv->txt_cursor, &sv->txt_mark, sv->txt_buffer, sizeof(sv->txt_buffer), &sv->txt_size, 0, r_string, "###r_edit");
          if(ui_committed(sig))
          {
            String8 string = str8(sv->txt_buffer, sv->txt_size);
            Vec4F32 new_rgba = v4f32((F32)f64_from_str8(string), rgba.y, rgba.z, rgba.w);
            Vec4F32 new_hsva = hsva_from_rgba(new_rgba);
            sv->color_ctx_menu_color_hsva = new_hsva;
          }
        }
        UI_Row
        {
          UI_FlagsAdd(UI_BoxFlag_DrawTextWeak) UI_PrefWidth(ui_em(4.5f, 1.f)) ui_labelf("G");
          UI_Signal sig = rd_line_editf(RD_LineEditFlag_Border, 0, 0, &sv->txt_cursor, &sv->txt_mark, sv->txt_buffer, sizeof(sv->txt_buffer), &sv->txt_size, 0, g_string, "###g_edit");
          if(ui_committed(sig))
          {
            String8 string = str8(sv->txt_buffer, sv->txt_size);
            Vec4F32 new_rgba = v4f32(rgba.x, (F32)f64_from_str8(string), rgba.z, rgba.w);
            Vec4F32 new_hsva = hsva_from_rgba(new_rgba);
            sv->color_ctx_menu_color_hsva = new_hsva;
          }
        }
        UI_Row
        {
          UI_FlagsAdd(UI_BoxFlag_DrawTextWeak) UI_PrefWidth(ui_em(4.5f, 1.f)) ui_labelf("B");
          UI_Signal sig = rd_line_editf(RD_LineEditFlag_Border, 0, 0, &sv->txt_cursor, &sv->txt_mark, sv->txt_buffer, sizeof(sv->txt_buffer), &sv->txt_size, 0, b_string, "###b_edit");
          if(ui_committed(sig))
          {
            String8 string = str8(sv->txt_buffer, sv->txt_size);
            Vec4F32 new_rgba = v4f32(rgba.x, rgba.y, (F32)f64_from_str8(string), rgba.w);
            Vec4F32 new_hsva = hsva_from_rgba(new_rgba);
            sv->color_ctx_menu_color_hsva = new_hsva;
          }
        }
        ui_spacer(ui_em(0.75f, 1.f));
        UI_Row
        {
          UI_FlagsAdd(UI_BoxFlag_DrawTextWeak) UI_PrefWidth(ui_em(4.5f, 1.f)) ui_labelf("H");
          UI_Signal sig = rd_line_editf(RD_LineEditFlag_Border, 0, 0, &sv->txt_cursor, &sv->txt_mark, sv->txt_buffer, sizeof(sv->txt_buffer), &sv->txt_size, 0, h_string, "###h_edit");
          if(ui_committed(sig))
          {
            String8 string = str8(sv->txt_buffer, sv->txt_size);
            Vec4F32 new_hsva = v4f32((F32)f64_from_str8(string), hsva.y, hsva.z, hsva.w);
            sv->color_ctx_menu_color_hsva = new_hsva;
          }
        }
        UI_Row
        {
          UI_FlagsAdd(UI_BoxFlag_DrawTextWeak) UI_PrefWidth(ui_em(4.5f, 1.f)) ui_labelf("S");
          UI_Signal sig = rd_line_editf(RD_LineEditFlag_Border, 0, 0, &sv->txt_cursor, &sv->txt_mark, sv->txt_buffer, sizeof(sv->txt_buffer), &sv->txt_size, 0, s_string, "###s_edit");
          if(ui_committed(sig))
          {
            String8 string = str8(sv->txt_buffer, sv->txt_size);
            Vec4F32 new_hsva = v4f32(hsva.x, (F32)f64_from_str8(string), hsva.z, hsva.w);
            sv->color_ctx_menu_color_hsva = new_hsva;
          }
        }
        UI_Row
        {
          UI_FlagsAdd(UI_BoxFlag_DrawTextWeak) UI_PrefWidth(ui_em(4.5f, 1.f)) ui_labelf("V");
          UI_Signal sig = rd_line_editf(RD_LineEditFlag_Border, 0, 0, &sv->txt_cursor, &sv->txt_mark, sv->txt_buffer, sizeof(sv->txt_buffer), &sv->txt_size, 0, v_string, "###v_edit");
          if(ui_committed(sig))
          {
            String8 string = str8(sv->txt_buffer, sv->txt_size);
            Vec4F32 new_hsva = v4f32(hsva.x, hsva.y, (F32)f64_from_str8(string), hsva.w);
            sv->color_ctx_menu_color_hsva = new_hsva;
          }
        }
        ui_spacer(ui_em(0.75f, 1.f));
        UI_Row
        {
          UI_FlagsAdd(UI_BoxFlag_DrawTextWeak) UI_PrefWidth(ui_em(4.5f, 1.f)) ui_labelf("A");
          UI_Signal sig = rd_line_editf(RD_LineEditFlag_Border, 0, 0, &sv->txt_cursor, &sv->txt_mark, sv->txt_buffer, sizeof(sv->txt_buffer), &sv->txt_size, 0, a_string, "###a_edit");
          if(ui_committed(sig))
          {
            String8 string = str8(sv->txt_buffer, sv->txt_size);
            Vec4F32 new_hsva = v4f32(hsva.x, hsva.y, hsva.z, (F32)f64_from_str8(string));
            sv->color_ctx_menu_color_hsva = new_hsva;
          }
        }
      }
      
      // rjf: commit state to theme
      Vec4F32 hsva = sv->color_ctx_menu_color_hsva;
      Vec3F32 hsv = v3f32(hsva.x, hsva.y, hsva.z);
      Vec3F32 rgb = rgb_from_hsv(hsv);
      Vec4F32 rgba = v4f32(rgb.x, rgb.y, rgb.z, sv->color_ctx_menu_color_hsva.w);
      rd_state->cfg_theme_target.colors[sv->color_ctx_menu_color] = rgba;
    }
  }
  
  //////////////////////////////
  //- rjf: cancels
  //
  UI_Focus(UI_FocusKind_On) if(ui_is_focus_active() && sv->preset_apply_confirm < RD_ThemePreset_COUNT && ui_slot_press(UI_EventActionSlot_Cancel))
  {
    sv->preset_apply_confirm = RD_ThemePreset_COUNT;
  }
  
  //////////////////////////////
  //- rjf: build items list
  //
  Rng1S64 visible_row_range = {0};
  UI_ScrollListParams scroll_list_params = {0};
  {
    Vec2F32 rect_dim = dim_2f32(rect);
    scroll_list_params.flags         = UI_ScrollListFlag_All;
    scroll_list_params.row_height_px = row_height_px;
    scroll_list_params.dim_px        = v2f32(rect_dim.x, rect_dim.y);
    scroll_list_params.cursor_range  = r2s64(v2s64(0, 0), v2s64(0, items.count));
    scroll_list_params.item_range    = r1s64(0, items.count);
    scroll_list_params.cursor_min_is_empty_selection[Axis2_Y] = 1;
  }
  UI_ScrollListSignal scroll_list_sig = {0};
  UI_Focus(UI_FocusKind_On)
    UI_ScrollList(&scroll_list_params, &scroll_pos.y, &sv->cursor, 0, &visible_row_range, &scroll_list_sig)
    UI_Focus(UI_FocusKind_Null)
  {
    for(S64 row_num = visible_row_range.min; row_num <= visible_row_range.max && row_num < items.count; row_num += 1)
    {
      //- rjf: unpack item
      RD_SettingsItem *item = &items.v[row_num];
      UI_Palette *palette = ui_top_palette();
      Vec4F32 rgba = ui_top_palette()->text_weak;
      OS_Cursor cursor = OS_Cursor_HandPoint;
      Rng1S32 s32_range = {0};
      B32 is_toggler = 0;
      B32 is_toggled = 0;
      B32 is_slider = 0;
      S32 slider_s32_val = 0;
      F32 slider_pct = 0.f;
      UI_BoxFlags flags = UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawHotEffects|UI_BoxFlag_DrawActiveEffects;
      RD_SettingVal *val_table = &rd_state->cfg_setting_vals[RD_CfgSrc_User][0];
      switch(item->kind)
      {
        case RD_SettingsItemKind_COUNT:{}break;
        case RD_SettingsItemKind_CategoryHeader:
        {
          cursor = OS_Cursor_HandPoint;
          flags = UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawHotEffects;
        }break;
        case RD_SettingsItemKind_ThemePreset:
        {
          Vec4F32 *colors = rd_theme_preset_colors_table[item->preset];
          Vec4F32 bg_color = colors[RD_ThemeColor_BaseBackground];
          Vec4F32 tx_color = colors[RD_ThemeColor_Text];
          Vec4F32 tw_color = colors[RD_ThemeColor_TextWeak];
          Vec4F32 bd_color = colors[RD_ThemeColor_BaseBorder];
          palette = ui_build_palette(ui_top_palette(),
                                     .text = tx_color,
                                     .text_weak = tw_color,
                                     .border = bd_color,
                                     .background = bg_color);
        }break;
        case RD_SettingsItemKind_ThemeColor:
        {
          rgba = rd_rgba_from_theme_color(item->color);
        }break;
        case RD_SettingsItemKind_WindowSetting: {val_table = &window->setting_vals[0];}goto setting;
        case RD_SettingsItemKind_GlobalSetting:{}goto setting;
        setting:;
        {
          s32_range = rd_setting_code_s32_range_table[item->code];
          if(s32_range.min != 0 || s32_range.max != 1)
          {
            cursor = OS_Cursor_LeftRight;
            is_slider = 1;
            slider_s32_val = val_table[item->code].s32;
            slider_pct = (F32)(slider_s32_val - s32_range.min) / dim_1s32(s32_range);
          }
          else
          {
            is_toggler = 1;
            is_toggled = !!val_table[item->code].s32;
          }
        }break;
      }
      
      //- rjf: build item widget
      UI_Box *item_box = &ui_nil_box;
      UI_Row
      {
        if(query.size == 0 && item->kind != RD_SettingsItemKind_CategoryHeader)
        {
          ui_set_next_flags(UI_BoxFlag_DrawSideLeft);
          ui_spacer(ui_em(2.f, 1.f));
        }
        UI_Focus(row_num+1 == sv->cursor.y ? UI_FocusKind_On : UI_FocusKind_Off) UI_Palette(palette)
        {
          ui_set_next_hover_cursor(cursor);
          item_box = ui_build_box_from_stringf(UI_BoxFlag_Clickable|flags, "###option_%S_%S", item->kind_string, item->string);
          UI_Parent(item_box)
          {
            if(item->icon_kind != RD_IconKind_Null)
            {
              UI_PrefWidth(ui_em(3.f, 1.f))
                RD_Font(RD_FontSlot_Icons)
                UI_Palette(ui_build_palette(ui_top_palette(), .text = rgba))
                UI_TextAlignment(UI_TextAlign_Center)
                ui_label(rd_icon_kind_text_table[item->icon_kind]);
            }
            if(query.size != 0 && item->kind_string.size != 0) UI_PrefWidth(ui_text_dim(10, 1))
            {
              UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_DrawText|UI_BoxFlag_DrawTextWeak, "%S", item->kind_string);
              ui_box_equip_fuzzy_match_ranges(box, &item->kind_string_matches);
            }
            UI_PrefWidth(ui_text_dim(10, 1))
            {
              UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_DrawText, "%S", item->string);
              ui_box_equip_fuzzy_match_ranges(box, &item->string_matches);
            }
            if(is_slider) UI_PrefWidth(ui_text_dim(10, 1))
            {
              UI_Flags(UI_BoxFlag_DrawTextWeak)
                ui_labelf("(%i)", slider_s32_val);
              UI_PrefWidth(ui_pct(slider_pct, 1.f)) UI_HeightFill UI_FixedX(0) UI_FixedY(0)
                UI_Palette(ui_build_palette(ui_top_palette(), .background = rd_rgba_from_theme_color(RD_ThemeColor_HighlightOverlay)))
                ui_build_box_from_key(UI_BoxFlag_DrawBackground, ui_key_zero());
            }
            if(is_toggler)
            {
              ui_spacer(ui_pct(1, 0));
              UI_PrefWidth(ui_em(2.5f, 1.f))
                RD_Font(RD_FontSlot_Icons)
                UI_Flags(UI_BoxFlag_DrawTextWeak)
                ui_label(rd_icon_kind_text_table[is_toggled ? RD_IconKind_CheckFilled : RD_IconKind_CheckHollow]);
            }
            if(item->kind == RD_SettingsItemKind_ThemePreset && sv->preset_apply_confirm == item->preset)
            {
              ui_spacer(ui_pct(1, 0));
              UI_PrefWidth(ui_text_dim(10, 1))
                RD_Palette(RD_PaletteCode_NegativePopButton)
                UI_CornerRadius(ui_top_font_size()*0.5f)
                UI_FontSize(ui_top_font_size()*0.9f)
                UI_TextAlignment(UI_TextAlign_Center)
                ui_build_box_from_stringf(UI_BoxFlag_DrawText|UI_BoxFlag_DrawBackground, "Click Again To Apply");
            }
          }
        }
      }
      
      //- rjf: interact
      UI_Signal sig = ui_signal_from_box(item_box);
      if(item->kind == RD_SettingsItemKind_ThemeColor && ui_clicked(sig))
      {
        Vec3F32 rgb = v3f32(rgba.x, rgba.y, rgba.z);
        Vec3F32 hsv = hsv_from_rgb(rgb);
        Vec4F32 hsva = v4f32(hsv.x, hsv.y, hsv.z, rgba.w);
        ui_ctx_menu_open(color_ctx_menu_keys[item->color], item_box->key, v2f32(0, dim_2f32(item_box->rect).y));
        sv->color_ctx_menu_color = item->color;
        sv->color_ctx_menu_color_hsva = v4f32(hsv.x, hsv.y, hsv.z, rgba.w);
        rd_cmd(RD_CmdKind_FocusPanel);
      }
      if((item->kind == RD_SettingsItemKind_GlobalSetting || item->kind == RD_SettingsItemKind_WindowSetting) &&
         is_toggler && ui_clicked(sig))
      {
        val_table[item->code].s32 ^= 1;
        val_table[item->code].set = 1;
      }
      if((item->kind == RD_SettingsItemKind_GlobalSetting || item->kind == RD_SettingsItemKind_WindowSetting) &&
         is_slider && ui_dragging(sig))
      {
        if(ui_pressed(sig))
        {
          ui_store_drag_struct(&slider_s32_val);
        }
        S32 pre_drag_val = *ui_get_drag_struct(S32);
        Vec2F32 delta = ui_drag_delta();
        S32 pst_drag_val = pre_drag_val + (S32)(delta.x/(ui_top_font_size()*2.f));
        pst_drag_val = clamp_1s32(s32_range, pst_drag_val);
        val_table[item->code].s32 = pst_drag_val;
        val_table[item->code].set = 1;
      }
      if(item->kind == RD_SettingsItemKind_ThemePreset && ui_clicked(sig))
      {
        if(sv->preset_apply_confirm == item->preset)
        {
          Vec4F32 *colors = rd_theme_preset_colors_table[item->preset];
          MemoryCopy(rd_state->cfg_theme_target.colors, colors, sizeof(rd_state->cfg_theme_target.colors));
          sv->preset_apply_confirm = RD_ThemePreset_COUNT;
        }
        else
        {
          sv->preset_apply_confirm = item->preset;
        }
      }
      if(item->kind != RD_SettingsItemKind_ThemePreset && ui_pressed(sig))
      {
        sv->preset_apply_confirm = RD_ThemePreset_COUNT;
      }
      if(item->kind != RD_SettingsItemKind_ThemePreset && ui_pressed(sig))
      {
        sv->preset_apply_confirm = RD_ThemePreset_COUNT;
      }
      if(item->kind == RD_SettingsItemKind_CategoryHeader && ui_pressed(sig))
      {
        sv->category_opened[item->category] ^= 1;
      }
    }
  }
  
  rd_store_view_scroll_pos(scroll_pos);
  scratch_end(scratch);
  ProfEnd();
}