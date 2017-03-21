/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the Qt SVG module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL21$
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
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file. Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** As a special exception, The Qt Company gives you certain additional
** rights. These rights are described in The Qt Company LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qsvggenerator.h"

#ifndef QT_NO_SVGGENERATOR

#include "qpainterpath.h"

#include "private/qpaintengine_p.h"
#include "private/qtextengine_p.h"
#include "private/qdrawhelper_p.h"

#include "qfile.h"
#include "qtextcodec.h"
#include "qtextstream.h"
#include "qbuffer.h"
#include "qmath.h"

#include "qdebug.h"

QT_BEGIN_NAMESPACE

static void translate_color(const QColor &color, QString *color_string,
                            QString *opacity_string)
{
    Q_ASSERT(color_string);
    Q_ASSERT(opacity_string);

    *color_string =
        QString::fromLatin1("#%1%2%3")
        .arg(color.red(), 2, 16, QLatin1Char('0'))
        .arg(color.green(), 2, 16, QLatin1Char('0'))
        .arg(color.blue(), 2, 16, QLatin1Char('0'));
    *opacity_string = QString::number(color.alphaF());
}

static void translate_dashPattern(QVector<qreal> pattern, const qreal& width, QString *pattern_string)
{
    Q_ASSERT(pattern_string);

    // Note that SVG operates in absolute lengths, whereas Qt uses a length/width ratio.
    foreach (qreal entry, pattern)
        *pattern_string += QString::fromLatin1("%1,").arg(entry * width);

    pattern_string->chop(1);
}

class QSvgPaintEnginePrivate : public QPaintEnginePrivate
{
public:
    QSvgPaintEnginePrivate()
    {
        size = QSize();
        viewBox = QRectF();
        outputDevice = 0;
        resolution = 72;

        attributes.document_title = QLatin1String("Qt SVG Document");
        attributes.document_description = QLatin1String("Generated with Qt");
        attributes.font_family = QLatin1String("serif");
        attributes.font_size = QLatin1String("10pt");
        attributes.font_style = QLatin1String("normal");
        attributes.font_weight = QLatin1String("normal");

        afterFirstUpdate = false;
        numGradients = 0;
    }

    QSize size;
    QRectF viewBox;
    QIODevice *outputDevice;
    QTextStream *stream;
    int resolution;

    QString header;
    QString defs;
    QString body;
    bool    afterFirstUpdate;

    QBrush brush;
    QPen pen;
    QMatrix matrix;
    QFont font;
	FILE * pFile;

    QString generateGradientName() {
        ++numGradients;
        currentGradientName = QString::fromLatin1("gradient%1").arg(numGradients);
        return currentGradientName;
    }

    QString currentGradientName;
    int numGradients;

    struct _attributes {
        QString document_title;
        QString document_description;
        QString font_weight;
        QString font_size;
        QString font_family;
        QString font_style;
        QString stroke, strokeOpacity;
        QString dashPattern, dashOffset;
        QString fill, fillOpacity;
    } attributes;
};

static inline QPaintEngine::PaintEngineFeatures svgEngineFeatures()
{
    return QPaintEngine::PaintEngineFeatures(
        QPaintEngine::AllFeatures
        & ~QPaintEngine::PatternBrush
        & ~QPaintEngine::PerspectiveTransform
        & ~QPaintEngine::ConicalGradientFill
        & ~QPaintEngine::PorterDuff);
}

class QSvgPaintEngine : public QPaintEngine
{
    Q_DECLARE_PRIVATE(QSvgPaintEngine)
public:

    QSvgPaintEngine()
        : QPaintEngine(*new QSvgPaintEnginePrivate,
                       svgEngineFeatures())
    {
    }

    bool begin(QPaintDevice *device) Q_DECL_OVERRIDE;
    bool end() Q_DECL_OVERRIDE;

	void GraphicsState();
    void updateState(const QPaintEngineState &state) Q_DECL_OVERRIDE;
    void popGroup();

    void drawEllipse(const QRectF &r) Q_DECL_OVERRIDE;
    void drawPath(const QPainterPath &path) Q_DECL_OVERRIDE;
    void drawPixmap(const QRectF &r, const QPixmap &pm, const QRectF &sr) Q_DECL_OVERRIDE;
    void drawPolygon(const QPointF *points, int pointCount, PolygonDrawMode mode) Q_DECL_OVERRIDE;
    void drawRects(const QRectF *rects, int rectCount) Q_DECL_OVERRIDE;
    void drawTextItem(const QPointF &pt, const QTextItem &item) Q_DECL_OVERRIDE;
    void drawImage(const QRectF &r, const QImage &pm, const QRectF &sr,
                   Qt::ImageConversionFlags flags = Qt::AutoColor) Q_DECL_OVERRIDE;

    QPaintEngine::Type type() const Q_DECL_OVERRIDE { return QPaintEngine::SVG; }

    QSize size() const { return d_func()->size; }
    void setSize(const QSize &size) {
        Q_ASSERT(!isActive());
        d_func()->size = size;
    }

    QRectF viewBox() const { return d_func()->viewBox; }
    void setViewBox(const QRectF &viewBox) {
        Q_ASSERT(!isActive());
        d_func()->viewBox = viewBox;
    }

    QString documentTitle() const { return d_func()->attributes.document_title; }
    void setDocumentTitle(const QString &title) {
        d_func()->attributes.document_title = title;
    }

    QString documentDescription() const { return d_func()->attributes.document_description; }
    void setDocumentDescription(const QString &description) {
        d_func()->attributes.document_description = description;
    }

    QIODevice *outputDevice() const { return d_func()->outputDevice; }
    void setOutputDevice(QIODevice *device) {
        Q_ASSERT(!isActive());
        d_func()->outputDevice = device;
    }

