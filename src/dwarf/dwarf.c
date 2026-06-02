// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Includes

#include "dwarf/generated/dwarf.meta.c"
#if defined(X64_H)
# include "dwarf/x64/dwarf_x64.c"
#endif

////////////////////////////////
//~ rjf: X-List Helpers

//- rjf: format address sizes

internal U64
dw_addr_size_from_format(DW_Format fmt)
{
  U64 result = 0;
  switch(fmt)
  {
    default:{}break;
#define X(name, string, addr_size) case DW_Format_##name:{result = (addr_size);}break;
    DW_Format_XList
#undef X
  }
  return result;
}

//- rjf: attribute classes

internal DW_AttribClass
dw_attrib_class_from_kind(DW_Version version, DW_ExtFlags exts, DW_AttribKind k)
{
  DW_AttribClass result = 0;
  if(result == 0) switch(k)
  {
    default:{}break;
#define X(name, code, version, classes) case DW_AttribKind_##name:{result = (classes);}break;
    DW_AttribKind_XList
#undef X
  }
  for(B32 retry = 0; !result && retry <= 1; retry += 1)
  {
    if(result == 0 && exts & DW_ExtFlag_GNU) switch(k)
    {
      default:{}break;
#define X(name, code, classes) case DW_GNU_AttribKind_##name:{result = (classes);}break;
      DW_GNU_AttribKind_XList
#undef X
    }
    if(result == 0 && exts & DW_ExtFlag_LLVM) switch(k)
    {
      default:{}break;
#define X(name, code, classes) case DW_LLVM_AttribKind_##name:{result = (classes);}break;
      DW_LLVM_AttribKind_XList
#undef X
    }
    if(result == 0 && exts & DW_ExtFlag_Apple) switch(k)
    {
      default:{}break;
#define X(name, code, classes) case DW_Apple_AttribKind_##name:{result = (classes);}break;
      DW_Apple_AttribKind_XList
#undef X
    }
    if(result == 0 && exts & DW_ExtFlag_MIPS) switch(k)
    {
      default:{}break;
#define X(name, code, classes) case DW_MIPS_AttribKind_##name:{result = (classes);}break;
      DW_MIPS_AttribKind_XList
#undef X
    }
    exts = DW_ExtFlag_All;
  }
  return result;
}

internal DW_AttribClass
dw_attrib_class_from_form_kind(DW_Version version, DW_ExtFlags exts, DW_FormKind k)
{
  DW_AttribClass result = 0;
  if(result == 0) switch(k)
  {
    default:{}break;
#define X(name, code, version, classes) case DW_FormKind_##name:{result = (classes);}break;
    DW_FormKind_XList
#undef X
  }
  for(B32 retry = 0; !result && retry <= 1; retry += 1)
  {
    if(result == 0 && exts & DW_ExtFlag_GNU) switch(k)
    {
      default:{}break;
#define X(name, code, classes) case DW_GNU_FormKind_##name:{result = (classes);}break;
      DW_GNU_FormKind_XList
#undef X
    }
    exts = DW_ExtFlag_All;
  }
  return result;
}

//- rjf: expr op metadata

internal DW_ExprOpInfo *
dw_info_from_expr_op(DW_Version version, DW_ExtFlags exts, DW_ExprOp op)
{
  DW_ExprOpInfo *result = &dw_expr_op_info_nil;
  if(result == &dw_expr_op_info_nil) switch(op)
  {
    default:{}break;
#define X(name_, code, version_, operand_count_, pop_count_, push_count_, operand_kind_0, operand_kind_1, valid_for_cfa_exprs_) case DW_ExprOp_##name_:{local_persist DW_ExprOpInfo info = {.push_count = (push_count_), .pop_count = (pop_count_), .operand_count = (operand_count_), .valid_for_cfa_exprs = (valid_for_cfa_exprs_), .operand_kinds = {DW_ExprOperandKind_##operand_kind_0, DW_ExprOperandKind_##operand_kind_1}, .name = str8_lit_comp(#name_), .version = DW_Version_##version_}; result = &info;}break;
    DW_ExprOp_XList
#undef X
  }
  for(B32 retry = 0; (result == &dw_expr_op_info_nil) && retry <= 1; retry += 1)
  {
    if((result == &dw_expr_op_info_nil) && exts & DW_ExtFlag_GNU) switch(op)
    {
      default:{}break;
#define X(name_, code, operand_count_, pop_count_, push_count_, operand_kind_0, operand_kind_1, valid_for_cfa_exprs_) case DW_GNU_ExprOp_##name_:{local_persist DW_ExprOpInfo info = {.push_count = (push_count_), .pop_count = (pop_count_), .operand_count = (operand_count_), .valid_for_cfa_exprs = (valid_for_cfa_exprs_), .operand_kinds = {DW_ExprOperandKind_##operand_kind_0, DW_ExprOperandKind_##operand_kind_1}, .name = str8_lit_comp(#name_)}; result = &info;}break;
      DW_GNU_ExprOp_XList
#undef X
    }
    exts = DW_ExtFlag_All;
  }
  return result;
}

