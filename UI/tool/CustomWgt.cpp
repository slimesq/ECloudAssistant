#include "CustomWgt.h"
#include <QHBoxLayout>
#include <QFont>

CustomWgt::CustomWgt(QWidget* parent) : QWidget{parent}, imagePath_(2, "")
{
    setWindowFlag(Qt::FramelessWindowHint);
    setFixedSize(180, 30);
    Init();
}

CustomWgt::~CustomWgt()
{
    imagePath_.clear();
}

void CustomWgt::setHightLight(bool flag)
{
    QString const str = flag ? imagePath_[1] : imagePath_[0];
    setPicture(str);
}

void CustomWgt::setImageAndText(QString const& text,
                                QString const& normal,
                                QString const& hightlight,
                                bool ishigtlight)
{
    if (normal.isEmpty() || hightlight.isEmpty())
    {
        return;
    }
    imagePath_[0] = normal;
    imagePath_[1] = hightlight;
    QString const str = ishigtlight ? hightlight : normal;
    setPicture(str);
    setLableTxt(text);
}

void CustomWgt::Init()
{
    name_ = new QLabel(this);
    image_ = new QLabel(this);
    image_->setFixedSize(20, 20);
    name_->setFixedSize(100, 20);

    image_->setStyleSheet("background-color:transparent;border:none;");
    name_->setStyleSheet("background-color:transparent;border:none;");

    QHBoxLayout* layout = new QHBoxLayout(this);

    layout->addWidget(image_);
    layout->addWidget(name_);
    setLayout(layout);
}

void CustomWgt::setLableTxt(QString const& text)
{
    name_->setText(text);
    QFont font("Microsoft Yahei", 11);
    name_->setFont(font);
    name_->setStyleSheet("color:#FFFFFF");
}

void CustomWgt::setPicture(QString const& imagepath)
{
    QPixmap pixmap;
    if (pixmap.load(imagepath))
    {
        image_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        image_->setPixmap(
            pixmap.scaled(image_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}
