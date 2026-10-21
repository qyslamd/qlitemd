// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "container_qpainter.h"
#include "container_qpainter_p.h"

#if QT_CONFIG(clipboard)
#include <QClipboard>
#endif
#include <QCursor>
#include <QDebug>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QImageReader>
#include <QLoggingCategory>
#include <QPainter>
#include <QPalette>
#include <QRegularExpression>
#include <QScreen>
#include <QTextLayout>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <queue>

QT_BEGIN_NAMESPACE

const int kDragDistance = 5;
// how long a repaint requested for a new animation frame may go unserved before the animation
// is considered invisible and is paused
const int kAnimationRepaintTimeoutMs = 500;
const qreal kAnimationIndicatorMaxSize = 48;
const qreal kAnimationIndicatorMinSize = 20;

using Font = QFont;
using Context = QPainter;

#ifdef Q_STATIC_LOGGING_CATEGORY
Q_STATIC_LOGGING_CATEGORY(log, "qlitehtml", QtCriticalMsg)
#else
static Q_LOGGING_CATEGORY(log, "qlitehtml", QtCriticalMsg)
#endif

static QFont toQFont(litehtml::uint_ptr hFont)
{
    return *reinterpret_cast<Font *>(hFont);
}

static QPainter *toQPainter(litehtml::uint_ptr hdc)
{
    return reinterpret_cast<Context *>(hdc);
}

static QRectF toQRectF(litehtml::position position)
{
    return {position.x, position.y, position.width, position.height};
}

static QRect toQRect(litehtml::position position)
{
    return {qRound(position.x), qRound(position.y), qRound(position.width), qRound(position.height)};
}

static bool isVisible(const litehtml::element::ptr &element)
{
    // TODO render_item::is_visible() would also take m_skip into account, so this might be wrong
    return element->css().get_display() != litehtml::display_none
           && element->css().get_visibility() == litehtml::visibility_visible;
}

static litehtml::elements_list path(const litehtml::element::ptr &element)
{
    litehtml::elements_list result;
    litehtml::element::ptr current = element;
    while (current) {
        result.push_back(current);
        current = current->parent();
    }
    std::reverse(std::begin(result), std::end(result));
    return result;
}

// <parent, first_different_child_a, first_different_child_b>
static std::tuple<litehtml::element::ptr, litehtml::element::ptr, litehtml::element::ptr>
getCommonParent(const litehtml::elements_list &a, const litehtml::elements_list &b)
{
    litehtml::element::ptr parent;
    auto ait = a.cbegin();
    auto bit = b.cbegin();
    while (ait != a.cend() && bit != b.cend()) {
        if (*ait != *bit)
            return {parent, *ait, *bit};
        parent = *ait;
        ++ait;
        ++bit;
    }
    return {parent,
            (ait != a.cend() ? *ait : litehtml::element::ptr()),
            (bit != b.cend() ? *bit : litehtml::element::ptr())};
}

static std::pair<Selection::Element, Selection::Element> getStartAndEnd(const Selection::Element &a,
                                                                        const Selection::Element &b)
{
    if (a.element == b.element) {
        if (a.index <= b.index)
            return {a, b};
        return {b, a};
    }
    const litehtml::elements_list aPath = path(a.element);
    const litehtml::elements_list bPath = path(b.element);
    litehtml::element::ptr commonParent;
    litehtml::element::ptr aBranch;
    litehtml::element::ptr bBranch;
    std::tie(commonParent, aBranch, bBranch) = getCommonParent(aPath, bPath);
    if (!commonParent) {
        qWarning() << "internal error: litehtml elements do not have common parent";
        return {a, b};
    }
    if (commonParent == a.element)
        return {a, a}; // 'a' already contains 'b'
    if (commonParent == b.element)
        return {b, b};
    // find out if a or b is first in the child sub-trees of commonParent
    for (const litehtml::element::ptr &child : commonParent->children()) {
        if (child == aBranch)
            return {a, b};
        if (child == bBranch)
            return {b, a};
    }
    qWarning() << "internal error: failed to find out order of litehtml elements";
    return {a, b};
}

// 1) stops right away if element == stop, otherwise stops whenever stop element is encountered
// 2) moves down the first children from element until there is none anymore
static litehtml::element::ptr firstLeaf(const litehtml::element::ptr &element,
                                        const litehtml::element::ptr &stop)
{
    if (element == stop)
        return element;
    litehtml::element::ptr current = element;
    while (current != stop && !current->children().empty())
        current = current->children().front();
    return current;
}

// 1) stops right away if element == stop, otherwise stops whenever stop element is encountered
// 2) starts at next sibling (up the hierarchy chain) if possible, otherwise root
// 3) returns first leaf of the element found in 2
static litehtml::element::ptr nextLeaf(const litehtml::element::ptr &element,
                                       const litehtml::element::ptr &stop)
{
    if (element == stop)
        return element;
    litehtml::element::ptr current = element;
    if (!current->is_root()) {
        // find next sibling
        const litehtml::element::ptr parent = current->parent();
        const litehtml::elements_list &children = parent->children();
        auto childIt = std::find_if(children.cbegin(),
                                    children.cend(),
                                    [&current](const litehtml::element::ptr &e) {
                                        return e == current;
                                    });
        if (childIt == children.cend()) {
            qWarning() << "internal error: filed to find litehtml child element in parent";
            return stop;
        }
        ++childIt;
        if (childIt == children.cend()) // no sibling, move up
            return nextLeaf(parent, stop);
        current = *childIt;
    }
    return firstLeaf(current, stop);
}

static Selection::Element selectionDetails(const litehtml::element::ptr &element,
                                           const QString &text,
                                           const QPointF &pos)
{
    // shortcut, which _might_ not really be correct
    if (!element->children().empty())
        return {element, -1, -1}; // everything selected
    const QFont &font = toQFont(element->css().get_font());
    const QFontMetricsF fm(font);
    qreal previous = 0;
    for (int i = 0; i < text.size(); ++i) {
        const qreal width = fm.size(0, text.left(i + 1)).width();
        if ((width + previous) / 2 >= pos.x())
            return {element, i, previous};
        previous = width;
    }
    return {element, int(text.size()), previous};
}

// Returns whether the intended child was found and stop.
// Does a depth-first iteration over elements that "pos" is inside, and executes
// \a action with them. If \a action returns \c true, the iteration is stopped.
static bool deepest_child_at_point(const litehtml::element::ptr &element,
                                   const QPoint &pos,
                                   const QPoint &viewportPos,
                                   const std::function<bool(const litehtml::element::ptr &)> &action,
                                   int level = 0)
{
    // TODO are there elements for which we should take viewportPos into account instead?
    // E.g. fixed position elements?
    if (!element)
        return false /*continue iterating*/;
    const QRectF placement = toQRectF(element->get_placement());
    // Do not continue down elements that do not cover the position. Exceptions:
    // - elements with 0 size (includes anchors and other weird elements)
    // - html and body, which for some reason have size == viewport size
    if (!placement.size().isEmpty() && element->tag() != litehtml::string_id::_html_
        && element->tag() != litehtml::string_id::_body_ && !placement.contains(pos)) {
        return false /*continue iterating*/;
    }
    // qDebug() << qPrintable(QString(level * 2, ' ')) << element->dump_get_name() << placement << pos;

    const litehtml::elements_list &children = element->children();
    for (auto it = children.cbegin(); it != children.cend(); ++it) {
        if (deepest_child_at_point(*it, pos, viewportPos, action, level + 1))
            return true;
    }
    if (placement.contains(pos))
        return action(element);
    return false /*continue iterating*/;
}

