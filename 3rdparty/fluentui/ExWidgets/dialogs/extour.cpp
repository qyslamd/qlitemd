#include "extour.h"

#include <QApplication>
#include <QEasingCurve>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <fluentui3styleproperties.h>
#include <functional>

#include "exfonticon.h"

namespace {

static inline QColor resolveAccentColor( const QWidget* w )
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

enum ArrowDirection
{
    ArrowNone,
    ArrowTop,     // 卡片在目标下方，小三角在卡片顶沿指向目标
    ArrowBottom,  // 卡片在目标上方，小三角在卡片底沿指向目标
    ArrowLeft,    // 卡片在目标右侧，小三角在卡片左沿指向目标
    ArrowRight    // 卡片在目标左侧，小三角在卡片右沿指向目标
};

struct CardPlacementResult
{
    QPoint pos;
    ArrowDirection arrowDir;
};

// 气泡卡片自适应位置计算：四向空间智能检测与翻转（Flip Fallback），箭头小尾巴尖端与高亮目标保持 6px 呼吸间距
static CardPlacementResult calculateCardPlacement( const QRectF& hole,
                                                   int cardW,
                                                   int cardH,
                                                   ExTourStep::Placement preferredPlacement,
                                                   int containerW,
                                                   int containerH )
{
    constexpr int gap    = 6;
    constexpr int margin = 16;
    CardPlacementResult res;

    // 计算四个方位的可用空间是否充裕
    const bool canPlaceBottom = ( hole.bottom() + gap + cardH <= containerH - margin );
    const bool canPlaceTop    = ( hole.top() - gap - cardH >= margin );
    const bool canPlaceRight  = ( hole.right() + gap + cardW <= containerW - margin );
    const bool canPlaceLeft   = ( hole.left() - gap - cardW >= margin );

    ExTourStep::Placement actualPlacement = preferredPlacement;

    // 自适应或溢出翻转策略
    if ( actualPlacement == ExTourStep::Auto )
    {
        if ( canPlaceBottom )
        {
            actualPlacement = ExTourStep::Bottom;
        }
        else if ( canPlaceTop )
        {
            actualPlacement = ExTourStep::Top;
        }
        else if ( canPlaceRight )
        {
            actualPlacement = ExTourStep::Right;
        }
        else if ( canPlaceLeft )
        {
            actualPlacement = ExTourStep::Left;
        }
        else
        {
            actualPlacement = ExTourStep::Bottom;
        }
    }
    else if ( actualPlacement == ExTourStep::Top && !canPlaceTop && canPlaceBottom )
    {
        actualPlacement = ExTourStep::Bottom;
    }
    else if ( actualPlacement == ExTourStep::Bottom && !canPlaceBottom && canPlaceTop )
    {
        actualPlacement = ExTourStep::Top;
    }
    else if ( actualPlacement == ExTourStep::Left && !canPlaceLeft && canPlaceRight )
    {
        actualPlacement = ExTourStep::Right;
    }
    else if ( actualPlacement == ExTourStep::Right && !canPlaceRight && canPlaceLeft )
    {
        actualPlacement = ExTourStep::Left;
    }

    int x = int( hole.center().x() - cardW / 2.0 );
    int y = int( hole.center().y() - cardH / 2.0 );

    if ( actualPlacement == ExTourStep::Bottom )
    {
        y            = int( hole.bottom() + gap );
        res.arrowDir = ArrowTop;
    }
    else if ( actualPlacement == ExTourStep::Top )
    {
        y            = int( hole.top() - gap - cardH );
        res.arrowDir = ArrowBottom;
    }
    else if ( actualPlacement == ExTourStep::Left )
    {
        x            = int( hole.left() - gap - cardW );
        res.arrowDir = ArrowRight;
    }
    else if ( actualPlacement == ExTourStep::Right )
    {
        x            = int( hole.right() + gap );
        res.arrowDir = ArrowLeft;
    }

    x = qBound( margin, x, qMax( margin, containerW - cardW - margin ) );
    y = qBound( margin, y, qMax( margin, containerH - cardH - margin ) );

    res.pos = QPoint( x, y );
    return res;
}

// 小圆点分页指示器
class TourDotsIndicator final : public QWidget
{
public:
    explicit TourDotsIndicator( QWidget* parent = nullptr )
        : QWidget( parent )
    {
        setFixedHeight( 20 );
        setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Fixed );
    }