    int resolution() { return d_func()->resolution; }
    void setResolution(int resolution) {
        Q_ASSERT(!isActive());
        d_func()->resolution = resolution;
    }
    void saveLinearGradientBrush(const QGradient *g)
    {
        QTextStream str(&d_func()->defs, QIODevice::Append);
        const QLinearGradient *grad = static_cast<const QLinearGradient*>(g);
        str << QLatin1String("<linearGradient ");
        saveGradientUnits(str, g);
        if (grad) {
            str << QLatin1String("x1=\"") <<grad->start().x()<< QLatin1String("\" ")
                << QLatin1String("y1=\"") <<grad->start().y()<< QLatin1String("\" ")
                << QLatin1String("x2=\"") <<grad->finalStop().x() << QLatin1String("\" ")
                << QLatin1String("y2=\"") <<grad->finalStop().y() << QLatin1String("\" ");
        }

        str << QLatin1String("id=\"") << d_func()->generateGradientName() << QLatin1String("\">\n");
        saveGradientStops(str, g);
        str << QLatin1String("</linearGradient>") <<endl;
    }
    void saveRadialGradientBrush(const QGradient *g)
    {
        QTextStream str(&d_func()->defs, QIODevice::Append);
        const QRadialGradient *grad = static_cast<const QRadialGradient*>(g);
        str << QLatin1String("<radialGradient ");
        saveGradientUnits(str, g);
        if (grad) {
            str << QLatin1String("cx=\"") <<grad->center().x()<< QLatin1String("\" ")
                << QLatin1String("cy=\"") <<grad->center().y()<< QLatin1String("\" ")
                << QLatin1String("r=\"") <<grad->radius() << QLatin1String("\" ")
                << QLatin1String("fx=\"") <<grad->focalPoint().x() << QLatin1String("\" ")
                << QLatin1String("fy=\"") <<grad->focalPoint().y() << QLatin1String("\" ");
        }
        str << QLatin1String("xml:id=\"") <<d_func()->generateGradientName()<< QLatin1String("\">\n");
        saveGradientStops(str, g);
        str << QLatin1String("</radialGradient>") << endl;
    }
    void saveConicalGradientBrush(const QGradient *)
    {
        qWarning("svg's don't support conical gradients!");
    }

    void saveGradientStops(QTextStream &str, const QGradient *g) {
        QGradientStops stops = g->stops();

        if (g->interpolationMode() == QGradient::ColorInterpolation) {
            bool constantAlpha = true;
            int alpha = stops.at(0).second.alpha();
            for (int i = 1; i < stops.size(); ++i)
                constantAlpha &= (stops.at(i).second.alpha() == alpha);

            if (!constantAlpha) {
                const qreal spacing = qreal(0.02);
                QGradientStops newStops;
                QRgb fromColor = qPremultiply(stops.at(0).second.rgba());
                QRgb toColor;
                for (int i = 0; i + 1 < stops.size(); ++i) {
                    int parts = qCeil((stops.at(i + 1).first - stops.at(i).first) / spacing);
                    newStops.append(stops.at(i));
                    toColor = qPremultiply(stops.at(i + 1).second.rgba());

                    if (parts > 1) {
                        qreal step = (stops.at(i + 1).first - stops.at(i).first) / parts;
                        for (int j = 1; j < parts; ++j) {
                            QRgb color = qUnpremultiply(INTERPOLATE_PIXEL_256(fromColor, 256 - 256 * j / parts, toColor, 256 * j / parts));
                            newStops.append(QGradientStop(stops.at(i).first + j * step, QColor::fromRgba(color)));
                        }
                    }
                    fromColor = toColor;
                }
                newStops.append(stops.back());
                stops = newStops;
            }
        }

        foreach(QGradientStop stop, stops) {
            QString color =
                QString::fromLatin1("#%1%2%3")
                .arg(stop.second.red(), 2, 16, QLatin1Char('0'))
                .arg(stop.second.green(), 2, 16, QLatin1Char('0'))
                .arg(stop.second.blue(), 2, 16, QLatin1Char('0'));
            str << QLatin1String("    <stop offset=\"")<< stop.first << QLatin1String("\" ")
                << QLatin1String("stop-color=\"") << color << QLatin1String("\" ")
                << QLatin1String("stop-opacity=\"") << stop.second.alphaF() <<QLatin1String("\" />\n");
        }
    }

    void saveGradientUnits(QTextStream &str, const QGradient *gradient)
    {
        str << QLatin1String("gradientUnits=\"");
        if (gradient && gradient->coordinateMode() == QGradient::ObjectBoundingMode)
            str << QLatin1String("objectBoundingBox");
        else
            str << QLatin1String("userSpaceOnUse");
        str << QLatin1String("\" ");
    }

    void generateQtDefaults()
    {
		fputs("fill=\"none\" ", d_func()->pFile);
		fputs("stroke=\"black\" ", d_func()->pFile);
		fputs("stroke-width=\"1\" ", d_func()->pFile);
		fputs("fill-rule=\"evenodd\" ", d_func()->pFile);
		fputs("stroke-linecap=\"square\" ", d_func()->pFile);
		fputs("stroke-linejoin=\"bevel\" ", d_func()->pFile);
		fputs(">\n", d_func()->pFile);
    }
    inline QTextStream &stream()
    {
        return *d_func()->stream;
    }


