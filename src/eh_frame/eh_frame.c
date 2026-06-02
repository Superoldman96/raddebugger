// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#if 0
internal U64
eh_parse_ptr(String8 frame_base, U64 off, U64 pc, EH_PtrCtx *ptr_ctx, EH_PtrEnc encoding, U64 *ptr_out)
{
  U64 ptr_off = off;
  
  if (encoding == EH_PtrEnc_Omit) {
    return 0;
  }
  
  // align read offset as needed
  if (encoding == EH_PtrEnc_Aligned) {
    ptr_off  = AlignPow2(ptr_off, ptr_ctx->ptr_align);
    encoding = EH_PtrEnc_Ptr;
  }
  
  // decode pointer value
  U64 decode_size  = 0;
  U64 raw_ptr_size = 0;
  U64 raw_ptr      = 0;
  switch (encoding & EH_PtrEnc_TypeMask) {
    default: { InvalidPath; } break;
    
    case EH_PtrEnc_Ptr   : { raw_ptr_size = 8; } goto ufixed;
    case EH_PtrEnc_UData2: { raw_ptr_size = 2; } goto ufixed;
    case EH_PtrEnc_UData4: { raw_ptr_size = 4; } goto ufixed;
    case EH_PtrEnc_UData8: { raw_ptr_size = 8; } goto ufixed;
    ufixed: {
      decode_size += str8_deserial_read(frame_base, ptr_off, &raw_ptr, raw_ptr_size, raw_ptr_size);
    } break;
    
    // TODO: Signed is actually just a flag that indicates this int is negavite.
    // There shouldn't be a read for Signed.
    // For instance, (EH_PtrEnc_UData2 | EH_PtrEnc_Signed) == EH_PtrEnc_SData etc.
    case EH_PtrEnc_Signed: { raw_ptr_size = 8; } goto sfixed; 
    
    case EH_PtrEnc_SData2: { raw_ptr_size = 2; } goto sfixed;
    case EH_PtrEnc_SData4: { raw_ptr_size = 4; } goto sfixed;
    case EH_PtrEnc_SData8: { raw_ptr_size = 8; } goto sfixed;
    sfixed: {
      decode_size += str8_deserial_read(frame_base, ptr_off, &raw_ptr, raw_ptr_size, raw_ptr_size);
      raw_ptr = extend_sign64(raw_ptr, raw_ptr_size);
    } break;
    
    case EH_PtrEnc_ULEB128: { decode_size += str8_deserial_read_uleb128(frame_base, ptr_off, &raw_ptr);       } break;
    case EH_PtrEnc_SLEB128: { decode_size += str8_deserial_read_sleb128(frame_base, ptr_off, (S64*)&raw_ptr); } break;
  }
  
  // apply relative bases
  if (decode_size > 0) {
    U64 ptr = raw_ptr;
    switch (encoding & EH_PtrEnc_ModifierMask) {
      case 0: break;
      case EH_PtrEnc_PcRel:   { ptr = pc + raw_ptr;                  } break;
      case EH_PtrEnc_TextRel: { ptr = ptr_ctx->text_vaddr + raw_ptr; } break;
      case EH_PtrEnc_DataRel: { ptr = ptr_ctx->data_vaddr + raw_ptr; } break;
      case EH_PtrEnc_FuncRel: {
        Assert(!"TODO: need a sample to verify implementation");
        ptr = ptr_ctx->func_vaddr + raw_ptr;
      } break;
      default: { InvalidPath; } break;
    }
    
    if (ptr_out) {
      *ptr_out = ptr;
    }
  }
  
  return decode_size;
}
#endif

