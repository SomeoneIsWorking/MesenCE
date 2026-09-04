#pragma once

#include "Core/Debugger/AddressInfo.h"
#include "NES/NesTypes.h"
#include "Shared/MemoryType.h"

#include <cstdint>
#include <vector>

class NesConsole;
class NesCpu;

enum class NesPortInstructionAction : uint8_t
{
	Continue,
	NativeHandled,
	Stop,
};

enum class NesPortRunStop : uint8_t
{
	BudgetExhausted,
	HookRequested,
};

enum class NesPortStateLoadResult : uint8_t
{
	Loaded,
	InvalidState,
	IncompatibleState,
};

struct NesPortCpuBoundaryState
{
	NesCpuState Cpu;
	uint64_t MasterClock = 0;
	uint16_t Operand = 0;
	NesAddrMode InstructionAddressMode = NesAddrMode::None;
	uint8_t PpuOffset = 0;
	uint8_t StartClockCount = 0;
	uint8_t EndClockCount = 0;
	uint8_t SpriteDmaOffset = 0;
	uint8_t IrqMask = 0;
	bool NeedHalt = false;
	bool SpriteDmaTransfer = false;
	bool DmcDmaRunning = false;
	bool AbortDmcDma = false;
	bool NeedDummyRead = false;
	bool CpuWrite = false;
	bool PreviousRunIrq = false;
	bool RunIrq = false;
	bool PreviousNmiFlag = false;
	bool PreviousNeedNmi = false;
	bool NeedNmi = false;
	bool Crashed = false;
	bool IsDmcDmaRead = false;
};

struct NesPortPhysicalAddress
{
	uint16_t LogicalAddress = 0;
	int32_t PhysicalAddress = -1;
	MemoryType Type = MemoryType::NesMemory;
	bool IsMapped = false;
};

struct NesPortMemoryCallbacks
{
	using Read = uint8_t (*)(void* context, uint16_t address);
	using Write = void (*)(void* context, uint16_t address, uint8_t value);
	using Resolve = NesPortPhysicalAddress (*)(void* context, uint16_t address);

	void* Context = nullptr;
	Read ReadData = nullptr;
	Write WriteData = nullptr;
	Read ReadExecute = nullptr;
	Resolve ResolvePhysical = nullptr;
};

class NesPortInstructionContext final
{
private:
	NesConsole& _console;
	NesCpu& _cpu;
	NesPortMemoryCallbacks _memory;

	static uint8_t ReadMemory(void* context, uint16_t address);
	static void WriteMemory(void* context, uint16_t address, uint8_t value);
	static uint8_t ReadExecuteMemory(void* context, uint16_t address);
	static NesPortPhysicalAddress ResolveMemory(void* context, uint16_t address);

public:
	NesPortInstructionContext(NesConsole& console, NesCpu& cpu);

	NesPortCpuBoundaryState GetCpuState() const;
	void SetCpuState(const NesPortCpuBoundaryState& state);
	const NesPortMemoryCallbacks& GetMemoryCallbacks() const { return _memory; }
	std::vector<uint8_t> SaveMachineState() const;
	NesPortStateLoadResult LoadMachineState(const std::vector<uint8_t>& state);
};

class INesPortInstructionHook
{
public:
	virtual ~INesPortInstructionHook() = default;
	virtual NesPortInstructionAction OnBeforeInstruction(NesPortInstructionContext& context) = 0;
};

struct NesPortRunResult
{
	NesPortRunStop Stop = NesPortRunStop::BudgetExhausted;
	uint64_t CoreInstructions = 0;
	uint64_t NativeInstructions = 0;
	uint16_t ProgramCounter = 0;

	uint64_t TotalInstructions() const { return CoreInstructions + NativeInstructions; }
};
