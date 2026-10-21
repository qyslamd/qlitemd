#include <exbreadcrumbbar.h>
#include <exteachingtip.h>

#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVariantAnimation>
#include <QtTest>

class NavigationHintTests final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { qRegisterMetaType<ExTeachingTip::CloseReason>(); }
    void breadcrumbOverflowKeepsOriginalIndex();
    void breadcrumbCanReplacePathInClickHandler();
    void breadcrumbRightToLeft();
    void tipNeedsVisibleTarget();
    void tipStaysClosedWhenParentPageShows();
    void tipFollowsAndClosesWithTarget();
    void tipClosesWhenPageHides();
    void tipLongTextShrinksBack();
    void tipLightDismiss();
    void tipClosesWhenTargetScrollsAway();
    void tipDiesWithHost();
    void breadcrumbTemplatePreservesData();
    void tipClosingCanBeCancelled();
    void tipSupportsUntargetedAndFooterClose();
    void tipCloseButtonSurvivesReopen();
    void tipReopenResetsScrollPosition();
    void tipActionButtonAccent();
    void tipOpenUsesScaleAnimation();
    void tipFirstOpenSnapshotScrollBar_data();
    void tipFirstOpenSnapshotScrollBar();
    void tipPropertyAccessors();
};

void NavigationHintTests::breadcrumbOverflowKeepsOriginalIndex()
{
    ExBreadcrumbBar bar;
    const QVariantList path { QStringLiteral( "主页" ), QStringLiteral( "项目" ), QStringLiteral( "音频设备" ), QStringLiteral( "高级设置" ) };
    bar.setItemsSource( path );
    bar.resize( 110, bar.sizeHint().height() );
    bar.show();
    auto* menu = bar.findChild<QMenu*>();
    QVERIFY( menu );
    QVERIFY( !menu->actions().isEmpty() );
    QSignalSpy clicked( &bar, &ExBreadcrumbBar::itemClicked );
    const int index = menu->actions().size() - 1;
    menu->actions().first()->trigger();
    QCOMPARE( clicked.count(), 1 );
    QCOMPARE( clicked.at( 0 ).at( 0 ).toInt(), index );
    QCOMPARE( clicked.at( 0 ).at( 1 ).toString(), path.at( index ).toString() );
    QCOMPARE( bar.itemsSource(), path );
    bar.resize( bar.sizeHint().width() + 100, bar.height() );
    QTRY_VERIFY( menu->actions().isEmpty() );
    bar.setItemsSource( {} );
    QCOMPARE( bar.itemsSource().size(), 0 );
    QCOMPARE( bar.minimumSizeHint().width(), 0 );
}

void NavigationHintTests::breadcrumbCanReplacePathInClickHandler()
{
    ExBreadcrumbBar bar;
    bar.setItemsSource( { QStringLiteral( "主页" ), QStringLiteral( "设备" ), QStringLiteral( "设置" ) } );
    bar.resize( 600, bar.sizeHint().height() );
    bar.show();
    connect( &bar, &ExBreadcrumbBar::itemClicked, &bar, [&bar]( int index )
    {
        bar.setItemsSource( bar.itemsSource().mid( 0, index + 1 ) );
    } );
    QToolButton* first = nullptr;
    for ( auto* button : bar.findChildren<QToolButton*>( QString(), Qt::FindDirectChildrenOnly ) )
        if ( button->text() == QStringLiteral( "主页" ) )
            first = button;
    QVERIFY( first );
    first->click();
    QCOMPARE( bar.itemsSource(), QVariantList { QStringLiteral( "主页" ) } );
    QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
    QCOMPARE( bar.findChildren<QToolButton*>( QString(), Qt::FindDirectChildrenOnly ).size(), 2 );
}