internal EH_FrameHdr
eh_parse_frame_hdr(String8 data, U64 address_size, EH_PtrCtx *ptr_ctx)
{
  EH_FrameHdr header = {0};
  U64 cursor = 0;
  
  U64 version_size = str8_deserial_read_struct(data, cursor, &header.version);
  if (version_size == 0) { goto exit; }
  cursor += version_size;
  
  if (header.version == 1) {
    U64 eh_frame_ptr_enc_size = str8_deserial_read_struct(data, cursor, &header.eh_frame_ptr_enc);
    if (eh_frame_ptr_enc_size == 0) { goto exit; }
    cursor += eh_frame_ptr_enc_size;
    
    U64 fde_count_enc_size = str8_deserial_read_struct(data, cursor, &header.fde_count_enc);
    if (fde_count_enc_size == 0) { goto exit; }
    cursor += fde_count_enc_size;
    
    U64 table_enc_size = str8_deserial_read_struct(data, cursor, &header.table_enc);
    if (table_enc_size == 0) { goto exit; }
    cursor += table_enc_size;
    
    cursor += eh_read_ptr(data, cursor, cursor, ptr_ctx, header.eh_frame_ptr_enc, &header.eh_frame_ptr);
    cursor += eh_read_ptr(data, cursor, cursor, ptr_ctx, header.fde_count_enc, &header.fde_count);
    
    switch (header.table_enc & EH_PtrEnc_TypeMask) {
      case EH_PtrEnc_Ptr:     { header.field_byte_size = address_size; } break;
      case EH_PtrEnc_ULEB128: { InvalidPath;                           } break; // TODO: when loading module convert these to UData8
      case EH_PtrEnc_UData2:  { header.field_byte_size = 2;            } break;
      case EH_PtrEnc_UData4:  { header.field_byte_size = 4;            } break;
      case EH_PtrEnc_UData8:  { header.field_byte_size = 8;            } break;
      case EH_PtrEnc_Signed:  { header.field_byte_size = address_size; } break;
      case EH_PtrEnc_SLEB128: { InvalidPath;                           } break; // TODO: when loading module convert these to UData8
      case EH_PtrEnc_SData2:  { header.field_byte_size = 2;            } break;
      case EH_PtrEnc_SData4:  { header.field_byte_size = 4;            } break;
      case EH_PtrEnc_SData8:  { header.field_byte_size = 8;            } break;
      default: { InvalidPath; } break;
    }
    header.entry_byte_size = header.field_byte_size * 2;
    
    header.table = str8_skip(data, cursor);
    AssertAlways(header.table.size == header.entry_byte_size * header.fde_count);
  } else {
    Assert(0 && "unknown version");
  }
  
  exit:;
  return header;
}

#if 0
internal U64
eh_parse_aug_data(String8 aug_string, String8 aug_data, U64 pc, EH_PtrCtx *ptr_ctx, EH_Augmentation *aug_out)
{
  // TODO: 
  // Handle "eh" param, it indicates presence of EH Data field.
  // On 32bit arch it is a 4-byte and on 64-bit 8-byte value.
  // Reference: https://refspecs.linuxfoundation.org/LSB_3.0.0/LSB-PDA/LSB-PDA/ehframechpt.html
  // Reference doc doesn't clarify structure for EH Data though
  
  U64 cursor = 0;
  
  U64         aug_data_size    = 0;
  EH_AugFlags aug_flags        = 0;
  EH_PtrEnc   lsda_encoding    = EH_PtrEnc_Omit;
  EH_PtrEnc   addr_encoding    = EH_PtrEnc_UData8;
  EH_PtrEnc   handler_encoding = EH_PtrEnc_Omit;
  U64         handler_ip       = 0;
  if (str8_match(str8_prefix(aug_string, 1), str8_lit("z"), 0)) {
    cursor = str8_deserial_read_uleb128(aug_data, cursor, &aug_data_size);
    
    for (U8 *ptr = aug_string.str+1; ptr < (aug_string.str+aug_string.size); ptr += 1) {
      switch (*ptr) {
        case 'L': {
          cursor += str8_deserial_read_struct(aug_data, cursor, &lsda_encoding);
          aug_flags |= EH_AugFlag_HasLSDA;
        } break;
        case 'P': {
          cursor += str8_deserial_read_struct(aug_data, cursor, &handler_encoding);
          cursor += eh_parse_ptr(aug_data, cursor, pc + cursor, ptr_ctx, handler_encoding, &handler_ip);
          aug_flags |= EH_AugFlag_HasHandler;
        } break;
        case 'R': {
          cursor += str8_deserial_read_struct(aug_data, cursor, &addr_encoding);
          aug_flags |= EH_AugFlag_HasAddrEnc;
        } break;
        case 'S': {
          aug_flags |= EH_AugFlag_SignalFrame;
        } break;
        default: { Assert(!"failed to parse augmentation string"); goto exit; } break;
      }
    }
  }
  
  if (aug_out) {
    aug_out->handler_ip       = handler_ip;
    aug_out->handler_encoding = handler_encoding;
    aug_out->lsda_encoding    = handler_encoding;
    aug_out->addr_encoding    = addr_encoding;
    aug_out->flags            = aug_flags;
    aug_out->size             = aug_data_size;
  }
  
  exit:;
  U64 parse_size = cursor; 
  return parse_size;
}
#endif

