#include "debugger.hh"

#include "debuggerhelp.hh"
#include "debuggernotimplemented.hh"

namespace luisavm {

Debugger::Debugger(Video& video, Keyboard& keyboard)
    : _keyboard(keyboard)
{
    _screens.push_back(make_unique<DebuggerHelp>(video));
    _screens.push_back(make_unique<DebuggerNotImplemented>(video));
    _screens.push_back(make_unique<DebuggerNotImplemented>(video));
    _screens.push_back(make_unique<DebuggerNotImplemented>(video));
    _screens.push_back(make_unique<DebuggerNotImplemented>(video));
    _screens.push_back(make_unique<DebuggerNotImplemented>(video));
    _screens.push_back(make_unique<DebuggerNotImplemented>(video));
    _screens.push_back(make_unique<DebuggerNotImplemented>(video));
}


void 
Debugger::Step()
{
    // verify keypress
    while(!_keyboard.Queue.empty()) {
        auto kp = _keyboard.Queue.back();
        if(kp.state == PRESSED) {
            Keypressed(kp);
        }
        _keyboard.Queue.pop();
    }

    // update screen
    if(_screens[_selected]->dirty) {
        _screens[_selected]->Update();
        _screens[_selected]->dirty = true;
    }
}


void 
Debugger::Keypressed(Keyboard::KeyPress const& kp)
{
    (void) kp;
    if(kp.key >= F1 && kp.key <= F8) {
        _selected = static_cast<int>(kp.key) - static_cast<int>(F1);
        _screens[_selected]->dirty = true;
    }
}


}  // namespace luisavm
