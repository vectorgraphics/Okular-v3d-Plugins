#include "V3dModelManager.h"

#include <QApplication>
#include <QColorSpace>
#include <QScrollBar>
#include <QPainter>
#include <QStandardPaths>
#include <QWindow>
#include <QScreen>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <set>
#include <QTimer>
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

namespace {
    // Every v3d render -- from any document, on any thread -- clears and
    // fills the same process-global Asymptote scene state: the camp::
    // vertex buffers (materialData/colorData/lineData/transparentData/
    // pointData, defined in the vendored Asymptote sources), camp::materials,
    // camp::pixelsPerBp, camp::projViewMat, camp::normMat, and the
    // static lastPage/lastModel locals in RenderModel. The per-manager
    // m_ModelsMutex only serializes renders of ONE document; with two
    // v3d-bearing documents open (two PixmapGenerationThreads), or a
    // standalone .v3d on the main thread plus a PDF on a worker, two
    // RenderModel calls run concurrently under different managers' mutexes
    // and tear the shared buffers (clear() while another thread
    // push_back's -> realloc UAF, the "multiple documents crash").
    //
    // This single process-wide mutex serializes the global-buffer section
    // of RenderModel (buffer clear + QueueMesh + HeadlessRenderer::render,
    // which reads the same buffers) across all documents.
    std::mutex v3dRenderGlobalsMutex;
}

bool fileExists(const std::string& path) {
    std::ifstream f{ path.c_str() };
    return f.good();
}

// Old v3d files may store an absolute path in the IBL image name; reduce it
// to the file name (relative paths, e.g. "snowyField/refl0", are preserved)
// and drop trailing slashes, so the name is a relative path within the image
// library.
static std::string normalizeIBLName(const std::string& imageName)
{
    std::string name = imageName;
    if (!name.empty() && name[0] == '/') {
        size_t lastSlash = name.find_last_of('/');
        name = (lastSlash != 0) ? name.substr(lastSlash + 1) : name.substr(1);
    }
    while (!name.empty() && name.back() == '/') name.pop_back();
    return name;
}