static litehtml::element::ptr element_with_id_or_name(const litehtml::element::ptr &root,
                                                      const std::string &id)
{
    std::queue<litehtml::element::ptr> pending;
    pending.push(root);
    while (!pending.empty()) {
        const litehtml::element::ptr element = pending.front();
        pending.pop();
        if (_s(element->id()) == id)
            return element;
        const char *nameAttr = element->get_attr("name");
        if (nameAttr && nameAttr == id)
            return element;
        const litehtml::elements_list &children = element->children();
        for (const auto &child : children) {
            pending.push(child);
        }
    }
    return {};
}

static Selection::Element selection_element_at_point(const litehtml::element::ptr &element,
                                                     const QPoint &pos,
                                                     const QPoint &viewportPos,
                                                     Selection::Mode mode)
{
    Selection::Element result;
    deepest_child_at_point(element,
                           pos,
                           viewportPos,
                           [mode, &result, &pos](const litehtml::element::ptr &element) {
                               const QRectF placement = toQRectF(element->get_placement());
                               std::string text;
                               element->get_text(text);
                               if (!text.empty()) {
                                   result = mode == Selection::Mode::Free
                                                ? selectionDetails(element,
                                                                   QString::fromStdString(text),
                                                                   QPointF(pos) - placement.topLeft())
                                                : Selection::Element({element, -1, -1});
                                   return true;
                               }
                               return false; /*continue*/
                           });
    return result;
}

// CSS: 400 == normal, 700 == bold.
// Qt5: 50 == normal, 75 == bold
// Qt6: == CSS
static QFont::Weight cssWeightToQtWeight(int cssWeight)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return QFont::Weight(cssWeight);
#else
    if (cssWeight <= 400)
        return QFont::Weight(cssWeight * 50 / 400);
    if (cssWeight >= 700)
        return QFont::Weight(75 + (cssWeight - 700) * 25 / 300);
    return QFont::Weight(50 + (cssWeight - 400) * 25 / 300);
#endif
}

static QFont::Style toQFontStyle(litehtml::font_style style)
{
    switch (style) {
    case litehtml::font_style_normal:
        return QFont::StyleNormal;
    case litehtml::font_style_italic:
        return QFont::StyleItalic;
    }
    // should not happen
    qWarning(log) << "Unknown litehtml font style:" << style;
    return QFont::StyleNormal;
}

static QColor toQColor(const litehtml::web_color &color)
{
    return {color.red, color.green, color.blue, color.alpha};
}

static Qt::PenStyle borderPenStyle(litehtml::border_style style)
{
    switch (style) {
    case litehtml::border_style_dotted:
        return Qt::DotLine;
    case litehtml::border_style_dashed:
        return Qt::DashLine;
    case litehtml::border_style_solid:
        return Qt::SolidLine;
    default:
        qWarning(log) << "Unsupported border style:" << style;
    }
    return Qt::SolidLine;
}

static QPen borderPen(const litehtml::border &border)
{
    return {toQColor(border.color), qreal(border.width), borderPenStyle(border.style)};
}

static QCursor toQCursor(const QString &c)
{
    if (c == "alias")
        return {Qt::PointingHandCursor}; // ???
    if (c == "all-scroll")
        return {Qt::SizeAllCursor};
    if (c == "auto")
        return {Qt::ArrowCursor}; // ???
    if (c == "cell")
        return {Qt::UpArrowCursor};
    if (c == "context-menu")
        return {Qt::ArrowCursor}; // ???
    if (c == "col-resize")
        return {Qt::SplitHCursor};
    if (c == "copy")
        return {Qt::DragCopyCursor};
    if (c == "crosshair")
        return {Qt::CrossCursor};
    if (c == "default")
        return {Qt::ArrowCursor};
    if (c == "e-resize")
        return {Qt::SizeHorCursor}; // ???
    if (c == "ew-resize")
        return {Qt::SizeHorCursor};
    if (c == "grab")
        return {Qt::OpenHandCursor};
    if (c == "grabbing")
        return {Qt::ClosedHandCursor};
    if (c == "help")
        return {Qt::WhatsThisCursor};
    if (c == "move")
        return {Qt::SizeAllCursor};
    if (c == "n-resize")
        return {Qt::SizeVerCursor}; // ???
    if (c == "ne-resize")
        return {Qt::SizeBDiagCursor}; // ???
    if (c == "nesw-resize")
        return {Qt::SizeBDiagCursor};
    if (c == "ns-resize")
        return {Qt::SizeVerCursor};
    if (c == "nw-resize")
        return {Qt::SizeFDiagCursor}; // ???
    if (c == "nwse-resize")
        return {Qt::SizeFDiagCursor};
    if (c == "no-drop")
        return {Qt::ForbiddenCursor};
    if (c == "none")
        return {Qt::BlankCursor};
    if (c == "not-allowed")
        return {Qt::ForbiddenCursor};
    if (c == "pointer")
        return {Qt::PointingHandCursor};
    if (c == "progress")
        return {Qt::BusyCursor};
    if (c == "row-resize")
        return {Qt::SplitVCursor};
    if (c == "s-resize")
        return {Qt::SizeVerCursor}; // ???
    if (c == "se-resize")
        return {Qt::SizeFDiagCursor}; // ???
    if (c == "sw-resize")
        return {Qt::SizeBDiagCursor}; // ???
    if (c == "text")
        return {Qt::IBeamCursor};
    if (c == "url")
        return {Qt::ArrowCursor}; // ???
    if (c == "w-resize")
        return {Qt::SizeHorCursor}; // ???
    if (c == "wait")
        return {Qt::BusyCursor};
    if (c == "zoom-in")
        return {Qt::ArrowCursor}; // ???
    qWarning(log) << QString("unknown cursor property \"%1\"").arg(c).toUtf8().constData();
    return {Qt::ArrowCursor};
}

bool Selection::isValid() const
{
    return !selection.isEmpty();
}

void Selection::update()
{
    const auto addElement = [this](const Selection::Element &element,
                                   const Selection::Element &end = {}) {
        std::string elemText;
        element.element->get_text(elemText);
        const QString textStr = QString::fromStdString(elemText);
        if (!textStr.isEmpty()) {
            QRectF rect = toQRectF(element.element->get_placement()).adjusted(-1, -1, 1, 1);
            if (element.index < 0) { // fully selected
                text += textStr;
            } else if (end.element) { // select from element "to end"
                if (element.element == end.element) {
                    // end.index is guaranteed to be >= element.index by caller, same for x
                    text += textStr.mid(element.index, end.index - element.index);
                    const qreal left = rect.left();
                    rect.setLeft(left + element.x);
                    rect.setRight(left + end.x);
                } else {
                    text += textStr.mid(element.index);
                    rect.setLeft(rect.left() + element.x);
                }
            } else { // select from start of element
                text += textStr.left(element.index);
                rect.setRight(rect.left() + element.x);
            }
            selection.append(rect);
        }
    };

    if (startElem.element && endElem.element) {
        // Edge cases:
        // start and end elements could be reversed or children of each other
        Selection::Element start;
        Selection::Element end;
        std::tie(start, end) = getStartAndEnd(startElem, endElem);

        selection.clear();
        text.clear();

        // Treats start element as a leaf even if it isn't, because it already contains all its
        // children
        addElement(start, end);
        if (start.element != end.element) {
            litehtml::element::ptr current = start.element;
            do {
                current = nextLeaf(current, end.element);
                if (current == end.element)
                    addElement(end);
                else
                    addElement({current, -1, -1});
            } while (current != end.element);
        }
    } else {
        selection = {};
        text.clear();
    }
#if QT_CONFIG(clipboard)
    QClipboard *cb = QGuiApplication::clipboard();
    if (cb->supportsSelection())
        cb->setText(text, QClipboard::Selection);
#endif
}

