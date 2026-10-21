// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qlitehtmlwidget.h"

#include "container_qpainter.h"

#include <QApplication>
#include <QDebug>
#include <QHelpEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStyle>
#include <QToolTip>

#if QLITEHTML_HAS_QPRINTER
#include <QtPrintSupport/qprinter.h>
#endif

QT_BEGIN_NAMESPACE

const int kScrollBarStep = 40;

namespace {

// Layout depends on the viewport height when the document uses percentage
// heights, percentage top/bottom offsets, CSS variables holding either, or
// fixed positioning. Strip code samples first: documentation often shows these
// declarations as text and must not disable the fast path.
bool ScanViewportDependent(const QString &html)
{
    static const QRegularExpression kPre(QStringLiteral("<pre\\b[^>]*>.*?</pre>"),
                                         QRegularExpression::CaseInsensitiveOption
                                             | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression
        kLineNumberCode(QStringLiteral("<table\\b[^>]*class=\"md-code\"[^>]*>.*?</table>"),
                        QRegularExpression::CaseInsensitiveOption
                            | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression kInlineCode(QStringLiteral("<code\\b[^>]*>.*?</code>"),
                                                QRegularExpression::CaseInsensitiveOption
                                                    | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression kCssComment(QStringLiteral("/\\*.*?\\*/"),
                                                QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression kHtmlComment(QStringLiteral("<!--.*?-->"),
                                                 QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression
        kHeightPercentage(QStringLiteral(
                              "(?<![\\w-])(?:min-|max-)?height\\s*[:=]\\s*[\"']?\\s*[0-9.]+%"),
                          QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression
        kTopBottomPercentage(QStringLiteral("(?<![\\w-])(?:top|bottom)\\s*:\\s*[+-]?[0-9.]+%"),
                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kVar(QStringLiteral("var\\s*\\("),
                                         QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kFixed(QStringLiteral("position\\s*:\\s*fixed"),
                                           QRegularExpression::CaseInsensitiveOption);

    QString text = html;
    text.remove(kPre);
    text.remove(kLineNumberCode);
    text.remove(kInlineCode);
    text.remove(kCssComment);
    text.remove(kHtmlComment);
    return text.contains(kHeightPercentage) || text.contains(kTopBottomPercentage)
           || text.contains(kVar) || text.contains(kFixed);
}

} // namespace

// copied from include/litehtml/master_css.h
const char master_css[] = R"##(
html {
    display: block;
    position: relative;
}

head {
    display: none
}

meta {
    display: none
}

title {
    display: none
}

link {
    display: none
}

style {
    display: none
}

script {
    display: none
}

body {
    display:block;
    margin:8px;
}

p {
    display:block;
    margin-top:1em;
    margin-bottom:1em;
}

b, strong {
    display:inline;
    font-weight:bold;
}

i, em, cite {
    display:inline;
    font-style:italic;
}

ins, u {
    text-decoration:underline
}

del, s, strike {
    text-decoration:line-through
}

center
{
    text-align:center;
    display:block;
}

a:link
{
    text-decoration: underline;
    color: #00f;
    cursor: pointer;
}

h1, h2, h3, h4, h5, h6, div {
    display:block;
}

h1 {
    font-weight:bold;
    margin-top:0.67em;
    margin-bottom:0.67em;
    font-size: 2em;
}

h2 {
    font-weight:bold;
    margin-top:0.83em;
    margin-bottom:0.83em;
    font-size: 1.5em;
}

h3 {
    font-weight:bold;
    margin-top:1em;
    margin-bottom:1em;
    font-size:1.17em;
}

h4 {
    font-weight:bold;
    margin-top:1.33em;
    margin-bottom:1.33em
}

h5 {
    font-weight:bold;
    margin-top:1.67em;
    margin-bottom:1.67em;
    font-size:.83em;
}

h6 {
    font-weight:bold;
    margin-top:2.33em;
    margin-bottom:2.33em;
    font-size:.67em;
}

br {
    display:inline-block;
}

br[clear="all"]
{
    clear:both;
}

br[clear="left"]
{
    clear:left;
}

br[clear="right"]
{
    clear:right;
}

span {
    display:inline
}

img {
    display: inline-block;
}

img[align="right"]
{
    float: right;
}

img[align="left"]
{
    float: left;
}

hr {
    display: block;
    margin-top: 0.5em;
    margin-bottom: 0.5em;
    margin-left: auto;
    margin-right: auto;
    border-style: inset;
    border-width: 1px
}


/***************** TABLES ********************/

table {
    display: table;
    border-collapse: separate;
    border-spacing: 2px;
    border-top-color:gray;
    border-left-color:gray;
    border-bottom-color:black;
    border-right-color:black;
    font-size: medium;
    font-weight: normal;
    font-style: normal;
}

tbody, tfoot, thead {
    display:table-row-group;
    vertical-align:middle;
}

tr {
    display: table-row;
    vertical-align: inherit;
    border-color: inherit;
}

td, th {
    display: table-cell;
    vertical-align: inherit;
    border-width:1px;
    padding:1px;
}

th {
    font-weight: bold;
}

table[border] {
    border-style:solid;
}

table[border|=0] {
    border-style:none;
}

table[border] td, table[border] th {
    border-style:solid;
    border-top-color:black;
    border-left-color:black;
    border-bottom-color:gray;
    border-right-color:gray;
}

table[border|=0] td, table[border|=0] th {
    border-style:none;
}

table[align=left] {
   float: left;
}

table[align=right] {
   float: right;
}

table[align=center] {
   margin-left: auto;
   margin-right: auto;
}

caption {
    display: table-caption;
}

td[nowrap], th[nowrap] {
    white-space:nowrap;
}

tt, code, kbd, samp {
    font-family: monospace
}

pre, xmp, plaintext, listing {
    display: block;
    font-family: monospace;
    white-space: pre;
    margin: 1em 0
}

/***************** LISTS ********************/

ul, menu, dir {
    display: block;
    list-style-type: disc;
    margin-top: 1em;
    margin-bottom: 1em;
    margin-left: 0;
    margin-right: 0;
    padding-left: 40px
}

ol {
    display: block;
    list-style-type: decimal;
    margin-top: 1em;
    margin-bottom: 1em;
    margin-left: 0;
    margin-right: 0;
    padding-left: 40px
}

li {
    display: list-item;
}

ul ul, ol ul {
    list-style-type: circle;
}

ol ol ul, ol ul ul, ul ol ul, ul ul ul {
    list-style-type: square;
}

dd {
    display: block;
    margin-left: 40px;
}

dl {
    display: block;
    margin-top: 1em;
    margin-bottom: 1em;
    margin-left: 0;
    margin-right: 0;
}

dt {
    display: block;
}

ol ul, ul ol, ul ul, ol ol {
    margin-top: 0;
    margin-bottom: 0
}

blockquote {
    display: block;
    margin-top: 1em;
    margin-bottom: 1em;
    margin-left: 40px;
    margin-right: 40px;
}

/*********** FORM ELEMENTS ************/

form {
    display: block;
    margin-top: 0em;
}

option {
    display: none;
}

input, textarea, keygen, select, button, isindex {
    margin: 0em;
    color: initial;
    line-height: normal;
    text-transform: none;
    text-indent: 0;
    text-shadow: none;
    display: inline-block;
}
input[type="hidden"] {
    display: none;
}


article, aside, footer, header, hgroup, nav, section
{
    display: block;
}

sub {
    vertical-align: sub;
    font-size: smaller;
}

sup {
    vertical-align: super;
    font-size: smaller;
}

figure {
    display: block;
    margin-top: 1em;
    margin-bottom: 1em;
    margin-left: 40px;
    margin-right: 40px;
}

figcaption {
    display: block;
}

)##";

class QLiteHtmlWidgetPrivate
{
public:
    QString html;
    DocumentContainerContext context;
    QUrl url;
    DocumentContainer documentContainer;
    qreal zoomFactor = 1;
    QUrl lastHighlightedLink;
    // Width used by the last full layout. -1 means "not laid out yet": a
    // computed width can legitimately be small, so 0 is not a safe sentinel.
    int lastLayoutWidth = -1;
    // Whether viewport height participates in layout (percentage heights,
    // percentage top/bottom, CSS variables, fixed positioning).
    bool heightAffectsLayout = false;
    int layoutCount = 0;
};

QLiteHtmlWidget::QLiteHtmlWidget(QWidget *parent)
    : QAbstractScrollArea(parent)
    , d(new QLiteHtmlWidgetPrivate)
{
    setMouseTracking(true);
    horizontalScrollBar()->setSingleStep(kScrollBarStep);
    verticalScrollBar()->setSingleStep(kScrollBarStep);

    d->documentContainer.setCursorCallback([this](const QCursor &c) { viewport()->setCursor(c); });
    d->documentContainer.setPaletteCallback([this] { return palette(); });
    d->documentContainer.setLinkCallback([this](const QUrl &url) {
        QUrl fullUrl = url;
        if (url.isRelative() && url.path(QUrl::FullyEncoded).isEmpty()) { // fragment/anchor only
            fullUrl = d->url;
            fullUrl.setFragment(url.fragment(QUrl::FullyEncoded));
        }
        // delay because document may not be changed directly during this callback
        QMetaObject::invokeMethod(
            this, [this, fullUrl] { emit linkClicked(fullUrl); }, Qt::QueuedConnection);
    });
    d->documentContainer.setClipboardCallback([this](bool yes) { emit copyAvailable(yes); });
    d->documentContainer.setRepaintCallback([this](const QRect &documentRect) {
        viewport()->update(fromVirtual(documentRect.translated(-scrollPosition())));
    });

    // TODO adapt mastercss to palette (default text & background color)
    d->context.setMasterStyleSheet(master_css);
}

QLiteHtmlWidget::~QLiteHtmlWidget()
{
    delete d;
}

void QLiteHtmlWidget::setUrl(const QUrl &url)
{
    d->url = url;
    QUrl baseUrl = url;
    baseUrl.setFragment({});
    const QString path = baseUrl.path(QUrl::FullyDecoded);
    const int lastSlash = path.lastIndexOf('/');
    const QString basePath = lastSlash >= 0 ? path.left(lastSlash) : QString();
    baseUrl.setPath(basePath);
    d->documentContainer.setBaseUrl(baseUrl.toString(QUrl::FullyEncoded));
    QMetaObject::invokeMethod(this, [this] { updateHightlightedLink(); }, Qt::QueuedConnection);
}

QUrl QLiteHtmlWidget::url() const
{
    return d->url;
}

void QLiteHtmlWidget::setHtml(const QString &content)
{
    d->html = content;
    // Recompute on every setHtml so the flag never goes stale; reset the
    // width cache so an empty/failed document cannot take the fast path.
    d->heightAffectsLayout = ScanViewportDependent(content);
    d->lastLayoutWidth = -1;
    d->documentContainer.setPaintDevice(viewport());
    d->documentContainer.setDocument(content.toUtf8(), &d->context);
    verticalScrollBar()->setValue(0);
    horizontalScrollBar()->setValue(0);
    render();
    QMetaObject::invokeMethod(this, [this] { updateHightlightedLink(); }, Qt::QueuedConnection);
}

QString QLiteHtmlWidget::html() const
{
    return d->html;
}

QString QLiteHtmlWidget::title() const
{
    return d->documentContainer.caption();
}

void QLiteHtmlWidget::setZoomFactor(qreal scale)
{
    Q_ASSERT(scale != 0);
    d->zoomFactor = scale;
    withFixedTextPosition([this] { render(); });
}

void QLiteHtmlWidget::setZoomFactorWithoutRender(qreal scale)
{
    Q_ASSERT(scale != 0);
    d->zoomFactor = scale;
}

int QLiteHtmlWidget::layoutCount() const
{
    return d->layoutCount;
}

int QLiteHtmlWidget::documentHeight() const
{
    if (!d->documentContainer.hasDocument())
        return 0;
    return d->documentContainer.documentHeight();
}

qreal QLiteHtmlWidget::zoomFactor() const
{
    return d->zoomFactor;
}

bool QLiteHtmlWidget::findText(const QString &text,
                               QTextDocument::FindFlags flags,
                               bool incremental,
                               bool *wrapped)
{
    bool success = false;
    QVector<QRect> oldSelection;
    QVector<QRect> newSelection;
    d->documentContainer
        .findText(text, flags, incremental, wrapped, &success, &oldSelection, &newSelection);
    // scroll to search result position and/or redraw as necessary
    QRect newSelectionCombined;
    for (const QRect &r : std::as_const(newSelection))
        newSelectionCombined = newSelectionCombined.united(r);
    QScrollBar *vBar = verticalScrollBar();
    const int top = newSelectionCombined.top();
    const int bottom = newSelectionCombined.bottom() - toVirtual(viewport()->size()).height();
    if (success && top < vBar->value() && vBar->minimum() <= top) {
        vBar->setValue(top);
    } else if (success && vBar->value() < bottom && bottom <= vBar->maximum()) {
        vBar->setValue(bottom);
    } else {
        viewport()->update(fromVirtual(newSelectionCombined.translated(-scrollPosition())));
        for (const QRect &r : std::as_const(oldSelection))
            viewport()->update(fromVirtual(r.translated(-scrollPosition())));
    }
    return success;
}

void QLiteHtmlWidget::setDefaultFont(const QFont &font)
{
    withFixedTextPosition([this, &font] {
        d->documentContainer.setDefaultFont(font);
        render();
    });
}

QFont QLiteHtmlWidget::defaultFont() const
{
    return d->documentContainer.defaultFont();
}

void QLiteHtmlWidget::setAntialias(bool on)
{
    withFixedTextPosition([this, on] {
        d->documentContainer.setAntialias(on);
        // force litehtml to recreate fonts
        setHtml(d->html);
    });
}

void QLiteHtmlWidget::scrollToAnchor(const QString &name)
{
    if (!d->documentContainer.hasDocument())
        return;
    horizontalScrollBar()->setValue(0);
    if (name.isEmpty()) {
        verticalScrollBar()->setValue(0);
        return;
    }
    const int y = d->documentContainer.anchorY(name);
    if (y >= 0)
        verticalScrollBar()->setValue(std::min(y, verticalScrollBar()->maximum()));
}

int QLiteHtmlWidget::anchorY(const QString &name) const
{
    if (!d->documentContainer.hasDocument())
        return -1;
    return d->documentContainer.anchorY(name);
}

void QLiteHtmlWidget::setResourceHandler(const QLiteHtmlWidget::ResourceHandler &handler)
{
    d->documentContainer.setDataCallback(handler);
}

void QLiteHtmlWidget::setPixmapCallBack(const QLiteHtmlWidget::PixmapCallBack &callback)
{
    d->documentContainer.setPixmapCallBack(callback);
}

QString QLiteHtmlWidget::selectedText() const
{
    return d->documentContainer.selectedText();
}

void QLiteHtmlWidget::clearSelection()
{
    d->documentContainer.clearSelection();
    viewport()->update();
}

QUrl QLiteHtmlWidget::imageAt(const QPoint &viewportPos) const
{
    if (!d->documentContainer.hasDocument())
        return {};
    QPoint virtualViewportPos;
    QPoint documentPos;
    htmlPos(viewport()->mapToParent(viewportPos), &virtualViewportPos,
            &documentPos);
    return d->documentContainer.imageAt(documentPos, virtualViewportPos);
}

void QLiteHtmlWidget::print(QPrinter *printer)
{
#if QLITEHTML_HAS_QPRINTER
    QPainter painter;
    if (!painter.begin(printer))
        return;

    DocumentContainer dc;
    dc.setDataCallback(d->documentContainer.dataCallback());
    dc.setPaletteCallback(d->documentContainer.paletteCallback());
    dc.setPixmapCallBack(d->documentContainer.pixmapCallBack());
    dc.setDefaultFont(d->documentContainer.defaultFont());
    dc.setPaintDevice(printer);
    dc.setBaseUrl(d->documentContainer.baseUrl());
    dc.setMediaType(DocumentContainer::MediaType::Print);
    dc.setDocument(d->html.toUtf8(), &d->context);

    const QRect pageRect = printer->pageRect(QPrinter::DevicePixel).toRect();
    // qlitehtml lays out in CSS pixels (96 dpi), while the printer's device
    // pixels can be much finer (HighResolution uses 1200 dpi). Lay out using
    // the logical page size and scale the painter by device/logical pixels;
    // passing device pixels to render() would make text microscopic and break
    // pagination (device page height vs logical document height).
    const qreal deviceToLogical = printer->resolution() > 0 ? 96.0 / printer->resolution() : 1.0;
    const QSize logicalPage(qMax(1, qRound(pageRect.width() * deviceToLogical)),
                            qMax(1, qRound(pageRect.height() * deviceToLogical)));
    dc.render(logicalPage.width(), logicalPage.height());

    QRect drawRect(QPoint(0, 0), logicalPage);
    painter.setClipping(true);
    painter.scale(printer->resolution() / 96.0, printer->resolution() / 96.0);
    painter.setClipRect(drawRect);

    QPoint scrollPosition(0, 0);
    while (true) {
        dc.setScrollPosition(scrollPosition);
        dc.draw(&painter, drawRect);
        scrollPosition.ry() += drawRect.height();
        if (scrollPosition.y() < dc.documentHeight())
            printer->newPage();
        else
            break;
    }

    painter.end();
#else
    Q_UNUSED(printer);
#endif
}

void QLiteHtmlWidget::paintEvent(QPaintEvent *event)
{
    if (!d->documentContainer.hasDocument())
        return;
    d->documentContainer.setScrollPosition(scrollPosition());
    QPainter p(viewport());
    p.setWorldTransform(QTransform().scale(d->zoomFactor, d->zoomFactor));
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::Antialiasing, true);
    d->documentContainer.draw(&p, toVirtual(event->rect()));
}

void QLiteHtmlWidget::resizeEvent(QResizeEvent *event)
{
    const int w = layoutWidth();
    const bool canTakeFastPath = w > 0 && !d->heightAffectsLayout && d->lastLayoutWidth >= 0
                                 && d->documentContainer.hasDocument();
    if (canTakeFastPath && w == d->lastLayoutWidth) {
        // Height-only change: layout input did not change. The viewport must
        // still follow the new geometry, then refresh scroll bar ranges and
        // repaint. Do not use withFixedTextPosition(): it would align the
        // view to a block and clamp, a pure side effect when nothing was
        // laid out again.
        QAbstractScrollArea::resizeEvent(event);
        updateScrollBars();
        return;
    }
    // Full path: identical to the original behavior, including position
    // preservation around the base class resize.
    withFixedTextPosition([this, event] {
        QAbstractScrollArea::resizeEvent(event);
        render();
    });
}

void QLiteHtmlWidget::mouseMoveEvent(QMouseEvent *event)
{
    QPoint viewportPos;
    QPoint pos;
    htmlPos(event->pos(), &viewportPos, &pos);
    const QVector<QRect> areas = d->documentContainer.mouseMoveEvent(pos, viewportPos);
    for (const QRect &r : areas)
        viewport()->update(fromVirtual(r.translated(-scrollPosition())));

    updateHightlightedLink();
}

void QLiteHtmlWidget::mousePressEvent(QMouseEvent *event)
{
    m_imagePressPos = event->pos();
    m_imagePressUrl = imageAt(viewport()->mapFromParent(event->pos()));
    QPoint viewportPos;
    QPoint pos;
    htmlPos(event->pos(), &viewportPos, &pos);
    const QVector<QRect> areas = d->documentContainer.mousePressEvent(pos,
                                                                      viewportPos,
                                                                      event->button());
    for (const QRect &r : areas)
        viewport()->update(fromVirtual(r.translated(-scrollPosition())));
}

void QLiteHtmlWidget::mouseReleaseEvent(QMouseEvent *event)
{
    QPoint viewportPos;
    QPoint pos;
    htmlPos(event->pos(), &viewportPos, &pos);
    const QVector<QRect> areas = d->documentContainer.mouseReleaseEvent(pos,
                                                                        viewportPos,
                                                                        event->button());
    for (const QRect &r : areas)
        viewport()->update(fromVirtual(r.translated(-scrollPosition())));

    if (event->button() == Qt::LeftButton && !m_imagePressUrl.isEmpty()) {
        const QUrl releaseUrl = imageAt(viewport()->mapFromParent(event->pos()));
        const bool sameImage = releaseUrl == m_imagePressUrl;
        const bool moved =
            (event->pos() - m_imagePressPos).manhattanLength() >
            QApplication::startDragDistance();
        if (sameImage && !moved &&
            d->documentContainer.linkAt(pos, viewportPos).isEmpty()) {
            const bool duplicate =
                m_lastImageClick.isValid() && m_lastImageClick.elapsed() < 300 &&
                m_lastImageClickUrl == releaseUrl;
            if (!duplicate) {
                m_lastImageClick.restart();
                m_lastImageClickUrl = releaseUrl;
                emit imageClicked(releaseUrl);
            }
        }
    }
    m_imagePressUrl.clear();
    m_imagePressPos = QPoint();
}

void QLiteHtmlWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    QPoint viewportPos;
    QPoint pos;
    htmlPos(event->pos(), &viewportPos, &pos);
    const QVector<QRect> areas = d->documentContainer.mouseDoubleClickEvent(pos,
                                                                            viewportPos,
                                                                            event->button());
    for (const QRect &r : areas) {
        viewport()->update(fromVirtual(r.translated(-scrollPosition())));
    }
}

bool QLiteHtmlWidget::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        auto helpEvent = static_cast<QHelpEvent *>(event);
        QPoint viewportPos;
        QPoint pos;
        htmlPos(helpEvent->pos(), &viewportPos, &pos);
        if (d->documentContainer.isAnimationAt(pos)) {
            QToolTip::showText(helpEvent->globalPos(),
                               tr("Play or pause the animation."),
                               viewport());
        } else {
            QToolTip::hideText();
            event->ignore();
        }
        return true;
    }
    return QAbstractScrollArea::viewportEvent(event);
}

void QLiteHtmlWidget::leaveEvent(QEvent *event)
{
    Q_UNUSED(event)
    const QVector<QRect> areas = d->documentContainer.leaveEvent();
    for (const QRect &r : areas)
        viewport()->update(fromVirtual(r.translated(-scrollPosition())));
    setHightlightedLink(QUrl());
}

void QLiteHtmlWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QPoint viewportPos;
    QPoint pos;
    htmlPos(event->pos(), &viewportPos, &pos);
    const QUrl imageUrl = imageAt(viewport()->mapFromParent(event->pos()));
    if (!imageUrl.isEmpty()) {
        emit imageContextMenuRequested(event->globalPos(), imageUrl);
        return;
    }
    emit contextMenuRequested(event->pos(), d->documentContainer.linkAt(pos, viewportPos));
}

static QAbstractSlider::SliderAction getSliderAction(int key)
{
    if (key == Qt::Key_Home)
        return QAbstractSlider::SliderToMinimum;
    if (key == Qt::Key_End)
        return QAbstractSlider::SliderToMaximum;
    if (key == Qt::Key_PageUp)
        return QAbstractSlider::SliderPageStepSub;
    if (key == Qt::Key_PageDown)
        return QAbstractSlider::SliderPageStepAdd;
    return QAbstractSlider::SliderNoAction;
}

void QLiteHtmlWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::KeypadModifier) {
        const QAbstractSlider::SliderAction sliderAction = getSliderAction(event->key());
        if (sliderAction != QAbstractSlider::SliderNoAction) {
            verticalScrollBar()->triggerAction(sliderAction);
            event->accept();
            return;
        }
    }

