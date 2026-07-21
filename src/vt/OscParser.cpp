#include "vt/OscParser.h"

#include <utility>

namespace liney {

void OscParser::feed(const char* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        ++streamOffset_;
        const unsigned char ch = static_cast<unsigned char>(data[i]);
        switch (state_) {
        case State::Ground:
            if (ch == 0x1b) state_ = State::Escape;
            break;
        case State::Escape:
            if (ch == ']') {
                payload_.clear();
                overflow_ = false;
                state_ = State::Osc;
            } else {
                state_ = ch == 0x1b ? State::Escape : State::Ground;
            }
            break;
        case State::Osc:
            if (ch == 0x07) {
                finishOsc();
                state_ = State::Ground;
            } else if (ch == 0x1b) {
                state_ = State::OscEscape;
            } else if (!overflow_) {
                if (payload_.size() < kMaxPayload) payload_.push_back(data[i]);
                else { overflow_ = true; payload_.clear(); }
            }
            break;
        case State::OscEscape:
            if (ch == '\\') {
                finishOsc();
                state_ = State::Ground;
            } else {
                // ESC inside an OSC that is not ST. Keep observing but never
                // let the bounded payload grow without limit.
                if (!overflow_ && payload_.size() + 2 <= kMaxPayload) {
                    payload_.push_back('\x1b');
                    payload_.push_back(data[i]);
                } else {
                    overflow_ = true;
                    payload_.clear();
                }
                state_ = State::Osc;
            }
            break;
        }
    }
}

void OscParser::emit(SemanticEventType type, std::string value) {
    if (events_.size() >= kMaxQueuedEvents) events_.erase(events_.begin());
    events_.push_back({type, std::move(value), streamOffset_, 0});
}

void OscParser::finishOsc() {
    if (overflow_) return;
    if (payload_.rfind("133;", 0) == 0 && payload_.size() >= 5) {
        const char mark = payload_[4];
        switch (mark) {
        case 'A': emit(SemanticEventType::PromptStart); break;
        case 'B': emit(SemanticEventType::CommandStart); break;
        case 'C': emit(SemanticEventType::OutputStart); break;
        case 'D': {
            std::string code;
            if (payload_.size() > 6 && payload_[5] == ';')
                code = payload_.substr(6);
            emit(SemanticEventType::CommandEnd, std::move(code));
            break;
        }
        default: break;
        }
        return;
    }
    if (payload_.rfind("8;", 0) == 0) {
        const size_t second = payload_.find(';', 2);
        if (second == std::string::npos) return;
        const std::string uri = payload_.substr(second + 1);
        emit(uri.empty() ? SemanticEventType::HyperlinkEnd
                         : SemanticEventType::HyperlinkStart,
             uri);
        return;
    }
    if (payload_.rfind("52;", 0) == 0) {
        const size_t second = payload_.find(';', 3);
        if (second == std::string::npos) return;
        // Keep only the encoded data; selection target is intentionally not
        // trusted. Decoding and permission policy belong to the UI layer.
        emit(SemanticEventType::ClipboardRequest, payload_.substr(second + 1));
        return;
    }
    static constexpr char kAgentPrefix[] = "777;agent-status;";
    if (payload_.rfind(kAgentPrefix, 0) == 0) {
        const std::string status = payload_.substr(sizeof(kAgentPrefix) - 1);
        if (status == "running" || status == "waiting" ||
            status == "needs-input" || status == "done" ||
            status == "failed")
            emit(SemanticEventType::AgentStatus, status);
    }
}

std::vector<SemanticEvent> OscParser::drain() {
    std::vector<SemanticEvent> out;
    out.swap(events_);
    return out;
}

} // namespace liney