void NavigationHintTests::breadcrumbRightToLeft()
{
    ExBreadcrumbBar bar;
    bar.setItemsSource( { QStringLiteral( "一级" ), QStringLiteral( "二级" ) } );
    bar.setLayoutDirection( Qt::RightToLeft );
    bar.resize( 600, bar.sizeHint().height() );
    bar.show();
    QList<QToolButton*> items;
    for ( auto* button : bar.findChildren<QToolButton*>( QString(), Qt::FindDirectChildrenOnly ) )
        if ( !button->menu() )
            items.append( button );
    QCOMPARE( items.size(), 2 );
    QVERIFY( items.first()->x() > items.last()->x() );
}

void NavigationHintTests::tipNeedsVisibleTarget()
{
    QWidget host;
    host.resize( 800, 600 );
    QPushButton target( &host );
    auto* tip = new ExTeachingTip( &host );
    tip->show();
    QVERIFY( !tip->isOpen() );
    tip->showAt( &target );
    QVERIFY( !tip->isOpen() );
    host.show();
    tip->showAt( &target );
    QVERIFY( tip->isOpen() );
    QVERIFY( !tip->isWindow() );
    QCOMPARE( tip->parentWidget(), &host );
}

void NavigationHintTests::tipStaysClosedWhenParentPageShows()
{
    QWidget host;
    host.resize( 800, 600 );
    QWidget page( &host );
    page.setGeometry( host.rect() );
    auto* tip = new ExTeachingTip( &page );
    tip->setTitle( QStringLiteral( "试试快捷配置" ) );
    auto* tourTip = new ExTeachingTip( &page );
    host.show();
    QVERIFY( !tip->isOpen() );
    QVERIFY( !tip->isVisible() );
    QVERIFY( !tourTip->isOpen() );
    QVERIFY( !tourTip->isVisible() );
    page.hide();
    page.show();
    QVERIFY( !tip->isOpen() );
    QVERIFY( !tip->isVisible() );
    QVERIFY( !tourTip->isOpen() );
    QVERIFY( !tourTip->isVisible() );
}

void NavigationHintTests::tipFollowsAndClosesWithTarget()
{
    QWidget host;
    host.resize( 900, 700 );
    auto* target = new QPushButton( &host );
    target->setGeometry( 400, 350, 100, 32 );
    auto* tip = new ExTeachingTip( &host );
    tip->setTitle( QStringLiteral( "引导" ) );
    host.show();
    tip->showAt( target );
    QVERIFY( tip->isOpen() );
    const QPoint oldPosition = tip->pos();
    target->move( 500, 420 );
    QTRY_VERIFY( tip->pos() != oldPosition );
    QVERIFY( host.rect().contains( tip->geometry() ) );
    QSignalSpy closed( tip, &ExTeachingTip::closed );
    delete target;
    QVERIFY( !tip->isOpen() );
    QVERIFY( !tip->target() );
    QCOMPARE( closed.count(), 1 );
    tip->dismiss();
    QCOMPARE( closed.count(), 1 );
}

void NavigationHintTests::tipClosesWhenPageHides()
{
    QWidget host;
    host.resize( 800, 600 );
    QWidget page( &host );
    page.setGeometry( host.rect() );
    QPushButton target( &page );
    target.setGeometry( 300, 300, 100, 32 );
    auto* tip = new ExTeachingTip( &host );
    host.show();
    tip->showAt( &target );
    QVERIFY( tip->isOpen() );
    page.hide();
    QVERIFY( !tip->isOpen() );
    page.show();
    QVERIFY( !tip->isOpen() );
}

void NavigationHintTests::tipLongTextShrinksBack()
{
    QWidget host;
    host.resize( 800, 600 );
    QPushButton target( &host );
    target.setGeometry( 350, 300, 100, 32 );
    auto* tip = new ExTeachingTip( &host );
    tip->setTitle( QStringLiteral( "引导标题" ) );
    tip->setSubtitle( QStringLiteral( "短内容" ) );
    host.show();
    tip->showAt( &target );
    QCoreApplication::processEvents();
    const int shortHeight = tip->height();
    tip->setSubtitle( QStringLiteral( "这是需要换行的很长的说明内容。\n" ).repeated( 100 ) );
    QTRY_VERIFY( tip->height() > shortHeight );
    QVERIFY( host.rect().contains( tip->geometry() ) );
    auto* scroll = tip->findChild<QScrollArea*>();
    QVERIFY( scroll );
    QTRY_VERIFY( scroll->verticalScrollBar()->maximum() > 0 );
    tip->setSubtitle( QStringLiteral( "短内容" ) );
    QTRY_COMPARE( tip->height(), shortHeight );
    QTRY_COMPARE( scroll->verticalScrollBar()->maximum(), 0 );
}