//- rjf: enum -> string

internal String8
dw_string_from_format(DW_Format fmt)
{
  String8 result = {0};
  switch(fmt)
  {
    default:{}break;
#define X(name, string, addr_size) case DW_Format_##name:{result = str8_lit(string);}break;
    DW_Format_XList
#undef X
  }
  return result;
}

internal String8
dw_string_from_tag_kind(DW_Version version, DW_ExtFlags exts, DW_TagKind k)
{
  String8 result = {0};
  if(result.size == 0) switch(k)
  {
    default:{}break;
#define X(name, code, version) case DW_TagKind_##name:{result = s(#name);}break;
    DW_TagKind_XList
#undef X
  }
  for(B32 retry = 0; !result.size && retry <= 1; retry += 1)
  {
    if(result.size == 0 && exts & DW_ExtFlag_GNU) switch(k)
    {
#define X(name, code) case DW_GNU_TagKind_##name:{result = s(#name);}break;
      DW_GNU_TagKind_XList
#undef X
    }
    exts = DW_ExtFlag_All;
  }
  return result;
}

internal String8
dw_string_from_attrib_kind(DW_Version version, DW_ExtFlags exts, DW_AttribKind k)
{
  String8 result = {0};
  if(result.size == 0) switch(k)
  {
    default:{}break;
#define X(name, code, version, classes) case DW_AttribKind_##name:{result = s(#name);}break;
    DW_AttribKind_XList
#undef X
  }
  for(B32 retry = 0; !result.size && retry <= 1; retry += 1)
  {
    if(result.size == 0 && exts & DW_ExtFlag_GNU) switch(k)
    {
      default:{}break;
#define X(name, code, classes) case DW_GNU_AttribKind_##name:{result = s(#name);}break;
      DW_GNU_AttribKind_XList
#undef X
    }
    if(result.size == 0 && exts & DW_ExtFlag_LLVM) switch(k)
    {
      default:{}break;
#define X(name, code, classes) case DW_LLVM_AttribKind_##name:{result = s(#name);}break;
      DW_LLVM_AttribKind_XList
#undef X
    }
    if(result.size == 0 && exts & DW_ExtFlag_Apple) switch(k)
    {
      default:{}break;
#define X(name, code, classes) case DW_Apple_AttribKind_##name:{result = s(#name);}break;
      DW_Apple_AttribKind_XList
#undef X
    }
    if(result.size == 0 && exts & DW_ExtFlag_MIPS) switch(k)
    {
      default:{}break;
#define X(name, code, classes) case DW_MIPS_AttribKind_##name:{result = s(#name);}break;
      DW_MIPS_AttribKind_XList
#undef X
    }
    exts = DW_ExtFlag_All;
  }
  return result;
}

internal String8
dw_string_from_form_kind(DW_Version version, DW_ExtFlags exts, DW_FormKind k)
{
  String8 result = {0};
  if(result.size == 0) switch(k)
  {
    default:{}break;
#define X(name, code, version, classes) case DW_FormKind_##name:{result = s(#name);}break;
    DW_FormKind_XList
#undef X
  }
  for(B32 retry = 0; !result.size && retry <= 1; retry += 1)
  {
    if(result.size == 0 && exts & DW_ExtFlag_GNU) switch(k)
    {
      default:{}break;
#define X(name, code, classes) case DW_GNU_FormKind_##name:{result = s(#name);}break;
      DW_GNU_FormKind_XList
#undef X
    }
    exts = DW_ExtFlag_All;
  }
  return result;
}

internal String8
dw_string_from_language(DW_Language k)
{
  String8 result = {0};
  switch(k)
  {
    default:{}break;
#define X(name, ...) case DW_Language_##name:{result = s(#name);}break;
    DW_Language_XList
#undef X
  }
  return result;
}

