#include "pagecarousel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include <excarousel.h>

namespace {
QWidget* makeCard( QWidget* parent )
{
    auto* card = new QWidget( parent );
    card->setProperty( "isCard", true );
    card->setAttribute( Qt::WA_StyledBackground, true );
    return card;
}

QLabel* makeSectionTitle( const QString& text, QWidget* parent )
{
    auto* label = new QLabel( text, parent );
    QFont f     = label->font();
    f.setBold( true );
    f.setPixelSize( 15 );
    label->setFont( f );
    return label;
}

class CarouselCustomCardSlide : public QWidget
{
public:
    enum SlideTheme
    {
        BlueCard,
        PurpleCard,
        GreenCard
    };

    CarouselCustomCardSlide( SlideTheme theme,
                            const QString& title,
                            const QString& subtitle,
                            QWidget* parent = nullptr )
        : QWidget( parent )
        , m_theme( theme )
        , m_title( title )
        , m_subtitle( subtitle )
    {
        setAttribute( Qt::WA_StyledBackground, true );
        auto* lay = new QVBoxLayout( this );
        lay->setContentsMargins( 24, 20, 24, 20 );
        lay->addStretch( 1 );

        m_titleLabel = new QLabel( m_title, this );
        QFont tf     = m_titleLabel->font();
        tf.setPixelSize( 22 );
        tf.setBold( true );
        m_titleLabel->setFont( tf );
        lay->addWidget( m_titleLabel );

        if ( !m_subtitle.isEmpty() )
        {
            m_subLabel = new QLabel( m_subtitle, this );
            QFont sf   = m_subLabel->font();
            sf.setPixelSize( 13 );
            m_subLabel->setFont( sf );
            lay->addWidget( m_subLabel );
        }

        applyTheme();
    }

protected:
    void changeEvent( QEvent* event ) override
    {
        QWidget::changeEvent( event );
        if ( event->type() == QEvent::PaletteChange ||
             event->type() == QEvent::ApplicationPaletteChange ||
             event->type() == QEvent::StyleChange )
        {
            applyTheme();
        }
    }

private:
    void applyTheme()
    {
        const bool isDark = palette().color( QPalette::Window ).lightness() < 128;

        QColor bgColor;
        QColor titleColor;
        QColor subTitleColor;

        if ( isDark )
        {
            switch ( m_theme )
            {
            case BlueCard:
                bgColor       = QColor( 16, 52, 98 );
                titleColor    = QColor( 225, 240, 255 );
                subTitleColor = QColor( 180, 210, 245, 220 );
                break;
            case PurpleCard:
                bgColor       = QColor( 64, 36, 102 );
                titleColor    = QColor( 242, 228, 255 );
                subTitleColor = QColor( 215, 195, 240, 220 );
                break;
            case GreenCard:
                bgColor       = QColor( 18, 70, 38 );
                titleColor    = QColor( 225, 255, 235 );
                subTitleColor = QColor( 185, 235, 205, 220 );
                break;
            }
        }
        else
        {
            switch ( m_theme )
            {
            case BlueCard:
                bgColor       = QColor( 230, 242, 254 );
                titleColor    = QColor( 0, 68, 130 );
                subTitleColor = QColor( 40, 100, 160 );
                break;
            case PurpleCard:
                bgColor       = QColor( 244, 238, 253 );
                titleColor    = QColor( 72, 34, 118 );
                subTitleColor = QColor( 105, 65, 155 );
                break;
            case GreenCard:
                bgColor       = QColor( 232, 248, 238 );
                titleColor    = QColor( 14, 88, 46 );
                subTitleColor = QColor( 36, 122, 72 );
                break;
            }
        }

        QPalette pal = palette();
        pal.setColor( QPalette::Window, bgColor );
        setPalette( pal );
        setAutoFillBackground( true );

        if ( m_titleLabel )
        {
            QPalette tp = m_titleLabel->palette();
            tp.setColor( QPalette::WindowText, titleColor );
            m_titleLabel->setPalette( tp );
            m_titleLabel->setStyleSheet( QString() );
        }
        if ( m_subLabel )
        {
            QPalette sp = m_subLabel->palette();
            sp.setColor( QPalette::WindowText, subTitleColor );
            m_subLabel->setPalette( sp );
            m_subLabel->setStyleSheet( QString() );
        }
        update();
    }

    SlideTheme m_theme;
    QString m_title;
    QString m_subtitle;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_subLabel   = nullptr;
};
}  // namespace

