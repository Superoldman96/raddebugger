// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef EVAL2_H
#define EVAL2_H

////////////////////////////////
//~ rjf: Generated Code

#include "eval2/generated/eval2.meta.h"

////////////////////////////////
//~ rjf: Extended Opcodes

enum
{
  E2_EvalOp_SetCtxID = RDI_EvalOp_COUNT,
};

////////////////////////////////
//~ rjf: Compilation Statuses

typedef enum E2_Status
{
  E2_Status_Good,
  
  //- rjf: need caller-provided info
  E2_Status_MissedSpaceRead,
  E2_Status_MissedIdentifierResolution,
  E2_Status_Access,
  E2_Status_NewCtxID,
  
  //- rjf: unrecoverable errors
  E2_Status_BadRegCode,
  E2_Status_MissingCtxFlag,
  E2_Status_DivideByZero,
  E2_Status_InsufficientStackSpace,
  E2_Status_BadOpTypes,
  E2_Status_UnsupportedOp,
  E2_Status_BadOffset,
  E2_Status_Error,
}
E2_Status;

////////////////////////////////
//~ rjf: Type Keys

typedef enum E2_TypeKeyKind
{
  E2_TypeKeyKind_Null,
  E2_TypeKeyKind_Basic,
  E2_TypeKeyKind_Ext,
  E2_TypeKeyKind_Cons,
  E2_TypeKeyKind_Reg,
}
E2_TypeKeyKind;

typedef struct E2_TypeKey E2_TypeKey;
struct E2_TypeKey
{
  E2_TypeKeyKind kind;
  U32 u32[3];
  // [0] -> E_TypeKind (Basic, Cons, Ext); Arch (Reg, RegAlias)
  // [1] -> Type Index In Debug Info (Ext); Code (Reg, RegAlias); Type Index In Constructed (Cons)
  // [2] -> Debug Info Number (Ext)
};

typedef struct E2_TypeKeyNode E2_TypeKeyNode;
struct E2_TypeKeyNode
{
  E2_TypeKeyNode *next;
  E2_TypeKey v;
};

typedef struct E2_TypeKeyList E2_TypeKeyList;
struct E2_TypeKeyList
{
  E2_TypeKeyNode *first;
  E2_TypeKeyNode *last;
  U64 count;
};

////////////////////////////////
//~ rjf: Unpacked Type Info

typedef enum E2_MemberKind
{
  E2_MemberKind_Null,
  E2_MemberKind_DataField,
  E2_MemberKind_StaticData,
  E2_MemberKind_Method,
  E2_MemberKind_StaticMethod,
  E2_MemberKind_VirtualMethod,
  E2_MemberKind_VTablePtr,
  E2_MemberKind_Base,
  E2_MemberKind_VirtualBase,
  E2_MemberKind_NestedType,
  E2_MemberKind_Padding,
  E2_MemberKind_COUNT
}
E2_MemberKind;

typedef U32 E2_TypeFlags;
enum
{
  E2_TypeFlag_Const    = (1<<0),
  E2_TypeFlag_Volatile = (1<<1),
  E2_TypeFlag_Restrict = (1<<2),
};

typedef struct E2_EnumVal E2_EnumVal;
struct E2_EnumVal
{
  String8 name;
  U64 val;
};

typedef struct E2_Member E2_Member;
struct E2_Member
{
  E2_MemberKind kind;
  E2_TypeKey type_key;
  String8 name;
  U64 off;
  E2_TypeKeyList inheritees;
};

typedef struct E2_Type E2_Type;
struct E2_Type
{
  E2_TypeKind kind;
  E2_TypeFlags flags;
  String8 name;
  U64 byte_size;
  U64 count;
  U64 depth;
  U32 off;
  Arch arch;
  E2_TypeKey direct_type_key;
  E2_TypeKey owner_type_key;
  E2_TypeKey *param_type_keys;
  E2_Member *members;
  E2_EnumVal *enum_vals;
  struct E2_Expr **args;
};

////////////////////////////////
//~ rjf: Evaluation Values

typedef enum E2_Mode
{
  E2_Mode_Type,     // no value content - just type content
  E2_Mode_Address,  // value is an address into a space
  E2_Mode_Value,    // value is just a value, does not refer elsewhere
  E2_Mode_COUNT
}
E2_Mode;

typedef struct E2_Val E2_Val;
struct E2_Val
{
  union
  {
    U512 u512;
    U256 u256;
    U128 u128;
    U64 u64;
    U32 u32;
    U16 u16;
    U8 u8;
    S64 s64;
    S32 s32;
    S16 s16;
    S8 s8;
    F64 f64;
    F32 f32;
  };
  String8 string;
};

////////////////////////////////
//~ rjf: Evaluation "Asset" Parameters (Debug Info / Modules)

typedef U64 E2_SpaceID;

typedef struct E2_DbgInfo E2_DbgInfo;
struct E2_DbgInfo
{
  DI_Key dbgi_key;
  RDI_Parsed *rdi;
  String8 name;
};