internal String8
dw_string_from_comp_unit_kind(DW_CompUnitKind k)
{
  String8 result = {0};
  switch(k)
  {
    default:{}break;
#define X(name, ...) case DW_CompUnitKind_##name:{result = s(#name);}break;
    DW_CompUnitKind_XList
#undef X
  }
  return result;
}

internal String8
dw_string_from_inl_kind(DW_InlKind k)
{
  String8 result = {0};
  switch(k)
  {
    default:{}break;
#define X(name, ...) case DW_InlKind_##name:{result = s(#name);}break;
    DW_InlKind_XList
#undef X
  }
  return result;
}

internal String8
dw_string_from_access_kind(DW_AccessKind k)
{
  String8 result = {0};
  switch(k)
  {
    default:{}break;
#define X(name, ...) case DW_AccessKind_##name:{result = s(#name);}break;
    DW_AccessKind_XList
#undef X
  }
  return result;
}

internal String8
dw_string_from_calling_convention_kind(DW_CallingConventionKind k)
{
  String8 result = {0};
  switch(k)
  {
    default:{}break;
#define X(name, ...) case DW_CallingConventionKind_##name:{result = s(#name);}break;
    DW_CallingConventionKind_XList
#undef X
  }
  return result;
}

internal String8
dw_string_from_ate(DW_ATE k)
{
  String8 result = {0};
  switch(k)
  {
    default:{}break;
#define X(name, ...) case DW_ATE_##name:{result = s(#name);}break;
    DW_ATE_XList
#undef X
  }
  return result;
}

internal String8
dw_string_from_visibility_kind(DW_VisibilityKind k)
{
  String8 result = {0};
  switch(k)
  {
    default:{}break;
#define X(name, ...) case DW_VisibilityKind_##name:{result = s(#name);}break;
    DW_VisibilityKind_XList
#undef X
  }
  return result;
}

internal String8
dw_string_from_std_opcode(DW_StdOpcode opcode)
{
  String8 result = {0};
  switch(opcode)
  {
    default:{}break;
#define X(name, ...) case DW_StdOpcode_##name:{result = s(#name);}break;
    DW_StdOpcode_XList
#undef X
  }
  return result;
}

internal String8
dw_string_from_ext_opcode(DW_ExtOpcode opcode)
{
  String8 result = {0};
  switch(opcode)
  {
    default:{}break;
#define X(name, ...) case DW_ExtOpcode_##name:{result = s(#name);}break;
    DW_ExtOpcode_XList
#undef X
  }
  return result;
}

internal String8
dw_string_from_lle(DW_LLE lle)
{
  String8 result = {0};
  switch(lle)
  {
    default:{}break;
#define X(name, ...) case DW_LLE_##name:{result = s(#name);}break;
    DW_LLE_XList
#undef X
  }
  return result;
}

internal String8
dw_string_from_rle(DW_RLE rle)
{
  String8 result = {0};
  switch(rle)
  {
    default:{}break;
#define X(name, ...) case DW_RLE_##name:{result = s(#name);}break;
    DW_RLE_XList
#undef X
  }
  return result;
}

internal String8
dw_regular_name_from_section_kind(DW_SectionKind k)
{
  String8 result = {0};
  switch(k)
  {
    default:{}break;
#define X(name, regular_name, ...) case DW_SectionKind_##name:{result = str8_lit(regular_name);}break;
    DW_SectionKind_XList
#undef X
  }
  return result;
}

internal String8
dw_macho_name_from_section_kind(DW_SectionKind k)
{
  String8 result = {0};
  switch(k)
  {
    default:{}break;
#define X(name, regular_name, macho_name, ...) case DW_SectionKind_##name:{result = str8_lit(macho_name);}break;
    DW_SectionKind_XList
#undef X
  }
  return result;
}

internal String8
dw_dwo_name_from_section_kind(DW_SectionKind k)
{
  String8 result = {0};
  switch(k)
  {
    default:{}break;
#define X(name, regular_name, macho_name, dwo_name, ...) case DW_SectionKind_##name:{result = str8_lit(dwo_name);}break;
    DW_SectionKind_XList
#undef X
  }
  return result;
}

//- rjf: architecture info

internal U64
dw_reg_count_from_arch(Arch arch)
{
  U64 result = 0;
  switch(arch)
  {
    default:{}break;
    case Arch_x64:{result = DW_RegCodeX64_LAST;}break;
  }
  return result;
}

////////////////////////////////
//~ TODO(rjf): OLD vvvvvvvvvvv