    void setCount( int current, int total )
    {
        m_current = current;
        m_total   = total;

        constexpr qreal paddingH = 6.0;
        constexpr qreal dotSize  = 8.0;
        constexpr qreal spacing  = 10.0;
        const int totalW         = ( total <= 0 ) ? 24 : qRound( 2.0 * paddingH + total * dotSize + ( total - 1 ) * spacing );
        setFixedSize( qMax( 24, totalW ), 20 );
        update();
    }

protected:
    void paintEvent( QPaintEvent* ) override
    {
        if ( m_total <= 0 )
        {
            return;
        }
        QPainter painter( this );
        painter.setRenderHint( QPainter::Antialiasing );

        const bool isDark        = palette().color( QPalette::Window ).lightness() < 128;
        const QColor activeColor = resolveAccentColor( this );
        // 未激活圆点颜色：深色模式浅灰，浅色模式淡灰
        const QColor inactiveColor = isDark ? QColor( 99, 99, 99 ) : QColor( 214, 214, 214 );

        constexpr qreal paddingH   = 6.0;
        constexpr qreal dotSize    = 8.0;
        constexpr qreal activeSize = 10.0;
        constexpr qreal spacing    = 10.0;

        const qreal cy = height() / 2.0;

        for ( int i = 0; i < m_total; ++i )
        {
            painter.setPen( Qt::NoPen );
            const qreal cx = paddingH + dotSize / 2.0 + i * ( dotSize + spacing );

            if ( i == m_current )
            {
                // 激活状态小圆点稍微放大 (scale 1.25)，以固定圆心绘制，两端预留安全内边距绝不裁剪
                painter.setBrush( activeColor );
                painter.drawEllipse( QPointF( cx, cy ), activeSize / 2.0, activeSize / 2.0 );
            }
            else
            {
                painter.setBrush( inactiveColor );
                painter.drawEllipse( QPointF( cx, cy ), dotSize / 2.0, dotSize / 2.0 );
            }
        }
    }

private:
    int m_current = 0;
    int m_total   = 0;
};

// 引导气泡卡片
class TourCard final : public QWidget
{
public:
    explicit TourCard( QWidget* parent = nullptr )
        : QWidget( parent )
    {
        setAttribute( Qt::WA_StyledBackground, false );
        setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Preferred );
        // 引导卡片宽度预设 500
        setFixedWidth( 500 );

        m_rootLayout = new QVBoxLayout( this );
        m_rootLayout->setContentsMargins( 22, 20, 22, 20 );
        m_rootLayout->setSpacing( 10 );

        // 顶栏：标题 + 关闭按钮
        auto* headerLayout = new QHBoxLayout;
        headerLayout->setContentsMargins( 0, 0, 0, 0 );
        headerLayout->setSpacing( 8 );

        m_titleLabel    = new QLabel( this );
        QFont titleFont = font();
        titleFont.setBold( true );
        titleFont.setPixelSize( 14 );
        m_titleLabel->setFont( titleFont );
        m_titleLabel->setWordWrap( true );
        headerLayout->addWidget( m_titleLabel, 1 );

        m_closeButton = new QToolButton( this );
        m_closeButton->setAutoRaise( true );
        m_closeButton->setCursor( Qt::PointingHandCursor );
        m_closeButton->setToolButtonStyle( Qt::ToolButtonTextOnly );
        m_closeButton->setFont( ExFontIcon::iconFont( 10 ) );
        m_closeButton->setText( ExFontIcon::iconString( SegoeIcon::ChromeClose ) );
        m_closeButton->setFixedSize( 26, 26 );
        headerLayout->addWidget( m_closeButton, 0, Qt::AlignTop );
        m_rootLayout->addLayout( headerLayout );

        // 中间：描述文本
        m_descLabel = new QLabel( this );
        m_descLabel->setWordWrap( true );
        QFont descFont = font();
        descFont.setPixelSize( 12 );
        m_descLabel->setFont( descFont );
        m_rootLayout->addWidget( m_descLabel );

        // 底栏：圆点指示器 + 按钮组
        auto* footerLayout = new QHBoxLayout;
        footerLayout->setContentsMargins( 0, 8, 0, 0 );
        footerLayout->setSpacing( 14 );

