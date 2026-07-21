#pragma once

#include <string>

namespace liney {

enum class CommandRisk { Low, Medium, High };

struct AiRequest {
    std::wstring command;
    std::wstring output;
    std::wstring cwd;
    int exitCode = 0;
};

struct AiAnswer {
    bool ok = false;
    std::wstring explanation;
    std::wstring suggestedCommand;
    std::wstring error;
};

// Remove common credential forms before any provider sees terminal data.
std::wstring redactSensitiveText(const std::wstring& text);
std::string buildAiPromptJson(const AiRequest& request, bool includeCwd);
AiAnswer parseAiAnswer(const std::string& content);
CommandRisk assessCommandRisk(const std::wstring& command);
const wchar_t* commandRiskLabel(CommandRisk risk);

} // namespace liney
