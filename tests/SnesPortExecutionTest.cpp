#include "SNES/Port/SnesPortExecution.h"
#include "SNES/SnesConsole.h"
#include "SNES/SnesCpu.h"
#include "SNES/SnesMemoryManager.h"
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
	constexpr uint8_t kEntryBank = 0x00;
	constexpr uint16_t kEntryPoint = 0x8000;
	constexpr uint16_t kNativeAddress = 0x8002;
	constexpr uint16_t kResumeAddress = 0x8004;
	constexpr uint16_t kStopAddress = 0x8005;

	std::vector<uint8_t> MakeTestRom()
	{
		// 128 KiB LoROM image with the header at 0x7FC0
		std::vector<uint8_t> rom(0x20000, 0);

		const size_t program = 0;
		rom[program + 0] = 0xA9; // LDA #$2A
		rom[program + 1] = 0x2A;
		rom[program + 2] = 0x85; // STA $02, replaced by the native hook
		rom[program + 3] = 0x02;
		rom[program + 4] = 0xEA; // NOP
		rom[program + 5] = 0x4C; // JMP $8005
		rom[program + 6] = 0x05;
		rom[program + 7] = 0x80;

		const size_t header = 0x7FC0;
		const char* title = "SNES PORT PROOF";
		for(size_t i = 0; i < 21; i++) {
			rom[header + i] = title[i] ? static_cast<uint8_t>(title[i]) : 0x20;
		}
		rom[header + 0x15] = 0x20; // LoROM, slow
		rom[header + 0x16] = 0x00; // ROM only
		rom[header + 0x17] = 0x07; // 128 KiB
		rom[header + 0x18] = 0x00; // no SRAM
		rom[header + 0x19] = 0x01; // USA
		rom[header + 0x1A] = 0x33;
		rom[header + 0x1B] = 0x00;

		rom[0x7FFC] = static_cast<uint8_t>(kEntryPoint);
		rom[0x7FFD] = static_cast<uint8_t>(kEntryPoint >> 8);
		return rom;
	}

	void Require(bool condition, const char* message)
	{
		if(!condition) {
			throw std::runtime_error(message);
		}
	}

	class NativeStoreHook final : public ISnesPortInstructionHook
	{
	private:
		std::vector<uint8_t> _initialState;
		bool _resolvedProgramRom = false;
		bool _sawExecuteCallback = false;
		bool _sawNativeStore = false;

	public:
		SnesPortInstructionAction OnBeforeInstruction(SnesPortInstructionContext& context) override
		{
			SnesPortCpuBoundaryState state = context.GetCpuState();
			const SnesPortMemoryCallbacks& memory = context.GetMemoryCallbacks();
			Require(memory.ReadData && memory.WriteData && memory.ReadExecute && memory.ResolvePhysical, "incomplete memory callback table");

			uint32_t pc = (static_cast<uint32_t>(state.Cpu.K) << 16) | state.Cpu.PC;
			if(pc == (static_cast<uint32_t>(kEntryBank) << 16 | kEntryPoint)) {
				_initialState = context.SaveMachineState();
				_sawExecuteCallback = memory.ReadExecute(memory.Context, pc) == 0xA9;
				SnesPortPhysicalAddress address = memory.ResolvePhysical(memory.Context, pc);
				_resolvedProgramRom = address.IsMapped && address.Type == MemoryType::SnesPrgRom && address.PhysicalAddress == 0;
				return SnesPortInstructionAction::Continue;
			}

			if(state.Cpu.PC == kNativeAddress) {
				memory.WriteData(memory.Context, 0x000002, static_cast<uint8_t>(state.Cpu.A));
				state.Cpu.PC = kResumeAddress;
				context.SetCpuState(state);
				_sawNativeStore = true;
				return SnesPortInstructionAction::NativeHandled;
			}

			if(state.Cpu.PC == kStopAddress) {
				return SnesPortInstructionAction::Stop;
			}

			return SnesPortInstructionAction::Continue;
		}

		const std::vector<uint8_t>& InitialState() const { return _initialState; }
		bool ResolvedProgramRom() const { return _resolvedProgramRom; }
		bool SawExecuteCallback() const { return _sawExecuteCallback; }
		bool SawNativeStore() const { return _sawNativeStore; }
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
		FolderUtilities::SetHomeFolder("build/snes-port-proof/test-home");
		std::vector<uint8_t> rom = MakeTestRom();
		InitializedEmulator initialized;
		Emulator& emulator = initialized.Instance;
		Require(emulator.LoadRom(VirtualFile(rom.data(), rom.size(), "snes-port-proof.sfc"), VirtualFile(), false), "synthetic SNES ROM did not load");

		auto* console = dynamic_cast<SnesConsole*>(emulator.GetConsoleUnsafe());
		Require(console, "loaded console is not SNES");

		console->DebugWrite(0x7E0002, 0x11);
		NativeStoreHook hook;
		SnesPortRunResult result = console->RunPortInstructions(16, hook);
		Require(result.Stop == SnesPortRunStop::HookRequested, "hook did not stop the run");
		Require(result.ProgramCounter == kStopAddress, "run stopped at the wrong address");
		Require(result.CoreInstructions == 2, "unexpected core instruction count");
		Require(result.NativeInstructions == 1, "unexpected native instruction count");
		Require(hook.SawExecuteCallback(), "execute callback did not read the entry code");
		Require(hook.ResolvedProgramRom(), "entry point did not resolve to program ROM");
		Require(hook.SawNativeStore(), "native store never ran");
		Require(console->DebugRead(0x7E0002) == 0x2A, "native store did not write A");

		std::vector<uint8_t> state = console->SavePortMachineState();
		Require(!state.empty(), "machine state is empty");
		console->DebugWrite(0x7E0002, 0x55);
		Require(console->LoadPortMachineState(state) == SnesPortStateLoadResult::Loaded, "machine state did not load");
		Require(console->DebugRead(0x7E0002) == 0x2A, "machine state did not restore memory");

		std::cout << "snes port proof passed\n";
		return 0;
	} catch(const std::exception& error) {
		std::cerr << "snes port proof failed: " << error.what() << "\n";
		return 1;
	}
}
