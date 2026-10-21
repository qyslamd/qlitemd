#include "pagenavigationhint.h"

#include <exbreadcrumbbar.h>
#include <exteachingtip.h>
#include <extour.h>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTextEdit>
#include <QVBoxLayout>

PageNavigationHint::PageNavigationHint( QWidget* parent ) : QFrame( parent )
{
    setFrameShape( QFrame::StyledPanel );
    auto* root = new QVBoxLayout( this );
    root->setContentsMargins( 0, 0, 0, 0 );
    auto* scroll = new QScrollArea( this );
    m_scroll = scroll;
    scroll->setWidgetResizable( true );
    scroll->setFrameShape( QFrame::NoFrame );
    root->addWidget( scroll );
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout( content );
    layout->setContentsMargins( 20, 20, 20, 20 );
    layout->setSpacing( 16 );
    scroll->setWidget( content );
    const auto heading = [content]( const QString& text )
    {
        auto* label = new QLabel( text, content );
        QFont font = label->font();
        font.setPointSize( 16 );
        font.setBold( true );
        label->setFont( font );
        return label;
    };

    layout->addWidget( heading( QStringLiteral( "ExBreadcrumbBar" ) ) );
    auto* breadcrumbHelp = new QLabel( tr( "点击路径返回上级；缩小宽度可查看溢出菜单。支持左右方向键、Home、End 和 Tab。" ), content );
    breadcrumbHelp->setWordWrap( true );
    layout->addWidget( breadcrumbHelp );
    auto* breadcrumb = new ExBreadcrumbBar( content );
    const QVariantList fullPath { tr( "主页" ), tr( "项目" ), tr( "DashStudio" ), tr( "设备配置" ), tr( "音频设备" ), tr( "高级设置" ) };
    breadcrumb->setItemsSource( fullPath );
    layout->addWidget( breadcrumb );
    auto* clickedLabel = new QLabel( tr( "当前路径：高级设置" ), content );
    layout->addWidget( clickedLabel );
    connect( breadcrumb, &ExBreadcrumbBar::itemClicked, clickedLabel, [breadcrumb, clickedLabel]( int index, const QVariant& item )
    {
        clickedLabel->setText( tr( "点击索引 %1：%2" ).arg( index ).arg( item.toString() ) );
        breadcrumb->setItemsSource( breadcrumb->itemsSource().mid( 0, index + 1 ) );
    } );
    auto* widthRow = new QHBoxLayout;
    widthRow->addWidget( new QLabel( tr( "最大宽度" ), content ) );
    auto* widthSlider = new QSlider( Qt::Horizontal, content );
    widthSlider->setRange( 100, 900 );
    widthSlider->setValue( 700 );
    breadcrumb->setMaximumWidth( widthSlider->value() );
    widthRow->addWidget( widthSlider, 1 );
    auto* resetPath = new QPushButton( tr( "恢复完整路径" ), content );
    widthRow->addWidget( resetPath );
    layout->addLayout( widthRow );
    connect( widthSlider, &QSlider::valueChanged, breadcrumb, &QWidget::setMaximumWidth );
    connect( resetPath, &QPushButton::clicked, breadcrumb, [breadcrumb, fullPath]() { breadcrumb->setItemsSource( fullPath ); } );
    auto* rtl = new QCheckBox( tr( "从右到左布局" ), content );
    layout->addWidget( rtl );
    connect( rtl, &QCheckBox::toggled, breadcrumb, [breadcrumb]( bool checked )
    {
        breadcrumb->setLayoutDirection( checked ? Qt::RightToLeft : Qt::LeftToRight );
    } );

    layout->addSpacing( 16 );
    layout->addWidget( heading( QStringLiteral( "ExTeachingTip" ) ) );
    auto* tipHelp = new QLabel( tr( "点击下面不同位置的按钮显示引导提示。空间不足时自动换边；提示不阻塞页面，切换页面会关闭。" ), content );
    tipHelp->setWordWrap( true );
    layout->addWidget( tipHelp );
    auto* options = new QHBoxLayout;
    options->addWidget( new QLabel( tr( "优先方向" ), content ) );
    auto* placement = new QComboBox( content );
    placement->addItem( tr( "自动" ), ExTeachingTip::Auto );
    placement->addItem( tr( "上方" ), ExTeachingTip::Top );
    placement->addItem( tr( "下方" ), ExTeachingTip::Bottom );
    placement->addItem( tr( "左侧" ), ExTeachingTip::Left );
    placement->addItem( tr( "右侧" ), ExTeachingTip::Right );
    placement->addItem( tr( "上方靠右" ), ExTeachingTip::TopRight );
    placement->addItem( tr( "上方靠左" ), ExTeachingTip::TopLeft );
    placement->addItem( tr( "下方靠右" ), ExTeachingTip::BottomRight );
    placement->addItem( tr( "下方靠左" ), ExTeachingTip::BottomLeft );
    placement->addItem( tr( "左侧靠上" ), ExTeachingTip::LeftTop );
    placement->addItem( tr( "左侧靠下" ), ExTeachingTip::LeftBottom );
    placement->addItem( tr( "右侧靠上" ), ExTeachingTip::RightTop );
    placement->addItem( tr( "右侧靠下" ), ExTeachingTip::RightBottom );
    placement->addItem( tr( "居中" ), ExTeachingTip::Center );
    options->addWidget( placement );
    auto* lightDismiss = new QCheckBox( tr( "点击外部或滚动时关闭" ), content );
    options->addWidget( lightDismiss );
    auto* longText = new QCheckBox( tr( "长内容" ), content );
    options->addWidget( longText );
    options->addStretch();
    layout->addLayout( options );

    auto* tip = new ExTeachingTip( this );
    m_tip = tip;
    tip->setTitle( tr( "试试快捷配置" ) );
    tip->setSubtitle( tr( "在这里配置设备参数。提示不会锁定主窗口，按 Esc 或点击关闭按钮即可关闭。" ) );
    tip->setActionButtonContent( tr( "知道了" ) );
    connect( tip, &ExTeachingTip::actionButtonClick, tip, &ExTeachingTip::dismiss );
    connect( placement, qOverload<int>( &QComboBox::currentIndexChanged ), tip, [placement, tip]()
    {
        tip->setPreferredPlacement( static_cast<ExTeachingTip::Placement>( placement->currentData().toInt() ) );
    } );
    connect( lightDismiss, &QCheckBox::toggled, tip, &ExTeachingTip::setIsLightDismissEnabled );
    connect( longText, &QCheckBox::toggled, tip, [tip]( bool checked )
    {
        const QString text = tr( "在这里配置设备参数。提示不会锁定主窗口，按 Esc 或点击关闭按钮即可关闭。" );
        tip->setSubtitle( checked ? ( text + QStringLiteral( "\n\n" ) ).repeated( 18 ) : text );
    } );
    auto* extraOptions = new QHBoxLayout;
    auto* footerClose = new QCheckBox( tr( "使用底部关闭按钮" ), content );
    auto* hideTail = new QCheckBox( tr( "隐藏尾巴" ), content );
    auto* untargeted = new QPushButton( tr( "显示无目标提示" ), content );
    extraOptions->addWidget( footerClose );
    extraOptions->addWidget( hideTail );
    extraOptions->addWidget( untargeted );
    extraOptions->addStretch();
    layout->addLayout( extraOptions );
    connect( footerClose, &QCheckBox::toggled, tip, [tip]( bool checked )
    {
        tip->setCloseButtonContent( checked ? tr( "关闭" ) : QString() );
    } );
    connect( hideTail, &QCheckBox::toggled, tip, [tip]( bool checked )
    {
        tip->setTailVisibility( checked ? ExTeachingTip::TailVisibility::Collapsed : ExTeachingTip::TailVisibility::Auto );
    } );
    connect( untargeted, &QPushButton::clicked, tip, [tip]() { tip->showAt( nullptr ); } );
    auto* targets = new QGridLayout;
    targets->setRowMinimumHeight( 1, 130 );
    targets->setColumnStretch( 1, 1 );
    const auto addTarget = [&]( const QString& text, int row, int column )
    {
        auto* button = new QPushButton( text, content );
        targets->addWidget( button, row, column );
        connect( button, &QPushButton::clicked, tip, [tip, button]() { tip->showAt( button ); } );
    };
    addTarget( tr( "左上目标" ), 0, 0 );
    addTarget( tr( "右上目标" ), 0, 2 );
    addTarget( tr( "中间目标" ), 1, 1 );
    addTarget( tr( "左下目标" ), 2, 0 );
    addTarget( tr( "右下目标" ), 2, 2 );
    layout->addLayout( targets );
    auto* status = new QLabel( tr( "提示已关闭" ), content );
    layout->addWidget( status );
    connect( tip, &ExTeachingTip::isOpenChanged, status, [status]( bool open )
    {
        status->setText( open ? tr( "提示已打开，可继续操作页面" ) : tr( "提示已关闭" ) );
    } );
    layout->addSpacing( 16 );
    layout->addWidget( heading( tr( "分步操作引导" ) ) );
    auto* tourHelp = new QLabel( tr( "点击“开始引导”，依次了解下面三个控件的用途。提示中的“下一步”切换目标，最后一步点击“完成”；按 Esc 或点击叉号可随时退出。" ), content );
    tourHelp->setWordWrap( true );
    layout->addWidget( tourHelp );

    auto* tourPanel = new QFrame( content );
    tourPanel->setFrameShape( QFrame::StyledPanel );
    auto* form = new QFormLayout( tourPanel );
    form->setContentsMargins( 16, 16, 16, 16 );
    form->setSpacing( 16 );

    // 控件 1：普通单行下拉框（中等长方形）
    auto* device = new QComboBox( tourPanel );
    device->addItems( { tr( "桌面扬声器" ), tr( "无线耳机" ), tr( "显示器内置音频" ) } );
    form->addRow( tr( "输出设备" ), device );

    // 控件 2：小尺寸复选框（紧凑型小方形区域）
    auto* spatialAudio = new QCheckBox( tr( "开启杜比全景声与空间音频增强" ), tourPanel );
    spatialAudio->setChecked( true );
    form->addRow( tr( "空间音效" ), spatialAudio );

    // 控件 3：横向细长条滑块（横向较长、高度较矮）
    auto* volume = new QSlider( Qt::Horizontal, tourPanel );
    volume->setRange( 0, 100 );
    volume->setValue( 60 );
    form->addRow( tr( "输出音量" ), volume );

    // 控件 4：大面积多行文本卡片（高度高、宽度大，展现大范围矩形镂空）
    auto* noteEdit = new QTextEdit( tourPanel );
    noteEdit->setFixedHeight( 72 );
    noteEdit->setPlaceholderText( tr( "在此输入当前设备预设与场景备忘..." ) );
    noteEdit->setPlainText( tr( "影院模式预设：强化低频动态响应，优化中高频人声对白表现。" ) );
    form->addRow( tr( "预设备忘" ), noteEdit );

    // 控件 5：固定紧凑尺寸按钮（小巧精致操作按钮，长宽比与之前完全不同）
    auto* btnRow = new QHBoxLayout;
    auto* apply = new QPushButton( tr( "应用配置" ), tourPanel );
    apply->setFixedSize( 110, 32 );
    auto* reset = new QPushButton( tr( "重置" ), tourPanel );
    reset->setFixedSize( 70, 32 );
    btnRow->addWidget( apply );
    btnRow->addWidget( reset );
    btnRow->addStretch();
    form->addRow( QString(), btnRow );

    auto* result = new QLabel( tr( "此处为演示配置，不会修改系统设置。" ), tourPanel );
    result->setWordWrap( true );
    form->addRow( result );
    connect( apply, &QPushButton::clicked, result, [device, volume, result]()
    {
        result->setText( tr( "已应用演示配置：%1，音量 %2%。" ).arg( device->currentText() ).arg( volume->value() ) );
    } );
    connect( reset, &QPushButton::clicked, result, [volume, noteEdit, result]()
    {
        volume->setValue( 50 );
        noteEdit->clear();
        result->setText( tr( "已恢复默认音频参数配置。" ) );
    } );
    layout->addWidget( tourPanel );
    auto* tourRow = new QHBoxLayout;
    auto* startTour = new QPushButton( tr( "普通分步引导 (ExTeachingTip)" ), content );
    auto* startDriverTour = new QPushButton( tr( "聚光灯漫游向导 (Driver.js 效果)" ), content );
    m_tourStatus = new QLabel( tr( "引导尚未开始" ), content );
    tourRow->addWidget( startTour );
    tourRow->addWidget( startDriverTour );
    tourRow->addWidget( m_tourStatus, 1 );
    layout->addLayout( tourRow );

    m_driverTour = new ExTour( this );
    m_driverTour->addStep( device,
                           tr( "1. 选择音频设备" ),
                           tr( "普通单行下拉框：聚光灯精准镂空目标，全屏暗色遮罩自动聚焦视线，气泡带有精准指向小尾巴。" ),
                           ExTourStep::Bottom );
    m_driverTour->addStep( spatialAudio,
                           tr( "2. 紧凑复选控件" ),
                           tr( "小尺寸开关：镂空区域平滑收缩聚焦于复选框，箭头小尾巴自动对齐控件中心。" ),
                           ExTourStep::Bottom );
    m_driverTour->addStep( volume,
                           tr( "3. 细长滑动条" ),
                           tr( "横向长条滑块：镂空区域迅速横向展开形变，补间动画流畅丝滑。" ),
                           ExTourStep::Bottom );
    m_driverTour->addStep( noteEdit,
                           tr( "4. 大面积文本卡片" ),
                           tr( "高大多行编辑框：镂空面积纵向延展扩张，卡片与箭头自适应避让并翻转指向。" ),
                           ExTourStep::Top );
    m_driverTour->addStep( apply,
                           tr( "5. 紧凑型独立按钮" ),
                           tr( "小巧固定尺寸按钮：聚光灯迅速聚焦于保存按钮。点击“完成”即可退出向导。" ),
                           ExTourStep::Top );

    connect( startDriverTour, &QPushButton::clicked, this, [this]()
    {
        if ( m_tourTip )
            m_tourTip->dismiss();
        if ( m_tip )
            m_tip->dismiss();
        m_driverTour->start();
    } );

    m_tourSteps = {
        { device, tr( "选择输出设备" ), tr( "在这里选择声音播放到哪个设备。你可以打开下拉框试试，然后点击“下一步”。" ) },
        { spatialAudio, tr( "开启空间音频" ), tr( "小巧的复选框，可启用虚拟环绕与全景声音效增强。" ) },
        { volume, tr( "调整输出音量" ), tr( "拖动滑块调整音量。引导不会锁定界面，你可以边看提示边操作。" ) },
        { noteEdit, tr( "场景配置备忘" ), tr( "多行大文本框，可在此记录当前设备的专属均衡器描述。" ) },
        { apply, tr( "应用配置" ), tr( "点击“应用配置”保存本次演示选择。点击提示中的“完成”结束引导。" ) }
    };
    m_tourTip = new ExTeachingTip( this );
    m_tourTip->setAnimationEnabled( false );
    m_tourTip->setPreferredPlacement( ExTeachingTip::Top );
    // 只由操作按钮推进步骤；叉号、Esc、目标失效都直接结束，不能自动跳到下一步。
    connect( startTour, &QPushButton::clicked, this, [this]() { showTourStep( 0 ); } );
    connect( m_tourTip, &ExTeachingTip::actionButtonClick, this, [this]()
    {
        if ( m_tourStep < 0 )
            return;
        const int next = m_tourStep + 1;
        if ( next < m_tourSteps.size() )
            showTourStep( next );
        else
            stopTour( true );
    } );
    connect( m_tourTip, &ExTeachingTip::closed, this, [this]()
    {
        if ( m_tourStep >= 0 )
            stopTour();
    } );
    // 两个演示互斥，避免多个浮层遮住对方。
    connect( tip, &ExTeachingTip::opened, this, [this]()
    {
        if ( m_tourStep >= 0 )
            stopTour();
    } );
    layout->addStretch();
}

