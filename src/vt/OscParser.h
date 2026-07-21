#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace liney {

enum class SemanticEventType {
    PromptStart,
    CommandStart,
    OutputStart,
    CommandEnd,
    HyperlinkStart,
    HyperlinkEnd,
    ClipboardRequest,
    AgentStatus
};

struct SemanticEvent {
    SemanticEventType type = SemanticEventType::PromptStart;
    std::string value; // exit code, URI, or OSC 52 base64 payload
    size_t streamOffset = 0; // bytes consumed through the OSC terminator
    uint64_t row = 0;        // filled by Terminal after applying those bytes
};

// Streaming, bounded observer for OSC 133/8/52. It never modifies the byte
// stream sent to the VT core; it only emits semantic/security events.
class OscParser {
public:
    void feed(const char* data, size_t len);
    std::vector<SemanticEvent> drain();

private:
    enum class State { Ground, Escape, Osc, OscEscape };
    void finishOsc();
    void emit(SemanticEventType type, std::string value = {});

    State state_ = State::Ground;
    std::string payload_;
    std::vector<SemanticEvent> events_;
    bool overflow_ = false;
    size_t streamOffset_ = 0;
    static constexpr size_t kMaxPayload = 64 * 1024;
    static constexpr size_t kMaxQueuedEvents = 256;
};

} // namespace liney
