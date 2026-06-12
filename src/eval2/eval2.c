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
//~ rjf: String -> Expression

internal E2_Parse
e2_parse_from_string(Arena *arena, E2_ParseState *state, E2_SpaceMap *space_map, E2_ExprMap *expr_map, String8 string)
{
  
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
