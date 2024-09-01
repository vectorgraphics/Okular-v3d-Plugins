#include "V3dModelManager.h"

#include <iostream>

#include <QApplication>
#include <QBoxLayout>
#include <QScrollBar>
#include <QPainter>

#include <area.h>
#include <document.h>
#include <generator.h>
#include <part/pageview.h>

#include "Utility/EventFilter.h"
#include "Utility/ProtectedFunctionCaller.h"

V3dModelManager::V3dModelManager(const Okular::Document* document, const std::string& shaderPath) 
    : m_Document(document)
    , m_HeadlessRenderer(std::make_unique<HeadlessRenderer>(shaderPath)) {

    m_PageView = GetPageViewWidget();

    m_EventFilter = new EventFilter(m_PageView, this);
    m_PageView->viewport()->installEventFilter(m_EventFilter);
}

void V3dModelManager::AddModel(V3dModel model, size_t pageNumber) {
    m_Models.resize(pageNumber + 1);
    m_Models[pageNumber].emplace_back(std::move(model));

    m_Models[pageNumber].back().initProjection();

    m_ModelImages.resize(pageNumber + 1);
    m_ModelImages[pageNumber].push_back(QImage{ });
}

QImage V3dModelManager::RenderModel(size_t pageNumber, size_t modelIndex, int width, int height) {
    if (!m_Models[pageNumber][modelIndex].m_HasChanged && 
        m_ModelImages[pageNumber][modelIndex].width() == width && 
        m_ModelImages[pageNumber][modelIndex].height() == height) {
        return m_ModelImages[pageNumber][modelIndex];
    }

    std::vector<float> vertices = m_Models[pageNumber][modelIndex].file->vertices;
    std::vector<unsigned int> indices = m_Models[pageNumber][modelIndex].file->indices;

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

    unsigned char* imageData = m_HeadlessRenderer->render(width, height, &imageSubresourceLayout, vertices, indices, mvp);

    unsigned char* imgDataTmp = imageData;

    size_t finalImageSize = width * height * 4;

    std::vector<unsigned char> vectorData;
    vectorData.reserve(finalImageSize);

    for (int32_t y = 0; y < height; y++) {
        unsigned int *row = (unsigned int*)imgDataTmp;
        size_t rowBytes = width * 4;
    
        vectorData.resize(vectorData.size() + rowBytes);
        std::memcpy(vectorData.data() + vectorData.size() - rowBytes, (unsigned char*)row, rowBytes);

        imgDataTmp += imageSubresourceLayout.rowPitch;
    }

    delete imageData;

    QImage image{ vectorData.data(), width, height, QImage::Format_ARGB32 };

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
    if (m_Models.size() == 0) { // No models
        return false;
    }

    m_MousePosition.x = event->x();
    m_MousePosition.y = event->y();

    if (m_MouseDown == false) {
        return true;
    }

    if (m_ActiveModel == nullptr) {
        return true;
    }

    NormalizedMousePosition normalizedMousePos = GetNormalizedMousePosition(m_ActiveModelInfo.x);
    
    m_LastMousePosition.x = m_MousePosition.x;
    m_LastMousePosition.y = m_MousePosition.y;

    glm::vec2 normalizedPositionOnPage = normalizedMousePos.currentPosition;
    glm::vec2 lastNormalizedPositionOnPage = normalizedMousePos.lastPosition;

    V3dModel& model = *m_ActiveModel;

#ifdef MOUSE_BOUNDARIES
    int leftPixel = model.minBound.x * m_CachedRequestSizes[normalizedMousePos.pageNumber].size.x;
    int rightPixel = leftPixel + (model.maxBound.x - model.minBound.x) * m_CachedRequestSizes[normalizedMousePos.pageNumber].size.x;

    int topPixel = model.minBound.y * m_CachedRequestSizes[normalizedMousePos.pageNumber].size.y;
    int bottomPixel = topPixel + (model.maxBound.y - model.minBound.y) * m_CachedRequestSizes[normalizedMousePos.pageNumber].size.y;

    if (m_ActiveModelInfo.x >= 0) {
        m_MouseBoundaryLines[m_ActiveModelInfo.x].push_back(Line{ glm::vec2{ leftPixel, topPixel }, glm::vec2{ rightPixel, topPixel } });
        m_MouseBoundaryLines[m_ActiveModelInfo.x].push_back(Line{ glm::vec2{ leftPixel, topPixel }, glm::vec2{ leftPixel, bottomPixel } });
        m_MouseBoundaryLines[m_ActiveModelInfo.x].push_back(Line{ glm::vec2{ rightPixel, topPixel }, glm::vec2{ rightPixel, bottomPixel } });
        m_MouseBoundaryLines[m_ActiveModelInfo.x].push_back(Line{ glm::vec2{ leftPixel, bottomPixel }, glm::vec2{ rightPixel, bottomPixel } });
    }
#endif

    glm::vec2 normalizedPositionOnModel = {
        (normalizedPositionOnPage.x - model.minBound.x) / (model.maxBound.x - model.minBound.x),
        (normalizedPositionOnPage.y - model.minBound.y) / (model.maxBound.y - model.minBound.y)
    };

    glm::vec2 lastNormalizedPositionOnModel = {
        (lastNormalizedPositionOnPage.x - model.minBound.x) / (model.maxBound.x - model.minBound.x),
        (lastNormalizedPositionOnPage.y - model.minBound.y) / (model.maxBound.y - model.minBound.y)
    };

    glm::vec2 pageViewSize = { m_PageView->width(), m_PageView->height() };

    switch (m_DragMode) {
        case DragMode::SHIFT: {
            model.dragModeShift(normalizedPositionOnModel, lastNormalizedPositionOnModel, pageViewSize);
            break;
        }
        case DragMode::ZOOM: {
            model.dragModeZoom(normalizedPositionOnModel, lastNormalizedPositionOnModel, pageViewSize);
            break;
        }
        case DragMode::PAN: {
            model.dragModePan(normalizedPositionOnModel, lastNormalizedPositionOnModel, pageViewSize);
            break;
        }
        case DragMode::ROTATE: {
            model.dragModeRotate(normalizedPositionOnModel, lastNormalizedPositionOnModel, pageViewSize);
            break;
        }
    }
    
    requestPixmapRefresh(m_ActiveModelInfo.x);

    return true;
}

