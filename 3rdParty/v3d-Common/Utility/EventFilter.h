#pragma once

#include <qobject.h>

class V3dModelManager;

class EventFilter : public QObject {
public:
    EventFilter(QObject* parent, V3dModelManager* modelManager);
    ~EventFilter() override = default;

    bool eventFilter(QObject *object, QEvent *event);

    V3dModelManager* modelManager;
};