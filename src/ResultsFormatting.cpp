#include "ResultsFormatting.h"

#include <sstream>
#include <ctime>
#include <iterator>

std::wstring ResultsFormatting::ToClipboardText(const LockResult& result)
{
    std::wstringstream ss;
    ss << L"File Lock Finder results\r\n";
    ss << L"Path: " << (result.selectedPath.empty() ? L"(none)" : result.selectedPath) << L"\r\n";
    ss << L"Status: " << result.statusMessage << L"\r\n";

    if (result.timestamp != 0)
    {
        wchar_t timeBuf[64] = {};
        tm tmValue{};
        if (localtime_s(&tmValue, &result.timestamp) == 0)
        {
            wcsftime(timeBuf, std::size(timeBuf), L"%Y-%m-%d %H:%M:%S", &tmValue);
            ss << L"Scanned at: " << timeBuf << L"\r\n";
        }
    }

    if (!result.advice.empty())
        ss << L"Advice: " << result.advice << L"\r\n";

    ss << L"\r\n";

    if (result.processes.empty())
    {
        ss << L"No locking processes were identified.\r\n";
        for (const auto& err : result.errors)
            ss << L"Note: " << err << L"\r\n";
    }
    else
    {
        ss << L"Process\tPID\tExecutable Path\tType\tDetection Source\tNotes\r\n";
        for (const auto& p : result.processes)
        {
            ss << p.processName << L"\t" << p.processId << L"\t" << p.executablePath << L"\t"
               << p.applicationType << L"\t" << p.detectionSource << L"\t" << p.notes << L"\r\n";
        }
    }

    return ss.str();
}
