#include "fpuniciclediagramwidget.h"

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QFrame>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <array>
#include <algorithm>
#include <vector>

#include "processorhandler.h"
#include "ripessettings.h"

namespace Ripes {
namespace {
struct Unit {
  QString name;
  unsigned latency;
  unsigned count;
  bool segmented;
  QColor color;
};

QString instructionAt(AInt address) {
  if (const auto program = ProcessorHandler::getProgram())
    if (const auto instruction = program->getDisassembled().getFromAddr(address))
      return *instruction;
  return QString("0x%1").arg(address, 0, 16);
}
} // namespace

FPUnicicleDiagramWidget::FPUnicicleDiagramWidget(QWidget *parent)
    : QWidget(parent), m_scene(new QGraphicsScene(this)),
      m_view(new QGraphicsView(m_scene, this)) {
  setWindowTitle("FP unicicle diagram");
  setWindowFlag(Qt::Window, true);
  resize(1200, 720);
  m_scene->setBackgroundBrush(Qt::white);
  m_view->setBackgroundBrush(Qt::white);
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
}

void FPUnicicleDiagramWidget::refreshDiagram() { drawDiagram(); }
void FPUnicicleDiagramWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  fitDiagram();
}

void FPUnicicleDiagramWidget::drawDiagram() {
  m_scene->clear();
  const auto number = [](const char *key) {
    return std::max(1u, RipesSettings::value(key).toUInt());
  };
  const auto flag = [](const char *key) {
    return RipesSettings::value(key).toBool();
  };
  const bool addSubSegmented =
      flag(RIPES_SETTING_RV5S_FALU_ADDSUB_PIPELINED);
  const bool multiplySegmented =
      flag(RIPES_SETTING_RV5S_FALU_MUL_PIPELINED);
  const bool divideSegmented =
      flag(RIPES_SETTING_RV5S_FALU_DIV_PIPELINED);
  const std::array<Unit, 4> units{{
      {"Integer ALU", 1, 1, true, QColor(205, 210, 216)},
      {"FP add/sub",
       std::clamp(number(RIPES_SETTING_RV5S_FALU_ADDSUB_LATENCY), 1u, 4u),
       addSubSegmented
           ? 1u
           : std::clamp(number(RIPES_SETTING_RV5S_FALU_ADDSUB_COUNT), 1u,
                        3u),
       addSubSegmented, QColor(144, 238, 144)},
      {"FP multiply",
       std::clamp(number(RIPES_SETTING_RV5S_FALU_MUL_LATENCY), 1u, 7u),
       multiplySegmented
           ? 1u
           : std::clamp(number(RIPES_SETTING_RV5S_FALU_MUL_COUNT), 1u, 3u),
       multiplySegmented, QColor(95, 211, 218)},
      {"FP divide",
       std::clamp(number(RIPES_SETTING_RV5S_FALU_DIV_LATENCY), 1u, 25u),
       divideSegmented
           ? 1u
           : std::clamp(number(RIPES_SETTING_RV5S_FALU_DIV_COUNT), 1u, 3u),
       divideSegmented, QColor(238, 166, 76)},
  }};
  std::vector<FPUnicicleStageInfo> activeStages;
  if (const auto *processor = ProcessorHandler::getProcessor()) {
    activeStages = processor->fpUnicicleStageInfos();
  }

  constexpr qreal cellW = 96;
  constexpr qreal cellH = 58;
  constexpr qreal minimumCellGap = 12;
  constexpr qreal rowSpacing = 98;
  constexpr qreal unitX = 360;
  constexpr qreal firstRowY = 70;
  const unsigned maximumLatency =
      std::max_element(units.begin(), units.end(),
                       [](const Unit &left, const Unit &right) {
                         return left.latency < right.latency;
                       })
          ->latency;
  const qreal corridorWidth =
      maximumLatency * cellW + (maximumLatency - 1) * minimumCellGap;
  const qreal unitEndX = unitX + corridorWidth;

  struct UnitLayout {
    unsigned type;
    unsigned copy;
    qreal y;
    qreal gap;
    qreal firstX;
    qreal lastX;
  };
  std::vector<UnitLayout> layouts;
  for (unsigned type = 0; type < units.size(); ++type) {
    const auto &unit = units.at(type);
    for (unsigned copy = 0; copy < unit.count; ++copy) {
      const qreal y = firstRowY + layouts.size() * rowSpacing;
      if (unit.latency == 1) {
        const qreal centeredX = unitX + (corridorWidth - cellW) / 2;
        layouts.push_back({type, copy, y, 0, centeredX, centeredX});
      } else if (!unit.segmented) {
        const qreal unitWidth = unit.latency * cellW;
        const qreal firstX = unitX + (corridorWidth - unitWidth) / 2;
        layouts.push_back(
            {type, copy, y, 0, firstX,
             firstX + (unit.latency - 1) * cellW});
      } else {
        const qreal gap =
            (corridorWidth - unit.latency * cellW) / (unit.latency - 1);
        layouts.push_back(
            {type, copy, y, gap, unitX, unitEndX - cellW});
      }
    }
  }

  const qreal firstCenter = layouts.front().y + cellH / 2;
  const qreal lastCenter = layouts.back().y + cellH / 2;
  const qreal axis = (firstCenter + lastCenter) / 2;
  constexpr qreal pipelineW = 112;
  constexpr qreal pipelineH = 58;
  constexpr qreal ifX = 20;
  constexpr qreal idX = 172;
  const qreal memX = unitEndX + 92;
  const qreal wbX = memX + 152;
  const qreal pipelineY = axis - pipelineH / 2;

  std::array<QString, 5> pipelineText;
  std::array<StageInfo, 5> pipelineInfo;
  if (const auto *processor = ProcessorHandler::getProcessor()) {
    for (unsigned stage = 0; stage < pipelineText.size(); ++stage) {
      const auto info = processor->stageInfo(StageIndex{0, stage});
      pipelineInfo.at(stage) = info;
      if (info.stage_valid)
        pipelineText.at(stage) = instructionAt(info.pc);
    }
  }

  QPen connectorPen(QColor(75, 82, 92), 2.0);
  connectorPen.setCapStyle(Qt::RoundCap);
  connectorPen.setJoinStyle(Qt::RoundJoin);
  const auto line = [this, connectorPen](qreal x1, qreal y1, qreal x2,
                                         qreal y2) {
    m_scene->addLine(x1, y1, x2, y2, connectorPen);
  };

  const qreal splitX = idX + pipelineW + 34;
  const qreal mergeX = memX - 34;
  const qreal inputDiagonalEndX = unitX - 18;
  const qreal outputDiagonalStartX = unitEndX + 18;
  line(ifX + pipelineW, axis, idX, axis);
  line(idX + pipelineW, axis, splitX, axis);
  for (const auto &layout : layouts) {
    const qreal centerY = layout.y + cellH / 2;
    line(splitX, axis, inputDiagonalEndX, centerY);
    line(inputDiagonalEndX, centerY, layout.firstX, centerY);

    const qreal lastRight = layout.lastX + cellW;
    line(lastRight, centerY, outputDiagonalStartX, centerY);
    line(outputDiagonalStartX, centerY, mergeX, axis);
  }
  line(mergeX, axis, memX, axis);
  line(memX + pipelineW, axis, wbX, axis);

  QPen cardPen(QColor(48, 54, 62), 1.6);
  const auto centeredText = [this](const QString &text, qreal x, qreal y,
                                   qreal width, qreal scale, bool bold) {
    auto *item = m_scene->addText(text);
    QFont font = item->font();
    font.setBold(bold);
    item->setFont(font);
    item->setDefaultTextColor(Qt::black);
    item->setScale(scale);
    const qreal textWidth = item->boundingRect().width() * scale;
    item->setPos(x + (width - textWidth) / 2, y);
  };

  const auto stageCard = [this, cardPen, centeredText](
                             const QString &stageName,
                             const QString &instruction, const QColor &color,
                             qreal x, qreal y, qreal width, qreal height) {
    const bool occupied = !instruction.isEmpty();
    QPainterPath path;
    path.addRoundedRect(QRectF(x, y, width, height), 6, 6);
    m_scene->addPath(path, cardPen,
                     occupied ? QBrush(color) : QBrush(QColor(250, 250, 250)));
    if (!occupied)
      return;

    centeredText(stageName, x, y + 1, width, .98, true);
    QFontMetricsF metrics{QFont()};
    centeredText(metrics.elidedText(instruction, Qt::ElideRight,
                                    width / .76 - 10),
                 x, y + 27, width, .76, true);
  };

  const auto inactiveStageName = [this](const QString &stageName,
                                        const QColor &color, qreal x,
                                        qreal y, qreal width) {
    auto *item = m_scene->addText(stageName);
    QFont font = item->font();
    font.setBold(true);
    item->setFont(font);
    item->setScale(.88);
    item->setDefaultTextColor(color.darker(175));
    const qreal textWidth = item->boundingRect().width() * item->scale();
    item->setPos(x + (width - textWidth) / 2, y + 15);
  };

  constexpr qreal legendX = 20;
  constexpr qreal legendY = 18;
  constexpr qreal legendW = 292;
  constexpr qreal legendRowH = 27;
  const qreal legendH = 34 + units.size() * legendRowH;
  QPainterPath legendPanel;
  legendPanel.addRoundedRect(QRectF(legendX, legendY, legendW, legendH), 8, 8);
  m_scene->addPath(legendPanel, QPen(QColor(175, 180, 188), 1.2),
                   QBrush(QColor(248, 249, 251)));
  auto *legendTitle = m_scene->addText("FUNCTIONAL UNITS");
  QFont legendTitleFont = legendTitle->font();
  legendTitleFont.setBold(true);
  legendTitle->setFont(legendTitleFont);
  legendTitle->setDefaultTextColor(Qt::black);
  legendTitle->setPos(legendX + 12, legendY + 4);

  for (unsigned type = 0; type < units.size(); ++type) {
    const auto &unit = units.at(type);
    const qreal rowY = legendY + 33 + type * legendRowH;
    QPainterPath colorMarker;
    colorMarker.addRoundedRect(QRectF(legendX + 12, rowY + 4, 16, 16), 3, 3);
    m_scene->addPath(colorMarker, QPen(unit.color.darker(160), 1),
                     QBrush(unit.color));

    const QString cycleText =
        unit.latency == 1 ? "1 cycle"
                          : QString("%1 cycles").arg(unit.latency);
    const QString unitText =
        unit.count == 1 ? "1 unit" : QString("%1 units").arg(unit.count);
    auto *entry = m_scene->addText(
        QString("%1   %2 · %3").arg(unit.name, cycleText, unitText));
    QFont entryFont = entry->font();
    entryFont.setBold(true);
    entry->setFont(entryFont);
    entry->setScale(.78);
    entry->setDefaultTextColor(Qt::black);
    entry->setPos(legendX + 36, rowY);
  }

  stageCard("IF", pipelineText.at(0), QColor(205, 210, 216), ifX,
            pipelineY, pipelineW, pipelineH);
  stageCard("ID", pipelineText.at(1), QColor(205, 210, 216), idX,
            pipelineY, pipelineW, pipelineH);
  stageCard("MEM", pipelineText.at(3), QColor(205, 210, 216), memX,
            pipelineY, pipelineW, pipelineH);
  stageCard("WB", pipelineText.at(4), QColor(205, 210, 216), wbX,
            pipelineY, pipelineW, pipelineH);

  const bool fpInClassicEX =
      pipelineInfo.at(2).stage_valid &&
      std::any_of(activeStages.begin(), activeStages.end(),
                  [&pipelineInfo](const FPUnicicleStageInfo &info) {
                    return info.valid && info.pc == pipelineInfo.at(2).pc;
                  });

  for (const auto &layout : layouts) {
    const unsigned type = layout.type;
    const unsigned copy = layout.copy;
    const auto &unit = units.at(type);
    const std::array<QString, 4> shortNames{
        "INTEGER ALU", "FP ADD/SUB", "FP MULTIPLY", "FP DIVIDE"};
    QString titleText = shortNames.at(type);
    if (unit.count > 1)
      titleText += QString(" · UNIT %1").arg(copy + 1);
    auto *title = m_scene->addText(titleText);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setDefaultTextColor(unit.color.darker(180));
    title->setPos(unitX, layout.y - 27);

    if (!unit.segmented) {
      QPainterPath background;
      background.addRoundedRect(
          QRectF(layout.firstX, layout.y, unit.latency * cellW, cellH), 6, 6);
      m_scene->addPath(background, Qt::NoPen, QBrush(QColor(250, 250, 250)));
    }

    for (unsigned stage = 0; stage < unit.latency; ++stage) {
      const qreal x = layout.firstX + stage * (cellW + layout.gap);
      QString activeInstruction;
      if (type == 0) {
        if (stage == 0 && pipelineInfo.at(2).stage_valid && !fpInClassicEX)
          activeInstruction = pipelineText.at(2);
      } else {
        const auto active = std::find_if(
            activeStages.begin(), activeStages.end(),
            [type, copy, stage](const FPUnicicleStageInfo &info) {
              return info.valid && info.unit == type &&
                     info.instance == copy && info.stage == stage;
            });
        if (active != activeStages.end())
          activeInstruction = instructionAt(active->pc);
      }

      const QString stageName =
          type == 0
              ? QString("EX")
              : QString("%1%2")
                    .arg(type == 1 ? QChar('A')
                                   : type == 2 ? QChar('M') : QChar('D'))
                    .arg(stage + 1);
      if (unit.segmented) {
        stageCard(stageName, activeInstruction, unit.color, x, layout.y,
                  cellW, cellH);
        if (type != 0 && activeInstruction.isEmpty())
          inactiveStageName(stageName, unit.color, x, layout.y, cellW);
        if (stage + 1 < unit.latency) {
          const qreal nextX =
              layout.firstX + (stage + 1) * (cellW + layout.gap);
          QPen internalPen(unit.color.darker(150), 1.4, Qt::SolidLine);
          m_scene->addLine(x + cellW, layout.y + cellH / 2, nextX,
                           layout.y + cellH / 2, internalPen);
        }
      } else {
        if (!activeInstruction.isEmpty()) {
          m_scene->addRect(x + 2, layout.y + 2, cellW - 4, cellH - 4,
                           Qt::NoPen, QBrush(unit.color));
          centeredText(stageName, x, layout.y + 1, cellW, .98, true);
          QFontMetricsF metrics{QFont()};
          centeredText(metrics.elidedText(activeInstruction, Qt::ElideRight,
                                          cellW / .76 - 10),
                       x, layout.y + 27, cellW, .76, true);
        } else if (type != 0) {
          inactiveStageName(stageName, unit.color, x, layout.y, cellW);
        }
        if (stage + 1 < unit.latency) {
          QPen separatorPen(unit.color.darker(170), 1.5, Qt::DashLine);
          m_scene->addLine(x + cellW, layout.y + 5, x + cellW,
                           layout.y + cellH - 5, separatorPen);
        }
      }
    }

    if (!unit.segmented) {
      QPainterPath border;
      border.addRoundedRect(
          QRectF(layout.firstX, layout.y, unit.latency * cellW, cellH), 6, 6);
      m_scene->addPath(border, QPen(unit.color.darker(170), 2.0),
                       Qt::NoBrush);
    }
  }

  m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-20, -20, 20, 20));
  fitDiagram();
}

void FPUnicicleDiagramWidget::fitDiagram() {
  m_view->resetTransform();
  const QRectF rect = m_scene->sceneRect();
  if (rect.isEmpty() || m_view->viewport()->height() <= 0)
    return;
  const qreal scale = std::min<qreal>(
      1.0, std::max<qreal>(1.0, m_view->viewport()->height() - 8) /
               rect.height());
  m_view->scale(scale, scale);
}
} // namespace Ripes