void NavigationHintTests::tipLightDismiss()
{
    QWidget host;
    host.resize( 800, 600 );
    QPushButton target( &host );
    target.setGeometry( 350, 300, 100, 32 );
    auto* tip = new ExTeachingTip( &host );
    host.show();
    tip->showAt( &target );
    QTest::mouseClick( &host, Qt::LeftButton, Qt::NoModifier, QPoint( 10, 10 ) );
    QVERIFY( tip->isOpen() );
    tip->setIsLightDismissEnabled( true );
    QTest::mouseClick( &host, Qt::LeftButton, Qt::NoModifier, QPoint( 10, 10 ) );
    QVERIFY( !tip->isOpen() );
    tip->showAt( &target );
    QTest::keyClick( &target, Qt::Key_Escape );
    QVERIFY( !tip->isOpen() );
}

void NavigationHintTests::tipClosesWhenTargetScrollsAway()
{
    QWidget host;
    host.resize( 800, 600 );
    QScrollArea area( &host );
    area.setGeometry( 20, 20, 500, 300 );
    auto* content = new QWidget;
    content->resize( 450, 1600 );
    auto* target = new QPushButton( content );
    target->setGeometry( 160, 40, 100, 32 );
    area.setWidget( content );
    auto* tip = new ExTeachingTip( &host );
    host.show();
    tip->showAt( target );
    QVERIFY( tip->isOpen() );
    area.verticalScrollBar()->setValue( 900 );
    QTRY_VERIFY( !tip->isOpen() );
}

void NavigationHintTests::tipDiesWithHost()
{
    auto* host = new QWidget;
    host->resize( 800, 600 );
    auto* target = new QPushButton( host );
    target->setGeometry( 300, 300, 100, 32 );
    QPointer<ExTeachingTip> tip = new ExTeachingTip;
    host->show();
    tip->showAt( target );
    QVERIFY( tip->isOpen() );
    delete host;
    QVERIFY( tip.isNull() );
}

void NavigationHintTests::breadcrumbTemplatePreservesData()
{
    class WideDelegate final : public QStyledItemDelegate
    {
    public:
        QSize sizeHint( const QStyleOptionViewItem&, const QModelIndex& ) const override { return QSize( 140, 24 ); }
    };
    ExBreadcrumbBar bar;
    auto* itemTemplate = new WideDelegate;
    const QVariantMap first { { QStringLiteral( "id" ), 1 }, { QStringLiteral( "title" ), QStringLiteral( "设备" ) } };
    const QVariantMap second { { QStringLiteral( "id" ), 2 }, { QStringLiteral( "title" ), QStringLiteral( "设置" ) } };
    bar.setItemTemplate( itemTemplate );
    bar.setItemsSource( { first, second } );
    bar.resize( 170, bar.sizeHint().height() );
    bar.show();
    QSignalSpy clicked( &bar, &ExBreadcrumbBar::itemClicked );
    auto* menu = bar.findChild<QMenu*>();
    QVERIFY( menu && menu->actions().size() == 1 );
    menu->actions().first()->trigger();
    QCOMPARE( clicked.count(), 1 );
    QCOMPARE( clicked.first().at( 1 ).toMap(), first );
    delete itemTemplate;
    QVERIFY( !bar.itemTemplate() );
    QCOMPARE( bar.itemsSource().size(), 2 );
}