// IBL image names come from untrusted v3d documents and must resolve within
// the image library directory -- locally and on the server. Reject '%'
// outright (ambiguous in URLs), a leading separator, '..' path components
// (either separator), and control characters (a NUL in particular would let a
// "..\0" bypass the ".." check below and be truncated to ".." at the OS
// layer); relative subdirectories are otherwise allowed. Keep in sync with
// settings::iblNameOK in the asymptote repo.
static bool iblNameOK(const std::string& name)
{
    if (name.find('%') != std::string::npos)
        return false;
    // Control characters are never valid in a file path (and a NUL would let a
    // "..\0" slip past the ".." check and truncate to ".." via the OS).
    for (unsigned char c : name) {
        if (c < 0x20 || c == 0x7f)
            return false;
    }
    // Must be a relative path within the image library: no leading separator.
    if (!name.empty() && (name[0] == '/' || name[0] == '\\'))
        return false;
    size_t start = 0;
    while (true) {
        size_t end = name.find_first_of("/\\", start);
        size_t len = (end == std::string::npos) ? std::string::npos : end - start;
        if (name.substr(start, len) == "..")
            return false;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return true;
}

// Log a rejected IBL name safely, for debugging. The name is untrusted document
// data, so non-printable bytes are escaped as \xHH -- the log must never emit
// raw control characters or terminal escape sequences taken from the file. It
// is gated behind the debug flag (LOG) so a hostile document cannot spam the
// console on the per-render path.
static void logRejectedIBLName(const std::string& name)
{
    static const char hex[] = "0123456789abcdef";
    std::string safe;
    safe.reserve(name.size());
    for (unsigned char c : name) {
        if (c >= 0x20 && c <= 0x7e) {
            safe += static_cast<char>(c);
        } else {
            safe += '\\';
            safe += 'x';
            safe += hex[(c >> 4) & 0xf];
            safe += hex[c & 0xf];
        }
    }
    LOG("[v3d] IBL image name rejected (not a safe image-library path): %s\n", safe.c_str());
}

// Custom event for marshaling IBL download prompts from background threads to main thread.
class IBLPromptEvent : public QEvent {
public:
    using Callback = std::function<bool()>;
    explicit IBLPromptEvent(Callback&& cb)
        : QEvent(QEvent::Type(QEvent::User + 1)), m_Callback(std::move(cb)) {}

    Callback m_Callback;
};

// Event filter installed on qApp to catch IBLPromptEvent and run the callback on the main thread.
//
// Lifetime anchor for the IBL prompt: this filter is a child of the manager's
// m_FilterParent and is removed from qApp + destroyed in ~V3dModelManager, so it
// can never outlive the manager it points at. The callback captured in the event
// therefore only ever runs on a live manager; the m_Manager check below makes that
// guarantee explicit and keeps it safe even if the destructor's filter removal is
// ever reordered.
class IBLEventFilter : public QObject {
public:
    // NO Q_OBJECT macro -- we only need eventFilter, no signals/slots.
    explicit IBLEventFilter(QObject* parent, V3dModelManager* manager)
        : QObject(parent), m_Manager(manager) {}

    bool eventFilter(QObject* /*object*/, QEvent* event) override {
        if (event->type() == QEvent::Type(QEvent::User + 1)) {
            IBLPromptEvent* promptEvent = static_cast<IBLPromptEvent*>(event);
            if (m_Manager != nullptr && !m_Manager->isDestroying()) {
                promptEvent->m_Callback();
            }
            // Do NOT delete event here -- Qt owns it and will delete it after
            // the filter chain returns. Deleting it ourselves causes use-after-free
            // when sendPostedEvents tries to clean up its smart pointer to the event.
            return true;
        }
        return false;
    }

private:
    V3dModelManager* m_Manager;
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
    // The filter holds a back-pointer to the manager; it is a child of
    // m_FilterParent and destroyed with the manager, so that back-pointer is
    // always valid for the filter's lifetime.
    m_IBLEventFilter = new IBLEventFilter(&m_FilterParent, this);
    qApp->installEventFilter(m_IBLEventFilter);
}

V3dModelManager::~V3dModelManager() {
    // Set before anything below can be observed: the deferred-refresh timers and
    // the IBL prompt event read this to refuse to touch the manager once it is gone.
    m_Destroying = true;

    // The deferred-refresh QTimers are children of m_FilterParent, so they are
    // destroyed together with the manager (member destruction, below) and can never
    // fire on freed state; they also delete themselves on fire, so nothing leaks.
    // Null out back-references first so in-flight events are safe.
    if (m_EventFilter) {
        m_EventFilter->modelManager = nullptr;
    }
    if (m_PresentationEventFilter) {
        m_PresentationEventFilter->modelManager = nullptr;
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
    if (m_PresentationWidget && m_PresentationEventFilter) {
        m_PresentationWidget->removeEventFilter(m_PresentationEventFilter);
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
        LOG("[v3d] RenderModel: EXIT - no HeadlessRenderer (shaders missing)\n");
        QImage image{ imageWidth, imageHeight, QImage::Format_ARGB32 };
        image.fill(Qt::black);
        return image;
    }

    // Guard against zero/negative dimensions (tiny models, small requests) which
    // produce NaN projection matrices or invalid Vulkan extents.
    if (imageWidth <= 0 || imageHeight <= 0) {
        LOG("[v3d] RenderModel: EXIT - zero/negative dimensions\n");
        QImage image{ std::max(imageWidth, 1), std::max(imageHeight, 1), QImage::Format_ARGB32 };
        image.fill(Qt::black);
        return image;
    }

    // Check IBL availability early. If the model needs IBL but the files
    // aren't cached yet, render the no-IBL fallback so the scene is visible
    // immediately (and shows what a "No" would look like while the download
    // prompt is pending); the model is tracked and re-rendered with IBL once
    // the files arrive.
    std::string imageName = m_Models[pageNumber][modelIndex].file->headerInfo.imageName;
    bool useIBL = false;
    std::string iblPath = "";
    if (!imageName.empty() && !m_IBLDeclined) {
        // A name that could escape the image library can never resolve and
        // must not prompt or download: render the scene without IBL instead.
        if (!iblNameOK(normalizeIBLName(imageName))) {
            logRejectedIBLName(imageName);
        } else {
            iblPath = resolveIBLPath(imageName);
            if (iblPath.empty()) {
                // IBL missing locally -- track this model so it is re-rendered
                // once the files arrive.
                m_ModelsWithMissingIBL.insert({pageNumber, modelIndex});

                // Marshal the download prompt to the main thread via custom event.
                // Post-and-forget: we can't wait synchronously because Okular's
                // background worker thread doesn't process events during PDF loading.
                // The popup will appear once Okular returns to its main event loop.
                // Debounced per image, so this fires at most once per image name.
                std::string base = getIBLBase();
                scheduleIBLPrompt(imageName, base);
            } else {
                useIBL = true;
            }
        }
    }

    if (!m_Models[pageNumber][modelIndex].m_HasChanged &&
        m_ModelImages[pageNumber][modelIndex].width() == imageWidth &&
        m_ModelImages[pageNumber][modelIndex].height() == imageHeight) {
        return m_ModelImages[pageNumber][modelIndex];
    }

    // Projection
    EnsureCachedRequestSize(pageNumber);
    glm::vec2 canvasSize = {
        (m_Models[pageNumber][modelIndex].maxBound.x - m_Models[pageNumber][modelIndex].minBound.x) * m_CachedRequestSizes[pageNumber].size.x,
        (m_Models[pageNumber][modelIndex].maxBound.y - m_Models[pageNumber][modelIndex].minBound.y) * m_CachedRequestSizes[pageNumber].size.y,
    };

    // Guard against zero canvas size which causes division-by-zero in setProjection.
    if (canvasSize.x <= 0.0f || canvasSize.y <= 0.0f) {
        LOG("[v3d] RenderModel: EXIT - zero canvas size (cachedRequestSize=%dx%d)\n",
            m_CachedRequestSizes.size() > pageNumber ? m_CachedRequestSizes[pageNumber].size.x : 0,
            m_CachedRequestSizes.size() > pageNumber ? m_CachedRequestSizes[pageNumber].size.y : 0);
        QImage image{ imageWidth, imageHeight, QImage::Format_ARGB32 };
        image.fill(Qt::black);
        return image;
    }

    m_Models[pageNumber][modelIndex].setProjection(canvasSize);

    // Scene bounds for QueueMesh culling (use headerInfo when frustum params invalid)
    float Zmin = m_Models[pageNumber][modelIndex].file->headerInfo.minBound.z;
    float Zmax = m_Models[pageNumber][modelIndex].file->headerInfo.maxBound.z;
    triple sceneMinBound(
        m_Models[pageNumber][modelIndex].xmin,
        m_Models[pageNumber][modelIndex].ymin,
        Zmin);
    triple sceneMaxBound(
        m_Models[pageNumber][modelIndex].xmax,
        m_Models[pageNumber][modelIndex].ymax,
        Zmax);

    if(sceneMinBound.getx() >= sceneMaxBound.getx() || sceneMinBound.gety() >= sceneMaxBound.gety() || sceneMinBound.getz() >= sceneMaxBound.getz()) {
        sceneMinBound = m_Models[pageNumber][modelIndex].file->headerInfo.minBound;
        sceneMaxBound = m_Models[pageNumber][modelIndex].file->headerInfo.maxBound;
    }

    using namespace std;
    using namespace camp;

    // Serialize the process-global Asymptote state across documents: from
    // here (first write of a camp global) through QueueMesh (fills the
    // buffers) and HeadlessRenderer::render (reads them) no other RenderModel
    // may run. The per-manager mutex is already held; this is the
    // cross-document one (see v3dRenderGlobalsMutex above). The guard
    // releases on every return path, including the !hasRenderableData early
    // out below.
    std::lock_guard<std::mutex> renderGlobalsLock{ v3dRenderGlobalsMutex };

    // Match vkrender.cc: projViewMat = projMat * viewMat
    projViewMat = m_Models[pageNumber][modelIndex].projMat * m_Models[pageNumber][modelIndex].viewMat;

    normMat = glm::dmat3{ glm::inverse(m_Models[pageNumber][modelIndex].viewMat) };

    // Match Asymptote prepareScene(): when display is triggered (m_HasChanged=true),
    // always clearData() + pic->render(). The per-object fast path (!remesh && onscreen)
    // avoids re-tessellation — algorithm §\ref{cull}.
    // When switching models (PDFs with multiple v3d images), force remesh so all
    // objects rebuild their persistent state for the new scene.
    static size_t lastPage = 0, lastModel = 0;
    bool switchedModel = (pageNumber != lastPage || modelIndex != lastModel);

    if (switchedModel) {
        m_Models[pageNumber][modelIndex].remesh = true;
        // Sync renderer draw mode to match the entering model, so the correct
        // pipelines are used. Without this, model 1 could render with model 0's
        // draw mode (e.g., WIREFRAME rendered as NORMAL).
        if (m_HeadlessRenderer)
            m_HeadlessRenderer->currentDrawMode = m_Models[pageNumber][modelIndex].drawMode;
        lastPage = pageNumber;
        lastModel = modelIndex;
    }

    // Match Asymptote updateHandler()/zoom()/fitscreen(): whenever the rendered
    // resolution changes, force a full re-tessellation (remesh=true) so every figure
    // re-queues with culling/resolution matching the new frustum. On initial load
    // Okular requests pixmaps at several sizes (background preload, then on-screen
    // display). Only the FIRST render of a model starts with remesh=true and it is
    // consumed after one render (see below). Without this, a later render at a
    // different size would take the per-object fast path (!remesh && onscreen) and
    // reuse geometry that was culled/tessellated for the old frustum -- which can
    // drop a transparent surface until the user forces a remesh (e.g. presses 'h').
    // Forcing remesh on a size change mirrors the reference, where any resize/zoom
    // sets remesh=true so all figures rebuild their persistent state.
    const QImage& cachedImage = m_ModelImages[pageNumber][modelIndex];
    if (cachedImage.width() != imageWidth || cachedImage.height() != imageHeight) {
        m_Models[pageNumber][modelIndex].remesh = true;
    }

    // clearData() — matches Asymptote prepareScene()
    materialData.clear();
    colorData.clear();
    lineData.clear();
    transparentData.clear();
    pointData.clear();

    bool orthographic = m_Models[pageNumber][modelIndex].file->headerInfo.orthographic;

    // pic->render(remesh) — matches Asymptote prepareScene() exactly. Do NOT centrally
    // reset renderCount here: the reference resets it via notRendered() inside each
    // object's queue()/clear() during QueueMesh, and the per-object fast path
    // (!remesh && Onscreen) intentionally leaves renderCount untouched so stable
    // geometry is not re-uploaded. Forcing remesh=true on a page/image switch (above)
    // makes every object re-queue -> notRendered() -> full re-upload, matching the
    // single-scene reference where remesh drives the same behavior.
    m_Models[pageNumber][modelIndex].file->QueueMesh(imageWidth, imageHeight, sceneMinBound, sceneMaxBound, m_Models[pageNumber][modelIndex].remesh, orthographic, m_Models[pageNumber][modelIndex].drawMode);

    // Check if any VertexBuffer has data to render (vkrender.cc: drawBuffer skips empty).
    bool hasRenderableData = !materialData.indices.empty() || !colorData.indices.empty() || !lineData.indices.empty() || !transparentData.indices.empty() || !pointData.indices.empty();

    if (!hasRenderableData) {
        LOG("[v3d] RenderModel: EXIT - no renderable data after QueueMesh (culled?)\n");
        // Match the reference (vkrender.cc): the frame is still cleared every
        // render, so an all-culled scene (e.g. translated fully offscreen) shows
        // the v3d background color, not black.
        const auto& bg = m_Models[pageNumber][modelIndex].file->headerInfo.background;
        QImage image{ imageWidth, imageHeight, QImage::Format_RGB32 };
        image.fill(QColor(qBound(0, int(bg.r * 255.0f + 0.5f), 255),
                          qBound(0, int(bg.g * 255.0f + 0.5f), 255),
                          qBound(0, int(bg.b * 255.0f + 0.5f), 255)));
        image.setColorSpace(QColorSpace());
        return image;
    }

    // Match Asymptote prepareScene(): remesh is consumed after one non-OUTLINE render.
    if (m_Models[pageNumber][modelIndex].drawMode != DRAWMODE_OUTLINE)
        m_Models[pageNumber][modelIndex].remesh = false;

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
    // Derive pipeline mode from global VertexBuffers (vkrender.cc: each data type
    // drawn independently; no intermediate Mesh struct needed).
    MeshPipelineMode pipelineMode = MeshPipelineMode::MaterialOnly;
    if (!colorData.indices.empty() && !materialData.indices.empty()) {
        pipelineMode = MeshPipelineMode::Mixed;
    } else if (!colorData.indices.empty()) {
        pipelineMode = MeshPipelineMode::ColorOnly;
    }

    unsigned char* imageData = m_HeadlessRenderer->render(
        glm::ivec2{ imageWidth, imageHeight }, 
        &imageSubresourceLayout, 
        m_Models[pageNumber][modelIndex].viewMat, 
        m_Models[pageNumber][modelIndex].projMat,
        m_Models[pageNumber][modelIndex].normMat,
        m_Models[pageNumber][modelIndex].file->materials,
        m_Models[pageNumber][modelIndex].file->headerInfo.lights,
        pipelineMode,
        bgColor,
        m_Models[pageNumber][modelIndex].file->headerInfo.orthographic,
        useIBL,
        iblPath,
        m_Models[pageNumber][modelIndex].drawMode,
        m_Models[pageNumber][modelIndex].remesh
    );

    if (!imageData) {
        LOG("[v3d] RenderModel: EXIT - HeadlessRenderer::render returned nullptr\n");
        QImage image{ imageWidth, imageHeight, QImage::Format_ARGB32 };
        image.fill(Qt::black);
        return image;
    }

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

    // Convert to RGB32 (no alpha channel) so Qt/Okular draws the pixels
    // directly without any compositing over its paperColor background.
    // The A-buffer blend shader already composited all transparent fragments
    // against the v3d background color, so the final pixel colors are correct
    // and no further alpha blending is needed.
    image = image.convertToFormat(QImage::Format_RGB32);

    // Prevent Qt/Okular from applying sRGB gamma correction on display.
    // The shader handles color space (OUTPUT_AS_SRGB when srgb=true).
    // With no managed color space, Qt treats pixel values as-is.
    image.setColorSpace(QColorSpace());

    // No vertical mirror needed: viewport {0,H,W,-H} maps NDC Y=+1 to
    // framebuffer row 0, and QImage row 0 = top of screen.
    // So scene +Y naturally appears at the top.

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

bool V3dModelManager::ShouldSuppressPageViewRender() const {
    // Event-driven state (see m_SuppressPageViewRender): true only between
    // presentation-only refreshes and the matching mouse release (or
    // PresentationWidget destroy). No clock, no window.
    return m_SuppressPageViewRender.load(std::memory_order_acquire);
}

QImage V3dModelManager::LastRenderedModelImage(size_t pageNumber, size_t modelIndex) const {
    QMutexLocker locker(&m_ModelsMutex);

    if (pageNumber >= m_ModelImages.size() || modelIndex >= m_ModelImages[pageNumber].size()) {
        return QImage();
    }
    // Implicitly shared copy (no pixel data duplication).
    return m_ModelImages[pageNumber][modelIndex];
}

QImage V3dModelManager::CachedModelImage(size_t pageNumber, size_t modelIndex, int width, int height) const {
    QMutexLocker locker(&m_ModelsMutex);

    if (pageNumber >= m_ModelImages.size() || modelIndex >= m_ModelImages[pageNumber].size()) {
        return QImage();
    }

    QImage cached = m_ModelImages[pageNumber][modelIndex];
    if (cached.isNull()) {
        return QImage();
    }
    if (width > 0 && height > 0 && (cached.width() != width || cached.height() != height)) {
        cached = cached.scaled(width, height, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
    return cached;
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

    LOG("[v3d] mouseMoveEvent: pos=(%d,%d) dragging=%d pageViewVisible=%d\n",
        m_MousePosition.x, m_MousePosition.y, (int)m_Dragging, m_PageView ? (int)m_PageView->isVisible() : 0);

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

    glm::vec2 pageViewSize = GetActiveViewportSize();

    bool controlKey = event->modifiers() & Qt::ControlModifier;
    bool shiftKey = event->modifiers() & Qt::ShiftModifier;
    bool altKey = event->modifiers() & Qt::AltModifier;

    if (controlKey && !shiftKey && !altKey) {
        EnsureCachedRequestSize((size_t)m_ActiveModelPage);

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
    LOG("[v3d] mouseButtonPressEvent: button=%d pos=(%d,%d)\n", (int)event->button(), (int)event->position().x(), (int)event->position().y());
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

    LOG("[v3d] mouseButtonPressEvent: hit page=%d (mousePos=%d,%d, lastSource=%s)\n",
        pageMouseIsOver, m_MousePosition.x, m_MousePosition.y,
        m_LastEventSource ? m_LastEventSource->metaObject()->className() : "(null)");

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

    // End of a gesture: stop suppressing PageView renders (if a presentation
    // drag was in progress). No catch-up refresh here: the frozen window keeps
    // its last frame until the user next interacts with it, at which point its
    // first frame re-renders it at its own size. Forcing a cross-size render on
    // every gesture end triggered full renderer resource recreations that
    // proved fragile (Vulkan device loss observed on some hardware, especially
    // with transparent scenes).
    m_SuppressPageViewRender.store(false, std::memory_order_release);

    return false;
}

bool V3dModelManager::wheelEvent(QWheelEvent* event) {
    LOG("[v3d] wheelEvent: delta=(%d,%d) models=%zu\n", event->angleDelta().x(), event->angleDelta().y(), m_Models.size());
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

    // Asymptote ignores shift on scroll — just use whichever axis has the delta.
    // Qt converts vertical→horizontal when shift is held, zeroing .y().
    int deltaY = event->angleDelta().y();
    if (deltaY == 0)
        deltaY = event->angleDelta().x();

    if (deltaY < 0) {
        targetModel->Zoom /= targetModel->file->headerInfo.zoomFactor;
    } else {
        targetModel->Zoom *= targetModel->file->headerInfo.zoomFactor;
    }

    float maxZoom = std::sqrt(std::numeric_limits<float>::max());
    float minZoom = 1 / maxZoom;

    if (targetModel->Zoom < minZoom) {
        targetModel->Zoom = minZoom;
    } else if (targetModel->Zoom > maxZoom) {
        targetModel->Zoom = maxZoom;
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

    LOG("[v3d] keyPressEvent: key=0x%x\n", event->key());

    if (event->key() == Qt::Key_H) {
        int pageMouseIsOver = GetPageMouseIsOver();
        if (pageMouseIsOver == -1 || pageMouseIsOver >= (int)m_Models.size()) return false;

        glm::vec2 normalizedMousePositionOnPage = GetNormalizedPositionRelativeToPage(m_MousePosition, pageMouseIsOver);
        for (auto& model : m_Models[pageMouseIsOver]) {
            bool horizontallyOnModel = normalizedMousePositionOnPage.x > model.minBound.x && normalizedMousePositionOnPage.x < model.maxBound.x;
            bool verticallyOnModel = normalizedMousePositionOnPage.y > model.minBound.y && normalizedMousePositionOnPage.y < model.maxBound.y;

            if (horizontallyOnModel && verticallyOnModel) {
                model.home();
                // Exception to the gesture-end catch-up rule: a mode/home
                // keystroke refreshes only the CURRENT window (per the
                // per-window gate). Refreshing both would force a full
                // renderer resource recreation at the other window's size on
                // every key press, and rapid repeated recreations have been
                // observed to lose the Vulkan device. The other window
                // catches up on its next interaction (or on Esc).
                refreshPixmap((size_t)pageMouseIsOver);
                break;
            }
        }
        return true;
    }

    if (event->key() == Qt::Key_M) {
        int pageMouseIsOver = GetPageMouseIsOver();
        if (pageMouseIsOver == -1 || pageMouseIsOver >= (int)m_Models.size()) return false;

        glm::vec2 normalizedMousePositionOnPage = GetNormalizedPositionRelativeToPage(m_MousePosition, pageMouseIsOver);
        for (auto& model : m_Models[pageMouseIsOver]) {
            bool horizontallyOnModel = normalizedMousePositionOnPage.x > model.minBound.x && normalizedMousePositionOnPage.x < model.maxBound.x;
            bool verticallyOnModel = normalizedMousePositionOnPage.y > model.minBound.y && normalizedMousePositionOnPage.y < model.maxBound.y;

            if (horizontallyOnModel && verticallyOnModel) {
                model.cycleMode();
                // See the Key_H case above: current window only.
                refreshPixmap((size_t)pageMouseIsOver);
                break;
            }
        }
        return true;
    }

    return false;
}

void V3dModelManager::DrawMouseBoundaries(QImage* img, size_t pageNumber) {
#ifndef MOUSE_BOUNDARIES
    (void)img; (void)pageNumber;
#endif
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
    // Request-cache state (m_CachedRequestSizes, m_Pages,
    // m_PresentationRequestSizes, m_PageViewObserver) is written here from
    // pixmap worker threads and read from the main thread, so everything
    // runs under m_ModelsMutex.
    QMutexLocker locker(&m_ModelsMutex);
    cacheRequestUnlocked(request);
}

void V3dModelManager::cacheRequestUnlocked(Okular::PixmapRequest* request) {
    Okular::Page* page = request->page();

    // Remember the PageView's observer: Okular's PageView passes itself as the
    // observer of its PixmapRequests (the pointer the Page stores as its
    // rendered-pixmap key). refreshPixmap() uses it to delete only the
    // PageView's page pixmap -- leaving the thumbnails' and presentation's
    // pixmaps untouched.
    if (request->priority() == PAGEVIEW_PRIO) {
        m_PageViewObserver = request->observer();
    }

    // Presentation-mode requests (PRESENTATION_PRIO / PRESENTATION_PRELOAD_PRIO)
    // come from Okular's PresentationWidget (a separate window from the PageView)
    // and are the actual on-screen view while a presentation is active. Treat them
    // like pageview requests: update the cached request size (which drives the
    // projection canvas) and cache the page. Without this, the plugin keeps
    // projecting for the stale normal-mode size while rendering into the
    // presentation window.
    if (request->priority() != PAGEVIEW_PRIO && request->priority() != PAGEVIEW_PRELOAD_PRIO &&
        request->priority() != PRESENTATION_PRIO && request->priority() != PRESENTATION_PRELOAD_PRIO) {
        if (m_CachedRequestSizes.size() < (size_t)(page->number() + 1) || (m_CachedRequestSizes[page->number()].size.x == 0 && m_CachedRequestSizes[page->number()].size.y == 0)) {
            cacheRequestSizeUnlocked(page->number(), request->width(), request->height(), request->priority());
        }
        return;
    }

    if (request->priority() == PRESENTATION_PRIO || request->priority() == PRESENTATION_PRELOAD_PRIO) {
        // Remember the presentation window's request size (device pixels) so
        // presentation-mode hit testing and pixmap re-requests can use it,
        // independent of interleaved PageView requests.
        if (m_PresentationRequestSizes.size() < (size_t)(page->number() + 1)) {
            m_PresentationRequestSizes.resize(page->number() + 1);
        }
        m_PresentationRequestSizes[page->number()] = glm::ivec2{ request->width(), request->height() };
    }

    cacheRequestSizeUnlocked(page->number(), request->width(), request->height(), request->priority());
    cachePageUnlocked(page->number(), page);

#ifdef MOUSE_BOUNDARIES
    if (m_MouseBoundaryLines.size() < page->number() + 1) {
        m_MouseBoundaryLines.resize(page->number() + 1);
        m_MouseBoundaryPoints.resize(page->number() + 1);
    }
#endif
}

void V3dModelManager::EnsureCachedRequestSize(size_t pageNumber) {
    // Caller holds m_ModelsMutex.
    if (m_CachedRequestSizes.size() <= pageNumber) {
        m_CachedRequestSizes.resize(pageNumber + 1);
    }
}

void V3dModelManager::CacheRequestSize(size_t pageNumber, int width, int height, int priority) {
    QMutexLocker locker(&m_ModelsMutex);
    cacheRequestSizeUnlocked(pageNumber, width, height, priority);
}

void V3dModelManager::cacheRequestSizeUnlocked(size_t pageNumber, int width, int height, int priority) {
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
    QMutexLocker locker(&m_ModelsMutex);
    cachePageUnlocked(pageNumber, page);
}

void V3dModelManager::cachePageUnlocked(size_t pageNumber, Okular::Page* page) {
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

std::vector<V3dModelManager::PageBorders> V3dModelManager::GetPresentationPageBorders() {
    std::vector<PageBorders> pageBorders;

    if (!m_PresentationWidget || !m_Document) {
        return pageBorders;
    }

    // The PresentationWidget follows the shared document viewport
    // (changePage -> setViewportPage), so it is the presentation's frame.
    int pageNumber = m_Document->viewport().pageNumber;
    if (pageNumber < 0) {
        return pageBorders;
    }

    EnsureCachedRequestSize((size_t)pageNumber);

    // The presentation window's request size (device pixels). Require an actual
    // presentation request for this page: before the first page is displayed
    // (e.g. the "Click to begin" summary frame) no page is on screen, so no hit
    // testing should happen -- letting the click through starts the show.
    if (m_PresentationRequestSizes.size() <= (size_t)pageNumber) {
        return pageBorders;
    }
    int devW = m_PresentationRequestSizes[pageNumber].x;
    int devH = m_PresentationRequestSizes[pageNumber].y;
    if (devW <= 0 || devH <= 0) {
        return pageBorders;
    }

    float dpr = m_PresentationWidget->devicePixelRatioF();
    glm::vec2 pageSize{ devW / dpr, devH / dpr };
    glm::vec2 widgetSize{ (float)m_PresentationWidget->width(), (float)m_PresentationWidget->height() };

    // PresentationFrame::recalcGeometry(): the page is fitted into the window
    // keeping its aspect ratio and centered in both directions.
    PageBorders border;
    border.pageNumber = pageNumber;
    border.le = border.ri = (widgetSize.x - pageSize.x) / 2.0f;
    border.hi = border.lo = (widgetSize.y - pageSize.y) / 2.0f;
    pageBorders.push_back(border);

    return pageBorders;
}

std::vector<V3dModelManager::PageBorders> V3dModelManager::GetActivePageBorders() {
    if (m_LastEventSource && m_LastEventSource->inherits("PresentationWidget")) {
        return GetPresentationPageBorders();
    }
    return GetPageBordersForVisiblePages();
}

glm::vec2 V3dModelManager::GetActiveViewportSize() {
    if (m_LastEventSource && m_LastEventSource->inherits("PresentationWidget")) {
        if (QWidget* widget = qobject_cast<QWidget*>(m_LastEventSource)) {
            return glm::vec2{ (float)widget->width(), (float)widget->height() };
        }
    }
    if (m_PageView) {
        return glm::vec2{ (float)m_PageView->width(), (float)m_PageView->height() };
    }
    return glm::vec2{ 0.0f, 0.0f };
}

void V3dModelManager::noteEventSource(QObject* object) {
    // Main thread only (both event filters run there).
    m_LastEventSource = object;
}

void V3dModelManager::handlePresentationWidgetEvent(QObject* object, QEvent::Type type) {
    if (!object || !object->inherits("PresentationWidget")) {
        return;
    }

    if (type == QEvent::Show) {
        QWidget* widget = qobject_cast<QWidget*>(object);
        if (!widget) {
            return;
        }
        if (m_PresentationWidget != widget) {
            m_PresentationWidget = widget;
            if (!m_PresentationEventFilter) {
                m_PresentationEventFilter = new EventFilter(&m_FilterParent, this);
            }
            if (m_PresentationEventFilterOn != widget) {
                m_PresentationEventFilterOn = widget;
                widget->installEventFilter(m_PresentationEventFilter);
            }
            LOG("[v3d] PresentationWidget shown (%p): mouse event filter installed\n", (void*)widget);
        }
    } else if (type == QEvent::Destroy) {
        LOG("[v3d] PresentationWidget destroyed (%p)\n", (void*)object);
        // m_PresentationWidget (QPointer) nulls itself; Qt unregisters the
        // event filter from the destroyed object automatically.
        m_PresentationWidget = nullptr;

        // A drag in the presentation window can never be released now, so
        // clear the suppression flag (the catch-up refresh below re-renders
        // the PageView anyway).
        m_SuppressPageViewRender.store(false, std::memory_order_release);

        // The presentation window is gone. Its refreshes intentionally left
        // the PageView's cached pixmap untouched (frozen), so refresh the
        // normal window now to show the final model state. Skipped during
        // application shutdown, when the document (and the cached Page*
        // pointers) may already be gone.
        if (!QCoreApplication::closingDown() && !m_Destroying && m_Document &&
            m_PageView && m_PageView->isVisible()) {
            const int page = m_Document->viewport().pageNumber;
            if (page >= 0) {
                refreshPixmap((size_t)page);
            }
        }
    }
}

int V3dModelManager::GetPageMouseIsOver() {
    std::vector<PageBorders> pageBorders = GetActivePageBorders();

    glm::vec2 viewPortSize = GetActiveViewportSize();

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
    std::vector<PageBorders> pageBorders = GetActivePageBorders();

    // The page size used for normalization MUST match the size used to compute
    // the borders above (same source, same dpr). m_CachedRequestSizes holds the
    // most recent request of ANY observer: in side-by-side mode the PageView can
    // re-request the page at its own size, which would silently desync the
    // presentation hit test (borders from the presentation size, normalization
    // from the PageView size) and make presses over the model pass through to
    // the PresentationWidget (-> page navigation).
    float dpr;
    glm::ivec2 devSize{ 0, 0 };
    if (m_LastEventSource && m_LastEventSource->inherits("PresentationWidget") &&
        m_PresentationRequestSizes.size() > (size_t)pageNumber &&
        (m_PresentationRequestSizes[pageNumber].x > 0 || m_PresentationRequestSizes[pageNumber].y > 0)) {
        devSize = m_PresentationRequestSizes[pageNumber];
        dpr = m_PresentationWidget ? m_PresentationWidget->devicePixelRatioF() : GetDevicePixelRatio();
    }
    if (devSize.x <= 0 || devSize.y <= 0) {
        dpr = GetDevicePixelRatio();
        EnsureCachedRequestSize((size_t)pageNumber);
        devSize = m_CachedRequestSizes[pageNumber].size;
    }
    glm::vec2 pageSize{ devSize.x / dpr, devSize.y / dpr };

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

    // Which window the user is currently interacting with (main thread only).
    const bool presentationActive = (m_PresentationWidget != nullptr);
    const bool interactingWithPresentation =
        presentationActive && m_LastEventSource != nullptr &&
        m_LastEventSource->inherits("PresentationWidget");

    // On a true dual-screen setup (normal window and presentation window on
    // different physical displays -- e.g. projector + laptop) the window that
    // is NOT being interacted with is the one that must stay live for the
    // audience (or as the presenter's preview), so BOTH windows are refreshed
    // on every drag frame. On the same screen -- side-by-side, or mirrored
    // displays (which X/Qt expose as a single screen) -- the inactive window
    // is at best a duplicate: skip it; it shows its last pixels and catches
    // up with one render on its next natural repaint. If the screens cannot
    // be determined, keep the safe status-quo behaviour (refresh both).
    bool dualScreen = true;
    if (presentationActive && m_PageView) {
        QWidget* pageViewWindow = m_PageView->window();
        if (pageViewWindow != nullptr) {
            QScreen* pageViewScreen = QApplication::screenAt(pageViewWindow->frameGeometry().center());
            QScreen* presScreen = QApplication::screenAt(m_PresentationWidget->frameGeometry().center());
            dualScreen = (pageViewScreen == nullptr || presScreen == nullptr || pageViewScreen != presScreen);
        }
    }

    // Whether the normal window is refreshed by this call.
    const bool refreshPageView =
        m_PageView && m_PageView->isVisible() && (!interactingWithPresentation || dualScreen);

    // Delete only the PageView's OWN pixmap -- not the pixmaps of ALL
    // observers (Page::deletePixmaps also orphaned the thumbnail panel's
    // pixmap: the panel is not repainted at that moment, so its next repaint,
    // e.g. after a small document scroll, painted the thumbnail from the
    // now-empty Page and the thumbnail "disappeared"). Thumbnails are static
    // previews that never need refreshing on model rotation, so their
    // pixmaps stay in place. The PresentationWidget likewise deletes its own
    // observer's pixmap in the resizeEvent handler triggered below and does
    // not need a wipe.
    //
    // The delete is ONLY done when the PageView is refreshed (or no
    // presentation is active). For a presentation-only refresh it must be
    // SKIPPED:
    //  - wiping the PageView's cached pixmap would be harmless by itself
    //    (Page::deletePixmap does not notify), BUT the presentation's new
    //    priority-0 request cancels any executing PageView request and the
    //    Document then notifies the PageView (notifyContentsCleared ->
    //    queued slotRequestVisiblePixmaps), which re-requests the page. Each
    //    drag frame cancels the previous catch-up render, starting a loop of
    //    delayed PageView renders that contends with the presentation's own
    //    renders for m_ModelsMutex and the GPU -- the "normal window lags
    //    the presentation window" effect and the interactive slowdown.
    //    With the PageView's pixmap left in place, it simply keeps showing
    //    its last pixels (frozen) until the user interacts with it.
    if (refreshPageView || !presentationActive) {
        if (m_PageViewObserver != nullptr) {
            m_Pages[pageNumber]->deletePixmap(m_PageViewObserver);
        }
    }

    // Normal view: the synthetic Ctrl key release makes the PageView redraw,
    // which re-requests the deleted pixmap. Skip it while the PageView is not
    // visible (e.g., covered by the fullscreen presentation window) or while
    // the user is interacting with the presentation window on the same screen
    // (the PageView is then just a duplicate and re-rendering it only slows
    // the interactive presentation down).
    if (refreshPageView) {
        QKeyEvent keyEvent(
            QEvent::KeyRelease, // type
            Qt::Key_Control,    // key
            Qt::NoModifier      // modifiers
        );

        ProtectedFunctionCaller::callKeyReleaseEvent(m_PageView, &keyEvent);
    }

    // Presentation mode: the PresentationWidget only re-requests its pixmap on
    // page change / resize (nothing re-triggers it after deletePixmaps()), so
    // deliver a synthetic resize event to it. Its resizeEvent() then calls
    // invalidatePixmaps() (clear m_lastRenderedPixmap, delete its own
    // observer's pixmap + requestPixmaps()) and generatePage() (repaint the
    // frame from the freshly rendered pixmap). The v3d generator renders
    // synchronously on this thread, so the refreshed pixmap is in place
    // before the next paint (no flicker).
    //
    // NOTE: resizeEvent() bails out early when size() == re->oldSize(), so a
    // same-size resize is a no-op and the window keeps painting its cached
    // m_lastRenderedPixmap forever. Report a harmless fake oldSize (1px
    // smaller) so the handler proceeds; the frame geometry is recomputed from
    // the widget's REAL size, so nothing visibly changes.
    //
    // Only re-trigger the presentation window when the user is interacting
    // with it, or when the windows are on different screens (the audience
    // must keep seeing live updates even while the presenter drives the
    // normal window).
    if (presentationActive && m_Document && m_Document->viewport().pageNumber == (int)pageNumber &&
        (interactingWithPresentation || dualScreen)) {
        if (interactingWithPresentation && !dualScreen) {
            // Presentation-only refresh: mark PageView rendering as
            // suppressed. The generators then serve PageView-priority
            // requests stale (see ShouldSuppressPageViewRender), which makes
            // the PageView's cancel/re-request catch-up loop (driven by the
            // Document, not by us) costless: no poppler/Vulkan re-render, no
            // mutex or GPU contention with the presentation's interactive
            // rendering. The flag is sticky for the duration of the drag and
            // cleared only by a real signal: mouse release (which also
            // triggers one catch-up PageView refresh) or PresentationWidget
            // destroy.
            m_SuppressPageViewRender.store(true, std::memory_order_release);
        }
        QWidget* presWidget = m_PresentationWidget.data();
        QResizeEvent resizeEvent(presWidget->size(), presWidget->size() - QSize(1, 0));
        QApplication::sendEvent(presWidget, &resizeEvent);
    }
}

std::string V3dModelManager::resolveIBLPath(const std::string& imageName) {
    if (imageName.empty()) return "";

    std::string name = normalizeIBLName(imageName);
    if (name.empty()) return "";

    // Reject names that could escape the image library directory; treat as
    // IBL unavailable (no local probes, no prompt, no download).
    if (!iblNameOK(name)) {
        logRejectedIBLName(imageName);
        return "";
    }

    // Helper: check if a path exists and return it.
    auto tryPath = [](const std::string& path) -> std::string {
        std::ifstream f(path.c_str());
        return f.good() ? path : "";
    };

    // The iblPath must point to a directory containing diffuse.exr and all
    // 11 reflection layers refl0..10.exr. We also verify that the parent
    // directory has refl.exr (common BRDF LUT). A directory missing any file
    // is a partial/interrupted download and must not be accepted: the
    // renderer would then bind null views/samplers for the missing layers.
    auto tryIBLDir = [&tryPath](const std::string& dir) -> std::string {
        if (!dir.empty() && dir.back() == '/') return dir;
        std::string d = dir + "/";
        // Check diffuse.exr exists in this directory.
        std::string diffuse = tryPath(d + "diffuse.exr");
        if (diffuse.empty()) return "";
        // Check all 11 reflection layers exist in this directory.
        for (int i = 0; i <= 10; ++i) {
            if (tryPath(d + "refl" + std::to_string(i) + ".exr").empty()) return "";
        }
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
        QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        return QString(QStringLiteral("%1/okular/ibl")).arg(dataDir).toStdString();
    };

    std::string iblBase = getIBLBase();

    // The IBL files for this document live in <iblBase>/<name>/ (name is a
    // relative path within the library, e.g. "snowyField"). A miss is a
    // miss: never substitute a different cached image for the one the
    // document requested.
    std::string found = tryIBLDir(iblBase + "/" + name);
    if (!found.empty()) return found;

    return "";
}

void V3dModelManager::performIBLDownload(const std::string& imageName, const std::string& iblBase) {
    // If the name contains an absolute path, extract just the filename; relative paths are ok.
    std::string name = normalizeIBLName(imageName);

    // Last line of defense: never prompt for or write outside the image
    // library directory, regardless of caller.
    if (!iblNameOK(name)) {
        logRejectedIBLName(imageName);
        return;
    }

    const char* imageUrlEnv = std::getenv("OKULAR_V3D_IMAGE_URL");
    std::string baseUrl = imageUrlEnv && strlen(imageUrlEnv) > 0
        ? std::string(imageUrlEnv)
        : "https://vectorgraphics.gitlab.io/asymptote/ibl";

    QString qImageName = QString::fromStdString(name);
    QString qIblBase = iblBase.empty()
        ? QString(QStringLiteral("%1/okular/ibl")).arg(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        : QString::fromStdString(iblBase);

    // Download directory: <iblBase>/<imageName>/  (e.g., $XDG_DATA_HOME/okular/ibl/snowyField/)
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

    // Build the message. Use concatenation rather than chained QString::arg:
    // both the source URL (OKULAR_V3D_IMAGE_URL) and the destination directory
    // (OKULAR_V3D_IMAGE_DIR / XDG) come from the environment and could contain
    // a literal '%1'/'%2'/'%3' that arg() would misread as a placeholder and
    // mangle. qImageName is known to be '%' -free (see iblNameOK).
    const QString fromUrl = QString::fromStdString(baseUrl) + QLatin1Char('/') + qImageName + QLatin1Char('/');
    QString msg = QStringLiteral("The IBL environment map '") + qImageName
               + QStringLiteral("' was not found locally.\n\n")
               + QStringLiteral("Would you like to download it (")
               + QString::number(quint64(tasks.size()))
               + QStringLiteral(" files) from:\n") + fromUrl
               + QStringLiteral("\n\nto:\n") + envDir
               + QLatin1Char('?');

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
        // RenderModel (worker thread) inserts into m_ModelsWithMissingIBL
        // under m_ModelsMutex; guard our iteration/clear the same way.
        {
            QMutexLocker locker(&m_ModelsMutex);
            for (const auto& [pn, mi] : m_ModelsWithMissingIBL) {
                pages.insert(pn);
            }
            m_ModelsWithMissingIBL.clear();
        }
        for (size_t pn : pages) {
            scheduleRefreshPixmap(pn, 25);
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

    std::set<size_t> affectedPages;
    // RenderModel (worker thread) inserts into m_ModelsWithMissingIBL and
    // resizes m_Models under m_ModelsMutex; guard our iteration/clear and
    // the m_Models[pn][mi] write the same way.
    {
        QMutexLocker locker(&m_ModelsMutex);
        for (const auto& [pn, mi] : m_ModelsWithMissingIBL) {
            if (m_Models.size() > pn && m_Models[pn].size() > mi) {
                m_Models[pn][mi].m_HasChanged = true;
            }
            affectedPages.insert(pn);
        }
        m_ModelsWithMissingIBL.clear();
    }

    // Force Okular to re-request pixmaps for each affected page.
    // Stagger the calls to avoid the 1/60s refresh throttle.
    for (size_t pn : affectedPages) {
        scheduleRefreshPixmap(pn, 25);
    }
}

void V3dModelManager::scheduleRefreshPixmap(size_t pageNumber, int delayMs) {
    // Parent to m_FilterParent (destroyed with the manager) so the timeout can
    // never fire after the manager is gone -- the old parentless timers read
    // m_Destroying from freed memory if destruction beat the delay -- and have
    // it delete itself on fire so it never leaks. Both this and refreshPixmap()
    // run on the main thread, so there is no cross-thread QObject ownership.
    QTimer* timer = new QTimer(&m_FilterParent);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, [this, pageNumber]() {
        if (!m_Destroying) refreshPixmap(pageNumber);
    });
    QObject::connect(timer, &QTimer::timeout, timer, &QObject::deleteLater);
    timer->start(delayMs);
}

std::string V3dModelManager::getIBLBase() const {
    const char* envDir = std::getenv("OKULAR_V3D_IMAGE_DIR");
    if (envDir && strlen(envDir) > 0) return std::string(envDir);
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QString(QStringLiteral("%1/okular/ibl")).arg(dataDir).toStdString();
}

void V3dModelManager::scheduleIBLPrompt(const std::string& imageName, const std::string& iblBase) {
    // Reject here: no prompt and no download for names that could escape the
    // image library directory.
    if (!iblNameOK(normalizeIBLName(imageName))) {
        logRejectedIBLName(imageName);
        return;
    }

    // Debounce: avoid spamming the user with repeated prompts for the same image.
    static std::set<std::string> pendingPrompts;
    if (pendingPrompts.count(imageName)) return;

    pendingPrompts.insert(imageName);

    // Post a custom event to qApp's main thread event queue. The IBLEventFilter
    // will catch it and run the callback on the main thread, showing the dialog.
    // This works from both background threads (PDF loading) and the main thread
    // (standalone .v3d), since postEvent always delivers to the receiver's thread.
    // The callback runs on the main thread via IBLEventFilter, which is destroyed
    // with the manager, so the raw 'this' capture here is safe: the filter can
    // never deliver the event to a destroyed manager.
    IBLPromptEvent* event = new IBLPromptEvent(
        [this, imageName, iblBase]() -> bool {
            performIBLDownload(imageName, iblBase);
            return !m_IBLDeclined;
        }
    );

    QCoreApplication::postEvent(qApp, event);
}