        m_dotsIndicator = new TourDotsIndicator( this );
        footerLayout->addWidget( m_dotsIndicator, 0, Qt::AlignVCenter );
        footerLayout->addStretch( 1 );

        m_prevButton = new QPushButton( tr( "上一步" ), this );
        m_prevButton->setCursor( Qt::PointingHandCursor );

        m_nextButton = new QPushButton( tr( "下一步" ), this );
        m_nextButton->setCursor( Qt::PointingHandCursor );
        m_nextButton->setProperty( ButtonAccentStyleProperty, true );

        footerLayout->addWidget( m_prevButton );
        footerLayout->addWidget( m_nextButton );
        m_rootLayout->addLayout( footerLayout );
    }

    void setContent( const QString& title, const QString& desc, int currentIndex, int totalCount )
    {
        m_titleLabel->setText( title );
        m_descLabel->setText( desc );
        m_dotsIndicator->setCount( currentIndex, totalCount );

        m_prevButton->setVisible( currentIndex > 0 );
        if ( currentIndex == totalCount - 1 )
        {
            m_nextButton->setText( tr( "完成" ) );
        }
        else
        {
            m_nextButton->setText( tr( "下一步" ) );
        }

        adjustSize();
    }

    ArrowDirection arrowDirection() const { return m_arrowDir; }

    void setArrowDirection( ArrowDirection dir )
    {
        if ( m_arrowDir == dir )
        {
            return;
        }

        m_arrowDir          = dir;
        constexpr int baseH = 22;
        constexpr int baseV = 20;
        constexpr int triH  = 8;
        if ( dir == ArrowTop )
        {
            m_rootLayout->setContentsMargins( baseH, baseV + triH, baseH, baseV );
        }
        else if ( dir == ArrowBottom )
        {
            m_rootLayout->setContentsMargins( baseH, baseV, baseH, baseV + triH );
        }
        else if ( dir == ArrowLeft )
        {
            m_rootLayout->setContentsMargins( baseH + triH, baseV, baseH, baseV );
        }
        else if ( dir == ArrowRight )
        {
            m_rootLayout->setContentsMargins( baseH, baseV, baseH + triH, baseV );
        }
        else
        {
            m_rootLayout->setContentsMargins( baseH, baseV, baseH, baseV );
        }
        update();
    }

    void setTargetAnchor( const QPointF& targetAnchorInCard )
    {
        m_targetAnchor = targetAnchorInCard;
        update();
    }

    QToolButton* closeButton() const { return m_closeButton; }

    QPushButton* prevButton() const { return m_prevButton; }

    QPushButton* nextButton() const { return m_nextButton; }