void NavigationHintTests::tipClosingCanBeCancelled()
{
    QWidget host;
    host.resize( 800, 600 );
    QPushButton target( &host );
    target.setGeometry( 300, 300, 100, 32 );
    auto* tip = new ExTeachingTip( &host );
    host.show();
    tip->showAt( &target );
    bool cancelClose = true;
    connect( tip, &ExTeachingTip::closing, tip, [&]( ExTeachingTip::CloseReason, bool* cancel )
    {
        *cancel = cancelClose;
    }, Qt::DirectConnection );
    QSignalSpy closed( tip, &ExTeachingTip::closed );
    tip->setIsOpen( false );
    QVERIFY( tip->isOpen() );
    QCOMPARE( closed.count(), 0 );
    cancelClose = false;
    tip->setIsOpen( false );
    QVERIFY( !tip->isOpen() );
    QCOMPARE( closed.count(), 1 );
    QCOMPARE( qvariant_cast<ExTeachingTip::CloseReason>( closed.first().at( 0 ) ), ExTeachingTip::CloseReason::Programmatic );
    tip->showAt( &target );
    cancelClose = true;
    target.hide();
    QVERIFY( !tip->isOpen() );
}

void NavigationHintTests::tipSupportsUntargetedAndFooterClose()
{
    QWidget host;
    host.resize( 800, 600 );
    auto* tip = new ExTeachingTip( &host );
    tip->setCloseButtonContent( QStringLiteral( "知道了" ) );
    host.show();
    tip->setIsOpen( true );
    QVERIFY( tip->isOpen() );
    QVERIFY( !tip->target() );
    QVERIFY( host.rect().contains( tip->geometry() ) );
    QTRY_VERIFY( tip->closeButton()->isVisible() );
    QSignalSpy clicked( tip, &ExTeachingTip::closeButtonClick );
    QSignalSpy closed( tip, &ExTeachingTip::closed );
    tip->closeButton()->click();
    QCOMPARE( clicked.count(), 1 );
    QCOMPARE( closed.count(), 1 );
    QCOMPARE( qvariant_cast<ExTeachingTip::CloseReason>( closed.first().at( 0 ) ), ExTeachingTip::CloseReason::CloseButton );
    // 标题关闭按钮不再使用会随平台主题错配的标准标题栏图标。
    const auto buttons = tip->findChildren<QToolButton*>();
    QVERIFY( !buttons.isEmpty() );
    QVERIFY( buttons.first()->icon().isNull() );
}

void NavigationHintTests::tipCloseButtonSurvivesReopen()
{
    QWidget host;
    host.resize( 800, 600 );
    QPushButton target( &host );
    target.setGeometry( 350, 300, 100, 32 );
    auto* tip = new ExTeachingTip( &host );
    tip->setTitle( QStringLiteral( "引导标题" ) );
    tip->setSubtitle( QStringLiteral( "短内容" ) );
    host.show();
    auto* close = tip->findChild<QToolButton*>();
    QVERIFY( close );
    for ( int i = 0; i < 10; ++i )
    {
        tip->showAt( &target );
        QTRY_VERIFY( !close->visibleRegion().isEmpty() );
        QTest::mouseClick( close, Qt::LeftButton );
        QVERIFY( !tip->isOpen() );
    }
    tip->showAt( &target );
    tip->setIsLightDismissEnabled( true );
    QVERIFY( close->isHidden() );
    tip->setIsLightDismissEnabled( false );
    QTRY_VERIFY( !close->visibleRegion().isEmpty() );
    tip->setCloseButtonContent( QStringLiteral( "关闭" ) );
    QVERIFY( close->isHidden() );
    tip->setCloseButtonContent( QString() );
    QTRY_VERIFY( !close->visibleRegion().isEmpty() );
}