    void qpenToSvg(const QPen &spen)
    {
        QString width;

        d_func()->pen = spen;

		QString qstrLatin, wVal;
		std::string latinString;
		const char * latinChar;
		std::string wValString;
		const char * wValChar;

        switch (spen.style()) {
        case Qt::NoPen:
			qstrLatin = QLatin1String("stroke=\"none\" ");
			latinString = qstrLatin.toStdString();
			latinChar = latinString.c_str();
			fputs(latinChar, d_func()->pFile);
            
			d_func()->attributes.stroke = QLatin1String("none");
            d_func()->attributes.strokeOpacity = QString();
            return;
            break;
        case Qt::SolidLine: {
            QString color, colorOpacity;

            translate_color(spen.color(), &color,
                            &colorOpacity);
            d_func()->attributes.stroke = color;
            d_func()->attributes.strokeOpacity = colorOpacity;
			
			qstrLatin = QLatin1String("stroke=\"");
			latinString = qstrLatin.toStdString();
			latinChar = latinString.c_str();
			fputs(latinChar, d_func()->pFile);

			latinString = color.toStdString();
			latinChar = latinString.c_str();
			fputs(latinChar, d_func()->pFile);

			qstrLatin = QLatin1String("\" ");
			latinString = qstrLatin.toStdString();
			latinChar = latinString.c_str();
			fputs(latinChar, d_func()->pFile);

			qstrLatin = QLatin1String("stroke-opacity=\"");
			latinString = qstrLatin.toStdString();
			latinChar = latinString.c_str();
			fputs(latinChar, d_func()->pFile);

			latinString = colorOpacity.toStdString();
			latinChar = latinString.c_str();
			fputs(latinChar, d_func()->pFile);

			QString qstrLatin = QLatin1String("\" ");
			latinString = qstrLatin.toStdString();
			latinChar = latinString.c_str();
			fputs(latinChar, d_func()->pFile);
        }
            break;
        case Qt::DashLine:
        case Qt::DotLine:
        case Qt::DashDotLine:
        case Qt::DashDotDotLine:
        case Qt::CustomDashLine: {
            QString color, colorOpacity, dashPattern, dashOffset;

            qreal penWidth = spen.width() == 0 ? qreal(1) : spen.widthF();

            translate_color(spen.color(), &color, &colorOpacity);
            translate_dashPattern(spen.dashPattern(), penWidth, &dashPattern);

            // SVG uses absolute offset
            dashOffset = QString::number(spen.dashOffset() * penWidth);

            d_func()->attributes.stroke = color;
            d_func()->attributes.strokeOpacity = colorOpacity;
            d_func()->attributes.dashPattern = dashPattern;
            d_func()->attributes.dashOffset = dashOffset;

			qstrLatin = QLatin1String("stroke=\"");
			latinString = qstrLatin.toStdString();
			latinChar = latinString.c_str();
			fputs(latinChar, d_func()->pFile);

			latinString = color.toStdString();
			latinChar = latinString.c_str();
			fputs(latinChar, d_func()->pFile);

			qstrLatin = QLatin1String("\" ");
			latinString = qstrLatin.toStdString();
			latinChar = latinString.c_str();
			fputs(latinChar, d_func()->pFile);

			qstrLatin = QLatin1String("stroke-opacity=\"");
			latinString = qstrLatin.toStdString();
			latinChar = latinString.c_str();
			fputs(latinChar, d_func()->pFile);

			latinString = colorOpacity.toStdString();
			latinChar = latinString.c_str();
			fputs(latinChar, d_func()->pFile);

			qstrLatin = QLatin1String("\" ");
			latinString = qstrLatin.toStdString();
			latinChar = latinString.c_str();
			fputs(latinChar, d_func()->pFile);

			qstrLatin = QLatin1String("stroke-dasharray=\"");
			latinString = qstrLatin.toStdString();
			latinChar = latinString.c_str();
			fputs(latinChar, d_func()->pFile);

			latinString = dashPattern.toStdString();
			latinChar = latinString.c_str();
			fputs(latinChar, d_func()->pFile);

			qstrLatin = QLatin1String("\" ");
			latinString = qstrLatin.toStdString();
			latinChar = latinString.c_str();
			fputs(latinChar, d_func()->pFile);

			qstrLatin = QLatin1String("stroke-dashoffset=\"");
			latinString = qstrLatin.toStdString();
			latinChar = latinString.c_str();
			fputs(latinChar, d_func()->pFile);

			latinString = dashOffset.toStdString();
			latinChar = latinString.c_str();
			fputs(latinChar, d_func()->pFile);

			qstrLatin = QLatin1String("\" ");
			latinString = qstrLatin.toStdString();
			latinChar = latinString.c_str();
			fputs(latinChar, d_func()->pFile);

            break;
        }
        default:
            qWarning("Unsupported pen style");
            break;
        }

		if (spen.widthF() == 0){
			fputs("stroke-width=\"1\" ", d_func()->pFile);
		}
		else{
			fputs("stroke-width=\"", d_func()->pFile);

			wVal = QString::number(spen.widthF());
			wValString = wVal.toStdString();
			wValChar = wValString.c_str();

			fputs(wValChar, d_func()->pFile);

			fputs("\" ", d_func()->pFile);
		}

        switch (spen.capStyle()) {
        case Qt::FlatCap:
			fputs("stroke-linecap=\"butt\" ", d_func()->pFile);
            break;
        case Qt::SquareCap:
			fputs("stroke-linecap=\"square\" ", d_func()->pFile);
            break;
        case Qt::RoundCap:
			fputs("stroke-linecap=\"round\" ", d_func()->pFile);
            break;
        default:
            qWarning("Unhandled cap style");
        }

        switch (spen.joinStyle()) {
        case Qt::SvgMiterJoin:
        case Qt::MiterJoin:
			fputs("stroke-linejoin=\"miter\" ", d_func()->pFile);
			fputs("stroke-miterlimit=\"", d_func()->pFile);\

			wVal = QString::number(spen.miterLimit());
			wValString = wVal.toStdString();
			wValChar = wValString.c_str();

			fputs(wValChar, d_func()->pFile);
			fputs("\" ", d_func()->pFile);
            break;
        case Qt::BevelJoin:
			fputs("stroke-linejoin=\"bevel\" ", d_func()->pFile);
            break;
        case Qt::RoundJoin:
			fputs("stroke-linejoin=\"round\" ", d_func()->pFile);
            break;
        default:
            qWarning("Unhandled join style");
        }
    }
    void qbrushToSvg(const QBrush &sbrush)
    {
        d_func()->brush = sbrush;

		std::string colorString;
		const char * colorChar;

		QString qstrLatin;
		std::string latinString;
		const char * latinChar;

        switch (sbrush.style()) {
        case Qt::SolidPattern: {
            QString color, colorOpacity;
            translate_color(sbrush.color(), &color, &colorOpacity);
			colorString = color.toStdString();
			colorChar = colorString.c_str();

			fputs("fill=\"", d_func()->pFile);
			fputs(colorChar, d_func()->pFile);
			fputs("\" ", d_func()->pFile);
			fputs("fill-opacity=\"", d_func()->pFile);

			colorString = colorOpacity.toStdString();
			colorChar = colorString.c_str();

			fputs(colorChar, d_func()->pFile);
			fputs("\" ", d_func()->pFile);

            d_func()->attributes.fill = color;
            d_func()->attributes.fillOpacity = colorOpacity;
        }
            break;
        case Qt::LinearGradientPattern:
            saveLinearGradientBrush(sbrush.gradient());
            d_func()->attributes.fill = QString::fromLatin1("url(#%1)").arg(d_func()->currentGradientName);
            d_func()->attributes.fillOpacity = QString();

			colorString = d_func()->currentGradientName.toStdString();
			colorChar = colorString.c_str();

			qstrLatin = QLatin1String("fill=\"url(#");
			latinString = qstrLatin.toStdString();
			latinChar = latinString.c_str();

			fputs(latinChar, d_func()->pFile);
			fputs(colorChar, d_func()->pFile);

			qstrLatin = QLatin1String(")\" ");
			latinString = qstrLatin.toStdString();
			latinChar = latinString.c_str();

			fputs(latinChar, d_func()->pFile);

            break;
        case Qt::RadialGradientPattern:
            saveRadialGradientBrush(sbrush.gradient());
            d_func()->attributes.fill = QString::fromLatin1("url(#%1)").arg(d_func()->currentGradientName);
            d_func()->attributes.fillOpacity = QString();
			
			colorString = d_func()->currentGradientName.toStdString();
			colorChar = colorString.c_str();

			qstrLatin = QLatin1String("fill=\"url(#");
			latinString = qstrLatin.toStdString();
			latinChar = latinString.c_str();
			
			fputs(latinChar, d_func()->pFile);
			fputs(colorChar, d_func()->pFile);

			qstrLatin = QLatin1String(")\" ");
			latinString = qstrLatin.toStdString();
			latinChar = latinString.c_str();

			fputs(latinChar, d_func()->pFile);

			break;
        case Qt::ConicalGradientPattern:
            saveConicalGradientBrush(sbrush.gradient());
            d_func()->attributes.fill = QString::fromLatin1("url(#%1)").arg(d_func()->currentGradientName);
            d_func()->attributes.fillOpacity = QString();

			colorString = d_func()->currentGradientName.toStdString();
			colorChar = colorString.c_str();

			qstrLatin = QLatin1String("fill=\"url(#");
			latinString = qstrLatin.toStdString();
			latinChar = latinString.c_str();

			fputs(latinChar, d_func()->pFile);
			fputs(colorChar, d_func()->pFile);

			qstrLatin = QLatin1String(")\" ");
			latinString = qstrLatin.toStdString();
			latinChar = latinString.c_str();

			fputs(latinChar, d_func()->pFile);
            
			break;
        case Qt::NoBrush:
			qstrLatin = QLatin1String("fill=\"none\" ");
			latinString = qstrLatin.toStdString();
			latinChar = latinString.c_str();

			fputs(latinChar, d_func()->pFile);
            d_func()->attributes.fill = QLatin1String("none");
            d_func()->attributes.fillOpacity = QString();
            return;
            break;
        default:
           break;
        }
    }
    void qfontToSvg(const QFont &sfont)
    {
        Q_D(QSvgPaintEngine);

        d->font = sfont;

        if (d->font.pixelSize() == -1)
            d->attributes.font_size = QString::number(d->font.pointSizeF() * d->resolution / 72);
        else
            d->attributes.font_size = QString::number(d->font.pixelSize());

        int svgWeight = d->font.weight();
        switch (svgWeight) {
        case QFont::Light:
            svgWeight = 100;
            break;
        case QFont::Normal:
            svgWeight = 400;
            break;
        case QFont::Bold:
            svgWeight = 700;
            break;
        default:
            svgWeight *= 10;
        }

        d->attributes.font_weight = QString::number(svgWeight);
        d->attributes.font_family = d->font.family();
        d->attributes.font_style = d->font.italic() ? QLatin1String("italic") : QLatin1String("normal");

		fputs("font-family=\"", d->pFile);
		
		std::string fontFamily = d->attributes.font_family.toStdString();
		const char * fontFamilyChar = fontFamily.c_str();

		fputs(fontFamilyChar, d->pFile);
		fputs("\" ", d->pFile);
		fputs("font-size=\"", d->pFile);

		fontFamily = d->attributes.font_size.toStdString();
		fontFamilyChar = fontFamily.c_str();
		
		fputs(fontFamilyChar, d->pFile);
		fputs("\" ", d->pFile);
		fputs("font-weight=\"", d->pFile);

		fontFamily = d->attributes.font_weight.toStdString();
		fontFamilyChar = fontFamily.c_str();

		fputs(fontFamilyChar, d->pFile);
		fputs("\" ", d->pFile);
		fputs("font-style=\"", d->pFile);

		fontFamily = d->attributes.font_style.toStdString();
		fontFamilyChar = fontFamily.c_str();

		fputs(fontFamilyChar, d->pFile);
		fputs("\" ", d->pFile);
		fputs("\n", d->pFile);
    }
};

