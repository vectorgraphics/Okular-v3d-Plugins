/*
    Orignal Source: txt plugin for Okular:
    SPDX-FileCopyrightText: 2013 Azat Khuzhin <a3at.mail@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later

    Code has been modified by Benjamin Bingham to become: v3d_generator
*/

#include "v3d_generator.h"

#include <gui/priorities.h>

OKULAR_EXPORT_PLUGIN(V3dGenerator, "libokularGenerator_v3d.json")

V3dGenerator::V3dGenerator(QObject *parent, const QVariantList &args) {
    Q_UNUSED(parent);
    Q_UNUSED(args);
}

void V3dGenerator::generatePixmap(Okular::PixmapRequest* request) {
    m_ModelManager.CacheRequest(request);

    // For thumbnail requests, return a blank image instead of triggering
    // expensive Vulkan rendering. Thumbnails should be static, non-interactive.
    bool isThumbnail = (request->priority() == THUMBNAILS_PRIO || request->priority() == THUMBNAILS_PRELOAD_PRIO);

    QImage image;
    if (isThumbnail) {
        image = QImage((int)request->width(), (int)request->height(), QImage::Format_ARGB32);
        image.fill(Qt::black);
    } else {
        image = m_ModelManager.RenderModel(0, 0, (int)request->width(), (int)request->height());
    }

    QPixmap* pixmap = new QPixmap(QPixmap::fromImage(image));
    request->page()->setPixmap(request->observer(), pixmap);

    signalPixmapRequestDone(request);
}

bool V3dGenerator::loadDocument(const QString &fileName, QVector<Okular::Page *> &pagesVector) {
    if (document() != nullptr) {
        m_ModelManager.SetDocument(document());
    }

    m_ModelManager.AddModel(V3dModel{ fileName.toStdString() }, 0);

    glm::vec2 pageSize = m_ModelManager.GetCanvasSize(0);

    while (!(pageSize.x > 600)) {
        pageSize.x *= 2;
        pageSize.y *= 2;
    }

    Okular::Page* page = new Okular::Page(0, pageSize.x, pageSize.y, Okular::Rotation0);

    pagesVector.append(page);

    return true;
}

bool V3dGenerator::doCloseDocument() {
    return true;
}

#include "v3d_generator.moc"
