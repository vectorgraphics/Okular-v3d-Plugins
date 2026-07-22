#include "V3dModelManager.h"

#include <QApplication>
#include <QColorSpace>
#include <QScrollBar>
#include <QPainter>
#include <QWindow>
#include <cstdlib>
#include <fstream>
#include <set>
#include <QTimer>
#include <QSemaphore>
#include <QThread>

#include <generator.h>
#include <gui/priorities.h>
#include <utils.h>

#include "Utility/EventFilter.h"
#include "Utility/ApplicationEventFilter.h"
#include "Utility/ProtectedFunctionCaller.h"

#include "seconds.h"

#include "rgba.h"
#include "bezierpatch.h"

namespace camp {
    glm::dmat4 projViewMat{ 1.0 };
    glm::dmat3 normMat{ 1.0 };

    const glm::dmat4& getProjViewMat()
    {
        // static glm::dmat4 dummy;
        return projViewMat;
    }
    const glm::dmat3& getNormMat()     { return normMat; }
}

bool fileExists(const std::string& path) {
    std::ifstream f{ path.c_str() };
    return f.good();
}

// Custom event for marshaling IBL download prompts from background threads to main thread.
class IBLPromptEvent : public QEvent {
public:
    using Callback = std::function<bool()>;
    IBLPromptEvent(Callback&& cb, QSemaphore* sem)
        : QEvent(QEvent::Type(QEvent::User + 1)), m_Callback(std::move(cb)), m_Semaphore(sem) {}

    Callback m_Callback;
    QSemaphore* m_Semaphore;
};

// Event filter installed on qApp to catch IBLPromptEvent and run the callback on the main thread.
class IBLEventFilter : public QObject {
public:
    // NO Q_OBJECT macro -- we only need eventFilter, no signals/slots.
    explicit IBLEventFilter(QObject* parent) : QObject(parent) {}

    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() == QEvent::Type(QEvent::User + 1)) {
            IBLPromptEvent* promptEvent = static_cast<IBLPromptEvent*>(event);
            promptEvent->m_Callback();
            if (promptEvent->m_Semaphore) {
                promptEvent->m_Semaphore->release();
            }
            // Do NOT delete event here -- Qt owns it and will delete it after
            // the filter chain returns. Deleting it ourselves causes use-after-free
            // when sendPostedEvents tries to clean up its smart pointer to the event.
            return true;
        }
        return false;
    }
};

V3dModelManager::V3dModelManager(const Okular::Document* document) 
    : m_Document(document)
    , m_HeadlessRenderer(nullptr) 
    , m_StartTime(std::chrono::system_clock::now()) {

    const std::vector<std::string> shaderSearchPaths {
        "./",
        "/usr/lib64/qt6/plugins/okular_generators/",
        "/usr/lib/x86_64-linux-gnu/qt6/plugins/okular/generators/",
        "C:\\Program Files\\Okular\\bin\\"
    };

    std::string shaderPath = "";

    for (const auto& path : shaderSearchPaths) {
        if (fileExists(path + "vertex.glsl") && fileExists(path + "fragment.glsl")) {
            shaderPath = path;
            break;
        }
    }

    if (shaderPath == "") {
        std::cout << "Shaders could not be found, disabling v3d rendering." << std::endl;
        // Do NOT call std::exit() -- this runs for every PDF opened.
        // m_HeadlessRenderer stays nullptr; RenderModel will return a blank image.
    } else {
        m_HeadlessRenderer = std::make_unique<HeadlessRenderer>(shaderPath);
    }

    m_PageView = GetPageViewWidget();

    // Parent filters to our own QObject so we fully control their lifetime.
    // Do NOT parent them to m_PageView or qApp -- those parents would delete
    // the filters independently during Qt teardown, causing double-frees.
    if (m_PageView) {
        m_EventFilter = new EventFilter(&m_FilterParent, this);
        m_PageView->viewport()->installEventFilter(m_EventFilter);
    }

    m_ApplicationEventFilter = new ApplicationEventFilter(&m_FilterParent, this);
    qApp->installEventFilter(m_ApplicationEventFilter);

    // Install IBL event filter on qApp to handle cross-thread IBL download prompts.
    m_IBLEventFilter = new IBLEventFilter(&m_FilterParent);
    qApp->installEventFilter(m_IBLEventFilter);
}

V3dModelManager::~V3dModelManager() {
    m_Destroying = true;

    // Don't touch m_PendingTimers -- they may have been created on a different thread.
    // The m_Destroying flag prevents callbacks from accessing destroyed members.
    // Timers self-destruct when their singleShot fires or Okular exits.
    // Null out back-references first so in-flight events are safe.
    if (m_EventFilter) {
        m_EventFilter->modelManager = nullptr;
    }
    if (m_ApplicationEventFilter) {
        m_ApplicationEventFilter->modelManager = nullptr;
    }

    // Unregister from Qt event chains before the filters are destroyed.
    QCoreApplication* app = QCoreApplication::instance();
    if (app && m_ApplicationEventFilter) {
        app->removeEventFilter(m_ApplicationEventFilter);
    }
    if (app && m_IBLEventFilter) {
        app->removeEventFilter(m_IBLEventFilter);
    }
    if (m_PageView && m_EventFilter) {
        m_PageView->viewport()->removeEventFilter(m_EventFilter);
    }

    // m_FilterParent destroys both filters as its children.
}



void V3dModelManager::AddModel(V3dModel model, size_t pageNumber) {
    QMutexLocker locker(&m_ModelsMutex);

    if (m_Models.size() < pageNumber + 1) {
        m_Models.resize(pageNumber + 1);
    }
    m_Models[pageNumber].emplace_back(std::move(model));

    m_Models[pageNumber].back().initProjection();

    if (m_ModelImages.size() < pageNumber + 1) {
        m_ModelImages.resize(pageNumber + 1);
    }
    m_ModelImages[pageNumber].push_back(QImage{ });
}

