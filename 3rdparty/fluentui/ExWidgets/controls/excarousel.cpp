#include "excarousel.h"
#include "exfonticon.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QGraphicsOpacityEffect>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QToolButton>
#include <QVariantAnimation>
#include <QWheelEvent>

#if QT_VERSION >= QT_VERSION_CHECK( 6, 0, 0 )
#    include <QEnterEvent>
#endif

namespace {
constexpr int kDefaultWidth          = 560;
constexpr int kDefaultHeight         = 260;
constexpr int kMinWidth              = 160;
constexpr int kMinHeight             = 96;
constexpr int kButtonSize            = 36;
constexpr int kButtonMargin          = 12;
constexpr int kPipsHeight            = 20;
constexpr int kPipsBottom            = 10;
constexpr int kPipSlotWidth          = 16;
constexpr qreal kPipNormalDiameter   = 6.0;
constexpr qreal kPipHoverDiameter    = 7.0;
constexpr qreal kPipSelectedDiameter = 8.0;
constexpr int kDefaultInterval       = 4000;
// 舒缓优雅的轮播滑动时长（550ms），对齐 ElaPromotionView 的 650ms 动效呼吸感
constexpr int kDefaultDuration       = 550;
constexpr qreal kDefaultCornerRadius = 8.0;

int wrapIndex( int index, int count )
{
    if ( count <= 0 )
    {
        return 0;
    }
    const int m = index % count;
    return m < 0 ? m + count : m;
}

inline QPoint getEventPos( const QMouseEvent* event )
{
#if QT_VERSION >= QT_VERSION_CHECK( 6, 0, 0 )
    return event->position().toPoint();
#else
    return event->pos();
#endif
}

inline QColor resolveAccentColor( const QWidget* w )
{
    if ( qApp )
    {
        const QVariant accentVar = qApp->property( "_q_accent_color" );
        if ( accentVar.isValid() && accentVar.canConvert<QColor>() )
        {
            const QColor col = accentVar.value<QColor>();
            if ( col.isValid() )
            {
                return col;
            }
        }
    }
    if ( w )
    {
        const QPalette pal = w->palette();
#if QT_VERSION >= QT_VERSION_CHECK( 6, 6, 0 )
        const QColor acc = pal.color( QPalette::Active, QPalette::Accent );
        if ( acc.isValid() )
        {
            return acc;
        }
#endif
        const QColor hl = pal.color( QPalette::Active, QPalette::Highlight );
        if ( hl.isValid() )
        {
            return hl;
        }
    }
    return QColor( 0, 120, 215 );
}

class CarouselImageSlide : public QWidget
{
public:
    explicit CarouselImageSlide( const QPixmap& pixmap,
                                 const QString& title           = QString(),
                                 const QString& subtitle        = QString(),
                                 Qt::AspectRatioMode aspectMode = Qt::KeepAspectRatioByExpanding,
                                 QWidget* parent                = nullptr )
        : QWidget( parent )
        , m_pixmap( pixmap )
        , m_title( title )
        , m_subtitle( subtitle )
        , m_aspectMode( aspectMode )
    { setAttribute( Qt::WA_OpaquePaintEvent, false ); }

    void setPixmap( const QPixmap& pixmap )
    {
        m_pixmap = pixmap;
        update();
    }

    void setTitle( const QString& title )
    {
        m_title = title;
        update();
    }

    void setSubtitle( const QString& subtitle )
    {
        m_subtitle = subtitle;
        update();
    }

