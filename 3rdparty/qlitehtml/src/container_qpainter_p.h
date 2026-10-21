// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "container_qpainter.h"

#include <litehtml.h>

#include <QBuffer>
#include <QByteArray>
#include <QElapsedTimer>
#include <QMovie>
#include <QObject>
#include <QPaintDevice>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>

#include <litehtml/types.h>
#include <memory>
#include <unordered_map>

QT_BEGIN_NAMESPACE

class Selection
{
public:
    struct Element
    {
        litehtml::element::ptr element;
        int index = -1;
        qreal x = -1;
    };

    enum class Mode { Free, Word };

    bool isValid() const;

    void update();
    QRectF boundingRect() const;

    Element startElem;
    Element endElem;
    QVector<QRectF> selection;
    QString text;

    QPoint selectionStartDocumentPos;
    Mode mode = Mode::Free;
    bool isSelecting = false;
};

// An image that has more than one frame, e.g. an animated GIF or WebP.
// The frames are decoded on demand by QMovie, which needs the encoded data to stay alive.
class AnimatedImage
{
public:
    QByteArray data; // must outlive buffer, which must outlive movie
    std::unique_ptr<QBuffer> buffer;
    std::unique_ptr<QMovie> movie;

    bool playing = false;
    bool started = false;

    // areas in document space where the image was drawn, used for repainting it
    QVector<QRect> rects;
    // runs while a frame change was advertised via the repaint callback but the image was not
    // drawn since. If that takes too long, nobody is interested in the animation (scrolled out
    // of view, widget hidden, ...) and it is paused until the image is drawn again.
    QElapsedTimer pendingRepaint;
};

// std::hash has no specialization for QUrl, so unordered containers need an explicit hasher
struct QUrlHash
{
    std::size_t operator()(const QUrl &url) const noexcept { return qHash(url); }
};

struct Index
{
    QString text;
    // only contains leaf elements
    std::unordered_map<litehtml::element::ptr, int> elementToIndex;

    using Entry = std::pair<int, litehtml::element::ptr>;
    std::vector<Entry> indexToElement;

    Entry findElement(int index) const;
};