QRectF Selection::boundingRect() const
{
    QRectF rect;
    for (const QRectF &r : selection)
        rect = rect.united(r);
    return rect;
}

DocumentContainer::DocumentContainer()
    : d(new DocumentContainerPrivate)
{}

DocumentContainer::~DocumentContainer() = default;

litehtml::uint_ptr DocumentContainerPrivate::create_font(const litehtml::font_description &descr,
                                                         const litehtml::document *doc,
                                                         litehtml::font_metrics *fm)
{
    Q_UNUSED(doc)
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QStringList splitNames = QString::fromStdString(descr.family).split(',', Qt::SkipEmptyParts);
#else
    const QStringList splitNames = QString::fromStdString(descr.family).split(',', QString::SkipEmptyParts);
#endif
    QStringList familyNames;
    std::transform(splitNames.cbegin(),
                   splitNames.cend(),
                   std::back_inserter(familyNames),
                   [this](const QString &s) {
                       // clean whitespace and quotes
                       QString name = s.trimmed();
                       if (name.startsWith('\"'))
                           name = name.mid(1);
                       if (name.endsWith('\"'))
                           name.chop(1);
                       const QString lowerName = name.toLower();
                       if (lowerName == "serif")
                           return serifFont();
                       if (lowerName == "sans-serif")
                           return sansSerifFont();
                       if (lowerName == "monospace")
                           return monospaceFont();
                       return name;
                   });
    auto font = new QFont();
#if QT_VERSION >= QT_VERSION_CHECK(5, 13, 0)
    font->setFamilies(familyNames);
#else
    if (!familyNames.isEmpty())
        font->setFamily(familyNames.first());
#endif
    font->setPixelSize(descr.size);
    font->setWeight(cssWeightToQtWeight(descr.weight));
    font->setStyle(toQFontStyle(descr.style));
    font->setStyleStrategy(m_antialias ? QFont::PreferAntialias : QFont::NoAntialias);
    if (descr.decoration_line & litehtml::text_decoration_line_underline)
        font->setUnderline(true);
    if (descr.decoration_line & litehtml::text_decoration_line_overline)
        font->setOverline(true);
    if (descr.decoration_line & litehtml::text_decoration_line_line_through)
        font->setStrikeOut(true);
    if (fm) {
        const QFontMetrics metrics(*font);
        fm->font_size = descr.size;
        fm->height = metrics.height();
        fm->ascent = metrics.ascent();
        fm->descent = metrics.descent();
        fm->x_height = metrics.xHeight();
        fm->draw_spaces = true;
        // The same values as used by the litehtml built-in containers.
        fm->sub_shift = descr.size / 5;
        fm->super_shift = descr.size / 3;
    }
    return reinterpret_cast<litehtml::uint_ptr>(font);
}

void DocumentContainerPrivate::delete_font(litehtml::uint_ptr hFont)
{
    auto font = reinterpret_cast<Font *>(hFont);
    delete font;
}

litehtml::pixel_t DocumentContainerPrivate::text_width(const char *text, litehtml::uint_ptr hFont)
{
    const QFontMetrics fm(toQFont(hFont));
    return fm.horizontalAdvance(QString::fromUtf8(text));
}

void DocumentContainerPrivate::draw_text(litehtml::uint_ptr hdc,
                                         const char *text,
                                         litehtml::uint_ptr hFont,
                                         litehtml::web_color color,
                                         const litehtml::position &pos)
{
    auto painter = toQPainter(hdc);
    painter->setFont(toQFont(hFont));
    painter->setPen(toQColor(color));
    painter->drawText(toQRectF(pos), 0, QString::fromUtf8(text));
}

litehtml::pixel_t DocumentContainerPrivate::pt_to_px(float pt) const
{
    // magic factor of 11/12 to account for differences to webengine/webkit
    return m_paintDevice->physicalDpiY() * pt * 11.f / m_paintDevice->logicalDpiY() / 12.f;
}

litehtml::pixel_t DocumentContainerPrivate::get_default_font_size() const
{
    return m_defaultFont.pointSize();
}

const char *DocumentContainerPrivate::get_default_font_name() const
{
    return m_defaultFontFamilyName.constData();
}

void DocumentContainerPrivate::draw_list_marker(litehtml::uint_ptr hdc,
                                                const litehtml::list_marker &marker)
{
    auto painter = toQPainter(hdc);
    if (marker.image.empty()) {
        if (marker.marker_type == litehtml::list_style_type_square) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(toQColor(marker.color));
            painter->drawRect(toQRectF(marker.pos));
        } else if (marker.marker_type == litehtml::list_style_type_disc) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(toQColor(marker.color));
            painter->drawEllipse(toQRectF(marker.pos));
        } else if (marker.marker_type == litehtml::list_style_type_circle) {
            painter->setPen(toQColor(marker.color));
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(toQRectF(marker.pos));
        } else {
            // TODO we do not get information about index and font for e.g. decimal / roman
            // at least draw a bullet
            painter->setPen(Qt::NoPen);
            painter->setBrush(toQColor(marker.color));
            painter->drawEllipse(toQRectF(marker.pos));
            qWarning(log) << "list marker of type" << marker.marker_type << "not supported";
        }
    } else {
        const QPixmap pixmap = getImageFrame(QString::fromStdString(marker.image),
                                             QString::fromUtf8(marker.baseurl),
                                             toQRect(marker.pos).translated(m_scrollPosition));
        painter->drawPixmap(toQRectF(marker.pos), pixmap, QRectF(pixmap.rect()));
    }
}

void DocumentContainerPrivate::load_image(const char *src, const char *baseurl, bool redraw_on_ready)
{
    const auto qtSrc = QString::fromUtf8(src);
    const auto qtBaseUrl = QString::fromUtf8(baseurl);
    Q_UNUSED(redraw_on_ready)
    qDebug(log) << "load_image:" << QString("src = \"%1\";").arg(qtSrc).toUtf8().constData()
                << QString("base = \"%1\"").arg(qtBaseUrl).toUtf8().constData();
    const QUrl url = resolveUrl(qtSrc, qtBaseUrl);
    if (m_pixmaps.contains(url))
        return;

    if (pixmap_callback_) {
        const QPixmap pixmap = pixmap_callback_(url);
        if (!pixmap.isNull()) {
            m_pixmaps.insert(url, pixmap);
            return;
        }
    }

    const QByteArray data = m_dataCallback(url);
    QPixmap pixmap;
    pixmap.loadFromData(data);
    m_pixmaps.insert(url, pixmap);
    loadAnimation(url, data);
}

void DocumentContainerPrivate::loadAnimation(const QUrl &url, const QByteArray &data)
{
    {   // only images with more than one frame are handled by a QMovie
        QBuffer probeBuffer;
        probeBuffer.setData(data);
        probeBuffer.open(QIODevice::ReadOnly);
        const QImageReader probe(&probeBuffer);
        if (!probe.supportsAnimation() || probe.imageCount() <= 1)
            return;
    }

    auto animation = std::make_unique<AnimatedImage>();
    animation->data = data;
    animation->buffer = std::make_unique<QBuffer>(&animation->data);
    if (!animation->buffer->open(QIODevice::ReadOnly))
        return;
    animation->movie = std::make_unique<QMovie>(animation->buffer.get());
    if (!animation->movie->isValid())
        return;
    QObject::connect(animation->movie.get(), &QMovie::frameChanged, &m_animationContext,
                     [this, url] { animationFrameChanged(url); });
    qDebug(log) << "load_image: animation with" << animation->movie->frameCount()
                << "frames for" << url;
    m_animations.emplace(url, std::move(animation));
}

AnimatedImage *DocumentContainerPrivate::animationForUrl(const QUrl &url) const
{
    const auto it = m_animations.find(url);
    return it == m_animations.end() ? nullptr : it->second.get();
}

