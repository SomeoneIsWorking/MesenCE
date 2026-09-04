#include "NES/NesConsole.h"
#include "NES/NesCpu.h"
#include "NES/Port/NesPortExecution.h"
#include "Shared/Emulator.h"
#include "Shared/MessageManager.h"
#include "Utilities/FolderUtilities.h"
#include "Utilities/VirtualFile.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
	constexpr uint16_t kEntryPoint = 0x8000;
	constexpr uint16_t kNativeAddress = 0x8002;
	constexpr uint16_t kStopAddress = 0x8005;

	std::vector<uint8_t> MakeTestRom()
	{
		std::vector<uint8_t> rom(16 + 0x4000 + 0x2000, 0);
		rom[0] = 'N';
		rom[1] = 'E';
		rom[2] = 'S';
		rom[3] = 0x1A;
		rom[4] = 1;
		rom[5] = 1;

		const size_t program = 16;
		rom[program + 0] = 0xA9; // LDA #$2A
		rom[program + 1] = 0x2A;
		rom[program + 2] = 0x85; // Replaced by the native hook.
		rom[program + 3] = 0x02;
		rom[program + 4] = 0xEA; // NOP
		rom[program + 5] = 0x4C; // JMP $8004
		rom[program + 6] = 0x04;
		rom[program + 7] = 0x80;

		for(size_t vector = 0x3FFA; vector <= 0x3FFE; vector += 2) {
			rom[program + vector] = static_cast<uint8_t>(kEntryPoint);
			rom[program + vector + 1] = static_cast<uint8_t>(kEntryPoint >> 8);
		}
		return rom;
	}

	void Require(bool condition, const char* message)
	{
		if(!condition) {
			throw std::runtime_error(message);
		}
	}

	class NativeStoreHook final : public INesPortInstructionHook
	{
	private:
		std::vector<uint8_t> _initialState;
		bool _resolvedProgramRom = false;
		bool _sawExecuteCallback = false;

	public:
		NesPortInstructionAction OnBeforeInstruction(NesPortInstructionContext& context) override
		{
			NesPortCpuBoundaryState state = context.GetCpuState();
			const NesPortMemoryCallbacks& memory = context.GetMemoryCallbacks();
			Require(memory.ReadData && memory.WriteData && memory.ReadExecute && memory.ResolvePhysical, "incomplete memory callback table");

			if(state.Cpu.PC == kEntryPoint) {
				_initialState = context.SaveMachineState();
				_sawExecuteCallback = memory.ReadExecute(memory.Context, state.Cpu.PC) == 0xA9;
				NesPortPhysicalAddress address = memory.ResolvePhysical(memory.Context, state.Cpu.PC);
				_resolvedProgramRom = address.IsMapped && address.Type == MemoryType::NesPrgRom && address.PhysicalAddress == 0;
				return NesPortInstructionAction::Continue;
			}

			if(state.Cpu.PC == kNativeAddress) {
				memory.WriteData(memory.Context, 0x0002, state.Cpu.A);
				state.Cpu.PC = 0x8004;
				context.SetCpuState(state);
				return NesPortInstructionAction::NativeHandled;
			}

			if(state.Cpu.PC == kStopAddress) {
				return NesPortInstructionAction::Stop;
			}

			return NesPortInstructionAction::Continue;
		}

		const std::vector<uint8_t>& InitialState() const { return _initialState; }
		bool ResolvedProgramRom() const { return _resolvedProgramRom; }
		bool SawExecuteCallback() const { return _sawExecuteCallback; }
	};

	class InitializedEmulator final
	{
	public:
		Emulator Instance;

		InitializedEmulator() { Instance.Initialize(false); }
		~InitializedEmulator() { Instance.Release(); }
	};
}

int main()
{
	try {
		MessageManager::SetOptions(false, true);
		FolderUtilities::SetHomeFolder("build/port-proof/test-home");
		std::vector<uint8_t> rom = MakeTestRom();
		InitializedEmulator initialized;
		Emulator& emulator = initialized.Instance;
		Require(emulator.LoadRom(VirtualFile(rom.data(), rom.size(), "port-proof.nes"), VirtualFile(), false), "synthetic NES ROM did not load");

		auto* console = dynamic_cast<NesConsole*>(emulator.GetConsoleUnsafe());
		Require(console, "loaded console is not NES");
		console->DebugWrite(0x0002, 0x11, true);
		NativeStoreHook hook;
		NesPortRunResult result = console->RunPortInstructions(16, hook);

		Require(result.Stop == NesPortRunStop::HookRequested, "bounded execution did not stop at the hook");
		Require(result.CoreInstructions == 2, "unexpected core instruction count");
		Require(result.NativeInstructions == 1, "native instruction was not counted");
		Require(result.ProgramCounter == kStopAddress, "unexpected stop program counter");
		Require(console->DebugRead(0x0002) == 0x2A, "native hook did not write through the direct memory callback");
		Require(hook.SawExecuteCallback(), "execute memory callback did not read the shipping ROM mapping");
		Require(hook.ResolvedProgramRom(), "logical program address did not resolve to physical PRG ROM");
		Require(!hook.InitialState().empty(), "full-machine state capture returned no bytes");

		Require(console->LoadPortMachineState(hook.InitialState()) == NesPortStateLoadResult::Loaded, "full-machine state restore failed");
		if(console->GetCpu()->GetPC() != kEntryPoint) {
			throw std::runtime_error("restored machine PC was " + std::to_string(console->GetCpu()->GetPC()));
		}
		Require(console->DebugRead(0x0002) == 0x11, "restored machine retained a post-snapshot RAM write");

		std::cout << "mesence NES port execution proof: 2 core, 1 native, typed stop, mapping and full state passed\n";
		return 0;
	} catch(const std::exception& error) {
		std::cerr << "mesence NES port execution proof failed: " << error.what() << '\n';
		return 1;
	}
}