#if 0
internal DW_AttribClass
dw_attrib_class_from_form_kind(DW_Version ver, DW_FormKind k)
{
  DW_AttribClass result = 0;
  switch(k)
  {
    default:{}break;
#define X(name, code, version, ext, class) case DW_FormKind_##name:{result = (class);}break;
    DW_FormKind_XList
#undef X
  }
  return result;
}

internal U64
dw_reg_size_from_code(Arch arch, DW_Reg reg_code)
{
  switch (arch) {
    case Arch_Null: break;
    case Arch_x64: return dw_reg_size_from_code_x64(reg_code);
    default: NotImplemented; break;
  }
  return 0;
}

internal U64
dw_reg_pos_from_code(Arch arch, DW_Reg reg_code)
{
  switch (arch) {
    case Arch_Null: break;
    case Arch_x64: return dw_reg_pos_from_code_x64(reg_code);
    default: NotImplemented; break;
  }
  return max_U64;
}

internal U64
dw_reg_count_from_arch(Arch arch)
{
  switch (arch) {
    default: { NotImplemented; } // fall-through
    case Arch_Null: return 0;
    case Arch_x64: return DW_RegX64_Last;
  }
}

internal U64
dw_reg_max_size_from_arch(Arch arch)
{
  local_persist U64 max_size = 0;
  if (max_size == 0) {
    U64 max_idx  = dw_reg_count_from_arch(arch);
    for EachIndex(reg_idx, max_idx) {
      U64 reg_size = dw_reg_size_from_code(arch, reg_idx);
      max_size = Max(max_size, reg_size);
    }
  }
  return max_size;
}

internal U64
dw_operand_count_from_cfa_opcode(DW_CFA_Opcode opcode)
{
  switch (opcode) {
#define X(_N, _ID, ...) case _ID: { local_persist DW_CFA_OperandType t[] = { DW_CFA_OperandType_Null, __VA_ARGS__ }; return ArrayCount(t)-1; }
    DW_CFA_Kind_XList
#undef X
    default: { NotImplemented; } break;
  }
  return 0;
}

internal B32
dw_is_cfa_expr_opcode_invalid(DW_ExprOp opcode)
{
  B32 is_invalid = 0;
  switch(opcode)
  {
    default:{}break;
    case DW_ExprOp_Addrx:
    case DW_ExprOp_Call2:
    case DW_ExprOp_Call4:
    case DW_ExprOp_CallRef:
    case DW_ExprOp_ConstType:
    case DW_ExprOp_Constx:
    case DW_ExprOp_Convert:
    case DW_ExprOp_DerefType:
    case DW_ExprOp_RegvalType:
    case DW_ExprOp_Reinterpret:
    case DW_ExprOp_PushObjectAddress:
    case DW_ExprOp_CallFrameCfa:
    {
      is_invalid = 1;
    }break;
  }
  return is_invalid;
}

internal B32
dw_is_new_row_cfa_opcode(DW_CFA_Opcode opcode)
{
  B32 is_new_row_op = 0;
  switch(opcode)
  {
    default:{}break;
    case DW_CFA_SetLoc:
    case DW_CFA_AdvanceLoc:
    case DW_CFA_AdvanceLoc1:
    case DW_CFA_AdvanceLoc2:
    case DW_CFA_AdvanceLoc4:
    {
      is_new_row_op = 1;
    }break;
  }
  return is_new_row_op;
}

internal DW_CFA_OperandType *
dw_operand_types_from_cfa_op(DW_CFA_Opcode opcode)
{
  switch (opcode) {
#define X(_N, _ID, ...) case _ID: { local_persist DW_CFA_OperandType t[] = { DW_CFA_OperandType_Null, __VA_ARGS__ }; return &t[0] + 1; }
    DW_CFA_Kind_XList
#undef X
    default: { NotImplemented; } break;
  }
  return 0;
}

internal String8
dw_string_from_language(Arena *arena, DW_Language kind)
{
  switch (kind) {
#define X(_N,_ID) case DW_Language_##_N: return str8_lit(Stringify(_N));
    DW_Language_XList
#undef X
  }
  return push_str8f(arena, "%x", kind);
}

internal String8
dw_string_from_comp_unit_kind(Arena *arena, DW_CompUnitKind kind)
{
  switch (kind) {
#define X(_N,_ID) case DW_CompUnitKind_##_N: return str8_lit(Stringify(_N));
    DW_CompUnitKind_XList
#undef X
  }
  return push_str8f(arena, "%x", kind);
}

internal String8
dw_string_from_inl(Arena *arena, DW_InlKind kind)
{
  switch (kind) {
#define X(_N,_ID) case _ID: return str8_lit(Stringify(_N));
    DW_Inl_XList
#undef X
  }
  return push_str8f(arena, "%x", kind);
}

