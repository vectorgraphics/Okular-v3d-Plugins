#pragma once

#include <atomic>
#include <cstdint>
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
#include <observer.h>
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

    // While the user is actively driving the presentation window (same
    // screen), generators should serve PageView-priority pixmap requests
    // from cached images instead of re-rendering: Okular's priority-0
    // presentation request cancels the PageView's executing request on
    // every drag frame (Document::requestPixmaps ->
    // cancelRenderingBecauseOf -> notifyContentsCleared -> queued
    // slotRequestVisiblePixmaps), which would otherwise start a catch-up
    // render loop that fights the presentation's own renders for the GPU
    // and the model mutex.
    bool ShouldSuppressPageViewRender() const;

    // Last fully rendered model image (for stale-serving), or a null image.
    QImage LastRenderedModelImage(size_t pageNumber, size_t modelIndex) const;

    // The page's last rendered model image scaled to the requested size,
    // WITHOUT triggering a render. For background preload requests, which
    // must not drive the shared renderer's size (each v3d page fits to a
    // slightly different size, so a fresh preload render would flip the
    // renderer's size and force the next visible render into a full
    // recreation). Null image if the page has never been rendered.
    QImage CachedModelImage(size_t pageNumber, size_t modelIndex, int width, int height) const;

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

    // True while the destructor is running (or has run). The IBL prompt event
    // filter and the deferred-refresh timers consult this to refuse to touch the
    // manager after it is gone. Set on the main thread only, so a plain read is
    // safe for the (main-thread) readers.
    bool isDestroying() const { return m_Destroying; }

