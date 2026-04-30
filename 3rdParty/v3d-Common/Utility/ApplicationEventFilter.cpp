#include "ApplicationEventFilter.h"

// #include <qcoreevent.h>
// #include <qevent.h>
// #include <QPainter>

#include "V3dModelManager.h"

ApplicationEventFilter::ApplicationEventFilter(QObject* parent, V3dModelManager* modelManager)
: QObject(parent), modelManager(modelManager) { }

bool ApplicationEventFilter::eventFilter(QObject *object, QEvent *event) {
    if (modelManager == nullptr) {
        return false;
    }

    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyPress = dynamic_cast<QKeyEvent*>(event);

        if (keyPress != nullptr) {
            return modelManager->keyPressEvent(keyPress);
        }
    }

    return false;
}
