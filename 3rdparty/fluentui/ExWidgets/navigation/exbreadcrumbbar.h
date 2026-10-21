#pragma once

#include "exwidgets_global.h"
#include "exwidgetsmacros.h"

#include <QScopedPointer>
#include <QVariant>
#include <QWidget>

class QAbstractItemDelegate;
class ExBreadcrumbBarPrivate;

// 路径由调用方维护，点击只发送原始索引，不自动截断路径。
class EXWIDGETS_EXPORT ExBreadcrumbBar final : public QWidget
{
    Q_OBJECT

public:
    EXWIDGETS_DECLARE_PROPERTY_D_NOTIFY( QVariantList, itemsSource, itemsSource, setItemsSource, itemsSourceChanged )
    EXWIDGETS_DECLARE_PROPERTY_D_NOTIFY( QAbstractItemDelegate*, itemTemplate, itemTemplate, setItemTemplate, itemTemplateChanged )

    void setItems( const QStringList& items );

    explicit ExBreadcrumbBar( QWidget* parent = nullptr );
    ~ExBreadcrumbBar() override;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    // 对应 WinUI ItemClickedEventArgs.Index / Item。
    void itemClicked( int index, const QVariant& item );

protected:
    void resizeEvent( QResizeEvent* event ) override;
    void changeEvent( QEvent* event ) override;
    void paintEvent( QPaintEvent* event ) override;
    bool eventFilter( QObject* watched, QEvent* event ) override;

private:
    QScopedPointer<ExBreadcrumbBarPrivate> d_ptr;
    Q_DECLARE_PRIVATE( ExBreadcrumbBar )
};

