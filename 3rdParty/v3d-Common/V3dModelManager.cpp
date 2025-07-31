#include "V3dModelManager.h"

#include <QApplication>
#include <QBoxLayout>
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
        if (fileExists(path + "vertex.spv") && fileExists(path + "fragment.spv")) {
            shaderPath = path;
            break;
        }
    }

    if (shaderPath == "") {
        std::cout << "Shaders could not be found, plugin cannot run without shaders." << std::endl;
        std::exit(1);
    }

    m_HeadlessRenderer = std::make_unique<HeadlessRenderer>(shaderPath);

    m_PageView = GetPageViewWidget();

    // Qt takes ownership of event filters once installed, and deletes them when no longer needed
    m_EventFilter = new EventFilter(m_PageView, this);
    m_PageView->viewport()->installEventFilter(m_EventFilter);

    m_ApplicationEventFilter = new ApplicationEventFilter(qApp, this);
    qApp->installEventFilter(m_ApplicationEventFilter);
}

void V3dModelManager::AddModel(V3dModel model, size_t pageNumber) {
    m_Models.resize(pageNumber + 1);
    m_Models[pageNumber].emplace_back(std::move(model));

    m_Models[pageNumber].back().initProjection();

    m_ModelImages.resize(pageNumber + 1);
    m_ModelImages[pageNumber].push_back(QImage{ });
}

QImage V3dModelManager::RenderModel(size_t pageNumber, size_t modelIndex, int imageWidth, int imageHeight) {
    if (!m_Models[pageNumber][modelIndex].m_HasChanged && 
        m_ModelImages[pageNumber][modelIndex].width() == imageWidth &&
        m_ModelImages[pageNumber][modelIndex].height() == imageHeight) {

        return m_ModelImages[pageNumber][modelIndex];
    }

    {
        utils::stopWatch timer{ };

        m_Models[pageNumber][modelIndex].file->QueueMesh(imageWidth, imageHeight);

        std::cout << "Queue Mesh: " << timer.seconds() * 1000.0 << "ms" << std::endl; // TODO optimize
    }

    Mesh mesh{ };

    {
        utils::stopWatch timer{ };
        mesh = m_Models[pageNumber][modelIndex].file->GetMesh();

        std::cout << "Get Mesh: " << timer.seconds() * 1000.0 << "ms" << std::endl; // TODO optimize
    }

    std::vector<float> vertices = mesh.vertices;
    std::vector<unsigned int> indices = mesh.indices;
    
    if (vertices.empty() || indices.empty()) {
        QImage image{ imageWidth, imageHeight, QImage::Format_ARGB32 };

        image.fill(Qt::black);

        return image;
    }

    VkSubresourceLayout imageSubresourceLayout;

    // Model
    glm::mat4 model = glm::mat4{ 1.0f };

    // Projection
    glm::vec2 canvasSize = {
        (m_Models[pageNumber][modelIndex].maxBound.x - m_Models[pageNumber][modelIndex].minBound.x) * m_CachedRequestSizes[pageNumber].size.x,
        (m_Models[pageNumber][modelIndex].maxBound.y - m_Models[pageNumber][modelIndex].minBound.y) * m_CachedRequestSizes[pageNumber].size.y,
    };

    m_Models[pageNumber][modelIndex].setProjection(canvasSize);

	glm::mat4 mvp = m_Models[pageNumber][modelIndex].projectionMatrix * m_Models[pageNumber][modelIndex].viewMatrix * model;

    unsigned char* imageData = nullptr;
    {
        utils::stopWatch timer{ };
        imageData = m_HeadlessRenderer->render(imageWidth, imageHeight, &imageSubresourceLayout, vertices, indices, mvp);

        std::cout << "Render: " << timer.seconds() * 1000.0 << "ms" << std::endl; // TODO optimize
    }

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

    delete imageData;

    QImage image{ vectorData.data(), imageWidth, imageHeight, QImage::Format_ARGB32 };

    image = image.mirrored(false, true);

    m_Models[pageNumber][modelIndex].m_HasChanged = false;
    m_ModelImages[pageNumber][modelIndex] = image;

    return image;
}

