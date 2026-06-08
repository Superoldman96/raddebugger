// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef WIN32_SYMBOL_SERVER_H
#define WIN32_SYMBOL_SERVER_H

typedef struct W32_SMSV_State W32_SMSV_State;
struct W32_SMSV_State
{
  Arena *arena;
  String8 symbol_cache_path;
  String8List symbol_server_urls;
};

global W32_SMSV_State *w32_smsv_state = 0;

#endif // WIN32_SYMBOL_SERVER_H