internal U64
eh_read_ptr(String8 data, U64 off, U64 pc, EH_PtrCtx *ptr_ctx, EH_PtrEnc encoding, U64 *ptr_out)
{
  U64 start_off = off;
  if(encoding != EH_PtrEnc_Omit)
  {
    // rjf: apply aligned encoding & align pointer offset
    if(encoding == EH_PtrEnc_Aligned)
    {
      off = AlignPow2(off, ptr_ctx->ptr_align);
      encoding = EH_PtrEnc_Ptr;
    }
    U64 start_ptr_read_off = off;
    
    // rjf: decode pointer value
    U64 raw_ptr_size = 0;
    U64 raw_ptr = 0;
    switch(encoding & EH_PtrEnc_TypeMask)
    {
      default:{}break;
      
      // rjf: fixed-size pointer reads
      case EH_PtrEnc_Ptr:   {raw_ptr_size = 8;}goto ufixed;
      case EH_PtrEnc_UData2:{raw_ptr_size = 2;}goto ufixed;
      case EH_PtrEnc_UData4:{raw_ptr_size = 4;}goto ufixed;
      case EH_PtrEnc_UData8:{raw_ptr_size = 8;}goto ufixed;
      ufixed:
      {
        off += str8_deserial_read(data, off, &raw_ptr, raw_ptr_size, raw_ptr_size);
      }break;
      
      // TODO: Signed is actually just a flag that indicates this int is negavite.
      // There shouldn't be a read for Signed.
      // For instance, (EH_PtrEnc_UData2 | EH_PtrEnc_Signed) == EH_PtrEnc_SData etc.
      case EH_PtrEnc_Signed:{raw_ptr_size = 8;}goto sfixed; 
      
      // rjf: fixed-size pointer reads w/ sign extension
      case EH_PtrEnc_SData2:{raw_ptr_size = 2;}goto sfixed;
      case EH_PtrEnc_SData4:{raw_ptr_size = 4;}goto sfixed;
      case EH_PtrEnc_SData8:{raw_ptr_size = 8;}goto sfixed;
      sfixed:
      {
        off += str8_deserial_read(data, off, &raw_ptr, raw_ptr_size, raw_ptr_size);
        raw_ptr = extend_sign64(raw_ptr, raw_ptr_size);
      }break;
      
      // rjf: uleb128/sleb128
      case EH_PtrEnc_ULEB128:{off += str8_deserial_read_uleb128(data, off, &raw_ptr);      }break;
      case EH_PtrEnc_SLEB128:{off += str8_deserial_read_sleb128(data, off, (S64*)&raw_ptr);}break;
    }
    
    // rjf: apply relative bases
    U64 ptr = raw_ptr;
    if(off != start_ptr_read_off)
    {
      switch(encoding & EH_PtrEnc_ModifierMask)
      {
        default:{}break;
        case 0:{}break;
        case EH_PtrEnc_PcRel:  { ptr = pc + raw_ptr;                  } break;
        case EH_PtrEnc_TextRel:{ ptr = ptr_ctx->text_vaddr + raw_ptr; } break;
        case EH_PtrEnc_DataRel:{ ptr = ptr_ctx->data_vaddr + raw_ptr; } break;
        case EH_PtrEnc_FuncRel:
        {
          Assert(!"TODO: need a sample to verify implementation");
          ptr = ptr_ctx->func_vaddr + raw_ptr;
        }break;
      }
    }
    
    // rjf: fill output
    if(ptr_out)
    {
      *ptr_out = ptr;
    }
  }
  U64 bytes_read = (off - start_off);
  return bytes_read;
}

