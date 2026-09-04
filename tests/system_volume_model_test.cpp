#include "models/system_volume_model.hpp"

#include <cstdlib>
#include <deque>
#include <initializer_list>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using music::SystemVolumeModel;

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool commandEquals(const SystemVolumeModel::Command& command, std::initializer_list<const char*> expected)
{
    if (command.size() != expected.size()) {
        return false;
    }

    std::size_t index = 0;
    for (const char* argument : expected) {
        if (command[index++] != argument) {
            return false;
        }
    }
    return true;
}

class FakeRunner {
public:
    SystemVolumeModel::CommandResult operator()(const SystemVolumeModel::Command& command)
    {
        commands.push_back(command);
        require(!results.empty(), "unexpected command");
        auto result = std::move(results.front());
        results.pop_front();
        return result;
    }

    std::deque<SystemVolumeModel::CommandResult> results;
    std::vector<SystemVolumeModel::Command> commands;
};

}  // namespace

int main()
{
    int percent = -1;
    require(SystemVolumeModel::parseVolumeOutput(
                "Volume: front-left: 32768 /  50% / -18.06 dB, front-right: 32768 /  50% / -18.06 dB\n", percent) &&
                percent == 50,
            "parse PulseAudio volume");
    require(!SystemVolumeModel::parseVolumeOutput("Volume: unavailable\n", percent), "reject missing percentage");
    require(!SystemVolumeModel::parseVolumeOutput("Volume: 150%\n", percent), "reject volume above 100 percent");
    require(SystemVolumeModel::clampPercent(-5) == 0, "clamp low volume");
    require(SystemVolumeModel::clampPercent(105) == 100, "clamp high volume");

    bool muted = false;
    require(SystemVolumeModel::parseMuteOutput("Mute: yes\n", muted) && muted, "parse muted state");
    require(SystemVolumeModel::parseMuteOutput("Mute: no\n", muted) && !muted, "parse unmuted state");
    require(!SystemVolumeModel::parseMuteOutput("Mute: unknown\n", muted), "reject unknown mute state");

    FakeRunner volume_runner;
    volume_runner.results.push_back({0, "Volume: mono: 39322 /  60% / -13.31 dB\n", {}});
    volume_runner.results.push_back({0, {}, {}});
    volume_runner.results.push_back({0, {}, {}});
    volume_runner.results.push_back({0, {}, {}});
    volume_runner.results.push_back({1, {}, "simulated write failure"});
    SystemVolumeModel volume_model([&volume_runner](const auto& command) { return volume_runner(command); });

    auto result = volume_model.adjustVolume(5);
    require(result.success && result.state.percent == 65, "initial adjustment reads and updates volume");
    require(volume_runner.commands.size() == 3, "initial adjustment command count");
    require(commandEquals(volume_runner.commands[0], {"pactl", "get-sink-volume", "@DEFAULT_SINK@"}),
            "initial volume read command");
    require(commandEquals(volume_runner.commands[1], {"pactl", "set-sink-volume", "@DEFAULT_SINK@", "65%"}),
            "initial volume write command");
    require(commandEquals(volume_runner.commands[2], {"pactl", "set-sink-mute", "@DEFAULT_SINK@", "0"}),
            "volume adjustment explicitly unmutes");

    result = volume_model.adjustVolume(5);
    require(result.success && result.state.percent == 70, "repeat adjustment uses cached volume");
    require(volume_runner.commands.size() == 4, "repeat adjustment uses cached volume and mute state");

    result = volume_model.adjustVolume(-5);
    require(!result.success && result.state.percent == 70, "failed write preserves cached volume");

    FakeRunner mute_runner;
    mute_runner.results.push_back({0, {}, {}});
    mute_runner.results.push_back({0, "Mute: yes\n", {}});
    mute_runner.results.push_back({0, "Volume: mono: 49152 /  75% / -7.50 dB\n", {}});
    mute_runner.results.push_back({0, {}, {}});
    mute_runner.results.push_back({0, {}, {}});
    SystemVolumeModel mute_model([&mute_runner](const auto& command) { return mute_runner(command); });

    result = mute_model.toggleMute();
    require(result.success && result.state.muted && result.state.percent == 75, "toggle reads mute and volume state");
    require(commandEquals(mute_runner.commands[0], {"pactl", "set-sink-mute", "@DEFAULT_SINK@", "toggle"}),
            "mute toggle command");
    require(commandEquals(mute_runner.commands[1], {"pactl", "get-sink-mute", "@DEFAULT_SINK@"}), "mute read command");

    result = mute_model.adjustVolume(5);
    require(result.success && result.state.percent == 80 && !result.state.muted, "volume up restores sound after mute");
    require(commandEquals(mute_runner.commands[3], {"pactl", "set-sink-volume", "@DEFAULT_SINK@", "80%"}),
            "volume up after mute command");
    require(commandEquals(mute_runner.commands[4], {"pactl", "set-sink-mute", "@DEFAULT_SINK@", "0"}),
            "volume up after mute explicitly restores sound");

    std::cout << "System volume model tests passed\n";
    return 0;
}