AnimatedImage *DocumentContainerPrivate::animationAt(const QPoint &documentPos) const
{
    for (const auto &[url, animation] : m_animations) {
        for (const QRect &rect : std::as_const(animation->rects)) {
            if (rect.contains(documentPos))
                return animation.get();
        }
    }
    return nullptr;
}

QVector<QRect> DocumentContainerPrivate::toggleAnimationAt(const QPoint &documentPos)
{
    AnimatedImage *animation = animationAt(documentPos);
    if (!animation || !m_repaintCallback)
        return {};
    animation->playing = !animation->playing;
    QMovie *movie = animation->movie.get();
    if (animation->playing) {
        animation->pendingRepaint.invalidate();
        if (animation->started) {
            movie->setPaused(false);
        } else {
            animation->started = true;
            movie->start();
        }
    } else {
        movie->setPaused(true);
    }
    return animation->rects;
}

// Registers the area where an animation was drawn, and makes sure that it is running.
// Animations are only started when they are actually painted, and paused again as soon as the
// repaints that they request do not result in them being painted anymore.
void DocumentContainerPrivate::animationDrawn(AnimatedImage *animation, const QRect &documentRect)
{
    animation->pendingRepaint.invalidate();
    if (!animation->rects.contains(documentRect))
        animation->rects.append(documentRect);
    if (!m_repaintCallback || !animation->playing)
        return;
    QMovie *movie = animation->movie.get();
    if (movie->state() == QMovie::NotRunning)
        movie->start();
    else if (movie->state() == QMovie::Paused)
        movie->setPaused(false);
}

void DocumentContainerPrivate::drawAnimationIndicator(QPainter *painter, const QRectF &rect) const
{
    const qreal size = qMin(kAnimationIndicatorMaxSize, qMin(rect.width(), rect.height()) * 0.4);
    if (size < kAnimationIndicatorMinSize)
        return;
    const QRectF circle(rect.center() - QPointF(size / 2, size / 2), QSizeF(size, size));
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, m_antialias);
    painter->setPen(QPen(QColor(255, 255, 255, 200), size / 16));
    painter->setBrush(QColor(0, 0, 0, 120));
    painter->drawEllipse(circle);
    const QPointF center = circle.center();
    const qreal cornerDistance = size * 0.28;
    const qreal halfSide = cornerDistance * std::sqrt(3.) / 2;
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(255, 255, 255, 220));
    painter->drawPolygon(QPolygonF({center + QPointF(cornerDistance, 0),
                                    center + QPointF(-cornerDistance / 2, halfSide),
                                    center + QPointF(-cornerDistance / 2, -halfSide)}));
    painter->restore();
}

void DocumentContainerPrivate::animationFrameChanged(const QUrl &url)
{
    AnimatedImage *animation = animationForUrl(url);
    if (!animation || !m_repaintCallback)
        return;
    // The repaints that we requested for the previous frames were not served for a while, so
    // nobody is looking at the animation and we stop wasting cycles on decoding it. Note that
    // the timer only runs while a repaint is pending, so this is independent of the frame rate
    // of the animation. Drawing the image starts the animation again.
    if (animation->pendingRepaint.isValid()) {
        if (animation->pendingRepaint.hasExpired(kAnimationRepaintTimeoutMs)) {
            animation->movie->setPaused(true);
            return;
        }
    } else {
        animation->pendingRepaint.start();
    }
    for (const QRect &rect : std::as_const(animation->rects))
        m_repaintCallback(rect);
}

void DocumentContainerPrivate::get_image_size(const char *src,
                                              const char *baseurl,
                                              litehtml::size &sz)
{
    const auto qtSrc = QString::fromUtf8(src);
    const auto qtBaseUrl = QString::fromUtf8(baseurl);
    if (qtSrc.isEmpty()) // for some reason that happens
        return;
    qDebug(log) << "get_image_size:" << QString("src = \"%1\";").arg(qtSrc).toUtf8().constData()
                << QString("base = \"%1\"").arg(qtBaseUrl).toUtf8().constData();
    const QPixmap pm = getPixmap(qtSrc, qtBaseUrl);
    sz.width = pm.width();
    sz.height = pm.height();
}

void DocumentContainerPrivate::drawSelection(QPainter *painter, const QRect &clip) const
{
    painter->save();
    painter->setClipRect(clip, Qt::IntersectClip);
    for (const QRectF &r : m_selection.selection) {
        const QRectF clientRect = r.translated(-m_scrollPosition.x(), -m_scrollPosition.y());
        const QPalette palette = m_paletteCallback();
        painter->fillRect(clientRect, palette.brush(QPalette::Highlight));
    }
    painter->restore();
}

static bool isInBody(const litehtml::element::ptr &e)
{
    litehtml::element::ptr current = e;
    while (current && current->tag() != litehtml::string_id::_body_)
        current = current->parent();
    return current.get() != nullptr;
}

void DocumentContainerPrivate::buildIndex()
{
    m_index.elementToIndex.clear();
    m_index.indexToElement.clear();
    m_index.text.clear();

    int index = 0;
    bool inBody = false;
    litehtml::element::ptr current = firstLeaf(m_document->root(), nullptr);
    while (current != m_document->root()) {
        m_index.elementToIndex.insert({current, index});
        if (!inBody)
            inBody = isInBody(current);
        if (inBody && isVisible(current)) {
            std::string text;
            current->get_text(text);
            if (!text.empty()) {
                m_index.indexToElement.push_back({index, current});
                const QString str = QString::fromStdString(text);
                m_index.text += str;
                index += str.size();
            }
        }
        current = nextLeaf(current, m_document->root());
    }
}

void DocumentContainerPrivate::updateSelection()
{
    const QString oldText = m_selection.text;
    m_selection.update();
    if (!m_clipboardCallback)
        return;

    const QString newText = m_selection.text;
    if (oldText.isEmpty() && !newText.isEmpty())
        m_clipboardCallback(true);
    else if (!oldText.isEmpty() && newText.isEmpty())
        m_clipboardCallback(false);
}

void DocumentContainerPrivate::clearSelection()
{
    const QString oldText = m_selection.text;
    m_selection = {};
    if (!m_clipboardCallback)
        return;

    if (!oldText.isEmpty())
        m_clipboardCallback(false);
}

static void applyBackgroundLayerClip(QPainter *painter, const litehtml::background_layer &layer)
{
    const QRegion initialClipRegion = painter->clipRegion();
    const Qt::ClipOperation initialClipOperation
        = initialClipRegion.isEmpty() ? Qt::ReplaceClip : Qt::IntersectClip;
    if (!initialClipRegion.isEmpty())
        painter->setClipRegion(initialClipRegion);
    painter->setClipRect(toQRectF(layer.clip_box), initialClipOperation);
    const auto bb = toQRect(layer.border_box);
    const auto &br = layer.border_radius;
    const QRegion horizontalMiddle(QRect(bb.x(),
                                         bb.y() + qRound(br.top_left_y),
                                         bb.width(),
                                         bb.height() - qRound(br.top_left_y) - qRound(br.bottom_left_y)));
    const QRegion horizontalTop(QRect(bb.x() + qRound(br.top_left_x),
                                      bb.y(),
                                      bb.width() - qRound(br.top_left_x) - qRound(br.top_right_x),
                                      qRound(br.top_left_y)));
    const QRegion horizontalBottom(QRect(bb.x() + qRound(br.bottom_left_x),
                                         bb.bottom() - qRound(br.bottom_left_y),
                                         bb.width() - qRound(br.bottom_left_x) - qRound(br.bottom_right_x),
                                         qRound(br.bottom_left_y)));
    const QRegion topLeft(QRect(bb.left(),
                                bb.top(),
                                2 * qRound(br.top_left_x),
                                2 * qRound(br.top_left_y)),
                          QRegion::Ellipse);
    const QRegion topRight(QRect(bb.right() - 2 * qRound(br.top_right_x),
                                 bb.top(),
                                 2 * qRound(br.top_right_x),
                                 2 * qRound(br.top_right_y)),
                           QRegion::Ellipse);
    const QRegion bottomLeft(QRect(bb.left(),
                                   bb.bottom() - 2 * qRound(br.bottom_left_y),
                                   2 * qRound(br.bottom_left_x),
                                   2 * qRound(br.bottom_left_y)),
                             QRegion::Ellipse);
    const QRegion bottomRight(QRect(bb.right() - 2 * qRound(br.bottom_right_x),
                                    bb.bottom() - 2 * qRound(br.bottom_right_y),
                                    2 * qRound(br.bottom_right_x),
                                    2 * qRound(br.bottom_right_y)),
                              QRegion::Ellipse);
    const QRegion clipRegion = horizontalMiddle.united(horizontalTop)
                                   .united(horizontalBottom)
                                   .united(topLeft)
                                   .united(topRight)
                                   .united(bottomLeft)
                                   .united(bottomRight);
    painter->setClipRegion(clipRegion, Qt::IntersectClip);
}