    void setBorderRadius( qreal radius )
    {
        if ( qFuzzyCompare( m_borderRadius, radius ) )
        {
            return;
        }
        m_borderRadius = radius;
        update();
    }

protected:
    void paintEvent( QPaintEvent* ) override
    {
        QPainter painter( this );
        painter.setRenderHints( QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing );

        const QRectF r = rect();

        // 自身圆角剪裁（静态展示与快照生成时保持圆角）
        if ( m_borderRadius > 0.0 )
        {
            QPainterPath clip;
            clip.addRoundedRect( r, m_borderRadius, m_borderRadius );
            painter.setClipPath( clip );
        }

        if ( !m_pixmap.isNull() )
        {
            const qreal dpr         = devicePixelRatioF();
            const QSizeF targetSize = r.size() * dpr;
            const QSize pixSize     = m_pixmap.size();

            if ( m_aspectMode == Qt::KeepAspectRatioByExpanding )
            {
                const qreal scaleW = targetSize.width() / pixSize.width();
                const qreal scaleH = targetSize.height() / pixSize.height();
                const qreal scale  = qMax( scaleW, scaleH );
                const qreal drawW  = ( pixSize.width() * scale ) / dpr;
                const qreal drawH  = ( pixSize.height() * scale ) / dpr;
                const qreal drawX  = r.left() + ( r.width() - drawW ) / 2.0;
                const qreal drawY  = r.top() + ( r.height() - drawH ) / 2.0;
                painter.drawPixmap( QRectF( drawX, drawY, drawW, drawH ), m_pixmap, QRectF( m_pixmap.rect() ) );
            }
            else
            {
                const QSize scaled = pixSize.scaled( targetSize.toSize(), m_aspectMode );
                const qreal drawW  = scaled.width() / dpr;
                const qreal drawH  = scaled.height() / dpr;
                const qreal drawX  = r.left() + ( r.width() - drawW ) / 2.0;
                const qreal drawY  = r.top() + ( r.height() - drawH ) / 2.0;
                painter.drawPixmap( QRectF( drawX, drawY, drawW, drawH ), m_pixmap, QRectF( m_pixmap.rect() ) );
            }
        }
        else
        {
            painter.fillRect( r, palette().color( QPalette::Window ) );
        }

        // 底部图文渐变暗色遮罩
        if ( !m_title.isEmpty() || !m_subtitle.isEmpty() )
        {
            const int bannerHeight = m_subtitle.isEmpty() ? 56 : 76;
            const QRectF bannerRect( r.left(), r.bottom() - bannerHeight + 1, r.width(), bannerHeight );

            QLinearGradient grad( bannerRect.topLeft(), bannerRect.bottomLeft() );
            grad.setColorAt( 0.0, QColor( 0, 0, 0, 0 ) );
            grad.setColorAt( 0.4, QColor( 0, 0, 0, 110 ) );
            grad.setColorAt( 1.0, QColor( 0, 0, 0, 175 ) );
            painter.fillRect( bannerRect, grad );

            painter.setPen( Qt::white );
            const int textLeft  = 20;
            const int textRight = 20;
            const int textWidth = qMax( 10, int( r.width() ) - textLeft - textRight );

            if ( m_subtitle.isEmpty() )
            {
                QFont titleFont = font();
                titleFont.setBold( true );
                titleFont.setPixelSize( 16 );
                painter.setFont( titleFont );
                painter.drawText( QRect( textLeft, int( r.bottom() ) - 40, textWidth, 28 ),
                                  Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                                  m_title );
            }
            else
            {
                QFont titleFont = font();
                titleFont.setBold( true );
                titleFont.setPixelSize( 16 );
                painter.setFont( titleFont );
                painter.drawText( QRect( textLeft, int( r.bottom() ) - 56, textWidth, 24 ),
                                  Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                                  m_title );

                QFont subFont = font();
                subFont.setPixelSize( 12 );
                painter.setFont( subFont );
                painter.setPen( QColor( 240, 240, 240, 220 ) );
                painter.drawText( QRect( textLeft, int( r.bottom() ) - 32, textWidth, 20 ),
                                  Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                                  m_subtitle );
            }
        }
    }

private:
    QPixmap m_pixmap;
    QString m_title;
    QString m_subtitle;
    Qt::AspectRatioMode m_aspectMode;
    qreal m_borderRadius = kDefaultCornerRadius;
};

class CarouselNavButton : public QToolButton
{
public:
    using QToolButton::QToolButton;

protected:
    void paintEvent( QPaintEvent* ) override
    {
        QPainter painter( this );
        painter.setRenderHint( QPainter::Antialiasing );

        QRectF r = QRectF( rect() ).adjusted( 1.0, 1.0, -1.0, -1.0 );
        if ( pressed )
        {
            r.adjust( 1.0, 1.0, -1.0, -1.0 );
        }

        QColor fill = palette().color( QPalette::Button );
        int alpha   = 190;
        if ( pressed )
        {
            alpha = 245;
        }
        else if ( hovered )
        {
            alpha = 230;
        }
        fill.setAlpha( alpha );

        painter.setPen( QPen( palette().color( QPalette::Mid ), 1.0 ) );
        painter.setBrush( fill );
        painter.drawEllipse( r );

        painter.setFont( ExFontIcon::iconFont( 16 ) );
        painter.setPen( palette().color( QPalette::ButtonText ) );

        SegoeIcon::Type iconType = SegoeIcon::ChevronRight;
        switch ( arrowType() )
        {
            case Qt::LeftArrow :
                iconType = SegoeIcon::ChevronLeft;
                break;
            case Qt::RightArrow :
                iconType = SegoeIcon::ChevronRight;
                break;
            case Qt::UpArrow :
                iconType = SegoeIcon::ChevronUp;
                break;
            case Qt::DownArrow :
                iconType = SegoeIcon::ChevronDown;
                break;
            default :
                iconType = SegoeIcon::ChevronRight;
                break;
        }

        painter.drawText( r, Qt::AlignCenter, ExFontIcon::iconString( iconType ) );
    }

    void enterEvent(
#if QT_VERSION >= QT_VERSION_CHECK( 6, 0, 0 )
        QEnterEvent* event
#else
        QEvent* event
#endif
        ) override
    {
        QToolButton::enterEvent( event );
        hovered = true;
        update();
    }

    void leaveEvent( QEvent* event ) override
    {
        QToolButton::leaveEvent( event );
        hovered = false;
        pressed = false;
        update();
    }

    void mousePressEvent( QMouseEvent* event ) override
    {
        if ( event->button() == Qt::LeftButton )
        {
            pressed = true;
            update();
        }
        QToolButton::mousePressEvent( event );
    }

