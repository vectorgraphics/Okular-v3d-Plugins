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

    // Track Okular's PresentationWidget (presentation mode shows pages in a
    // separate top-level window, not the PageView): install the plugin's mouse
    // filter on it when it appears.
    if (event->type() == QEvent::Show || event->type() == QEvent::Destroy) {
        modelManager->handlePresentationWidgetEvent(object, event->type());
    }

    return false;
}