QImage V3dModelManager::RenderModel(size_t pageNumber, size_t modelIndex, int imageWidth, int imageHeight) {
    QMutexLocker locker(&m_ModelsMutex);

    // If shaders were not found at construction, rendering is disabled.
    if (!m_HeadlessRenderer) {
        QImage image{ imageWidth, imageHeight, QImage::Format_ARGB32 };
        image.fill(Qt::black);
        return image;
    }

    // Guard against zero/negative dimensions (tiny models, small requests) which
    // produce NaN projection matrices or invalid Vulkan extents.
    if (imageWidth <= 0 || imageHeight <= 0) {
        QImage image{ std::max(imageWidth, 1), std::max(imageHeight, 1), QImage::Format_ARGB32 };
        image.fill(Qt::black);
        return image;
    }

    // Check IBL availability early -- if the model needs IBL but files aren't
    // available yet, defer rendering entirely until download completes.
    std::string imageName = m_Models[pageNumber][modelIndex].file->headerInfo.imageName;
    bool useIBL = false;
    std::string iblPath = "";
    if (!imageName.empty() && !m_IBLDeclined) {
        iblPath = resolveIBLPath(imageName, /*doSchedule=*/false);
        if (iblPath.empty()) {
            // IBL missing -- track this model and return blank.
            m_ModelsWithMissingIBL.insert({pageNumber, modelIndex});

            // Marshal the download prompt to the main thread via custom event.
            // Post-and-forget: we can't wait synchronously because Okular's
            // background worker thread doesn't process events during PDF loading.
            // The popup will appear once Okular returns to its main event loop.
            std::string base = getIBLBase();
            scheduleIBLPrompt(imageName, base);

            QImage image{ imageWidth, imageHeight, QImage::Format_ARGB32 };
            image.fill(Qt::black);
            return image;
        }
        useIBL = true;
    }

    if (!m_Models[pageNumber][modelIndex].m_HasChanged && 
        m_ModelImages[pageNumber][modelIndex].width() == imageWidth &&
        m_ModelImages[pageNumber][modelIndex].height() == imageHeight) {

        return m_ModelImages[pageNumber][modelIndex];
    }

    m_Models[pageNumber][modelIndex].remesh = true; // TODO

    // Projection
    EnsureCachedRequestSize(pageNumber);
    glm::vec2 canvasSize = {
        (m_Models[pageNumber][modelIndex].maxBound.x - m_Models[pageNumber][modelIndex].minBound.x) * m_CachedRequestSizes[pageNumber].size.x,
        (m_Models[pageNumber][modelIndex].maxBound.y - m_Models[pageNumber][modelIndex].minBound.y) * m_CachedRequestSizes[pageNumber].size.y,
    };

    // Guard against zero canvas size which causes division-by-zero in setProjection.
    if (canvasSize.x <= 0.0f || canvasSize.y <= 0.0f) {
        QImage image{ imageWidth, imageHeight, QImage::Format_ARGB32 };
        image.fill(Qt::black);
        return image;
    }

    m_Models[pageNumber][modelIndex].setProjection(canvasSize);

    triple sceneMinBound = m_Models[pageNumber][modelIndex].viewParam.minValues;
    triple sceneMaxBound = m_Models[pageNumber][modelIndex].viewParam.maxValues;

    if(sceneMinBound.getx() >= sceneMaxBound.getx() || sceneMinBound.gety() >= sceneMaxBound.gety() || sceneMinBound.getz() >= sceneMaxBound.getz()) {
        sceneMinBound = m_Models[pageNumber][modelIndex].file->headerInfo.minBound;
        sceneMaxBound = m_Models[pageNumber][modelIndex].file->headerInfo.maxBound;
    }

    using namespace std;
    using namespace camp;

    static glm::mat4 const verticalFlipMat = glm::scale(glm::dmat4(1.0f), glm::dvec3(1.0f, -1.0f, 1.0f));

    projViewMat = glm::dmat4{ verticalFlipMat * m_Models[pageNumber][modelIndex].projectionMatrix * m_Models[pageNumber][modelIndex].viewMatrix };

    auto valPtr = glm::value_ptr(projViewMat);

    normMat = glm::dmat3{ glm::inverse(m_Models[pageNumber][modelIndex].viewMatrix) };

    if (m_ReQueueModels || m_Models[pageNumber][modelIndex].remesh) {
        // Always clean up existing mesh data on the GPU before uploading new data.
        // Using the renderer's own flag ensures we also catch stale data left over
        // from a previous document (where the per-model 'initialized' flag is false
        // for the newly-added model but the GPU still holds the old mesh).
        if (m_HeadlessRenderer->meshInitialized) {
            m_HeadlessRenderer->cleanupMeshData();

            materialData.clear();
            colorData.clear();
        }

        bool orthographic = m_Models[pageNumber][modelIndex].file->headerInfo.orthographic;

        m_Models[pageNumber][modelIndex].file->QueueMesh(imageWidth, imageHeight, sceneMinBound, sceneMaxBound, m_Models[pageNumber][modelIndex].remesh, orthographic);
    }

    Mesh mesh = m_Models[pageNumber][modelIndex].file->GetMesh();

    if (mesh.vertices.empty() || mesh.indices.empty()) {
        QImage image{ imageWidth, imageHeight, QImage::Format_ARGB32 };

        image.fill(Qt::black);

        return image;
    }

    if (m_ReQueueModels || m_Models[pageNumber][modelIndex].remesh) {
        m_HeadlessRenderer->copyMeshToGPU(mesh);

        m_Models[pageNumber][modelIndex].remesh = false;
        m_ReQueueModels = false;
    }

    m_Models[pageNumber][modelIndex].initialized = true;

    VkSubresourceLayout imageSubresourceLayout;

    glm::vec4 bgColor{
        m_Models[pageNumber][modelIndex].file->headerInfo.background.r,
        m_Models[pageNumber][modelIndex].file->headerInfo.background.g,
        m_Models[pageNumber][modelIndex].file->headerInfo.background.b,
        m_Models[pageNumber][modelIndex].file->headerInfo.background.a
    };

    // IBL is enabled when the v3d header contains an environment map name.
    // Already resolved above -- use the values from the early check.
    unsigned char* imageData = m_HeadlessRenderer->render(
        glm::ivec2{ imageWidth, imageHeight }, 
        &imageSubresourceLayout, 
        m_Models[pageNumber][modelIndex].viewMatrix, 
        m_Models[pageNumber][modelIndex].projectionMatrix,
        m_Models[pageNumber][modelIndex].file->materials,
        std::vector<V3dHeaderInfo::Light>{ m_Models[pageNumber][modelIndex].file->headerInfo.light },
        mesh.pipelineMode,
        bgColor,
        m_Models[pageNumber][modelIndex].file->headerInfo.orthographic,
        useIBL,
        iblPath
    );

    unsigned char* imgDataTmp = imageData;

    // renderheadless.cpp clamps targetSize to maxFramebufferWidth/Height.
    // Use the actual clamped dimensions from the subresource layout to avoid
    // reading past the end of the returned buffer.
    int32_t actualHeight = imageSubresourceLayout.size / imageSubresourceLayout.rowPitch;
    int32_t actualRowPixels = imageSubresourceLayout.rowPitch / 4;

    std::vector<unsigned char> vectorData;
    vectorData.reserve(imageWidth * imageHeight * 4);

    for (int32_t y = 0; y < actualHeight && y < imageHeight; y++) {
        unsigned int *row = (unsigned int*)imgDataTmp;
        size_t rowBytes = std::min(actualRowPixels, imageWidth) * 4;
    
        vectorData.resize(vectorData.size() + rowBytes);
        std::memcpy(vectorData.data() + vectorData.size() - rowBytes, (unsigned char*)row, rowBytes);

        imgDataTmp += imageSubresourceLayout.rowPitch;
    }

    delete[] imageData;

    // Vulkan outputs BGRA bytes (VK_FORMAT_B8G8R8A8_UNORM).
    // On little-endian x86, QImage::Format_ARGB32 stores bytes as B,G,R,A --
    // so the byte order matches directly. No swap needed.
    QImage image{ vectorData.data(), imageWidth, imageHeight, QImage::Format_ARGB32 };
    image = image.copy();  // Deep copy so we don't alias vectorData's buffer

    // Prevent Qt/Okular from applying sRGB gamma correction on display.
    // The shader handles color space (OUTPUT_AS_SRGB when srgb=true).
    // With no managed color space, Qt treats pixel values as-is.
    image.setColorSpace(QColorSpace());

    image = image.mirrored(false, true);

    // Force all pixels to fully opaque so Okular draws them directly
    // without alpha compositing over its paperColor background.
    for (int y = 0; y < imageHeight; y++) {
        unsigned char *row = image.scanLine(y);
        for (int x = 0; x < imageWidth; x++) {
            row[x * 4 + 3] = 255;
        }
    }

    m_Models[pageNumber][modelIndex].m_HasChanged = false;
    m_ModelImages[pageNumber][modelIndex] = image;

    return image;
}

