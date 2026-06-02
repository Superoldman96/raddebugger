// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#include "dwarf/dwarf.c"
// #include "dwarf/dwarf_parse.c"
// #include "dwarf/dwarf_expr.c"
#include "dwarf/dwarf_expr_2.c"
// #include "dwarf/dwarf_unwind.c"
// #include "dwarf/dwarf_dump.c"
#include "dwarf/dwarf_parse_2.c"
#include "dwarf/dwarf_unwind_2.c"
#include "dwarf/dwarf_dump_2.c"
#if defined(COFF_H)
# include "dwarf/dwarf_parse_coff.c"
#endif
#if defined(ELF_H)
# include "dwarf/dwarf_parse_elf.c"
#endif

#if 0 // TODO(rjf): this uses concepts from the old parser
# include "dwarf/dwarf_writer.c"
#endif