internal U64
eh_read_aug_data(String8 data, U64 off, String8 string, U64 pc, EH_PtrCtx *ptr_ctx, EH_Augmentation *aug_out)
{
  // TODO: 
  // Handle "eh" param, it indicates presence of EH Data field.
  // On 32bit arch it is a 4-byte and on 64-bit 8-byte value.
  // Reference: https://refspecs.linuxfoundation.org/LSB_3.0.0/LSB-PDA/LSB-PDA/ehframechpt.html
  // Reference doc doesn't clarify structure for EH Data though
  U64 start_off = off;
  U64 aug_data_size = 0;
  EH_AugFlags aug_flags = 0;
  EH_PtrEnc lsda_encoding = EH_PtrEnc_Omit;
  EH_PtrEnc addr_encoding = EH_PtrEnc_UData8;
  EH_PtrEnc handler_encoding = EH_PtrEnc_Omit;
  U64 handler_ip = 0;
  if(str8_match(str8_prefix(string, 1), str8_lit("z"), 0))
  {
    off += str8_deserial_read_uleb128(data, off, &aug_data_size);
    for(U8 *ptr = string.str+1; ptr < (string.str+string.size); ptr += 1)
    {
      switch(*ptr)
      {
        default:{goto exit;}break;
        case 'L':
        {
          off += str8_deserial_read_struct(data, off, &lsda_encoding);
          aug_flags |= EH_AugFlag_HasLSDA;
        }break;
        case 'P':
        {
          off += str8_deserial_read_struct(data, off, &handler_encoding);
          off += eh_read_ptr(data, off, pc + (off - start_off), ptr_ctx, handler_encoding, &handler_ip);
          aug_flags |= EH_AugFlag_HasHandler;
        }break;
        case 'R':
        {
          off += str8_deserial_read_struct(data, off, &addr_encoding);
          aug_flags |= EH_AugFlag_HasAddrEnc;
        }break;
        case 'S':
        {
          aug_flags |= EH_AugFlag_SignalFrame;
        }break;
      }
    }
  }
  if(aug_out != 0)
  {
    aug_out->handler_ip       = handler_ip;
    aug_out->handler_encoding = handler_encoding;
    aug_out->lsda_encoding    = handler_encoding;
    aug_out->addr_encoding    = addr_encoding;
    aug_out->flags            = aug_flags;
    aug_out->size             = aug_data_size;
  }
  exit:;
  U64 bytes_read = (off - start_off);
  return bytes_read;
}

internal U64
eh_read_cfi_header(String8 data, U64 off, DW_CFIHeader *cfi_header_out)
{
  U64 start_off = off;
  
  // rjf: read length / format
  U64 length = 0;
  DW_Format fmt = DW_Format_Null;
  U64 length_size = dw2_read_initial_length(data, off, &length, &fmt);
  
  // rjf: find entry info, read ID
  Rng1U64 entry_range = r1u64(off, off + length_size + length);
  String8 entry_data  = str8_substr(data, entry_range);
  U64 id = 0;
  U64 id_size = dw2_read_fmt_u64(entry_data, length_size, fmt, &id);
  
  // rjf: fill
  cfi_header_out->kind            = (id == 0) ? DW_CFIKind_CIE : DW_CFIKind_FDE;
  cfi_header_out->fmt             = fmt;
  cfi_header_out->entry_range     = entry_range;
  cfi_header_out->cie_pointer     = id;
  cfi_header_out->cie_pointer_off = off + length_size;
  
  U64 bytes_read = (off - start_off);
  return bytes_read;
}