    QAbstractScrollArea::keyPressEvent(event);
}

void QLiteHtmlWidget::updateHightlightedLink()
{
    QPoint viewportPos;
    QPoint pos;
    htmlPos(mapFromGlobal(QCursor::pos()), &viewportPos, &pos);
    setHightlightedLink(d->documentContainer.linkAt(pos, viewportPos));
}

void QLiteHtmlWidget::setHightlightedLink(const QUrl &url)
{
    if (d->lastHighlightedLink == url)
        return;
    d->lastHighlightedLink = url;
    emit linkHighlighted(d->lastHighlightedLink);
}

void QLiteHtmlWidget::withFixedTextPosition(const std::function<void()> &action)
{
    // remember element to which to scroll after re-rendering
    QPoint viewportPos;
    QPoint pos;
    htmlPos({}, &viewportPos, &pos); // top-left
    const int y = d->documentContainer.withFixedElementPosition(pos.y(), action);
    if (y >= 0)
        verticalScrollBar()->setValue(std::min(y, verticalScrollBar()->maximum()));
}

int QLiteHtmlWidget::layoutWidth() const
{
    const int fullWidth = width() / d->zoomFactor;
    const int scrollbarWidth = style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, this);
    return fullWidth - scrollbarWidth - 2;
}

void QLiteHtmlWidget::updateScrollBars()
{
    if (!d->documentContainer.hasDocument())
        return;
    const QSize vViewportSize = toVirtual(viewport()->size());
    const int w = layoutWidth();
    // scroll bars reflect virtual/scaled size of html document
    horizontalScrollBar()->setPageStep(vViewportSize.width());
    horizontalScrollBar()->setRange(0, std::max(0, d->documentContainer.documentWidth() - w));
    verticalScrollBar()->setPageStep(vViewportSize.height());
    verticalScrollBar()
        ->setRange(0, std::max(0, d->documentContainer.documentHeight() - vViewportSize.height()));
    viewport()->update();
}