protected:
    void changeEvent( QEvent* event ) override
    {
        if ( event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange )
        {
            update();
        }
        QWidget::changeEvent( event );
    }

    void paintEvent( QPaintEvent* ) override
    {
        QPainter painter( this );
        painter.setRenderHint( QPainter::Antialiasing );

        const bool isDark = palette().color( QPalette::Window ).lightness() < 128;
        // 气泡卡片背景色：深色 #272727，浅色 #FBFBFD
        const QColor bg = isDark ? QColor( 39, 39, 39 ) : QColor( 251, 251, 253 );

        // 浅色模式绘制极淡柔和边框保证可辨识度，深色模式纯净无边框（border.width: 0）
        const QPen borderPen = isDark ? Qt::NoPen : QPen( QColor( 210, 210, 210, 160 ), 1.0 );

        // 气泡卡片圆角：5px
        constexpr qreal kRadius = 5.0;
        constexpr qreal triW    = 16.0;
        constexpr qreal triH    = 8.0;

        QRectF body = rect();
        if ( m_arrowDir == ArrowTop )
        {
            body.adjust( 0, triH, 0, 0 );
        }
        else if ( m_arrowDir == ArrowBottom )
        {
            body.adjust( 0, 0, 0, -triH );
        }
        else if ( m_arrowDir == ArrowLeft )
        {
            body.adjust( triH, 0, 0, 0 );
        }
        else if ( m_arrowDir == ArrowRight )
        {
            body.adjust( 0, 0, -triH, 0 );
        }

        body = body.adjusted( 0.5, 0.5, -0.5, -0.5 );

        // 单条连续闭合路径顺时针绘制圆角矩形与小指示三角形，彻底消除昂贵的布尔运算（path.united），性能极致丝滑
        QPainterPath path;
        path.moveTo( body.left() + kRadius, body.top() );

        // 顶边与上指示小三角
        if ( m_arrowDir == ArrowTop )
        {
            const qreal triX = qBound( body.left() + kRadius + triW / 2.0, m_targetAnchor.x(), body.right() - kRadius - triW / 2.0 );
            path.lineTo( triX - triW / 2.0, body.top() );
            path.lineTo( triX, body.top() - triH );
            path.lineTo( triX + triW / 2.0, body.top() );
        }
        path.lineTo( body.right() - kRadius, body.top() );

        // 右上圆角
        path.arcTo( QRectF( body.right() - 2.0 * kRadius, body.top(), 2.0 * kRadius, 2.0 * kRadius ), 90.0, -90.0 );

        // 右边与右指示小三角
        if ( m_arrowDir == ArrowRight )
        {
            const qreal triY = qBound( body.top() + kRadius + triW / 2.0, m_targetAnchor.y(), body.bottom() - kRadius - triW / 2.0 );
            path.lineTo( body.right(), triY - triW / 2.0 );
            path.lineTo( body.right() + triH, triY );
            path.lineTo( body.right(), triY + triW / 2.0 );
        }
        path.lineTo( body.right(), body.bottom() - kRadius );

        // 右下圆角
        path.arcTo( QRectF( body.right() - 2.0 * kRadius, body.bottom() - 2.0 * kRadius, 2.0 * kRadius, 2.0 * kRadius ), 0.0, -90.0 );

        // 底边与下指示小三角
        if ( m_arrowDir == ArrowBottom )
        {
            const qreal triX = qBound( body.left() + kRadius + triW / 2.0, m_targetAnchor.x(), body.right() - kRadius - triW / 2.0 );
            path.lineTo( triX + triW / 2.0, body.bottom() );
            path.lineTo( triX, body.bottom() + triH );
            path.lineTo( triX - triW / 2.0, body.bottom() );
        }
        path.lineTo( body.left() + kRadius, body.bottom() );

        // 左下圆角
        path.arcTo( QRectF( body.left(), body.bottom() - 2.0 * kRadius, 2.0 * kRadius, 2.0 * kRadius ), 270.0, -90.0 );

        // 左边与左指示小三角
        if ( m_arrowDir == ArrowLeft )
        {
            const qreal triY = qBound( body.top() + kRadius + triW / 2.0, m_targetAnchor.y(), body.bottom() - kRadius - triW / 2.0 );
            path.lineTo( body.left(), triY + triW / 2.0 );
            path.lineTo( body.left() - triH, triY );
            path.lineTo( body.left(), triY - triW / 2.0 );
        }
        path.lineTo( body.left(), body.top() + kRadius );

        // 左上圆角
        path.arcTo( QRectF( body.left(), body.top(), 2.0 * kRadius, 2.0 * kRadius ), 180.0, -90.0 );

        path.closeSubpath();

        painter.setPen( borderPen );
        painter.setBrush( bg );
        painter.drawPath( path );
    }

private:
    QVBoxLayout* m_rootLayout          = nullptr;
    QLabel* m_titleLabel               = nullptr;
    QLabel* m_descLabel                = nullptr;
    TourDotsIndicator* m_dotsIndicator = nullptr;
    QToolButton* m_closeButton         = nullptr;
    QPushButton* m_prevButton          = nullptr;
    QPushButton* m_nextButton          = nullptr;

    ArrowDirection m_arrowDir = ArrowTop;
    QPointF m_targetAnchor;
};

