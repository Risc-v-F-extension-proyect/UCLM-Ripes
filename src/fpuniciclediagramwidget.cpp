#include "fpuniciclediagramwidget.h"

#include <QFrame>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QFontMetricsF>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QScrollBar>
#include <QTimer>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <vector>

#include "ripessettings.h"
#include "processorhandler.h"

namespace Ripes {

namespace {
const QColor kBackgroundColor = QColor(255, 255, 255);
const QColor kStageFillColor = QColor(255, 255, 255);
const QColor kLineColor = QColor(35, 35, 35);
const QColor kSeparatorColor = QColor(80, 80, 80);
const QColor kTextColor = QColor(20, 20, 20);
const QColor kActiveFillColor = QColor(142, 224, 224);
const QColor kStageActiveFillColor = QColor(214, 218, 224);
const QColor kAddSubActiveColor = QColor(104, 174, 226);
const QColor kMulActiveColor = QColor(238, 171, 81);
const QColor kDivActiveColor = QColor(180, 144, 220);

QColor unitFillColor(const QString &name) {
  Q_UNUSED(name);
  return kStageFillColor;
}

QColor activeFillColor(const QString &name) {
  if (name == "suma") {
    return kAddSubActiveColor;
  }
  if (name == "mult") {
    return kMulActiveColor;
  }
  if (name == "div") {
    return kDivActiveColor;
  }
  return kActiveFillColor;
}

QString fpStagePrefix(const QString &name, unsigned stage) {
  if (name == "suma") {
    return QString("A%1").arg(stage + 1);
  }
  if (name == "mult") {
    return QString("M%1").arg(stage + 1);
  }
  if (name == "div") {
    return QString("D%1").arg(stage + 1);
  }
  return {};
}

QString instructionTextForAddress(AInt address) {
  if (const auto program = ProcessorHandler::getProgram()) {
    const auto &disassembled = program->getDisassembled();
    if (const auto instruction = disassembled.getFromAddr(address)) {
      return *instruction;
    }
  }
  return QString("0x%1").arg(address, 0, 16);
}

QString elidedDiagramText(const QString &text, qreal availableWidth,
                          qreal scale) {
  const QFontMetricsF metrics{QFont()};
  return metrics.elidedText(text, Qt::ElideRight,
                            static_cast<int>(availableWidth / scale));
}
} // namespace

FPUnicicleDiagramWidget::FPUnicicleDiagramWidget(QWidget *parent)
    : QWidget(parent), m_scene(new QGraphicsScene(this)),
      m_view(new QGraphicsView(m_scene, this)) {
  setWindowTitle("FP unicicle diagram");
  setWindowFlag(Qt::Tool, true);
  setWindowFlag(Qt::WindowMinimizeButtonHint, true);
  setWindowFlag(Qt::WindowCloseButtonHint, true);
  setWindowModality(Qt::ApplicationModal);

  m_scene->setBackgroundBrush(kBackgroundColor);
  m_view->setBackgroundBrush(kBackgroundColor);
  m_view->viewport()->setAutoFillBackground(true);
  m_view->viewport()->setPalette(QPalette(kBackgroundColor));
  m_view->setRenderHint(QPainter::Antialiasing);
  m_view->setDragMode(QGraphicsView::ScrollHandDrag);
  m_view->setFrameShape(QFrame::NoFrame);
  m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_view->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  m_view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_view);

  drawDiagram();
}

void FPUnicicleDiagramWidget::refreshDiagram() {
  const int scrollValue = m_view->horizontalScrollBar()->value();
  drawDiagram();
  m_view->horizontalScrollBar()->setValue(
      std::min(scrollValue, m_view->horizontalScrollBar()->maximum()));
}

void FPUnicicleDiagramWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  fitDiagramToHeight();
}

void FPUnicicleDiagramWidget::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  QTimer::singleShot(0, this, [this] { refreshDiagram(); });
}

