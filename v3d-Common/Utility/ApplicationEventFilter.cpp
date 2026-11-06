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

        // This filter is installed on qApp, so it receives every window's
        // keys. Deliver to the manager only if the key was pressed in this
        // document's own window (PageView or presentation), so with several
        // windows open H/M cannot act on a model in a window the user is not
        // looking at.
        if (keyPress != nullptr && modelManager->keyEventInOwnWindow(object)) {
            return modelManager->keyPressEvent(keyPress);
        }
    }

    // Re-resolve and rebind m_PageView (posted from cacheRequestUnlocked,
    // possibly on a pixmap worker thread, when this document's own
    // PageView-priority requests start arriving).
    if (event->type() == QEvent::Type(QEvent::User + 2)) {
        modelManager->rebindPageView();
        return true;
    }

    // Track Okular's PresentationWidget (presentation mode shows pages in a
    // separate top-level window, not the PageView): install the plugin's mouse
    // filter on it when it appears.
    if (event->type() == QEvent::Show || event->type() == QEvent::Destroy) {
        modelManager->handlePresentationWidgetEvent(object, event->type());
    }

    return false;
}