void NavigationHintTests::tipReopenResetsScrollPosition()
{
    QWidget host;
    host.resize( 800, 600 );
    QPushButton target( &host );
    target.setGeometry( 350, 300, 100, 32 );
    auto* tip = new ExTeachingTip( &host );
    tip->setTitle( QStringLiteral( "引导标题" ) );
    tip->setSubtitle( QStringLiteral( "长内容\n" ).repeated( 100 ) );
    host.show();
    tip->showAt( &target );
    auto* scroll = tip->findChild<QScrollArea*>();
    auto* close = tip->findChild<QToolButton*>();
    QVERIFY( scroll && close );
    QTRY_VERIFY( scroll->verticalScrollBar()->maximum() > 0 );
    for ( int i = 0; i < 3; ++i )
    {
        scroll->verticalScrollBar()->setValue( scroll->verticalScrollBar()->maximum() );
        QTRY_VERIFY( close->visibleRegion().isEmpty() );
        tip->dismiss();
        tip->showAt( &target );
        QTRY_COMPARE( scroll->verticalScrollBar()->value(), 0 );
        QTRY_VERIFY( !close->visibleRegion().isEmpty() );
    }
}

void NavigationHintTests::tipActionButtonAccent()
{
    ExTeachingTip tip;
    QVERIFY( tip.actionButtonAccent() );
    QVERIFY( tip.actionButton()->property( "accent" ).toBool() );
    QVERIFY( !tip.actionButton()->isDefault() );
    QVERIFY( !tip.closeButton()->property( "accent" ).toBool() );
    tip.setActionButtonAccent( false );
    QVERIFY( !tip.actionButtonAccent() );
    QVERIFY( !tip.actionButton()->property( "accent" ).toBool() );
    QVERIFY( tip.setProperty( "actionButtonAccent", true ) );
    QVERIFY( tip.actionButtonAccent() );
    QVERIFY( tip.actionButton()->property( "accent" ).toBool() );
    // 与直接配置底层 QPushButton 共用一份状态，避免两套属性相互覆盖。
    tip.actionButton()->setProperty( "accent", false );
    QVERIFY( !tip.actionButtonAccent() );
}

void NavigationHintTests::tipOpenUsesScaleAnimation()
{
    QWidget host;
    host.resize( 800, 600 );
    QPushButton target( &host );
    target.setGeometry( 350, 300, 100, 32 );
    auto* tip = new ExTeachingTip( &host );
    tip->setTitle( QStringLiteral( "缩放动画" ) );
    host.show();
    tip->showAt( &target );

    auto* animation = tip->findChild<QVariantAnimation*>();
    auto* scroll = tip->findChild<QScrollArea*>();
    QVERIFY( animation && scroll );
    QCOMPARE( animation->startValue().toDouble(), 0.01 );
    QCOMPARE( animation->endValue().toDouble(), 1.0 );
    QCOMPARE( animation->duration(), 167 );
    QCOMPARE( animation->easingCurve().type(), QEasingCurve::OutCubic );
    QCOMPARE( animation->state(), QAbstractAnimation::Running );
    QVERIFY( scroll->isHidden() );
    const QRect animationGeometry = tip->geometry();
    // 显示后宿主产生的延迟布局请求不能在下一轮事件循环中取消动画。
    QEvent layoutRequest( QEvent::LayoutRequest );
    QCoreApplication::sendEvent( &host, &layoutRequest );
    QTest::qWait( 50 );
    QCOMPARE( animation->state(), QAbstractAnimation::Running );
    QTRY_COMPARE( animation->state(), QAbstractAnimation::Stopped );
    QTRY_VERIFY( scroll->isVisible() );
    QCOMPARE( tip->geometry(), animationGeometry );
}

void NavigationHintTests::tipFirstOpenSnapshotScrollBar_data()
{
    QTest::addColumn<int>( "contentHeight" );
    QTest::addColumn<bool>( "needsScrollBar" );
    QTest::newRow( "short-content" ) << 40 << false;
    QTest::newRow( "overflow-content" ) << 1000 << true;
}