V3dModel& V3dModelManager::Model(size_t pageNumber, size_t modelIndex) {
    QMutexLocker locker(&m_ModelsMutex);

    if (pageNumber >= m_Models.size()) {
        qWarning() << "V3dModelManager::Model: pageNumber" << pageNumber << "out of range (size =" << m_Models.size() << ")";
        return m_EmptyModel;
    }
    if (modelIndex >= m_Models[pageNumber].size()) {
        qWarning() << "V3dModelManager::Model: modelIndex" << modelIndex << "out of range for page" << pageNumber << "(size =" << m_Models[pageNumber].size() << ")";
        return m_EmptyModel;
    }

    return m_Models[pageNumber][modelIndex];
}

std::vector<V3dModel>& V3dModelManager::Models(size_t pageNumber) {
    QMutexLocker locker(&m_ModelsMutex);

    // Sanity guard: refuse to resize to an absurdly large index
    if (pageNumber > 100000) {
        qWarning() << "V3dModelManager::Models: pageNumber" << pageNumber << "exceeds maximum allowed (100000)";
        return m_EmptyModels;
    }

    if (pageNumber >= m_Models.size()) {
        m_Models.resize(pageNumber + 1);
    }
    return m_Models[pageNumber];
}

bool V3dModelManager::Empty() {
    QMutexLocker locker(&m_ModelsMutex);

    return m_Models.empty();
}

glm::vec2 V3dModelManager::GetCanvasSize(size_t pageNumber) {
    if (pageNumber >= m_Models.size() || m_Models[pageNumber].empty()) {
        qWarning() << "V3dModelManager::GetCanvasSize: pageNumber" << pageNumber << "invalid or empty";
        return glm::vec2{ 0.0f, 0.0f };
    }
    return glm::vec2{ m_Models[pageNumber][0].file->headerInfo.canvasWidth, m_Models[pageNumber][0].file->headerInfo.canvasHeight };
}

void V3dModelManager::SetDocument(const Okular::Document* document) {
    m_Document = document;
    m_ActiveModel = nullptr;
    m_ActiveModelPage = -1;
    m_Dragging = false;
}

bool V3dModelManager::mouseMoveEvent(QMouseEvent* event) {
    if (m_Models.size() == 0 || !m_PageView) {
        // If the document has no models, this is just a plain PDF document, no need for any special interaction
        return false;
    }

    // Always keep an updated record of the mouse position even if the mouse is not down
    m_MousePosition.x = (int)event->position().x();
    m_MousePosition.y = (int)event->position().y();

    if (!m_Dragging) { m_LastMousePosition = m_MousePosition; return false; }

    {
        QMutexLocker locker(&m_ModelsMutex);

    glm::vec2 normalizedMousePositionOnPage = GetNormalizedPositionRelativeToPage(m_MousePosition, m_ActiveModelPage);
    glm::vec2 lastNormalizedMousePositionOnPage = GetNormalizedPositionRelativeToPage(m_LastMousePosition, m_ActiveModelPage);

    V3dModel& model = *m_ActiveModel;

#ifdef MOUSE_BOUNDARIES
    if (m_ActiveModelPage >= 0) {

        float dpr = GetDevicePixelRatio();

        int pg = GetPageMouseIsOver();

        EnsureCachedRequestSize((size_t)pg);

        int leftPixel = model.minBound.x * m_CachedRequestSizes[pg].size.x;
        int rightPixel = leftPixel + (model.maxBound.x - model.minBound.x) * m_CachedRequestSizes[pg].size.x;

        int topPixel = model.minBound.y * m_CachedRequestSizes[pg].size.y;
        int bottomPixel = topPixel + (model.maxBound.y - model.minBound.y) * m_CachedRequestSizes[pg].size.y;

        glm::vec2 mousePositionPixelSpace = normalizedMousePositionOnPage * glm::vec2{ m_CachedRequestSizes[pg].size };

        m_MouseBoundaryLines[m_ActiveModelPage].push_back(Line{ glm::vec2{ leftPixel, topPixel }, glm::vec2{ rightPixel, topPixel } });
        m_MouseBoundaryLines[m_ActiveModelPage].push_back(Line{ glm::vec2{ leftPixel, topPixel }, glm::vec2{ leftPixel, bottomPixel } });
        m_MouseBoundaryLines[m_ActiveModelPage].push_back(Line{ glm::vec2{ rightPixel, topPixel }, glm::vec2{ rightPixel, bottomPixel } });
        m_MouseBoundaryLines[m_ActiveModelPage].push_back(Line{ glm::vec2{ leftPixel, bottomPixel }, glm::vec2{ rightPixel, bottomPixel } });

        m_MouseBoundaryPoints[m_ActiveModelPage].push_back(Point{ glm::vec2{ mousePositionPixelSpace } });
    }
#endif

    glm::vec2 normalizedPositionOnModel = {
        (normalizedMousePositionOnPage.x - model.minBound.x) / (model.maxBound.x - model.minBound.x),
        (normalizedMousePositionOnPage.y - model.minBound.y) / (model.maxBound.y - model.minBound.y)
    };

    glm::vec2 lastNormalizedPositionOnModel = {
        (lastNormalizedMousePositionOnPage.x - model.minBound.x) / (model.maxBound.x - model.minBound.x),
        (lastNormalizedMousePositionOnPage.y - model.minBound.y) / (model.maxBound.y - model.minBound.y)
    };

    glm::vec2 pageViewSize = { m_PageView->width(), m_PageView->height() };

    bool controlKey = event->modifiers() & Qt::ControlModifier;
    bool shiftKey = event->modifiers() & Qt::ShiftModifier;
    bool altKey = event->modifiers() & Qt::AltModifier;

    if (controlKey && !shiftKey && !altKey) {
        float dpr = GetDevicePixelRatio();
        EnsureCachedRequestSize((size_t)m_ActiveModelPage);

        glm::vec2 canvasSize = {
            (model.maxBound.x - model.minBound.x) * (m_CachedRequestSizes[m_ActiveModelPage].size.x),
            (model.maxBound.y - model.minBound.y) * (m_CachedRequestSizes[m_ActiveModelPage].size.y),
        };

        model.dragModeShift(normalizedPositionOnModel, lastNormalizedPositionOnModel, canvasSize);
        m_ReQueueModels = true;
    } else if (!controlKey && shiftKey && !altKey) {
        model.dragModeZoom(normalizedPositionOnModel, lastNormalizedPositionOnModel, pageViewSize);
    } else if (!controlKey && !shiftKey && altKey) {
        model.dragModePan(normalizedPositionOnModel, lastNormalizedPositionOnModel, pageViewSize);
        m_ReQueueModels = true;
    } else {
        model.dragModeRotate(normalizedPositionOnModel, lastNormalizedPositionOnModel, pageViewSize);
    }

        } // QMutexLocker scope -- release BEFORE requesting refresh

    // requestPixmapRefresh must be called OUTSIDE the mutex. If held while
    // refreshPixmap() calls deletePixmaps(), Okular may request new pixmaps
    // from another thread/context which calls RenderModel() -- that also acquires
    // m_ModelsMutex, causing a deadlock (QMutex is non-recursive) or event-loop
    // corruption that stops MouseMove routing.
    requestPixmapRefresh(m_ActiveModelPage);

    m_LastMousePosition = m_MousePosition;

    return true;
}

