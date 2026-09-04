#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace music {

struct SystemVolumeState {
    int percent = 50;
    bool muted = false;
    bool volume_known = false;
    bool mute_known = false;
};

struct SystemVolumeResult {
    SystemVolumeState state;
    bool success = false;
};

class SystemVolumeModel {
public:
    struct CommandResult {
        int exit_code = -1;
        std::string output;
        std::string error;

        bool succeeded() const noexcept { return exit_code == 0; }
    };

    using Command = std::vector<std::string>;
    using CommandRunner = std::function<CommandResult(const Command&)>;

    explicit SystemVolumeModel(CommandRunner command_runner = {});

    SystemVolumeResult adjustVolume(int delta_percent);
    SystemVolumeResult toggleMute();

    const SystemVolumeState& state() const noexcept;

    static int clampPercent(int percent) noexcept;
    static bool parseVolumeOutput(const std::string& output, int& percent);
    static bool parseMuteOutput(const std::string& output, bool& muted);

private:
    CommandRunner _command_runner;
    SystemVolumeState _state;

    bool readVolume();
    bool readMute();
    bool ensureUnmuted();
    CommandResult run(const Command& command) const;
    static CommandResult runCommand(const Command& command);
};

}  // namespace music
