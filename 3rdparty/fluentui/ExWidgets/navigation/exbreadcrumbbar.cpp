#include "exbreadcrumbbar.h"
#include "exfonticon.h"

#include <QAbstractItemDelegate>
#include <QAction>
#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyleOptionFocusRect>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QWidgetAction>

namespace {
// WinUI 3 BreadcrumbBarChevronFontSize=12，Chevron 区域宽度 16px
constexpr int SeparatorWidth        = 16;
constexpr int ItemHorizontalPadding = 8;
constexpr int ItemHeight            = 28;
constexpr qreal ItemCornerRadius    = 4.0;

class BreadcrumbButton final : public QToolButton
{
public:
    explicit BreadcrumbButton( QWidget* parent = nullptr )
        : QToolButton( parent )
    {
        setAutoRaise( true );
        setFocusPolicy( Qt::StrongFocus );
        setToolButtonStyle( Qt::ToolButtonTextOnly );
        setAttribute( Qt::WA_Hover, true );
    }

    QPointer<QAbstractItemDelegate> itemTemplate;
    QPersistentModelIndex modelIndex;
    bool current    = false;
    bool isOverflow = false;
    bool dropDown   = false;

    void updateInteraction()
    {
        if ( current )
        {
            setCursor( Qt::ArrowCursor );
        }
        else
        {
            setCursor( Qt::PointingHandCursor );
        }
    }

    QSize sizeHint() const override
    {
        if ( itemTemplate && modelIndex.isValid() )
        {
            const QSize delegateSize = itemTemplate->sizeHint( viewOption(), modelIndex );
            return QSize( delegateSize.width() + ItemHorizontalPadding * 2, qMax( ItemHeight, delegateSize.height() ) );
        }

        QFont itemFont = font();
        if ( current )
        {
            itemFont.setBold( true );
        }
        const QFontMetrics fm( itemFont );
        const int textW = fm.horizontalAdvance( text() );
        const int w     = textW + ItemHorizontalPadding * 2;
        const int h     = qMax( ItemHeight, fm.height() + 8 );
        return QSize( w, h );
    }

protected:
    void paintEvent( QPaintEvent* ) override
    {
        QPainter painter( this );
        painter.setRenderHint( QPainter::Antialiasing );

        const bool isDark      = palette().color( QPalette::Window ).lightness() < 128;
        const bool interactive = !current || isOverflow || dropDown;

        // 1. WinUI 3 Subtle 悬停/按下卡片微底色（当前项/最后一项不绘制背景）
        if ( interactive && ( underMouse() || isDown() ) )
        {
            QColor hoverColor;
            if ( isDown() )
            {
                hoverColor = isDark ? QColor( 255, 255, 255, 24 ) : QColor( 0, 0, 0, 24 );
            }
            else
            {
                hoverColor = isDark ? QColor( 255, 255, 255, 18 ) : QColor( 0, 0, 0, 15 );
            }
            painter.setPen( Qt::NoPen );
            painter.setBrush( hoverColor );
            painter.drawRoundedRect( QRectF( rect() ).adjusted( 0.5, 0.5, -0.5, -0.5 ), ItemCornerRadius, ItemCornerRadius );
        }

        // 2. 自定义 ItemTemplate 委托绘制
        if ( itemTemplate && modelIndex.isValid() )
        {
            painter.save();
            auto option = viewOption();
            itemTemplate->paint( &painter, option, modelIndex );
            painter.restore();
        }
        else
        {
            // 3. WinUI 3 标准分级文字绘制
            QFont textFont = font();
            if ( current )
            {
                textFont.setBold( true );
            }
            painter.setFont( textFont );
            const QFontMetrics fm( textFont );

            QColor textColor = palette().color( isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::WindowText );
            if ( isEnabled() )
            {
                if ( current )
                {
                    // 当前项：主文字色（100% WindowText）高亮加粗
                }
                else if ( underMouse() )
                {
                    // 悬停祖先项：提亮为主文字色
                }
                else
                {
                    // 常规祖先项：次级文字色（TextFillColorSecondary，约 72% alpha）
                    textColor.setAlphaF( textColor.alphaF() * 0.72 );
                }
            }

            const QRect textRect = rect().adjusted( ItemHorizontalPadding, 0, -ItemHorizontalPadding, 0 );
            painter.setPen( textColor );
            painter.drawText( textRect,
                              Qt::AlignVCenter | Qt::AlignLeading | Qt::TextSingleLine,
                              fm.elidedText( text(), Qt::ElideRight, qMax( 0, textRect.width() ) ) );
        }

        // 4. 键盘焦点框
        if ( hasFocus() )
        {
            QStyleOptionFocusRect focus;
            focus.initFrom( this );
            focus.rect = rect().adjusted( 1, 1, -1, -1 );
            style()->drawPrimitive( QStyle::PE_FrameFocusRect, &focus, &painter, this );
        }
    }

private:
    QStyleOptionViewItem viewOption() const
    {
        QStyleOptionViewItem option;
        option.initFrom( this );
        option.font = font();
        if ( current )
        {
            option.font.setBold( true );
        }
        option.fontMetrics      = QFontMetrics( option.font );
        option.widget           = this;
        option.rect             = rect().adjusted( ItemHorizontalPadding, 2, -ItemHorizontalPadding, -2 );
        option.textElideMode    = Qt::ElideRight;
        option.displayAlignment = Qt::AlignVCenter | Qt::AlignLeading;
        return option;
    }
};
}  // namespace