bool V3dModelManager::mouseButtonPressEvent(QMouseEvent* event) {
    if (m_Models.size() == 0 || !m_PageView) {
        // If the document has no models, this is just a plain PDF document, no need for any special interaction
        return false;
    }

    // Always keep an updated record of the mouse position even if the click is not the left button
    m_MousePosition.x = (int)event->position().x();
    m_MousePosition.y = (int)event->position().y();

    if (!(event->button() & Qt::MouseButton::LeftButton)) {
        return false;
    }

    int pageMouseIsOver = GetPageMouseIsOver();

    if (pageMouseIsOver == -1 || pageMouseIsOver >= (int)m_Models.size()) return false;

    glm::vec2 normalizedMousePositionOnPage = GetNormalizedPositionRelativeToPage(m_MousePosition, pageMouseIsOver);

    V3dModel* modelMouseIsOver = nullptr;
    {
        QMutexLocker locker(&m_ModelsMutex);

    for (auto& model : m_Models[pageMouseIsOver]) {
        bool horizontallyOnModel = normalizedMousePositionOnPage.x > model.minBound.x && normalizedMousePositionOnPage.x < model.maxBound.x;
        bool verticallyOnModel = normalizedMousePositionOnPage.y > model.minBound.y && normalizedMousePositionOnPage.y < model.maxBound.y;

        if (!(horizontallyOnModel && verticallyOnModel)) continue; // Making the assumption no two models overlap

        modelMouseIsOver = &model;
        break;
        } // QMutexLocker scope
    }

    if (modelMouseIsOver != nullptr) {
        m_Dragging = true;
        m_LastMousePosition = m_MousePosition;
        m_ActiveModel = modelMouseIsOver;
        m_ActiveModelPage = pageMouseIsOver;

        return true;
    }

    return false;
}

bool V3dModelManager::mouseButtonReleaseEvent(QMouseEvent* event) {
    if (m_Models.size() == 0 || !m_PageView) {
        // If the document has no models, this is just a plain PDF document, no need for any special interaction
        return false;
    }

    if (!(event->button() & Qt::MouseButton::LeftButton)) {
        return false;
    }

    m_Dragging = false;
    m_ActiveModel = nullptr;
    m_ActiveModelPage = -1;

    return false;
}

bool V3dModelManager::wheelEvent(QWheelEvent* event) {
    if (m_Models.size() == 0 || !m_PageView) {
        // If the document has no models, this is just a plain PDF document, no need for any special interaction
        return false;
    }

    // Only allow wheel zoom for standalone v3d files
    // (single model that fills the entire page). For PDFs with embedded
    // models, wheel always scrolls to avoid fighting Okular's scroll.
    V3dModel* targetModel = nullptr;
    int targetPage = -1;

    if (m_Models.size() == 1 && m_Models[0].size() == 1) {
        const auto& model = m_Models[0][0];
        if (model.minBound.x <= 0.0f && model.maxBound.x >= 1.0f &&
            model.minBound.y <= 0.0f && model.maxBound.y >= 1.0f) {
            // Standalone v3d: zoom directly
            targetModel = &m_Models[0][0];
            targetPage = 0;
        }
    }

    // If already actively dragging a model, use that instead
    if (m_Dragging && m_ActiveModel) {
        targetModel = m_ActiveModel;
        targetPage = m_ActiveModelPage;
    }

    if (targetModel == nullptr || targetPage == -1) {
        return false;
    }

    {
        QMutexLocker locker(&m_ModelsMutex);

    glm::vec2 normalizedPositionOnPage = GetNormalizedPositionRelativeToPage(m_MousePosition, targetPage);

    bool horizontallyOnModel = normalizedPositionOnPage.x > targetModel->minBound.x && normalizedPositionOnPage.x < targetModel->maxBound.x;
    bool verticallyOnModel = normalizedPositionOnPage.y > targetModel->minBound.y && normalizedPositionOnPage.y < targetModel->maxBound.y;

    if (!horizontallyOnModel || !verticallyOnModel) {
        return false;
    }

    if (event->angleDelta().y() < 0) {
        targetModel->zoom /= targetModel->file->headerInfo.zoomFactor;
    } else {
        targetModel->zoom *= targetModel->file->headerInfo.zoomFactor;
    }

    float maxZoom = std::sqrt(std::numeric_limits<float>::max());
    float minZoom = 1 / maxZoom;

    if (targetModel->zoom < minZoom) {
        targetModel->zoom = minZoom;
    } else if (targetModel->zoom > maxZoom) {
        targetModel->zoom = maxZoom;
    }

    float dpr = GetDevicePixelRatio();
    EnsureCachedRequestSize((size_t)targetPage);

    glm::vec2 canvasSize = {
        (targetModel->maxBound.x - targetModel->minBound.x) * (m_CachedRequestSizes[targetPage].size.x / dpr),
        (targetModel->maxBound.y - targetModel->minBound.y) * (m_CachedRequestSizes[targetPage].size.y / dpr),
    };

    targetModel->setProjection(canvasSize);
        } // QMutexLocker scope

    requestPixmapRefresh((size_t)targetPage);

    return true;
}

