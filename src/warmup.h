#pragma once

#include <thread>

// Opens the connection to the graphics driver on a thread of its own, so that
// it runs alongside the interface being read instead of after it. Returns a
// thread that does nothing where there is nothing to win.
std::thread warmGraphics();