internal String8
dw_string_from_access_kind(Arena *arena, DW_AccessKind kind)
{
  switch (kind) {
#define X(_N,_ID) case _ID: return str8_lit(Stringify(_N));
    DW_AccessKind_XList
#undef X
  }
  return push_str8f(arena, "%llx", kind);
}

internal String8
dw_string_from_calling_convetion(Arena *arena, DW_CallingConventionKind kind)
{
  switch (kind) {
#define X(_N,_ID) case _ID: return str8_lit(Stringify(_N));
    DW_CallingConventionKind_XList
#undef X
  }
  return push_str8f(arena, "%llx", kind);
}

internal String8
dw_string_from_attrib_type_encoding(Arena *arena, DW_ATE kind)
{
  switch (kind) {
#define X(_N,_ID) case _ID: return str8_lit(Stringify(_N));
    DW_ATE_XList
#undef X
  }
  return push_str8f(arena, "%llx", kind);
}

internal String8
dw_string_from_attrib_visibility(Arena *arena, DW_Vis vis)
{
  switch (vis) {
#define X(_N,_ID) case _ID: return str8_lit(Stringify(_N));
    DW_Vis_XList
#undef X
  }
  return push_str8f(arena, "%llx", vis);
}

internal String8
dw_string_from_std_opcode(Arena *arena, DW_StdOpcode kind)
{
  switch (kind) {
#define X(_N,_ID) case DW_StdOpcode_##_N: return str8_lit(Stringify(_N));
    DW_StdOpcode_XList
#undef X
  }
  return push_str8f(arena, "%x", kind);
}

internal String8
dw_string_from_ext_opcode(Arena *arena, DW_ExtOpcode kind)
{
  switch (kind) {
#define X(_N,_ID) case DW_ExtOpcode_##_N: return str8_lit(Stringify(_N));
    DW_ExtOpcode_XList
#undef X
    default: InvalidPath; break;
  }
  return push_str8f(arena, "%x", kind);
}

internal String8
dw_string_from_loc_list_entry_kind(Arena *arena, DW_LLE kind)
{
  NotImplemented;
  return str8_zero();
}

internal String8
dw_string_from_section_kind(Arena *arena, DW_SectionKind kind)
{
  NotImplemented;
  return str8_zero();
}

internal String8
dw_string_from_rng_list_entry_kind(Arena *arena, DW_RLE kind)
{
  NotImplemented;
  return str8_zero();
}

internal String8
dw_string_from_register(Arena *arena, Arch arch, U64 reg_id)
{
  String8 reg_str = str8_zero();
  switch (arch) {
    case Arch_Null: break;
    case Arch_x64: {
      switch (reg_id) {
#define X(_N, _ID, ...) case DW_RegX64_##_N: reg_str = str8_lit(Stringify(_N)); break;
        DW_Regs_X64_XList
#undef X
      }
    } break;
    case Arch_arm32: NotImplemented; break;
    case Arch_arm64: NotImplemented; break;
    case Arch_x86: NotImplemented; break;
    default: InvalidPath; break;
  }
  if (reg_str.size == 0) {
    reg_str = push_str8f(arena, "%#llx", reg_id);
  }
  return reg_str;
}

internal String8
dw_string_from_cfa_opcode(DW_CFA_Opcode opcode)
{
  switch (opcode) {
#define X(_NAME, _ID, ...) case _ID: return str8_lit(Stringify(_NAME));
    DW_CFA_Kind_XList
#undef X
    default: InvalidPath; break;
  }
  return str8_zero();
}

internal String8
dw_name_string_from_section_kind(DW_SectionKind k)
{
  switch (k) {
#define X(_N,_L,_M,_D) case DW_Section_##_N: return str8_lit(_L);
    DW_SectionKind_XList
#undef X
  }
  return str8_zero();
}

internal String8
dw_mach_name_string_from_section_kind(DW_SectionKind k)
{
  switch (k) {
#define X(_N,_L,_M,_D) case DW_Section_##_N: return str8_lit(_M);
    DW_SectionKind_XList
#undef X
  }
  return str8_zero();
}

internal String8
dw_dwo_name_string_from_section_kind(DW_SectionKind k)
{
  switch (k) {
#define X(_N,_L,_M,_D) case DW_Section_##_N: return str8_lit(_D); 
    DW_SectionKind_XList
#undef X
  }
  return str8_zero();
}
#endif
