// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "qlitehtml_global.h"

#include <QAbstractScrollArea>
#include <QElapsedTimer>
#include <QPoint>
#include <QPixmap>
#include <QTextDocument>
#include <QUrl>

QT_FORWARD_DECLARE_CLASS(QPrinter)

#include <functional>

QT_BEGIN_NAMESPACE

class QLiteHtmlWidgetPrivate;

class QLITEHTML_EXPORT QLiteHtmlWidget : public QAbstractScrollArea
{
    Q_OBJECT
public:
    explicit QLiteHtmlWidget(QWidget *parent = nullptr);
    ~QLiteHtmlWidget() override;

    // declaring the getters Q_INVOKABLE to make them Squish-testable
    void setUrl(const QUrl &url);
    Q_INVOKABLE QUrl url() const;
    void setHtml(const QString &content);
    Q_INVOKABLE QString html() const;
    Q_INVOKABLE QString title() const;

    void setZoomFactor(qreal scale);
    qreal zoomFactor() const;
    // Changes the zoom factor without rendering. The next setHtml() lays out
    // with the new factor, so swapping documents costs no extra layout.
    void setZoomFactorWithoutRender(qreal scale);

    // Number of full litehtml layouts performed so far; used by tests and by
    // clients that need to know whether cached document geometry is stale.
    Q_INVOKABLE int layoutCount() const;
    // Document height in virtual (unzoomed) pixels; 0 when there is no
    // document.
    Q_INVOKABLE int documentHeight() const;

    bool findText(const QString &text,
                  QTextDocument::FindFlags flags,
                  bool incremental,
                  bool *wrapped = nullptr);

    void setDefaultFont(const QFont &font);
    QFont defaultFont() const;
    void setAntialias(bool on);

    void scrollToAnchor(const QString &name);
    // Document Y coordinate of the named anchor, or -1 if not found.
    // Same semantics as DocumentContainer::anchorY().
    int anchorY(const QString &name) const;

    using ResourceHandler = std::function<QByteArray(QUrl)>;
    void setResourceHandler(const ResourceHandler &handler);

    using PixmapCallBack = std::function<QPixmap(const QUrl &)>;
    void setPixmapCallBack(const PixmapCallBack &callback);

    // declaring this Q_INVOKABLE to make it Squish-testable
    Q_INVOKABLE QString selectedText() const;
    void clearSelection();

    // Hit test in viewport coordinates. Returns an empty QUrl when the point
    // does not land on an image.
    QUrl imageAt(const QPoint &viewportPos) const;

    void print(QPrinter *printer);

signals:
    void linkClicked(const QUrl &url);
    void linkHighlighted(const QUrl &url);
    void copyAvailable(bool available);
    void contextMenuRequested(const QPoint &pos, const QUrl &url);
    void imageClicked(const QUrl &url);
    void imageContextMenuRequested(const QPoint &globalPos, const QUrl &url);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool viewportEvent(QEvent *event) override;

private:
    void updateHightlightedLink();
    void setHightlightedLink(const QUrl &url);
    void withFixedTextPosition(const std::function<void()> &action);
    void render();
    int layoutWidth() const;
    void updateScrollBars();
    QPoint scrollPosition() const;
    void htmlPos(const QPoint &pos, QPoint *viewportPos, QPoint *htmlPos) const;
    QPoint toVirtual(const QPoint &p) const;
    QSize toVirtual(const QSize &s) const;
    QRect toVirtual(const QRect &r) const;
    QRect fromVirtual(const QRect &r) const;

    QLiteHtmlWidgetPrivate *d;
    QPoint m_imagePressPos;
    QUrl m_imagePressUrl;
    QElapsedTimer m_lastImageClick;
    QUrl m_lastImageClickUrl;
};

QT_END_NAMESPACE