void DocumentContainerPrivate::draw_image(litehtml::uint_ptr hdc,
                                          const litehtml::background_layer &layer,
                                          const std::string &url,
                                          const std::string &base_url)
{
    if (layer.is_root)
        return;
    auto painter = toQPainter(hdc);
    painter->save();
    applyBackgroundLayerClip(painter, layer);
    const QUrl imageUrl = resolveUrl(QString::fromStdString(url), QString::fromStdString(base_url));
    // the layer boxes are in viewport space, the repainting is done in document space
    const QPixmap pixmap = getImageFrame(imageUrl,
                                         toQRect(layer.border_box).translated(m_scrollPosition));
    const QRectF originRect = toQRectF(layer.origin_box);
    const QRectF borderBox = toQRectF(layer.border_box);
    if (layer.repeat == litehtml::background_repeat_no_repeat) {
        painter->drawPixmap(originRect, pixmap, QRectF(pixmap.rect()));
    } else if (layer.repeat == litehtml::background_repeat_repeat_x) {
        if (originRect.width() > 0) {
            qreal x = borderBox.left();
            while (x <= borderBox.right()) {
                painter->drawPixmap(QRectF(x, originRect.top(), originRect.width(), originRect.height()),
                                    pixmap, QRectF(pixmap.rect()));
                x += originRect.width();
            }
        }
    } else {
        qWarning(log) << "unsupported background repeat" << layer.repeat;
    }
    if (AnimatedImage *animation = animationForUrl(imageUrl)) {
        if (m_repaintCallback && !animation->playing)
            drawAnimationIndicator(painter, originRect);
    }
    painter->restore();
}

void DocumentContainerPrivate::draw_solid_fill(litehtml::uint_ptr hdc,
                                               const litehtml::background_layer &layer,
                                               const litehtml::web_color &color)
{
    if (layer.is_root)
        return;
    auto painter = toQPainter(hdc);
    painter->save();
    applyBackgroundLayerClip(painter, layer);
    painter->setPen(Qt::NoPen);
    painter->setBrush(toQColor(color));
    painter->drawRect(toQRectF(layer.border_box));
    drawSelection(painter, toQRect(layer.border_box));
    painter->restore();
}

void DocumentContainerPrivate::draw_linear_gradient(
    litehtml::uint_ptr hdc,
    const litehtml::background_layer &layer,
    const litehtml::background_layer::linear_gradient &gradient)
{
    Q_UNUSED(hdc)
    Q_UNUSED(layer)
    Q_UNUSED(gradient)
    qWarning(log) << "draw_linear_gradient not implemented";
}

void DocumentContainerPrivate::draw_radial_gradient(
    litehtml::uint_ptr hdc,
    const litehtml::background_layer &layer,
    const litehtml::background_layer::radial_gradient &gradient)
{
    Q_UNUSED(hdc)
    Q_UNUSED(layer)
    Q_UNUSED(gradient)
    qWarning(log) << "draw_radial_gradient not implemented";
}

void DocumentContainerPrivate::draw_conic_gradient(
    litehtml::uint_ptr hdc,
    const litehtml::background_layer &layer,
    const litehtml::background_layer::conic_gradient &gradient)
{
    Q_UNUSED(hdc)
    Q_UNUSED(layer)
    Q_UNUSED(gradient)
    qWarning(log) << "draw_conic_gradient not implemented";
}

void DocumentContainerPrivate::draw_borders(litehtml::uint_ptr hdc,
                                            const litehtml::borders &borders,
                                            const litehtml::position &draw_pos,
                                            bool root)
{
    Q_UNUSED(root)
    // TODO: special border styles
    auto painter = toQPainter(hdc);
    const auto &r = borders.radius;
    const qreal left   = draw_pos.left();
    const qreal top    = draw_pos.top();
    const qreal right  = draw_pos.right();
    const qreal bottom = draw_pos.bottom();
    if (borders.top.style != litehtml::border_style_none
        && borders.top.style != litehtml::border_style_hidden) {
        painter->setPen(borderPen(borders.top));
        painter->drawLine(QPointF(left + r.top_left_x, top),
                          QPointF(right - r.top_right_x, top));
        painter->drawArc(QRectF(left, top, 2 * r.top_left_x, 2 * r.top_left_y),
                         90 * 16, 90 * 16);
        painter->drawArc(QRectF(right - 2 * r.top_right_x, top,
                                2 * r.top_right_x, 2 * r.top_right_y),
                         0, 90 * 16);
    }
    if (borders.bottom.style != litehtml::border_style_none
        && borders.bottom.style != litehtml::border_style_hidden) {
        painter->setPen(borderPen(borders.bottom));
        painter->drawLine(QPointF(left + r.bottom_left_x, bottom),
                          QPointF(right - r.bottom_right_x, bottom));
        painter->drawArc(QRectF(left, bottom - 2 * r.bottom_left_y,
                                2 * r.bottom_left_x, 2 * r.bottom_left_y),
                         180 * 16, 90 * 16);
        painter->drawArc(QRectF(right - 2 * r.bottom_right_x, bottom - 2 * r.bottom_right_y,
                                2 * r.bottom_right_x, 2 * r.bottom_right_y),
                         270 * 16, 90 * 16);
    }
    if (borders.left.style != litehtml::border_style_none
        && borders.left.style != litehtml::border_style_hidden) {
        painter->setPen(borderPen(borders.left));
        painter->drawLine(QPointF(left, top + r.top_left_y),
                          QPointF(left, bottom - r.bottom_left_y));
    }
    if (borders.right.style != litehtml::border_style_none
        && borders.right.style != litehtml::border_style_hidden) {
        painter->setPen(borderPen(borders.right));
        painter->drawLine(QPointF(right, top + r.top_right_y),
                          QPointF(right, bottom - r.bottom_right_y));
    }
}

void DocumentContainerPrivate::set_caption(const char *caption)
{
    m_caption = QString::fromUtf8(caption);
}

void DocumentContainerPrivate::set_base_url(const char *base_url)
{
    m_baseUrl = QString::fromUtf8(base_url);
}

void DocumentContainerPrivate::link(const std::shared_ptr<litehtml::document> &doc,
                                    const litehtml::element::ptr &el)
{
    // TODO
    qWarning(log) << "link not implemented";
    Q_UNUSED(doc)
    Q_UNUSED(el)
}