void FPUnicicleDiagramWidget::drawDiagram() {
  m_scene->clear();

  constexpr qreal stageW = 132;
  constexpr qreal stageH = 48;
  constexpr qreal stageGap = 70;
  constexpr qreal idToUnitGap = 68;
  constexpr qreal ifX = 30;
  constexpr qreal idX = ifX + stageW + stageGap;
  constexpr qreal unitX = idX + stageW + idToUnitGap;
  constexpr qreal cellW = 132;
  constexpr qreal cellH = stageH;
  constexpr qreal rowGap = 72;

  const QList<UnitConfig> configs = currentUnitConfig();
  std::vector<std::vector<QString>> activeStages;
  qreal maxUnitWidth = 0;
  for (const UnitConfig &config : configs) {
    maxUnitWidth =
        std::max(maxUnitWidth, cellW * static_cast<qreal>(config.latency));
    for (unsigned unit = 0; unit < config.count; ++unit) {
      activeStages.push_back(std::vector<QString>(config.latency));
    }
  }

  if (const auto *processor = ProcessorHandler::getProcessor()) {
    for (const auto &info : processor->fpUnicicleStageInfos()) {
      if (info.valid && info.unit < activeStages.size() &&
          info.stage < activeStages.at(info.unit).size()) {
        activeStages.at(info.unit).at(info.stage) =
            instructionTextForAddress(info.pc);
      }
    }
  }
  const qreal unitCenterX = unitX + maxUnitWidth / 2;

  struct UnitRow {
    qreal leftX = 0;
    qreal centerY = 0;
    qreal rightX = 0;
  };
  std::vector<UnitRow> unitRows;

  qreal unitY = 40;
  qreal firstUnitCenter = -1;
  qreal lastUnitCenter = -1;
  qreal maxUnitRight = unitX;
  unsigned physicalUnit = 0;

  for (const UnitConfig &config : configs) {
    for (unsigned unit = 0; unit < config.count; ++unit) {
      const qreal unitWidth = cellW * static_cast<qreal>(config.latency);
      const qreal rowUnitX = unitCenterX - unitWidth / 2;
      drawFunctionalUnit(config, unit, activeStages.at(physicalUnit), rowUnitX,
                         unitY, cellW, cellH);
      const qreal unitCenter = unitY + cellH / 2;
      if (firstUnitCenter < 0) {
        firstUnitCenter = unitCenter;
      }
      lastUnitCenter = unitCenter;
      const qreal unitRight = rowUnitX + unitWidth;
      maxUnitRight = std::max(maxUnitRight, unitRight);
      unitRows.push_back({rowUnitX, unitCenter, unitRight});
      unitY += rowGap;
      ++physicalUnit;
    }
  }

  const qreal exitY = (firstUnitCenter + lastUnitCenter) / 2;
  const qreal ifY = exitY - stageH / 2;
  const qreal inputAxisX = unitCenterX - maxUnitWidth / 2;
  const qreal outputAxisX = unitCenterX + maxUnitWidth / 2;
  const qreal memX = maxUnitRight + 110;
  const qreal wbX = memX + stageW + stageGap;
  std::array<QString, 5> stageTexts;
  if (const auto *processor = ProcessorHandler::getProcessor()) {
    for (unsigned stage = 0; stage < stageTexts.size(); ++stage) {
      const StageInfo info = processor->stageInfo(StageIndex{0, stage});
      if (info.stage_valid) {
        stageTexts.at(stage) = instructionTextForAddress(info.pc);
      }
    }
  }

  QPen connectorPen(kLineColor, 1.7);
  for (const UnitRow &row : unitRows) {
    if (row.leftX > inputAxisX) {
      m_scene->addLine(idX + stageW, exitY, inputAxisX, row.centerY, connectorPen);
      drawArrow(inputAxisX, row.centerY, row.leftX, row.centerY);
    } else {
      drawArrow(idX + stageW, exitY, row.leftX, row.centerY);
    }

    if (row.rightX < outputAxisX) {
      m_scene->addLine(row.rightX, row.centerY, outputAxisX, row.centerY,
                       connectorPen);
    }
    drawArrow(outputAxisX, row.centerY, memX, exitY);
  }

  drawArrow(ifX + stageW, exitY, idX, exitY);
  drawArrow(memX + stageW, exitY, wbX, exitY);

  drawStage("IF", stageTexts.at(0), ifX, ifY, stageW, stageH);
  drawStage("ID", stageTexts.at(1), idX, ifY, stageW, stageH);
  drawStage("MEM", stageTexts.at(3), memX, ifY, stageW, stageH);
  drawStage("WB", stageTexts.at(4), wbX, ifY, stageW, stageH);

  m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-30, -30, 30, 30));
  fitDiagramToHeight();
}

