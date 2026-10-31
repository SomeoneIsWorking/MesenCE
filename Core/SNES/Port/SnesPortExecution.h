#pragma once

#include "Debugger/AddressInfo.h"
#include "Shared/MemoryType.h"
#include "SNES/SnesCpuTypes.h"

#include <cstdint>
#include <vector>

class SnesConsole;
class SnesCpu;

enum class SnesPortInstructionAction : uint8_t
{
	Continue,
	NativeHandled,
	Stop,
};

enum class SnesPortRunStop : uint8_t
{
	BudgetExhausted,
	HookRequested,
};

enum class SnesPortStateLoadResult : uint8_t
{
	Loaded,
	InvalidState,
	IncompatibleState,
};

struct SnesPortCpuBoundaryState
{
	SnesCpuState Cpu;
	uint32_t Operand = 0;
	uint32_t ReadWriteMask = 0xFFFFFF;
	bool ImmediateMode = false;
};

struct SnesPortPhysicalAddress
{
	uint32_t LogicalAddress = 0;
	int32_t PhysicalAddress = -1;
	MemoryType Type = MemoryType::SnesMemory;
	bool IsMapped = false;
};

struct SnesPortMemoryCallbacks
{
	using Read = uint8_t (*)(void* context, uint32_t address);
	using Write = void (*)(void* context, uint32_t address, uint8_t value);
	using Resolve = SnesPortPhysicalAddress (*)(void* context, uint32_t address);

	void* Context = nullptr;
	Read ReadData = nullptr;
	Write WriteData = nullptr;
	Read ReadExecute = nullptr;
	Resolve ResolvePhysical = nullptr;
};

class SnesPortInstructionContext final
{
private:
	SnesConsole& _console;
	SnesCpu& _cpu;
	SnesPortMemoryCallbacks _memory;

	static uint8_t ReadMemory(void* context, uint32_t address);
	static void WriteMemory(void* context, uint32_t address, uint8_t value);
	static uint8_t ReadExecuteMemory(void* context, uint32_t address);
	static SnesPortPhysicalAddress ResolveMemory(void* context, uint32_t address);

public:
	SnesPortInstructionContext(SnesConsole& console, SnesCpu& cpu);

	SnesPortCpuBoundaryState GetCpuState() const;
	void SetCpuState(const SnesPortCpuBoundaryState& state);
	const SnesPortMemoryCallbacks& GetMemoryCallbacks() const { return _memory; }
	std::vector<uint8_t> SaveMachineState() const;
	SnesPortStateLoadResult LoadMachineState(const std::vector<uint8_t>& state);
};

class ISnesPortInstructionHook
{
public:
	virtual ~ISnesPortInstructionHook() = default;
	virtual SnesPortInstructionAction OnBeforeInstruction(SnesPortInstructionContext& context) = 0;
};

struct SnesPortRunResult
{
	SnesPortRunStop Stop = SnesPortRunStop::BudgetExhausted;
	uint64_t CoreInstructions = 0;
	uint64_t NativeInstructions = 0;
	uint16_t ProgramCounter = 0;
	uint8_t ProgramBank = 0;

	uint64_t TotalInstructions() const { return CoreInstructions + NativeInstructions; }
};