internal U64
eh_read_cie(String8 data, U64 off, DW_Format fmt, Arch arch, U64 pc, EH_PtrCtx *ptr_ctx, DW_CIE *cie_out)
{
  U64 start_off = off;
  
  // rjf: skip format-dependent prefix
  off += (fmt == DW_Format_32Bit) ? 4 : 12;
  
  // rjf: read id
  U64 id = 0;
  off += dw2_read_fmt_u64(data, off, fmt, &id);
  
  // rjf: read version
  U8 version = 0;
  off += str8_deserial_read_struct(data, off, &version);
  
  // rjf: read entry info
  U64 aug_string_off_first = 0;
  U64 aug_string_off_opl = 0;
  U64 aug_data_off_first = 0;
  U64 aug_data_off_opl = 0;
  U64 code_align_factor = 0;
  S64 data_align_factor = 0;
  U64 ret_addr_reg = 0;
  EH_Augmentation aug = {0};
  if(version == 1)
  {
    // rjf: read aug string
    aug_string_off_first = off;
    String8 aug_string = {0};
    off += str8_deserial_read_cstr(data, off, &aug_string);
    aug_string_off_opl = off;
    
    // rjf: read eh data
    U64 eh_data = 0;
    if(str8_match(aug_string, s("eh"), 0))
    {
      U64 arch_byte_size = byte_size_from_arch(arch);
      off += str8_deserial_read(data, off, &eh_data, arch_byte_size, arch_byte_size);
    }
    
    // rjf: read align factors
    off += str8_deserial_read_uleb128(data, off, &code_align_factor);
    off += str8_deserial_read_sleb128(data, off, &data_align_factor);
    
    // rjf: read return address register
    off += str8_deserial_read(data, off, &ret_addr_reg, sizeof(U8), sizeof(U8));
    
    // rjf: parse augmentation
    aug_data_off_first = off;
    U64 aug_data_size = eh_read_aug_data(data, off, aug_string, pc + (off - start_off), ptr_ctx, &aug);
    aug_data_off_opl = off + aug.size;
    off += aug_data_size;
  }
  
  // rjf: fill output
  cie_out->aug_string_range      = r1u64(aug_string_off_first, aug_string_off_opl);
  cie_out->aug_data_range        = r1u64(aug_data_off_first, aug_data_off_opl);
  cie_out->code_align_factor     = code_align_factor;
  cie_out->data_align_factor     = data_align_factor;
  cie_out->ret_addr_reg          = ret_addr_reg;
  cie_out->format                = fmt;
  cie_out->version               = version;
  cie_out->address_size          = byte_size_from_arch(arch);
  cie_out->segment_selector_size = 0;
  cie_out->ext[EH_CIE_Ext_LSDAEnc]    = EH_PtrEnc_Omit;
  cie_out->ext[EH_CIE_Ext_AddrEnc]    = EH_PtrEnc_Omit;
  cie_out->ext[EH_CIE_Ext_HandlerEnc] = EH_PtrEnc_Omit;
  if(aug.flags & EH_AugFlag_HasLSDA)
  {
    cie_out->ext[EH_CIE_Ext_LSDAEnc] = aug.lsda_encoding;
  }
  if(aug.flags & EH_AugFlag_HasAddrEnc)
  {
    cie_out->ext[EH_CIE_Ext_AddrEnc] = aug.addr_encoding;
  }
  if(aug.flags & EH_AugFlag_HasHandler)
  {
    cie_out->ext[EH_CIE_Ext_HandlerEnc] = aug.handler_encoding;
    cie_out->ext[EH_CIE_Ext_HandlerIp ] = aug.handler_ip;
  }
  
  U64 bytes_read = (off - start_off);
  return bytes_read;
}

internal U64
eh_read_fde(String8 data, U64 off, DW_Format fmt, Arch arch, U64 pc, EH_PtrCtx *ptr_ctx, DW_CIE *cie, DW_FDE *fde_out)
{
  U64 start_off = off;
  
  // rjf: skip format-dependent prefix
  off += (fmt == DW_Format_32Bit) ? 8 : 20;
  
  // rjf: read pc begin / delta
  U64 pc_begin = 0;
  U64 pc_delta = 0;
  EH_PtrEnc addr_enc = cie->ext[EH_CIE_Ext_AddrEnc];
  if(addr_enc != EH_PtrEnc_Omit)
  {
    off += eh_read_ptr(data, off, pc + (off - start_off), ptr_ctx, addr_enc, &pc_begin);
    off += eh_read_ptr(data, off, pc + (off - start_off), ptr_ctx, addr_enc & EH_PtrEnc_TypeMask, &pc_delta);
  }
  
  // rjf: skip aug data
  off += dim_1u64(cie->aug_data_range);
  
  // rjf: fill output
  fde_out->pc_range = r1u64(pc_begin, pc_begin + pc_delta);
  
  U64 bytes_read = (off - start_off);
  return bytes_read;
}