void DocumentContainerPrivate::on_anchor_click(const char *url, const litehtml::element::ptr &el)
{
    Q_UNUSED(el)
    m_linkActivated = true;
    if (!m_blockLinks)
        m_linkCallback(resolveUrl(QString::fromUtf8(url), m_baseUrl));
}

void DocumentContainerPrivate::set_cursor(const char *cursor)
{
    m_cursorCallback(toQCursor(QString::fromUtf8(cursor)));
}

void DocumentContainerPrivate::transform_text(litehtml::string &text, litehtml::text_transform tt)
{
    // TODO
    qWarning(log) << "transform_text not implemented";
    Q_UNUSED(text)
    Q_UNUSED(tt)
}

void DocumentContainerPrivate::import_css(litehtml::string &text,
                                          const litehtml::string &url,
                                          litehtml::string &baseurl)
{
    const QUrl actualUrl = resolveUrl(QString::fromStdString(url), QString::fromStdString(baseurl));
    const QString urlString = actualUrl.toString(QUrl::None);
    const int lastSlash = urlString.lastIndexOf('/');
    baseurl = urlString.left(lastSlash).toStdString();
    text = QString::fromUtf8(m_dataCallback(actualUrl)).toStdString();
}

void DocumentContainerPrivate::set_clip(const litehtml::position &pos,
                                        const litehtml::border_radiuses &bdr_radius)
{
    // TODO
    qWarning(log) << "set_clip not implemented";
    Q_UNUSED(pos)
    Q_UNUSED(bdr_radius)
}

void DocumentContainerPrivate::del_clip()
{
    // TODO
    qWarning(log) << "del_clip not implemented";
}

void DocumentContainerPrivate::get_viewport(litehtml::position &viewport) const
{
    viewport = litehtml::position(m_clientRect.x(), m_clientRect.y(), m_clientRect.width(), m_clientRect.height());
}

std::shared_ptr<litehtml::element> DocumentContainerPrivate::create_element(
    const char *tag_name,
    const litehtml::string_map &attributes,
    const std::shared_ptr<litehtml::document> &doc)
{
    // TODO
    qDebug(log) << "create_element" << QString::fromUtf8(tag_name);
    Q_UNUSED(attributes)
    Q_UNUSED(doc)
    return {};
}

void DocumentContainerPrivate::get_media_features(litehtml::media_features &media) const
{
    media.type = mediaType;
    qDebug(log) << "get_media_features";
}

void DocumentContainerPrivate::on_mouse_event(const litehtml::element::ptr &el,
                                              litehtml::mouse_event event)
{
    Q_UNUSED(el)
    Q_UNUSED(event)
}

void DocumentContainerPrivate::get_language(litehtml::string &language, litehtml::string &culture) const
{
    // TODO
    qWarning(log) << "get_language not implemented";
    Q_UNUSED(language)
    Q_UNUSED(culture)
}

void DocumentContainer::setPaintDevice(QPaintDevice *paintDevice)
{
    d->m_paintDevice = paintDevice;
}

void DocumentContainer::setScrollPosition(const QPoint &pos)
{
    d->m_scrollPosition = pos;
}

void DocumentContainer::setDocument(const QByteArray &data, DocumentContainerContext *context)
{
    d->m_pixmaps.clear();
    d->m_animations.clear();
    d->clearSelection();
    d->m_document = litehtml::document::createFromString(data.constData(),
                                                         d.get(),
                                                         context->d->masterCss.toUtf8().constData());
    d->buildIndex();
}

bool DocumentContainer::hasDocument() const
{
    return d->m_document.get();
}

void DocumentContainer::setBaseUrl(const QString &url)
{
    d->set_base_url(url.toUtf8().constData());
}

QString DocumentContainer::baseUrl() const
{
    return d->m_baseUrl;
}

void DocumentContainer::render(int width, int height)
{
    d->m_clientRect = {0, 0, width, height};
    for (const auto &[url, animation] : d->m_animations)
        animation->rects.clear();
    if (!d->m_document)
        return;
    d->m_document->render(width);
    d->updateSelection();
}

void DocumentContainer::draw(QPainter *painter, const QRect &clip)
{
    d->drawSelection(painter, clip);
    const QPoint pos = -d->m_scrollPosition;
    const litehtml::position clipRect(clip.x(), clip.y(), clip.width(), clip.height());
    d->m_document->draw(reinterpret_cast<litehtml::uint_ptr>(painter), pos.x(), pos.y(), &clipRect);
}

int DocumentContainer::documentWidth() const
{
    return d->m_document->width();
}

int DocumentContainer::documentHeight() const
{
    return d->m_document->height();
}

int DocumentContainer::anchorY(const QString &anchorName) const
{
    // do manual recursive search instead of using litehtml's select_one css selector method,
    // since we can have '.' in our IDs which has special meaning (class selector) for css selectors
    // and litehtml doesn't support escaping them
    litehtml::element::ptr element = element_with_id_or_name(d->m_document->root(),
                                                             anchorName.toStdString());
    if (!element) {
        // Convert the anchor name to lowercase, as the current litehtml implementation
        // converts all IDs to lowercase when parsing the HTML. This will change in future
        // versions of litehtml, see https://github.com/litehtml/litehtml/pull/347)
        const QString lowerAnchorName = anchorName.toLower();
        element = element_with_id_or_name(d->m_document->root(), lowerAnchorName.toStdString());
        if (!element)
            return -1;
    }
    while (element) {
        if (element->get_placement().y > 0)
            return qRound(element->get_placement().y);
        element = element->parent();
    }
    return 0;
}

static litehtml::media_type fromQt(const DocumentContainer::MediaType mt)
{
    using MT = DocumentContainer::MediaType;
    switch (mt)
    {
    case MT::None:
        return litehtml::media_type_unknown;
    case MT::All:
        return litehtml::media_type_all;
    case MT::Screen:
        return litehtml::media_type_screen;
    case MT::Print:
        return litehtml::media_type_print;
    case MT::Braille:
    case MT::Embossed:
    case MT::Handheld:
    case MT::Projection:
    case MT::Speech:
    case MT::TTY:
    case MT::TV:
        return litehtml::media_type_first_deprecated;
    }
    Q_UNREACHABLE();
}

void DocumentContainer::setMediaType(MediaType mt)
{
    d->mediaType = fromQt(mt);
}

QVector<QRect> DocumentContainer::mousePressEvent(const QPoint &documentPos,
                                                  const QPoint &viewportPos,
                                                  Qt::MouseButton button)
{
    if (!d->m_document || button != Qt::LeftButton)
        return {};
    QVector<QRect> redrawRects;
    // selection
    if (d->m_selection.isValid())
        redrawRects.append(d->m_selection.boundingRect().toAlignedRect());
    d->clearSelection();
    d->m_selection.selectionStartDocumentPos = documentPos;
    d->m_selection.startElem = selection_element_at_point(d->m_document->root(),
                                                          documentPos,
                                                          viewportPos,
                                                          d->m_selection.mode);
    // post to litehtml
    litehtml::position::vector redrawBoxes;
    if (d->m_document->on_lbutton_down(
            documentPos.x(), documentPos.y(), viewportPos.x(), viewportPos.y(), redrawBoxes)) {
        for (const litehtml::position &box : redrawBoxes)
            redrawRects.append(toQRect(box));
    }
    return redrawRects;
}

