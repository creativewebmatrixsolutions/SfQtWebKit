/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtWebEngine module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** As a special exception, The Qt Company gives you certain additional
** rights. These rights are described in The Qt Company LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file. Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

import QtQuick 2.0
import QtTest 1.0
import QtWebEngine 1.1

TestWebEngineView {
    id: webEngineView
    width: 200
    height: 400
    focus: true

    property string lastUrl

    SignalSpy {
        id: spy
        target: webEngineView
        signalName: "linkHovered"
    }

    onLinkHovered: {
        webEngineView.lastUrl = hoveredUrl
    }

    TestCase {
        name: "DesktopWebEngineViewLinkHovered"

        // Delayed windowShown to workaround problems with Qt5 in debug mode.
        when: false
        Timer {
            running: parent.windowShown
            repeat: false
            interval: 1
            onTriggered: parent.when = true
        }

        function init() {
            webEngineView.lastUrl = ""
            spy.clear()
        }

        function test_linkHovered() {
            compare(spy.count, 0)
            mouseMove(webEngineView, 100, 300)
            webEngineView.url = Qt.resolvedUrl("test2.html")
            verify(webEngineView.waitForLoadSucceeded())

            // We get a linkHovered signal with empty hoveredUrl after page load
            spy.wait()
            compare(spy.count, 1)
            compare(webEngineView.lastUrl, "")

            mouseMove(webEngineView, 100, 100)
            spy.wait()
            compare(spy.count, 2)
            compare(webEngineView.lastUrl, Qt.resolvedUrl("test1.html"))
            mouseMove(webEngineView, 100, 300)
            spy.wait()
            compare(spy.count, 3)
            compare(webEngineView.lastUrl, "")
        }

        function test_linkHoveredDoesntEmitRepeated() {
            compare(spy.count, 0)
            webEngineView.url = Qt.resolvedUrl("test2.html")
            verify(webEngineView.waitForLoadSucceeded())

            // We get a linkHovered signal with empty hoveredUrl after page load
            spy.wait()
            compare(spy.count, 1)
            compare(webEngineView.lastUrl, "")

            for (var i = 0; i < 100; i += 10)
                mouseMove(webEngineView, 100, 100 + i)

            spy.wait()
            compare(spy.count, 2)
            compare(webEngineView.lastUrl, Qt.resolvedUrl("test1.html"))

            for (var i = 0; i < 100; i += 10)
                mouseMove(webEngineView, 100, 300 + i)

            spy.wait()
            compare(spy.count, 3)
            compare(webEngineView.lastUrl, "")
        }
    }
}