class ExBreadcrumbBarPrivate
{
    Q_DECLARE_PUBLIC( ExBreadcrumbBar )

public:
    explicit ExBreadcrumbBarPrivate( ExBreadcrumbBar* q )
        : q_ptr( q )
    {
    }

    ExBreadcrumbBar* q_ptr = nullptr;
    QVariantList items;
    QStandardItemModel* model = nullptr;
    QPointer<QAbstractItemDelegate> itemTemplate;
    QMetaObject::Connection templateDestroyed;
    QMetaObject::Connection templateSizeChanged;
    QList<BreadcrumbButton*> buttons;
    BreadcrumbButton* overflowButton = nullptr;
    QMenu* overflowMenu              = nullptr;
    QList<QRect> separators;
    int firstVisible = -1;

    void setupUi();
    void layoutItems();
    void rebuildItems();
    void activateItem( int index );
};

void ExBreadcrumbBarPrivate::setupUi()
{
    QFont itemFont = q_ptr->font();
    itemFont.setPixelSize( 14 );
    q_ptr->setFont( itemFont );
    q_ptr->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );

    model                      = new QStandardItemModel( q_ptr );
    overflowButton             = new BreadcrumbButton( q_ptr );
    overflowButton->isOverflow = true;
    overflowButton->setText( QStringLiteral( "\u2026" ) );
    overflowButton->setAccessibleName( ExBreadcrumbBar::tr( "上级路径" ) );
    overflowButton->setToolTip( ExBreadcrumbBar::tr( "显示折叠的路径" ) );
    overflowButton->setPopupMode( QToolButton::InstantPopup );
    overflowButton->updateInteraction();

    overflowMenu = new QMenu( overflowButton );
    overflowButton->setMenu( overflowMenu );
    overflowButton->installEventFilter( q_ptr );
    overflowButton->hide();
}