QVector<QRect> DocumentContainer::mouseMoveEvent(const QPoint &documentPos,
                                                 const QPoint &viewportPos)
{
    if (!d->m_document)
        return {};
    QVector<QRect> redrawRects;
    // selection
    if (d->m_selection.isSelecting
        || (!d->m_selection.selectionStartDocumentPos.isNull()
            && (d->m_selection.selectionStartDocumentPos - documentPos).manhattanLength() >= kDragDistance
            && d->m_selection.startElem.element)) {
        const Selection::Element element = selection_element_at_point(d->m_document->root(),
                                                                      documentPos,
                                                                      viewportPos,
                                                                      d->m_selection.mode);
        if (element.element) {
            redrawRects.append(
                d->m_selection.boundingRect().toAlignedRect() /*.adjusted(-1, -1, +1, +1)*/); // redraw old selection area
            d->m_selection.endElem = element;
            d->updateSelection();
            redrawRects.append(d->m_selection.boundingRect().toAlignedRect());
        }
        d->m_selection.isSelecting = true;
    }
    litehtml::position::vector redrawBoxes;
    if (d->m_document->on_mouse_over(
            documentPos.x(), documentPos.y(), viewportPos.x(), viewportPos.y(), redrawBoxes)) {
        for (const litehtml::position &box : redrawBoxes)
            redrawRects.append(toQRect(box));
    }
    return redrawRects;
}

QVector<QRect> DocumentContainer::mouseReleaseEvent(const QPoint &documentPos,
                                                    const QPoint &viewportPos,
                                                    Qt::MouseButton button)
{
    if (!d->m_document || button != Qt::LeftButton)
        return {};
    QVector<QRect> redrawRects;
    // selection
    d->m_selection.isSelecting = false;
    d->m_selection.selectionStartDocumentPos = {};
    if (d->m_selection.isValid())
        d->m_blockLinks = true;
    else
        d->clearSelection();
    litehtml::position::vector redrawBoxes;
    if (d->m_document->on_lbutton_up(
            documentPos.x(), documentPos.y(), viewportPos.x(), viewportPos.y(), redrawBoxes)) {
        for (const litehtml::position &box : redrawBoxes)
            redrawRects.append(toQRect(box));
    }
    d->m_blockLinks = false;
    if (!d->m_linkActivated && !d->m_selection.isValid())
        redrawRects += d->toggleAnimationAt(documentPos);
    d->m_linkActivated = false;
    return redrawRects;
}

bool DocumentContainer::isAnimationAt(const QPoint &documentPos) const
{
    return d->animationAt(documentPos) != nullptr;
}

QVector<QRect> DocumentContainer::mouseDoubleClickEvent(const QPoint &documentPos,
                                                        const QPoint &viewportPos,
                                                        Qt::MouseButton button)
{
    if (!d->m_document || button != Qt::LeftButton)
        return {};
    QVector<QRect> redrawRects;
    d->clearSelection();
    d->m_selection.mode = Selection::Mode::Word;
    const Selection::Element element = selection_element_at_point(d->m_document->root(),
                                                                  documentPos,
                                                                  viewportPos,
                                                                  d->m_selection.mode);
    if (element.element) {
        d->m_selection.startElem = element;
        d->m_selection.endElem = d->m_selection.startElem;
        d->m_selection.isSelecting = true;
        d->updateSelection();
        if (d->m_selection.isValid())
            redrawRects.append(d->m_selection.boundingRect().toAlignedRect());
    } else {
        if (d->m_selection.isValid())
            redrawRects.append(d->m_selection.boundingRect().toAlignedRect());
        d->clearSelection();
    }
    return redrawRects;
}

QVector<QRect> DocumentContainer::leaveEvent()
{
    if (!d->m_document)
        return {};
    litehtml::position::vector redrawBoxes;
    if (d->m_document->on_mouse_leave(redrawBoxes)) {
        QVector<QRect> redrawRects;
        for (const litehtml::position &box : redrawBoxes)
            redrawRects.append(toQRect(box));
        return redrawRects;
    }
    return {};
}

QUrl DocumentContainer::linkAt(const QPoint &documentPos, const QPoint &viewportPos)
{
    if (!d->m_document)
        return {};
    const char *href = nullptr;
    deepest_child_at_point(d->m_document->root(),
                           documentPos,
                           viewportPos,
                           [&href](const litehtml::element::ptr &e) {
                               for (litehtml::element::ptr current = e; current;
                                    current = current->parent()) {
                                   if (current->tag() == litehtml::_a_) {
                                       href = current->get_attr("href");
                                       if (href)
                                           return true;
                                   }
                               }
                               return false; /*continue*/
                           });
    if (href)
        return d->resolveUrl(QString::fromUtf8(href), d->m_baseUrl);
    return {};
}

QUrl DocumentContainer::imageAt(const QPoint &documentPos,
                                const QPoint &viewportPos) const
{
    if (!d->m_document)
        return {};
    const char *src = nullptr;
    deepest_child_at_point(d->m_document->root(),
                           documentPos,
                           viewportPos,
                           [&src](const litehtml::element::ptr &e) {
                               if (e->tag() != litehtml::_img_)
                                   return false;
                               src = e->get_attr("src");
                               return src != nullptr;
                           });
    if (src)
        return d->resolveUrl(QString::fromUtf8(src), d->m_baseUrl);
    return {};
}

QString DocumentContainer::caption() const
{
    return d->m_caption;
}

QString DocumentContainer::selectedText() const
{
    return d->m_selection.text;
}

void DocumentContainer::clearSelection()
{
    d->clearSelection();
}

void DocumentContainer::findText(const QString &text,
                                 QTextDocument::FindFlags flags,
                                 bool incremental,
                                 bool *wrapped,
                                 bool *success,
                                 QVector<QRect> *oldSelection,
                                 QVector<QRect> *newSelection)
{
    if (success)
        *success = false;
    if (oldSelection)
        oldSelection->clear();
    if (newSelection)
        newSelection->clear();
    if (!d->m_document)
        return;
    const bool backward = flags & QTextDocument::FindBackward;
    int startIndex = backward ? -1 : 0;
    if (d->m_selection.startElem.element && d->m_selection.endElem.element) { // selection
        // poor-man's incremental search starts at beginning of selection,
        // non-incremental at end (forward search) or beginning (backward search)
        Selection::Element start;
        Selection::Element end;
        std::tie(start, end) = getStartAndEnd(d->m_selection.startElem, d->m_selection.endElem);
        Selection::Element searchStart;
        if (incremental || backward) {
            if (start.index < 0) // fully selected
                searchStart = {firstLeaf(start.element, nullptr), 0, -1};
            else
                searchStart = start;
        } else {
            if (end.index < 0) // fully selected
                searchStart = {nextLeaf(end.element, nullptr), 0, -1};
            else
                searchStart = end;
        }
        const auto findInIndex = d->m_index.elementToIndex.find(searchStart.element);
        if (findInIndex == std::end(d->m_index.elementToIndex)) {
            qWarning() << "internal error: cannot find litehmtl element in index";
            return;
        }
        startIndex = findInIndex->second + searchStart.index;
        if (backward)
            --startIndex;
    }

    const auto fillXPos = [](const Selection::Element &e) {
        std::string ttext;
        e.element->get_text(ttext);
        const QString text = QString::fromStdString(ttext);
        const auto fontPtr = e.element->css().get_font();
        if (!fontPtr)
            return e;
        const QFont &font = toQFont(fontPtr);
        const QFontMetricsF fm(font);
        return Selection::Element{e.element, e.index, fm.size(0, text.left(e.index)).width()};
    };

    QString term = QRegularExpression::escape(text);
    if (flags & QTextDocument::FindWholeWords)
        term = QString("\\b%1\\b").arg(term);
    const QRegularExpression::PatternOptions patternOptions
        = (flags & QTextDocument::FindCaseSensitively) ? QRegularExpression::NoPatternOption
                                                       : QRegularExpression::CaseInsensitiveOption;
    const QRegularExpression expression(term, patternOptions);

    int foundIndex = backward ? d->m_index.text.lastIndexOf(expression, startIndex)
                              : d->m_index.text.indexOf(expression, startIndex);
    if (foundIndex < 0) { // wrap
        foundIndex = backward ? d->m_index.text.lastIndexOf(expression)
                              : d->m_index.text.indexOf(expression);
        if (wrapped && foundIndex >= 0)
            *wrapped = true;
    }
    if (foundIndex >= 0) {
        const Index::Entry startEntry = d->m_index.findElement(foundIndex);
        const Index::Entry endEntry = d->m_index.findElement(foundIndex + text.size());
        if (!startEntry.second || !endEntry.second) {
            qWarning() << "internal error: search ended up with nullptr elements";
            return;
        }
        if (oldSelection) {
            for (const QRectF &r : std::as_const(d->m_selection.selection))
                oldSelection->append(r.toAlignedRect());
        }
        d->clearSelection();
        d->m_selection.startElem = fillXPos({startEntry.second, foundIndex - startEntry.first, -1});
        d->m_selection.endElem = fillXPos(
            {endEntry.second, int(foundIndex + text.size() - endEntry.first), -1});
        d->updateSelection();
        if (newSelection) {
            for (const QRectF &r : std::as_const(d->m_selection.selection))
                newSelection->append(r.toAlignedRect());
        }
        if (success)
            *success = true;
        return;
    }
    return;
}

