#ifdef _WIN32
#include "CandidateWindow.h"

#include <algorithm>
#include <utility>
#include <windowsx.h>

namespace {
constexpr wchar_t kWindowClass[] = L"WowTypeCandidateWindow";
constexpr int kCellWidth = 176;
constexpr int kRowHeight = 34;
constexpr int kPadding = 8;
constexpr int kMaxColumns = 9;
}

CandidateWindow::~CandidateWindow() {
    Hide();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool CandidateWindow::Create(HINSTANCE instance) {
    if (hwnd_) return true;
    instance_ = instance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                            kWindowClass, L"WowType", WS_POPUP, 0, 0, 1, 1, nullptr,
                            nullptr, instance_, this);
    return hwnd_ != nullptr;
}

void CandidateWindow::Show(const std::vector<std::wstring> &items, int cursor,
                           const RECT *anchor, const std::wstring &aux,
                           const std::wstring &status) {
    if (!hwnd_) return;
    items_ = items;
    cursor_ = cursor;
    aux_ = aux;
    status_ = status;
    if (items_.empty() && aux_.empty() && status_.empty()) {
        Hide();
        return;
    }

    const int columns = std::clamp(static_cast<int>(items_.size()), 1, kMaxColumns);
    const int width = std::max(260, columns * kCellWidth + kPadding * 2);
    const int rows = items_.empty() ? 0 : 1;
    const int extraRows = (!aux_.empty() ? 1 : 0) + (!status_.empty() ? 1 : 0);
    const int height = kPadding * 2 + (rows + extraRows) * kRowHeight;

    POINT origin{};
    if (anchor) {
        origin.x = anchor->left;
        origin.y = anchor->bottom;
    } else {
        const HWND foreground = GetForegroundWindow();
        const DWORD thread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
        GUITHREADINFO info{};
        info.cbSize = sizeof(info);
        if (thread && GetGUIThreadInfo(thread, &info) && info.hwndCaret) {
            origin.x = info.rcCaret.left;
            origin.y = info.rcCaret.bottom;
            ClientToScreen(info.hwndCaret, &origin);
        } else {
            GetCursorPos(&origin);
        }
    }
    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    if (origin.x + width > screenWidth) origin.x = std::max(0, screenWidth - width);
    if (origin.y + height > screenHeight) origin.y = std::max(0, origin.y - height - 24);

    SetWindowPos(hwnd_, HWND_TOPMOST, origin.x, origin.y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void CandidateWindow::Hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

LRESULT CALLBACK CandidateWindow::WindowProc(HWND hwnd, UINT message,
                                              WPARAM wParam, LPARAM lParam) {
    CandidateWindow *self = reinterpret_cast<CandidateWindow *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
        self = static_cast<CandidateWindow *>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(message, wParam, lParam)
                : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CandidateWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_LBUTTONUP: {
        const int x = GET_X_LPARAM(lParam) - kPadding;
        const size_t index = x < 0 ? items_.size()
                                   : static_cast<size_t>(x / kCellWidth);
        if (index < items_.size() && selectHandler_) selectHandler_(index);
        return 0;
    }
    case WM_PAINT:
        Paint();
        return 0;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void CandidateWindow::Paint() {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd_, &ps);
    RECT client{};
    GetClientRect(hwnd_, &client);
    FillRect(dc, &client, GetSysColorBrush(COLOR_WINDOW));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));

    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ oldFont = SelectObject(dc, font);
    RECT cell{ kPadding, kPadding, 0, kPadding + kRowHeight };
    for (size_t i = 0; i < items_.size(); ++i) {
        cell.left = kPadding + static_cast<LONG>(i) * kCellWidth;
        cell.right = cell.left + kCellWidth;
        if (static_cast<int>(i) == cursor_) {
            HBRUSH selected = CreateSolidBrush(RGB(32, 122, 83));
            FillRect(dc, &cell, selected);
            DeleteObject(selected);
            SetTextColor(dc, RGB(255, 255, 255));
        } else {
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        }
        wchar_t label[8]{};
        wsprintfW(label, L"%u.", static_cast<unsigned>(i + 1));
        RECT labelRect = cell;
        labelRect.right = labelRect.left + 24;
        DrawTextW(dc, label, -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT textRect = cell;
        textRect.left += 26;
        DrawTextW(dc, items_[i].c_str(), static_cast<int>(items_[i].size()),
                  &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    LONG y = kPadding + (items_.empty() ? 0 : kRowHeight);
    SetTextColor(dc, RGB(100, 100, 100));
    if (!aux_.empty()) {
        RECT line{ kPadding, y, client.right - kPadding, y + kRowHeight };
        DrawTextW(dc, aux_.c_str(), static_cast<int>(aux_.size()), &line,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        y += kRowHeight;
    }
    if (!status_.empty()) {
        RECT line{ kPadding, y, client.right - kPadding, y + kRowHeight };
        DrawTextW(dc, status_.c_str(), static_cast<int>(status_.size()), &line,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    SelectObject(dc, oldFont);
    EndPaint(hwnd_, &ps);
}
#endif