    void mouseReleaseEvent( QMouseEvent* event ) override
    {
        pressed = false;
        update();
        QToolButton::mouseReleaseEvent( event );
    }

private:
    bool hovered = false;
    bool pressed = false;
};

class CarouselPips : public QWidget
{
public:
    explicit CarouselPips( QWidget* parent = nullptr )
        : QWidget( parent )
    {
        setAttribute( Qt::WA_TransparentForMouseEvents, false );
        setMouseTracking( true );
        setSizePolicy( QSizePolicy::Minimum, QSizePolicy::Fixed );
        setCursor( Qt::PointingHandCursor );
    }

    void setCount( int value )
    {
        if ( m_count == value )
        {
            return;
        }
        m_count = qMax( 0, value );
        updateGeometry();
        update();
    }

    void setCurrent( int value )
    {
        if ( m_current == value )
        {
            return;
        }
        m_current = value;
        update();
    }

    QSize sizeHint() const override
    {
        if ( m_count <= 0 )
        {
            return QSize( 0, kPipsHeight );
        }
        return QSize( m_count * kPipSlotWidth, kPipsHeight );
    }

    int indexAt( const QPoint& pos ) const
    {
        if ( m_count <= 0 )
        {
            return -1;
        }
        const int totalWidth = m_count * kPipSlotWidth;
        const int startX     = ( width() - totalWidth ) / 2;
        if ( pos.x() < startX || pos.x() >= startX + totalWidth )
        {
            return -1;
        }
        if ( pos.y() < 0 || pos.y() > height() )
        {
            return -1;
        }
        const int idx = ( pos.x() - startX ) / kPipSlotWidth;
        if ( idx >= 0 && idx < m_count )
        {
            return idx;
        }
        return -1;
    }

protected:
    void mouseMoveEvent( QMouseEvent* event ) override
    {
        const int idx = indexAt( getEventPos( event ) );
        if ( m_hoverIndex != idx )
        {
            m_hoverIndex = idx;
            update();
        }
        QWidget::mouseMoveEvent( event );
    }

    void leaveEvent( QEvent* event ) override
    {
        if ( m_hoverIndex != -1 )
        {
            m_hoverIndex = -1;
            update();
        }
        QWidget::leaveEvent( event );
    }

    void changeEvent( QEvent* event ) override
    {
        QWidget::changeEvent( event );
        if ( event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange
             || event->type() == QEvent::StyleChange )
        {
            update();
        }
    }

    void paintEvent( QPaintEvent* ) override
    {
        if ( m_count <= 0 )
        {
            return;
        }
        QPainter painter( this );
        painter.setRenderHint( QPainter::Antialiasing );

        // 动态跟随当前主题强调色（联动全局与调色板）
        const QColor accent = resolveAccentColor( this );
        const bool isDark   = palette().color( QPalette::Window ).lightness() < 128;

        const int totalWidth = m_count * kPipSlotWidth;
        const qreal startX   = ( width() - totalWidth ) / 2.0;
        const qreal centerY  = height() / 2.0;

        for ( int i = 0; i < m_count; ++i )
        {
            const bool selected = ( i == m_current );
            const bool hovered  = ( i == m_hoverIndex );

            const qreal diameter = selected ? kPipSelectedDiameter : ( hovered ? kPipHoverDiameter : kPipNormalDiameter );
            const qreal radius   = diameter / 2.0;
            const qreal cx       = startX + i * kPipSlotWidth + ( kPipSlotWidth / 2.0 );

            QColor dotColor;
            if ( selected )
            {
                dotColor = accent;
            }
            else if ( hovered )
            {
                dotColor = isDark ? QColor( 255, 255, 255, 220 ) : QColor( 0, 0, 0, 180 );
            }
            else
            {
                dotColor = isDark ? QColor( 255, 255, 255, 120 ) : QColor( 0, 0, 0, 95 );
            }

            painter.setPen( Qt::NoPen );

            // 柔和微弱底层投影，确保在任何色彩明暗的背景图片上圆点轮廓清晰
            painter.setBrush( QColor( 0, 0, 0, 40 ) );
            painter.drawEllipse( QPointF( cx, centerY + 0.5 ), radius, radius );

            // WinUI 3 原生标准正圆形 Pip 点绘制（绝不变长为胶囊椭圆）
            painter.setBrush( dotColor );
            painter.drawEllipse( QPointF( cx, centerY ), radius, radius );
        }
    }

private:
    int m_count      = 0;
    int m_current    = 0;
    int m_hoverIndex = -1;
};

class CarouselViewport : public QWidget
{
public:
    explicit CarouselViewport( QWidget* parent = nullptr )
        : QWidget( parent )
    { setAttribute( Qt::WA_OpaquePaintEvent, false ); }

    void setBorderRadius( qreal radius )
    {
        if ( qFuzzyCompare( m_borderRadius, radius ) )
        {
            return;
        }
        m_borderRadius = radius;
        updateClipPath();
        update();
    }