bool V3dModelManager::keyPressEvent(QKeyEvent* event) {
    if (m_Models.size() == 0 || !m_PageView) {
        return false;
    }

    if (event->key() == Qt::Key_H) {
        int pageMouseIsOver = GetPageMouseIsOver();
        if (pageMouseIsOver == -1 || pageMouseIsOver >= (int)m_Models.size()) return false;

        glm::vec2 normalizedMousePositionOnPage = GetNormalizedPositionRelativeToPage(m_MousePosition, pageMouseIsOver);
        for (auto& model : m_Models[pageMouseIsOver]) {
            bool horizontallyOnModel = normalizedMousePositionOnPage.x > model.minBound.x && normalizedMousePositionOnPage.x < model.maxBound.x;
            bool verticallyOnModel = normalizedMousePositionOnPage.y > model.minBound.y && normalizedMousePositionOnPage.y < model.maxBound.y;

            if (horizontallyOnModel && verticallyOnModel) {
                model.home();
                requestPixmapRefresh(pageMouseIsOver);
                break;
            }
        }
        return true;
    }

    return false;
}

void V3dModelManager::DrawMouseBoundaries(QImage* img, size_t pageNumber) {
#ifdef MOUSE_BOUNDARIES

    if (img == nullptr) {
        return;
    }

    QPainter painter{ img };

    if (m_MouseBoundaryLines.size() > pageNumber) {

        for (auto line : m_MouseBoundaryLines[pageNumber]) {
            painter.setPen(line.color);
            painter.setBrush(line.color);

            painter.drawLine((int)line.start.x, (int)line.start.y, (int)line.end.x, (int)line.end.y);
        }

        m_MouseBoundaryLines[pageNumber].clear();
    }

    if (m_MouseBoundaryPoints.size() > pageNumber) {
        
        constexpr int pointRadius = 5;

        for (auto point : m_MouseBoundaryPoints[pageNumber]) {
            painter.setPen(point.color);
            painter.setBrush(point.color);

            painter.drawEllipse(point.pos.x - pointRadius, point.pos.y - pointRadius, 2 * pointRadius, 2 * pointRadius);
        }

        m_MouseBoundaryPoints[pageNumber].clear();
    }

#endif
}

void V3dModelManager::CacheRequest(Okular::PixmapRequest* request) {
    Okular::Page* page = request->page();

    if (request->priority() != PAGEVIEW_PRIO && request->priority() != PAGEVIEW_PRELOAD_PRIO) {
        if (m_CachedRequestSizes.size() < (size_t)(page->number() + 1) || (m_CachedRequestSizes[page->number()].size.x == 0 && m_CachedRequestSizes[page->number()].size.y == 0)) {
            CacheRequestSize(page->number(), request->width(), request->height(), request->priority());
        }

        return;
    }

    CacheRequestSize(page->number(), request->width(), request->height(), request->priority());
    CachePage(page->number(), page);

#ifdef MOUSE_BOUNDARIES
    if (m_MouseBoundaryLines.size() < page->number() + 1) {
        m_MouseBoundaryLines.resize(page->number() + 1);
        m_MouseBoundaryPoints.resize(page->number() + 1);
    }
#endif
}

void V3dModelManager::EnsureCachedRequestSize(size_t pageNumber) {
    if (m_CachedRequestSizes.size() <= pageNumber) {
        m_CachedRequestSizes.resize(pageNumber + 1);
    }
}

void V3dModelManager::CacheRequestSize(size_t pageNumber, int width, int height, int priority) {
    if (m_CachedRequestSizes.size() < pageNumber + 1) {
        m_CachedRequestSizes.resize(pageNumber + 1);
    }

    std::chrono::duration<double> requestTime = std::chrono::system_clock::now() - m_StartTime;

    if (requestTime > m_CachedRequestSizes[pageNumber].requestTime) {
        m_CachedRequestSizes[pageNumber] = RequestCache{ 
            glm::ivec2{ width, height }, 
            priority,
            requestTime
        };
    }
}

void V3dModelManager::CachePage(size_t pageNumber, Okular::Page* page) {
    if (page == nullptr) {
        return;
    }

    if (m_Pages.size() < pageNumber + 1) {
        m_Pages.resize(pageNumber + 1);
    }

    m_Pages[pageNumber] = page;
}

float V3dModelManager::GetDevicePixelRatio() {
    auto window = QApplication::activeWindow();

    if (window != nullptr) {
        return window->devicePixelRatio();

    } else {
        return qGuiApp->devicePixelRatio();
    }
}