void ExBreadcrumbBarPrivate::rebuildItems()
{
    QWidget* focused    = QApplication::focusWidget();
    const bool hadFocus = focused && ( focused == q_ptr || q_ptr->isAncestorOf( focused ) );

    overflowMenu->hide();
    overflowMenu->clear();

    for ( auto* button : buttons )
    {
        button->hide();
        button->removeEventFilter( q_ptr );
        QObject::disconnect( button, nullptr, q_ptr, nullptr );
        button->deleteLater();
    }
    buttons.clear();
    model->clear();

    const int count = items.size();
    for ( int i = 0; i < count; ++i )
    {
        auto* item = new QStandardItem( items.at( i ).toString() );
        item->setData( items.at( i ), Qt::UserRole );
        model->appendRow( item );

        auto* button         = new BreadcrumbButton( q_ptr );
        button->modelIndex   = model->index( i, 0 );
        button->itemTemplate = itemTemplate;
        button->current      = ( i == count - 1 );
        button->updateInteraction();
        button->setText( item->text() );
        button->setAccessibleName( item->text() );
        button->setToolTip( item->text() );
        button->installEventFilter( q_ptr );

        // WinUI 3 规范：最后一项是只读的当前位置标识，不可点击；祖先项点击触发 itemClicked
        if ( !button->current )
        {
            QObject::connect( button, &QToolButton::clicked, q_ptr, [ this, i ]() { activateItem( i ); } );
        }

        buttons.append( button );
    }

    firstVisible = -1;
    layoutItems();
    if ( hadFocus && !buttons.isEmpty() )
    {
        buttons.last()->setFocus( Qt::OtherFocusReason );
    }
    q_ptr->updateGeometry();
}

void ExBreadcrumbBarPrivate::activateItem( int index )
{
    if ( index < 0 || index >= items.size() )
    {
        return;
    }
    const QVariant item = items.at( index );
    Q_EMIT q_ptr->itemClicked( index, item );
}

void ExBreadcrumbBarPrivate::layoutItems()
{
    if ( !overflowButton || !overflowMenu )
    {
        return;
    }
    separators.clear();

    const int count         = int( buttons.size() );
    const int available     = qMax( 0, q_ptr->contentsRect().width() );
    const int overflowWidth = qMax( 28, overflowButton->sizeHint().width() );

    int first = 0;
    // WinUI 3 BreadcrumbLayout：从末尾项向前保留可完整容纳的项目
    if ( count > 1 && q_ptr->sizeHint().width() > available )
    {
        first    = count - 1;
        int used = overflowWidth + SeparatorWidth + buttons.last()->sizeHint().width();
        while ( first > 1 && used + SeparatorWidth + buttons.at( first - 1 )->sizeHint().width() <= available )
        {
            --first;
            used += SeparatorWidth + buttons.at( first )->sizeHint().width();
        }
    }

    if ( first != firstVisible )
    {
        overflowMenu->hide();
        qDeleteAll( overflowMenu->actions() );
        overflowMenu->clear();
        // WinUI 3 CloneEllipsisItemSource 逆序排列（最近的父级在最上方）
        for ( int i = first - 1; i >= 0; --i )
        {
            QAction* action = nullptr;
            if ( itemTemplate )
            {
                auto* widgetAction   = new QWidgetAction( overflowMenu );
                auto* button         = new BreadcrumbButton( overflowMenu );
                button->dropDown     = true;
                button->itemTemplate = itemTemplate;
                button->modelIndex   = model->index( i, 0 );
                button->setText( items.at( i ).toString() );
                button->updateInteraction();
                widgetAction->setDefaultWidget( button );
                overflowMenu->addAction( widgetAction );
                QObject::connect( button,
                                  &QToolButton::clicked,
                                  widgetAction,
                                  [ this, widgetAction ]()
                                  {
                                      overflowMenu->hide();
                                      widgetAction->trigger();
                                  } );
                action = widgetAction;
            }
            else
            {
                QString text = items.at( i ).toString();
                text.replace( QStringLiteral( "&" ), QStringLiteral( "&&" ) );
                action = overflowMenu->addAction( text );
            }
            QObject::connect( action, &QAction::triggered, q_ptr, [ this, i ]() { activateItem( i ); } );
        }
        firstVisible = first;
    }

    const bool overflow = first > 0;
    overflowButton->setVisible( overflow );

    int x       = q_ptr->contentsRect().left();
    const int h = qMin( q_ptr->contentsRect().height(), q_ptr->sizeHint().height() );
    const int y = q_ptr->contentsRect().top() + ( q_ptr->contentsRect().height() - h ) / 2;

    const auto place = [ this, y, h ]( QWidget* widget, int left, int w )
    { widget->setGeometry( QStyle::visualRect( q_ptr->layoutDirection(), q_ptr->contentsRect(), QRect( left, y, qMax( 0, w ), h ) ) ); };

    // 放置溢出按钮与后续分隔符
    if ( overflow )
    {
        place( overflowButton, x, qMin( available, overflowWidth ) );
        x += overflowWidth;
        separators.append( QStyle::visualRect( q_ptr->layoutDirection(), q_ptr->contentsRect(), QRect( x, y, SeparatorWidth, h ) ) );
        x += SeparatorWidth;
    }

    bool restoreFocus = false;
    for ( int i = 0; i < count; ++i )
    {
        auto* button = buttons.at( i );
        restoreFocus |= ( i < first && button->hasFocus() );
        button->setVisible( i >= first );
        if ( i < first )
        {
            continue;
        }

        // 若不是第一个可见项（且之前没加溢出分隔符），则在它前面添加 Chevron 分隔符
        if ( i > first )
        {
            separators.append( QStyle::visualRect( q_ptr->layoutDirection(), q_ptr->contentsRect(), QRect( x, y, SeparatorWidth, h ) ) );
            x += SeparatorWidth;
        }

        const int w = qMin( button->sizeHint().width(), qMax( 0, q_ptr->contentsRect().right() + 1 - x ) );
        place( button, x, w );
        x += w;
    }

    if ( restoreFocus )
    {
        overflowButton->setFocus( Qt::OtherFocusReason );
    }
    q_ptr->setFocusProxy( overflow ? overflowButton : ( count ? buttons.first() : nullptr ) );
    q_ptr->update();
}