// 全屏半透明遮罩与聚光灯镂空系统
class TourMask final : public QWidget
{
public:
    explicit TourMask( QWidget* parent = nullptr )
        : QWidget( parent )
    {
        setAttribute( Qt::WA_TranslucentBackground, true );
        setAttribute( Qt::WA_NoSystemBackground, true );
        setFocusPolicy( Qt::StrongFocus );

        m_card = new TourCard( this );

        // 动效时长恢复为 260ms (搭配 OutCubic 缓动，兼顾灵动感与丝滑减速)
        m_anim = new QVariantAnimation( this );
        m_anim->setDuration( 260 );
        m_anim->setEasingCurve( QEasingCurve::OutCubic );

        connect( m_anim,
                 &QVariantAnimation::valueChanged,
                 this,
                 [ this ]( const QVariant& val )
                 {
                     const qreal progress = val.toReal();

                     const qreal x = m_startHole.x() + ( m_targetHole.x() - m_startHole.x() ) * progress;
                     const qreal y = m_startHole.y() + ( m_targetHole.y() - m_startHole.y() ) * progress;
                     const qreal w = m_startHole.width() + ( m_targetHole.width() - m_startHole.width() ) * progress;
                     const qreal h = m_startHole.height() + ( m_targetHole.height() - m_startHole.height() ) * progress;
                     m_currentHole = QRectF( x, y, w, h );

                     m_currentRadius = m_startRadius + ( m_targetRadius - m_startRadius ) * progress;

                     const qreal cardX = m_startCardPos.x() + ( m_targetCardPos.x() - m_startCardPos.x() ) * progress;
                     const qreal cardY = m_startCardPos.y() + ( m_targetCardPos.y() - m_startCardPos.y() ) * progress;
                     m_card->move( qRound( cardX ), qRound( cardY ) );

                     // 过渡过半时平滑切换箭头指向，避免起飞瞬间在原位置突兀跳变
                     if ( progress >= 0.5 && m_card->arrowDirection() != m_targetArrowDir )
                     {
                         m_card->setArrowDirection( m_targetArrowDir );
                     }

                     m_card->setTargetAnchor( m_currentHole.center() - QPointF( cardX, cardY ) );

                     update();
                 } );

        connect( m_anim,
                 &QVariantAnimation::finished,
                 this,
                 [ this ]()
                 {
                     m_currentHole   = m_targetHole;
                     m_currentRadius = m_targetRadius;
                     m_card->setArrowDirection( m_targetArrowDir );
                     m_card->move( m_targetCardPos.toPoint() );
                     m_card->setTargetAnchor( m_targetHole.center() - QPointF( m_targetCardPos ) );
                     update();
                 } );
    }

    void setCloseOnMaskClick( bool close ) { m_closeOnMaskClick = close; }

    TourCard* card() const { return m_card; }

    void morphTo( const QRectF& targetRect, qreal radius, ExTourStep::Placement placement, bool animated )
    {
        m_targetPlacement = placement;

        const int actualCardW = qMin( 500, qMax( 320, width() - 32 ) );
        m_card->setFixedWidth( actualCardW );
        m_card->adjustSize();

        const CardPlacementResult finalPlacement =
            calculateCardPlacement( targetRect, m_card->width(), m_card->height(), placement, width(), height() );
        m_targetArrowDir = finalPlacement.arrowDir;

        if ( !animated || m_currentHole.isEmpty() )
        {
            m_currentHole   = targetRect;
            m_targetHole    = targetRect;
            m_currentRadius = radius;
            m_targetRadius  = radius;
            m_card->setArrowDirection( finalPlacement.arrowDir );
            m_card->move( finalPlacement.pos );
            m_card->setTargetAnchor( targetRect.center() - QPointF( finalPlacement.pos ) );
            update();
            return;
        }

        m_anim->stop();
        m_startHole     = m_currentHole;
        m_targetHole    = targetRect;
        m_startRadius   = m_currentRadius;
        m_targetRadius  = radius;
        m_startCardPos  = m_card->pos();
        m_targetCardPos = finalPlacement.pos;

        m_anim->setStartValue( 0.0 );
        m_anim->setEndValue( 1.0 );
        m_anim->start();
    }

    void updateTargetHole( const QRectF& newHole, qreal radius, ExTourStep::Placement placement )
    {
        if ( newHole.isEmpty() )
        {
            return;
        }

        m_targetPlacement = placement;
        m_currentHole     = newHole;
        m_targetHole      = newHole;
        m_currentRadius   = radius;
        m_targetRadius    = radius;

        const int actualCardW = qMin( 500, qMax( 320, width() - 32 ) );
        m_card->setFixedWidth( actualCardW );
        m_card->adjustSize();

        const CardPlacementResult res = calculateCardPlacement( newHole, m_card->width(), m_card->height(), placement, width(), height() );
        m_targetArrowDir              = res.arrowDir;
        m_card->setArrowDirection( res.arrowDir );
        m_card->move( res.pos );
        m_card->setTargetAnchor( newHole.center() - QPointF( res.pos ) );
        update();
    }

