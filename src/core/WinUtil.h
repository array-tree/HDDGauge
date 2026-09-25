#pragma once

// Keep windows.h from dragging in the world before Qt does.
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <winioctl.h>

#include <QString>

namespace wu {

// RAII wrapper around a Win32 HANDLE.
class Handle
{
public:
    Handle() = default;
    explicit Handle(HANDLE h) : m_h(h) {}
    ~Handle() { close(); }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    Handle(Handle&& other) : m_h(other.m_h) { other.m_h = INVALID_HANDLE_VALUE; }
    Handle& operator=(Handle&& other)
    {
        if (this != &other) { close(); m_h = other.m_h; other.m_h = INVALID_HANDLE_VALUE; }
        return *this;
    }

    bool valid() const { return m_h != INVALID_HANDLE_VALUE && m_h != nullptr; }
    HANDLE get() const { return m_h; }
    HANDLE* addr() { return &m_h; }

    void reset(HANDLE h = INVALID_HANDLE_VALUE) { close(); m_h = h; }
    void close()
    {
        if (valid()) { ::CloseHandle(m_h); }
        m_h = INVALID_HANDLE_VALUE;
    }

private:
    HANDLE m_h = INVALID_HANDLE_VALUE;
};

/// Human readable text for a Win32 error code.
QString winError(DWORD code);

/// Last error, formatted.
QString lastError();

/// Trim trailing spaces / NULs from a fixed-size ASCII field.
QString fixedAscii(const char* s);

QString busName(int busType);

/// True when the current process token is a member of the Administrators group.
bool isElevated();

QString formatBytes(quint64 bytes);
QString formatRate(double bytesPerSecond);

} // namespace wu