class QSvgGeneratorPrivate
{
public:
    QSvgPaintEngine *engine;

    uint owns_iodevice : 1;
    QString fileName;
};

/*!
    \class QSvgGenerator
    \ingroup painting
    \inmodule QtSvg
    \since 4.3
    \brief The QSvgGenerator class provides a paint device that is used to create SVG drawings.
    \reentrant

    This paint device represents a Scalable Vector Graphics (SVG) drawing. Like QPrinter, it is
    designed as a write-only device that generates output in a specific format.

    To write an SVG file, you first need to configure the output by setting the \l fileName
    or \l outputDevice properties. It is usually necessary to specify the size of the drawing
    by setting the \l size property, and in some cases where the drawing will be included in
    another, the \l viewBox property also needs to be set.

    \snippet svggenerator/window.cpp configure SVG generator

    Other meta-data can be specified by setting the \a title, \a description and \a resolution
    properties.

    As with other QPaintDevice subclasses, a QPainter object is used to paint onto an instance
    of this class:

    \snippet svggenerator/window.cpp begin painting
    \dots
    \snippet svggenerator/window.cpp end painting

    Painting is performed in the same way as for any other paint device. However,
    it is necessary to use the QPainter::begin() and \l{QPainter::}{end()} to
    explicitly begin and end painting on the device.

    The \l{SVG Generator Example} shows how the same painting commands can be used
    for painting a widget and writing an SVG file.

    \sa QSvgRenderer, QSvgWidget, {Qt SVG C++ Classes}
*/

/*!
    Constructs a new generator.
*/
QSvgGenerator::QSvgGenerator()
    : d_ptr(new QSvgGeneratorPrivate)
{
    Q_D(QSvgGenerator);

    d->engine = new QSvgPaintEngine;
    d->owns_iodevice = false;
}

/*!
    Destroys the generator.
*/
QSvgGenerator::~QSvgGenerator()
{
    Q_D(QSvgGenerator);
    if (d->owns_iodevice)
        delete d->engine->outputDevice();
    delete d->engine;
}

