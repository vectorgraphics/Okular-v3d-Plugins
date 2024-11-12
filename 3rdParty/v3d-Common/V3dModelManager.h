#pragma once

#include <vector>
#include <string>
#include <memory>

#include <QImage>
#include <QtGui/QMouseEvent>
#include <QAbstractScrollArea>
#include <page.h>
#include <QPainter>

#include <document.h>

#include "V3dModel.h"
#include "Rendering/renderheadless.h"

// #define MOUSE_BOUNDARIES

class EventFilter;

class V3dModelManager {
public:
    friend class EventFilter;

    V3dModelManager(const Okular::Document* document);

    void AddModel(V3dModel model, size_t pageNumber);

    QImage RenderModel(size_t pageNumber, size_t modelIndex, int width, int height);

    V3dModel& Model(size_t pageNumber, size_t modelIndex);
    std::vector<V3dModel>& Models(size_t pageNumber);
    bool Empty();

    glm::vec2 GetCanvasSize(size_t pageNumber);

    void SetDocument(const Okular::Document* document);

    bool mouseMoveEvent(QMouseEvent* event);
    bool mouseButtonPressEvent(QMouseEvent* event);
    bool mouseButtonReleaseEvent(QMouseEvent* event);
    bool wheelEvent(QWheelEvent* event);

    void DrawMouseBoundaries(QImage* img, size_t pageNumber);

    void CacheRequest(Okular::PixmapRequest* request);

private:
    void CacheRequestSize(size_t pageNumber, int width, int height, int priority);
    void CachePage(size_t pageNumber, Okular::Page* page);

#ifdef MOUSE_BOUNDARIES
    struct Line {
        glm::vec2 start;
        glm::vec2 end;
        QColor color{ Qt::red };
    };

    struct Point {
        glm::vec2 pos;
        QColor color{ Qt::red };
    };

    std::vector<std::vector<Line>> m_MouseBoundaryLines;
    std::vector<std::vector<Point>> m_MouseBoundaryPoints;

#endif

    struct NormalizedMousePosition {
        glm::vec2 currentPosition;
        glm::vec2 lastPosition;

        int pageNumber;
    };
    
    // Gets the normalized mouse position with respect to the page provided by pageReference, or if the pageReference is
    // set to -1, than with respect to whatever page the mouse is currently over, in a one page document it will always be
    // with respect to the only page.

    // PageNumber will be set to -1 if the mouse is not over the pageReference, or if its not over a page at all, 
    // and otherwise will be set to the page the mouse is over top of.
    NormalizedMousePosition GetNormalizedMousePosition(int pageReference = -1);

    const Okular::Document* m_Document;

    QAbstractScrollArea* GetPageViewWidget();

    std::chrono::duration<double> m_MinTimeBetweenRefreshes{ 1.0 / 60.0 }; // In Seconds
    std::chrono::time_point<std::chrono::system_clock> m_LastPixmapRefreshTime;

    void requestPixmapRefresh(size_t pageNumber);
    void refreshPixmap(size_t pageNumber);

    std::vector<std::vector<V3dModel>> m_Models;
    std::vector<std::vector<QImage>> m_ModelImages;

    std::unique_ptr<HeadlessRenderer> m_HeadlessRenderer;

    bool m_MouseDown{ false };

    enum class DragMode {
        SHIFT,
        ZOOM,
        PAN,
        ROTATE
    };

    DragMode m_DragMode{ DragMode::ROTATE };

    glm::ivec2 m_MousePosition;
    glm::ivec2 m_LastMousePosition;

    QAbstractScrollArea* m_PageView{ nullptr };
    EventFilter* m_EventFilter{ nullptr };

    std::chrono::time_point<std::chrono::system_clock> m_StartTime{ };

    struct RequestCache {
        glm::ivec2 size;
        int priority{ std::numeric_limits<int>::max() };
        std::chrono::duration<double> requestTime{ };
    };

    std::vector<RequestCache> m_CachedRequestSizes;
    std::vector<Okular::Page*> m_Pages;

    V3dModel* m_ActiveModel;
    glm::ivec2 m_ActiveModelInfo{ -1, -1 };
};