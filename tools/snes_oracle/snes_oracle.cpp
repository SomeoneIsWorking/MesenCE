// Headless Mesen SNES run for differential checks: same ROM, scripted controller 1, and at chosen
// frames a 256x224 PPM of the visible picture and the 128 KiB of WRAM. A separate process; ports
// compare its files, they never link it.
#include "SNES/Port/SnesPortExecution.h"
#include "SNES/SnesConsole.h"
#include "SNES/SnesMemoryManager.h"
#include "SNES/SnesPpu.h"
#include "SNES/Input/SnesController.h"
#include "Shared/BaseControlDevice.h"
#include "Shared/BaseControlManager.h"
#include "Shared/EmuSettings.h"
#include "Shared/Emulator.h"
#include "Shared/Interfaces/IInputProvider.h"
#include "Utilities/FolderUtilities.h"
#include "Utilities/VirtualFile.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
// script button names in the order of the SNES pad's shift register
const char* const kScriptButtons[] = {"b", "y", "select", "start", "up", "down", "left", "right", "a", "x", "l", "r"};
const SnesController::Buttons kMesenButtons[] = {
	SnesController::B, SnesController::Y, SnesController::Select, SnesController::Start,
	SnesController::Up, SnesController::Down, SnesController::Left, SnesController::Right,
	SnesController::A, SnesController::X, SnesController::L, SnesController::R,
};

std::map<uint64_t, uint16_t> LoadScript(const std::string& path)
{
	std::map<uint64_t, uint16_t> changes;
	std::ifstream file(path);
	if(!file) {
		throw std::runtime_error("cannot open input script " + path);
	}
	std::string line;
	while(std::getline(file, line)) {
		line = line.substr(0, line.find('#'));
		std::istringstream fields(line);
		uint64_t frame;
		std::string buttons;
		if(!(fields >> frame >> buttons)) {
			continue;
		}
		uint16_t mask = 0;
		if(buttons != "-") {
			std::stringstream list(buttons);
			std::string name;
			while(std::getline(list, name, ',')) {
				bool found = false;
				for(int bit = 0; bit < 12; bit++) {
					if(name == kScriptButtons[bit]) {
						mask |= 1 << bit;
						found = true;
					}
				}
				if(!found) {
					throw std::runtime_error("unknown button " + name);
				}
			}
		}
		changes[frame] = mask;
	}
	return changes;
}

class ScriptInput final : public IInputProvider
{
public:
	std::map<uint64_t, uint16_t> Changes;
	uint64_t Frame = 0;

	bool SetInput(BaseControlDevice* device) override
	{
		if(device->GetPort() != 0) {
			return false;
		}
		uint16_t mask = 0;
		auto after = Changes.upper_bound(Frame);
		if(after != Changes.begin()) {
			mask = std::prev(after)->second;
		}
		for(int bit = 0; bit < 12; bit++) {
			device->SetBitValue(kMesenButtons[bit], (mask >> bit) & 1);
		}
		return true;
	}
};

// One line per instruction from power-on: program address and the master clock before it.
class TraceHook final : public ISnesPortInstructionHook
{
public:
	TraceHook(SnesConsole& console, std::ofstream& out) : _console(console), _out(out) {}

	SnesPortInstructionAction OnBeforeInstruction(SnesPortInstructionContext& context) override
	{
		SnesPortCpuBoundaryState state = context.GetCpuState();
		char line[40];
		int length = std::snprintf(line, sizeof(line), "%02X%04X %llu\n", state.Cpu.K, state.Cpu.PC, (unsigned long long)_console.GetMasterClock());
		_out.write(line, length);
		return SnesPortInstructionAction::Continue;
	}

private:
	SnesConsole& _console;
	std::ofstream& _out;
};

// Instructions and master cycles per program address, cycles charged to the instruction they precede.
class ProfileHook final : public ISnesPortInstructionHook
{
public:
	explicit ProfileHook(SnesConsole& console) : _console(console) {}

	SnesPortInstructionAction OnBeforeInstruction(SnesPortInstructionContext& context) override
	{
		SnesPortCpuBoundaryState state = context.GetCpuState();
		uint64_t clock = _console.GetMasterClock();
		if(_hasPrevious) {
			auto& entry = Entries[_previous];
			entry.first++;
			entry.second += clock - _previousClock;
		}
		_previous = (static_cast<uint32_t>(state.Cpu.K) << 16) | state.Cpu.PC;
		_previousClock = clock;
		_hasPrevious = true;
		return SnesPortInstructionAction::Continue;
	}

	std::map<uint32_t, std::pair<uint64_t, uint64_t>> Entries;

private:
	SnesConsole& _console;
	uint32_t _previous = 0;
	uint64_t _previousClock = 0;
	bool _hasPrevious = false;
};