/*!
    \property QSvgGenerator::title
    \brief the title of the generated SVG drawing
    \since 4.5
    \sa description
*/
QString QSvgGenerator::title() const
{
    Q_D(const QSvgGenerator);

    return d->engine->documentTitle();
}

void QSvgGenerator::setTitle(const QString &title)
{
    Q_D(QSvgGenerator);

    d->engine->setDocumentTitle(title);
}

/*!
    \property QSvgGenerator::description
    \brief the description of the generated SVG drawing
    \since 4.5
    \sa title
*/
QString QSvgGenerator::description() const
{
    Q_D(const QSvgGenerator);

    return d->engine->documentDescription();
}

void QSvgGenerator::setDescription(const QString &description)
{
    Q_D(QSvgGenerator);

    d->engine->setDocumentDescription(description);
}

/*!
    \property QSvgGenerator::size
    \brief the size of the generated SVG drawing
    \since 4.5

    By default this property is set to \c{QSize(-1, -1)}, which
    indicates that the generator should not output the width and
    height attributes of the \c<svg> element.

    \note It is not possible to change this property while a
    QPainter is active on the generator.

    \sa viewBox, resolution
*/
QSize QSvgGenerator::size() const
{
    Q_D(const QSvgGenerator);
    return d->engine->size();
}

void QSvgGenerator::setSize(const QSize &size)
{
    Q_D(QSvgGenerator);
    if (d->engine->isActive()) {
        qWarning("QSvgGenerator::setSize(), cannot set size while SVG is being generated");
        return;
    }
    d->engine->setSize(size);
}

/*!
    \property QSvgGenerator::viewBox
    \brief the viewBox of the generated SVG drawing
    \since 4.5

    By default this property is set to \c{QRect(0, 0, -1, -1)}, which
    indicates that the generator should not output the viewBox attribute
    of the \c<svg> element.

    \note It is not possible to change this property while a
    QPainter is active on the generator.

    \sa viewBox(), size, resolution
*/
QRectF QSvgGenerator::viewBoxF() const
{
    Q_D(const QSvgGenerator);
    return d->engine->viewBox();
}

/*!
    \since 4.5

    Returns viewBoxF().toRect().

    \sa viewBoxF()
*/
QRect QSvgGenerator::viewBox() const
{
    Q_D(const QSvgGenerator);
    return d->engine->viewBox().toRect();
}

void QSvgGenerator::setViewBox(const QRectF &viewBox)
{
    Q_D(QSvgGenerator);
    if (d->engine->isActive()) {
        qWarning("QSvgGenerator::setViewBox(), cannot set viewBox while SVG is being generated");
        return;
    }
    d->engine->setViewBox(viewBox);
}

void QSvgGenerator::setViewBox(const QRect &viewBox)
{
    setViewBox(QRectF(viewBox));
}

/*!
    \property QSvgGenerator::fileName
    \brief the target filename for the generated SVG drawing
    \since 4.5

    \sa outputDevice
*/
QString QSvgGenerator::fileName() const
{
    Q_D(const QSvgGenerator);
    return d->fileName;
}

QString tempFileName;

void QSvgGenerator::setFileName(const QString &fileName)
{
    Q_D(QSvgGenerator);
    if (d->engine->isActive()) {
        qWarning("QSvgGenerator::setFileName(), cannot set file name while SVG is being generated");
        return;
    }

    if (d->owns_iodevice)
        delete d->engine->outputDevice();

    d->owns_iodevice = true;
	tempFileName = fileName;
    d->fileName = fileName;
    QFile *file = new QFile(fileName);
    d->engine->setOutputDevice(file);
}

/*!
    \property QSvgGenerator::outputDevice
    \brief the output device for the generated SVG drawing
    \since 4.5

    If both output device and file name are specified, the output device
    will have precedence.

    \sa fileName
*/
QIODevice *QSvgGenerator::outputDevice() const
{
    Q_D(const QSvgGenerator);
    return d->engine->outputDevice();
}

void QSvgGenerator::setOutputDevice(QIODevice *outputDevice)
{
    Q_D(QSvgGenerator);
    if (d->engine->isActive()) {
        qWarning("QSvgGenerator::setOutputDevice(), cannot set output device while SVG is being generated");
        return;
    }
    d->owns_iodevice = false;
    d->engine->setOutputDevice(outputDevice);
    d->fileName = QString();
}

/*!
    \property QSvgGenerator::resolution
    \brief the resolution of the generated output
    \since 4.5

    The resolution is specified in dots per inch, and is used to
    calculate the physical size of an SVG drawing.

    \sa size, viewBox
*/
int QSvgGenerator::resolution() const
{
    Q_D(const QSvgGenerator);
    return d->engine->resolution();
}

void QSvgGenerator::setResolution(int dpi)
{
    Q_D(QSvgGenerator);
    d->engine->setResolution(dpi);
}

/*!
    Returns the paint engine used to render graphics to be converted to SVG
    format information.
*/
QPaintEngine *QSvgGenerator::paintEngine() const
{
    Q_D(const QSvgGenerator);
    return d->engine;
}

/*!
    \reimp
*/
int QSvgGenerator::metric(QPaintDevice::PaintDeviceMetric metric) const
{
    Q_D(const QSvgGenerator);
    switch (metric) {
    case QPaintDevice::PdmDepth:
        return 32;
    case QPaintDevice::PdmWidth:
        return d->engine->size().width();
    case QPaintDevice::PdmHeight:
        return d->engine->size().height();
    case QPaintDevice::PdmDpiX:
        return d->engine->resolution();
    case QPaintDevice::PdmDpiY:
        return d->engine->resolution();
    case QPaintDevice::PdmHeightMM:
        return qRound(d->engine->size().height() * 25.4 / d->engine->resolution());
    case QPaintDevice::PdmWidthMM:
        return qRound(d->engine->size().width() * 25.4 / d->engine->resolution());
    case QPaintDevice::PdmNumColors:
        return 0xffffffff;
    case QPaintDevice::PdmPhysicalDpiX:
        return d->engine->resolution();
    case QPaintDevice::PdmPhysicalDpiY:
        return d->engine->resolution();
    case QPaintDevice::PdmDevicePixelRatio:
        return 1;
    default:
        qWarning("QSvgGenerator::metric(), unhandled metric %d\n", metric);
        break;
    }
    return 0;
}

