#pragma once

#ifdef _WIN32
#include <windows.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class CandidateWindow {
public:
    using SelectHandler = std::function<void(size_t)>;

    CandidateWindow() = default;
    ~CandidateWindow();
    CandidateWindow(const CandidateWindow &) = delete;
    CandidateWindow &operator=(const CandidateWindow &) = delete;

    bool Create(HINSTANCE instance);
    void Show(const std::vector<std::wstring> &items, int cursor, const RECT *anchor,
              const std::wstring &aux = {}, const std::wstring &status = {});
    void Hide();
    void SetSelectHandler(SelectHandler handler) { selectHandler_ = std::move(handler); }
    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void Paint();

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    std::vector<std::wstring> items_;
    int cursor_ = -1;
    std::wstring aux_;
    std::wstring status_;
    SelectHandler selectHandler_;
};
#endif