    qreal borderRadius() const { return m_borderRadius; }

    void startTransition( const QPixmap& from, const QPixmap& to, int direction )
    {
        m_fromPix   = from;
        m_toPix     = to;
        m_direction = direction;
        m_progress  = 0.0;
        m_animating = true;
        update();
    }

    void setTransitionProgress( qreal progress )
    {
        m_progress = progress;
        update();
    }

    void endTransition()
    {
        m_animating = false;
        m_fromPix   = QPixmap();
        m_toPix     = QPixmap();
        m_progress  = 0.0;
        update();
    }

    bool isAnimating() const { return m_animating; }

protected:
    void resizeEvent( QResizeEvent* event ) override
    {
        QWidget::resizeEvent( event );
        updateClipPath();
        const QRect r           = rect();
        const auto childrenList = children();
        for ( QObject* obj : childrenList )
        {
            if ( auto* w = qobject_cast<QWidget*>( obj ) )
            {
                w->resize( r.size() );
            }
        }
    }

    void paintEvent( QPaintEvent* ) override
    {
        QPainter painter( this );
        painter.setRenderHints( QPainter::Antialiasing | QPainter::SmoothPixmapTransform );

        // 关键优化：使用预计算好的圆角路径，杜绝每一帧重复构造 QPainterPath 与软光栅求交，跑满显示器极限帧率
        if ( !m_clipPath.isEmpty() )
        {
            painter.setClipPath( m_clipPath );
        }

        if ( m_animating && !m_fromPix.isNull() && !m_toPix.isNull() )
        {
            const qreal p      = m_progress;
            const qreal w      = width();
            const qreal offset = p * w;

            // 彻底去除半透明重影虚化，对齐 ElaPromotionView 实体物理平移质感
            // 离开画面 (from)：自 0 滑向 -direction * w
            const qreal fromX = -m_direction * offset;
            painter.drawPixmap( QPointF( fromX, 0 ), m_fromPix );

            // 进入画面 (to)：自 direction * w 滑向 0
            const qreal toX = m_direction * ( w - offset );
            painter.drawPixmap( QPointF( toX, 0 ), m_toPix );
        }
        else
        {
            painter.setPen( Qt::NoPen );
            painter.setBrush( palette().color( QPalette::Base ) );
            if ( !m_clipPath.isEmpty() )
            {
                painter.fillPath( m_clipPath, painter.brush() );
            }
            else
            {
                painter.fillRect( rect(), painter.brush() );
            }
        }
    }

private:
    void updateClipPath()
    {
        m_clipPath = QPainterPath();
        if ( m_borderRadius > 0.0 && width() > 0 && height() > 0 )
        {
            m_clipPath.addRoundedRect( rect(), m_borderRadius, m_borderRadius );
        }
    }

    qreal m_borderRadius = kDefaultCornerRadius;
    QPainterPath m_clipPath;
    bool m_animating = false;
    qreal m_progress = 0.0;
    int m_direction  = 1;
    QPixmap m_fromPix;
    QPixmap m_toPix;
};
}  // namespace

class ExCarouselPrivate
{
public:
    explicit ExCarouselPrivate( ExCarousel* q )
        : q( q )
    {
    }

    void setupUi();
    void relayoutChrome();
    void placeIdle();
    void goTo( int index, int direction, int overrideDuration = -1 );
    void finishAnimation( int index );
    void restartTimer();
    void stopTimer();
    bool timerShouldRun() const;
    void updateChromeVisibility();
    int nextIndex( int from, int direction ) const;
    void applySlideBorderRadius();
    void animateButtonsOpacity( qreal targetOpacity );

    ExCarousel* q                      = nullptr;
    CarouselViewport* viewport         = nullptr;
    CarouselNavButton* prevButton      = nullptr;
    CarouselNavButton* nextButton      = nullptr;
    CarouselPips* pips                 = nullptr;
    QGraphicsOpacityEffect* prevEffect = nullptr;
    QGraphicsOpacityEffect* nextEffect = nullptr;
    QTimer* timer                      = nullptr;
    QList<QWidget*> slides;
    int currentIndex                               = -1;
    int pendingIndex                               = -1;
    int pendingDirection                           = 1;
    bool autoPlay                                  = true;
    int interval                                   = kDefaultInterval;
    bool wrap                                      = true;
    bool showNavigationButtons                     = true;
    bool showIndicators                            = true;
    int animationDuration                          = kDefaultDuration;
    bool pauseOnHover                              = true;
    bool hovered                                   = false;
    bool animating                                 = false;
    qreal borderRadius                             = kDefaultCornerRadius;
    ExCarousel::NavigationButtonTrigger navTrigger = ExCarousel::AlwaysVisible;
};