void NavigationHintTests::tipFirstOpenSnapshotScrollBar()
{
    QFETCH( int, contentHeight );
    QFETCH( bool, needsScrollBar );
    class PaintCounter final : public QObject
    {
    public:
        int paints = 0;
        bool eventFilter( QObject*, QEvent* event ) override
        {
            if ( event->type() == QEvent::Paint )
                ++paints;
            return false;
        }
    } counter;

    QWidget host;
    host.resize( 800, 600 );
    QPushButton target( &host );
    target.setGeometry( 350, 300, 100, 32 );
    ExTeachingTip tip( &host );
    tip.setTitle( QStringLiteral( "首次显示" ) );
    auto* content = new QLabel( QStringLiteral( "提示内容" ) );
    content->setFixedHeight( contentHeight );
    tip.setContent( content );
    auto* scroll = tip.findChild<QScrollArea*>();
    QVERIFY( scroll );
    auto* bar = scroll->verticalScrollBar();
    bar->installEventFilter( &counter );
    host.show();
    tip.showAt( &target );

    // 尚未进入事件循环，此时的 Paint 来自同步生成动画快照。
    QCOMPARE( counter.paints > 0, needsScrollBar );
    QCOMPARE( bar->maximum() > 0, needsScrollBar );
    auto* animation = tip.findChild<QVariantAnimation*>();
    QVERIFY( animation );
    QTRY_COMPARE( animation->state(), QAbstractAnimation::Stopped );
    QCOMPARE( bar->isVisible(), needsScrollBar );
}

void NavigationHintTests::tipPropertyAccessors()
{
    ExTeachingTip tip;
    // d 指针声明宏仍需向 Qt 元对象系统暴露原来的属性名。
    const char* properties[] = { "title", "subtitle", "actionButtonContent", "actionButtonAccent",
                                 "closeButtonContent", "target", "content", "heroContent", "iconSource",
                                 "tailVisibility", "placementMargin", "heroContentPlacement",
                                 "preferredPlacement", "isLightDismissEnabled", "isOpen" };
    for ( const char* name : properties )
        QVERIFY2( tip.metaObject()->indexOfProperty( name ) >= 0, name );
    QVERIFY( tip.setProperty( "title", QStringLiteral( "标题" ) ) );
    QCOMPARE( tip.title(), QStringLiteral( "标题" ) );
    QVERIFY( tip.setProperty( "subtitle", QStringLiteral( "说明" ) ) );
    QCOMPARE( tip.subtitle(), QStringLiteral( "说明" ) );
    tip.setActionButtonContent( QStringLiteral( "下一步" ) );
    QCOMPARE( tip.actionButton()->text(), tip.actionButtonContent() );
    tip.setCloseButtonContent( QStringLiteral( "退出" ) );
    QCOMPARE( tip.closeButton()->text(), tip.closeButtonContent() );
    tip.setPlacementMargin( QMargins( 1, 2, 3, 4 ) );
    QCOMPARE( tip.placementMargin(), QMargins( 1, 2, 3, 4 ) );
    tip.setPreferredPlacement( ExTeachingTip::BottomRight );
    QCOMPARE( tip.preferredPlacement(), ExTeachingTip::BottomRight );
    tip.setHeroContentPlacement( ExTeachingTip::HeroContentPlacement::Bottom );
    QCOMPARE( tip.heroContentPlacement(), ExTeachingTip::HeroContentPlacement::Bottom );
    tip.setTailVisibility( ExTeachingTip::TailVisibility::Collapsed );
    QCOMPARE( tip.tailVisibility(), ExTeachingTip::TailVisibility::Collapsed );
    tip.setIsLightDismissEnabled( true );
    QVERIFY( tip.isLightDismissEnabled() );
    // 内容仍使用弱引用，外部删除后 getter 不能返回悬空指针。
    auto* content = new QLabel( QStringLiteral( "自定义内容" ) );
    tip.setContent( content );
    QCOMPARE( tip.content(), static_cast<QWidget*>( content ) );
    delete content;
    QVERIFY( !tip.content() );
}

QTEST_MAIN( NavigationHintTests )
#include "tst_navigationhint.moc"