#if 0
internal B32
eh_parse_fde(String8 data, DW_Format format, U64 pc, DW_CIE *cie, EH_PtrCtx *ptr_ctx, DW_FDE *fde_out)
{
  B32 is_parsed = 0;
  U64 cursor    = format == DW_Format_32Bit ? 8 : 20;
  
  U64       pc_begin = 0;
  U64       pc_delta = 0;
  EH_PtrEnc addr_enc = cie->ext[EH_CIE_Ext_AddrEnc];
  if (addr_enc != EH_PtrEnc_Omit) {
    U64 pc_begin_size = eh_parse_ptr(data, cursor, pc + cursor, ptr_ctx, addr_enc, &pc_begin);
    if (pc_begin_size == 0) { goto exit; }
    cursor += pc_begin_size;
    
    U64 pc_delta_size = eh_parse_ptr(data, cursor, pc + cursor, ptr_ctx, addr_enc & EH_PtrEnc_TypeMask, &pc_delta);
    if (pc_delta_size == 0) { goto exit; }
    cursor += pc_delta_size;
  }
  
  if (cursor + cie->aug_data.size > data.size) { goto exit; }
  cursor += cie->aug_data.size;
  
  fde_out->format   = format;
  fde_out->pc_range = rng_1u64(pc_begin, pc_begin + pc_delta);
  fde_out->insts    = str8_skip(data, cursor);
  
  is_parsed = 1;
  exit:;
  return is_parsed;
}
#endif

#if 0
internal U64
eh_find_nearest_fde(EH_FrameHdr header, EH_PtrCtx *ptr_ctx, U64 pc)
{
  U64 fde_addr = max_U64;
  U64 fde_idx  = max_U64;
  
  if (header.version == 1) {
    if (header.fde_count > 0) {
      U64 first = 0;
      U64 first_size = eh_parse_ptr(header.table, 0, ptr_ctx->pc_vaddr, ptr_ctx, header.table_enc, &first);
      AssertAlways(first_size);
      if (first == pc) {
        fde_idx = 0;
        goto exit;
      }
      if (first > pc) {
        goto exit;
      }
      
      U64 last_off  = header.table.size - header.entry_byte_size;
      U64 last      = 0;
      U64 last_size = eh_parse_ptr(header.table, last_off, ptr_ctx->pc_vaddr + last_off, ptr_ctx, header.table_enc, &last);
      AssertAlways(last_size);
      if (last <= pc) {
        fde_idx = header.fde_count - 1;
        goto exit;
      }
      
      U64 l = 0;
      U64 r = header.fde_count - 1;
      while (l <= r) {
        U64 m         = l + (r - l) / 2;
        U64 m_pc_off  = m * header.entry_byte_size;
        U64 m_pc      = 0;
        U64 m_pc_size = eh_parse_ptr(header.table, m_pc_off, ptr_ctx->pc_vaddr + m_pc_off, ptr_ctx, header.table_enc, &m_pc);
        Assert(m_pc_size);
        if (m_pc > pc) {
          r = m - 1;
        } else if (m_pc < pc) {
          l = m + 1;
        } else {
          fde_idx = m;
          goto exit;
        }
      }
      
      fde_idx = l > 0 ? l-1 : 0;
    }
  }
  
  exit:;
  if (fde_idx < header.fde_count) {
    U64 fde_addr_off  = (fde_idx * header.entry_byte_size) + header.field_byte_size;
    U64 fde_addr_size = eh_parse_ptr(header.table, fde_addr_off, ptr_ctx->pc_vaddr + fde_addr_off, ptr_ctx, header.table_enc, &fde_addr);
    Assert(fde_addr_size);
  }
  return fde_addr;
}
#endif

internal int
eh_frame_hdr_entry_sort(void *raw_a, void *raw_b)
{
  return ((EH_FrameHdrEntry *)raw_a)->addr < ((EH_FrameHdrEntry *)raw_b)->addr;
}

