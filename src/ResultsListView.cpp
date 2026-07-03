#include "ResultsListView.h"

#include <commctrl.h>
#include <iterator>

namespace
{
    void SetSubItemText(HWND listView, int item, int subItem, const std::wstring& text)
    {
        ListView_SetItemText(listView, item, subItem, const_cast<LPWSTR>(text.c_str()));
    }
}

void ResultsListView::SetupColumns(HWND listView)
{
    struct ColumnDef { const wchar_t* title; int width; };
    const ColumnDef columns[] = {
        { L"Process Name", 150 },
        { L"PID", 70 },
        { L"Executable Path", 260 },
        { L"Type", 90 },
        { L"Detection Source", 130 },
        { L"Notes", 260 },
    };

    for (int i = 0; i < static_cast<int>(std::size(columns)); ++i)
    {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<LPWSTR>(columns[i].title);
        col.cx = columns[i].width;
        ListView_InsertColumn(listView, i, &col);
    }
}

void ResultsListView::Populate(HWND listView, const LockResult& result)
{
    ListView_DeleteAllItems(listView);

    for (size_t i = 0; i < result.processes.size(); ++i)
    {
        const auto& p = result.processes[i];

        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(i);
        item.iSubItem = 0;
        std::wstring name = p.processName.empty() ? L"(unknown)" : p.processName;
        item.pszText = const_cast<LPWSTR>(name.c_str());
        int index = ListView_InsertItem(listView, &item);

        SetSubItemText(listView, index, 1, std::to_wstring(p.processId));
        SetSubItemText(listView, index, 2, p.executablePath);
        SetSubItemText(listView, index, 3, p.applicationType);
        SetSubItemText(listView, index, 4, p.detectionSource);
        SetSubItemText(listView, index, 5, p.notes);
    }
}
