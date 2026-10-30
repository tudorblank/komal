#pragma once
// The app's messages, one line each, tagged with where they come from: "[gpu] ...". Everything
// that isn't something the user did (that's the action log, command/actions): the platform, the
// GPU, files.
//
//   Log::info("gpu", "%s, %u images", name, count);
//   Log::error("import", "%s: %s", path, why);
//
// Lines go to stderr, unless a sink takes them instead (the tests read what was logged).

#if defined(__GNUC__) || defined(__clang__)
#define KOMAL_PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define KOMAL_PRINTF(f, a)
#endif

namespace Log {

void info(const char* tag, const char* format, ...) KOMAL_PRINTF(2, 3);
void error(const char* tag, const char* format, ...) KOMAL_PRINTF(2, 3);

using Sink = void (*)(const char* line);   // a whole line, without its newline
void setSink(Sink sink);                    // null: stderr again

}