std::vector<V3dModelManager::PageBorders> V3dModelManager::GetPageBordersForVisiblePages() {
    if (!m_PageView) return {};

    auto visiblePages = m_Document->visiblePageRects();

    std::vector<PageBorders> pageBorders{ };

    if (visiblePages.size() == 0) return pageBorders;

    pageBorders.resize(visiblePages.size());

    glm::vec2 viewPortSize{ m_PageView->width(), m_PageView->height() };
    float dpr = GetDevicePixelRatio();

    if (visiblePages.size() == 1) {
        // There is exactly one visible page
        Okular::NormalizedRect rect = visiblePages[0]->rect;

        pageBorders[0].pageNumber = visiblePages[0]->pageNumber;
        EnsureCachedRequestSize(pageBorders[0].pageNumber);

        glm::vec2 pageSize{ m_CachedRequestSizes[pageBorders[0].pageNumber].size.x / dpr, m_CachedRequestSizes[pageBorders[0].pageNumber].size.y / dpr };

        if (rect.left == 0.0f && rect.right == 1.0f && rect.top == 0.0f && rect.bottom == 1.0f) {
            // Page is fully visible and centered in all directions
            pageBorders[0].hi = pageBorders[0].lo = (viewPortSize.y - pageSize.y) / 2.0f;
            pageBorders[0].le = pageBorders[0].ri = (viewPortSize.x - pageSize.x) / 2.0f;
        }
        else if (rect.left == 0.0f && rect.right == 1.0f) {
            // Page is horizontaly centered
            pageBorders[0].hi = -rect.top * pageSize.y;
            pageBorders[0].lo = -(1.0f - rect.bottom) * pageSize.y;
            pageBorders[0].le = pageBorders[0].ri = (viewPortSize.x - pageSize.x) / 2.0f;
        }
        else if (rect.top == 0.0f && rect.bottom == 1.0f) {
            // Page is vertically centered
            pageBorders[0].hi = pageBorders[0].lo = (viewPortSize.y - pageSize.y) / 2.0f;
            pageBorders[0].le = -rect.left * pageSize.x;
            pageBorders[0].ri = -(1.0f - rect.right) * pageSize.x;
        }
        else {
            // Page fills the entire viewport
            pageBorders[0].hi = -rect.top * pageSize.y;
            pageBorders[0].lo = -(1.0f - rect.bottom) * pageSize.y;
            pageBorders[0].le = -rect.left * pageSize.x;
            pageBorders[0].ri = -(1.0f - rect.right) * pageSize.x;
        }
    }
    else {
        // There is more then one visible page

        size_t i = 0;
        for (auto& page : visiblePages) {
            Okular::NormalizedRect rect = page->rect;
            EnsureCachedRequestSize(page->pageNumber);
            glm::vec2 pageSize{ m_CachedRequestSizes[page->pageNumber].size.x / dpr, m_CachedRequestSizes[page->pageNumber].size.y / dpr };

            pageBorders[i].pageNumber = page->pageNumber;

            if (rect.left == 0.0f && rect.right == 1.0f) {
                // Either horizontally centerd or edge case #1
                pageBorders[i].le = pageBorders[i].ri = (viewPortSize.x - pageSize.x) / 2.0f;
            }
            else if (rect.left != 0.0f && rect.right != 1.0f) {
                pageBorders[i].le = -rect.left * pageSize.x;
                pageBorders[i].ri = -rect.right * pageSize.x;
            }
            else {
                // EDGE CASE // TODO
            }

            ++i;
        }

        // First page
        auto& firstPage = visiblePages[0];
        Okular::NormalizedRect firstPageRect = firstPage->rect;
        EnsureCachedRequestSize(firstPage->pageNumber);
        glm::vec2 firstPageSize{ m_CachedRequestSizes[firstPage->pageNumber].size.x / dpr, m_CachedRequestSizes[firstPage->pageNumber].size.y / dpr };

        pageBorders[0].pageNumber = firstPage->pageNumber;

        if (firstPageRect.top == 0.0f) {
            // Vertical margin calculation

            float totalPageHeight = 0;
            for (int i = visiblePages[0]->pageNumber; i <= visiblePages[visiblePages.size() - 1]->pageNumber; ++i) {
                EnsureCachedRequestSize((size_t)i);
                totalPageHeight += m_CachedRequestSizes[i].size.y / dpr;
            }

            totalPageHeight += (visiblePages.size() - 1.0f) * 10.0f; // Margins

            float verticalMargin = (viewPortSize.y - totalPageHeight) / 2.0f;

            pageBorders[0].hi = verticalMargin;
            pageBorders[0].lo = viewPortSize.y - pageBorders[0].hi - firstPageSize.y;
        }
        else {
            pageBorders[0].hi = -firstPageRect.top * firstPageSize.y;
            pageBorders[0].lo = viewPortSize.y - (1.0f - firstPageRect.top) * firstPageSize.y;
        }

        // All other pages
        i = 0;
        for (auto& page : visiblePages) {
            if (i == 0) { ++i; continue; } // First page is the base case and is handled above

            EnsureCachedRequestSize(page->pageNumber);
            glm::vec2 pageSize{ m_CachedRequestSizes[page->pageNumber].size.x / dpr, m_CachedRequestSizes[page->pageNumber].size.y / dpr };

            pageBorders[i].hi = viewPortSize.y - pageBorders[i - 1].lo + 10;
            pageBorders[i].lo = viewPortSize.y - pageBorders[i].hi - pageSize.y;

            ++i;
        }
    }

    return pageBorders;
}

int V3dModelManager::GetPageMouseIsOver() {
    std::vector<PageBorders> pageBorders = GetPageBordersForVisiblePages();

    glm::vec2 viewPortSize{ m_PageView->width(), m_PageView->height() };

    for (auto& border : pageBorders) {
        if (m_MousePosition.x > border.le &&
            m_MousePosition.x < (viewPortSize.x - border.ri) &&
            m_MousePosition.y > border.hi &&
            m_MousePosition.y < (viewPortSize.y - border.lo)) {

            return border.pageNumber;
        }
    }

    return -1;
}

glm::vec2 V3dModelManager::GetNormalizedPositionRelativeToPage(const glm::vec2& pos, int pageNumber) {
    std::vector<PageBorders> pageBorders = GetPageBordersForVisiblePages();

    float dpr = GetDevicePixelRatio();
    EnsureCachedRequestSize((size_t)pageNumber);
    glm::vec2 pageSize{ m_CachedRequestSizes[pageNumber].size.x / dpr, m_CachedRequestSizes[pageNumber].size.y / dpr };

    auto it = std::find_if(pageBorders.begin(), pageBorders.end(), [&pageNumber](PageBorders border){
        return border.pageNumber == pageNumber;
    });

    if (it == pageBorders.end()) {
        return glm::vec2{ 0.0f, 0.0f };
    }

    PageBorders& border = *it;

    // Guard against zero page size which causes division-by-zero.
    if (pageSize.x <= 0.0f || pageSize.y <= 0.0f) {
        return glm::vec2{ 0.0f, 0.0f };
    }

    return (pos - glm::vec2{ border.le, border.hi }) / pageSize;
}

QAbstractScrollArea* V3dModelManager::GetPageViewWidget() {
    QAbstractScrollArea* pageView = nullptr;

    for (QWidget* widget : QApplication::allWidgets()) {
        // Okular's PageView class inherits from QAbstractScrollArea and has the
        // Q_OBJECT macro, so its meta-object class name is "PageView".  Using the
        // Qt meta-object system to identify it by type is far more robust than
        // counting children (QBoxLayout / QFrame heuristics) which can break if
        // Okular's internal widget hierarchy changes.
        if (!widget->inherits("PageView")) {
            continue;
        }

        QAbstractScrollArea* scrollArea = dynamic_cast<QAbstractScrollArea*>(widget);
        if (scrollArea == nullptr) {
            continue;
        }

        if (pageView != nullptr) {
            std::cout << "ERROR, multiple pageViews found" << std::endl;
        }

        pageView = scrollArea;
    }

    if (pageView == nullptr) {
        std::cout << "ERROR, pageview was not found" << std::endl;
    }

    return pageView;
}

void V3dModelManager::requestPixmapRefresh(size_t pageNumber) {
    auto elapsedTime = std::chrono::system_clock::now() - m_LastPixmapRefreshTime;

    auto elapsedTimeSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(elapsedTime);

    if (elapsedTimeSeconds > m_MinTimeBetweenRefreshes) {
        refreshPixmap(pageNumber);
        m_LastPixmapRefreshTime = std::chrono::system_clock::now();
    }
}

