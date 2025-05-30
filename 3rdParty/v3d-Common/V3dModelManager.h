#pragma once

#include <memory>

#include <QtGui/QMouseEvent>
#include <QAbstractScrollArea>

#include <document.h>
#include <page.h>

#include "Rendering/renderheadless.h"
#include "V3dModel.h"

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

     /*
     hi: the distance between the top of the viewport and the top of the page
     lo: the distance between the bottom of the viewport and the bottom of the page
     le: the distance between the left side of the viewport and the left side of the page
     ri: the distance between the right side of the viewport and the right side of the page
     */
    struct PageBorders {
        float hi, lo, le, ri;
        int pageNumber{ 0 };
    };

    // Returns the page borders for all currently visible pages
    std::vector<PageBorders> GetPageBordersForVisiblePages();

    // Returns the index of the page the mouse is over, or -1 if the mouse is not over a page
    int GetPageMouseIsOver();

    // Returns the given position relative to the given page in normalized coordinates
    glm::vec2 GetNormalizedPositionRelativeToPage(const glm::vec2& pos, int pageNumber);

    const Okular::Document* m_Document;

    QAbstractScrollArea* GetPageViewWidget();

    std::chrono::duration<double> m_MinTimeBetweenRefreshes{ 1.0 / 60.0 }; // In Seconds
    std::chrono::time_point<std::chrono::system_clock> m_LastPixmapRefreshTime;

    void requestPixmapRefresh(size_t pageNumber);
    void refreshPixmap(size_t pageNumber);

    std::vector<std::vector<V3dModel>> m_Models;
    std::vector<std::vector<QImage>> m_ModelImages;

    std::unique_ptr<HeadlessRenderer> m_HeadlessRenderer;

    bool m_Dragging{ false };

    glm::ivec2 m_MousePosition;
    glm::ivec2 m_LastMousePosition;

    QAbstractScrollArea* m_PageView{ nullptr };
    EventFilter* m_EventFilter{ nullptr };

    std::chrono::time_point<std::chrono::system_clock> m_StartTime{ };

    struct RequestCache {
        glm::ivec2 size{ 0, 0 };
        int priority{ std::numeric_limits<int>::max() };
        std::chrono::duration<double> requestTime{ };
    };

    std::vector<RequestCache> m_CachedRequestSizes;
    std::vector<Okular::Page*> m_Pages;

    V3dModel* m_ActiveModel{ nullptr };
    int m_ActiveModelPage{ -1};
};