void FPUnicicleDiagramWidget::drawStage(const QString &name,
                                        const QString &activeText, qreal x,
                                        qreal y, qreal w, qreal h) {
  m_scene->addRect(x, y, w, h, QPen(kLineColor, 1.6),
                   QBrush(activeText.isEmpty() ? kStageFillColor
                                               : kStageActiveFillColor));
  auto *text = m_scene->addText(name);
  text->setDefaultTextColor(kTextColor);
  text->setPos(x + w / 2 - text->boundingRect().width() / 2,
               activeText.isEmpty()
                   ? y + h / 2 - text->boundingRect().height() / 2
                   : y + 5);
  if (!activeText.isEmpty()) {
    auto *pcText = m_scene->addText(elidedDiagramText(activeText, w - 10, 0.72));
    pcText->setDefaultTextColor(kTextColor);
    QFont activeFont = pcText->font();
    activeFont.setBold(true);
    pcText->setFont(activeFont);
    pcText->setScale(0.72);
    const QRectF bounds = pcText->boundingRect();
    pcText->setPos(x + w / 2 - bounds.width() * 0.72 / 2,
                   y + h - bounds.height() * 0.72 - 4);
  }
}

void FPUnicicleDiagramWidget::drawFunctionalUnit(const UnitConfig &config,
                                                 unsigned unitIndex,
                                                 const std::vector<QString> &activeStages,
                                                 qreal x,
                                                 qreal y, qreal cellWidth,
                                                 qreal cellHeight) {
  const qreal width = cellWidth * static_cast<qreal>(config.latency);
  QString title;
  if (config.name == "ALU") {
    title = "Integer ALU";
  } else if (config.name == "suma") {
    title = QString("FP add/sub unit %1").arg(unitIndex + 1);
  } else if (config.name == "mult") {
    title = QString("FP multiply unit %1").arg(unitIndex + 1);
  } else if (config.name == "div") {
    title = QString("FP divide unit %1").arg(unitIndex + 1);
  }

  auto *label = m_scene->addText(title);
  label->setDefaultTextColor(kTextColor);
  label->setPos(x + width / 2 - label->boundingRect().width() / 2, y - 26);

  m_scene->addRect(x, y, width, cellHeight, QPen(kLineColor, 1.5),
                   QBrush(unitFillColor(config.name)));

  for (unsigned stage = 0; stage < activeStages.size(); ++stage) {
    if (activeStages.at(stage).isEmpty()) {
      continue;
    }
    const qreal sx = x + cellWidth * static_cast<qreal>(stage);
    m_scene->addRect(sx, y, cellWidth, cellHeight, Qt::NoPen,
                     QBrush(activeFillColor(config.name)));
    const QString prefix = fpStagePrefix(config.name, stage);
    if (!prefix.isEmpty()) {
      auto *prefixText = m_scene->addText(prefix);
      prefixText->setDefaultTextColor(kTextColor);
      prefixText->setPos(sx + cellWidth / 2 -
                             prefixText->boundingRect().width() / 2,
                         y + 5);
    }

    auto *stageText =
        m_scene->addText(elidedDiagramText(activeStages.at(stage),
                                           cellWidth - 10, 0.72));
    stageText->setDefaultTextColor(kTextColor);
    QFont activeFont = stageText->font();
    activeFont.setBold(true);
    stageText->setFont(activeFont);
    stageText->setScale(0.72);
    const QRectF bounds = stageText->boundingRect();
    stageText->setPos(sx + cellWidth / 2 - bounds.width() * 0.72 / 2,
                      y + cellHeight - bounds.height() * 0.72 - 4);
  }

  for (unsigned stage = 1; stage < config.latency; ++stage) {
    QPen pen(kSeparatorColor, 1.25);
    if (!config.segmented) {
      pen.setStyle(Qt::DashLine);
    }
    const qreal sx = x + cellWidth * static_cast<qreal>(stage);
    m_scene->addLine(sx, y, sx, y + cellHeight, pen);
  }
  m_scene->addRect(x, y, width, cellHeight, QPen(kLineColor, 1.5),
                   Qt::NoBrush);
}