bool V3dModelManager::mouseButtonPressEvent(QMouseEvent* event) {
    if (m_Models.size() == 0) { // No models
        return false;
    }

    if (!(event->button() & Qt::MouseButton::LeftButton)) {
        return false;
    }

    if (m_MouseDown != false) {
       return true;
    }

    NormalizedMousePosition normalizedMousePos = GetNormalizedMousePosition();

    glm::vec2 normalizedPositionOnPage = normalizedMousePos.currentPosition;
    glm::vec2 lastNormalizedPositionOnPage = normalizedMousePos.lastPosition;

    m_ActiveModel = nullptr;
    m_ActiveModelInfo = glm::ivec2{ -1, -1 };

    // If we are not over the page, than we are not over any models on that page, and so we should not have an activeModel
    if (normalizedMousePos.pageNumber != -1) {
        int modelIndex = 0;
        for (auto& model : m_Models[normalizedMousePos.pageNumber]) {
            bool horizontallyOnModel = normalizedPositionOnPage.x > model.minBound.x && normalizedPositionOnPage.x < model.maxBound.x;
            bool verticallyOnModel = normalizedPositionOnPage.y > model.minBound.y && normalizedPositionOnPage.y < model.maxBound.y;

            if (!(horizontallyOnModel && verticallyOnModel)) {
                continue;
            }

            m_ActiveModel = &model;
            m_ActiveModelInfo = glm::ivec2{ normalizedMousePos.pageNumber, modelIndex };
            break;

            ++modelIndex;
        }
    }

    m_LastMousePosition.x = m_MousePosition.x;
    m_LastMousePosition.y = m_MousePosition.y;

    m_MouseDown = true;

    bool controlKey = event->modifiers() & Qt::ControlModifier;
    bool shiftKey = event->modifiers() & Qt::ShiftModifier;
    bool altKey = event->modifiers() & Qt::AltModifier;

    if (controlKey && !shiftKey && !altKey) {
        m_DragMode = DragMode::SHIFT;
    } else if (!controlKey && shiftKey && !altKey) {
        m_DragMode = DragMode::ZOOM;
    } else if (!controlKey && !shiftKey && altKey) {
        m_DragMode = DragMode::PAN;
    } else {
        m_DragMode = DragMode::ROTATE;
    }

    return true;
}