/*****************************************************************************
 * class QSvgPaintEngine
 */

bool QSvgPaintEngine::begin(QPaintDevice *)
{

	QString qtempString;
	std::string stdtempString;
	const char * tempChar;

    Q_D(QSvgPaintEngine);
    if (!d->outputDevice) {
        qWarning("QSvgPaintEngine::begin(), no output device");
        return false;
    }

    if (!d->outputDevice->isOpen()) {
        if (!d->outputDevice->open(QIODevice::WriteOnly | QIODevice::Text)) {
            qWarning("QSvgPaintEngine::begin(), could not open output device: '%s'",
                     qPrintable(d->outputDevice->errorString()));
            return false;
        }
    } else if (!d->outputDevice->isWritable()) {
        qWarning("QSvgPaintEngine::begin(), could not write to read-only output device: '%s'",
                 qPrintable(d->outputDevice->errorString()));
        return false;
    }
	//QSvgGenerator generator;
	stdtempString = tempFileName.toStdString();
	tempChar = stdtempString.c_str();
	d->pFile = fopen(tempChar, "a+");
    d->stream = new QTextStream(&d->header);

	float pixelToMM = 0.2645833333;
    // stream out the header...
	fputs("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n<svg",d->pFile);
    if (d->size.isValid()) {
		qreal wmm = d->size.width() * pixelToMM;
		qreal hmm = d->size.height() * pixelToMM;

		qtempString = QString::number(wmm);
		stdtempString = qtempString.toStdString();
		tempChar = stdtempString.c_str();

		fputs(" width=\"", d->pFile);
		fputs(tempChar, d->pFile);

		qtempString = QString::number(hmm);
		stdtempString = qtempString.toStdString();
		tempChar = stdtempString.c_str();

		fputs("mm\" height=\"", d->pFile);
		fputs(tempChar, d->pFile);
		fputs("mm\"\n", d->pFile);
    }

	fputs(" xmlns=\"http://www.w3.org/2000/svg\"", d->pFile);
	fputs(" xmlns:xlink=\"http://www.w3.org/1999/xlink\" ", d->pFile);
	fputs(" version=\"1.2\" baseProfile=\"tiny\">\n", d->pFile);

    if (!d->attributes.document_title.isEmpty()) {
		stdtempString = d->attributes.document_title.toStdString();
		tempChar = stdtempString.c_str();
		fputs("<title>", d->pFile);
		fputs(tempChar, d->pFile);
		fputs("</title>\n", d->pFile);
    }

    if (!d->attributes.document_description.isEmpty()) {
		stdtempString = d->attributes.document_description.toStdString();
		tempChar = stdtempString.c_str();
		fputs("<desc>", d->pFile);
		fputs(tempChar, d->pFile);
		fputs("</desc>\n", d->pFile);
    }

	fputs("<defs>\n", d->pFile);
	fputs("</defs>\n", d->pFile);
    // Start the initial graphics state...
	fputs("<g ", d->pFile);
    generateQtDefaults();
	fputs("\n", d->pFile);
    return true;
}

bool QSvgPaintEngine::end()
{
    Q_D(QSvgPaintEngine);

	std::string stdtempString;
	const char * tempChar;

    d->stream->setString(&d->defs);

	stdtempString = d->header.toStdString();
	tempChar = stdtempString.c_str();
	fputs(tempChar, d->pFile);

	stdtempString = d->defs.toStdString();
	tempChar = stdtempString.c_str();
	fputs(tempChar, d->pFile);

	stdtempString = d->body.toStdString();
	tempChar = stdtempString.c_str();
	fputs(tempChar, d->pFile);

	if (d->afterFirstUpdate){
		fputs("</g>\n", d->pFile);
	}
	fputs("</g>\n", d->pFile);
	fputs("</svg>\n", d->pFile);

    delete d->stream;

	if (d->pFile != NULL)
	{
		fclose(d->pFile);
	}

    return true;
}

void QSvgPaintEngine::GraphicsState()
{
	Q_D(QSvgPaintEngine);

	QString qtempString;
	std::string stdtempString;
	const char * tempChar;

	QPaintEngine::DirtyFlags flags = state->state();

	// always stream full gstate, which is not required, but...
	flags |= QPaintEngine::AllDirty;

	// close old state and start a new one...
	if (d->afterFirstUpdate)
		fputs("</g>\n\n", d->pFile);

	fputs("<g ", d->pFile);

	if (flags & QPaintEngine::DirtyBrush) {
		qbrushToSvg(state->brush());
	}

	if (flags & QPaintEngine::DirtyPen) {
		qpenToSvg(state->pen());
	}

	if (flags & QPaintEngine::DirtyTransform) {
		d->matrix = state->matrix();

		fputs("transform=\"matrix(", d->pFile);

		qtempString = QString::number(d->matrix.m11());
		stdtempString = qtempString.toStdString();
		tempChar = stdtempString.c_str();

		fputs(tempChar, d->pFile);
		fputs(",", d->pFile);

		qtempString = QString::number(d->matrix.m12());
		stdtempString = qtempString.toStdString();
		tempChar = stdtempString.c_str();

		fputs(tempChar, d->pFile);
		fputs(",", d->pFile);

		qtempString = QString::number(d->matrix.m21());
		stdtempString = qtempString.toStdString();
		tempChar = stdtempString.c_str();
		fputs(tempChar, d->pFile);

		fputs(",", d->pFile);
		qtempString = QString::number(d->matrix.m22());
		stdtempString = qtempString.toStdString();
		tempChar = stdtempString.c_str();
		fputs(tempChar, d->pFile);

		fputs(",", d->pFile);
		qtempString = QString::number(d->matrix.dx());
		stdtempString = qtempString.toStdString();
		tempChar = stdtempString.c_str();
		fputs(tempChar, d->pFile);

		fputs(",", d->pFile);
		qtempString = QString::number(d->matrix.dy());
		stdtempString = qtempString.toStdString();
		tempChar = stdtempString.c_str();
		fputs(tempChar, d->pFile);

		fputs(")\"\n", d->pFile);
	}

	if (flags & QPaintEngine::DirtyFont) {
		qfontToSvg(state->font());
	}

	if (flags & QPaintEngine::DirtyOpacity) {
		if (!qFuzzyIsNull(state->opacity() - 1)){
			fputs("opacity=\"", d->pFile);

			qtempString = QString::number(state->opacity());
			stdtempString = qtempString.toStdString();
			tempChar = stdtempString.c_str();

			fputs(tempChar, d->pFile);
			fputs("\" ", d->pFile);
		}
	}
	fputs(">\n", d->pFile);
	d->afterFirstUpdate = true;
}

