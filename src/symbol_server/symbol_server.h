// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef SYMBOL_SERVER_H
#define SYMBOL_SERVER_H

internal void smsv_init(void);
internal String8 smsv_local_path_from_key(Arena *arena, String8 module_name, String8 dbg_name, Guid guid, U64 age);

#endif // SYMBOL_SERVER_H