internal String8
eh_frame_hdr_from_call_frame_info(Arena *arena, U64 fde_count, U64 *fde_offsets, DW_FDE *fde)
{
  Temp scratch = scratch_begin(&arena, 1);
  
  // make .eh_frame_hdr
  String8List srl = {0};
  str8_serial_begin(scratch.arena, &srl);
  str8_serial_push_u8(scratch.arena, &srl, 1);                // version
  str8_serial_push_u8(scratch.arena, &srl, EH_PtrEnc_Omit);   // omit eh_frame_ptr field
  str8_serial_push_u8(scratch.arena, &srl, EH_PtrEnc_UData8); // fde_count encoding
  str8_serial_push_u8(scratch.arena, &srl, EH_PtrEnc_UData8); // table encoding
  str8_serial_push_u64(scratch.arena, &srl, fde_count);   // fde_count
  String8 header = str8_serial_end(scratch.arena, &srl);
  
  // alloc buffer for output
  U64 buffer_size = header.size + sizeof(EH_FrameHdrEntry) * fde_count;
  U8 *buffer      = push_array(arena, U8, buffer_size);
  
  // copy header
  MemoryCopyStr8(buffer, header);
  
  // write the table
  EH_FrameHdrEntry *table = (EH_FrameHdrEntry *)(buffer + header.size);
  for EachIndex(fde_idx, fde_count) {
    table[fde_idx].addr       = fde[fde_idx].pc_range.min;
    table[fde_idx].fde_offset = fde_offsets[fde_idx];
  }
  radsort(table, fde_count, eh_frame_hdr_entry_sort);
  
  String8 eh_frame_hdr = str8(buffer, buffer_size);
  scratch_end(scratch);
  return eh_frame_hdr;
}

#if 0
internal
DW_DECODE_PTR(eh_decode_ptr)
{
  EH_DecodePtrCtx *ctx = ud;
  return eh_parse_ptr(data, 0, ctx->ptr_ctx->pc_vaddr, ctx->ptr_ctx, ctx->addr_enc, ptr_out);
}
#endif

internal String8
eh_string_from_ptr_enc_type(EH_PtrEnc type)
{
  switch (type) {
    case EH_PtrEnc_Ptr:     return str8_lit("Ptr");
    case EH_PtrEnc_ULEB128: return str8_lit("ULEB128");
    case EH_PtrEnc_UData2:  return str8_lit("UData2");
    case EH_PtrEnc_UData4:  return str8_lit("UData4");
    case EH_PtrEnc_UData8:  return str8_lit("UData8");
    case EH_PtrEnc_Signed:  return str8_lit("Signed");
    case EH_PtrEnc_SLEB128: return str8_lit("SLEB128");
    case EH_PtrEnc_SData2:  return str8_lit("SData2");
    case EH_PtrEnc_SData4:  return str8_lit("SData4");
    case EH_PtrEnc_SData8:  return str8_lit("SData8");
  }
  return str8_zero();
}

internal String8
eh_string_from_ptr_enc_modifier(EH_PtrEnc modifier)
{
  switch (modifier) {
    case EH_PtrEnc_PcRel:   return str8_lit("PcRel");
    case EH_PtrEnc_TextRel: return str8_lit("TextRel");
    case EH_PtrEnc_DataRel: return str8_lit("DataRel");
    case EH_PtrEnc_FuncRel: return str8_lit("FuncRel");
    case EH_PtrEnc_Aligned: return str8_lit("Aligned");
  }
  return str8_zero();
}

internal String8
eh_string_from_ptr_enc(Arena *arena, EH_PtrEnc enc)
{
  String8 type_str    = eh_string_from_ptr_enc_type(enc & EH_PtrEnc_TypeMask);
  String8 modifer_str = eh_string_from_ptr_enc_modifier(enc & EH_PtrEnc_ModifierMask);
  String8 indir_str   = enc & EH_PtrEnc_Indirect ? str8_lit("Indirect") : str8_zero();
  String8 result      = str8f(arena, "Type: %S, Modifier %S (%S)", type_str, modifer_str, indir_str);
  return result;
}