void V3dModelManager::refreshPixmap(size_t pageNumber) {
    if (pageNumber >= m_Pages.size() || !m_Pages[pageNumber]) {
        return;
    }
    m_Pages[pageNumber]->deletePixmaps();

    QKeyEvent keyEvent(
        QEvent::KeyRelease, // type
        Qt::Key_Control,    // key
        Qt::NoModifier      // modifiers
    );

    ProtectedFunctionCaller::callKeyReleaseEvent(m_PageView, &keyEvent);
}

std::string V3dModelManager::resolveIBLPath(const std::string& imageName, bool doSchedule) {
    if (imageName.empty()) return "";

    // Strip leading "/" from old files that may still store an absolute path.
    // Preserve relative paths (e.g., "snowyField/refl0") in case we use subdirs later.
    std::string name = imageName;
    if (!name.empty() && name[0] == '/') {
        size_t lastSlash = name.find_last_of('/');
        name = (lastSlash != 0) ? name.substr(lastSlash + 1) : name.substr(1);
    }
    // Remove trailing slash if present.
    while (!name.empty() && name.back() == '/') name.pop_back();
    if (name.empty()) return "";

    // Helper: check if a path exists and return it.
    auto tryPath = [](const std::string& path) -> std::string {
        std::ifstream f(path.c_str());
        return f.good() ? path : "";
    };

    // The iblPath must point to a directory containing diffuse.exr + refl0..10.exr.
    // We also verify that the parent directory has refl.exr (common BRDF LUT).
    auto tryIBLDir = [&tryPath](const std::string& dir) -> std::string {
        if (!dir.empty() && dir.back() == '/') return dir;
        std::string d = dir + "/";
        // Check diffuse.exr exists in this directory.
        std::string diffuse = tryPath(d + "diffuse.exr");
        if (diffuse.empty()) return "";
        // Check refl.exr exists in the parent directory.
        std::string parent = d.substr(0, d.size() - 1);
        size_t slash = parent.find_last_of('/');
        if (slash == std::string::npos) return "";
        std::string parentDir = parent.substr(0, slash + 1);
        std::string refl = tryPath(parentDir + "refl.exr");
        if (refl.empty()) return "";
        return d.substr(0, d.size() - 1);  // Return without trailing slash.
    };

    auto getIBLBase = []() -> std::string {
        const char* envDir = std::getenv("OKULAR_V3D_IMAGE_DIR");
        if (envDir && strlen(envDir) > 0) return std::string(envDir);
        const char* xdgDataHome = std::getenv("XDG_DATA_HOME");
        if (xdgDataHome && strlen(xdgDataHome) > 0) {
            return std::string(xdgDataHome) + "/okular/ibl";
        }
        const char* home = std::getenv("HOME");
        if (home) return std::string(home) + "/.local/share/okular/ibl";
        return "";
    };

    std::string iblBase = getIBLBase();

    // Strategy A: name contains a "/" -- it's a relative subdirectory path (e.g., "snowyField/refl0").
    // Try as-is first.
    if (name.find('/') != std::string::npos) {
        std::string dir = iblBase + "/" + name;
        std::string found = tryIBLDir(dir);
        if (!found.empty()) return found;
    }

    // Strategy B: name is a bare identifier (e.g., "snowyField").
    // 1. Check <iblBase>/<name>/ as an IBL subdirectory.
    std::string dir = iblBase + "/" + name;
    std::string found = tryIBLDir(dir);
    if (!found.empty()) return found;

    // Strategy C: scan all immediate subdirectories of iblBase for one containing diffuse.exr.
    // This handles the case where imageName matches a file inside a subdir we haven't guessed yet.
    if (!iblBase.empty()) {
        QDir qDir(QString::fromStdString(iblBase));
        QStringList subdirs = qDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& sd : subdirs) {
            std::string candidate = iblBase + "/" + sd.toStdString();
            found = tryIBLDir(candidate);
            if (!found.empty()) return found;
        }
    }

    // 4. Not found locally. Schedule deferred download prompt only if requested.
    if (doSchedule) {
        scheduleIBLDownload(name, iblBase);
    }
    return "";
}

void V3dModelManager::scheduleIBLDownload(const std::string& imageName, const std::string& iblBase) {
    // Debounce: avoid spamming the user with repeated prompts for the same image.
    static std::set<std::string> pendingDownloads;
    if (pendingDownloads.count(imageName)) return;

    pendingDownloads.insert(imageName);

    m_PendingIBLImageName = imageName;
    m_PendingIBLIblBase = iblBase;

    // Schedule on the main event loop so we're outside the render path / mutex hold.
    QTimer* timer = new QTimer();
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, [this]() {
        if (!m_Destroying) performIBLDownload(m_PendingIBLImageName, m_PendingIBLIblBase);
    });
    timer->start(0);
}

