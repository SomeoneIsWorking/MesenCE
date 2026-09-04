#include "pch.h"

#include "NES/NesConsole.h"
#include "NES/NesCpu.h"
#include "NES/NesMemoryManager.h"
#include "NES/Port/NesPortExecution.h"

NesPortInstructionContext::NesPortInstructionContext(NesConsole& console, NesCpu& cpu)
	: _console(console), _cpu(cpu)
{
	_memory.Context = this;
	_memory.ReadData = &ReadMemory;
	_memory.WriteData = &WriteMemory;
	_memory.ReadExecute = &ReadExecuteMemory;
	_memory.ResolvePhysical = &ResolveMemory;
}

NesPortCpuBoundaryState NesPortInstructionContext::GetCpuState() const
{
	return _cpu.GetPortBoundaryState();
}

void NesPortInstructionContext::SetCpuState(const NesPortCpuBoundaryState& state)
{
	_cpu.SetPortBoundaryState(state);
}

uint8_t NesPortInstructionContext::ReadMemory(void* context, uint16_t address)
{
	auto& self = *static_cast<NesPortInstructionContext*>(context);
	return self._console.GetMemoryManager()->DebugRead(address);
}

void NesPortInstructionContext::WriteMemory(void* context, uint16_t address, uint8_t value)
{
	auto& self = *static_cast<NesPortInstructionContext*>(context);
	self._console.GetMemoryManager()->DebugWrite(address, value, true);
}

uint8_t NesPortInstructionContext::ReadExecuteMemory(void* context, uint16_t address)
{
	return ReadMemory(context, address);
}

NesPortPhysicalAddress NesPortInstructionContext::ResolveMemory(void* context, uint16_t address)
{
	auto& self = *static_cast<NesPortInstructionContext*>(context);
	AddressInfo relative { address, MemoryType::NesMemory };
	AddressInfo absolute = self._console.GetAbsoluteAddress(relative);
	return { address, absolute.Address, absolute.Type, absolute.Address >= 0 };
}

std::vector<uint8_t> NesPortInstructionContext::SaveMachineState() const
{
	return _console.SavePortMachineState();
}

NesPortStateLoadResult NesPortInstructionContext::LoadMachineState(const std::vector<uint8_t>& state)
{
	return _console.LoadPortMachineState(state);
}
