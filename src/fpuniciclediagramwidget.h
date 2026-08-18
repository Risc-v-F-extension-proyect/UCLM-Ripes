#pragma once

#include <QWidget>

QT_FORWARD_DECLARE_CLASS(QGraphicsScene)
QT_FORWARD_DECLARE_CLASS(QGraphicsView)
QT_FORWARD_DECLARE_CLASS(QResizeEvent)

namespace Ripes {
class FPUnicicleDiagramWidget : public QWidget {
  Q_OBJECT
public:
  explicit FPUnicicleDiagramWidget(QWidget *parent = nullptr);
  void refreshDiagram();
protected:
  void resizeEvent(QResizeEvent *event) override;
private:
  void drawDiagram();
  void fitDiagram();
  QGraphicsScene *m_scene;
  QGraphicsView *m_view;
};
} // namespace Ripes