void ExCarouselPrivate::setupUi()
{
    viewport = new CarouselViewport( q );
    viewport->setObjectName( QStringLiteral( "exCarouselViewport" ) );
    viewport->setBorderRadius( borderRadius );
    viewport->installEventFilter( q );

    prevButton = new CarouselNavButton( q );
    prevButton->setObjectName( QStringLiteral( "exCarouselPrev" ) );
    prevButton->setArrowType( Qt::LeftArrow );
    prevButton->setCursor( Qt::PointingHandCursor );
    prevButton->setFocusPolicy( Qt::NoFocus );
    prevButton->setFixedSize( kButtonSize, kButtonSize );
    prevButton->setToolTip( ExCarousel::tr( "上一张" ) );
    prevEffect = new QGraphicsOpacityEffect( prevButton );
    prevButton->setGraphicsEffect( prevEffect );

    nextButton = new CarouselNavButton( q );
    nextButton->setObjectName( QStringLiteral( "exCarouselNext" ) );
    nextButton->setArrowType( Qt::RightArrow );
    nextButton->setCursor( Qt::PointingHandCursor );
    nextButton->setFocusPolicy( Qt::NoFocus );
    nextButton->setFixedSize( kButtonSize, kButtonSize );
    nextButton->setToolTip( ExCarousel::tr( "下一张" ) );
    nextEffect = new QGraphicsOpacityEffect( nextButton );
    nextButton->setGraphicsEffect( nextEffect );

    pips = new CarouselPips( q );
    pips->setObjectName( QStringLiteral( "exCarouselIndicators" ) );
    pips->installEventFilter( q );

    timer = new QTimer( q );
    timer->setTimerType( Qt::CoarseTimer );
    QObject::connect( timer, &QTimer::timeout, q, &ExCarousel::next );
    QObject::connect( prevButton, &QAbstractButton::clicked, q, &ExCarousel::previous );
    QObject::connect( nextButton, &QAbstractButton::clicked, q, &ExCarousel::next );

    q->setFocusPolicy( Qt::StrongFocus );
    q->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Preferred );
    updateChromeVisibility();
}

void ExCarouselPrivate::applySlideBorderRadius()
{
    for ( QWidget* s : slides )
    {
        if ( auto* imgSlide = dynamic_cast<CarouselImageSlide*>( s ) )
        {
            imgSlide->setBorderRadius( borderRadius );
        }
    }
}

void ExCarouselPrivate::relayoutChrome()
{
    const QRect r = q->rect();
    viewport->setGeometry( r );

    prevButton->move( r.left() + kButtonMargin, r.center().y() - kButtonSize / 2 );
    nextButton->move( r.right() - kButtonMargin - kButtonSize + 1, r.center().y() - kButtonSize / 2 );
    const QSize pipHint = pips->sizeHint();
    pips->setGeometry(
        ( r.width() - pipHint.width() ) / 2, r.bottom() - kPipsBottom - kPipsHeight + 1, qMax( pipHint.width(), 1 ), kPipsHeight );
    prevButton->raise();
    nextButton->raise();
    pips->raise();
}

void ExCarouselPrivate::placeIdle()
{
    relayoutChrome();

    const QRect vr = viewport->rect();
    for ( int i = 0; i < slides.size(); ++i )
    {
        QWidget* slide = slides.at( i );
        slide->setGeometry( vr );
        slide->setVisible( !animating && ( i == currentIndex ) );
    }
    pips->setCount( slides.size() );
    pips->setCurrent( currentIndex );
    updateChromeVisibility();
}

int ExCarouselPrivate::nextIndex( int from, int direction ) const
{
    if ( slides.isEmpty() )
    {
        return -1;
    }
    const int candidate = from + direction;
    if ( wrap )
    {
        return wrapIndex( candidate, slides.size() );
    }
    return qBound( 0, candidate, slides.size() - 1 );
}

void ExCarouselPrivate::goTo( int index, int direction, int overrideDuration )
{
    if ( slides.isEmpty() )
    {
        return;
    }
    index = wrap ? wrapIndex( index, slides.size() ) : qBound( 0, index, slides.size() - 1 );
    if ( index == currentIndex && !animating )
    {
        return;
    }

    if ( animating )
    {
        pendingIndex     = index;
        pendingDirection = direction;
        return;
    }

    const int dur = overrideDuration >= 0 ? overrideDuration : animationDuration;
    if ( currentIndex < 0 || dur <= 0 || !q->isVisible() || viewport->width() <= 0 || viewport->height() <= 0 )
    {
        currentIndex = index;
        placeIdle();
        Q_EMIT q->currentIndexChanged( currentIndex );
        restartTimer();
        return;
    }

    if ( direction == 0 )
    {
        direction = index >= currentIndex ? 1 : -1;
    }
    if ( wrap && currentIndex == slides.size() - 1 && index == 0 )
    {
        direction = 1;
    }
    else if ( wrap && currentIndex == 0 && index == slides.size() - 1 )
    {
        direction = -1;
    }

    QWidget* from      = slides.at( currentIndex );
    QWidget* to        = slides.at( index );
    const QSize vpSize = viewport->size();

    // 关键优化：使用 grab() 捕获控件当前的实际屏幕映射，自动精准对齐高 DPI 物理与逻辑尺寸，彻底根治尺寸跳动
    from->setGeometry( QRect( QPoint( 0, 0 ), vpSize ) );
    from->show();
    QPixmap fromPix = from->grab( QRect( QPoint( 0, 0 ), vpSize ) );

    to->setGeometry( QRect( QPoint( 0, 0 ), vpSize ) );
    to->show();
    QPixmap toPix = to->grab( QRect( QPoint( 0, 0 ), vpSize ) );

    from->hide();
    to->hide();

    viewport->startTransition( fromPix, toPix, direction );
    animating = true;

    prevButton->raise();
    nextButton->raise();
    pips->raise();

    // 采用 Qt 核心通用的 QVariantAnimation，零额外 MOC 依赖，完全兼容 Qt 5.14 ~ Qt 6.10
    auto* anim = new QVariantAnimation( q );
    anim->setDuration( dur );
    anim->setStartValue( qreal( 0.0 ) );
    anim->setEndValue( qreal( 1.0 ) );
    // 采用与 ElaPromotionView 完全一致的 QEasingCurve::OutCubic 三次缓出阻尼曲线
    anim->setEasingCurve( QEasingCurve::OutCubic );

    QObject::connect( anim,
                      &QVariantAnimation::valueChanged,
                      q,
                      [ this ]( const QVariant& value ) { viewport->setTransitionProgress( value.toReal() ); } );

    QObject::connect( anim,
                      &QVariantAnimation::finished,
                      q,
                      [ this, index, anim ]()
                      {
                          anim->deleteLater();
                          finishAnimation( index );
                      } );

    anim->start();
}

