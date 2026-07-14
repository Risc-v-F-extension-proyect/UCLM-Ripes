#pragma once

#include <QList>
#include <QString>
#include <QWidget>
#include <vector>

QT_FORWARD_DECLARE_CLASS(QGraphicsScene)
QT_FORWARD_DECLARE_CLASS(QGraphicsView)
QT_FORWARD_DECLARE_CLASS(QResizeEvent)
QT_FORWARD_DECLARE_CLASS(QShowEvent)

namespace Ripes {

class FPUnicicleDiagramWidget : public QWidget {
  Q_OBJECT

public:
  explicit FPUnicicleDiagramWidget(QWidget *parent = nullptr);
  void refreshDiagram();

protected:
  void resizeEvent(QResizeEvent *event) override;
  void showEvent(QShowEvent *event) override;

private:
  struct UnitConfig {
    QString name;
    unsigned latency = 1;
    unsigned count = 1;
    bool segmented = false;
  };

  void drawDiagram();
  void drawStage(const QString &name, const QString &activeText, qreal x,
                 qreal y, qreal w, qreal h);
  void drawFunctionalUnit(const UnitConfig &config, unsigned unitIndex,
                          const std::vector<QString> &activeStages, qreal x,
                          qreal y, qreal cellWidth, qreal cellHeight);
  void drawArrow(qreal x1, qreal y1, qreal x2, qreal y2);
  void fitDiagramToHeight();
  QList<UnitConfig> currentUnitConfig() const;

  QGraphicsScene *m_scene = nullptr;
  QGraphicsView *m_view = nullptr;
};

} // namespace Ripes