V3dModel& V3dModelManager::Model(size_t pageNumber, size_t modelIndex) {
    return m_Models[pageNumber][modelIndex];
}

std::vector<V3dModel>& V3dModelManager::Models(size_t pageNumber) {
    return m_Models[pageNumber];
}

bool V3dModelManager::Empty() {
    return m_Models.empty();
}

glm::vec2 V3dModelManager::GetCanvasSize(size_t pageNumber) {
    return glm::vec2{ m_Models[pageNumber][0].file->headerInfo.canvasWidth, m_Models[pageNumber][0].file->headerInfo.canvasHeight };
}

void V3dModelManager::SetDocument(const Okular::Document* document) {
    m_Document = document;
}

bool V3dModelManager::mouseMoveEvent(QMouseEvent* event) {
    if (m_Models.size() == 0) {
        // If the document has no models, this is just a plain PDF document, no need for any special interaction
        return false;
    }

    // Always keep an updated record of the mouse position even if the mouse is not down
    m_MousePosition.x = (int)event->position().x();
    m_MousePosition.y = (int)event->position().y();

    if (!m_Dragging) { m_LastMousePosition = m_MousePosition; return false; }

    glm::vec2 normalizedMousePositionOnPage = GetNormalizedPositionRelativeToPage(m_MousePosition, m_ActiveModelPage);
    glm::vec2 lastNormalizedMousePositionOnPage = GetNormalizedPositionRelativeToPage(m_LastMousePosition, m_ActiveModelPage);

    V3dModel& model = *m_ActiveModel;

#ifdef MOUSE_BOUNDARIES
    if (m_ActiveModelPage >= 0) {

        float dpr = GetDevicePixelRatio();

        int pg = GetPageMouseIsOver();

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

        glm::vec2 canvasSize = {
            (model.maxBound.x - model.minBound.x) * (m_CachedRequestSizes[m_ActiveModelPage].size.x),
            (model.maxBound.y - model.minBound.y) * (m_CachedRequestSizes[m_ActiveModelPage].size.y),
        };

        model.dragModeShift(normalizedPositionOnModel, lastNormalizedPositionOnModel, canvasSize);
    } else if (!controlKey && shiftKey && !altKey) {
        model.dragModeZoom(normalizedPositionOnModel, lastNormalizedPositionOnModel, pageViewSize);
    } else if (!controlKey && !shiftKey && altKey) {
        model.dragModePan(normalizedPositionOnModel, lastNormalizedPositionOnModel, pageViewSize);
    } else {
        model.dragModeRotate(normalizedPositionOnModel, lastNormalizedPositionOnModel, pageViewSize);
    }

    requestPixmapRefresh(m_ActiveModelPage);

    m_LastMousePosition = m_MousePosition;

    return true;
}

bool V3dModelManager::mouseButtonPressEvent(QMouseEvent* event) {
    if (m_Models.size() == 0) {
        // If the document has no models, this is just a plain PDF document, no need for any special interaction
        return false;
    }

    // Always keep an updated record of the mouse position even if the click is not the left button
    m_MousePosition.x = (int)event->position().x();
    m_MousePosition.y = (int)event->position().y();

    // m_LastMousePosition = m_MousePosition;

    if (!(event->button() & Qt::MouseButton::LeftButton)) {
        return false;
    }

    int pageMouseIsOver = GetPageMouseIsOver();

    if (pageMouseIsOver == -1) return false;

    glm::vec2 normalizedMousePositionOnPage = GetNormalizedPositionRelativeToPage(m_MousePosition, pageMouseIsOver);

    V3dModel* modelMouseIsOver = nullptr;
    for (auto& model : m_Models[pageMouseIsOver]) {
        bool horizontallyOnModel = normalizedMousePositionOnPage.x > model.minBound.x && normalizedMousePositionOnPage.x < model.maxBound.x;
        bool verticallyOnModel = normalizedMousePositionOnPage.y > model.minBound.y && normalizedMousePositionOnPage.y < model.maxBound.y;

        if (!(horizontallyOnModel && verticallyOnModel)) continue; // Making the assumption no two models overlap

        modelMouseIsOver = &model;
        break;
    }

    if (modelMouseIsOver != nullptr) {
        m_Dragging = true;
        m_ActiveModel = modelMouseIsOver;
        m_ActiveModelPage = pageMouseIsOver;

        return true;
    }

    return false;
}