void QLiteHtmlWidget::render()
{
    if (!d->documentContainer.hasDocument())
        return;
    const int w = layoutWidth();
    const QSize vViewportSize = toVirtual(viewport()->size());
    d->documentContainer.render(w, vViewportSize.height());
    ++d->layoutCount;
    // Publish the new width before updating scroll bars: their visibility can
    // trigger a nested resize, and that resize must see the fresh value.
    d->lastLayoutWidth = w;
    updateScrollBars();
}

QPoint QLiteHtmlWidget::scrollPosition() const
{
    return {horizontalScrollBar()->value(), verticalScrollBar()->value()};
}

void QLiteHtmlWidget::htmlPos(const QPoint &pos, QPoint *viewportPos, QPoint *htmlPos) const
{
    *viewportPos = toVirtual(viewport()->mapFromParent(pos));
    *htmlPos = *viewportPos + scrollPosition();
}

QPoint QLiteHtmlWidget::toVirtual(const QPoint &p) const
{
    return {int(p.x() / d->zoomFactor), int(p.y() / d->zoomFactor)};
}

QSize QLiteHtmlWidget::toVirtual(const QSize &s) const
{
    return {int(s.width() / d->zoomFactor), int(s.height() / d->zoomFactor)};
}

QRect QLiteHtmlWidget::toVirtual(const QRect &r) const
{
    return {toVirtual(r.topLeft()), toVirtual(r.size())};
}

QRect QLiteHtmlWidget::fromVirtual(const QRect &r) const
{
    const QPoint tl{int(r.x() * d->zoomFactor), int(r.y() * d->zoomFactor)};
    // round size up, and add one since the topleft point was rounded down
    const QSize s{int(r.width() * d->zoomFactor + 0.5) + 1,
                  int(r.height() * d->zoomFactor + 0.5) + 1};
    return {tl, s};
}

QT_END_NAMESPACE
