#include "pch.h"

#include "SNES/Port/SnesPortExecution.h"
#include "SNES/SnesConsole.h"
#include "SNES/SnesCpu.h"
#include "SNES/SnesMemoryManager.h"

SnesPortInstructionContext::SnesPortInstructionContext(SnesConsole& console, SnesCpu& cpu)
	: _console(console), _cpu(cpu)
{
	_memory.Context = this;
	_memory.ReadData = &ReadMemory;
	_memory.WriteData = &WriteMemory;
	_memory.ReadExecute = &ReadExecuteMemory;
	_memory.ResolvePhysical = &ResolveMemory;
}

SnesPortCpuBoundaryState SnesPortInstructionContext::GetCpuState() const
{
	return _cpu.GetPortBoundaryState();
}

void SnesPortInstructionContext::SetCpuState(const SnesPortCpuBoundaryState& state)
{
	_cpu.SetPortBoundaryState(state);
}

uint8_t SnesPortInstructionContext::ReadMemory(void* context, uint32_t address)
{
	auto& self = *static_cast<SnesPortInstructionContext*>(context);
	return self._console.GetMemoryManager()->Peek(address);
}

void SnesPortInstructionContext::WriteMemory(void* context, uint32_t address, uint8_t value)
{
	auto& self = *static_cast<SnesPortInstructionContext*>(context);
	self._console.GetMemoryManager()->Write(address, value, MemoryOperationType::Write);
}

uint8_t SnesPortInstructionContext::ReadExecuteMemory(void* context, uint32_t address)
{
	return ReadMemory(context, address);
}

SnesPortPhysicalAddress SnesPortInstructionContext::ResolveMemory(void* context, uint32_t address)
{
	auto& self = *static_cast<SnesPortInstructionContext*>(context);
	AddressInfo relative { static_cast<int32_t>(address), MemoryType::SnesMemory };
	AddressInfo absolute = self._console.GetAbsoluteAddress(relative);
	return { address, absolute.Address, absolute.Type, absolute.Address >= 0 };
}

std::vector<uint8_t> SnesPortInstructionContext::SaveMachineState() const
{
	return _console.SavePortMachineState();
}

SnesPortStateLoadResult SnesPortInstructionContext::LoadMachineState(const std::vector<uint8_t>& state)
{
	return _console.LoadPortMachineState(state);
}