void QSvgPaintEngine::drawPixmap(const QRectF &r, const QPixmap &pm,
                                 const QRectF &sr)
{
    QRect sourceRect = sr.toRect();
    QPixmap pixmap = sourceRect != pm.rect() ? pm.copy(sourceRect) : pm;
    drawImage(r, pixmap.toImage(), sr);
}

void QSvgPaintEngine::drawImage(const QRectF &r, const QImage &image,
                                const QRectF &sr,
                                Qt::ImageConversionFlags flags)
{
    //Q_D(QSvgPaintEngine);

	GraphicsState();
	QString qtempString;
	std::string stdtempString;
	const char * tempChar;

    Q_UNUSED(sr);
    Q_UNUSED(flags);

	qtempString = QString::number(r.x());
	stdtempString = qtempString.toStdString();
	tempChar = stdtempString.c_str();

	fputs("<image ", d_func()->pFile);
	fputs("x=\"", d_func()->pFile);
	fputs(tempChar, d_func()->pFile);

	qtempString = QString::number(r.y());
	stdtempString = qtempString.toStdString();
	tempChar = stdtempString.c_str();

	fputs("\" ", d_func()->pFile);
	fputs("y=\"", d_func()->pFile);
	fputs(tempChar, d_func()->pFile);

	qtempString = QString::number(r.width());
	stdtempString = qtempString.toStdString();
	tempChar = stdtempString.c_str();

	fputs("\" ", d_func()->pFile);
	fputs("width=\"", d_func()->pFile);
	fputs(tempChar, d_func()->pFile);

	qtempString = QString::number(r.height());
	stdtempString = qtempString.toStdString();
	tempChar = stdtempString.c_str();

	fputs("\" ", d_func()->pFile);
	fputs("height=\"", d_func()->pFile);
	fputs(tempChar, d_func()->pFile);

	fputs("\" ", d_func()->pFile);
	fputs("preserveAspectRatio=\"none\" ", d_func()->pFile);

    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QBuffer::ReadWrite);
    image.save(&buffer, "PNG");
    buffer.close();

	fputs("xlink:href=\"data:image/png;base64,", d_func()->pFile);
	fputs(data.toBase64(), d_func()->pFile);
	fputs("\" />\n", d_func()->pFile);
}

void QSvgPaintEngine::updateState(const QPaintEngineState &state)
{
    Q_D(QSvgPaintEngine);
	if (!d->afterFirstUpdate)
		GraphicsState();
}

void QSvgPaintEngine::drawEllipse(const QRectF &r)
{
    Q_D(QSvgPaintEngine);

	GraphicsState();
	QString qtempString;
	std::string stdtempString;
	const char * tempChar;

    const bool isCircle = r.width() == r.height();

	fputs("<", d->pFile);
	fputs((isCircle ? "circle" : "ellipse"), d->pFile);

	if (state->pen().isCosmetic()){
		fputs(" vector-effect=\"non-scaling-stroke\"", d->pFile);
	}
    const QPointF c = r.center();

	qtempString = QString::number(c.x());
	stdtempString = qtempString.toStdString();
	tempChar = stdtempString.c_str();
	
	fputs(" cx=\"", d->pFile);
	fputs(tempChar, d->pFile);

	qtempString = QString::number(c.y());
	stdtempString = qtempString.toStdString();
	tempChar = stdtempString.c_str();

	fputs("\" cy=\"", d->pFile);
	fputs(tempChar, d->pFile);

	if (isCircle){
		
		qtempString = QString::number((r.width() / qreal(2.0)));
		stdtempString = qtempString.toStdString();
		tempChar = stdtempString.c_str();
		
		fputs("\" r=\"", d->pFile);
		fputs(tempChar, d->pFile);
	}
	else{
		qtempString = QString::number((r.width() / qreal(2.0)));
		stdtempString = qtempString.toStdString();
		tempChar = stdtempString.c_str();

		fputs("\" rx=\"", d->pFile);
		fputs(tempChar, d->pFile);

		qtempString = QString::number((r.height() / qreal(2.0)));
		stdtempString = qtempString.toStdString();
		tempChar = stdtempString.c_str();

		fputs("\" ry=\"", d->pFile);
		fputs(tempChar, d->pFile);
	}
	fputs("\"/>", d->pFile);
	fputs("\n", d->pFile);
}

void QSvgPaintEngine::drawPath(const QPainterPath &p)
{
    Q_D(QSvgPaintEngine);

	GraphicsState();
	QString qtempString;
	std::string stdtempString;
	const char * tempChar;

	fputs("<path vector-effect=\"", d->pFile);
	fputs((state->pen().isCosmetic() ? "non-scaling-stroke" : "none"), d->pFile);
	fputs("\" fill-rule=\"", d->pFile);
	fputs((p.fillRule() == Qt::OddEvenFill ? "evenodd" : "nonzero"), d->pFile);
	fputs("\" d=\"", d->pFile);

    for (int i=0; i<p.elementCount(); ++i) {
        const QPainterPath::Element &e = p.elementAt(i);
        switch (e.type) {
        case QPainterPath::MoveToElement:
			
			qtempString = QString::number(e.x);
			stdtempString = qtempString.toStdString();
			tempChar = stdtempString.c_str();

			fputs("M", d->pFile);
			fputs(tempChar, d->pFile);

			qtempString = QString::number(e.y);
			stdtempString = qtempString.toStdString();
			tempChar = stdtempString.c_str();

			fputs(",", d->pFile);
			fputs(tempChar, d->pFile);
            break;
        case QPainterPath::LineToElement:

			qtempString = QString::number(e.x);
			stdtempString = qtempString.toStdString();
			tempChar = stdtempString.c_str();

			fputs("L", d->pFile);
			fputs(tempChar, d->pFile);

			qtempString = QString::number(e.y);
			stdtempString = qtempString.toStdString();
			tempChar = stdtempString.c_str();

			fputs(",", d->pFile);
			fputs(tempChar, d->pFile);
            break;
        case QPainterPath::CurveToElement:

			qtempString = QString::number(e.x);
			stdtempString = qtempString.toStdString();
			tempChar = stdtempString.c_str();

			fputs("C", d->pFile);
			fputs(tempChar, d->pFile);

			qtempString = QString::number(e.y);
			stdtempString = qtempString.toStdString();
			tempChar = stdtempString.c_str();

			fputs(",", d->pFile);
			fputs(tempChar, d->pFile);
            ++i;
            while (i < p.elementCount()) {
                const QPainterPath::Element &e = p.elementAt(i);
                if (e.type != QPainterPath::CurveToDataElement) {
                    --i;
                    break;
				}
				else{
					fputs(" ", d->pFile);
				}

				qtempString = QString::number(e.x);
				stdtempString = qtempString.toStdString();
				tempChar = stdtempString.c_str();

				fputs(tempChar, d->pFile);
				fputs(",", d->pFile);

				qtempString = QString::number(e.y);
				stdtempString = qtempString.toStdString();
				tempChar = stdtempString.c_str();

				fputs(tempChar, d->pFile);
                ++i;
            }
            break;
        default:
            break;
        }
        if (i != p.elementCount() - 1) {
			fputs(" ", d->pFile);
        }
    }

	fputs("\"/>", d->pFile);
	fputs("\n", d->pFile);
}

