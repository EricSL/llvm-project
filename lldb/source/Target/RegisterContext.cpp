//===-- RegisterContext.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/RegisterContext.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Value.h"
#include "lldb/Expression/DWARFExpression.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Scalar.h"

using namespace lldb;
using namespace lldb_private;

RegisterContext::RegisterContext(Thread &thread, uint32_t concrete_frame_idx)
    : m_thread(thread), m_concrete_frame_idx(concrete_frame_idx),
      m_stop_id(thread.GetProcess()->GetStopID()) {}

RegisterContext::~RegisterContext() = default;

void RegisterContext::InvalidateIfNeeded(bool force) {
  ProcessSP process_sp(m_thread.GetProcess());
  bool invalidate = force;
  uint32_t process_stop_id = UINT32_MAX;

  if (process_sp)
    process_stop_id = process_sp->GetStopID();
  else
    invalidate = true;

  if (!invalidate)
    invalidate = process_stop_id != GetStopID();

  if (invalidate) {
    InvalidateAllRegisters();
    SetStopID(process_stop_id);
  }
}

const RegisterInfo *
RegisterContext::GetRegisterInfoByName(llvm::StringRef reg_name,
                                       uint32_t start_idx) {
  if (reg_name.empty())
    return nullptr;

  const uint32_t num_registers = GetRegisterCount();
  for (uint32_t reg = start_idx; reg < num_registers; ++reg) {
    const RegisterInfo *reg_info = GetRegisterInfoAtIndex(reg);

    if (reg_name.equals_lower(reg_info->name) ||
        reg_name.equals_lower(reg_info->alt_name))
      return reg_info;
  }
  return nullptr;
}

uint32_t
RegisterContext::UpdateDynamicRegisterSize(const lldb_private::ArchSpec &arch,
                                           RegisterInfo *reg_info) {
  ExecutionContext exe_ctx(CalculateThread());

  // In MIPS, the floating point registers size is depends on FR bit of SR
  // register. if SR.FR  == 1 then all floating point registers are 64 bits.
  // else they are all 32 bits.

  int expr_result;
  uint32_t addr_size = arch.GetAddressByteSize();
  const uint8_t *dwarf_opcode_ptr = reg_info->dynamic_size_dwarf_expr_bytes;
  const size_t dwarf_opcode_len = reg_info->dynamic_size_dwarf_len;

  DataExtractor dwarf_data(dwarf_opcode_ptr, dwarf_opcode_len,
                           arch.GetByteOrder(), addr_size);
  ModuleSP opcode_ctx;
  DWARFExpression dwarf_expr(opcode_ctx, dwarf_data, nullptr, 0,
                             dwarf_opcode_len);
  Value result;
  Status error;
  const lldb::offset_t offset = 0;
  if (dwarf_expr.Evaluate(&exe_ctx, this, opcode_ctx, dwarf_data, nullptr,
                          offset, dwarf_opcode_len, eRegisterKindDWARF, nullptr,
                          nullptr, 0, result, &error)) { // TODO(paolosev)
    expr_result = result.GetScalar().SInt(-1);
    switch (expr_result) {
    case 0:
      return 4;
    case 1:
      return 8;
    default:
      return reg_info->byte_size;
    }
  } else {
    printf("Error executing DwarfExpression::Evaluate %s\n", error.AsCString());
    return reg_info->byte_size;
  }
}

const RegisterInfo *RegisterContext::GetRegisterInfo(lldb::RegisterKind kind,
                                                     uint32_t num) {
  const uint32_t reg_num = ConvertRegisterKindToRegisterNumber(kind, num);
  if (reg_num == LLDB_INVALID_REGNUM)
    return nullptr;
  return GetRegisterInfoAtIndex(reg_num);
}

const char *RegisterContext::GetRegisterName(uint32_t reg) {
  const RegisterInfo *reg_info = GetRegisterInfoAtIndex(reg);
  if (reg_info)
    return reg_info->name;
  return nullptr;
}

