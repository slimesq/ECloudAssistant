#ifndef CUSTOMWGT_H
#define CUSTOMWGT_H
#include <QLabel>
#include <vector>
#include <QWidget>

class CustomWgt : public QWidget
{
    Q_OBJECT
public:
    explicit CustomWgt(QWidget* parent = nullptr);
    ~CustomWgt();
    void setHightLight(bool flag = true);
    void setImageAndText(QString const& text,
                         QString const& normal,
                         QString const& hightlight,
                         bool ishigtlight = false);

protected:
    void Init();
    void setLableTxt(QString const& text);
    void setPicture(QString const& imagepath);

private:
    QLabel* name_;
    QLabel* image_;
    std::vector<QString> imagePath_;  //[0] 存放非高亮图片路径 [1]高亮
};

#endif  // CUSTOMWGT_H