ExBreadcrumbBar::ExBreadcrumbBar( QWidget* parent )
    : QWidget( parent )
    , d_ptr( new ExBreadcrumbBarPrivate( this ) )
{
    Q_D( ExBreadcrumbBar );
    d->setupUi();
}

ExBreadcrumbBar::~ExBreadcrumbBar()
{
    Q_D( ExBreadcrumbBar );
    disconnect( d->templateDestroyed );
    disconnect( d->templateSizeChanged );
}

QVariantList ExBreadcrumbBar::itemsSource() const
{
    Q_D( const ExBreadcrumbBar );
    return d->items;
}

void ExBreadcrumbBar::setItemsSource( QVariantList items )
{
    Q_D( ExBreadcrumbBar );
    if ( items == d->items )
    {
        return;
    }
    d->items = std::move( items );
    d->rebuildItems();
    Q_EMIT itemsSourceChanged( d->items );
}

void ExBreadcrumbBar::setItems( const QStringList& items )
{
    QVariantList list;
    list.reserve( items.size() );
    for ( const QString& str : items )
    {
        list.append( str );
    }
    setItemsSource( list );
}

QAbstractItemDelegate* ExBreadcrumbBar::itemTemplate() const
{
    Q_D( const ExBreadcrumbBar );
    return d->itemTemplate.data();
}

void ExBreadcrumbBar::setItemTemplate( QAbstractItemDelegate* itemTemplate )
{
    Q_D( ExBreadcrumbBar );
    if ( itemTemplate == d->itemTemplate )
    {
        return;
    }
    disconnect( d->templateDestroyed );
    disconnect( d->templateSizeChanged );
    d->itemTemplate = itemTemplate;
    if ( itemTemplate )
    {
        d->templateDestroyed   = connect( itemTemplate,
                                          &QObject::destroyed,
                                          this,
                                          [ this ]()
                                          {
                                            Q_D( ExBreadcrumbBar );
                                            d->itemTemplate.clear();
                                            d->rebuildItems();
                                            Q_EMIT itemTemplateChanged( nullptr );
                                          } );
        d->templateSizeChanged = connect( itemTemplate,
                                          &QAbstractItemDelegate::sizeHintChanged,
                                          this,
                                          [ this ]()
                                          {
                                              Q_D( ExBreadcrumbBar );
                                              d->layoutItems();
                                              updateGeometry();
                                          } );
    }
    d->rebuildItems();
    Q_EMIT itemTemplateChanged( itemTemplate );
}