uint64_t RegisterContext::GetPC(uint64_t fail_value) {
  uint32_t reg = ConvertRegisterKindToRegisterNumber(eRegisterKindGeneric,
                                                     LLDB_REGNUM_GENERIC_PC);
  uint64_t pc = ReadRegisterAsUnsigned(reg, fail_value);

  if (pc != fail_value) {
    TargetSP target_sp = m_thread.CalculateTarget();
    if (target_sp) {
      Target *target = target_sp.get();
      if (target)
        pc = target->GetOpcodeLoadAddress(pc, AddressClass::eCode);
    }
  }

  return pc;
}

bool RegisterContext::SetPC(uint64_t pc) {
  uint32_t reg = ConvertRegisterKindToRegisterNumber(eRegisterKindGeneric,
                                                     LLDB_REGNUM_GENERIC_PC);
  bool success = WriteRegisterFromUnsigned(reg, pc);
  if (success) {
    StackFrameSP frame_sp(
        m_thread.GetFrameWithConcreteFrameIndex(m_concrete_frame_idx));
    if (frame_sp)
      frame_sp->ChangePC(pc);
    else
      m_thread.ClearStackFrames();
  }
  return success;
}

bool RegisterContext::SetPC(Address addr) {
  TargetSP target_sp = m_thread.CalculateTarget();
  Target *target = target_sp.get();

  lldb::addr_t callAddr = addr.GetCallableLoadAddress(target);
  if (callAddr == LLDB_INVALID_ADDRESS)
    return false;

  return SetPC(callAddr);
}

uint64_t RegisterContext::GetSP(uint64_t fail_value) {
  uint32_t reg = ConvertRegisterKindToRegisterNumber(eRegisterKindGeneric,
                                                     LLDB_REGNUM_GENERIC_SP);
  return ReadRegisterAsUnsigned(reg, fail_value);
}

bool RegisterContext::SetSP(uint64_t sp) {
  uint32_t reg = ConvertRegisterKindToRegisterNumber(eRegisterKindGeneric,
                                                     LLDB_REGNUM_GENERIC_SP);
  return WriteRegisterFromUnsigned(reg, sp);
}

uint64_t RegisterContext::GetFP(uint64_t fail_value) {
  uint32_t reg = ConvertRegisterKindToRegisterNumber(eRegisterKindGeneric,
                                                     LLDB_REGNUM_GENERIC_FP);
  return ReadRegisterAsUnsigned(reg, fail_value);
}

bool RegisterContext::SetFP(uint64_t fp) {
  uint32_t reg = ConvertRegisterKindToRegisterNumber(eRegisterKindGeneric,
                                                     LLDB_REGNUM_GENERIC_FP);
  return WriteRegisterFromUnsigned(reg, fp);
}

uint64_t RegisterContext::GetReturnAddress(uint64_t fail_value) {
  uint32_t reg = ConvertRegisterKindToRegisterNumber(eRegisterKindGeneric,
                                                     LLDB_REGNUM_GENERIC_RA);
  return ReadRegisterAsUnsigned(reg, fail_value);
}

uint64_t RegisterContext::GetFlags(uint64_t fail_value) {
  uint32_t reg = ConvertRegisterKindToRegisterNumber(eRegisterKindGeneric,
                                                     LLDB_REGNUM_GENERIC_FLAGS);
  return ReadRegisterAsUnsigned(reg, fail_value);
}

uint64_t RegisterContext::ReadRegisterAsUnsigned(uint32_t reg,
                                                 uint64_t fail_value) {
  if (reg != LLDB_INVALID_REGNUM)
    return ReadRegisterAsUnsigned(GetRegisterInfoAtIndex(reg), fail_value);
  return fail_value;
}

uint64_t RegisterContext::ReadRegisterAsUnsigned(const RegisterInfo *reg_info,
                                                 uint64_t fail_value) {
  if (reg_info) {
    RegisterValue value;
    if (ReadRegister(reg_info, value))
      return value.GetAsUInt64();
  }
  return fail_value;
}

