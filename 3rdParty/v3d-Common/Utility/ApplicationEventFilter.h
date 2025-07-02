#pragma once

#include <qobject.h>

class V3dModelManager;

class ApplicationEventFilter : public QObject {
public:
    ApplicationEventFilter(QObject* parent, V3dModelManager* modelManager);
    ~ApplicationEventFilter() override = default;

    bool eventFilter(QObject *object, QEvent *event) override;

    V3dModelManager* modelManager;
};