QSize ExBreadcrumbBar::sizeHint() const
{
    Q_D( const ExBreadcrumbBar );
    int w = 0;
    int h = qMax( 32, fontMetrics().height() + 10 );
    for ( const auto* button : d->buttons )
    {
        w += button->sizeHint().width();
        h = qMax( h, button->sizeHint().height() );
    }
    const int sepCount = qMax( 0, int( d->buttons.size() ) - 1 );
    return QSize( w + sepCount * SeparatorWidth, h );
}

QSize ExBreadcrumbBar::minimumSizeHint() const
{
    Q_D( const ExBreadcrumbBar );
    return QSize( d->items.isEmpty() ? 0 : ( d->items.size() == 1 ? 16 : 56 ), sizeHint().height() );
}

void ExBreadcrumbBar::resizeEvent( QResizeEvent* event )
{
    QWidget::resizeEvent( event );
    Q_D( ExBreadcrumbBar );
    d->layoutItems();
}

void ExBreadcrumbBar::changeEvent( QEvent* event )
{
    QWidget::changeEvent( event );
    if ( event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange || event->type() == QEvent::LayoutDirectionChange
         || event->type() == QEvent::PaletteChange )
    {
        Q_D( ExBreadcrumbBar );
        d->layoutItems();
        updateGeometry();
        update();
    }
}

void ExBreadcrumbBar::paintEvent( QPaintEvent* )
{
    Q_D( ExBreadcrumbBar );
    if ( d->separators.isEmpty() )
    {
        return;
    }

    QPainter painter( this );
    painter.setRenderHint( QPainter::Antialiasing );

    painter.setFont( ExFontIcon::iconFont( 13 ) );

    // WinUI 3 规范：分隔符使用次级文字色（约 65% alpha），清晰自然
    QColor chevronColor = palette().color( isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::WindowText );
    chevronColor.setAlphaF( chevronColor.alphaF() * 0.65 );
    painter.setPen( chevronColor );

    const QChar chevronChar = isRightToLeft()
                                  ? ExFontIcon::iconChar( SegoeIcon::ChevronLeft )
                                  : ExFontIcon::iconChar( SegoeIcon::ChevronRight );
    const QString glyph( chevronChar );

    for ( const QRect& sepRect : d->separators )
    {
        painter.drawText( sepRect, Qt::AlignCenter, glyph );
    }
}

bool ExBreadcrumbBar::eventFilter( QObject* watched, QEvent* event )
{
    Q_D( ExBreadcrumbBar );
    if ( event->type() == QEvent::KeyPress )
    {
        auto* key    = static_cast<QKeyEvent*>( event );
        auto* button = qobject_cast<QToolButton*>( watched );
        if ( button && ( key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter ) )
        {
            if ( button == d->overflowButton )
            {
                button->showMenu();
            }
            else
            {
                button->click();
            }
            return true;
        }
        QList<QToolButton*> visible;
        if ( !d->overflowButton->isHidden() )
        {
            visible.append( d->overflowButton );
        }
        for ( auto* item : d->buttons )
        {
            if ( !item->isHidden() )
            {
                visible.append( item );
            }
        }
        const int index = visible.indexOf( button );
        if ( index >= 0 && key->modifiers() == Qt::NoModifier )
        {
            int next = index;
            switch ( key->key() )
            {
                case Qt::Key_Home :
                    next = 0;
                    break;
                case Qt::Key_End :
                    next = visible.size() - 1;
                    break;
                case Qt::Key_Left :
                    next += isRightToLeft() ? 1 : -1;
                    break;
                case Qt::Key_Right :
                    next += isRightToLeft() ? -1 : 1;
                    break;
                default :
                    return QWidget::eventFilter( watched, event );
            }
            visible.at( qBound( 0, next, int( visible.size() ) - 1 ) )->setFocus( Qt::OtherFocusReason );
            return true;
        }
    }
    return QWidget::eventFilter( watched, event );
}