bool RegisterContext::WriteRegisterFromUnsigned(uint32_t reg, uint64_t uval) {
  if (reg == LLDB_INVALID_REGNUM)
    return false;
  return WriteRegisterFromUnsigned(GetRegisterInfoAtIndex(reg), uval);
}

bool RegisterContext::WriteRegisterFromUnsigned(const RegisterInfo *reg_info,
                                                uint64_t uval) {
  if (reg_info) {
    RegisterValue value;
    if (value.SetUInt(uval, reg_info->byte_size))
      return WriteRegister(reg_info, value);
  }
  return false;
}

bool RegisterContext::CopyFromRegisterContext(lldb::RegisterContextSP context) {
  uint32_t num_register_sets = context->GetRegisterSetCount();
  // We don't know that two threads have the same register context, so require
  // the threads to be the same.
  if (context->GetThreadID() != GetThreadID())
    return false;

  if (num_register_sets != GetRegisterSetCount())
    return false;

  RegisterContextSP frame_zero_context = m_thread.GetRegisterContext();

  for (uint32_t set_idx = 0; set_idx < num_register_sets; ++set_idx) {
    const RegisterSet *const reg_set = GetRegisterSet(set_idx);

    const uint32_t num_registers = reg_set->num_registers;
    for (uint32_t reg_idx = 0; reg_idx < num_registers; ++reg_idx) {
      const uint32_t reg = reg_set->registers[reg_idx];
      const RegisterInfo *reg_info = GetRegisterInfoAtIndex(reg);
      if (!reg_info || reg_info->value_regs)
        continue;
      RegisterValue reg_value;

      // If we can reconstruct the register from the frame we are copying from,
      // then do so, otherwise use the value from frame 0.
      if (context->ReadRegister(reg_info, reg_value)) {
        WriteRegister(reg_info, reg_value);
      } else if (frame_zero_context->ReadRegister(reg_info, reg_value)) {
        WriteRegister(reg_info, reg_value);
      }
    }
  }
  return true;
}

lldb::tid_t RegisterContext::GetThreadID() const { return m_thread.GetID(); }

uint32_t RegisterContext::NumSupportedHardwareBreakpoints() { return 0; }

uint32_t RegisterContext::SetHardwareBreakpoint(lldb::addr_t addr,
                                                size_t size) {
  return LLDB_INVALID_INDEX32;
}

bool RegisterContext::ClearHardwareBreakpoint(uint32_t hw_idx) { return false; }

uint32_t RegisterContext::NumSupportedHardwareWatchpoints() { return 0; }

uint32_t RegisterContext::SetHardwareWatchpoint(lldb::addr_t addr, size_t size,
                                                bool read, bool write) {
  return LLDB_INVALID_INDEX32;
}

bool RegisterContext::ClearHardwareWatchpoint(uint32_t hw_index) {
  return false;
}

bool RegisterContext::HardwareSingleStep(bool enable) { return false; }

Status RegisterContext::ReadRegisterValueFromMemory(
    const RegisterInfo *reg_info, lldb::addr_t src_addr, uint32_t src_len,
    RegisterValue &reg_value) {
  Status error;
  if (reg_info == nullptr) {
    error.SetErrorString("invalid register info argument.");
    return error;
  }

  // Moving from addr into a register
  //
  // Case 1: src_len == dst_len
  //
  //   |AABBCCDD| Address contents
  //   |AABBCCDD| Register contents
  //
  // Case 2: src_len > dst_len
  //
  //   Status!  (The register should always be big enough to hold the data)
  //
  // Case 3: src_len < dst_len
  //
  //   |AABB| Address contents
  //   |AABB0000| Register contents [on little-endian hardware]
  //   |0000AABB| Register contents [on big-endian hardware]
  if (src_len > RegisterValue::kMaxRegisterByteSize) {
    error.SetErrorString("register too small to receive memory data");
    return error;
  }

  const uint32_t dst_len = reg_info->byte_size;

  if (src_len > dst_len) {
    error.SetErrorStringWithFormat(
        "%u bytes is too big to store in register %s (%u bytes)", src_len,
        reg_info->name, dst_len);
    return error;
  }

  ProcessSP process_sp(m_thread.GetProcess());
  if (process_sp) {
    uint8_t src[RegisterValue::kMaxRegisterByteSize];

    // Read the memory
    const uint32_t bytes_read =
        process_sp->ReadMemory(src_addr, src, src_len, error);

    // Make sure the memory read succeeded...
    if (bytes_read != src_len) {
      if (error.Success()) {
        // This might happen if we read _some_ bytes but not all
        error.SetErrorStringWithFormat("read %u of %u bytes", bytes_read,
                                       src_len);
      }
      return error;
    }

    // We now have a memory buffer that contains the part or all of the
    // register value. Set the register value using this memory data.
    // TODO: we might need to add a parameter to this function in case the byte
    // order of the memory data doesn't match the process. For now we are
    // assuming they are the same.
    reg_value.SetFromMemoryData(reg_info, src, src_len,
                                process_sp->GetByteOrder(), error);
  } else
    error.SetErrorString("invalid process");

  return error;
}

