### A common set of files used to support V3D files inside of Okular Plugins.

# To use the plugin:
1. Add an instance of `V3dModelManager` as a member of your generator
2. In the `loadDocument` function of your generator give the model manager a pointer to the document, and add in any models located in the document:
    ```
    if (document() != nullptr) {
        m_ModelManager.SetDocument(document());
    }

    m_ModelManager.AddModel(V3dModel{ fileName.toStdString() }, 0);
    ```
3. In the `generatePixmap` function of you generator, cache the request, than call the Render function and present the QImage to the page however you want to:
    ```    
    m_ModelManager.CacheRequest(request);

    QImage image = m_ModelManager.RenderModel(0, 0, (int)request->width(), (int)request->height());
    ```

## TODO
* multi page documents - Basics are done, still need to do:
    * in the short term im assuming all pages in a document are the same size, fix this
    * the 12 pixel margin may change depending on resolution, 2k vs 4k vs 1080p
* Okular zoom optimization
* shader paths change when an external user uses the plugin
    * Currently the plugin looks for the shaders in the path that Okular is run in
* New renderer
* Clean up includes
* Clean up CMAKE files
* Look into adding a tile manager to the generator
* panning and shifting
* make the index and vertex buffers once, than cache them, currently they are recreated every time we rerender
* documentation
* Presentation Mode dosent work at all
* Customizable Controls

## BUGS
* gamma3 still doesn’t work
* zooming in and out a lot can still cause a DEVICE OUT OF MEMORY vulkan error
* Opening multiple documents causes a crash
* Flickering on model movement on some systems