void DocumentContainer::setDefaultFont(const QFont &font)
{
    d->m_defaultFont = font;
    d->m_defaultFontFamilyName = d->m_defaultFont.family().toUtf8();
    // Since font family name and size are read only once, when parsing html,
    // we need to trigger the reparse of this info.
    if (d->m_document && d->m_document->root()) {
        d->m_document->root()->refresh_styles();
        d->m_document->root()->compute_styles();
    }
}

QFont DocumentContainer::defaultFont() const
{
    return d->m_defaultFont;
}

void DocumentContainer::setAntialias(bool on)
{
    d->m_antialias = on;
}

bool DocumentContainer::antialias() const
{
    return d->m_antialias;
}

void DocumentContainer::setDataCallback(const DocumentContainer::DataCallback &callback)
{
    d->m_dataCallback = callback;
}

DocumentContainer::DataCallback DocumentContainer::dataCallback() const
{
    return d->m_dataCallback;
}

void DocumentContainer::setPixmapCallBack(const DocumentContainer::PixmapCallBack &callback)
{
    d->pixmap_callback_ = callback;
}

DocumentContainer::PixmapCallBack DocumentContainer::pixmapCallBack() const
{
    return d->pixmap_callback_;
}

void DocumentContainer::setCursorCallback(const DocumentContainer::CursorCallback &callback)
{
    d->m_cursorCallback = callback;
}

void DocumentContainer::setLinkCallback(const DocumentContainer::LinkCallback &callback)
{
    d->m_linkCallback = callback;
}

void DocumentContainer::setPaletteCallback(const DocumentContainer::PaletteCallback &callback)
{
    d->m_paletteCallback = callback;
}

DocumentContainer::PaletteCallback DocumentContainer::paletteCallback() const
{
    return d->m_paletteCallback;
}

void DocumentContainer::setClipboardCallback(const DocumentContainer::ClipboardCallback &callback)
{
    d->m_clipboardCallback = callback;
}

void DocumentContainer::setRepaintCallback(const DocumentContainer::RepaintCallback &callback)
{
    d->m_repaintCallback = callback;
}

static litehtml::element::ptr elementForY(int y, const litehtml::element::ptr &element)
{
    if (!element)
        return {};
    if (element->get_placement().y >= y)
        return element;
    for (const litehtml::element::ptr &child : element->children()) {
        litehtml::element::ptr result = elementForY(y, child);
        if (result)
            return result;
    }
    return {};
}

static litehtml::element::ptr elementForY(int y, const litehtml::document::ptr &document)
{
    if (!document)
        return {};

    return elementForY(y, document->root());
}

int DocumentContainer::withFixedElementPosition(int y, const std::function<void()> &action)
{
    const litehtml::element::ptr element = elementForY(y, d->m_document);
    action();
    if (element)
        return qRound(element->get_placement().y);
    return -1;
}

QPixmap DocumentContainerPrivate::getPixmap(const QUrl &url)
{
    if (!m_pixmaps.contains(url)) {
        qWarning(log) << "getPixmap: pixmap not loaded for" << url;
        return {};
    }
    return m_pixmaps.value(url);
}

QPixmap DocumentContainerPrivate::getPixmap(const QString &imageUrl, const QString &baseUrl)
{
    return getPixmap(resolveUrl(imageUrl, baseUrl));
}

// Returns the pixmap to draw at documentRect, which is the current frame for animated images.
QPixmap DocumentContainerPrivate::getImageFrame(const QUrl &url, const QRect &documentRect)
{
    if (AnimatedImage *animation = animationForUrl(url)) {
        animationDrawn(animation, documentRect);
        if (animation->started) {
            const QPixmap frame = animation->movie->currentPixmap();
            if (!frame.isNull())
                return frame;
        }
    }
    return getPixmap(url);
}

QPixmap DocumentContainerPrivate::getImageFrame(const QString &imageUrl,
                                                const QString &baseUrl,
                                                const QRect &documentRect)
{
    return getImageFrame(resolveUrl(imageUrl, baseUrl), documentRect);
}

QString DocumentContainerPrivate::serifFont() const
{
    // TODO make configurable
    return {"Times New Roman"};
}

QString DocumentContainerPrivate::sansSerifFont() const
{
    // TODO make configurable
    return {"Arial"};
}

QString DocumentContainerPrivate::monospaceFont() const
{
    // TODO make configurable
    return {"Courier"};
}

QUrl DocumentContainerPrivate::resolveUrl(const QString &url, const QString &baseUrl) const
{
    // several cases:
    // full url: "https://foo.bar/blah.css"
    // relative path: "foo/bar.css"
    // server relative path: "/foo/bar.css"
    // net path: "//foo.bar/blah.css"
    // fragment only: "#foo-fragment"
    const QUrl qurl = QUrl::fromEncoded(url.toUtf8());
    if (qurl.scheme().isEmpty()) {
        if (url.startsWith('#')) // leave alone if just a fragment
            return qurl;
        const QUrl pageBaseUrl = QUrl(baseUrl.isEmpty() ? m_baseUrl : baseUrl);
        if (url.startsWith("//")) // net path
            return QUrl(pageBaseUrl.scheme() + ":" + url);
        QUrl serverUrl = QUrl(pageBaseUrl);
        serverUrl.setPath("");
        const QString actualBaseUrl = url.startsWith('/')
                                          ? serverUrl.toString(QUrl::FullyEncoded)
                                          : pageBaseUrl.toString(QUrl::FullyEncoded);
        QUrl resolvedUrl(actualBaseUrl + '/' + url);
        resolvedUrl.setPath(resolvedUrl.path(QUrl::FullyEncoded | QUrl::NormalizePathSegments), QUrl::TolerantMode);
        return resolvedUrl;
    }
    return qurl;
}

Index::Entry Index::findElement(int index) const
{
    const auto upper = std::upper_bound(std::begin(indexToElement),
                                        std::end(indexToElement),
                                        Entry{index, {}},
                                        [](const Entry &a, const Entry &b) {
                                            return a.first < b.first;
                                        });
    if (upper == std::begin(indexToElement)) // should not happen for index >= 0
        return {-1, {}};
    return *(upper - 1);
}

DocumentContainerContext::DocumentContainerContext()
    : d(new DocumentContainerContextPrivate)
{}

DocumentContainerContext::~DocumentContainerContext() = default;

void DocumentContainerContext::setMasterStyleSheet(const QString &css)
{
    d->masterCss = css;
}

QT_END_NAMESPACE
