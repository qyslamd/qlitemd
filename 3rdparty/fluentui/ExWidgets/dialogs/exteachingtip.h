#pragma once

#include "exwidgets_global.h"
#include "exwidgetsmacros.h"
#include <QIcon>
#include <QMargins>
#include <QPointer>
#include <QScopedPointer>
#include <QWidget>

class QPushButton;
class ExTeachingTipPrivate;

// 窗口内的非模态引导浮层，不创建顶层窗口、不抢占显示时的焦点。
// showAt() 会将浮层交给目标所在窗口持有；应使用 new 创建，不加入布局。
class EXWIDGETS_EXPORT ExTeachingTip final : public QWidget
{
    Q_OBJECT

public:
    enum Placement { Auto, Top, Bottom, Left, Right, TopRight, TopLeft, BottomRight, BottomLeft,
                     LeftTop, LeftBottom, RightTop, RightBottom, Center };
    Q_ENUM( Placement )
    enum class TailVisibility { Auto, Visible, Collapsed };
    Q_ENUM( TailVisibility )
    enum class CloseReason { CloseButton, LightDismiss, Programmatic };
    Q_ENUM( CloseReason )
    enum class HeroContentPlacement { Auto, Top, Bottom };
    Q_ENUM( HeroContentPlacement )

    EXWIDGETS_DECLARE_PROPERTY_D( QString, title, title, setTitle )
    EXWIDGETS_DECLARE_PROPERTY_D( QString, subtitle, subtitle, setSubtitle )
    EXWIDGETS_DECLARE_PROPERTY_D( QString, actionButtonContent, actionButtonContent, setActionButtonContent )
    // 默认使用强调色；仅改变操作按钮外观，不设置默认按钮或移动焦点。
    EXWIDGETS_DECLARE_PROPERTY_D( bool, actionButtonAccent, actionButtonAccent, setActionButtonAccent )
    EXWIDGETS_DECLARE_PROPERTY_D( QString, closeButtonContent, closeButtonContent, setCloseButtonContent )
    EXWIDGETS_DECLARE_PROPERTY_D( QWidget*, target, target, setTarget )
    EXWIDGETS_DECLARE_PROPERTY_D( QWidget*, content, content, setContent )
    EXWIDGETS_DECLARE_PROPERTY_D( QWidget*, heroContent, heroContent, setHeroContent )
    EXWIDGETS_DECLARE_PROPERTY_D( QIcon, iconSource, iconSource, setIconSource )
    EXWIDGETS_DECLARE_PROPERTY_D( TailVisibility, tailVisibility, tailVisibility, setTailVisibility )
    EXWIDGETS_DECLARE_PROPERTY_D( QMargins, placementMargin, placementMargin, setPlacementMargin )
    EXWIDGETS_DECLARE_PROPERTY_D( HeroContentPlacement, heroContentPlacement, heroContentPlacement, setHeroContentPlacement )
    EXWIDGETS_DECLARE_PROPERTY_D( Placement, preferredPlacement, preferredPlacement, setPreferredPlacement )
    EXWIDGETS_DECLARE_PROPERTY_D( bool, isLightDismissEnabled, isLightDismissEnabled, setIsLightDismissEnabled )
    // 默认启用打开动画；关闭开关时立即结束正在播放的动画。
    EXWIDGETS_DECLARE_PROPERTY_D( bool, animationEnabled, isAnimationEnabled, setAnimationEnabled )
    Q_PROPERTY( bool isOpen READ isOpen WRITE setIsOpen NOTIFY isOpenChanged )

    explicit ExTeachingTip( QWidget* parent = nullptr );
    ~ExTeachingTip() override;

    // Qt 中直接配置按钮的图标、样式和信号，不移植 XAML Style/ICommand。
    QPushButton* actionButton() const;
    QPushButton* closeButton() const;
    bool isOpen() const;
    void showAt( QWidget* target );
    void setVisible( bool visible ) override;

public Q_SLOTS:
    void setIsOpen( bool open );
    void dismiss();

Q_SIGNALS:
    void isOpenChanged( bool open );
    void opened();
    // cancel 只在本次同步信号调用期间有效，必须使用同线程直接连接。
    // 宿主/目标失效等强制关闭不允许取消。
    void closing( ExTeachingTip::CloseReason reason, bool* cancel );
    void closed( ExTeachingTip::CloseReason reason );
    // 操作按钮不自动关闭，调用方可更新内容或显式 dismiss()。
    void actionButtonClick();
    void closeButtonClick();

protected:
    bool event( QEvent* event ) override;
    bool eventFilter( QObject* watched, QEvent* event ) override;
    void paintEvent( QPaintEvent* event ) override;
    void showEvent( QShowEvent* event ) override;
    void hideEvent( QHideEvent* event ) override;
    void closeEvent( QCloseEvent* event ) override;

private:
    Q_DECLARE_PRIVATE( ExTeachingTip )
    QScopedPointer<ExTeachingTipPrivate> d_ptr;

    void schedulePlacement();
    bool updatePlacement();
    QRect visibleTargetRect() const;
    void requestClose( CloseReason reason );
    void forceClose();
    void startOpenAnimation();
    void stopOpenAnimation();
    void updateButtons();
    void replaceContent( QPointer<QWidget>& current, QWidget* replacement, QWidget* container );
};