class DocumentContainerPrivate final : public litehtml::document_container
{
public: // document_container API
    litehtml::uint_ptr create_font(const litehtml::font_description &descr,
                                   const litehtml::document *doc,
                                   litehtml::font_metrics *fm) override;
    void delete_font(litehtml::uint_ptr hFont) override;
    litehtml::pixel_t text_width(const char *text, litehtml::uint_ptr hFont) override;
    void draw_text(litehtml::uint_ptr hdc,
                   const char *text,
                   litehtml::uint_ptr hFont,
                   litehtml::web_color color,
                   const litehtml::position &pos) override;
    litehtml::pixel_t pt_to_px(float pt) const override;
    litehtml::pixel_t get_default_font_size() const override;
    const char *get_default_font_name() const override;
    void draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker &marker) override;
    void load_image(const char *src, const char *baseurl, bool redraw_on_ready) override;
    void get_image_size(const char *src, const char *baseurl, litehtml::size &sz) override;
    void draw_image(litehtml::uint_ptr hdc,
                    const litehtml::background_layer &layer,
                    const std::string &url,
                    const std::string &base_url) override;
    void draw_solid_fill(litehtml::uint_ptr hdc,
                         const litehtml::background_layer &layer,
                         const litehtml::web_color &color) override;
    void draw_linear_gradient(litehtml::uint_ptr hdc,
                               const litehtml::background_layer &layer,
                               const litehtml::background_layer::linear_gradient &gradient) override;
    void draw_radial_gradient(litehtml::uint_ptr hdc,
                               const litehtml::background_layer &layer,
                               const litehtml::background_layer::radial_gradient &gradient) override;
    void draw_conic_gradient(litehtml::uint_ptr hdc,
                              const litehtml::background_layer &layer,
                              const litehtml::background_layer::conic_gradient &gradient) override;
    void draw_borders(litehtml::uint_ptr hdc,
                      const litehtml::borders &borders,
                      const litehtml::position &draw_pos,
                      bool root) override;
    void set_caption(const char *caption) override;
    void set_base_url(const char *base_url) override;
    void link(const std::shared_ptr<litehtml::document> &doc,
              const litehtml::element::ptr &el) override;
    void on_anchor_click(const char *url, const litehtml::element::ptr &el) override;
    void on_mouse_event(const litehtml::element::ptr &el, litehtml::mouse_event event) override;
    void set_cursor(const char *cursor) override;
    void transform_text(litehtml::string &text, litehtml::text_transform tt) override;
    void import_css(litehtml::string &text, const litehtml::string &url, litehtml::string &baseurl) override;
    void set_clip(const litehtml::position &pos,
                  const litehtml::border_radiuses &bdr_radius) override;
    void del_clip() override;
    void get_viewport(litehtml::position &viewport) const override;
    std::shared_ptr<litehtml::element> create_element(
        const char *tag_name,
        const litehtml::string_map &attributes,
        const std::shared_ptr<litehtml::document> &doc) override;
    void get_media_features(litehtml::media_features &media) const override;
    void get_language(litehtml::string &language, litehtml::string &culture) const override;

    QPixmap getPixmap(const QUrl &url);
    QPixmap getPixmap(const QString &imageUrl, const QString &baseUrl);
    QPixmap getImageFrame(const QString &imageUrl,
                          const QString &baseUrl,
                          const QRect &documentRect);
    QPixmap getImageFrame(const QUrl &url, const QRect &documentRect);
    void loadAnimation(const QUrl &url, const QByteArray &data);
    AnimatedImage *animationForUrl(const QUrl &url) const;
    AnimatedImage *animationAt(const QPoint &documentPos) const;
    QVector<QRect> toggleAnimationAt(const QPoint &documentPos);
    void animationDrawn(AnimatedImage *animation, const QRect &documentRect);
    void animationFrameChanged(const QUrl &url);
    void drawAnimationIndicator(QPainter *painter, const QRectF &rect) const;
    QString serifFont() const;
    QString sansSerifFont() const;
    QString monospaceFont() const;
    QUrl resolveUrl(const QString &url, const QString &baseUrl) const;
    void drawSelection(QPainter *painter, const QRect &clip) const;
    void buildIndex();
    void updateSelection();
    void clearSelection();

    QPaintDevice *m_paintDevice = nullptr;
    litehtml::document::ptr m_document;
    litehtml::media_type mediaType = litehtml::media_type_screen;
    Index m_index;
    QString m_baseUrl;
    QRect m_clientRect;
    QPoint m_scrollPosition;
    QString m_caption;
    QFont m_defaultFont = QFont(sansSerifFont(), 16);
    QByteArray m_defaultFontFamilyName = m_defaultFont.family().toUtf8();
    bool m_antialias = true;
    QHash<QUrl, QPixmap> m_pixmaps;
    std::unordered_map<QUrl, std::unique_ptr<AnimatedImage>, QUrlHash> m_animations;
    // context object for the QMovie connections, so they die with this container
    QObject m_animationContext;
    Selection m_selection;
    DocumentContainer::DataCallback m_dataCallback;
    DocumentContainer::PixmapCallBack pixmap_callback_;
    DocumentContainer::CursorCallback m_cursorCallback;
    DocumentContainer::LinkCallback m_linkCallback;
    DocumentContainer::PaletteCallback m_paletteCallback;
    DocumentContainer::ClipboardCallback m_clipboardCallback;
    DocumentContainer::RepaintCallback m_repaintCallback;
    bool m_blockLinks = false;
    bool m_linkActivated = false;
};

class DocumentContainerContextPrivate
{
public:
    QString masterCss;
};

QT_END_NAMESPACE
