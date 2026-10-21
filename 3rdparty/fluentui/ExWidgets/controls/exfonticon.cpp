#include "exfonticon.h"

#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QFontDatabase>
#include <QPainter>
#include <QPalette>

class ExFontIconPrivate
{
public:
    explicit ExFontIconPrivate( ExFontIcon* q )
        : q_ptr( q )
    {
    }

    void init();
    bool loadFont( const QString& path );

    ExFontIcon* q_ptr{ nullptr };
};

void ExFontIconPrivate::init()
{
    QFontDatabase fontDB;
    QStringList systemFamilies = fontDB.families();
    // Win11 系统默认包含 Segoe Fluent Icons，若无则尝试加载应用内置资源
    if ( !systemFamilies.contains( QLatin1String( SegoeIcon::SegoeFontName ) ) )
    {
        loadFont( QStringLiteral( ":/resource/Segoe Fluent Icons.ttf" ) );
    }
}

bool ExFontIconPrivate::loadFont( const QString& path )
{
    int id = QFontDatabase::addApplicationFont( path );
    if ( -1 == id )
    {
        qDebug() << "[ExFontIcon] Failed to load font:" << path;
        return false;
    }

    const QStringList names = QFontDatabase::applicationFontFamilies( id );
    return !names.isEmpty();
}

ExFontIcon::ExFontIcon()
    : d_ptr( new ExFontIconPrivate( this ) )
{
    Q_D( ExFontIcon );
    d->init();
}

ExFontIcon::~ExFontIcon() = default;

ExFontIcon* ExFontIcon::instance()
{
    static ExFontIcon _instance;
    return &_instance;
}

bool ExFontIcon::loadFont( const QString& path )
{
    Q_D( ExFontIcon );
    return d->loadFont( path );
}

QIcon ExFontIcon::getIcon( SegoeIcon::Type icon, const QString& family )
{
    return getIcon( static_cast<int>( icon ), 25, 30, 30, QColor(), family );
}

QIcon ExFontIcon::getIcon( SegoeIcon::Type icon, const QColor& iconColor, const QString& family )
{
    return getIcon( static_cast<int>( icon ), 25, 30, 30, iconColor, family );
}

QIcon ExFontIcon::getIcon( SegoeIcon::Type icon, int pixelSize, const QString& family )
{
    return getIcon( static_cast<int>( icon ), pixelSize, pixelSize, pixelSize, QColor(), family );
}

QIcon ExFontIcon::getIcon( SegoeIcon::Type icon, int pixelSize, const QColor& iconColor, const QString& family )
{
    return getIcon( static_cast<int>( icon ), pixelSize, pixelSize, pixelSize, iconColor, family );
}

QIcon ExFontIcon::getIcon( SegoeIcon::Type icon, int pixelSize, int fixedWidth, int fixedHeight, const QString& family )
{
    return getIcon( static_cast<int>( icon ), pixelSize, fixedWidth, fixedHeight, QColor(), family );
}

QIcon ExFontIcon::getIcon( SegoeIcon::Type icon, int pixelSize, int fixedWidth, int fixedHeight, const QColor& iconColor, const QString& family )
{
    return getIcon( static_cast<int>( icon ), pixelSize, fixedWidth, fixedHeight, iconColor, family );
}

QIcon ExFontIcon::getIcon( int unicode, const QString& family )
{
    return getIcon( unicode, 25, 30, 30, QColor(), family );
}

QIcon ExFontIcon::getIcon( int unicode, const QColor& iconColor, const QString& family )
{
    return getIcon( unicode, 25, 30, 30, iconColor, family );
}

QIcon ExFontIcon::getIcon( int unicode, int pixelSize, const QString& family )
{
    return getIcon( unicode, pixelSize, pixelSize, pixelSize, QColor(), family );
}

QIcon ExFontIcon::getIcon( int unicode, int pixelSize, const QColor& iconColor, const QString& family )
{
    return getIcon( unicode, pixelSize, pixelSize, pixelSize, iconColor, family );
}

QIcon ExFontIcon::getIcon( int unicode, int pixelSize, int fixedWidth, int fixedHeight, const QString& family )
{
    return getIcon( unicode, pixelSize, fixedWidth, fixedHeight, QColor(), family );
}

QIcon ExFontIcon::getIcon( int unicode, int pixelSize, int fixedWidth, int fixedHeight, const QColor& iconColor, const QString& family )
{
    QFont iconFont( family );
    iconFont.setPixelSize( pixelSize );
    iconFont.setWeight( QFont::Normal );
    iconFont.setStyleStrategy( QFont::NoFontMerging );

    QPixmap pix( fixedWidth, fixedHeight );
    pix.fill( Qt::transparent );

    QPainter painter;
    painter.begin( &pix );
    painter.setRenderHints( QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform );
    painter.setFont( iconFont );

    QColor penColor = iconColor.isValid()
                          ? iconColor
                          : ( qApp ? qApp->palette().color( QPalette::Active, QPalette::WindowText ) : QColor( Qt::black ) );
    painter.setPen( penColor );
    painter.drawText( pix.rect(), Qt::AlignCenter, QChar( static_cast<ushort>( unicode ) ) );
    painter.end();

    return QIcon( pix );
}

QChar ExFontIcon::iconChar( SegoeIcon::Type icon )
{
    return QChar( static_cast<ushort>( icon ) );
}

QChar ExFontIcon::iconChar( int unicode )
{
    return QChar( static_cast<ushort>( unicode ) );
}

QString ExFontIcon::iconString( SegoeIcon::Type icon )
{
    return QString( QChar( static_cast<ushort>( icon ) ) );
}

QString ExFontIcon::iconString( int unicode )
{
    return QString( QChar( static_cast<ushort>( unicode ) ) );
}

QFont ExFontIcon::iconFont( int pixelSize, const QString& family )
{
    QFont font( family );
    font.setPixelSize( pixelSize );
    font.setStyleStrategy( QFont::NoFontMerging );
    return font;
}
