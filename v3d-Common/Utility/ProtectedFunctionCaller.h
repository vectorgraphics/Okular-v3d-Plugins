#pragma once

#include <qabstractscrollarea.h>

class ProtectedFunctionCaller : public QAbstractScrollArea {
public:
    static void callKeyReleaseEvent(QAbstractScrollArea* obj, QKeyEvent* event);
};