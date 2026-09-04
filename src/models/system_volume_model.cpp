#include "models/system_volume_model.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

extern char** environ;

namespace music {
namespace {

constexpr std::size_t kMaxCommandOutput = 16 * 1024;

std::string commandName(const SystemVolumeModel::Command& command)
{
    return command.empty() ? "pactl" : command.front();
}

}  // namespace

SystemVolumeModel::SystemVolumeModel(CommandRunner command_runner) : _command_runner(std::move(command_runner))
{
    if (!_command_runner) {
        _command_runner = runCommand;
    }
}

SystemVolumeResult SystemVolumeModel::adjustVolume(int delta_percent)
{
    if (!_state.volume_known && !readVolume()) {
        return {_state, false};
    }

    const auto requested = static_cast<std::int64_t>(_state.percent) + delta_percent;
    const int target = static_cast<int>(std::clamp<std::int64_t>(requested, 0, 100));
    if (target != _state.percent) {
        const Command command = {"pactl", "set-sink-volume", "@DEFAULT_SINK@", std::to_string(target) + "%"};
        const CommandResult result = run(command);
        if (!result.succeeded()) {
            spdlog::warn("SystemVolumeModel: failed to set volume to {}%: {}", target,
                         result.error.empty() ? "pactl command failed" : result.error);
            return {_state, false};
        }

        _state.percent = target;
        _state.volume_known = true;
    }

    if (!ensureUnmuted()) {
        return {_state, false};
    }

    spdlog::info("SystemVolumeModel: volume {}%", target);
    return {_state, true};
}

SystemVolumeResult SystemVolumeModel::toggleMute()
{
    const CommandResult toggle_result = run({"pactl", "set-sink-mute", "@DEFAULT_SINK@", "toggle"});
    if (!toggle_result.succeeded()) {
        spdlog::warn("SystemVolumeModel: failed to toggle mute: {}",
                     toggle_result.error.empty() ? "pactl command failed" : toggle_result.error);
        return {_state, false};
    }

    _state.mute_known = false;
    if (!readMute()) {
        return {_state, false};
    }

    if (!_state.volume_known) {
        (void)readVolume();
    }

    spdlog::info("SystemVolumeModel: {}", _state.muted ? "muted" : "sound on");
    return {_state, true};
}

const SystemVolumeState& SystemVolumeModel::state() const noexcept { return _state; }

int SystemVolumeModel::clampPercent(int percent) noexcept { return std::clamp(percent, 0, 100); }

bool SystemVolumeModel::parseVolumeOutput(const std::string& output, int& percent)
{
    std::size_t marker = output.find('%');
    while (marker != std::string::npos) {
        std::size_t start = marker;
        while (start > 0 && std::isdigit(static_cast<unsigned char>(output[start - 1]))) {
            --start;
        }

        if (start < marker && (start == 0 || std::isspace(static_cast<unsigned char>(output[start - 1])))) {
            int parsed = 0;
            const auto result = std::from_chars(output.data() + start, output.data() + marker, parsed);
            if (result.ec == std::errc{} && result.ptr == output.data() + marker && parsed >= 0 && parsed <= 100) {
                percent = parsed;
                return true;
            }
        }

        marker = output.find('%', marker + 1);
    }
    return false;
}

bool SystemVolumeModel::parseMuteOutput(const std::string& output, bool& muted)
{
    const std::size_t marker = output.find("Mute:");
    if (marker == std::string::npos) {
        return false;
    }

    std::size_t value = marker + std::strlen("Mute:");
    while (value < output.size() && std::isspace(static_cast<unsigned char>(output[value]))) {
        ++value;
    }

    if (output.compare(value, 3, "yes") == 0) {
        muted = true;
        return true;
    }
    if (output.compare(value, 2, "no") == 0) {
        muted = false;
        return true;
    }
    return false;
}

bool SystemVolumeModel::readVolume()
{
    const CommandResult result = run({"pactl", "get-sink-volume", "@DEFAULT_SINK@"});
    int percent = 0;
    if (!result.succeeded() || !parseVolumeOutput(result.output, percent)) {
        spdlog::warn("SystemVolumeModel: failed to read volume: {}",
                     result.error.empty() ? "unexpected pactl output" : result.error);
        return false;
    }

    _state.percent = percent;
    _state.volume_known = true;
    return true;
}

bool SystemVolumeModel::readMute()
{
    const CommandResult result = run({"pactl", "get-sink-mute", "@DEFAULT_SINK@"});
    bool muted = false;
    if (!result.succeeded() || !parseMuteOutput(result.output, muted)) {
        spdlog::warn("SystemVolumeModel: failed to read mute state: {}",
                     result.error.empty() ? "unexpected pactl output" : result.error);
        return false;
    }

    _state.muted = muted;
    _state.mute_known = true;
    return true;
}

bool SystemVolumeModel::ensureUnmuted()
{
    if (_state.mute_known && !_state.muted) {
        return true;
    }

    const CommandResult result = run({"pactl", "set-sink-mute", "@DEFAULT_SINK@", "0"});
    if (!result.succeeded()) {
        spdlog::warn("SystemVolumeModel: failed to unmute after volume adjustment: {}",
                     result.error.empty() ? "pactl command failed" : result.error);
        return false;
    }

    const bool was_muted = _state.mute_known && _state.muted;
    _state.muted = false;
    _state.mute_known = true;
    if (was_muted) {
        spdlog::info("SystemVolumeModel: sound on after volume adjustment");
    }
    return true;
}

SystemVolumeModel::CommandResult SystemVolumeModel::run(const Command& command) const
{
    return _command_runner(command);
}

SystemVolumeModel::CommandResult SystemVolumeModel::runCommand(const Command& command)
{
    CommandResult result;
    if (command.empty()) {
        result.error = "empty command";
        return result;
    }

    int output_pipe[2] = {-1, -1};
    if (::pipe(output_pipe) != 0) {
        result.error = std::string("pipe failed: ") + std::strerror(errno);
        return result;
    }

    posix_spawn_file_actions_t actions;
    int spawn_error = posix_spawn_file_actions_init(&actions);
    const bool actions_initialized = spawn_error == 0;
    if (spawn_error == 0) {
        spawn_error = posix_spawn_file_actions_adddup2(&actions, output_pipe[1], STDOUT_FILENO);
    }
    if (spawn_error == 0) {
        spawn_error = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    }
    if (spawn_error == 0) {
        spawn_error = posix_spawn_file_actions_addclose(&actions, output_pipe[0]);
    }
    if (spawn_error == 0) {
        spawn_error = posix_spawn_file_actions_addclose(&actions, output_pipe[1]);
    }

    if (spawn_error != 0) {
        if (actions_initialized) {
            posix_spawn_file_actions_destroy(&actions);
        }
        ::close(output_pipe[0]);
        ::close(output_pipe[1]);
        result.error = std::string("spawn setup failed: ") + std::strerror(spawn_error);
        return result;
    }

    std::vector<char*> arguments;
    arguments.reserve(command.size() + 1);
    for (const auto& argument : command) {
        arguments.push_back(const_cast<char*>(argument.c_str()));
    }
    arguments.push_back(nullptr);

    pid_t child = -1;
    spawn_error = ::posix_spawnp(&child, arguments.front(), &actions, nullptr, arguments.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(output_pipe[1]);

    if (spawn_error != 0) {
        ::close(output_pipe[0]);
        result.error = commandName(command) + " spawn failed: " + std::strerror(spawn_error);
        return result;
    }

    char buffer[512];
    while (true) {
        const ssize_t count = ::read(output_pipe[0], buffer, sizeof(buffer));
        if (count > 0) {
            const std::size_t available = kMaxCommandOutput - std::min(result.output.size(), kMaxCommandOutput);
            result.output.append(buffer, std::min(static_cast<std::size_t>(count), available));
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    ::close(output_pipe[0]);

    int status = 0;
    while (::waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        result.error = std::string("waitpid failed: ") + std::strerror(errno);
        return result;
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
        if (result.exit_code != 0) {
            result.error = commandName(command) + " exited with status " + std::to_string(result.exit_code);
        }
    } else if (WIFSIGNALED(status)) {
        result.error = commandName(command) + " terminated by signal " + std::to_string(WTERMSIG(status));
    } else {
        result.error = commandName(command) + " did not exit normally";
    }

    return result;
}

}  // namespace music