Status RegisterContext::WriteRegisterValueToMemory(
    const RegisterInfo *reg_info, lldb::addr_t dst_addr, uint32_t dst_len,
    const RegisterValue &reg_value) {
  uint8_t dst[RegisterValue::kMaxRegisterByteSize];

  Status error;

  ProcessSP process_sp(m_thread.GetProcess());
  if (process_sp) {

    // TODO: we might need to add a parameter to this function in case the byte
    // order of the memory data doesn't match the process. For now we are
    // assuming they are the same.

    const uint32_t bytes_copied = reg_value.GetAsMemoryData(
        reg_info, dst, dst_len, process_sp->GetByteOrder(), error);

    if (error.Success()) {
      if (bytes_copied == 0) {
        error.SetErrorString("byte copy failed.");
      } else {
        const uint32_t bytes_written =
            process_sp->WriteMemory(dst_addr, dst, bytes_copied, error);
        if (bytes_written != bytes_copied) {
          if (error.Success()) {
            // This might happen if we read _some_ bytes but not all
            error.SetErrorStringWithFormat("only wrote %u of %u bytes",
                                           bytes_written, bytes_copied);
          }
        }
      }
    }
  } else
    error.SetErrorString("invalid process");

  return error;
}

bool RegisterContext::ReadAllRegisterValues(
    lldb_private::RegisterCheckpoint &reg_checkpoint) {
  return ReadAllRegisterValues(reg_checkpoint.GetData());
}

bool RegisterContext::WriteAllRegisterValues(
    const lldb_private::RegisterCheckpoint &reg_checkpoint) {
  return WriteAllRegisterValues(reg_checkpoint.GetData());
}

TargetSP RegisterContext::CalculateTarget() {
  return m_thread.CalculateTarget();
}

ProcessSP RegisterContext::CalculateProcess() {
  return m_thread.CalculateProcess();
}

ThreadSP RegisterContext::CalculateThread() {
  return m_thread.shared_from_this();
}

StackFrameSP RegisterContext::CalculateStackFrame() {
  // Register contexts might belong to many frames if we have inlined functions
  // inside a frame since all inlined functions share the same registers, so we
  // can't definitively say which frame we come from...
  return StackFrameSP();
}

void RegisterContext::CalculateExecutionContext(ExecutionContext &exe_ctx) {
  m_thread.CalculateExecutionContext(exe_ctx);
}

bool RegisterContext::ConvertBetweenRegisterKinds(lldb::RegisterKind source_rk,
                                                  uint32_t source_regnum,
                                                  lldb::RegisterKind target_rk,
                                                  uint32_t &target_regnum) {
  const uint32_t num_registers = GetRegisterCount();
  for (uint32_t reg = 0; reg < num_registers; ++reg) {
    const RegisterInfo *reg_info = GetRegisterInfoAtIndex(reg);

    if (reg_info->kinds[source_rk] == source_regnum) {
      target_regnum = reg_info->kinds[target_rk];
      return (target_regnum != LLDB_INVALID_REGNUM);
    }
  }
  return false;
}