void PageNavigationHint::showTourStep( int index )
{
    if ( !m_tourTip || !isVisible() || index < 0 || index >= m_tourSteps.size() )
        return;

    // 先完整关闭旧提示，再滚动和切换目标，避免旧目标的移动被当成退出引导。
    m_tourStep = -1;
    m_tourTip->dismiss();
    if ( m_tip )
        m_tip->dismiss();
    const auto& step = m_tourSteps.at( index );
    if ( !step.target || !step.target->isVisible() )
    {
        stopTour();
        return;
    }
    m_scroll->ensureWidgetVisible( step.target, 24, 100 );
    m_tourTip->setTitle( tr( "%1 / %2 · %3" ).arg( index + 1 ).arg( m_tourSteps.size() ).arg( step.title ) );
    m_tourTip->setSubtitle( step.description );
    m_tourTip->setActionButtonContent( index + 1 == m_tourSteps.size() ? tr( "完成" ) : tr( "下一步" ) );
    m_tourStep = index;
    m_tourStatus->setText( tr( "正在引导：第 %1 / %2 步" ).arg( index + 1 ).arg( m_tourSteps.size() ) );
    m_tourTip->showAt( step.target );
    if ( !m_tourTip->isOpen() )
        stopTour();
}

void PageNavigationHint::stopTour( bool completed )
{
    m_tourStep = -1;
    if ( m_tourTip )
        m_tourTip->dismiss();
    m_tourStatus->setText( completed ? tr( "引导已完成，可以重新体验" ) : tr( "引导已退出，可以重新开始" ) );
}

void PageNavigationHint::hideEvent( QHideEvent* event )
{
    QFrame::hideEvent( event );
    // 无目标提示不绑定某个按钮，因此由页面明确管理其可见性。
    if ( m_tip )
        m_tip->dismiss();
    if ( m_tourStep >= 0 )
        stopTour();
    if ( m_driverTour && m_driverTour->isRunning() )
        m_driverTour->exit();
}