void ExCarouselPrivate::finishAnimation( int index )
{
    animating = false;
    viewport->endTransition();
    currentIndex = index;
    placeIdle();
    Q_EMIT q->currentIndexChanged( currentIndex );

    if ( pendingIndex >= 0 && pendingIndex != currentIndex )
    {
        const int next = pendingIndex;
        const int dir  = pendingDirection;
        pendingIndex   = -1;
        // 快速连续翻页时缩短过渡时长，体验更加跟手灵动
        goTo( next, dir, qMin( animationDuration, 180 ) );
        return;
    }
    pendingIndex = -1;
    restartTimer();
}

bool ExCarouselPrivate::timerShouldRun() const
{ return autoPlay && slides.size() > 1 && q->isVisible() && !( pauseOnHover && hovered ) && !animating; }

void ExCarouselPrivate::restartTimer()
{
    stopTimer();
    if ( timerShouldRun() )
    {
        timer->start( interval );
    }
}

void ExCarouselPrivate::stopTimer()
{ timer->stop(); }

void ExCarouselPrivate::animateButtonsOpacity( qreal targetOpacity )
{
    if ( prevEffect )
    {
        auto* anim = new QPropertyAnimation( prevEffect, "opacity", prevButton );
        anim->setDuration( 160 );
        anim->setStartValue( prevEffect->opacity() );
        anim->setEndValue( targetOpacity );
        anim->start( QAbstractAnimation::DeleteWhenStopped );
    }
    if ( nextEffect )
    {
        auto* anim = new QPropertyAnimation( nextEffect, "opacity", nextButton );
        anim->setDuration( 160 );
        anim->setStartValue( nextEffect->opacity() );
        anim->setEndValue( targetOpacity );
        anim->start( QAbstractAnimation::DeleteWhenStopped );
    }
}

void ExCarouselPrivate::updateChromeVisibility()
{
    const bool hasSlides = slides.size() > 1;
    const bool showBtns  = showNavigationButtons && hasSlides;
    prevButton->setVisible( showBtns );
    nextButton->setVisible( showBtns );
    pips->setVisible( showIndicators && hasSlides );

    if ( navTrigger == ExCarousel::OnHover )
    {
        const qreal target = hovered ? 1.0 : 0.0;
        if ( prevEffect )
        {
            prevEffect->setOpacity( target );
        }
        if ( nextEffect )
        {
            nextEffect->setOpacity( target );
        }
    }
    else
    {
        if ( prevEffect )
        {
            prevEffect->setOpacity( 1.0 );
        }
        if ( nextEffect )
        {
            nextEffect->setOpacity( 1.0 );
        }
    }

    prevButton->setEnabled( wrap || currentIndex > 0 );
    nextButton->setEnabled( wrap || currentIndex < slides.size() - 1 );
}

ExCarousel::ExCarousel( QWidget* parent )
    : QWidget( parent )
    , d( new ExCarouselPrivate( this ) )
{
    setObjectName( QStringLiteral( "exCarousel" ) );
    d->setupUi();
}

ExCarousel::~ExCarousel() = default;

int ExCarousel::addSlide( QWidget* widget )
{
    insertSlide( d->slides.size(), widget );
    return d->slides.size() - 1;
}

int ExCarousel::addPixmap( const QPixmap& pixmap )
{ return addPixmap( pixmap, QString(), QString(), Qt::KeepAspectRatioByExpanding ); }

int ExCarousel::addPixmap( const QPixmap& pixmap, const QString& title, const QString& subtitle, Qt::AspectRatioMode aspectMode )
{
    auto* slide = new CarouselImageSlide( pixmap, title, subtitle, aspectMode, d->viewport );
    slide->setBorderRadius( d->borderRadius );
    return addSlide( slide );
}

