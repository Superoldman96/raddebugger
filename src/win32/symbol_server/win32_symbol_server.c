// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal void
smsv_init(void)
{
  Arena *arena = arena_alloc();
  w32_smsv_state = push_array(arena, W32_SMSV_State, 1);
  w32_smsv_state->arena = arena;
  {
    // rjf: extract parameterizations from _NT_SYMBOL_PATH
    {
      // rjf: get rules string from environment
      String8 rules = {0};
      for EachNode(n, String8Node, get_process_info()->environment.first)
      {
        String8 string = n->string;
        if(str8_match(string, s("_NT_SYMBOL_PATH="), StringMatchFlag_RightSideSloppy))
        {
          rules = str8_skip(string, 16);
        }
      }
      
      // rjf: parse rules string
      {
        Temp scratch = scratch_begin(0, 0);
        if(str8_match(rules, s("srv*"), StringMatchFlag_RightSideSloppy))
        {
          String8List parts = str8_split(scratch.arena, rules, (U8 *)"*", 1, 0);
          for EachNode(n, String8Node, parts.first)
          {
            PathStyle style = path_style_from_str8(n->string);
            if(w32_smsv_state->symbol_cache_path.size != 0 && style == PathStyle_WindowsAbsolute)
            {
              w32_smsv_state->symbol_cache_path = n->string;
            }
            else if(str8_match(n->string, s("https://"), StringMatchFlag_RightSideSloppy) ||
                    str8_match(n->string, s("http://"), StringMatchFlag_RightSideSloppy))
            {
              str8_list_push(arena, &w32_smsv_state->symbol_server_urls, n->string);
            }
            else if(str8_match(n->string, s("\\\\"), StringMatchFlag_RightSideSloppy))
            {
              // TODO(rjf): network share; currently unsupported
            }
          }
        }
        else
        {
          w32_smsv_state->symbol_cache_path = str8_skip_chop_whitespace(rules);
        }
        scratch_end(scratch);
      }
    }
    
    // rjf: always fall back to public MS server
    {
      String8 public_ms_server = s("https://msdl.microsoft.com/download/symbols");
      B32 public_ms_server_already_present = 0;
      for EachNode(n, String8Node, w32_smsv_state->symbol_server_urls.first)
      {
        if(str8_match(public_ms_server, n->string, 0))
        {
          public_ms_server_already_present = 1;
          break;
        }
      }
      if(!public_ms_server_already_present)
      {
        str8_list_push(arena, &w32_smsv_state->symbol_server_urls, public_ms_server);
      }
    }
    
    // rjf fall back to using local appdata temp symbol cache folder
    if(w32_smsv_state->symbol_cache_path.size == 0)
    {
      Temp scratch = scratch_begin(0, 0);
      U64 size = KB(32);
      U16 *buffer = push_array_no_zero(scratch.arena, U16, size);
      if(SUCCEEDED(SHGetFolderPathW(0, CSIDL_LOCAL_APPDATA, 0, 0, (WCHAR*)buffer)))
      {
        String8 local_appdata = str8_from_16(scratch.arena, str16_cstring(buffer));
        w32_smsv_state->symbol_cache_path = str8f(arena, "%S\\Temp\\SymbolCache", local_appdata);
      }
      scratch_end(scratch);
    }
  }
}

internal String8
smsv_local_path_from_key(Arena *arena, String8 module_name, String8 dbg_name, Guid guid, U64 age)
{
  String8 result = {0};
  Temp scratch = scratch_begin(&arena, 1);
  {
    // rjf: form unique identifier based on guid
    String8 debug_info_unique_identifier = {0};
    {
      debug_info_unique_identifier = str8f(scratch.arena, "%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%x",
                                           guid.data1, guid.data2, guid.data3, guid.data4[0], guid.data4[1], guid.data4[2], guid.data4[3], guid.data4[4], guid.data4[5], guid.data4[6], guid.data4[7], age);
    }
    
    // rjf: form full path
    if(debug_info_unique_identifier.size != 0)
    {
      // TODO(rjf): assuming 'stripped' for now
      String8 symbol_cache_path = w32_smsv_state->symbol_cache_path;
      String8 path = str8f(scratch.arena, "%S/%S/%S/stripped/%S", symbol_cache_path, module_name, debug_info_unique_identifier, dbg_name);
      result = path_normalized_from_string(arena, path);
    }
  }
  scratch_end(scratch);
  return result;
}