private:
    mutable QMutex m_ModelsMutex; // mutable: also locked from const getters (LastRenderedModelImage)

    // The public Cache* entry points take m_ModelsMutex; the *Unlocked
    // variants below assume the caller already holds it (QMutex is
    // non-recursive).
    void EnsureCachedRequestSize(size_t pageNumber); // caller holds m_ModelsMutex

    // Lazily create the HeadlessRenderer on first use (the first AddModel of
    // a document that actually contains 3D models): documents without v3d
    // content never pay for shader probing + glslang + Vulkan init at open
    // time, and the renderer is destroyed with the manager instead of
    // leaking a driver context per opened document. No-op once created (or
    // once the shaders were found to be missing). Caller holds m_ModelsMutex.
    void ensureHeadlessRenderer();
    void CacheRequestSize(size_t pageNumber, int width, int height, int priority);
    void cacheRequestUnlocked(Okular::PixmapRequest* request);
    void cacheRequestSizeUnlocked(size_t pageNumber, int width, int height, int priority);
    void cachePageUnlocked(size_t pageNumber, Okular::Page* page);

    // Re-resolve the PageView this document is shown in (see
    // resolvePageView) and rebind m_PageView (and the mouse EventFilter) to
    // it. The constructor-time scan (GetPageViewWidget) is correct except
    // when this document's own window's PageView does not exist yet, in
    // which case it binds to the other window's view; this document's first
    // PAGEVIEW_PRIO pixmap request (by which time its PageView exists)
    // triggers the re-resolution. Posted to the main event loop from
    // cacheRequestUnlocked() (which may run on a pixmap worker thread) and
    // delivered through ApplicationEventFilter.
    void rebindPageView();

    // True if a key event (received by the app-level event filter, which
    // sees every window's keys) was pressed in this document's own window --
    // the PageView's top-level window or the active presentation window.
    // Without this gate, with several windows open, H/M could act on a model
    // in a window the user is not looking at. Main thread only.
    bool keyEventInOwnWindow(QObject* object) const;

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

    // Page borders in the PresentationWidget's coordinate system (presentation mode).
    // The visible page is centered in the widget at the last presentation request size.
    std::vector<PageBorders> GetPresentationPageBorders();

    // Page borders in the coordinate system of the widget the last mouse event came
    // from (PageView viewport in normal mode, PresentationWidget in presentation mode).
    std::vector<PageBorders> GetActivePageBorders();

    // Size of the widget the last mouse event came from (fallback: PageView).
    glm::vec2 GetActiveViewportSize();

    // Returns the index of the page the mouse is over, or -1 if the mouse is not over a page
    int GetPageMouseIsOver();

    // Returns the given position relative to the given page in normalized coordinates
    glm::vec2 GetNormalizedPositionRelativeToPage(const glm::vec2& pos, int pageNumber);

    // Records which widget the most recent mouse event came from (set by EventFilter).
    void noteEventSource(QObject* object);

    // Reacts to Show/Destroy of Okular's PresentationWidget (from the app-level filter):
    // installs/removes the mouse event filter on the presentation window.
    void handlePresentationWidgetEvent(QObject* object, QEvent::Type type);

    const Okular::Document* m_Document;

    QAbstractScrollArea* GetPageViewWidget();

    std::chrono::duration<double> m_MinTimeBetweenRefreshes{ 1.0 / 60.0 }; // In Seconds
    std::chrono::time_point<std::chrono::system_clock> m_LastPixmapRefreshTime;

    void requestPixmapRefresh(size_t pageNumber);
    void refreshPixmap(size_t pageNumber);

    // Resolve an IBL environment map name to a full filesystem path.
    // Lookup order: $OKULAR_V3D_IMAGE_DIR/imageName > XDG data dir > (return empty).
    std::string resolveIBLPath(const std::string& imageName);

    // Actually show the QMessageBox and perform the download (runs on main event loop, not render path).
    void performIBLDownload(const std::string& imageName, const std::string& iblBase);

    // Schedule a main-thread QTimer that calls refreshPixmap(pageNumber) after
    // delayMs. The timer is parented to m_FilterParent (destroyed with the
    // manager) so it can never fire on freed state, and it deletes itself on fire
    // so it never leaks.
    void scheduleRefreshPixmap(size_t pageNumber, int delayMs);

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
    // ensureHeadlessRenderer() probed the shader paths and found no complete
    // set: rendering is disabled for this document and it must not re-probe.
    bool m_HeadlessRendererMissing{ false };

    bool m_Dragging{ false };

    glm::ivec2 m_MousePosition{ 0, 0 };
    glm::ivec2 m_LastMousePosition{ 0, 0 };

    QPointer<QAbstractScrollArea> m_PageView;
    // The PageView's DocumentObserver subobject, captured from the PageView's
    // own PixmapRequest (see CacheRequest). refreshPixmap() deletes only this
    // observer's page pixmap; every other observer's pixmaps are left alone.
    Okular::DocumentObserver* m_PageViewObserver{ nullptr };
    // Observer of the last PAGEVIEW_PRIO request seen (each PageView has a
    // stable observer, so a new one means a new PageView shows this
    // document). Equality only -- never dereferenced. Written under
    // m_ModelsMutex (also from pixmap worker threads).
    Okular::DocumentObserver* m_LastSeenPageViewObserver{ nullptr };
    // A PageViewRebindEvent (QEvent::User + 2) is in the main event queue.
    // Guarded by m_ModelsMutex.
    bool m_PendingRebindPosted{ false };
    QObject m_FilterParent;

    EventFilter* m_EventFilter{ nullptr };
    ApplicationEventFilter* m_ApplicationEventFilter{ nullptr };
    IBLEventFilter* m_IBLEventFilter{ nullptr };

    // Presentation mode: Okular shows pages in a separate top-level
    // PresentationWidget (not the PageView). When it appears, the plugin
    // installs a mouse EventFilter on it and hit-tests/refreshes in its
    // coordinate system. QPointer nulls itself if the widget is destroyed.
    QPointer<QWidget> m_PresentationWidget;
    EventFilter* m_PresentationEventFilter{ nullptr };
    // Widget the presentation mouse filter is currently installed on
    // (idempotency guard; QPointer clears on destruction).
    QPointer<QWidget> m_PresentationEventFilterOn;

    // Widget that delivered the most recent mouse event (main thread only).
    QObject* m_LastEventSource{ nullptr };

    // Last pixmap request sizes (device pixels) coming from the presentation
    // window, per page. Used for presentation-mode hit testing and to
    // re-request the presentation pixmap after deletePixmaps().
    std::vector<glm::ivec2> m_PresentationRequestSizes;

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
    // Written on the main thread (download declined), read on render worker
    // threads (RenderModel): keep it lock-free but atomic.
    std::atomic<bool> m_IBLDeclined{ false };
    bool m_Destroying = false;

    V3dModel* m_ActiveModel{ nullptr };
    int m_ActiveModelPage{ -1};

    // True while the user is actively dragging a model in the presentation
    // window on the same screen: PageView-priority requests are then served
    // stale by the generators (ShouldSuppressPageViewRender()). Written on
    // the main thread during presentation-only refreshes; cleared only by
    // real signals -- mouse release and PresentationWidget destroy. No
    // timeout, so it cannot lapse mid-drag.
    std::atomic<bool> m_SuppressPageViewRender{ false };
};

