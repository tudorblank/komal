#pragma once
// Messages for the user that need no answer (an import that failed): shown in the window's
// bottom-right corner for a few seconds, newest at the bottom, then gone. The one way something
// that went wrong reaches the user; it's logged as well.
//
//   notices.post("Couldn't import photo.png: unknown format");

#include "base/deadline.hpp"

#include <deque>
#include <string>

class Notices {
public:
    void post(std::string text);   // main thread
    void draw();                   // once a frame, after the panels
    double nextFrameIn() const;    // until the oldest one goes (base/frames.hpp)

private:
    static constexpr double kShowMs  = 6000.0;
    static constexpr size_t kMaxShown = 4;   // older ones make room

    struct Notice {
        std::string text;
        Deadline    until;
    };
    std::deque<Notice> m_shown;
};
