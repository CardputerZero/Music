#include "core/music_types.hpp"

namespace music {

const char* scanPhaseName(ScanPhase phase)
{
    switch (phase) {
        case ScanPhase::Idle:
            return "idle";
        case ScanPhase::OpeningDatabase:
            return "opening database";
        case ScanPhase::Discovering:
            return "discovering";
        case ScanPhase::ReadingMetadata:
            return "reading metadata";
        case ScanPhase::Committing:
            return "committing";
        case ScanPhase::Complete:
            return "complete";
        case ScanPhase::Error:
            return "error";
        case ScanPhase::Stopped:
            return "stopped";
    }
    return "unknown";
}

}  // namespace music
