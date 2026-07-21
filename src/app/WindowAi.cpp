#include "app/Window.h"
#include "app/WindowInternal.h"

#include <windows.h>
#include <winhttp.h>

#include "core/Ai.h"
#include "util/Http.h"
#include "util/Json.h"
#include "util/Process.h"

namespace liney {
namespace {

std::wstring environment(const wchar_t* name) {
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) return {};
    std::wstring value(size - 1, L'\0');
    GetEnvironmentVariableW(name, value.data(), size);
    return value;
}

std::wstring quoteArgument(const std::wstring& value) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') ++slashes;
        else {
            if (c == L'\"') out.append(slashes + 1, L'\\');
            else out.append(slashes, L'\\');
            slashes = 0;
            out.push_back(c);
        }
    }
    out.append(slashes * 2, L'\\');
    out.push_back(L'\"');
    return out;
}

bool splitHttpsUrl(const std::wstring& url, std::wstring& host,
                   std::wstring& path) {
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS || parts.nPort != 443)
        return false;
    host.assign(parts.lpszHostName, parts.dwHostNameLength);
    path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength)
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    return !host.empty() && !path.empty() && host.find(L'@') == std::wstring::npos;
}

std::string responseText(const Json& response, bool responsesApi) {
    if (!responsesApi)
        return response["choices"].isArray() && !response["choices"].items().empty()
            ? response["choices"].items()[0]["message"]["content"].asString()
            : std::string();
    const Json& output = response["output"];
    if (!output.isArray()) return {};
    for (const Json& item : output.items()) {
        const Json& content = item["content"];
        if (!content.isArray()) continue;
        for (const Json& part : content.items())
            if (part["type"].asString() == "output_text")
                return part["text"].asString();
    }
    return {};
}

AiAnswer requestCompatibleApi(const std::wstring& provider,
                              const std::wstring& configuredEndpoint,
                              const std::wstring& model,
                              const std::string& prompt) {
    const bool openai = provider == L"openai";
    const std::wstring endpoint = openai
        ? L"https://api.openai.com/v1/responses" : configuredEndpoint;
    const std::wstring key = environment(openai ? L"OPENAI_API_KEY"
                                                : L"LINEY_AI_API_KEY");
    if (key.empty())
        return {false, {}, {}, openai
            ? L"OPENAI_API_KEY is not set."
            : L"LINEY_AI_API_KEY is not set."};
    std::wstring host, path;
    if (!splitHttpsUrl(endpoint, host, path))
        return {false, {}, {}, L"The AI endpoint must be an HTTPS URL on port 443."};

    Json body = Json::object();
    body.set("model", Json::str(wideToUtf8(model)));
    if (openai) {
        body.set("input", Json::str(prompt));
    } else {
        Json messages = Json::array();
        Json message = Json::object();
        message.set("role", Json::str("user"));
        message.set("content", Json::str(prompt));
        messages.push(std::move(message));
        body.set("messages", std::move(messages));
        Json format = Json::object();
        format.set("type", Json::str("json_object"));
        body.set("response_format", std::move(format));
    }
    const std::string raw = httpsPostJson(host, path, body.dump(0), key);
    bool ok = false;
    const Json response = raw.empty() ? Json() : Json::parse(raw, &ok);
    if (!ok) return {false, {}, {}, L"The AI request failed or returned invalid JSON."};
    const std::string content = responseText(response, openai);
    if (content.empty()) return {false, {}, {}, L"The AI response contained no text."};
    return parseAiAnswer(content);
}

} // namespace

void Window::requestAiForLastCommand(TerminalSession* session) {
    if (!session || session->commandBlocks().empty() || aiBusy_.exchange(true))
        return;
    const size_t index = session->commandBlocks().size() - 1;
    const CommandBlock block = session->commandBlocks()[index];
    AiRequest request;
    request.command = block.command;
    request.cwd = block.cwd.empty() ? session->cwd() : block.cwd;
    request.exitCode = block.exitCode;
    request.output = utf8ToWide(session->commandOutputUtf8(index));
    const std::string prompt = buildAiPromptJson(request, aiIncludeCwd_);
    const std::wstring provider = aiProvider_;
    const std::wstring model = aiModel_;
    const std::wstring endpoint = aiEndpoint_;
    aiRequestedCwd_ = request.cwd;
    showBalloon(L"Liney AI", L"Analyzing the last command…");
    updateThreads_.emplace_back([this, provider, model, endpoint, prompt, request]() {
        AiAnswer answer;
        if (provider == L"codex") {
            bool ok = false;
            const std::wstring command =
                L"codex exec --sandbox read-only --skip-git-repo-check " +
                quoteArgument(utf8ToWide(prompt));
            const std::wstring output = runCapture(command, request.cwd, &ok, 30000);
            answer = ok ? parseAiAnswer(wideToUtf8(output))
                        : AiAnswer{false, {}, {}, L"Codex CLI failed. Confirm it is installed and signed in."};
        } else {
            answer = requestCompatibleApi(provider, endpoint, model, prompt);
        }
        {
            std::lock_guard<std::mutex> lock(aiMutex_);
            aiAnswer_ = std::move(answer);
        }
        aiReady_ = true;
        aiBusy_ = false;
    });
}

void Window::pollAiResult() {
    if (!aiReady_.exchange(false)) return;
    AiAnswer answer;
    {
        std::lock_guard<std::mutex> lock(aiMutex_);
        answer = aiAnswer_;
    }
    if (!answer.ok) {
        MessageBoxW(hwnd_, answer.error.c_str(), L"Liney AI",
                    MB_OK | MB_ICONERROR);
        return;
    }
    MessageBoxW(hwnd_, answer.explanation.substr(0, 12000).c_str(),
                L"Liney AI explanation", MB_OK | MB_ICONINFORMATION);
    if (answer.suggestedCommand.empty()) return;

    const CommandRisk risk = assessCommandRisk(answer.suggestedCommand);
    std::wstring prompt = L"Suggested command (risk: ";
    prompt += commandRiskLabel(risk);
    prompt += L"):\n\n" + answer.suggestedCommand +
              L"\n\nYes = copy\nNo = review for execution\nCancel = close";
    const int action = MessageBoxW(hwnd_, prompt.c_str(), L"Liney AI suggestion",
        MB_YESNOCANCEL | (risk == CommandRisk::High ? MB_ICONWARNING : MB_ICONQUESTION) |
        MB_DEFBUTTON3);
    if (action == IDYES) {
        setClipboardText(answer.suggestedCommand);
        return;
    }
    if (action != IDNO) return;
    TerminalSession* current = activeSession();
    if (!current || _wcsicmp(current->cwd().c_str(), aiRequestedCwd_.c_str()) != 0) {
        MessageBoxW(hwnd_, L"The active terminal directory changed. The command was not run.",
                    L"Liney AI", MB_OK | MB_ICONWARNING);
        return;
    }
    std::wstring confirm = L"Run this command in the active terminal?\n\n" +
                           answer.suggestedCommand + L"\n\nRisk: " +
                           commandRiskLabel(risk);
    if (MessageBoxW(hwnd_, confirm.c_str(), L"Confirm AI command",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
        const std::wstring line = answer.suggestedCommand + L"\r";
        sendUtf16(line.c_str(), line.size());
    }
}

} // namespace liney