void V3dModelManager::performIBLDownload(const std::string& imageName, const std::string& iblBase) {
    // If the name contains an absolute path, extract just the filename; relative paths are ok.
    std::string name = imageName;
    if (!name.empty() && name[0] == '/') {
        size_t lastSlash = name.rfind('/');
        name = (lastSlash != 0) ? name.substr(lastSlash + 1) : name.substr(1);
    }

    const char* imageUrlEnv = std::getenv("OKULAR_V3D_IMAGE_URL");
    std::string baseUrl = imageUrlEnv && strlen(imageUrlEnv) > 0
        ? std::string(imageUrlEnv)
        : "https://vectorgraphics.gitlab.io/asymptote/ibl";

    QString qImageName = QString::fromStdString(name);
    QString qIblBase = iblBase.empty()
        ? QDir::homePath() + QStringLiteral("/.local/share/okular/ibl")
        : QString::fromStdString(iblBase);

    // Download directory: <iblBase>/<imageName>/  (e.g., ~/.local/share/okular/ibl/snowyField/)
    QString envDir = qIblBase + QLatin1Char('/') + qImageName;

    // Files to download:
    //   refl.exr          > <iblBase>/refl.exr       (common BRDF LUT, shared by all)
    //   diffuse.exr       > <envDir>/diffuse.exr     (irradiance map)
    //   refl0.exr..refl10.exr  > <envDir>/           (reflection mip levels)
    struct DownloadTask {
        QString remoteFile;  // filename on server
        QString localPath;   // full local path to write
    };

    std::vector<DownloadTask> tasks;

    // refl.exr -- common, stored at <iblBase>/refl.exr on the server.
    QString reflLocal = qIblBase + QStringLiteral("/refl.exr");
    tasks.push_back({QStringLiteral("refl.exr"), reflLocal});

    // Environment map files live in <imageName>/ subdirectory on the server.
    QString remoteSubdir = qImageName + QLatin1Char('/');
    tasks.push_back({remoteSubdir + QStringLiteral("diffuse.exr"), envDir + QStringLiteral("/diffuse.exr")});
    for (int i = 0; i <= 10; ++i) {
        QString fname = QString::fromLatin1("refl%1.exr").arg(i);
        tasks.push_back({remoteSubdir + fname, envDir + QLatin1Char('/') + fname});
    }

    // Build the message.
    QString msg = QString::fromLatin1("The IBL environment map '%1' was not found locally.\n\n")
        .arg(qImageName);
    msg += QString::fromLatin1("Would you like to download it (%1 files) from:\n%2?");
    msg = msg.arg(tasks.size()).arg(QString::fromStdString(baseUrl));

    // Show a QMessageBox asking if the user wants to download.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int result = QMessageBox::question(
        nullptr,
        QStringLiteral("IBL Environment Map Missing"),
        msg,
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes
    );
    QApplication::restoreOverrideCursor();

    if (result != QMessageBox::Yes) {
        // User declined -- render normally without IBL.
        m_IBLDeclined = true;
        std::set<size_t> pages;
        for (const auto& [pn, mi] : m_ModelsWithMissingIBL) {
            pages.insert(pn);
        }
        m_ModelsWithMissingIBL.clear();
        for (size_t pn : pages) {
            QTimer* timer = new QTimer();
            timer->setSingleShot(true);
            QObject::connect(timer, &QTimer::timeout, [this, pn]() {
                if (!m_Destroying) refreshPixmap(pn);
            });
            timer->start(25);
        }
        return;
    }

    QDir dir;
    if (!dir.mkpath(envDir)) {
        std::cout << "Failed to create directory: " << envDir.toStdString() << std::endl;
        return;
    }
    if (!dir.exists(qIblBase) && !dir.mkpath(qIblBase)) {
        std::cout << "Failed to create base IBL directory: " << qIblBase.toStdString() << std::endl;
        return;
    }

    // Start async downloads. Each reply's finished signal calls onIBLDownloadReplyFinished().
    // NOTE: no parent -- we manage lifetime manually to avoid QObject child deletion conflicts.
    m_IBLNetManager = new QNetworkAccessManager();
    m_PendingDownloadCount = 0;

    for (const auto& task : tasks) {
        // Skip refl.exr if it already exists.
        if (task.localPath == reflLocal && QFile::exists(task.localPath)) {
            continue;
        }

        QString urlStr = QString::fromStdString(baseUrl) + QLatin1Char('/') + task.remoteFile;
        QUrl reqUrl(urlStr);
        QNetworkRequest netRequest(reqUrl);
        QNetworkReply* reply = m_IBLNetManager->get(netRequest);

        // Store the local path in the reply's property so we can write it on completion.
        reply->setProperty("localPath", task.localPath);
        reply->setProperty("remoteFile", task.remoteFile);

        m_PendingDownloadCount++;
        // Store a raw pointer to 'this' in the reply so we can route the callback.
        reply->setProperty("manager", QVariant::fromValue(reinterpret_cast<qlonglong>(this)));
        QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply]() {
            onIBLDownloadReplyFinished(reply);
        });
    }

    if (m_PendingDownloadCount == 0) {
        // All files already present -- nothing to download.
        delete m_IBLNetManager;
        m_IBLNetManager = nullptr;
        onIBLDownloadComplete();
    }
}

void V3dModelManager::onIBLDownloadReplyFinished(QNetworkReply* reply) {
    if (m_Destroying) { reply->deleteLater(); return; }

    QString localPath = reply->property("localPath").toString();
    QString remoteFile = reply->property("remoteFile").toString();

    if (reply->error() != QNetworkReply::NoError) {
        std::cout << "IBL download failed for " << remoteFile.toStdString()
                  << ": " << reply->errorString().toStdString() << std::endl;
        reply->deleteLater();
        m_PendingDownloadCount--;
        if (m_PendingDownloadCount == 0) onIBLDownloadComplete();
        return;
    }

    // Reject HTML responses (directory listings, 404 pages).
    QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
    if (contentType.contains(QLatin1String("text/html"))) {
        std::cout << "IBL: server returned HTML for " << remoteFile.toStdString() << ", skipping." << std::endl;
        reply->deleteLater();
        m_PendingDownloadCount--;
        if (m_PendingDownloadCount == 0) onIBLDownloadComplete();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    if (!data.isEmpty()) {
        QFile file(localPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(data);
            file.close();
        } else {
            std::cout << "IBL: failed to write " << localPath.toStdString() << std::endl;
        }
    }

    m_PendingDownloadCount--;
    if (m_PendingDownloadCount == 0) {
        onIBLDownloadComplete();
    }
}

void V3dModelManager::onIBLDownloadComplete() {
    if (m_IBLNetManager) {
        delete m_IBLNetManager;
        m_IBLNetManager = nullptr;
    }

    m_ReQueueModels = true;
    std::set<size_t> affectedPages;
    for (const auto& [pn, mi] : m_ModelsWithMissingIBL) {
        if (m_Models.size() > pn && m_Models[pn].size() > mi) {
            m_Models[pn][mi].m_HasChanged = true;
        }
        affectedPages.insert(pn);
    }
    m_ModelsWithMissingIBL.clear();

    // Force Okular to re-request pixmaps for each affected page.
    // Stagger the calls to avoid the 1/60s refresh throttle.
    for (size_t pn : affectedPages) {
        QTimer* timer = new QTimer();
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, [this, pn]() {
            if (!m_Destroying) refreshPixmap(pn);
        });
        timer->start(25);
    }
}

std::string V3dModelManager::getIBLBase() const {
    const char* envDir = std::getenv("OKULAR_V3D_IMAGE_DIR");
    if (envDir && strlen(envDir) > 0) return std::string(envDir);
    const char* xdgDataHome = std::getenv("XDG_DATA_HOME");
    if (xdgDataHome && strlen(xdgDataHome) > 0) {
        return std::string(xdgDataHome) + "/okular/ibl";
    }
    const char* home = std::getenv("HOME");
    if (home) return std::string(home) + "/.local/share/okular/ibl";
    return "";
}

void V3dModelManager::scheduleIBLPrompt(const std::string& imageName, const std::string& iblBase) {
    // Debounce: avoid spamming the user with repeated prompts for the same image.
    static std::set<std::string> pendingPrompts;
    if (pendingPrompts.count(imageName)) return;

    pendingPrompts.insert(imageName);

    // Post a custom event to qApp's main thread event queue. The IBLEventFilter
    // will catch it and run the callback on the main thread, showing the dialog.
    // This works from both background threads (PDF loading) and the main thread
    // (standalone .v3d), since postEvent always delivers to the receiver's thread.
    IBLPromptEvent* event = new IBLPromptEvent(
        [this, imageName, iblBase]() -> bool {
            performIBLDownload(imageName, iblBase);
            return !m_IBLDeclined;
        },
        nullptr  // No semaphore needed -- post-and-forget.
    );

    QCoreApplication::postEvent(qApp, event);
}