typedef struct E2_Module E2_Module;
struct E2_Module
{
  E2_SpaceID space_id;
  Rng1U64 addr_range;
  E2_DbgInfo *dbg_info;
  Arch arch;
  String8 name;
};

typedef struct E2_Assets E2_Assets;
struct E2_Assets
{
  E2_Module *modules;
  U64 modules_count;
  E2_DbgInfo *dbg_infos;
  U64 dbg_infos_count;
};

////////////////////////////////
//~ rjf: Evaluation Contexts

typedef U64 E2_CtxID;

typedef U32 E2_CtxFlags;
enum
{
  E2_CtxFlag_HasFrameBase  = (1<<0),
  E2_CtxFlag_HasCFA        = (1<<1),
  E2_CtxFlag_HasModuleBase = (1<<2),
  E2_CtxFlag_HasTLSBase    = (1<<3),
};

typedef struct E2_Ctx E2_Ctx;
struct E2_Ctx
{
  // rjf: base info
  E2_CtxID id;
  E2_SpaceID space_id;
  E2_SpaceID reg_space_id;
  Arch arch;
  U64 addr;
  
  // rjf: optional extensions
  E2_CtxFlags flags;
  U64 frame_base_addr;
  U64 cfa_addr;
  U64 module_base_addr;
  U64 tls_base_addr;
};

////////////////////////////////
//~ rjf: Expression Tree Building

typedef struct E2_Expr E2_Expr;
struct E2_Expr
{
  E2_Expr *first;
  E2_Expr *last;
  E2_Expr *next;
  String8 string;
  RDI_EvalOp op;
  E2_TypeKey type_key;
  E2_Mode mode;
  E2_Val val;
};

typedef struct E2_ExprMapNode E2_ExprMapNode;
struct E2_ExprMapNode
{
  E2_ExprMapNode *next;
  String8 name;
  E2_Expr *expr;
};

typedef struct E2_ExprMap E2_ExprMap;
struct E2_ExprMap
{
  E2_ExprMapNode **slots;
  U64 slots_count;
};

typedef struct E2_ParseState E2_ParseState;
struct E2_ParseState
{
  U64 string_off;
};

typedef struct E2_Parse E2_Parse;
struct E2_Parse
{
  E2_Status status;
  E2_SpaceID space_id;
  Rng1U64 missed_read_space_addr_range;
  E2_CtxID ctx_id;
  E2_CtxFlags missing_ctx_flags;
  E2_Expr *expr;
  E2_Expr *access_expr;
};

////////////////////////////////
//~ rjf: Interpretation

typedef struct E2_SpaceMapNode E2_SpaceMapNode;
struct E2_SpaceMapNode
{
  E2_SpaceMapNode *next;
  E2_SpaceID space_id;
  MemoryMap memory_map;
};

typedef struct E2_SpaceMap E2_SpaceMap;
struct E2_SpaceMap
{
  E2_SpaceMapNode **slots;
  U64 slots_count;
};

typedef struct E2_InterpStackValNode E2_InterpStackValNode;
struct E2_InterpStackValNode
{
  E2_InterpStackValNode *next;
  E2_Val val;
};

typedef struct E2_InterpState E2_InterpState;
struct E2_InterpState
{
  U64 bytecode_off;
  E2_Ctx selected_ctx;
  E2_InterpStackValNode *top_val;
  E2_InterpStackValNode *free_val;
};

typedef struct E2_Interp E2_Interp;
struct E2_Interp
{
  E2_Status status;
  E2_SpaceID space_id;
  Rng1U64 missed_read_space_addr_range;
  E2_CtxID ctx_id;
  E2_CtxFlags missing_ctx_flags;
  E2_Val val;
};

////////////////////////////////
//~ rjf: Globals

read_only global E2_Expr e2_expr_nil = {&e2_expr_nil, &e2_expr_nil, &e2_expr_nil};

////////////////////////////////
//~ rjf: Space -> Memory Map Helpers

internal void e2_space_map_push(Arena *arena, E2_SpaceMap *map, E2_SpaceID space_id, Rng1U64 addr_range, void *data);
internal U64 e2_space_map_read(E2_SpaceMap *map, E2_SpaceID space_id, Rng1U64 addr_range, void *out);

////////////////////////////////
//~ rjf: Name -> Expr Map Helpers

internal void e2_expr_map_push(Arena *arena, E2_ExprMap *map, String8 name, E2_Expr *expr);
internal E2_Expr *e2_expr_from_name(E2_ExprMap *map, String8 name);

////////////////////////////////
//~ rjf: String -> Expression

internal E2_Parse e2_parse_from_string(Arena *arena, E2_ParseState *state, E2_SpaceMap *space_map, E2_ExprMap *expr_map, String8 string);

////////////////////////////////
//~ rjf: Expression -> Bytecode

internal String8 e2_bytecode_from_expr(Arena *arena, E2_Expr *expr);

////////////////////////////////
//~ rjf: Bytecode -> Result

internal E2_Interp e2_interp_from_bytecode(Arena *arena, E2_InterpState *state, E2_SpaceMap *space_map, String8 bytecode);

#endif // EVAL2_H
