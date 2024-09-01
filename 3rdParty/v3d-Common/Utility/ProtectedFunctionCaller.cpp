#include "ProtectedFunctionCaller.h"

void ProtectedFunctionCaller::callKeyReleaseEvent(QAbstractScrollArea* obj, QKeyEvent* event) {
    void (QAbstractScrollArea::*funcPtr)(QKeyEvent*) = &ProtectedFunctionCaller::keyReleaseEvent;

    (obj->*funcPtr)(event);
}
