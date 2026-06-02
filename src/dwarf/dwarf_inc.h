// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef DWARF_INC_H
#define DWARF_INC_H

#include "dwarf/dwarf.h"
// #include "dwarf/dwarf_parse.h"
// #include "dwarf/dwarf_expr.h"
#include "dwarf/dwarf_expr_2.h"
// #include "dwarf/dwarf_unwind.h"
// #include "dwarf/dwarf_dump.h"
#include "dwarf/dwarf_parse_2.h"
#include "dwarf/dwarf_unwind_2.h"
#include "dwarf/dwarf_dump_2.h"
#if defined(COFF_H)
# include "dwarf/dwarf_parse_coff.h"
#endif
#if defined(ELF_H)
# include "dwarf/dwarf_parse_elf.h"
#endif

#if 0 // TODO(rjf): this uses concepts from the old parser
# include "dwarf/dwarf_writer.h"
#endif

#endif // DWARF_INC_H
