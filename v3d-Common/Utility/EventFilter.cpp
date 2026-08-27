#include "EventFilter.h"

#include <qcoreevent.h>
#include <qevent.h>
#include <QPainter>
#include <QWheelEvent>

#include "V3dModelManager.h"

EventFilter::EventFilter(QObject* parent, V3dModelManager* modelManager) 
    : QObject(parent), modelManager(modelManager) { }

bool EventFilter::eventFilter(QObject *object, QEvent *event) {
    if (modelManager == nullptr) {
        return false;
    }

    // Tell the manager which widget delivered this event so it can hit-test in
    // the right coordinate system (PageView viewport vs PresentationWidget).
    //
    // ONLY user-input events update the per-window gate source. This filter
    // also receives non-input events (Paint, HoverMove, ...) for its widget;
    // if those updated the source, a PageView repaint (e.g., the stale-serve
    // repaint that happens on every presentation drag frame) could flip the
    // gate to the PageView path. With input events alone that cannot happen:
    // under system load, delayed/coalesced event delivery used to let such a
    // flipped source be observed by refreshPixmap() mid-drag, re-rendering
    // the normal window while the user was driving the presentation window.
    const QEvent::Type eventType = event->type();
    if (eventType == QEvent::MouseMove || eventType == QEvent::MouseButtonPress ||
        eventType == QEvent::MouseButtonRelease || eventType == QEvent::Wheel) {
        modelManager->noteEventSource(object);
    }

    if (eventType == QEvent::MouseMove) {
        QMouseEvent* mouseMove = dynamic_cast<QMouseEvent*>(event);

        if (mouseMove != nullptr) {
            return modelManager->mouseMoveEvent(mouseMove);
        }

    } else if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mousePress = dynamic_cast<QMouseEvent*>(event);

        if (mousePress != nullptr) {
            return modelManager->mouseButtonPressEvent(mousePress);
        }

    } else if (event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent* mouseRelease = dynamic_cast<QMouseEvent*>(event);

        if (mouseRelease != nullptr) {
            return modelManager->mouseButtonReleaseEvent(mouseRelease);
        }
    } else if (event->type() == QEvent::Wheel) {

        QWheelEvent* wheelMove = dynamic_cast<QWheelEvent*>(event);

        if (wheelMove != nullptr) {
            return modelManager->wheelEvent(wheelMove);
        }
    }

    return false;
}