bool V3dModelManager::mouseButtonReleaseEvent(QMouseEvent* event) {
    if (m_Models.size() == 0) {
        // If the document has no models, this is just a plain PDF document, no need for any special interaction
        return false;
    }

    if (!(event->button() & Qt::MouseButton::LeftButton)) {
        return false;
    }

    m_Dragging = false;

    return false;
}

bool V3dModelManager::wheelEvent(QWheelEvent* event) {
    if (m_Models.size() == 0) {
        // If the document has no models, this is just a plain PDF document, no need for any special interaction
        return false;
    }

    if (m_ActiveModel == nullptr && m_ActiveModelPage == -1) {
        return false;
    }

    glm::vec2 normalizedPositionOnPage = GetNormalizedPositionRelativeToPage(m_MousePosition, m_ActiveModelPage);

    bool horizontallyOnModel = normalizedPositionOnPage.x > m_ActiveModel->minBound.x && normalizedPositionOnPage.x < m_ActiveModel->maxBound.x;
    bool verticallyOnModel = normalizedPositionOnPage.y > m_ActiveModel->minBound.y && normalizedPositionOnPage.y < m_ActiveModel->maxBound.y;

    if (!horizontallyOnModel || !verticallyOnModel) {
        return false;
    }

    if (event->angleDelta().y() < 0) {
        m_ActiveModel->zoom /= m_ActiveModel->file->headerInfo.zoomFactor;
    } else {
        m_ActiveModel->zoom *= m_ActiveModel->file->headerInfo.zoomFactor;
    }

    float maxZoom = std::sqrt(std::numeric_limits<float>::max());
    float minZoom = 1 / maxZoom;

    if (m_ActiveModel->zoom < minZoom) {
        m_ActiveModel->zoom = minZoom;
    } else if (m_ActiveModel->zoom > maxZoom) {
        m_ActiveModel->zoom = maxZoom;
    }

    float dpr = GetDevicePixelRatio();

    glm::vec2 canvasSize = {
        (m_ActiveModel->maxBound.x - m_ActiveModel->minBound.x) * (m_CachedRequestSizes[m_ActiveModelPage].size.x / dpr),
        (m_ActiveModel->maxBound.y - m_ActiveModel->minBound.y) * (m_CachedRequestSizes[m_ActiveModelPage].size.y / dpr),
    };

    m_ActiveModel->setProjection(canvasSize);
    requestPixmapRefresh(m_ActiveModelPage);

    return true;
}

bool V3dModelManager::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_T) {
        std::cout << "TEST" << std::endl;
    }

    return true;
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

void V3dModelManager::CacheRequestSize(size_t pageNumber, int width, int height, int priority) {
    if (m_Pages.size() < pageNumber + 1) {
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
        glm::vec2 firstPageSize{ m_CachedRequestSizes[firstPage->pageNumber].size.x / dpr, m_CachedRequestSizes[firstPage->pageNumber].size.y / dpr };

        pageBorders[0].pageNumber = firstPage->pageNumber;

        if (firstPageRect.top == 0.0f) {
            // Vertical margin calculation

            float totalPageHeight = 0;
            for (int i = visiblePages[0]->pageNumber; i <= visiblePages[visiblePages.size() - 1]->pageNumber; ++i) {
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
    glm::vec2 pageSize{ m_CachedRequestSizes[pageNumber].size.x / dpr, m_CachedRequestSizes[pageNumber].size.y / dpr };

    auto it = std::find_if(pageBorders.begin(), pageBorders.end(), [&pageNumber](PageBorders border){
        return border.pageNumber == pageNumber;
    });

    PageBorders& border = *it;

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
    m_Pages[pageNumber]->deletePixmaps();

    QKeyEvent* keyEvent = new QKeyEvent(
        QEvent::KeyRelease, // type
        Qt::Key_Control,    // key
        Qt::NoModifier      // modifiers
    );

    ProtectedFunctionCaller::callKeyReleaseEvent(m_PageView, keyEvent);
}
