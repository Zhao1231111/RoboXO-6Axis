#ifndef TELEOP_KEYBOARD_H
#define TELEOP_KEYBOARD_H

#include "ipc_protocol.h"

// Keyboard teleop for terminal-based jog control.
// Reads keypresses (non-blocking) and translates them into jog commands
// written directly to the global g_jog_cmd / g_io_queue.
//
// Key bindings:
//   1-6     : select joint axis J1-J6
//   x/y/z   : select cartesian axis X/Y/Z
//   r/p/w   : select cartesian axis Rx/Ry/Rz
//   +/-     : jog selected axis in +/- direction
//   [/]     : decrease/increase speed ratio (1%,5%,10%,50%,100%)
//   g/h     : gripper open/close
//   e       : EStop
//   q       : quit teleop
//   space   : immediate jog stop
//
// run_teleop() blocks until 'q' is pressed.
void run_teleop();

#endif // TELEOP_KEYBOARD_H