    void repositionCard()
    {
        if ( !m_card || m_currentHole.isEmpty() )
        {
            return;
        }
        updateTargetHole( m_currentHole, m_currentRadius, m_targetPlacement );
    }

protected:
    void paintEvent( QPaintEvent* ) override
    {
        QPainter painter( this );
        painter.setRenderHint( QPainter::Antialiasing );

        const bool isDark = palette().color( QPalette::Window ).lightness() < 128;

        // 1. 全屏半透明遮罩背景 (53% 透明黑度)
        QPainterPath path;
        path.setFillRule( Qt::OddEvenFill );
        path.addRect( rect() );

        if ( !m_currentHole.isEmpty() )
        {
            path.addRoundedRect( m_currentHole, m_currentRadius, m_currentRadius );
        }

        painter.fillPath( path, QColor( 0, 0, 0, 136 ) );

        // 2. 镂空边缘微弱边框指引
        if ( !m_currentHole.isEmpty() && isDark )
        {
            // 深色底色下提供 0.5px 极微弱边缘指引，避免全黑背景时丢失边界
            painter.setPen( QPen( QColor( 255, 255, 255, 20 ), 1.0 ) );
            painter.setBrush( Qt::NoBrush );
            painter.drawRoundedRect( m_currentHole, m_currentRadius, m_currentRadius );
        }

        // 3. 卡片柔和多层悬浮阴影
        if ( m_card && m_card->isVisible() && !m_card->size().isEmpty() )
        {
            const QRectF cRect       = m_card->geometry();
            const QColor shadowColor = isDark ? QColor( 0, 0, 0 ) : QColor( 140, 140, 140 );
            for ( int i = 0; i < 5; ++i )
            {
                const int alpha = isDark ? ( 22 - i * 4 ) : ( 16 - i * 3 );
                if ( alpha <= 0 )
                {
                    continue;
                }
                painter.setPen( QPen( QColor( shadowColor.red(), shadowColor.green(), shadowColor.blue(), alpha ), 1.5 + i * 0.5 ) );
                painter.setBrush( Qt::NoBrush );
                painter.drawRoundedRect( cRect.adjusted( -i, -i, i, i ), 5.0 + i, 5.0 + i );
            }
        }
    }

    void changeEvent( QEvent* event ) override
    {
        if ( event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange )
        {
            update();
        }
        QWidget::changeEvent( event );
    }

    bool handleKeyEvent( QKeyEvent* key )
    {
        if ( key->key() == Qt::Key_Escape )
        {
            if ( onExitRequested )
            {
                onExitRequested();
            }
            return true;
        }

        // 如果焦点在卡片内的交互按钮上，Enter/Space 由按钮自身处理，不作为向导全局快捷键拦截
        QWidget* focused             = QApplication::focusWidget();
        const bool focusOnCardAction = focused && m_card && m_card->isAncestorOf( focused ) && qobject_cast<QAbstractButton*>( focused );

        if ( key->key() == Qt::Key_Right || key->key() == Qt::Key_Down )
        {
            if ( !focusOnCardAction && onNextRequested )
            {
                onNextRequested();
                return true;
            }
        }
        if ( key->key() == Qt::Key_Left || key->key() == Qt::Key_Up )
        {
            if ( !focusOnCardAction && onPrevRequested )
            {
                onPrevRequested();
                return true;
            }
        }
        if ( key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Space )
        {
            if ( !focusOnCardAction && onNextRequested )
            {
                onNextRequested();
                return true;
            }
        }
        return false;
    }

    bool eventFilter( QObject* watched, QEvent* event ) override
    {
        if ( event->type() == QEvent::Resize && watched == parentWidget() )
        {
            setGeometry( parentWidget()->rect() );
            update();
            if ( onTargetPosUpdateRequested )
            {
                onTargetPosUpdateRequested();
            }
            else
            {
                repositionCard();
            }
        }
        else if ( ( event->type() == QEvent::Resize || event->type() == QEvent::Move ) && watched != parentWidget() )
        {
            if ( onTargetPosUpdateRequested )
            {
                onTargetPosUpdateRequested();
            }
        }
        else if ( event->type() == QEvent::KeyPress )
        {
            if ( handleKeyEvent( static_cast<QKeyEvent*>( event ) ) )
            {
                return true;
            }
        }
        return QWidget::eventFilter( watched, event );
    }

