#include "V3dModelManager.h"

#include <QApplication>
#include <QBoxLayout>
#include <QColorSpace>
#include <QScrollBar>
#include <QPainter>
#include <QWindow>

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
        // Do NOT call std::exit() — this runs for every PDF opened.
        // m_HeadlessRenderer stays nullptr; RenderModel will return a blank image.
    } else {
        m_HeadlessRenderer = std::make_unique<HeadlessRenderer>(shaderPath);
    }

    m_PageView = GetPageViewWidget();

    // Parent filters to our own QObject so we fully control their lifetime.
    // Do NOT parent them to m_PageView or qApp — those parents would delete
    // the filters independently during Qt teardown, causing double-frees.
    if (m_PageView) {
        m_EventFilter = new EventFilter(&m_FilterParent, this);
        m_PageView->viewport()->installEventFilter(m_EventFilter);
    }

    m_ApplicationEventFilter = new ApplicationEventFilter(&m_FilterParent, this);
    qApp->installEventFilter(m_ApplicationEventFilter);
}

V3dModelManager::~V3dModelManager() {
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

    unsigned char* imageData = m_HeadlessRenderer->render(
        glm::ivec2{ imageWidth, imageHeight }, 
        &imageSubresourceLayout, 
        m_Models[pageNumber][modelIndex].viewMatrix, 
        m_Models[pageNumber][modelIndex].projectionMatrix,
        m_Models[pageNumber][modelIndex].file->materials,
        std::vector<V3dHeaderInfo::Light>{ m_Models[pageNumber][modelIndex].file->headerInfo.light },
        mesh.pipelineMode,
        bgColor,
        m_Models[pageNumber][modelIndex].file->headerInfo.orthographic
    );

    unsigned char* imgDataTmp = imageData;

    size_t finalImageSize = imageWidth * imageHeight * 4;

    std::vector<unsigned char> vectorData;
    vectorData.reserve(finalImageSize);

    for (int32_t y = 0; y < imageHeight; y++) {
        unsigned int *row = (unsigned int*)imgDataTmp;
        size_t rowBytes = imageWidth * 4;
    
        vectorData.resize(vectorData.size() + rowBytes);
        std::memcpy(vectorData.data() + vectorData.size() - rowBytes, (unsigned char*)row, rowBytes);

        imgDataTmp += imageSubresourceLayout.rowPitch;
    }

    delete[] imageData;

    // Vulkan outputs BGRA bytes (VK_FORMAT_B8G8R8A8_UNORM).
    // On little-endian x86, QImage::Format_ARGB32 stores bytes as B,G,R,A —
    // so the byte order matches directly. No swap needed.
    QImage image{ vectorData.data(), imageWidth, imageHeight, QImage::Format_ARGB32 };

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

    return m_Models[pageNumber][modelIndex];
}

std::vector<V3dModel>& V3dModelManager::Models(size_t pageNumber) {
    QMutexLocker locker(&m_ModelsMutex);

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

    requestPixmapRefresh(m_ActiveModelPage);
        } // QMutexLocker scope

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
        QAbstractScrollArea* scrollArea = dynamic_cast<QAbstractScrollArea*>(widget);

        if (scrollArea == nullptr) {
            continue;
        }

        QWidget* parent = dynamic_cast<QWidget*>(widget->parent());

        if (parent == nullptr) {
            continue;
        }

        // if (parent->children().size() != 9) {
        if (parent->children().size() < 8) {
            continue;
        }

        int QBoxLayoutCount = 0;
        for (auto child : parent->children()) {
            QBoxLayout* qBox = dynamic_cast<QBoxLayout*>(child);

            if (qBox != nullptr) {
                QBoxLayoutCount += 1;
            }
        }

        if (QBoxLayoutCount == 0) {
            continue;
        }

        int QFrameCount = 0;
        for (auto child : parent->children()) {
            QFrame* qFrame = dynamic_cast<QFrame*>(child);

            if (qFrame != nullptr) {
                QFrameCount += 1;
            }
        }

        // if (QFrameCount != 6) {
        if (QFrameCount < 5) {
            continue;
        }

        if (pageView != nullptr) {
            std::cout << "ERROR, multiple pageViews found" << std::endl;
        }

        pageView = dynamic_cast<QAbstractScrollArea*>(widget);
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
