#pragma once

#include <functional>
#include <memory>

#include <QtGui/QMouseEvent>
#include <QAbstractScrollArea>
#include <QMessageBox>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QDir>
#include <QEventLoop>
#include <QMutex>
#include <QPointer>
#include <set>

#include <document.h>
#include <page.h>

#include "Rendering/renderheadless.h"
#include "V3dModel.h"

// #define MOUSE_BOUNDARIES

class IBLEventFilter;
class IBLPromptEvent;

class EventFilter;
class ApplicationEventFilter;

class V3dModelManager {
public:
    friend class EventFilter;
    friend class ApplicationEventFilter;

    V3dModelManager(const Okular::Document* document);
    ~V3dModelManager();

    void AddModel(V3dModel model, size_t pageNumber);

    QImage RenderModel(size_t pageNumber, size_t modelIndex, int imageWidth, int imageHeight);

    V3dModel& Model(size_t pageNumber, size_t modelIndex);
    std::vector<V3dModel>& Models(size_t pageNumber);
    bool Empty();

    glm::vec2 GetCanvasSize(size_t pageNumber);

    void SetDocument(const Okular::Document* document);

    bool mouseMoveEvent(QMouseEvent* event);
    bool mouseButtonPressEvent(QMouseEvent* event);
    bool mouseButtonReleaseEvent(QMouseEvent* event);
    bool wheelEvent(QWheelEvent* event);
    bool keyPressEvent(QKeyEvent* event);

    void DrawMouseBoundaries(QImage* img, size_t pageNumber);

    void CacheRequest(Okular::PixmapRequest* request);
    void CachePage(size_t pageNumber, Okular::Page* page);

    std::string getIBLBase() const;
    void scheduleIBLPrompt(const std::string& imageName, const std::string& iblBase);

private:
    bool m_ReQueueModels{ true }; // TODO should be per model

    QMutex m_ModelsMutex;

    void EnsureCachedRequestSize(size_t pageNumber);
    void CacheRequestSize(size_t pageNumber, int width, int height, int priority);

    float GetDevicePixelRatio();

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
        float hi{ 0.0f }, lo{ 0.0f }, le{ 0.0f }, ri{ 0.0f };
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

    // Resolve an IBL environment map name to a full filesystem path.
    // Lookup order: $OKULAR_V3D_IMAGE_DIR/imageName > XDG data dir > (return empty, schedule download).
    std::string resolveIBLPath(const std::string& imageName, bool doSchedule = true);

    // Schedule a deferred download prompt for a missing IBL image (via QTimer::singleShot).
    void scheduleIBLDownload(const std::string& imageName, const std::string& iblBase);

    // Actually show the QMessageBox and perform the download (runs on main event loop, not render path).
    void performIBLDownload(const std::string& imageName, const std::string& iblBase);

    // Called when each async QNetworkReply finishes. When all replies are done,
    // marks models as changed so Okular re-renders with IBL on the next cycle.
    void onIBLDownloadReplyFinished(QNetworkReply* reply);
    void onIBLDownloadComplete();

    std::vector<std::vector<V3dModel>> m_Models;
    std::vector<std::vector<QImage>> m_ModelImages;

    // Sentinel objects returned when bounds checks fail (avoids UB / exceptions)
    V3dModel m_EmptyModel{ "" };
    std::vector<V3dModel> m_EmptyModels;

    std::unique_ptr<HeadlessRenderer> m_HeadlessRenderer;

    bool m_Dragging{ false };

    glm::ivec2 m_MousePosition{ 0, 0 };
    glm::ivec2 m_LastMousePosition{ 0, 0 };

    QPointer<QAbstractScrollArea> m_PageView;
    QObject m_FilterParent;

    EventFilter* m_EventFilter{ nullptr };
    ApplicationEventFilter* m_ApplicationEventFilter{ nullptr };
    IBLEventFilter* m_IBLEventFilter{ nullptr };

    std::chrono::time_point<std::chrono::system_clock> m_StartTime{ };

    struct RequestCache {
        glm::ivec2 size{ 0, 0 };
        int priority{ std::numeric_limits<int>::max() };
        std::chrono::duration<double> requestTime{ };
    };

    std::vector<RequestCache> m_CachedRequestSizes;
    std::vector<Okular::Page*> m_Pages;

    // Models whose IBL was missing at render time. After download completes,
    // their cached images are invalidated so they re-render with IBL enabled.
    std::set<std::pair<size_t, size_t>> m_ModelsWithMissingIBL;

    // Async download state: number of replies still pending.
    int m_PendingDownloadCount = 0;
    QNetworkAccessManager* m_IBLNetManager = nullptr;  // lives for duration of download batch.
    bool m_IBLDeclined = false;
    bool m_Destroying = false;
    std::string m_PendingIBLImageName;
    std::string m_PendingIBLIblBase;

    V3dModel* m_ActiveModel{ nullptr };
    int m_ActiveModelPage{ -1};
};