bool V3dModelManager::mouseButtonReleaseEvent(QMouseEvent* event) {
    if (!(event->button() & Qt::MouseButton::LeftButton)) {
        return false;
    }

    if (m_MouseDown != true) {
        return true;
    }

    m_MouseDown = false;

    return true;
}

bool V3dModelManager::wheelEvent(QWheelEvent* event) {
    if (m_ActiveModel != nullptr && m_ActiveModelInfo.x != -1 && m_ActiveModelInfo.y != -1) {
        NormalizedMousePosition normalizedMousePos = GetNormalizedMousePosition(m_ActiveModelInfo.x);
        glm::vec2 normalizedPositionOnPage = normalizedMousePos.currentPosition;

        bool horizontallyOnModel = normalizedPositionOnPage.x > m_ActiveModel->minBound.x && normalizedPositionOnPage.x < m_ActiveModel->maxBound.x;
        bool verticallyOnModel = normalizedPositionOnPage.y > m_ActiveModel->minBound.y && normalizedPositionOnPage.y < m_ActiveModel->maxBound.y;

        if (horizontallyOnModel && verticallyOnModel) {
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

            glm::vec2 canvasSize = {
                (m_ActiveModel->maxBound.x - m_ActiveModel->minBound.x) * m_CachedRequestSizes[m_ActiveModelInfo.x].size.x,
                (m_ActiveModel->maxBound.y - m_ActiveModel->minBound.y) * m_CachedRequestSizes[m_ActiveModelInfo.x].size.y,
            };

            m_ActiveModel->setProjection(canvasSize);
            requestPixmapRefresh(m_ActiveModelInfo.x);

            return true;
        }
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

    CacheRequestSize(page->number(), request->width(), request->height(), request->priority());
    CachePage(page->number(), page);

#ifdef MOUSE_BOUNDARIES
    if (m_MouseBoundaryLines.size() < page->number() + 1) {
        m_MouseBoundaryLines.resize(page->number() + 1);
        m_MouseBoundaryPoints.resize(page->number() + 1);
    }
#endif MOUSE_BOUNDARIES
}

void V3dModelManager::CacheRequestSize(size_t pageNumber, int width, int height, int priority) {
    if (m_Pages.size() < pageNumber + 1) {
        m_CachedRequestSizes.resize(pageNumber + 1);
    }

    if (priority <= m_CachedRequestSizes[pageNumber].priority) {
        m_CachedRequestSizes[pageNumber] = RequestCache{ glm::ivec2{ width, height }, priority };
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

V3dModelManager::NormalizedMousePosition V3dModelManager::GetNormalizedMousePosition(int pageReference) {
    auto visiblePages = m_Document->visiblePageRects();
    NormalizedMousePosition normalizedMousePosition{ };

    normalizedMousePosition.currentPosition = glm::vec2{ };
    normalizedMousePosition.lastPosition = glm::vec2{ };
    normalizedMousePosition.pageNumber = -1;

    if (m_Models.size() == 1) {
        // The document has only one page
        int pageNumber = 0;

        if (visiblePages.size() != 1) {
            return normalizedMousePosition;
        }

        Okular::NormalizedRect rect = visiblePages[0]->rect;
        if (rect.left == 0.0 && rect.right == 1.0 && rect.top == 0.0 && rect.bottom == 1.0) {
            // The full page is visible
            int horizontalMargin = (m_PageView->width() - m_CachedRequestSizes[pageNumber].size.x) / 2;
            int verticalMargin = (m_PageView->height() - m_CachedRequestSizes[pageNumber].size.y) / 2;

            bool horizontallyOnPage = m_MousePosition.x > horizontalMargin && m_MousePosition.x < horizontalMargin + m_CachedRequestSizes[pageNumber].size.x;
            bool verticallyOnPage = m_MousePosition.y > verticalMargin && m_MousePosition.y < verticalMargin + m_CachedRequestSizes[pageNumber].size.y;

            if (horizontallyOnPage && verticallyOnPage) {
                normalizedMousePosition.pageNumber = 0;
            } else {
                normalizedMousePosition.pageNumber = -1;
            }

            normalizedMousePosition.currentPosition = {
                (float)(m_MousePosition.x - horizontalMargin) / (float)m_CachedRequestSizes[pageNumber].size.x,
                (float)(m_MousePosition.y - verticalMargin) / (float)m_CachedRequestSizes[pageNumber].size.y
            };

            normalizedMousePosition.lastPosition = {
                (float)(m_LastMousePosition.x - horizontalMargin) / (float)m_CachedRequestSizes[pageNumber].size.x,
                (float)(m_LastMousePosition.y - verticalMargin) / (float)m_CachedRequestSizes[pageNumber].size.y
            };

        } else {
            // Only a section of the page is visible

            float leftPixel = 0.0;
            if (rect.left == 0.0 && rect.right == 1.0) {
                // Page is fully visible horizontally
                int horizontalMargin = (m_PageView->width() - m_CachedRequestSizes[pageNumber].size.x) / 2;
                leftPixel = horizontalMargin;
            } else {
                leftPixel = -(float)rect.left * (float)m_CachedRequestSizes[pageNumber].size.x;
            }

            float topPixel = -(float)rect.top * (float)m_CachedRequestSizes[pageNumber].size.y;

            normalizedMousePosition.pageNumber = 0;

            normalizedMousePosition.currentPosition = {
                (float)(m_MousePosition.x - leftPixel) / (float)m_CachedRequestSizes[pageNumber].size.x,
                (float)(m_MousePosition.y - topPixel) / (float)m_CachedRequestSizes[pageNumber].size.y
            };

            normalizedMousePosition.lastPosition = {
                (float)(m_LastMousePosition.x - leftPixel) / (float)m_CachedRequestSizes[pageNumber].size.x,
                (float)(m_LastMousePosition.y - topPixel) / (float)m_CachedRequestSizes[pageNumber].size.y
            };
        }

    } else {     
        // TODO these constexpr values can be found with the following:
        // std::cout << "All pages are fully visible" << std::endl;
        // std::cout << "CONTENT AREA HEIGHT: " << m_PageView->verticalScrollBar()->maximum() + m_PageView->viewport()->height() << std::endl;
        // std::cout << "Total page height: " << m_CachedRequestSizes[visiblePages[0]->pageNumber].size.y * m_Document->pages() << std::endl;
        // std::cout << "Difference: " << (m_PageView->verticalScrollBar()->maximum() + m_PageView->viewport()->height()) - m_CachedRequestSizes[visiblePages[0]->pageNumber].size.y * m_Document->pages() << std::endl;
        constexpr int verticalPageMargin = 12;
        constexpr int verticalBorderHeight = 6;
        constexpr int horizontalBorderWidth = 3;

        int contentAreaHeight = m_PageView->verticalScrollBar()->maximum() + m_PageView->viewport()->height();
        int totalPageHeight = m_CachedRequestSizes[visiblePages[0]->pageNumber].size.y * m_Document->pages();
        int yDifference = contentAreaHeight - totalPageHeight;

        int contentAreaWidth = m_PageView->horizontalScrollBar()->maximum() + m_PageView->viewport()->width();
        int totalPageWidth = m_CachedRequestSizes[visiblePages[0]->pageNumber].size.x;
        int xDifference = contentAreaWidth - totalPageWidth;

        // One margin between each page, and a half margin on top of the top page, and another on the bottom of the last page
        int verticalMargin = (yDifference - (visiblePages.size() * verticalPageMargin)) / 2;
        int horizontalMargin = xDifference / 2;

        // The document has more than one page   
        bool oneNotFullyVisible = false;

        for (auto visiblePage : visiblePages) {
            int pageNumber = visiblePage->pageNumber;

            Okular::NormalizedRect rect = visiblePage->rect;
            if (!(rect.left == 0.0 && rect.right == 1.0 && rect.top == 0.0 && rect.bottom == 1.0)) {
                oneNotFullyVisible = true;
                break;
            }
        }

        if (oneNotFullyVisible) {
            // One page minimum is not fully visible

            if (visiblePages.size() == 1) {
                // Only one partially visible page, the page will either take up the entire viewport, or well be able to
                // see of the edges on the left and right, in that case it will be centred horizontally.

                int pageMouseIsOver = -1;
                if (pageReference == -1) {
                    // Find what page were over top of
                    bool horizontallyOnPage = false;

                    Okular::NormalizedRect rect = visiblePages[0]->rect;
                    if (rect.left == 0.0 && rect.right == 1.0) {
                        // The page is horizontally centred, and there exists a horizontal margin on either side.
                        horizontallyOnPage = m_MousePosition.x > horizontalMargin && 
                            m_MousePosition.x < horizontalMargin + m_CachedRequestSizes[visiblePages[0]->pageNumber].size.x;

                    } else {
                        // There is no margin on either side, the page takes up the whole viewport
                        horizontallyOnPage = true;
                    }

                    // If there is only one visible page, but the full document has more than one page, than we must
                    // either be on the page vertically or on one of the pages above or below, which is not possible because
                    // only one page is visible.
                    bool verticallyOnPage = true;

                    if (horizontallyOnPage && verticallyOnPage) {
                        pageMouseIsOver = visiblePages[0]->pageNumber;
                    }

                } else {
                    pageMouseIsOver = pageReference;
                }

                normalizedMousePosition.pageNumber = pageMouseIsOver;

                if (pageMouseIsOver != -1) {
                    // Calculate normalized coords on page

                    Okular::NormalizedRect rect = visiblePages[0]->rect;

                    int leftOfPage = 0;
                    int topOfPage = -(rect.top * m_CachedRequestSizes[normalizedMousePosition.pageNumber].size.y);

                    if (rect.left == 0.0 && rect.right == 1.0) {
                        // The page is horizontally centred, and there exists a horizontal margin on either side.
                        leftOfPage = horizontalMargin;
                    } else {
                        // There is no margin on either side, the page takes up the whole viewport
                        leftOfPage = -(rect.left * m_CachedRequestSizes[normalizedMousePosition.pageNumber].size.x);
                    }

                    normalizedMousePosition.currentPosition = {
                        (float)(m_MousePosition.x - leftOfPage) / (float)m_CachedRequestSizes[normalizedMousePosition.pageNumber].size.x,
                        (float)(m_MousePosition.y - topOfPage) / (float)m_CachedRequestSizes[normalizedMousePosition.pageNumber].size.y
                    };

                    normalizedMousePosition.lastPosition = {
                        (float)(m_LastMousePosition.x - leftOfPage) / (float)m_CachedRequestSizes[normalizedMousePosition.pageNumber].size.x,
                        (float)(m_LastMousePosition.y - topOfPage) / (float)m_CachedRequestSizes[normalizedMousePosition.pageNumber].size.y
                    };
                }
            } else {
                // There are multiple pages with varying visiblity
                int pageMouseIsOver = -1;
                if (pageReference == -1) {
                    // Find what page the mouse is over right now
                    int j = 0;
                    int totalPageHeightSoFar = 0;
                    for (auto page : visiblePages) {
                        bool horizontallyOnPage = false;
                        bool verticallyOnPage = false;

                        Okular::NormalizedRect rect = page->rect;
                        if (rect.left == 0.0 && rect.right == 1.0) {
                            // The page is horizontally centred, and there exists a horizontal margin on either side.

                            horizontallyOnPage = m_MousePosition.x > horizontalMargin && 
                                m_MousePosition.x < horizontalMargin + m_CachedRequestSizes[page->pageNumber].size.x;

                        } else {
                            // There is no margin on either side, the page takes up the whole viewport
                            horizontallyOnPage = true;
                        }

                        int topOfPage = totalPageHeightSoFar;
                        int bottomOfPage = totalPageHeightSoFar + (m_CachedRequestSizes[page->pageNumber].size.y * (rect.bottom - rect.top));

                        verticallyOnPage = m_MousePosition.y > topOfPage && m_MousePosition.y < bottomOfPage;

                        totalPageHeightSoFar = bottomOfPage + verticalPageMargin;

                        if (horizontallyOnPage && verticallyOnPage) {
                            pageMouseIsOver = visiblePages[j]->pageNumber;
                            break;
                        }

                        ++j;
                    }

                } else {
                    // Use the given pageReference as the page the mouse is over
                    pageMouseIsOver = pageReference;
                }

                normalizedMousePosition.pageNumber = pageMouseIsOver;

                int topOfPage = 0;
                int leftOfPage = 0;
                if (pageMouseIsOver != -1) {
                    int j = 0;
                    int totalPageHeightSoFar = 0;

                    bool isFullyVisible = false;

                    int k = 0;
                    for (auto page : visiblePages) {
                        Okular::NormalizedRect rect = page->rect;

                        if (rect.left == 0.0 && rect.right == 1.0) {
                            // The page is horizontally centred, and there exists a horizontal margin on either side.
                            leftOfPage = horizontalMargin;

                        } else {
                            // There is no margin on either side, the page takes up the whole viewport
                            leftOfPage = -(rect.left * m_CachedRequestSizes[normalizedMousePosition.pageNumber].size.x);
                        }

                        if (rect.left == 0.0 && rect.top == 0.0 && rect.right == 1.0 && rect.bottom == 1.0) {
                            isFullyVisible = true;
                        }

                        if (pageMouseIsOver == page->pageNumber) {
                            if (k == 0 && !isFullyVisible) {
                                topOfPage = -m_CachedRequestSizes[page->pageNumber].size.y * rect.top;
                            }

                            break;
                        }

                        totalPageHeightSoFar += m_CachedRequestSizes[page->pageNumber].size.y * (rect.bottom - rect.top);
                        totalPageHeightSoFar += 12;
                        topOfPage = totalPageHeightSoFar;

                        ++k;
                    }

                    normalizedMousePosition.currentPosition = {
                        (float)(m_MousePosition.x - leftOfPage) / (float)m_CachedRequestSizes[normalizedMousePosition.pageNumber].size.x,
                        (float)(m_MousePosition.y - topOfPage) / (float)m_CachedRequestSizes[normalizedMousePosition.pageNumber].size.y
                    };

                    normalizedMousePosition.lastPosition = {
                        (float)(m_LastMousePosition.x - leftOfPage) / (float)m_CachedRequestSizes[normalizedMousePosition.pageNumber].size.x,
                        (float)(m_LastMousePosition.y - topOfPage) / (float)m_CachedRequestSizes[normalizedMousePosition.pageNumber].size.y
                    };
                }
            }

        } else {
            // All pages are fully visible
            // TODO assuming in the short term that all pages are the same size

            int pageCount = m_Document->pages();
            if (visiblePages.size() == pageCount) {
                // Case 1
                // The document has a low number of pages (2-4) and so literally all pages are on screen. 
                // Pages are horizontally and vertically centred as a whole.

                int pageMouseIsOver = -1;
                if (pageReference == -1) {
                    // Find what page the mouse is over right now

                    bool horizontallyOnPage = m_MousePosition.x > horizontalMargin &&
                        m_MousePosition.x < contentAreaWidth - horizontalMargin;

                    int j = 0;
                    for (auto page : visiblePages) {
                        int totalMarginSize = j * verticalPageMargin;
                        int totalPreviousPageHeight = j * m_CachedRequestSizes[page->pageNumber].size.y;

                        int topOfPage = verticalMargin + verticalBorderHeight + totalMarginSize + totalPreviousPageHeight;
                        int bottomOfPage = topOfPage + m_CachedRequestSizes[page->pageNumber].size.y;

                        if (m_MousePosition.y > topOfPage && m_MousePosition.y < bottomOfPage) {
                            pageMouseIsOver = j;
                            break;
                        }
                        ++j;
                    }

                    if (!horizontallyOnPage || pageMouseIsOver == -1) {
                        pageMouseIsOver = -1;
                    }

                } else {
                    // Use the given pageReference as the page the mouse is over
                    pageMouseIsOver = pageReference;
                }

                normalizedMousePosition.pageNumber = pageMouseIsOver;
                
                if (pageMouseIsOver != -1) {
                    int leftOfPage = horizontalMargin;

                    int totalMarginSize = pageMouseIsOver * verticalPageMargin;
                    int totalPreviousPageHeight = pageMouseIsOver * m_CachedRequestSizes[0].size.y; // TODO assumes all pages are the same size

                    int topOfPage = verticalMargin + verticalBorderHeight + totalMarginSize + totalPreviousPageHeight;

                    normalizedMousePosition.currentPosition = {
                        (float)(m_MousePosition.x - leftOfPage) / (float)m_CachedRequestSizes[normalizedMousePosition.pageNumber].size.x,
                        (float)(m_MousePosition.y - topOfPage) / (float)m_CachedRequestSizes[normalizedMousePosition.pageNumber].size.y
                    };

                    normalizedMousePosition.lastPosition = {
                        (float)(m_LastMousePosition.x - leftOfPage) / (float)m_CachedRequestSizes[normalizedMousePosition.pageNumber].size.x,
                        (float)(m_LastMousePosition.y - topOfPage) / (float)m_CachedRequestSizes[normalizedMousePosition.pageNumber].size.y
                    };
                }

            } else {
                // Case 2:
                // The document has many pages, and they are lined up in a way that, for example, 3 full pages are off the 
                // top of the screen, 4 full pages are visible, and then 10 pages are entirely off the page at the bottom

                // In case 2 we cant exactly know how many pixels are between the top of the top page, and the top of the viewport,
                // but if we assume its 6 pixels, well be off by at most 6 pixels, and considering this case is already very rare,
                // this wont be very noticeable.

                int pageMouseIsOver = -1;
                if (pageReference == -1) {
                    // Find what page the mouse is over right now

                    bool horizontallyOnPage = m_MousePosition.x > horizontalMargin &&
                        m_MousePosition.x < contentAreaWidth - horizontalMargin;

                    int j = 0;
                    for (auto page : visiblePages) {
                        int totalMarginSize = j * verticalPageMargin;
                        int totalPreviousPageHeight = j * m_CachedRequestSizes[page->pageNumber].size.y;

                        int topOfPage = verticalBorderHeight + totalMarginSize + totalPreviousPageHeight;
                        int bottomOfPage = topOfPage + m_CachedRequestSizes[page->pageNumber].size.y;

                        if (m_MousePosition.y > topOfPage && m_MousePosition.y < bottomOfPage) {
                            pageMouseIsOver = j;
                            break;
                        }
                        ++j;
                    }

                    if (!horizontallyOnPage || pageMouseIsOver == -1) {
                        pageMouseIsOver = -1;
                    }

                } else {
                    // Use the given pageReference as the page the mouse is over
                    pageMouseIsOver = pageReference;
                }

                normalizedMousePosition.pageNumber = pageMouseIsOver;
                
                if (pageMouseIsOver != -1) {
                    int leftOfPage = horizontalMargin;

                    int totalMarginSize = pageMouseIsOver * verticalPageMargin;
                    int totalPreviousPageHeight = pageMouseIsOver * m_CachedRequestSizes[0].size.y; // TODO assumes all pages are the same size

                    int topOfPage = verticalBorderHeight + totalMarginSize + totalPreviousPageHeight;

                    normalizedMousePosition.currentPosition = {
                        (float)(m_MousePosition.x - leftOfPage) / (float)m_CachedRequestSizes[normalizedMousePosition.pageNumber].size.x,
                        (float)(m_MousePosition.y - topOfPage) / (float)m_CachedRequestSizes[normalizedMousePosition.pageNumber].size.y
                    };

                    normalizedMousePosition.lastPosition = {
                        (float)(m_LastMousePosition.x - leftOfPage) / (float)m_CachedRequestSizes[normalizedMousePosition.pageNumber].size.x,
                        (float)(m_LastMousePosition.y - topOfPage) / (float)m_CachedRequestSizes[normalizedMousePosition.pageNumber].size.y
                    };
                }
            }
        }
    }

#ifdef MOUSE_BOUNDARIES
    if (pageReference != -1) {
        m_MouseBoundaryPoints[pageReference].push_back(Point{ normalizedMousePosition.currentPosition * glm::vec2{ (float)m_CachedRequestSizes[pageReference].size.x, (float)m_CachedRequestSizes[pageReference].size.y } });
    }
#endif MOUSE_BOUNDARIES

    return normalizedMousePosition;
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

        if (parent->children().size() != 9) {
            continue;
        } 

        int QBoxLayoutCount = 0;
        for (auto child : parent->children()) {
            QBoxLayout* qBox = dynamic_cast<QBoxLayout*>(child);

            if (qBox != nullptr) {
                QBoxLayoutCount += 1;
            }
        }

        if (QBoxLayoutCount != 1) {
            continue;
        } 

        int QFrameCount = 0;
        for (auto child : parent->children()) {
            QFrame* qFrame = dynamic_cast<QFrame*>(child);

            if (qFrame != nullptr) {
                QFrameCount += 1;
            }
        }

        if (QFrameCount != 6) {
            continue;
        } 

        if (pageView != nullptr) {
            std::cout << "ERROR, multiple pageViews found" << std::endl;
        }

        pageView = dynamic_cast<QAbstractScrollArea*>(widget);
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