    void keyPressEvent( QKeyEvent* event ) override
    {
        if ( handleKeyEvent( event ) )
        {
            return;
        }
        QWidget::keyPressEvent( event );
    }

    void mousePressEvent( QMouseEvent* event ) override
    {
        if ( m_closeOnMaskClick && !m_currentHole.contains( event->pos() ) )
        {
            if ( onExitRequested )
            {
                onExitRequested();
            }
            return;
        }
        QWidget::mousePressEvent( event );
    }

public:
    std::function<void()> onExitRequested;
    std::function<void()> onNextRequested;
    std::function<void()> onPrevRequested;
    std::function<void()> onTargetPosUpdateRequested;

private:
    TourCard* m_card          = nullptr;
    QVariantAnimation* m_anim = nullptr;

    QRectF m_startHole;
    QRectF m_targetHole;
    QRectF m_currentHole;

    qreal m_startRadius   = 2.0;
    qreal m_targetRadius  = 2.0;
    qreal m_currentRadius = 2.0;

    QPointF m_startCardPos;
    QPointF m_targetCardPos;

    ArrowDirection m_arrowDir               = ArrowTop;
    ArrowDirection m_targetArrowDir         = ArrowTop;
    ExTourStep::Placement m_targetPlacement = ExTourStep::Auto;
    bool m_closeOnMaskClick                 = false;
};

}  // namespace

class ExTourPrivate
{
    Q_DECLARE_PUBLIC( ExTour )

public:
    explicit ExTourPrivate( ExTour* q )
        : q_ptr( q )
    {
    }

    ExTour* q_ptr = nullptr;
    QList<ExTourStep> steps;
    int currentIndex      = -1;
    bool closeOnMaskClick = false;
    bool animated         = true;

    QPointer<QWidget> hostWindow;
    QPointer<TourMask> mask;

    void showStep( int index );
    void cleanup();
    void syncTargetPosition();
    QRectF calculateHoleRect( const ExTourStep& step );
};

QRectF ExTourPrivate::calculateHoleRect( const ExTourStep& step )
{
    if ( !step.target || !hostWindow || !step.target->isVisible() )
    {
        return QRectF();
    }
    const QPoint topLeft = step.target->mapTo( hostWindow, QPoint( 0, 0 ) );
    const QRect rawRect( topLeft, step.target->size() );
    const QMargins m = step.targetMargin;
    return QRectF( rawRect.adjusted( -m.left(), -m.top(), m.right(), m.bottom() ) );
}

void ExTourPrivate::syncTargetPosition()
{
    if ( !mask || currentIndex < 0 || currentIndex >= steps.size() )
    {
        return;
    }
    const ExTourStep& step = steps.at( currentIndex );
    if ( !step.target || !step.target->isVisible() || !hostWindow )
    {
        return;
    }

    const QRectF newHole = calculateHoleRect( step );
    if ( !newHole.isEmpty() )
    {
        mask->updateTargetHole( newHole, step.targetRadius, step.placement );
    }
}

void ExTourPrivate::showStep( int index )
{
    if ( index < 0 || index >= steps.size() )
    {
        cleanup();
        Q_EMIT q_ptr->finished();
        return;
    }

    const ExTourStep& step = steps.at( index );
    if ( !step.target || !step.target->isVisible() )
    {
        // 目标失效或未显示，则跳过到下一步
        showStep( index + 1 );
        return;
    }

    // 寻找宿主顶层窗口
    QWidget* host = step.target->window();
    if ( !host )
    {
        cleanup();
        Q_EMIT q_ptr->cancelled();
        return;
    }

    if ( hostWindow != host )
    {
        cleanup();
        hostWindow = host;
    }

    if ( !mask )
    {
        mask = new TourMask( hostWindow );
        mask->setCloseOnMaskClick( closeOnMaskClick );
        mask->onExitRequested            = [ this ]() { q_ptr->exit(); };
        mask->onNextRequested            = [ this ]() { q_ptr->next(); };
        mask->onPrevRequested            = [ this ]() { q_ptr->previous(); };
        mask->onTargetPosUpdateRequested = [ this ]()
        {
            syncTargetPosition();
            if ( hostWindow )
            {
                QTimer::singleShot( 0, hostWindow, [ this ]() { syncTargetPosition(); } );
            }
        };

        TourCard* card = mask->card();
        QObject::connect( card->closeButton(), &QToolButton::clicked, q_ptr, &ExTour::exit );
        QObject::connect( card->prevButton(), &QPushButton::clicked, q_ptr, &ExTour::previous );
        QObject::connect( card->nextButton(), &QPushButton::clicked, q_ptr, &ExTour::next );

        hostWindow->installEventFilter( mask );
    }

    // 移除上一步目标控件的事件过滤器
    if ( currentIndex >= 0 && currentIndex < steps.size() )
    {
        const ExTourStep& prevStep = steps.at( currentIndex );
        if ( prevStep.target && mask )
        {
            prevStep.target->removeEventFilter( mask );
        }
    }

    mask->setGeometry( hostWindow->rect() );
    mask->show();
    mask->raise();

    currentIndex = index;

    // 更新卡片文案
    mask->card()->setContent( step.title, step.description, index, steps.size() );

    // 计算聚光灯目标并启动平滑补间动画
    const QRectF holeRect = calculateHoleRect( step );
    mask->morphTo( holeRect, step.targetRadius, step.placement, animated );

    // 监听当前目标控件的移动与尺寸变化
    if ( step.target && mask )
    {
        step.target->installEventFilter( mask );
    }

    Q_EMIT q_ptr->stepChanged( index );
}