void WriteFrame(const std::filesystem::path& path, SnesPpu& ppu)
{
	uint16_t* buffer = ppu.GetScreenBuffer();
	bool highRes = ppu.IsHighResOutput();
	int stride = highRes ? 512 : 256;
	// scanline 1 is buffer row 0 with overscan, else row 7: the 224 lines are centred in 239
	int top = ppu.GetStateRef().OverscanMode ? 0 : 7;
	std::ofstream out(path, std::ios::binary);
	out << "P6\n256 224\n255\n";
	for(int y = 0; y < 224; y++) {
		const uint16_t* row = buffer + (highRes ? (top + y) * 2 : top + y) * stride;
		for(int x = 0; x < 256; x++) {
			uint16_t c = row[highRes ? x * 2 + 1 : x];
			uint8_t r = c & 0x1F, g = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;
			uint8_t rgb[3] = {(uint8_t)((r << 3) | (r >> 2)), (uint8_t)((g << 3) | (g >> 2)), (uint8_t)((b << 3) | (b >> 2))};
			out.write(reinterpret_cast<char*>(rgb), 3);
		}
	}
}
} // namespace

int main(int argc, char** argv)
{
	try {
		std::string rom, script, outDir;
		uint64_t frames = 0;
		uint64_t traceInstructions = 0;
		uint64_t profileFrames = 0;
		std::set<uint64_t> capture;
		for(int i = 1; i + 1 < argc; i += 2) {
			std::string arg = argv[i], value = argv[i + 1];
			if(arg == "--rom") rom = value;
			else if(arg == "--input") script = value;
			else if(arg == "--out") outDir = value;
			else if(arg == "--frames") frames = std::stoull(value);
			else if(arg == "--trace") traceInstructions = std::stoull(value);
			else if(arg == "--profile-frames") profileFrames = std::stoull(value);
			else if(arg == "--capture") {
				std::stringstream list(value);
				std::string item;
				while(std::getline(list, item, ',')) capture.insert(std::stoull(item));
			} else throw std::runtime_error("unknown argument " + arg);
		}
		if(rom.empty() || outDir.empty() || (frames == 0 && traceInstructions == 0 && profileFrames == 0)) {
			throw std::runtime_error("usage: snes_oracle --rom R --out DIR (--frames N [--capture a,b] [--input script] | --trace N)");
		}
		std::filesystem::create_directories(outDir);
		FolderUtilities::SetHomeFolder((std::filesystem::path(outDir) / "mesen-home").string());

		std::ifstream romFile(rom, std::ios::binary);
		std::vector<uint8_t> romBytes{std::istreambuf_iterator<char>(romFile), {}};

		Emulator emu;
		emu.Initialize(false);
		SnesConfig& config = emu.GetSettings()->GetSnesConfig();
		config.RamPowerOnState = RamState::AllZeros;
		// the 32040 Hz APU rate bsnes and the LakeSnes fork use, over Mesen's 32000 Hz default
		config.SpcClockSpeedAdjustment = 40;
		config.EnableRandomPowerOnState = false;
		config.Port1.Type = ControllerType::SnesController;
		if(!emu.LoadRom(VirtualFile(romBytes.data(), romBytes.size(), "game.sfc"), VirtualFile(), false)) {
			throw std::runtime_error("Mesen rejected the ROM");
		}
		auto* console = dynamic_cast<SnesConsole*>(emu.GetConsoleUnsafe());
		if(!console) {
			throw std::runtime_error("not a SNES console");
		}
		ScriptInput input;
		if(!script.empty()) {
			input.Changes = LoadScript(script);
		}
		console->GetControlManager()->RegisterInputProvider(&input);

		if(traceInstructions > 0) {
			std::ofstream trace(std::filesystem::path(outDir) / "trace.txt");
			TraceHook hook(*console, trace);
			console->RunPortInstructions(traceInstructions, hook);
		}
		if(profileFrames > 0) {
			ProfileHook hook(*console);
			while(console->GetPpu()->GetFrameCount() < profileFrames) {
				console->RunPortInstructions(1000, hook);
			}
			std::ofstream profile(std::filesystem::path(outDir) / "profile.txt");
			for(auto& [address, entry] : hook.Entries) {
				profile << std::hex << address << std::dec << " " << entry.first << " " << entry.second << "\n";
			}
		}

		for(uint64_t frame = 0; frame < frames; frame++) {
			input.Frame = frame;
			console->RunFrame();
			if(capture.count(frame)) {
				char name[32];
				std::snprintf(name, sizeof(name), "frame_%06llu", (unsigned long long)frame);
				WriteFrame(std::filesystem::path(outDir) / (std::string(name) + ".ppm"), *console->GetPpu());
				std::ofstream wram(std::filesystem::path(outDir) / (std::string(name) + ".wram"), std::ios::binary);
				wram.write(reinterpret_cast<char*>(console->GetMemoryManager()->DebugGetWorkRam()), 0x20000);
			}
		}
		emu.Release();
		return 0;
	} catch(const std::exception& error) {
		std::cerr << "snes_oracle: " << error.what() << "\n";
		return 1;
	}
}