PageCarousel::PageCarousel( QWidget* parent )
    : QFrame( parent )
{
    setFrameShape( QFrame::StyledPanel );

    // 规范：引入 Gallery 标准 QScrollArea 容器，防止在任何窗口高度或窄宽度下挤压卡片内部布局
    auto* rootLayout = new QVBoxLayout( this );
    rootLayout->setContentsMargins( 0, 0, 0, 0 );

    auto* scrollArea = new QScrollArea( this );
    scrollArea->setWidgetResizable( true );
    scrollArea->setFrameShape( QFrame::NoFrame );
    scrollArea->setAutoFillBackground( false );
    scrollArea->viewport()->setAutoFillBackground( false );
    rootLayout->addWidget( scrollArea );

    auto* content = new QWidget( scrollArea );
    content->setAutoFillBackground( false );
    auto* mainLay = new QVBoxLayout( content );
    mainLay->setContentsMargins( 16, 16, 16, 16 );
    mainLay->setSpacing( 16 );
    scrollArea->setWidget( content );

    auto* title = new QLabel( tr( "ExCarousel 轮播图" ), content );
    {
        QFont f = title->font();
        f.setPointSize( 16 );
        f.setBold( true );
        title->setFont( f );
    }
    mainLay->addWidget( title );

    auto* hint = new QLabel( tr( "现代 Fluent 风格水平轮播图。支持高质量平滑缩放图片与任意 QWidget "
                                 "混排，提供基于视口全局圆角裁剪的丝滑平移过渡、自动播放、首尾循环、指示胶囊点以及悬停淡入两侧切换按钮。" ),
                             content );
    hint->setWordWrap( true );
    mainLay->addWidget( hint );

    // ---- 卡片1：图片轮播与综合设置 ----
    auto* demoCard = makeCard( content );
    auto* demoLay  = new QVBoxLayout( demoCard );
    demoLay->setContentsMargins( 16, 16, 16, 16 );
    demoLay->setSpacing( 14 );
    demoLay->addWidget( makeSectionTitle( tr( "图文轮播" ), demoCard ) );

    auto* carousel = new ExCarousel( demoCard );
    carousel->setObjectName( QStringLiteral( "exCarouselDemo" ) );
    carousel->setFixedHeight( 280 );
    carousel->setBorderRadius( 6.0 );

    // 接入微软 WinUI 3 Gallery 原版示例图片集（SampleMedia: Cliff, Grapes, Rainier, Sunset, Valley）
    carousel->addImage(
        QStringLiteral( ":/images/cliff.jpg" ), tr( "Cliff" ), tr( "微软 WinUI 3 Gallery 原版示例图片：海岸峭壁" ) );
    carousel->addImage(
        QStringLiteral( ":/images/grapes.jpg" ), tr( "Grapes" ), tr( "微软 WinUI 3 Gallery 原版示例图片：新鲜葡萄" ) );
    carousel->addImage(
        QStringLiteral( ":/images/rainier.jpg" ), tr( "Mount Rainier" ), tr( "微软 WinUI 3 Gallery 原版示例图片：雷尼尔雪山" ) );
    carousel->addImage(
        QStringLiteral( ":/images/sunset.jpg" ), tr( "Sunset" ), tr( "微软 WinUI 3 Gallery 原版示例图片：金色晚霞" ) );
    carousel->addImage(
        QStringLiteral( ":/images/valley.jpg" ), tr( "Valley" ), tr( "微软 WinUI 3 Gallery 原版示例图片：群山峡谷" ) );

    carousel->setAutoPlay( true );
    carousel->setInterval( 3500 );
    carousel->setAnimationDuration( 600 );
    carousel->setNavigationButtonTrigger( ExCarousel::OnHover );  // 默认悬停显现按钮，更具沉浸感
    demoLay->addWidget( carousel );

    auto* status = new QLabel( demoCard );
    status->setWordWrap( true );
    const auto updateStatus = [ status, carousel ]()
    {
        status->setText(
            tr( "当前第 %1 / %2 张 (支持滚轮、左右方向键或两侧按钮切换)" ).arg( carousel->currentIndex() + 1 ).arg( carousel->count() ) );
    };
    updateStatus();
    QObject::connect( carousel, &ExCarousel::currentIndexChanged, demoCard, updateStatus );
    demoLay->addWidget( status );

    // 控制面板第 1 行：核心开关
    auto* opts1 = new QHBoxLayout;
    opts1->setSpacing( 16 );

    auto* autoPlay = new QCheckBox( tr( "自动播放" ), demoCard );
    autoPlay->setChecked( true );
    QObject::connect( autoPlay, &QCheckBox::toggled, carousel, &ExCarousel::setAutoPlay );

    auto* wrap = new QCheckBox( tr( "首尾循环" ), demoCard );
    wrap->setChecked( true );
    QObject::connect( wrap, &QCheckBox::toggled, carousel, &ExCarousel::setWrap );

    auto* buttons = new QCheckBox( tr( "切换按钮" ), demoCard );
    buttons->setChecked( true );
    QObject::connect( buttons, &QCheckBox::toggled, carousel, &ExCarousel::setShowNavigationButtons );

    auto* indicators = new QCheckBox( tr( "指示胶囊点" ), demoCard );
    indicators->setChecked( true );
    QObject::connect( indicators, &QCheckBox::toggled, carousel, &ExCarousel::setShowIndicators );

    opts1->addWidget( autoPlay );
    opts1->addWidget( wrap );
    opts1->addWidget( buttons );
    opts1->addWidget( indicators );
    opts1->addStretch( 1 );
    demoLay->addLayout( opts1 );

    // 控制面板第 2 行：参数调节（采用网格排版，窄屏下结构稳固不撑爆）
    auto* gridOpts = new QGridLayout;
    gridOpts->setHorizontalSpacing( 16 );
    gridOpts->setVerticalSpacing( 10 );

    auto* triggerLabel = new QLabel( tr( "按钮显隐:" ), demoCard );
    auto* triggerCombo = new QComboBox( demoCard );
    triggerCombo->addItem( tr( "悬停显现 (OnHover)" ), int( ExCarousel::OnHover ) );
    triggerCombo->addItem( tr( "常驻显示 (AlwaysVisible)" ), int( ExCarousel::AlwaysVisible ) );
    QObject::connect(
        triggerCombo,
        QOverload<int>::of( &QComboBox::currentIndexChanged ),
        carousel,
        [ carousel, triggerCombo ]( int )
        {
            carousel->setNavigationButtonTrigger( static_cast<ExCarousel::NavigationButtonTrigger>( triggerCombo->currentData().toInt() ) );
        } );

    auto* intervalLabel = new QLabel( tr( "轮播间隔 (ms):" ), demoCard );
    auto* interval      = new QSpinBox( demoCard );
    interval->setRange( 500, 20000 );
    interval->setSingleStep( 500 );
    interval->setValue( 3500 );
    QObject::connect( interval, QOverload<int>::of( &QSpinBox::valueChanged ), carousel, &ExCarousel::setInterval );

    auto* durationLabel = new QLabel( tr( "切页耗时 (ms):" ), demoCard );
    auto* duration      = new QSpinBox( demoCard );
    duration->setRange( 100, 2000 );
    duration->setSingleStep( 50 );
    duration->setValue( 300 );
    QObject::connect( duration, QOverload<int>::of( &QSpinBox::valueChanged ), carousel, &ExCarousel::setAnimationDuration );

    gridOpts->addWidget( triggerLabel, 0, 0 );
    gridOpts->addWidget( triggerCombo, 0, 1 );
    gridOpts->addWidget( intervalLabel, 0, 2 );
    gridOpts->addWidget( interval, 0, 3 );
    gridOpts->addWidget( durationLabel, 0, 4 );
    gridOpts->addWidget( duration, 0, 5 );
    gridOpts->setColumnStretch( 6, 1 );
    demoLay->addLayout( gridOpts );

    mainLay->addWidget( demoCard );

    // ---- 卡片2：任意自定义 QWidget Slide 演示 ----
    auto* widgetCard = makeCard( content );
    auto* widgetLay  = new QVBoxLayout( widgetCard );
    widgetLay->setContentsMargins( 16, 16, 16, 16 );
    widgetLay->setSpacing( 12 );
    widgetLay->addWidget( makeSectionTitle( tr( "自定义 QWidget 幻灯片演示" ), widgetCard ) );

    auto* widgetCarousel = new ExCarousel( widgetCard );
    widgetCarousel->setObjectName( QStringLiteral( "exCarouselWidgetDemo" ) );
    widgetCarousel->setFixedHeight( 160 );
    widgetCarousel->setBorderRadius( 8.0 );
    widgetCarousel->addSlide(
        new CarouselCustomCardSlide( CarouselCustomCardSlide::BlueCard, tr( "Fluent 蓝卡片" ), tr( "原生 QWidget 自由布局与绘制" ), widgetCarousel ) );
    widgetCarousel->addSlide(
        new CarouselCustomCardSlide( CarouselCustomCardSlide::PurpleCard, tr( "优雅淡紫卡片" ), tr( "支持控件嵌套与富文本交互" ), widgetCarousel ) );
    widgetCarousel->addSlide(
        new CarouselCustomCardSlide( CarouselCustomCardSlide::GreenCard, tr( "森林之绿卡片" ), tr( "平滑动效与手势兼容" ), widgetCarousel ) );
    widgetCarousel->setAutoPlay( true );
    widgetCarousel->setInterval( 4000 );
    widgetCarousel->setAnimationDuration( 300 );

    widgetLay->addWidget( widgetCarousel );
    mainLay->addWidget( widgetCard );

    mainLay->addStretch( 1 );
}
