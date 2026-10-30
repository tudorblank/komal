#include "base/log.hpp"

#include <cstdarg>
#include <cstdio>

namespace {

Log::Sink g_sink = nullptr;

void write(const char* level, const char* tag, const char* format, va_list args)
{
    char line[1024];
    const int head = std::snprintf(line, sizeof line, "[%s]%s ", tag, level);
    if (head > 0 && size_t(head) < sizeof line) std::vsnprintf(line + head, sizeof line - size_t(head), format, args);
    if (g_sink) {
        g_sink(line);
        return;
    }
    std::fputs(line, stderr);
    std::fputc('\n', stderr);
}

}

namespace Log {

void setSink(Sink sink) { g_sink = sink; }

void info(const char* tag, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    write("", tag, format, args);
    va_end(args);
}

void error(const char* tag, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    write(" error:", tag, format, args);
    va_end(args);
}

}