void ExTourPrivate::cleanup()
{
    if ( currentIndex >= 0 && currentIndex < steps.size() )
    {
        const ExTourStep& prevStep = steps.at( currentIndex );
        if ( prevStep.target && mask )
        {
            prevStep.target->removeEventFilter( mask );
        }
    }
    currentIndex = -1;
    if ( mask )
    {
        if ( hostWindow )
        {
            hostWindow->removeEventFilter( mask );
        }
        mask->hide();
        mask->deleteLater();
        mask = nullptr;
    }
    hostWindow.clear();
}

ExTour::ExTour( QObject* parent )
    : QObject( parent )
    , d_ptr( new ExTourPrivate( this ) )
{
}

ExTour::~ExTour()
{ d_ptr->cleanup(); }

bool ExTour::closeOnMaskClick() const
{
    Q_D( const ExTour );
    return d->closeOnMaskClick;
}

void ExTour::setCloseOnMaskClick( bool close )
{
    Q_D( ExTour );
    d->closeOnMaskClick = close;
    if ( d->mask )
    {
        d->mask->setCloseOnMaskClick( close );
    }
}

bool ExTour::isAnimated() const
{
    Q_D( const ExTour );
    return d->animated;
}

void ExTour::setAnimated( bool animated )
{
    Q_D( ExTour );
    d->animated = animated;
}

void ExTour::addStep( const ExTourStep& step )
{
    Q_D( ExTour );
    d->steps.append( step );
}

void ExTour::addStep( QWidget* target, const QString& title, const QString& description, ExTourStep::Placement placement )
{
    ExTourStep step;
    step.target      = target;
    step.title       = title;
    step.description = description;
    step.placement   = placement;
    addStep( step );
}

void ExTour::setSteps( const QList<ExTourStep>& steps )
{
    Q_D( ExTour );
    d->steps = steps;
}

QList<ExTourStep> ExTour::steps() const
{
    Q_D( const ExTour );
    return d->steps;
}

int ExTour::currentStep() const
{
    Q_D( const ExTour );
    return d->currentIndex;
}

int ExTour::stepCount() const
{
    Q_D( const ExTour );
    return d->steps.size();
}

bool ExTour::isRunning() const
{
    Q_D( const ExTour );
    return d->currentIndex >= 0 && d->mask != nullptr;
}

void ExTour::start( int initialIndex )
{
    Q_D( ExTour );
    if ( d->steps.isEmpty() )
    {
        return;
    }
    d->showStep( qBound( 0, initialIndex, d->steps.size() - 1 ) );
}

void ExTour::next()
{
    Q_D( ExTour );
    if ( !isRunning() )
    {
        return;
    }
    d->showStep( d->currentIndex + 1 );
}

void ExTour::previous()
{
    Q_D( ExTour );
    if ( !isRunning() || d->currentIndex <= 0 )
    {
        return;
    }
    d->showStep( d->currentIndex - 1 );
}

void ExTour::exit()
{
    Q_D( ExTour );
    if ( !isRunning() )
    {
        return;
    }
    d->cleanup();
    Q_EMIT cancelled();
}
