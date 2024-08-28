/*
    Orignal Source: txt plugin for Okular:
    SPDX-FileCopyrightText: 2013 Azat Khuzhin <a3at.mail@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later

    Code has been modified by Benjamin Bingham to become: v3d_generator
*/
#pragma once

#include <memory>

#include <core/textdocumentgenerator.h>

#include "../3rdParty/V3D-Common/Rendering/renderheadless.h"
#include "../3rdParty/V3D-Common/V3dFile/V3dFile.h"

#include <thread>
#include "part/pageview.h"

#include <core/generator.h>

#include <chrono>

#include "../3rdParty/V3D-Common/V3dModelManager.h"

class EventFilter;

class V3dGenerator : public Okular::Generator {
    Q_OBJECT
    Q_INTERFACES(Okular::Generator)

public:
    V3dGenerator(QObject *parent, const QVariantList &args);
    ~V3dGenerator() override = default;

    void generatePixmap(Okular::PixmapRequest* request) override;
    bool loadDocument(const QString &fileName, QVector<Okular::Page*> &pages) override;
    bool doCloseDocument() override;

private:
    //V3dModelManager m_ModelManager{ document(), "kde/src/okular/generators/Okular-v3d-Plugin-Code/3rdParty/V3D-Common/shaders/" };
    V3dModelManager m_ModelManager{ document(), "./" };
};