int ExCarousel::addImage( const QString& filePath, const QString& title, const QString& subtitle, Qt::AspectRatioMode aspectMode )
{
    QPixmap pix( filePath );
    return addPixmap( pix, title, subtitle, aspectMode );
}

void ExCarousel::insertSlide( int index, QWidget* widget )
{
    if ( !widget )
    {
        return;
    }
    index = qBound( 0, index, d->slides.size() );
    widget->setParent( d->viewport );
    widget->installEventFilter( this );
    if ( auto* imgSlide = dynamic_cast<CarouselImageSlide*>( widget ) )
    {
        imgSlide->setBorderRadius( d->borderRadius );
    }
    d->slides.insert( index, widget );
    if ( d->currentIndex < 0 )
    {
        d->currentIndex = 0;
    }
    else if ( index <= d->currentIndex )
    {
        d->currentIndex += 1;
    }
    d->placeIdle();
    d->restartTimer();
}

void ExCarousel::removeSlide( int index )
{
    QWidget* taken = takeSlide( index );
    delete taken;
}

QWidget* ExCarousel::slide( int index ) const
{
    if ( index < 0 || index >= d->slides.size() )
    {
        return nullptr;
    }
    return d->slides.at( index );
}

QWidget* ExCarousel::takeSlide( int index )
{
    if ( index < 0 || index >= d->slides.size() )
    {
        return nullptr;
    }
    QWidget* widget = d->slides.takeAt( index );
    widget->removeEventFilter( this );
    widget->setParent( nullptr );
    if ( d->slides.isEmpty() )
    {
        d->currentIndex = -1;
    }
    else if ( d->currentIndex >= d->slides.size() )
    {
        d->currentIndex = d->slides.size() - 1;
    }
    else if ( index < d->currentIndex )
    {
        d->currentIndex -= 1;
    }
    d->placeIdle();
    d->restartTimer();
    return widget;
}

int ExCarousel::count() const
{ return d->slides.size(); }

int ExCarousel::currentIndex() const
{ return d->currentIndex; }

void ExCarousel::setCurrentIndex( int index )
{
    if ( d->slides.isEmpty() )
    {
        return;
    }
    const int bounded = d->wrap ? wrapIndex( index, d->slides.size() ) : qBound( 0, index, d->slides.size() - 1 );
    int direction     = bounded >= d->currentIndex ? 1 : -1;
    if ( d->wrap && d->currentIndex == d->slides.size() - 1 && bounded == 0 )
    {
        direction = 1;
    }
    else if ( d->wrap && d->currentIndex == 0 && bounded == d->slides.size() - 1 )
    {
        direction = -1;
    }
    d->goTo( bounded, direction );
}

void ExCarousel::next()
{
    if ( d->slides.size() < 2 )
    {
        return;
    }
    const int index = d->nextIndex( d->currentIndex, 1 );
    if ( index == d->currentIndex )
    {
        return;
    }
    d->goTo( index, 1 );
}

void ExCarousel::previous()
{
    if ( d->slides.size() < 2 )
    {
        return;
    }
    const int index = d->nextIndex( d->currentIndex, -1 );
    if ( index == d->currentIndex )
    {
        return;
    }
    d->goTo( index, -1 );
}

bool ExCarousel::autoPlay() const
{ return d->autoPlay; }

void ExCarousel::setAutoPlay( bool enabled )
{
    if ( d->autoPlay == enabled )
    {
        return;
    }
    d->autoPlay = enabled;
    d->restartTimer();
    Q_EMIT autoPlayChanged( enabled );
}

int ExCarousel::interval() const
{ return d->interval; }

void ExCarousel::setInterval( int msec )
{
    msec = qMax( 200, msec );
    if ( d->interval == msec )
    {
        return;
    }
    d->interval = msec;
    d->restartTimer();
    Q_EMIT intervalChanged( msec );
}

bool ExCarousel::wrap() const
{ return d->wrap; }

void ExCarousel::setWrap( bool wrap )
{
    if ( d->wrap == wrap )
    {
        return;
    }
    d->wrap = wrap;
    d->updateChromeVisibility();
    d->restartTimer();
    Q_EMIT wrapChanged( wrap );
}

bool ExCarousel::showNavigationButtons() const
{ return d->showNavigationButtons; }

void ExCarousel::setShowNavigationButtons( bool show )
{
    if ( d->showNavigationButtons == show )
    {
        return;
    }
    d->showNavigationButtons = show;
    d->updateChromeVisibility();
    Q_EMIT showNavigationButtonsChanged( show );
}

bool ExCarousel::showIndicators() const
{ return d->showIndicators; }

void ExCarousel::setShowIndicators( bool show )
{
    if ( d->showIndicators == show )
    {
        return;
    }
    d->showIndicators = show;
    d->updateChromeVisibility();
    Q_EMIT showIndicatorsChanged( show );
}

