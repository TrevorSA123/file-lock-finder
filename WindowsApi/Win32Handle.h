#pragma once

#include <windows.h>
#include <utility>

// Minimal RAII wrapper around HANDLE (the spirit of .NET's SafeHandle):
// guarantees CloseHandle is called exactly once and never leaked, even on
// early-return error paths.
class Win32Handle
{
public:
    Win32Handle() = default;
    explicit Win32Handle(HANDLE h) : m_handle(h) {}

    ~Win32Handle() { Reset(); }

    Win32Handle(const Win32Handle&) = delete;
    Win32Handle& operator=(const Win32Handle&) = delete;

    Win32Handle(Win32Handle&& other) noexcept : m_handle(other.m_handle) { other.m_handle = nullptr; }
    Win32Handle& operator=(Win32Handle&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_handle = other.m_handle;
            other.m_handle = nullptr;
        }
        return *this;
    }

    bool IsValid() const { return m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE; }
    HANDLE Get() const { return m_handle; }

    void Reset(HANDLE h = nullptr)
    {
        if (IsValid())
            CloseHandle(m_handle);
        m_handle = h;
    }

    HANDLE Release()
    {
        HANDLE h = m_handle;
        m_handle = nullptr;
        return h;
    }

private:
    HANDLE m_handle = nullptr;
};