void QSvgPaintEngine::drawPolygon(const QPointF *points, int pointCount,
                                  PolygonDrawMode mode)
{
    Q_ASSERT(pointCount >= 2);

    //Q_D(QSvgPaintEngine);

	GraphicsState();
	QString qtempString;
	std::string stdtempString;
	const char * tempChar;

    QPainterPath path(points[0]);
    for (int i=1; i<pointCount; ++i)
        path.lineTo(points[i]);

    if (mode == PolylineMode) {

		fputs("<polyline fill=\"none\" vector-effect=\"", d_func()->pFile);
		fputs((state->pen().isCosmetic() ? "non-scaling-stroke" : "none"), d_func()->pFile);
		fputs("\" points=\"", d_func()->pFile);

        for (int i = 0; i < pointCount; ++i) {
            const QPointF &pt = points[i];

			qtempString = QString::number(pt.x());
			stdtempString = qtempString.toStdString();
			tempChar = stdtempString.c_str();

			fputs(tempChar, d_func()->pFile);
			fputs(",", d_func()->pFile);

			qtempString = QString::number(pt.y());
			stdtempString = qtempString.toStdString();
			tempChar = stdtempString.c_str();

			fputs(tempChar, d_func()->pFile);
			fputs(" ", d_func()->pFile);
        }
		fputs("\" />", d_func()->pFile);
    } else {
        path.closeSubpath();
        drawPath(path);
    }
}

void QSvgPaintEngine::drawRects(const QRectF *rects, int rectCount)
{
    Q_D(QSvgPaintEngine);

	GraphicsState();
	QString qtempString;
	std::string stdtempString;
	const char * tempChar;

    for (int i=0; i < rectCount; ++i) {
        const QRectF &rect = rects[i];

		fputs("<rect", d->pFile);
		if (state->pen().isCosmetic())
			fputs(" vector-effect=\"non-scaling-stroke\"", d->pFile);

		fputs(" x=\"", d->pFile);

		qtempString = QString::number(rect.x());
		stdtempString = qtempString.toStdString();
		tempChar = stdtempString.c_str();

		fputs(tempChar, d->pFile);
		fputs("\" y=\"", d->pFile);

		qtempString = QString::number(rect.y());
		stdtempString = qtempString.toStdString();
		tempChar = stdtempString.c_str();

		fputs(tempChar, d->pFile);
		fputs("\" width=\"", d->pFile);

		qtempString = QString::number(rect.width());
		stdtempString = qtempString.toStdString();
		tempChar = stdtempString.c_str();

		fputs(tempChar, d->pFile);
		fputs("\" height=\"", d->pFile);

		qtempString = QString::number(rect.height());
		stdtempString = qtempString.toStdString();
		tempChar = stdtempString.c_str();

		fputs(tempChar, d->pFile);
		fputs("\"/>\n", d->pFile);
    }
}

void QSvgPaintEngine::drawTextItem(const QPointF &pt, const QTextItem &textItem)
{
    Q_D(QSvgPaintEngine);

	GraphicsState();
    if (d->pen.style() == Qt::NoPen)
        return;

    const QTextItemInt &ti = static_cast<const QTextItemInt &>(textItem);
    if (ti.chars == 0)
        QPaintEngine::drawTextItem(pt, ti); // Draw as path
    QString s = QString::fromRawData(ti.chars, ti.num_chars);

	QString xVal = QString::number(pt.x());
	std::string xValString = xVal.toStdString();
	const char * xValChar = xValString.c_str();

	QString yVal = QString::number(pt.y());
	std::string yValString = yVal.toStdString();
	const char * yValChar = yValString.c_str();

	std::string strokeOpacity = d->attributes.strokeOpacity.toStdString();
	const char * strokeOpacityChar = strokeOpacity.c_str();

	std::string stroke = d->attributes.stroke.toStdString();
	const char * strokeChar = stroke.c_str();

	fputs("<text ", d->pFile);
	fputs("fill=\"", d->pFile);
	fputs(strokeChar, d->pFile);
	fputs("\" ", d->pFile);
	fputs("fill-opacity=\"", d->pFile);
	fputs(strokeOpacityChar, d->pFile);
	fputs("\" ", d->pFile);
	fputs("stroke=\"none\" ", d->pFile);
	fputs("xml:space=\"preserve\" ", d->pFile);
	fputs("x=\"", d->pFile);
	fputs(xValChar, d->pFile);
	fputs("\" y=\"", d->pFile);
	fputs(yValChar, d->pFile);
	fputs("\" ", d->pFile);

    qfontToSvg(textItem.font());

	std::string stdHtmlElapsed = s.toHtmlEscaped().toStdString();
	const char * htmlElapsed = stdHtmlElapsed.c_str();

	fputs(" >", d->pFile);
	fputs(htmlElapsed, d->pFile);
	fputs("</text>\n", d->pFile);
}

QT_END_NAMESPACE

#endif // QT_NO_SVGGENERATOR