int ExCarousel::animationDuration() const
{ return d->animationDuration; }

void ExCarousel::setAnimationDuration( int msec )
{
    msec = qMax( 0, msec );
    if ( d->animationDuration == msec )
    {
        return;
    }
    d->animationDuration = msec;
    Q_EMIT animationDurationChanged( msec );
}

bool ExCarousel::pauseOnHover() const
{ return d->pauseOnHover; }

void ExCarousel::setPauseOnHover( bool pause )
{
    if ( d->pauseOnHover == pause )
    {
        return;
    }
    d->pauseOnHover = pause;
    d->restartTimer();
    Q_EMIT pauseOnHoverChanged( pause );
}

qreal ExCarousel::borderRadius() const
{ return d->borderRadius; }

void ExCarousel::setBorderRadius( qreal radius )
{
    radius = qMax( 0.0, radius );
    if ( qFuzzyCompare( d->borderRadius, radius ) )
    {
        return;
    }
    d->borderRadius = radius;
    d->viewport->setBorderRadius( radius );
    d->applySlideBorderRadius();
    Q_EMIT borderRadiusChanged( radius );
}

ExCarousel::NavigationButtonTrigger ExCarousel::navigationButtonTrigger() const
{ return d->navTrigger; }

void ExCarousel::setNavigationButtonTrigger( NavigationButtonTrigger trigger )
{
    if ( d->navTrigger == trigger )
    {
        return;
    }
    d->navTrigger = trigger;
    d->updateChromeVisibility();
    Q_EMIT navigationButtonTriggerChanged( trigger );
}

QSize ExCarousel::sizeHint() const
{ return QSize( kDefaultWidth, kDefaultHeight ); }

QSize ExCarousel::minimumSizeHint() const
{ return QSize( kMinWidth, kMinHeight ); }

#if QT_VERSION >= QT_VERSION_CHECK( 6, 0, 0 )
void ExCarousel::enterEvent( QEnterEvent* event )
#else
void ExCarousel::enterEvent( QEvent* event )
#endif
{
    QWidget::enterEvent( event );
    d->hovered = true;
    d->restartTimer();
    if ( d->navTrigger == OnHover )
    {
        d->animateButtonsOpacity( 1.0 );
    }
}

void ExCarousel::leaveEvent( QEvent* event )
{
    QWidget::leaveEvent( event );
    d->hovered = false;
    d->restartTimer();
    if ( d->navTrigger == OnHover )
    {
        d->animateButtonsOpacity( 0.0 );
    }
}

void ExCarousel::resizeEvent( QResizeEvent* event )
{
    QWidget::resizeEvent( event );
    if ( d->animating )
    {
        d->finishAnimation( d->currentIndex );
    }
    else
    {
        d->placeIdle();
    }
}

void ExCarousel::hideEvent( QHideEvent* event )
{
    QWidget::hideEvent( event );
    d->stopTimer();
}

void ExCarousel::showEvent( QShowEvent* event )
{
    QWidget::showEvent( event );
    d->placeIdle();
    d->restartTimer();
}

void ExCarousel::changeEvent( QEvent* event )
{
    QWidget::changeEvent( event );
    if ( event->type() == QEvent::LanguageChange )
    {
        d->prevButton->setToolTip( tr( "上一张" ) );
        d->nextButton->setToolTip( tr( "下一张" ) );
    }
    else if ( event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange
              || event->type() == QEvent::StyleChange )
    {
        d->prevButton->update();
        d->nextButton->update();
        d->pips->update();
        d->viewport->update();
        update();
    }
}

void ExCarousel::keyPressEvent( QKeyEvent* event )
{
    if ( event->key() == Qt::Key_Left )
    {
        previous();
        event->accept();
        return;
    }
    if ( event->key() == Qt::Key_Right )
    {
        next();
        event->accept();
        return;
    }
    QWidget::keyPressEvent( event );
}

void ExCarousel::wheelEvent( QWheelEvent* event )
{
    const int delta =
#if QT_VERSION >= QT_VERSION_CHECK( 5, 15, 0 )
        event->angleDelta().y() != 0 ? event->angleDelta().y() : event->angleDelta().x();
#else
        event->delta();
#endif
    if ( delta > 0 )
    {
        previous();
    }
    else if ( delta < 0 )
    {
        next();
    }
    event->accept();
}

bool ExCarousel::eventFilter( QObject* watched, QEvent* event )
{
    if ( watched == d->pips && event->type() == QEvent::MouseButtonRelease )
    {
        const auto* mouse = static_cast<QMouseEvent*>( event );
        const int index   = d->pips->indexAt( getEventPos( mouse ) );
        if ( index >= 0 )
        {
            setCurrentIndex( index );
        }
        return true;
    }

    if ( event->type() == QEvent::MouseButtonRelease )
    {
        for ( int i = 0; i < d->slides.size(); ++i )
        {
            if ( watched == d->slides.at( i ) && i == d->currentIndex )
            {
                Q_EMIT slideClicked( i );
                break;
            }
        }
    }

    return QWidget::eventFilter( watched, event );
}