void FPUnicicleDiagramWidget::drawArrow(qreal x1, qreal y1, qreal x2,
                                        qreal y2) {
  QPen pen(kLineColor, 1.7);
  m_scene->addLine(x1, y1, x2, y2, pen);
}

void FPUnicicleDiagramWidget::fitDiagramToHeight() {
  m_view->resetTransform();

  const QRectF sceneRect = m_scene->sceneRect();
  if (sceneRect.isEmpty() || m_view->viewport()->height() <= 0) {
    return;
  }

  const qreal availableHeight =
      std::max<qreal>(1.0, static_cast<qreal>(m_view->viewport()->height() - 8));
  const qreal scale = std::min<qreal>(1.0, availableHeight / sceneRect.height());
  m_view->scale(scale, scale);
}

QList<FPUnicicleDiagramWidget::UnitConfig>
FPUnicicleDiagramWidget::currentUnitConfig() const {
  const auto addSubLatency =
      RipesSettings::value(RIPES_SETTING_RV5S_FALU_ADDSUB_LATENCY).toUInt();
  const auto mulLatency =
      RipesSettings::value(RIPES_SETTING_RV5S_FALU_MUL_LATENCY).toUInt();
  const auto divLatency =
      RipesSettings::value(RIPES_SETTING_RV5S_FALU_DIV_LATENCY).toUInt();

  const bool addSubSegmented =
      RipesSettings::value(RIPES_SETTING_RV5S_FALU_ADDSUB_PIPELINED).toBool();
  const bool mulSegmented =
      RipesSettings::value(RIPES_SETTING_RV5S_FALU_MUL_PIPELINED).toBool();

  const unsigned addSubCount =
      addSubSegmented
          ? 1
          : std::min(3u, std::max(1u, RipesSettings::value(
                                           RIPES_SETTING_RV5S_FALU_ADDSUB_COUNT)
                                           .toUInt()));
  const unsigned mulCount =
      mulSegmented
          ? 1
          : std::min(3u, std::max(1u, RipesSettings::value(
                                           RIPES_SETTING_RV5S_FALU_MUL_COUNT)
                                           .toUInt()));
  const unsigned divCount =
      std::min(3u, std::max(1u, RipesSettings::value(
                                     RIPES_SETTING_RV5S_FALU_DIV_COUNT)
                                     .toUInt()));

  return {
      {"ALU", 1, 1, true},
      {"suma", std::min(5u, std::max(2u, addSubLatency)), addSubCount,
       addSubSegmented},
      {"mult", std::min(10u, std::max(3u, mulLatency)), mulCount,
       mulSegmented},
      {"div", std::min(25u, std::max(4u, divLatency)), divCount, false}};
}

} // namespace Ripes
